[CmdletBinding()]
param([string]$BootstrapScript='')
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$root=Split-Path -Parent $PSScriptRoot
if(!$BootstrapScript){$BootstrapScript=Join-Path $root 'launcher/bootstrap-tools.ps1'}
$work=Join-Path ([IO.Path]::GetTempPath()) ('redbaron-tool-tests-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $work | Out-Null
$originalPath=$env:PATH
$compiler=Join-Path $env:WINDIR 'Microsoft.NET/Framework64/v4.0.30319/csc.exe'
$source=Join-Path $work 'ToolStub.cs'
@'
using System;
using System.IO;
using System.Reflection;
class ToolStub {
    static int Main(string[] args) {
        string path = Assembly.GetExecutingAssembly().Location;
        if (Path.GetFileName(path) != "vswhere.exe" && (args.Length != 1 || args[0] != "--version")) return 2;
        Console.WriteLine(File.ReadAllText(path + ".txt"));
        return File.Exists(path + ".fail") ? 1 : 0;
    }
}
'@ | Set-Content -LiteralPath $source -Encoding UTF8
& $compiler /nologo /target:exe ('/out:'+(Join-Path $work 'ToolStub.exe')) $source
if($LASTEXITCODE){throw 'Could not compile tool inventory fixtures.'}
function New-Clang([string]$Name,[string]$Version,[string]$Target) {
    $directory=Join-Path $work $Name
    New-Item -ItemType Directory -Path $directory | Out-Null
    $path=Join-Path $directory 'clang++.exe'
    Copy-Item -LiteralPath (Join-Path $work 'ToolStub.exe') -Destination $path
    [IO.File]::WriteAllText(($path+'.txt'),"clang version $Version`r`nTarget: $Target`r`nThread model: posix")
    return $path
}
$catalog=Join-Path $work 'inventory.json'
. (Join-Path $root 'launcher/tool-inventory.ps1') -CatalogFile $catalog
$checks=0
try {
    foreach($case in @(
        @('msvc','23.1.4','x86_64-pc-windows-msvc',$true),
        @('msvc-versioned','20.1.0','x86_64-pc-windows-msvc19.44.35207',$true),
        @('mingw','22.1.8','x86_64-w64-windows-gnu',$false),
        @('linux','23.1.4','x86_64-unknown-linux-gnu',$false),
        @('arm','23.1.4','aarch64-pc-windows-msvc',$false),
        @('x86','23.1.4','i686-pc-windows-msvc',$false),
        @('old','19.1.8','x86_64-pc-windows-msvc',$false),
        @('unknown','23.1.4','',$false)
    )) {
        $candidate=New-Clang $case[0] $case[1] $case[2]
        $env:PATH=Split-Path -Parent $candidate
        $entry=Get-RedBaronToolInventory | Where-Object {$_.key -eq 'clang'}
        if($entry.available -ne $case[3]){throw ('Unexpected availability for '+$case[0])}
        $checks++;Write-Output ('PASS '+$case[0])
    }
    $mingw=Join-Path $work 'mingw/clang++.exe'
    $msvc=Join-Path $work 'msvc/clang++.exe'
    $env:PATH=Split-Path -Parent $mingw
    ConvertTo-Json -InputObject @(@{key='clang';path=$msvc}) | Set-Content -LiteralPath $catalog -Encoding UTF8
    $entry=Get-RedBaronToolInventory | Where-Object {$_.key -eq 'clang'}
    if(!$entry.available -or $entry.path -ne $msvc){throw 'Compatible cached Clang was not selected after incompatible PATH Clang.'}
    $checks++;Write-Output 'PASS compatible cache overrides incompatible PATH'
    $env:PATH=Split-Path -Parent $msvc
    ConvertTo-Json -InputObject @(@{key='clang';path=$mingw}) | Set-Content -LiteralPath $catalog -Encoding UTF8
    $entry=Get-RedBaronToolInventory | Where-Object {$_.key -eq 'clang'}
    if(!$entry.available -or $entry.path -ne $msvc){throw 'Compatible PATH Clang was not selected before incompatible cache.'}
    $checks++;Write-Output 'PASS compatible PATH overrides incompatible cache'
    $env:PATH=Split-Path -Parent $mingw
    $entry=Get-RedBaronToolInventory | Where-Object {$_.key -eq 'clang'}
    if($entry.available){throw 'Incompatible cached Clang was accepted.'}
    $checks++;Write-Output 'PASS incompatible PATH and cache rejected'
    $env:PATH=Split-Path -Parent $msvc
    [IO.File]::WriteAllText(($msvc+'.fail'),'')
    $entry=Get-RedBaronToolInventory | Where-Object {$_.key -eq 'clang'}
    if($entry.available){throw 'Failed compiler command was accepted.'}
    $checks++;Write-Output 'PASS failed compiler command rejected'
    Remove-Item -LiteralPath ($msvc+'.fail')

    # Exercise the real offline bootstrap with the user's failure condition:
    # a recent MinGW compiler on PATH and in the shared tool cache. Stop before
    # Microsoft installation using an intentionally absent VS offline layout.
    foreach($tool in @(@('cmake','cmake version 4.4.5'),@('ninja','1.13.2'))) {
        $path=Join-Path (Split-Path -Parent $mingw) ($tool[0]+'.exe')
        Copy-Item -LiteralPath (Join-Path $work 'ToolStub.exe') -Destination $path
        [IO.File]::WriteAllText(($path+'.txt'),$tool[1])
    }
    $offline=Join-Path $work 'offline'
    $offlineBin=Join-Path $offline 'portable/llvm/bin'
    New-Item -ItemType Directory -Path $offlineBin -Force | Out-Null
    Copy-Item -LiteralPath $msvc,($msvc+'.txt') -Destination $offlineBin
    $entries=@(foreach($file in Get-ChildItem -LiteralPath $offlineBin -File) {
        @{path=('portable/llvm/bin/'+$file.Name);size=$file.Length;sha256=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash}
    })
    $manifest=Join-Path $offline 'manifest.json'
    @{version=1;files=$entries} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $manifest -Encoding UTF8
    $env:PATH=Split-Path -Parent $mingw
    $programFilesX86=${env:ProgramFiles(x86)}
    $toolRoot=Join-Path $work 'bootstrap-cache'
    $bootstrapError=''
    try {
        ${env:ProgramFiles(x86)}=$work
        & $BootstrapScript -ToolRoot $toolRoot -OfflineToolsRoot $offline -OfflineManifest $manifest -InventoryFile $catalog | Out-Null
    } catch {$bootstrapError=$_.Exception.Message}
    finally {${env:ProgramFiles(x86)}=$programFilesX86}
    if($bootstrapError -ne 'The offline Microsoft Build Tools layout is missing.') {
        throw ('Unexpected bootstrap result: '+$bootstrapError)
    }
    $selected=Join-Path $toolRoot 'portable/llvm/bin/clang++.exe'
    if(!(Test-Path -LiteralPath $selected) -or (Get-Command clang++).Source -ne $selected) {
        throw 'Offline bootstrap did not replace incompatible Clang with the verified bundled tool.'
    }
    $checks++;Write-Output 'PASS offline bootstrap selects bundled MSVC Clang over PATH/cache MinGW without downloading'

    # Complete the bootstrap through VsDevCmd and the final inventory. A shared
    # tools directory can contain working CMake/Ninja and an incompatible Clang.
    # Reordering their directories after selection must not reselect that Clang.
    $fakeProgramFiles=Join-Path $work 'Program Files (x86)'
    $fakeVs=Join-Path $work 'Visual Studio'
    $fakeSdk=Join-Path $work 'Windows Kits/10'
    foreach($file in @(
        (Join-Path $fakeProgramFiles 'Microsoft Visual Studio/Installer/vswhere.exe'),
        (Join-Path $fakeVs 'VC/Auxiliary/Build/Microsoft.VCToolsVersion.default.txt'),
        (Join-Path $fakeVs 'VC/Tools/MSVC/14.51.36231/bin/Hostx64/x64/cl.exe'),
        (Join-Path $fakeVs 'VC/Tools/MSVC/14.51.36231/include/vector'),
        (Join-Path $fakeVs 'VC/Tools/MSVC/14.51.36231/lib/x64/msvcprt.lib'),
        (Join-Path $fakeVs 'Common7/Tools/VsDevCmd.bat'),
        (Join-Path $fakeSdk 'Include/10.0.28000.0/um/Windows.h'),
        (Join-Path $fakeSdk 'Lib/10.0.28000.0/um/x64/kernel32.lib'),
        (Join-Path $fakeSdk 'Lib/10.0.28000.0/ucrt/x64/ucrt.lib'),
        (Join-Path $fakeSdk 'bin/10.0.28000.0/x64/rc.exe')
    )) {
        New-Item -ItemType Directory -Path (Split-Path -Parent $file) -Force | Out-Null
        [IO.File]::WriteAllText($file,'fixture')
    }
    $vswhereFixture=Join-Path $fakeProgramFiles 'Microsoft Visual Studio/Installer/vswhere.exe'
    Copy-Item -LiteralPath (Join-Path $work 'ToolStub.exe') -Destination $vswhereFixture -Force
    [IO.File]::WriteAllText(($vswhereFixture+'.txt'),$fakeVs)
    [IO.File]::WriteAllText((Join-Path $fakeVs 'VC/Auxiliary/Build/Microsoft.VCToolsVersion.default.txt'),'14.51.36231')
    function Get-ItemProperty {
        param($Path,$Name,$ErrorAction)
        if($Path -ne 'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows Kits\Installed Roots' -or $Name -ne 'KitsRoot10'){throw 'Unexpected registry fixture request.'}
        return [pscustomobject]@{KitsRoot10=$fakeSdk}
    }
    $sharedBin=Split-Path -Parent $mingw
    [IO.File]::WriteAllText((Join-Path $fakeVs 'Common7/Tools/VsDevCmd.bat'),('@set "PATH='+$sharedBin+';'+(Join-Path $fakeSdk 'bin/10.0.28000.0/x64')+';%PATH%"'+"`r`n@exit /b 0`r`n"))
    try {
        ${env:ProgramFiles(x86)}=$fakeProgramFiles
        foreach($mode in @('offline','cached')) {
            $env:PATH=$sharedBin
            $fullCatalog=Join-Path $work ($mode+'-full-inventory.json')
            if($mode -eq 'cached') {
                ConvertTo-Json -InputObject @(@{key='clang';path=$msvc}) | Set-Content -LiteralPath $fullCatalog -Encoding UTF8
            }
            $arguments=@{ToolRoot=(Join-Path $work ($mode+'-full-cache'));InventoryFile=$fullCatalog}
            if($mode -eq 'offline'){$arguments.OfflineToolsRoot=$offline;$arguments.OfflineManifest=$manifest}
            $bootstrapOutput=@(& $BootstrapScript @arguments)
            $expectedClang=if($mode -eq 'offline'){Join-Path $arguments.ToolRoot 'portable/llvm/bin/clang++.exe'}else{$msvc}
            $stored=Get-Content -LiteralPath $fullCatalog -Raw | ConvertFrom-Json
            if(@($stored | Where-Object {!$_.available}).Count -or (Get-Command clang++).Source -ne $expectedClang -or ($stored | Where-Object {$_.key -eq 'clang'}).path -ne $expectedClang) {
                throw ('Full '+$mode+' bootstrap lost the selected compatible compiler. Expected: '+$expectedClang+'; command: '+(Get-Command clang++).Source+'; catalog: '+($stored | Where-Object {$_.key -eq 'clang'}).path)
            }
            if($bootstrapOutput -notcontains 'Build tools are ready.'){throw 'Full bootstrap did not finish.'}
            $checks++;Write-Output ('PASS full '+$mode+' bootstrap preserves MSVC Clang with shared CMake/Ninja/MinGW directory and imported VS environment')
        }
    } finally {
        ${env:ProgramFiles(x86)}=$programFilesX86
        Remove-Item Function:\Get-ItemProperty
    }
} finally {$env:PATH=$originalPath}
Write-Output ("Passed $checks tool inventory checks. Fixtures: $work")
