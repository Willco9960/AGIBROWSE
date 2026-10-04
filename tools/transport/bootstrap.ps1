[CmdletBinding()]
param([string]$CacheDirectory = (Join-Path $PSScriptRoot '../../build/transport-deps'), [switch]$DownloadOnly,[switch]$ValidateCacheOnly)
$ErrorActionPreference = 'Stop'
$cache = [IO.Path]::GetFullPath($CacheDirectory)
$lock = Get-Content (Join-Path $PSScriptRoot 'dependencies.lock.json') -Raw | ConvertFrom-Json
New-Item -ItemType Directory -Force -Path $cache | Out-Null
function Invoke-Bounded {
    param([string]$Executable,[string[]]$Arguments,[int]$TimeoutSeconds,[string]$Phase)
    $start=[Diagnostics.ProcessStartInfo]::new($Executable)
    $start.UseShellExecute=$false; $start.CreateNoWindow=$true
    $start.RedirectStandardOutput=$true; $start.RedirectStandardError=$true
    foreach($arg in $Arguments){$start.ArgumentList.Add($arg)}
    $process=[Diagnostics.Process]::new();$process.StartInfo=$start
    $timer=[Diagnostics.Stopwatch]::StartNew();$next=0
    try {
        if(-not $process.Start()){throw "$Phase did not start"}
        $stdout=$process.StandardOutput.ReadToEndAsync();$stderr=$process.StandardError.ReadToEndAsync()
        while(-not $process.WaitForExit(1000)) {
            if($timer.Elapsed.TotalSeconds -ge $TimeoutSeconds){$process.Kill($true);$process.WaitForExit();throw "$Phase exceeded ${TimeoutSeconds}s"}
            if($timer.Elapsed.TotalSeconds -ge $next){Write-Host "$Phase elapsed=$([int]$timer.Elapsed.TotalSeconds)s";$next=$timer.Elapsed.TotalSeconds+10}
        }
        ($stdout.GetAwaiter().GetResult()+$stderr.GetAwaiter().GetResult()) | Set-Content (Join-Path $cache "$Phase.log")
        if($process.ExitCode){throw "$Phase failed; see $cache/$Phase.log"}
        Write-Host "$Phase completed=$([int]$timer.Elapsed.TotalSeconds)s"
    } finally {if(-not $process.HasExited){$process.Kill($true);$process.WaitForExit()};$process.Dispose()}
}
$sevenZip=Join-Path $env:ProgramFiles '7-Zip/7z.exe'
if(-not(Test-Path $sevenZip)){$sevenZip=(Get-Command 7z -ErrorAction Stop).Source}
foreach ($name in @('openssl','boost','perl')) {
    $pin = $lock.$name
    $archive = Join-Path $cache ([IO.Path]::GetFileName(([Uri]$pin.url).AbsolutePath))
    if (-not (Test-Path -LiteralPath $archive)) {
        if($ValidateCacheOnly){throw "$name archive absent from validation-only cache"}
        Invoke-Bounded -Executable (Join-Path $env:SystemRoot 'System32/curl.exe') -Arguments @('--fail','--location','--proto','=https','--silent','--show-error','--connect-timeout','20','--max-time','150','--speed-limit','32768','--speed-time','45','--retry','1','--retry-delay','2','--retry-max-time','300','--retry-all-errors','--output',"$archive.partial",$pin.url) -TimeoutSeconds 330 -Phase "$name-download"
        if ((Get-FileHash "$archive.partial" -Algorithm SHA256).Hash -ine $pin.sha256) { throw "$name download checksum mismatch" }
        Move-Item -LiteralPath "$archive.partial" -Destination $archive
    }
    if ((Get-FileHash $archive -Algorithm SHA256).Hash -ine $pin.sha256) { throw "$name cache checksum mismatch" }
    Write-Host "Verified $name $($pin.version) SHA256 $($pin.sha256)"
    if ($name -eq 'perl') {
        $directory = Join-Path $cache 'perl'
        if (-not (Test-Path (Join-Path $directory 'agi-source.sha256'))) {
            if($ValidateCacheOnly){throw "$name extraction marker absent"}
            New-Item -ItemType Directory -Force -Path $directory | Out-Null
            # Extract only the native interpreter/runtime, not bundled compilers.
            Invoke-Bounded -Executable $sevenZip -Arguments @('x','-y','-aoa','-bso0','-bsp0','-bse1',"-o$directory",$archive,'perl/*','-r') -TimeoutSeconds 120 -Phase 'perl-extraction'
        }
    } else {
        $directory = Join-Path $cache $(if ($name -eq 'openssl') { 'openssl-3.5.9' } else { 'boost_1_92_0' })
        if (-not (Test-Path (Join-Path $directory 'agi-source.sha256'))) {
            if($ValidateCacheOnly){throw "$name extraction marker absent"}
            Invoke-Bounded -Executable $sevenZip -Arguments @('x','-y','-aoa','-bso0','-bsp0','-bse1',"-o$cache",$archive) -TimeoutSeconds 120 -Phase "$name-decompression"
            $tarArchive=$archive -replace '\.gz$',''
            $args=@('x','-y','-aoa','-bso0','-bsp0','-bse1',"-o$cache",$tarArchive)
            if($name -eq 'boost'){$args+=@('boost_1_92_0/boost/*','boost_1_92_0/LICENSE_1_0.txt','-r')}
            Invoke-Bounded -Executable $sevenZip -Arguments $args -TimeoutSeconds 120 -Phase "$name-extraction"
        }
    }
    foreach($required in $(if($name -eq 'openssl'){@('Configure','LICENSE.txt','include/openssl/ssl.h.in')}elseif($name -eq 'boost'){@('boost/beast.hpp','boost/asio.hpp','boost/version.hpp')}else{@('perl/bin/perl.exe','perl/lib/Config.pm')})) {
        if(-not(Test-Path (Join-Path $directory $required))){throw "$name partial extraction: $required"}
    }
    $sourceMarker=Join-Path $directory 'agi-source.sha256'
    if(Test-Path $sourceMarker){if((Get-Content $sourceMarker -Raw).Trim() -ine $pin.sha256){throw "$name source marker mismatch"}}
    else {Set-Content $sourceMarker $pin.sha256}
}
if ($DownloadOnly) { return }
$install = Join-Path $cache 'openssl-install'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs = & $vswhere -latest -products '*' -version '[17.0,18.0)' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'MSVC v143 x64 missing' }
$vcvars = Join-Path $vs 'VC/Auxiliary/Build/vcvars64.bat'
$toolchain=Get-ChildItem (Join-Path $vs 'VC/Tools/MSVC') -Directory | Sort-Object Name -Descending | Select-Object -First 1
$compiler=Join-Path $toolchain.FullName 'bin/Hostx64/x64/cl.exe'
$identity=@{lockSha256=(Get-FileHash (Join-Path $PSScriptRoot 'dependencies.lock.json') -Algorithm SHA256).Hash; compilerVersion=(Get-Item $compiler).VersionInfo.FileVersion; flags='VC-WIN64A no-shared no-asm no-tests no-module no-legacy no-ssl3 no-comp /MT'}
$buildMarker=Join-Path $install 'agi-build.json'
function Verify-Install {
    foreach($file in @('lib/libssl.lib','lib/libcrypto.lib','include/openssl/ssl.h','include/openssl/opensslv.h')){if(-not(Test-Path (Join-Path $install $file))){throw "OpenSSL partial install: $file"}}
    $receipt=Get-Content $buildMarker -Raw | ConvertFrom-Json
    foreach($name in $identity.Keys){if($receipt.$name -ne $identity.$name){throw "OpenSSL cache identity mismatch: $name"}}
    if($receipt.sslSha256 -ne (Get-FileHash (Join-Path $install 'lib/libssl.lib') -Algorithm SHA256).Hash -or $receipt.cryptoSha256 -ne (Get-FileHash (Join-Path $install 'lib/libcrypto.lib') -Algorithm SHA256).Hash){throw 'OpenSSL cache bytes mismatch'}
}
if(Test-Path $buildMarker){Verify-Install;Write-Output $cache;return}
if($ValidateCacheOnly){throw 'OpenSSL complete build receipt absent'}
$perl = Join-Path $cache 'perl/perl/bin/perl.exe'
$source = Join-Path $cache 'openssl-3.5.9'
$batch = Join-Path $cache 'build-openssl.cmd'
@"
@echo off
call "$vcvars" >nul
if errorlevel 1 exit /b 1
set PATH=$cache\perl\perl\bin;%PATH%
set LANG=C
cd /d "$source"
"$perl" Configure VC-WIN64A no-shared no-asm no-tests no-module no-legacy no-ssl3 no-comp --prefix="$install" --openssldir="$install" --libdir=lib /MT
if errorlevel 1 exit /b 1
nmake build_libs
if errorlevel 1 exit /b 1
nmake install_dev
exit /b %errorlevel%
"@ | Set-Content -LiteralPath $batch -Encoding ascii
$start = [Diagnostics.ProcessStartInfo]::new($env:ComSpec)
$start.ArgumentList.Add('/d'); $start.ArgumentList.Add('/c'); $start.ArgumentList.Add($batch)
$start.UseShellExecute = $false; $start.CreateNoWindow = $true
$start.RedirectStandardOutput = $true; $start.RedirectStandardError = $true
$effectivePath = [Environment]::GetEnvironmentVariable('PATH','Process')
$pairs = [Collections.Generic.Dictionary[string,string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach ($entry in [Environment]::GetEnvironmentVariables('Process').GetEnumerator()) { $pairs[[string]$entry.Key]=[string]$entry.Value }
$start.Environment.Clear()
foreach ($entry in $pairs.GetEnumerator()) { if ($entry.Key -ine 'PATH') { $start.Environment.Add($entry.Key,$entry.Value) } }
$start.Environment.Add('PATH',$effectivePath)
$process = [Diagnostics.Process]::new(); $process.StartInfo=$start
try {
    if (-not $process.Start()) { throw 'OpenSSL build did not start' }
    $stdout=$process.StandardOutput.ReadToEndAsync(); $stderr=$process.StandardError.ReadToEndAsync()
    $timer=[Diagnostics.Stopwatch]::StartNew();$next=0
    while(-not $process.WaitForExit(1000)) {
        if($timer.Elapsed.TotalSeconds -ge 600){$process.Kill($true);$process.WaitForExit();throw 'OpenSSL build exceeded600s'}
        if($timer.Elapsed.TotalSeconds -ge $next){Write-Host "OpenSSL build elapsed=$([int]$timer.Elapsed.TotalSeconds)s";$next=$timer.Elapsed.TotalSeconds+10}
    }
    ($stdout.GetAwaiter().GetResult()+$stderr.GetAwaiter().GetResult()) | Set-Content (Join-Path $cache 'openssl-build.log')
    if ($process.ExitCode) { throw "OpenSSL build exit $($process.ExitCode); see $cache/openssl-build.log" }
    Write-Host "OpenSSL build completed=$([int]$timer.Elapsed.TotalSeconds)s"
} finally { if(-not $process.HasExited){$process.Kill($true);$process.WaitForExit()};$process.Dispose() }
foreach($file in @('lib/libssl.lib','lib/libcrypto.lib','include/openssl/ssl.h','include/openssl/opensslv.h')){if(-not(Test-Path (Join-Path $install $file))){throw "OpenSSL partial install: $file"}}
$identity.sslSha256=(Get-FileHash (Join-Path $install 'lib/libssl.lib') -Algorithm SHA256).Hash
$identity.cryptoSha256=(Get-FileHash (Join-Path $install 'lib/libcrypto.lib') -Algorithm SHA256).Hash
$identity | ConvertTo-Json | Set-Content $buildMarker
Verify-Install
Write-Output $cache
