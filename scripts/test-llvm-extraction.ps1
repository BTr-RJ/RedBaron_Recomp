[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$CMakePath, [string]$BootstrapScript='')
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$root=Split-Path -Parent $PSScriptRoot
if(!$BootstrapScript){$BootstrapScript=Join-Path $root 'launcher/bootstrap-tools.ps1'}
$CMakePath=(Get-Item -LiteralPath $CMakePath).FullName
$work=Join-Path ([IO.Path]::GetTempPath()) ('redbaron-llvm-tests-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $work | Out-Null
$originalPath=$env:PATH
$originalLocation=Get-Location
. (Join-Path $root 'launcher/install-progress.ps1')

# Execute the production missing-Clang branch, mocking only the verified
# download. The real CMake creates and extracts real XZ archives without a
# network transfer or a Microsoft installer. No duplicate extraction logic.
$tokens=$null;$parseErrors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile($BootstrapScript,[ref]$tokens,[ref]$parseErrors)
if($parseErrors.Count){throw 'Bootstrap script has parse errors.'}
$branches=@($ast.FindAll({param($node)
    $node -is [Management.Automation.Language.IfStatementAst] -and
    $node.Clauses[0].Item1.Extent.Text -eq '!$clangOk'
},$true))
if($branches.Count -ne 1){throw 'Could not locate the missing-Clang bootstrap branch.'}
$branch=[scriptblock]::Create($branches[0].Extent.Text)
$clangOk=$false
$compilerFolder='clang+llvm-23.1.4-x86_64-pc-windows-msvc'
$relativeCompiler=$compilerFolder+'/bin/clang++.exe'
function Download([string]$url,[string]$hash,[string]$name) {
    if($name -ne 'llvm.tar.xz'){throw 'Unexpected tool download.'}
    return $script:fixtureArchive
}
function Assert([bool]$condition,[string]$message) {if(!$condition){throw $message}}
$checks=0
try {
    $fixtures=Join-Path $work 'fixture source'
    $fixtureBin=Join-Path $fixtures ($compilerFolder+'/bin')
    New-Item -ItemType Directory -Path $fixtureBin -Force | Out-Null
    [IO.File]::WriteAllText((Join-Path $fixtureBin 'clang++.exe'),'compiler fixture bytes')
    $goodArchive=Join-Path $work 'valid llvm.tar.xz'
    Set-Location -LiteralPath $fixtures
    & $CMakePath -E tar cJf $goodArchive $compilerFolder
    if($LASTEXITCODE){throw 'Could not create XZ fixture.'}
    Set-Location -LiteralPath $originalLocation.Path
    $magic=[IO.File]::ReadAllBytes($goodArchive)
    Assert ([BitConverter]::ToString($magic,0,6) -eq 'FD-37-7A-58-5A-00') 'Fixture is not XZ compressed.'

    # Exclude Windows/System32, Git, Python and any external xz from PATH.
    $env:PATH=Split-Path -Parent $CMakePath
    Assert (!(Get-Command tar.exe,xz.exe -CommandType Application -ErrorAction SilentlyContinue)) 'tar or xz unexpectedly present on the test PATH.'
    $script:fixtureArchive=$goodArchive
    $ToolRoot=Join-Path $work 'fresh cache with spaces'
    . $branch
    $expectedCompiler=Join-Path (Join-Path $ToolRoot 'llvm') $relativeCompiler
    Assert (([IO.File]::ReadAllText($expectedCompiler)) -eq 'compiler fixture bytes') 'XZ extraction changed compiler bytes.'
    Assert ((Get-Command clang++ -CommandType Application).Source -eq $expectedCompiler) 'Extracted compiler was not selected.'
    Assert ((Get-Location).Path -eq $originalLocation.Path) 'Extraction changed the caller working directory.'
    $checks++;Write-Output 'PASS real XZ extraction without tar/xz on PATH, spaces, compiler selection and working directory'

    [IO.File]::WriteAllText($expectedCompiler,'existing compiler preserved')
    . $branch
    Assert (([IO.File]::ReadAllText($expectedCompiler)) -eq 'existing compiler preserved') 'Existing compiler was overwritten.'
    $checks++;Write-Output 'PASS complete tool folder reused without overwriting'

    # Reproduce a tar.exe found on PATH that cannot initialize its XZ filter.
    $badTarFolder=Join-Path $work 'broken tar on PATH'
    New-Item -ItemType Directory -Path $badTarFolder | Out-Null
    $badTarSource=Join-Path $work 'BrokenTar.cs'
    @'
using System;
using System.IO;
using System.Reflection;
class BrokenTar {
    static int Main() {
        File.WriteAllText(Assembly.GetExecutingAssembly().Location + ".called", "called");
        Console.Error.WriteLine("tar.exe: Error opening archive: Can't initialize filter; unable to run program xz -d -qq");
        return 1;
    }
}
'@ | Set-Content -LiteralPath $badTarSource -Encoding UTF8
    $csc=Join-Path $env:WINDIR 'Microsoft.NET/Framework64/v4.0.30319/csc.exe'
    & $csc /nologo /target:exe ('/out:'+(Join-Path $badTarFolder 'tar.exe')) $badTarSource
    if($LASTEXITCODE){throw 'Could not create broken tar fixture.'}
    $env:PATH=$badTarFolder+';'+(Split-Path -Parent $CMakePath)
    $ToolRoot=Join-Path $work 'broken tar cache'
    . $branch
    Assert (Test-Path -LiteralPath (Join-Path (Join-Path $ToolRoot 'llvm') $relativeCompiler)) 'Broken PATH tar prevented extraction.'
    Assert (!(Test-Path -LiteralPath (Join-Path $badTarFolder 'tar.exe.called'))) 'Bootstrap invoked the broken PATH tar.'
    $checks++;Write-Output 'PASS broken PATH tar requiring external xz is never invoked'

    $ToolRoot=Join-Path $work 'incomplete cache'
    $incomplete=Join-Path $ToolRoot 'llvm'
    New-Item -ItemType Directory -Path $incomplete -Force | Out-Null
    $sentinel=Join-Path $incomplete 'keep.txt'
    [IO.File]::WriteAllText($sentinel,'preserve me')
    $failure=''
    try {. $branch}catch{$failure=$_.Exception.Message}
    Assert ($failure -eq "Incomplete tool folder preserved: $incomplete") 'Incomplete tool folder was accepted.'
    Assert (([IO.File]::ReadAllText($sentinel)) -eq 'preserve me') 'Incomplete tool folder was changed.'
    $checks++;Write-Output 'PASS incomplete tool folder rejected and preserved'

    $script:fixtureArchive=Join-Path $work 'corrupt llvm.tar.xz'
    [IO.File]::WriteAllText($script:fixtureArchive,'not an archive')
    $ToolRoot=Join-Path $work 'corrupt archive cache'
    $failure=''
    try {. $branch}catch{$failure=$_.Exception.Message}
    Assert ($failure -like 'LLVM extraction failed; work retained at *') 'Corrupt archive was accepted or lost its diagnostic.'
    Assert (Test-Path -LiteralPath (Join-Path $ToolRoot 'llvm') -PathType Container) 'Failed extraction folder was not retained.'
    Assert (Test-Path -LiteralPath $script:fixtureArchive -PathType Leaf) 'Failed archive was removed.'
    $checks++;Write-Output 'PASS corrupt archive fails with retained diagnostics'

    $script:fixtureArchive=Join-Path $work 'missing compiler.tar.xz'
    Set-Location -LiteralPath $fixtures
    [IO.File]::WriteAllText((Join-Path $fixtures 'readme.txt'),'no compiler')
    & $CMakePath -E tar cJf $script:fixtureArchive readme.txt
    if($LASTEXITCODE){throw 'Could not create incomplete XZ fixture.'}
    Set-Location -LiteralPath $originalLocation.Path
    $ToolRoot=Join-Path $work 'missing compiler cache'
    $failure=''
    try {. $branch}catch{$failure=$_.Exception.Message}
    Assert ($failure -like 'LLVM extraction failed; work retained at *') 'Archive without clang++ was accepted.'
    $checks++;Write-Output 'PASS successful extraction without required compiler rejected'
} finally {
    $env:PATH=$originalPath
    Set-Location -LiteralPath $originalLocation.Path
}
Write-Output ("Passed $checks LLVM extraction checks. Fixtures: $work")
