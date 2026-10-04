[CmdletBinding()]
param([string]$CacheDirectory=(Join-Path $PSScriptRoot '../../build/transport-deps'))
$ErrorActionPreference='Stop'
$cache=[IO.Path]::GetFullPath($CacheDirectory)
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../build/transport-cache-tests'))
New-Item -ItemType Directory -Force -Path $root | Out-Null
$run=Join-Path $root ([Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $run | Out-Null
$bootstrap=Join-Path $PSScriptRoot 'bootstrap.ps1'
$cases=@()
function Expect-Failure([string]$directory,[string]$expected,[string]$name){
    $message=$null
    try{& $bootstrap -CacheDirectory $directory -ValidateCacheOnly | Out-Null}catch{$message=$_.Exception.Message}
    if(-not $message -or $message -notlike "*$expected*"){throw "$name expected rejection missing; actual=$message"}
    $script:cases+=[ordered]@{name=$name;passed=$true;reason=$expected}
    Write-Host "PASS $name"
}
& $bootstrap -CacheDirectory $cache -ValidateCacheOnly | Out-Null
$cases+=[ordered]@{name='complete pinned native cache validates';passed=$true}
$bad=Join-Path $run 'corrupt-archive';New-Item -ItemType Directory -Path $bad | Out-Null
Set-Content (Join-Path $bad 'openssl-3.5.9.tar.gz') 'corrupt archive fixture'
Expect-Failure $bad 'cache checksum mismatch' 'archive byte corruption rejects before extraction/network'
$missing=Join-Path $run 'missing';New-Item -ItemType Directory -Path $missing | Out-Null
Expect-Failure $missing 'archive absent' 'missing archive never retrieves in validate-only mode'
# All aliases remain inside the ignored cache. Test writes affect only this new
# install directory; source junctions and archive hard links are read-only here.
$partial=Join-Path $run 'partial-install';New-Item -ItemType Directory -Path $partial | Out-Null
foreach($archive in @('openssl-3.5.9.tar.gz','boost_1_92_0.tar.gz','strawberry-perl-5.42.3.1-64bit-portable.zip')){New-Item -ItemType HardLink -Path (Join-Path $partial $archive) -Target (Join-Path $cache $archive) | Out-Null}
foreach($source in @('openssl-3.5.9','boost_1_92_0','perl')){New-Item -ItemType Junction -Path (Join-Path $partial $source) -Target (Join-Path $cache $source) | Out-Null}
$install=Join-Path $partial 'openssl-install';New-Item -ItemType Directory -Path $install | Out-Null
Expect-Failure $partial 'complete build receipt absent' 'partial static install has no accepted receipt'
Copy-Item (Join-Path $cache 'openssl-install/agi-build.json') (Join-Path $install 'agi-build.json')
Expect-Failure $partial 'partial install' 'receipt cannot bless missing SSL crypto or headers'
foreach($relative in @('lib/libssl.lib','lib/libcrypto.lib','include/openssl/ssl.h','include/openssl/opensslv.h')) {
    $target=Join-Path $install $relative
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
    New-Item -ItemType HardLink -Path $target -Target (Join-Path $cache "openssl-install/$relative") | Out-Null
}
$marker=Join-Path $install 'agi-build.json'
$receipt=Get-Content $marker -Raw | ConvertFrom-Json
$receipt.flags='unreviewed build flags';$receipt | ConvertTo-Json | Set-Content $marker
Expect-Failure $partial 'cache identity mismatch' 'stale build flags cannot reuse native libraries'
$receipt=Get-Content (Join-Path $cache 'openssl-install/agi-build.json') -Raw | ConvertFrom-Json
$receipt.cryptoSha256=('0'*64);$receipt | ConvertTo-Json | Set-Content $marker
Expect-Failure $partial 'cache bytes mismatch' 'manifest cannot bless mismatched crypto library digest'
$cases | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $run 'result.json')
Write-Host "PASS dependency cache checks=$($cases.Count); result=$run/result.json"
