[CmdletBinding()]
param(
    [string]$Archive = '',
    [string]$Destination = ''
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$project = Split-Path -Parent $PSScriptRoot
if (!$Destination) { $Destination = Join-Path $project 'video-sdk-work/fidelityfx-2.0.0' }
$Destination = [IO.Path]::GetFullPath($Destination)
$expected = '2C353F94A20A233E033A15C0DDFECAA93500846FC399F13E753F0AC27B9F63DD'
if (!$Archive) {
    $cache = Join-Path $project 'video-sdk-work/downloads'
    New-Item -ItemType Directory -Path $cache -Force | Out-Null
    $Archive = Join-Path $cache 'FidelityFX-SDK-v2.0.0.zip'
    if (!(Test-Path -LiteralPath $Archive)) {
        Write-Host 'Downloading the official AMD FidelityFX SDK 2.0.0 (FSR 3.1.5)...'
        [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
        Invoke-WebRequest -UseBasicParsing -Uri 'https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/releases/download/v2.0.0/FidelityFX-SDK-v2.0.0.zip' -OutFile $Archive
    }
}
$Archive = [IO.Path]::GetFullPath($Archive)
if ((Get-FileHash -LiteralPath $Archive -Algorithm SHA256).Hash -ne $expected) {
    throw 'FidelityFX archive SHA-256 does not match the pinned official release. No files extracted.'
}
if ((Test-Path -LiteralPath $Destination) -and (Get-ChildItem -LiteralPath $Destination -Force | Select-Object -First 1)) {
    throw 'Dependency destination must be empty. Existing files have been preserved.'
}
Add-Type -AssemblyName System.IO.Compression.FileSystem
[IO.Compression.ZipFile]::ExtractToDirectory($Archive, $Destination)
$kit = Join-Path $Destination 'Kits/FidelityFX'
$dll = Join-Path $kit 'signedbin/amd_fidelityfx_upscaler_dx12.dll'
if (!(Test-Path -LiteralPath $dll)) { throw 'Official upscaler provider is missing from the extracted archive.' }
Copy-Item -LiteralPath (Join-Path $Destination 'docs/license.md') -Destination (Join-Path $kit 'FidelityFX-LICENSE.txt')
Write-Output $kit
