[CmdletBinding()]
param([string]$CacheDirectory = (Join-Path $PSScriptRoot '../../build/cef-sdk'))
$ErrorActionPreference = 'Stop'
$pin = Get-Content (Join-Path $PSScriptRoot 'cef.lock.json') -Raw | ConvertFrom-Json
$cache = [IO.Path]::GetFullPath($CacheDirectory)
New-Item -ItemType Directory -Force -Path $cache | Out-Null
$archive = Join-Path $cache $pin.archive
if (-not (Test-Path -LiteralPath $archive)) {
    $partial = $archive + '.partial'
    Invoke-WebRequest -Uri $pin.url -OutFile $partial
    if ((Get-FileHash -LiteralPath $partial -Algorithm SHA256).Hash.ToLowerInvariant() -ne $pin.sha256) {
        throw "CEF download SHA256 mismatch: $partial"
    }
    Move-Item -LiteralPath $partial -Destination $archive
}
if ((Get-Item -LiteralPath $archive).Length -ne $pin.size -or
    (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant() -ne $pin.sha256 -or
    (Get-FileHash -LiteralPath $archive -Algorithm SHA1).Hash.ToLowerInvariant() -ne $pin.upstreamSha1) {
    throw "CEF archive size/checksum mismatch: $archive"
}
$sdk = Join-Path $cache ($pin.archive -replace '\.tar\.bz2$', '')
$marker = Join-Path $sdk 'agi-browse-pin.sha256'
if (-not (Test-Path -LiteralPath $marker)) {
    & tar.exe -xf $archive -C $cache
    if ($LASTEXITCODE -ne 0) { throw 'CEF extraction failed' }
    foreach ($required in @('Release/bootstrap.exe', 'Release/libcef.dll', 'Release/libcef.lib',
                            'include/cef_version.h', 'Resources/icudtl.dat', 'LICENSE.txt')) {
        if (-not (Test-Path -LiteralPath (Join-Path $sdk $required))) { throw "Missing SDK file: $required" }
    }
    Set-Content -LiteralPath $marker -Value $pin.sha256 -Encoding ascii
}
if ((Get-Content -LiteralPath $marker -Raw).Trim() -ne $pin.sha256) { throw 'CEF SDK pin marker mismatch' }
$sdk
