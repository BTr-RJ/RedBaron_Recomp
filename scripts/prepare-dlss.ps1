[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$Destination, [string]$Source = '')
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$commit = '374959484e79a640feaba44c93ac8cfb0a03f5b5'
$dllHash = '3975567B8943C53ACCE397F2B72380092F84F162D00B0D2C7D08A1025C563983'
function Native([string[]]$Arguments) {
    & git @Arguments | Out-Host
    if ($LASTEXITCODE -ne 0) { throw 'DLSS SDK download failed. The working folder has been preserved.' }
}
if ($Source) {
    $sdk = [IO.Path]::GetFullPath($Source)
} else {
    $sdk = [IO.Path]::GetFullPath($Destination)
    if (Test-Path -LiteralPath $sdk) { throw 'DLSS SDK destination must not already exist.' }
    New-Item -ItemType Directory -Path (Split-Path -Parent $sdk) -Force | Out-Null
    # Official SDK stays local. Its NVIDIA license applies independently of the port.
    Native @('clone','--filter=blob:none','--no-checkout','https://github.com/NVIDIA/DLSS.git',$sdk)
    Native @('-C',$sdk,'sparse-checkout','set','--no-cone','/include/',
        '/lib/Windows_x86_64/x64/nvsdk_ngx_d.lib',
        '/lib/Windows_x86_64/rel/nvngx_dlss.dll','/LICENSE.txt')
    Native @('-C',$sdk,'checkout','--detach',$commit)
}
foreach ($name in @('LICENSE.txt','include/nvsdk_ngx.h','lib/Windows_x86_64/x64/nvsdk_ngx_d.lib','lib/Windows_x86_64/rel/nvngx_dlss.dll')) {
    if (!(Test-Path -LiteralPath (Join-Path $sdk $name))) { throw "Missing DLSS SDK file: $name" }
}
if ((Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $sdk 'lib/Windows_x86_64/rel/nvngx_dlss.dll')).Hash -ne $dllHash) {
    throw 'Unexpected DLSS binary. Use the pinned official NVIDIA DLSS 310.9.1 SDK.'
}
Write-Output $sdk
