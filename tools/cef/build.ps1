[CmdletBinding()]
param(
    [string]$BuildDirectory = (Join-Path $PSScriptRoot '../../build/windows-cef'),
    [string]$CacheDirectory = (Join-Path $PSScriptRoot '../../build/cef-sdk'),
    [string]$CMakeExecutable = 'cmake',
    [ValidateSet('Visual Studio 17 2022', 'Ninja')]
    [string]$Generator = 'Visual Studio 17 2022'
)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
& (Join-Path $root 'tools/transport/bootstrap.ps1') | Out-Null
$build = [IO.Path]::GetFullPath($BuildDirectory)
$cef = & (Join-Path $PSScriptRoot 'bootstrap.ps1') -CacheDirectory $CacheDirectory
New-Item -ItemType Directory -Force -Path $build | Out-Null
function Invoke-NormalizedCMake {
    param([string[]]$Arguments, [string]$LogName)
    $environment = [Collections.Generic.Dictionary[string,string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($entry in [Environment]::GetEnvironmentVariables('Process').GetEnumerator()) {
        $key = [string]$entry.Key
        if (-not $environment.ContainsKey($key)) { $environment[$key] = [string]$entry.Value }
    }
    # Preserve the effective PATH, including Codex's tool locations, under one
    # canonical name. Only the child environment is changed.
    $environment['PATH'] = [Environment]::GetEnvironmentVariable('PATH', 'Process')
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = (Get-Command $CMakeExecutable -ErrorAction Stop).Source
    $start.WorkingDirectory = $root
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    foreach ($argument in $Arguments) { $start.ArgumentList.Add($argument) }
    $start.Environment.Clear()
    foreach ($entry in $environment.GetEnumerator()) {
        $name = if ($entry.Key -ieq 'PATH') { 'PATH' } else { $entry.Key }
        $start.Environment.Add($name, $entry.Value)
    }
    $duplicates = @($start.Environment.Keys | Group-Object { $_.ToUpperInvariant() } | Where-Object Count -gt 1)
    $pathCount = @($start.Environment.Keys | Where-Object { $_ -ieq 'PATH' }).Count
    if ($duplicates.Count -or $pathCount -ne 1) { throw 'Child environment normalization failed' }
    Write-Host 'Verified child environment: duplicate names=0; PATH entries=1'
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $start
    try {
        if (-not $process.Start()) { throw 'CMake process did not start' }
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        $process.WaitForExit()
        $output = $stdout.GetAwaiter().GetResult() + $stderr.GetAwaiter().GetResult()
        $log = Join-Path $build $LogName
        Set-Content -LiteralPath $log -Value $output -Encoding utf8
        Write-Host "CMake exit $($process.ExitCode); log: $log"
        if ($process.ExitCode -ne 0) {
            Write-Host (($output -split "`n" | Select-Object -Last 30) -join "`n")
            throw "CMake failed with exit $($process.ExitCode): $log"
        }
    } finally { $process.Dispose() }
}
$savedEnvironment = @{}
try {
    $generatorArguments = @('-A', 'x64', '-T', 'v143')
    if ($Generator -eq 'Ninja') {
        $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
        $installation = & $vswhere -latest -products '*' -version '[17.0,18.0)' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if (-not $installation) { throw 'Visual Studio 2022 MSVC x64 toolchain is missing' }
        $vcvars = Join-Path $installation 'VC/Auxiliary/Build/vcvars64.bat'
        $toolchainEnvironment = & $env:ComSpec /d /s /c "call `"$vcvars`" >nul && set"
        if ($LASTEXITCODE -ne 0) { throw 'MSVC environment initialization failed' }
        foreach ($line in $toolchainEnvironment) {
            if ($line -match '^(PATH|INCLUDE|LIB|LIBPATH|VCToolsInstallDir|VCINSTALLDIR|WindowsSdkDir|WindowsSDKVersion|VSINSTALLDIR|VSCMD_ARG_TGT_ARCH)=(.*)$') {
                $key = $matches[1]
                $savedEnvironment[$key] = [Environment]::GetEnvironmentVariable($key, 'Process')
                [Environment]::SetEnvironmentVariable($key, $matches[2], 'Process')
            }
        }
        $compiler = Join-Path $env:VCToolsInstallDir 'bin/Hostx64/x64/cl.exe'
        if (-not (Test-Path -LiteralPath $compiler)) { throw 'MSVC x64 compiler was not found after vcvars64' }
        $generatorArguments = @('-DCMAKE_BUILD_TYPE=Release', "-DCMAKE_CXX_COMPILER=$compiler")
    }
    Invoke-NormalizedCMake -Arguments (@('-S', $root, '-B', $build, '-G', $Generator) + $generatorArguments + @('-DAGI_BROWSE_WITH_CEF=ON', "-DCEF_ROOT=$cef")) -LogName 'configure.log'
    Invoke-NormalizedCMake -Arguments @('--build', $build, '--config', 'Release', '--parallel', '4') -LogName 'build.log'
} finally {
    foreach ($key in $savedEnvironment.Keys) {
        [Environment]::SetEnvironmentVariable($key, $savedEnvironment[$key], 'Process')
    }
}
$launcher = Join-Path $build 'browser/Release/agi-browse-host.exe'
$actual = (Get-FileHash -LiteralPath $launcher -Algorithm SHA256).Hash
$upstream = (Get-FileHash -LiteralPath (Join-Path $cef 'Release/bootstrap.exe') -Algorithm SHA256).Hash
if ($actual -ne $upstream) { throw 'Browser launcher is not the pinned sandbox bootstrap' }
Write-Host "Built CEF host: $launcher"
