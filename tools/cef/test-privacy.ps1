[CmdletBinding()]
param(
    [string]$BuildDirectory = (Join-Path $PSScriptRoot '../../build/windows-cef'),
    [string]$EvidenceDirectory = (Join-Path $PSScriptRoot '../../build/cef-privacy')
)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$exe=[IO.Path]::GetFullPath((Join-Path $BuildDirectory 'browser/Release/agi-browse-host.exe'))
$evidence=[IO.Path]::GetFullPath($EvidenceDirectory)
New-Item -ItemType Directory -Force -Path $evidence | Out-Null
$engineLog=Join-Path (Split-Path -Parent $exe) 'debug.log'
$baseline=if(Test-Path -LiteralPath $engineLog){(Get-FileHash -LiteralPath $engineLog -Algorithm SHA256).Hash}else{''}
& (Join-Path $PSScriptRoot 'test-lifecycle.ps1') -BuildDirectory $BuildDirectory -EvidenceDirectory $evidence -PrivacyProbe -SecurityProbe
function Assert-NoSentinel([string]$Path) {
    if(Test-Path -LiteralPath $Path){
        $text=[Text.Encoding]::UTF8.GetString([IO.File]::ReadAllBytes($Path))
        if($text -match 'SENTINEL_(PASSWORD|MARKED)_010|PRIVATE_(PASSWORD|MARKED|SUBTREE)_NAME_010'){
            # Report the channel only; never echo the matched secret/log line.
            throw "Privacy sentinel leaked in output channel: $([IO.Path]::GetFileName($Path))"
        }
    }
}
$runs=@(Get-ChildItem -LiteralPath $evidence -Directory | Where-Object {Test-Path -LiteralPath (Join-Path $_.FullName 'result.json')})
foreach($run in $runs){
    foreach($name in @('lifecycle.jsonl','stdout.txt','stderr.txt','result.json')){Assert-NoSentinel (Join-Path $run.FullName $name)}
    if(Test-Path -LiteralPath (Join-Path $run.FullName 'debug.log')){Assert-NoSentinel (Join-Path $run.FullName 'debug.log');throw 'CEF unexpectedly created working-directory debug.log'}
}
$engineUnchanged=!(Test-Path -LiteralPath $engineLog) -or ($baseline -and (Get-FileHash -LiteralPath $engineLog -Algorithm SHA256).Hash -eq $baseline)
if(-not $engineUnchanged){Assert-NoSentinel $engineLog;throw 'CEF unexpectedly created or changed its default engine log'}
$startup=Join-Path $evidence ([Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $startup | Out-Null
$cases=@(
    @('--enable-logging','--log-severity=verbose'),
    @('--log-file='+ (Join-Path $startup 'requested.log')),
    @('--log-net-log='+ (Join-Path $startup 'requested-netlog.json')),
    @('--trace-startup','--trace-startup-file='+ (Join-Path $startup 'requested-trace.json')),
    @('--enable-crash-reporter','--crash-dumps-dir='+ (Join-Path $startup 'dumps'))
)
$results=@();$index=0
foreach($case in $cases){
    $out=Join-Path $startup "startup-$index-out.txt";$err=Join-Path $startup "startup-$index-err.txt"
    # The sentinel is deliberately supplied as a URL reflection. Do not store
    # process command lines or raw matches in exported/committed evidence.
    $process=Start-Process -FilePath $exe -ArgumentList (@('--url=https://example.test/?q=SENTINEL_PASSWORD_010')+$case) -PassThru -WindowStyle Hidden -WorkingDirectory $startup -RedirectStandardOutput $out -RedirectStandardError $err
    try {
        if(-not $process.WaitForExit(10000)){Stop-Process -Id $process.Id -Force;throw 'Hostile logging startup did not fail before initialization'}
        if($process.ExitCode -ne 64){throw 'Hostile logging startup was not rejected with exit64'}
        Assert-NoSentinel $out;Assert-NoSentinel $err
        $results+=@{caseIndex=$index;exitCode=$process.ExitCode;passed=$true}
    } finally {$process.Dispose()}
    $index++
}
foreach($name in @('requested.log','requested-netlog.json','requested-trace.json','dumps','debug.log')) {
    if(Test-Path -LiteralPath (Join-Path $startup $name)){throw 'Rejected startup created a requested diagnostic export'}
}
# Default engine log must also remain absent/unchanged across hostile starts.
if(Test-Path -LiteralPath $engineLog){if(-not $baseline -or (Get-FileHash -LiteralPath $engineLog -Algorithm SHA256).Hash -ne $baseline){throw 'Rejected startup changed the default engine log'}}
@{passed=$true;startupCases=$results;sentinelScanPassed=$true;engineLogUnchanged=$true;coverage='supported nonfatal host diagnostics, console fallback and startup diagnostic switches; no crash or future semantic-stream claim'} |
    ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $startup 'privacy-result.json') -Encoding utf8
Write-Host "PASS: positive renderer paths, current nonfatal output sentinel scans, five rejected logging startups. Evidence: $startup"
