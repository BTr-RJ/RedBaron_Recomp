[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$SdkSource,
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
} finally {Pop-Location}
Native 'cmake' @('-S',$source,'-B',$build,'-G','Ninja','-DCMAKE_BUILD_TYPE=Release',
    '-DCMAKE_C_COMPILER=clang','-DCMAKE_CXX_COMPILER=clang++','-DCMAKE_C_FLAGS=-march=x86-64-v2',
    '-DCMAKE_CXX_FLAGS=-march=x86-64-v2','-DREXGLUE_ENABLE_SPATIAL_FSR=ON','-DREXGLUE_ENABLE_FIDELITYFX=OFF',
    ('-DCMAKE_INSTALL_PREFIX='+$install))
Native 'cmake' @('--build',$build,'--parallel',"$Parallel")
Native 'cmake' @('--install',$build)
Write-Output "Video SDK ready: $install"
Write-Output 'Select this installed SDK in the launcher when installing the game. Existing installations are unchanged.'
