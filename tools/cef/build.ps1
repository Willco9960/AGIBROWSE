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
$build = [IO.Path]::GetFullPath($BuildDirectory)
$cef = & (Join-Path $PSScriptRoot 'bootstrap.ps1') -CacheDirectory $CacheDirectory
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
    & $CMakeExecutable -S $root -B $build -G $Generator @generatorArguments '-DAGI_BROWSE_WITH_CEF=ON' "-DCEF_ROOT=$cef"
    if ($LASTEXITCODE -ne 0) { throw 'CEF CMake configure failed' }
    & $CMakeExecutable --build $build --config Release --parallel 4
    if ($LASTEXITCODE -ne 0) { throw 'CEF build failed' }
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
