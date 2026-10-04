[CmdletBinding()]
param(
    [string]$BuildDirectory = (Join-Path $PSScriptRoot '../../build/windows-cef'),
    [string]$EvidenceDirectory = (Join-Path $PSScriptRoot '../../build/cef-profiles')
)
$ErrorActionPreference='Stop'
function Get-FixtureStorageDiagnostic([string]$FixtureRoot) {
    $diagnosticProcess=$null
    try {
        $python=(Get-Command python -ErrorAction Stop).Source
        $diagnosticStart=[Diagnostics.ProcessStartInfo]::new()
        $diagnosticStart.FileName=$python;$diagnosticStart.UseShellExecute=$false
        $diagnosticStart.CreateNoWindow=$true
        $diagnosticStart.RedirectStandardOutput=$true;$diagnosticStart.RedirectStandardError=$true
        $diagnosticPath=[Environment]::GetEnvironmentVariable('Path')
        foreach($key in @($diagnosticStart.Environment.Keys)){if($key -ieq 'Path'){[void]$diagnosticStart.Environment.Remove($key)}}
        $diagnosticStart.Environment['Path']=$diagnosticPath
        foreach($argument in @('-I',(Join-Path $PSScriptRoot 'profile-storage-diagnostic.py'),$FixtureRoot)){$diagnosticStart.ArgumentList.Add($argument)}
        $diagnosticProcess=[Diagnostics.Process]::Start($diagnosticStart)
        $diagnosticOutput=$diagnosticProcess.StandardOutput.ReadToEndAsync()
        $diagnosticErrors=$diagnosticProcess.StandardError.ReadToEndAsync()
        if(-not $diagnosticProcess.WaitForExit(10000)){$diagnosticProcess.Kill($true);[void]$diagnosticProcess.WaitForExit(5000);return @{status=2}}
        if($diagnosticProcess.ExitCode -ne 0){return @{status=2}}
        $payload=$diagnosticOutput.GetAwaiter().GetResult()
        if($payload.Length -gt 2048){return @{status=2}}
        $result=$payload | ConvertFrom-Json
        if($result.status -isnot [long] -and $result.status -isnot [int]){return @{status=2}}
        if($result.status -notin @(0,2,3)){return @{status=2}}
        if($result.status -ne 0){return @{status=[int]$result.status}}
        if($result.localStateExists -isnot [bool] -or @($result.profiles).Count -ne 3){return @{status=2}}
        $observations=@(foreach($item in $result.profiles) {
            if($item.status -notin @(0,1,2) -or $item.databaseExists -isnot [bool] -or $item.walExists -isnot [bool]){return @{status=2}}
            $observation=[ordered]@{status=[int]$item.status;databaseExists=$item.databaseExists;walExists=$item.walExists}
            foreach($metric in @('rows','persistentRows','expiryRows','encryptedRows')) {
                $value=$item.$metric
                if($null -ne $value -and (($value -isnot [long] -and $value -isnot [int]) -or $value -notin @(0,1,2))){return @{status=2}}
                $observation[$metric]=$value
            }
            $observation
        })
        return [ordered]@{status=0;localStateExists=$result.localStateExists;profiles=$observations}
    } catch {return @{status=2}}
    finally {if($diagnosticProcess){try{if(-not $diagnosticProcess.HasExited){$diagnosticProcess.Kill($true);[void]$diagnosticProcess.WaitForExit(5000)}}finally{$diagnosticProcess.Dispose()}}}
}
$evidence=[IO.Path]::GetFullPath($EvidenceDirectory)
New-Item -ItemType Directory -Force -Path $evidence | Out-Null
$suite=Join-Path $evidence ([Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $suite | Out-Null
Set-Content -LiteralPath (Join-Path $suite 'profile-fixture.marker') -Value 'AGI-BROWSE isolated profile fixture v1'
$fixtureProfilesRoot=Join-Path $suite 'profiles'
$portFile=Join-Path $suite 'fixture-port.txt';$stopFile=Join-Path $suite 'fixture-stop';$cacheResult=Join-Path $suite 'cache-result.json'
$node=(Get-Command node -ErrorAction Stop).Source
$server=Start-Process -FilePath $node -ArgumentList @("`"$(Join-Path $PSScriptRoot 'profile-fixture-server.mjs')`"","`"$portFile`"","`"$stopFile`"","`"$cacheResult`"") -WindowStyle Hidden -PassThru
$browserFailure=$null;$serverForcedCleanup=$false;$serverExitCode=$null
$storageBeforeRestart=@{status=1};$storageAfterRestart=@{status=1};$restartAttempted=$false
try {
    $deadline=(Get-Date).AddSeconds(10)
    while(-not (Test-Path -LiteralPath $portFile)) {if($server.HasExited -or (Get-Date) -gt $deadline){throw 'Isolated loopback fixture did not start'};Start-Sleep -Milliseconds 100}
    $port=0;if(-not [int]::TryParse((Get-Content -LiteralPath $portFile -Raw),[ref]$port) -or $port -lt 1 -or $port -gt 65535){throw 'Invalid isolated fixture port'}
    $url="http://127.0.0.1:$port/profile.html"
    & (Join-Path $PSScriptRoot 'test-lifecycle.ps1') -BuildDirectory $BuildDirectory -EvidenceDirectory $suite -FixtureUrl $url -ProfileProbe -ProfileFixtureRoot $fixtureProfilesRoot
    $storageBeforeRestart=Get-FixtureStorageDiagnostic $fixtureProfilesRoot
    $restartAttempted=$true
    & (Join-Path $PSScriptRoot 'test-lifecycle.ps1') -BuildDirectory $BuildDirectory -EvidenceDirectory $suite -FixtureUrl $url -ProfileProbe -ProfileRestart -ProfileFixtureRoot $fixtureProfilesRoot
} catch {
    $browserFailure=$_
} finally {
    if($restartAttempted){$storageAfterRestart=Get-FixtureStorageDiagnostic $fixtureProfilesRoot}
    Set-Content -LiteralPath $stopFile -Value 'stop'
    if(-not $server.WaitForExit(10000)){$serverForcedCleanup=$true;Stop-Process -Id $server.Id -Force -ErrorAction SilentlyContinue;[void]$server.WaitForExit(5000)}
    if($server.HasExited){$serverExitCode=$server.ExitCode}
    $server.Dispose()
}
$cache=if(Test-Path -LiteralPath $cacheResult){Get-Content -LiteralPath $cacheResult -Raw | ConvertFrom-Json}else{$null}
$passed=-not $browserFailure -and -not $serverForcedCleanup -and $serverExitCode -eq 0 -and $cache -and $cache.httpCacheRequests -eq 3 -and -not $cache.timeout
[ordered]@{passed=[bool]$passed;serverForcedCleanup=$serverForcedCleanup;serverExitCode=$serverExitCode;httpCacheRequests=$(if($cache){$cache.httpCacheRequests}else{$null});storageBeforeRestart=$storageBeforeRestart;storageAfterRestart=$storageAfterRestart} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $suite 'fixture-result.json')
if($browserFailure){throw $browserFailure}
if($serverForcedCleanup -or $serverExitCode -ne 0){throw 'Isolated fixture server did not stop naturally with exit zero'}
if(-not $passed){throw 'Actual HTTP cache isolation/persistence request count failed'}
Write-Host 'PASS: nine real profile storage/permission stages, native creation menu input and exactly three HTTP-cache requests across restart.'
