[CmdletBinding()]
param([string]$CacheDirectory = (Join-Path $PSScriptRoot '../../build/cef-sdk'))
$ErrorActionPreference = 'Stop'
$pin = Get-Content (Join-Path $PSScriptRoot 'cef.lock.json') -Raw | ConvertFrom-Json
$cache = [IO.Path]::GetFullPath($CacheDirectory)
New-Item -ItemType Directory -Force -Path $cache | Out-Null
function Write-BootstrapProgress {
    param([string]$Message)
    $line = [DateTime]::UtcNow.ToString('o') + ' CEF bootstrap: ' + $Message
    Add-Content -LiteralPath (Join-Path $cache 'bootstrap.log') -Value $line -Encoding utf8
    Write-Host $line
}
function Invoke-BoundedProcess {
    param([string]$Executable, [string[]]$Arguments, [int]$TimeoutSeconds, [string]$Phase)
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $Executable
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    foreach ($argument in $Arguments) { $start.ArgumentList.Add($argument) }
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $start
    $timer = [Diagnostics.Stopwatch]::StartNew()
    $launched = $false
    try {
        if (-not $process.Start()) { throw "$Phase did not start" }
        $launched = $true
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        $nextProgressSeconds = 0
        while (-not $process.WaitForExit(1000)) {
            if ($timer.Elapsed.TotalSeconds -ge $TimeoutSeconds) {
                $process.Kill($true)
                $process.WaitForExit()
                throw "$Phase exceeded its ${TimeoutSeconds}s process limit"
            }
            if ($timer.Elapsed.TotalSeconds -ge $nextProgressSeconds) {
                $progress = "$Phase running: elapsed=$([int]$timer.Elapsed.TotalSeconds)s"
                if ($Phase -eq 'download') {
                    $bytes = if (Test-Path -LiteralPath ($archive + '.partial')) { (Get-Item -LiteralPath ($archive + '.partial')).Length } else { 0 }
                    $progress += "; archiveBytes=$bytes/$($pin.size)"
                }
                Write-BootstrapProgress $progress
                $nextProgressSeconds = $timer.Elapsed.TotalSeconds + 10
            }
        }
        $diagnostics = $stdout.GetAwaiter().GetResult() + $stderr.GetAwaiter().GetResult()
        if ($diagnostics.Trim()) {
            Add-Content -LiteralPath (Join-Path $cache 'bootstrap.log') -Value $diagnostics -Encoding utf8
        }
        if ($process.ExitCode -ne 0) {
            throw "$Phase failed with exit $($process.ExitCode): $($diagnostics.Trim())"
        }
        Write-BootstrapProgress "$Phase completed in $([int]$timer.Elapsed.TotalSeconds)s"
    } catch {
        Write-BootstrapProgress "$Phase failed: $($_.Exception.Message)"
        throw
    } finally {
        if ($launched -and -not $process.HasExited) { $process.Kill($true); $process.WaitForExit() }
        $process.Dispose()
    }
}
Write-BootstrapProgress "start version=$($pin.version); cache=$cache"
$archive = Join-Path $cache $pin.archive
if (-not (Test-Path -LiteralPath $archive)) {
    $partial = $archive + '.partial'
    $curl = Join-Path $env:SystemRoot 'System32/curl.exe'
    if (-not (Test-Path -LiteralPath $curl)) { throw 'Native Windows curl.exe is required for bounded CEF retrieval' }
    Write-BootstrapProgress 'download start: connect=20s; attempt=300s; retry=1; retryBudget=600s; idleRate=32768B/s for45s'
    Invoke-BoundedProcess -Executable $curl -Arguments @(
        '--fail', '--location', '--proto', '=https', '--silent', '--show-error',
        '--connect-timeout', '20', '--max-time', '300', '--speed-limit', '32768', '--speed-time', '45',
        '--retry', '1', '--retry-delay', '2', '--retry-max-time', '600', '--retry-all-errors',
        '--output', $partial, $pin.url
    ) -TimeoutSeconds 630 -Phase 'download'
    Write-BootstrapProgress 'download SHA256 verification start'
    if ((Get-FileHash -LiteralPath $partial -Algorithm SHA256).Hash.ToLowerInvariant() -ne $pin.sha256) {
        throw "CEF download SHA256 mismatch: $partial"
    }
    Move-Item -LiteralPath $partial -Destination $archive
} else {
    Write-BootstrapProgress 'archive cache hit'
}
Write-BootstrapProgress 'archive size/SHA256/upstreamSHA1 verification start'
if ((Get-Item -LiteralPath $archive).Length -ne $pin.size -or
    (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant() -ne $pin.sha256 -or
    (Get-FileHash -LiteralPath $archive -Algorithm SHA1).Hash.ToLowerInvariant() -ne $pin.upstreamSha1) {
    throw "CEF archive size/checksum mismatch: $archive"
}
Write-BootstrapProgress 'archive checksums verified'
$sdk = Join-Path $cache ($pin.archive -replace '\.tar\.bz2$', '')
$marker = Join-Path $sdk 'agi-browse-pin.sha256'
if (-not (Test-Path -LiteralPath $marker)) {
    Write-BootstrapProgress 'extraction start: processLimit=90s'
    $tar = Join-Path $env:SystemRoot 'System32/tar.exe'
    if (-not (Test-Path -LiteralPath $tar)) { throw 'Native Windows tar.exe is required for CEF extraction' }
    Invoke-BoundedProcess -Executable $tar -Arguments @('-xf', $archive, '-C', $cache) -TimeoutSeconds 90 -Phase 'extraction'
    foreach ($required in @('Release/bootstrap.exe', 'Release/libcef.dll', 'Release/libcef.lib',
                            'include/cef_version.h', 'Resources/icudtl.dat', 'LICENSE.txt')) {
        if (-not (Test-Path -LiteralPath (Join-Path $sdk $required))) { throw "Missing SDK file: $required" }
    }
    Set-Content -LiteralPath $marker -Value $pin.sha256 -Encoding ascii
}
if ((Get-Content -LiteralPath $marker -Raw).Trim() -ne $pin.sha256) { throw 'CEF SDK pin marker mismatch' }
Write-BootstrapProgress 'SDK ready'
$sdk
