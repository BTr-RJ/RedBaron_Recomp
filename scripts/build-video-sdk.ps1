[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$SdkSource,
    [switch]$Fsr315,
    [switch]$Dlss,
    [string]$DlssSdk = '',
    [string]$FidelityFxArchive = '',
    [ValidateRange(1,64)][int]$Parallel=4
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$project=Split-Path -Parent $PSScriptRoot
$original=[IO.Path]::GetFullPath($SdkSource)
if(!(Test-Path -LiteralPath (Join-Path $original 'src/ui/presenter.cpp'))) {throw 'Provide the compatible ReXGlue SDK source checkout (including submodules).'}
$job=Join-Path $project ('video-sdk-work/'+(Get-Date -Format 'yyyyMMdd_HHmmss_fff'))
if($job.StartsWith($original.TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)) {
    throw 'The isolated build folder must be outside the original SDK source tree.'
}
$source=Join-Path $job 'source'
$build=Join-Path $job 'build'
$install=Join-Path $job 'install'
$fidelityFx=''
if ($Dlss) { $Fsr315 = $true }
$dlssRoot=''
if ($Dlss) {
    $dlssRoot = & (Join-Path $PSScriptRoot 'prepare-dlss.ps1') -Destination (Join-Path $job 'dlss') -Source $DlssSdk
}
if($Fsr315) {
    $fidelityFx = & (Join-Path $PSScriptRoot 'prepare-fsr315.ps1') -Archive $FidelityFxArchive -Destination (Join-Path $job 'fidelityfx')
}
New-Item -ItemType Directory -Path $source -Force | Out-Null
# Work on an isolated copy. Never patch or remove anything in the original SDK.
& robocopy $original $source /E /XD .git out build /XF *.bak /R:1 /W:1 /NJH /NJS /NFL /NDL
if($LASTEXITCODE -ge 8) {throw 'Could not copy the SDK source.'}
function Native([string]$Program,[string[]]$Arguments) {
    & $Program @Arguments
    if($LASTEXITCODE -ne 0) {throw "$Program failed with exit code $LASTEXITCODE. Work retained at $job"}
}
$patch=Join-Path $project 'patches/rexglue-spatial-fsr-vsync.patch'
Push-Location $source
try {
    Native 'git' @('init','--quiet')
    Native 'git' @('apply','--check','--ignore-space-change',$patch)
    Native 'git' @('apply','--ignore-space-change',$patch)
    if($Fsr315) {
        $temporalPatch=Join-Path $project 'patches/rexglue-fsr315-preview.patch'
        Native 'git' @('apply','--check','--ignore-space-change',$temporalPatch)
        Native 'git' @('apply','--ignore-space-change',$temporalPatch)
        $videoPatch=Join-Path $project 'patches/rexglue-video-pacing-hdr-dlss.patch'
        Native 'git' @('apply','--check','--ignore-space-change',$videoPatch)
        Native 'git' @('apply','--ignore-space-change',$videoPatch)
        $modesPatch=Join-Path $project 'patches/rexglue-dlss-modes-hdr-cursor.patch'
        Native 'git' @('apply','--check','--ignore-space-change',$modesPatch)
        Native 'git' @('apply','--ignore-space-change',$modesPatch)
    }
    $profilesPatch=Join-Path $project 'patches/rexglue-local-profiles-lan.patch'
    Native 'git' @('apply','--check','--ignore-space-change',$profilesPatch)
    Native 'git' @('apply','--ignore-space-change',$profilesPatch)
} finally {Pop-Location}
Native 'cmake' @('-S',$source,'-B',$build,'-G','Ninja','-DCMAKE_BUILD_TYPE=Release',
    '-DCMAKE_C_COMPILER=clang','-DCMAKE_CXX_COMPILER=clang++','-DCMAKE_C_FLAGS=-march=x86-64-v2',
    '-DCMAKE_CXX_FLAGS=-march=x86-64-v2','-DREXGLUE_ENABLE_SPATIAL_FSR=ON','-DREXGLUE_ENABLE_FIDELITYFX=OFF',
    ('-DCMAKE_INSTALL_PREFIX='+$install),('-DREDBARON_FFX315_ROOT='+$fidelityFx),('-DREDBARON_DLSS_ROOT='+$dlssRoot))
Native 'cmake' @('--build',$build,'--parallel',"$Parallel")
Native 'cmake' @('--install',$build)
if($Fsr315) {
    Copy-Item -LiteralPath (Join-Path $fidelityFx 'signedbin/amd_fidelityfx_upscaler_dx12.dll') -Destination (Join-Path $install 'bin')
    Copy-Item -LiteralPath (Join-Path $fidelityFx 'FidelityFX-LICENSE.txt') -Destination (Join-Path $install 'bin')
    Write-Output 'FSR 3.1.5 Super Resolution preview enabled. Object motion and HUD composition remain experimental; frame generation is not included.'
}
if($Dlss) {
    Copy-Item -LiteralPath (Join-Path $dlssRoot 'lib/Windows_x86_64/rel/nvngx_dlss.dll') -Destination (Join-Path $install 'bin')
    Copy-Item -LiteralPath (Join-Path $dlssRoot 'LICENSE.txt') -Destination (Join-Path $install 'bin/NVIDIA-DLSS-LICENSE.txt')
    Write-Output 'DLSS Super Resolution preview enabled. NVIDIA GPU/driver capability is checked at runtime. No frame generation.'
}
Write-Output "Video SDK ready: $install"
Write-Output 'Select this installed SDK in the launcher when installing the game. Existing installations are unchanged.'
