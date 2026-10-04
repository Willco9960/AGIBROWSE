[CmdletBinding()]
param(
    [string]$BuildDirectory = (Join-Path $PSScriptRoot '../../build/windows-cef'),
    [string]$EvidenceDirectory = (Join-Path $PSScriptRoot '../../build/cef-profiles')
)
$ErrorActionPreference='Stop'
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
try {
    $deadline=(Get-Date).AddSeconds(10)
    while(-not (Test-Path -LiteralPath $portFile)) {if($server.HasExited -or (Get-Date) -gt $deadline){throw 'Isolated loopback fixture did not start'};Start-Sleep -Milliseconds 100}
    $port=0;if(-not [int]::TryParse((Get-Content -LiteralPath $portFile -Raw),[ref]$port) -or $port -lt 1 -or $port -gt 65535){throw 'Invalid isolated fixture port'}
    $url="http://127.0.0.1:$port/profile.html"
    & (Join-Path $PSScriptRoot 'test-lifecycle.ps1') -BuildDirectory $BuildDirectory -EvidenceDirectory $suite -FixtureUrl $url -ProfileProbe -ProfileFixtureRoot $fixtureProfilesRoot
    & (Join-Path $PSScriptRoot 'test-lifecycle.ps1') -BuildDirectory $BuildDirectory -EvidenceDirectory $suite -FixtureUrl $url -ProfileProbe -ProfileRestart -ProfileFixtureRoot $fixtureProfilesRoot
} catch {
    $browserFailure=$_
} finally {
    Set-Content -LiteralPath $stopFile -Value 'stop'
    if(-not $server.WaitForExit(10000)){$serverForcedCleanup=$true;Stop-Process -Id $server.Id -Force -ErrorAction SilentlyContinue;[void]$server.WaitForExit(5000)}
    if($server.HasExited){$serverExitCode=$server.ExitCode}
    $server.Dispose()
}
$cache=if(Test-Path -LiteralPath $cacheResult){Get-Content -LiteralPath $cacheResult -Raw | ConvertFrom-Json}else{$null}
$passed=-not $browserFailure -and -not $serverForcedCleanup -and $serverExitCode -eq 0 -and $cache -and $cache.httpCacheRequests -eq 3 -and -not $cache.timeout
[ordered]@{passed=[bool]$passed;serverForcedCleanup=$serverForcedCleanup;serverExitCode=$serverExitCode;httpCacheRequests=$(if($cache){$cache.httpCacheRequests}else{$null})} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $suite 'fixture-result.json')
if($browserFailure){throw $browserFailure}
if($serverForcedCleanup -or $serverExitCode -ne 0){throw 'Isolated fixture server did not stop naturally with exit zero'}
if(-not $passed){throw 'Actual HTTP cache isolation/persistence request count failed'}
Write-Host 'PASS: nine real profile storage/permission stages, native creation menu input and exactly three HTTP-cache requests across restart.'
