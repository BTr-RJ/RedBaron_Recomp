[CmdletBinding()]
param(
    [string]$SdkRoot = '',
    [string]$BuildDirectory = '',
    [string]$EosSdk = '',
    [ValidateRange(1, 64)][int]$Parallel = 4,
    [switch]$ConfigureOnly
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$projectRoot = Split-Path -Parent $PSScriptRoot
if (!$SdkRoot) { $SdkRoot = Join-Path $projectRoot 'sdk_experimental' }
if (!$BuildDirectory) { $BuildDirectory = Join-Path $projectRoot 'build_cpp_release' }
$SdkRoot = [IO.Path]::GetFullPath($SdkRoot)
$BuildDirectory = [IO.Path]::GetFullPath($BuildDirectory)
$portDir = Join-Path $projectRoot 'port'
$codegen = Join-Path $SdkRoot 'bin/rexglue.exe'

foreach ($required in @(
    (Join-Path $projectRoot 'game/default.xex'),
    $codegen,
    (Join-Path $SdkRoot 'lib/cmake/rexglue/rexglueConfig.cmake'),
    (Join-Path $SdkRoot 'bin/rexruntime.dll'),
    (Join-Path $SdkRoot 'bin/rexgpu-xenos.dll')
)) {
    if (!(Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Required file missing: $required. Provide your local game files and install the SDK."
    }
}
Get-Command cmake -ErrorAction Stop | Out-Null
& (Join-Path $PSScriptRoot 'prepare-game.ps1') -GameRoot (Join-Path $projectRoot 'game')

function Invoke-CheckedNative {
    param([string]$Program, [string[]]$Arguments)
    # Windows PowerShell 5 treats redirected native stderr as ErrorRecords,
    # even for successful tools that log progress there (including ReXGlue).
    $previousPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        & $Program @Arguments 2>&1 | ForEach-Object { $_.ToString() }
        $nativeExit = $LASTEXITCODE
    } finally { $ErrorActionPreference = $previousPreference }
    if ($nativeExit -ne 0) { throw "$Program failed: $nativeExit" }
}

if (!$ConfigureOnly -and (Test-Path -LiteralPath (Join-Path $BuildDirectory 'snoopyflyingace.exe'))) {
    $backupDir = Join-Path $BuildDirectory ('backups/build_' + (Get-Date -Format 'yyyyMMdd_HHmmss_fff'))
    New-Item -ItemType Directory -Path $backupDir | Out-Null
    foreach ($name in @('snoopyflyingace.exe', 'redbaron_net.exe', 'datachannel.dll', 'rexruntime.dll', 'rexgpu-xenos.dll', 'amd_fidelityfx_upscaler_dx12.dll', 'FidelityFX-LICENSE.txt', 'nvngx_dlss.dll', 'NVIDIA-DLSS-LICENSE.txt', 'snoopyflyingace.toml')) {
        $source = Join-Path $BuildDirectory $name
        if (Test-Path -LiteralPath $source) { Copy-Item -LiteralPath $source -Destination $backupDir }
    }
    Write-Output "Build backup: $backupDir"
}

Push-Location $portDir
try {
    Invoke-CheckedNative $codegen @('codegen', (Join-Path $portDir 'snoopyflyingace_manifest.toml'))
} finally { Pop-Location }

$cmakeArgs = @('-S', $portDir, '-B', $BuildDirectory, '-G', 'Ninja',
    '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_CXX_COMPILER=clang++',
    ('-Drexglue_DIR=' + (Join-Path $SdkRoot 'lib/cmake/rexglue')))
if($EosSdk){$cmakeArgs += ('-DREDBARON_EOS_SDK=' + [IO.Path]::GetFullPath($EosSdk))}
Invoke-CheckedNative 'cmake' $cmakeArgs
if ($ConfigureOnly) { Write-Output 'Code generation and configuration complete.'; return }

Invoke-CheckedNative 'cmake' @('--build', $BuildDirectory, '--target', 'snoopyflyingace', 'redbaron_probe', 'redbaron_content_inspect', '--parallel', "$Parallel")
foreach ($name in @('rexruntime.dll', 'rexgpu-xenos.dll')) {
    Copy-Item -LiteralPath (Join-Path $SdkRoot ('bin/' + $name)) -Destination $BuildDirectory
}
foreach ($name in @('amd_fidelityfx_upscaler_dx12.dll', 'FidelityFX-LICENSE.txt', 'nvngx_dlss.dll', 'NVIDIA-DLSS-LICENSE.txt')) {
    $dependency = Join-Path $SdkRoot ('bin/' + $name)
    if (Test-Path -LiteralPath $dependency) { Copy-Item -LiteralPath $dependency -Destination $BuildDirectory }
}
$networkExe=Join-Path $SdkRoot 'bin/redbaron_net.exe'
$networkDll=Join-Path $SdkRoot 'bin/datachannel.dll'
if((Test-Path -LiteralPath $networkExe -PathType Leaf) -and (Test-Path -LiteralPath $networkDll -PathType Leaf)) {
    Copy-Item -LiteralPath $networkExe,$networkDll -Destination $BuildDirectory -Force
    $networkLicenses=Join-Path $SdkRoot 'licenses/network'
    if(!(Test-Path -LiteralPath $networkLicenses -PathType Container)){throw 'Bundled online component licenses are missing.'}
    $licenseDestination=Join-Path $BuildDirectory 'network-licenses'
    New-Item -ItemType Directory -Path $licenseDestination -Force | Out-Null
    Get-ChildItem -LiteralPath $networkLicenses -File | Copy-Item -Destination $licenseDestination -Force
} else {
    Invoke-CheckedNative 'python' @((Join-Path $PSScriptRoot 'build-network.py'), '--parallel', "$Parallel", '--stage', $BuildDirectory)
}
Write-Output (Join-Path $BuildDirectory 'snoopyflyingace.exe')
