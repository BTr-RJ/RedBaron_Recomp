[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$Source,
    [Parameter(Mandatory=$true)][string]$Destination,
    [Parameter(Mandatory=$true)][string]$Sdk,
    [Parameter(Mandatory=$true)][string]$SourceArchive,
    [Parameter(Mandatory=$true)][string]$LauncherExe,
    [string]$SdkArchive = '',
    [string]$BootstrapScript = '',
    [string]$LauncherConfig = '',
    [string]$OnlineConfig = '',
    [string]$OfflineToolsRoot = '', [string]$OfflineManifest = ''
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
[Threading.Thread]::CurrentThread.CurrentUICulture = [Globalization.CultureInfo]::GetCultureInfo('en-US')
. (Join-Path $PSScriptRoot 'install-progress.ps1')
Write-RBProgress 'Checking game files'

function Full-Path([string]$Value) { [IO.Path]::GetFullPath($Value).TrimEnd('\','/') }
function Is-Within([string]$Child,[string]$Parent) {
    $Child.Equals($Parent,[StringComparison]::OrdinalIgnoreCase) -or
    $Child.StartsWith($Parent + '\',[StringComparison]::OrdinalIgnoreCase)
}
function Assert-NoReparse([string]$Path) {
    $current = $Path
    while ($current) {
        if (Test-Path -LiteralPath $current) {
            if ((Get-Item -LiteralPath $current -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) {
                throw "Links and junctions are not supported during installation: $current"
            }
        }
        $parent = Split-Path -Parent $current
        if ($parent -eq $current) { break }
        $current = $parent
    }
}
function Read-BE32([byte[]]$Data,[int]$Offset) {
    if ($Offset -lt 0 -or $Offset + 4 -gt $Data.Length) { throw 'Incomplete XEX header.' }
    [uint32](([uint64]$Data[$Offset] -shl 24) -bor ([uint64]$Data[$Offset+1] -shl 16) -bor ([uint64]$Data[$Offset+2] -shl 8) -bor $Data[$Offset+3])
}
function Validate-Game([string]$Folder) {
    foreach($item in @('default.xex','Config','Packages')) {
        if(!(Test-Path -LiteralPath (Join-Path $Folder $item))){throw "Incomplete game files: missing $item"}
    }
    $header=[IO.File]::ReadAllBytes((Join-Path $Folder 'default.xex'))
    if($header.Length -lt 24 -or [Text.Encoding]::ASCII.GetString($header,0,4) -ne 'XEX2'){throw 'default.xex is not a valid XEX2 file.'}
    $count=Read-BE32 $header 20
    if($count -gt 4096){throw 'Invalid XEX header table.'}
    $title=[uint32]0
    for($i=0;$i -lt $count;$i++) {
        if((Read-BE32 $header (24+8*$i)) -eq 0x00040006){$offset=Read-BE32 $header (28+8*$i);$title=Read-BE32 $header ([int]$offset+12)}
    }
    if($title -ne 0x584109B5){throw ('Incompatible title: {0:X8}; expected 584109B5.' -f $title)}
}
function Package-Magic([string]$Path) {
    $stream=[IO.File]::OpenRead($Path)
    try {$bytes=New-Object byte[] 4;if($stream.Read($bytes,0,4) -ne 4){return $false};return [Text.Encoding]::ASCII.GetString($bytes) -in @('LIVE','PIRS','CON ')}finally{$stream.Dispose()}
}

$Source = Full-Path $Source
$Destination = Full-Path $Destination
$bundledSdk=!([string]::IsNullOrWhiteSpace($SdkArchive))
if($bundledSdk) {
    if(!(Test-Path -LiteralPath $SdkArchive -PathType Leaf)){throw 'The bundled SDK archive is missing.'}
    $Sdk=Join-Path $Destination 'sdk_experimental'
} else {$Sdk=Full-Path $Sdk}
if ((Is-Within $Destination $Source) -or (Is-Within $Source $Destination) -or
    (!$bundledSdk -and ((Is-Within $Destination $Sdk) -or (Is-Within $Sdk $Destination)))) {
    throw 'Choose a destination folder separate from the source files and SDK.'
}
Assert-NoReparse $Source
Assert-NoReparse $Destination
if ((Test-Path -LiteralPath $Destination) -and @(Get-ChildItem -LiteralPath $Destination -Force).Count) {
    throw 'The destination must be empty. Choose another folder to preserve existing installations and saves.'
}
$packages=New-Object 'System.Collections.Generic.List[string]'
if(Test-Path -LiteralPath $Source -PathType Leaf) {
    if([IO.Path]::GetFileName($Source) -ieq 'default.xex'){$Source=Split-Path -Parent $Source}
    elseif(Package-Magic $Source){$packages.Add($Source)}
    else {throw 'Select default.xex, an extracted game folder, or a compatible Xbox 360 STFS game package.'}
} elseif(!(Test-Path -LiteralPath $Source -PathType Container)){throw 'The selected game source does not exist.'}
Assert-NoReparse $Source
if((Is-Within $Destination $Source) -or (Is-Within $Source $Destination)){throw 'Choose a destination folder separate from the game source.'}
if(!$bundledSdk) {
    foreach ($file in @('bin/rexglue.exe','bin/rexruntime.dll','bin/rexgpu-xenos.dll','lib/cmake/rexglue/rexglueConfig.cmake')) {
        if (!(Test-Path -LiteralPath (Join-Path $Sdk $file))) { throw "Incomplete SDK: $file" }
    }
}

# Enumerate before creating the destination; never follow source links.
$files = New-Object 'System.Collections.Generic.List[System.IO.FileInfo]'
$queue = New-Object 'System.Collections.Generic.Queue[string]'
if(!$packages.Count){$queue.Enqueue($Source)}
$extracted=!(Test-Path -LiteralPath $Source -PathType Leaf) -and (Test-Path -LiteralPath (Join-Path $Source 'default.xex'))
if($extracted){Validate-Game $Source}
while ($queue.Count) {
    foreach ($item in Get-ChildItem -LiteralPath $queue.Dequeue() -Force) {
        if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Link found in source folder: $($item.FullName)" }
        if ($item.PSIsContainer) { $queue.Enqueue($item.FullName) }
        elseif($extracted){$files.Add($item)}
        elseif(($item.Name -match '^[0-9A-Fa-f]{32,42}$' -or $item.Extension -in @('.stfs','.con','.live','.pirs')) -and (Package-Magic $item.FullName)){$packages.Add($item.FullName)}
    }
}
if(!$extracted -and !$packages.Count){throw 'No game files or compatible STFS packages found. Select the package file directly or the folder containing default.xex.'}
$bytes=if($extracted){($files | Measure-Object Length -Sum).Sum}else{($packages | ForEach-Object {(Get-Item -LiteralPath $_).Length} | Measure-Object -Sum).Sum}
$drive = New-Object IO.DriveInfo ([IO.Path]::GetPathRoot($Destination))
$workingSpace=if($BootstrapScript -and !(Get-Command clang++ -ErrorAction SilentlyContinue)){10GB}else{4GB}
if ($drive.AvailableFreeSpace -lt $bytes + $workingSpace) { throw 'Not enough free space. Allow room for the game, build files and the compiler download.' }
New-Item -ItemType Directory -Path $Destination -Force | Out-Null
$marker = Join-Path $Destination '.redbaron-install.json'
@{status='installing';titleId='584109B5';started=(Get-Date).ToString('o')} | ConvertTo-Json | Set-Content -LiteralPath $marker -Encoding UTF8
try {
    Write-Output 'Preparing port source files...'
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    Expand-RBArchive $SourceArchive $Destination 'Preparing port files'
    if($bundledSdk) {
        Write-Output 'Unpacking the included ReXGlue SDK and online components...'
        Expand-RBArchive $SdkArchive $Sdk 'Unpacking SDK'
    }
    foreach ($file in @('bin/rexglue.exe','bin/rexruntime.dll','bin/rexgpu-xenos.dll','lib/cmake/rexglue/rexglueConfig.cmake')) {
        if (!(Test-Path -LiteralPath (Join-Path $Sdk $file))) { throw "Incomplete SDK: $file" }
    }
    if($BootstrapScript) {
        Write-RBProgress 'Checking build tools'
        Write-Output 'Checking build tools...'
        $needPython=!(Test-Path -LiteralPath (Join-Path $Sdk 'bin/redbaron_net.exe') -PathType Leaf) -or !(Test-Path -LiteralPath (Join-Path $Sdk 'bin/datachannel.dll') -PathType Leaf)
        & $BootstrapScript -ToolRoot (Join-Path $Destination '.redbaron-tools') -RequirePython:$needPython -OfflineToolsRoot $OfflineToolsRoot -OfflineManifest $OfflineManifest
    }
    foreach ($tool in @('cmake','ninja','clang++')) { Get-Command $tool -ErrorAction Stop | Out-Null }
    if(!$extracted) {
        $inspector=Join-Path $Sdk 'bin/redbaron_content_inspect.exe'
        if(!(Test-Path -LiteralPath $inspector -PathType Leaf)){throw 'The bundled package extractor is missing. Use a newer installer or an extracted game folder.'}
        $compatiblePackages=New-Object 'System.Collections.Generic.List[string]'
        foreach($package in $packages) {
            $output=& $inspector $package 2>$null
            if($LASTEXITCODE -eq 0 -and ($output -join ' ') -match 'title=584109B5 type=000D0000'){$compatiblePackages.Add($package)}
        }
        if($compatiblePackages.Count -ne 1){throw 'Select exactly one Snoopy Flying Ace Arcade game package. Unrelated packages are not installed.'}
        Write-Output 'Extracting the selected Xbox 360 game package...'
        & $inspector --extract-game (Join-Path $Destination 'game') $compatiblePackages[0]
        if($LASTEXITCODE){throw 'Game package extraction failed. The original package was preserved.'}
        Validate-Game (Join-Path $Destination 'game')
    }
    if($extracted){Write-Output ('Copying {0} game files...' -f $files.Count)}
    $index=0
    [long]$copied=0
    if($extracted){Write-RBProgress 'Copying game files' 0 $bytes}
    foreach ($file in $files) {
        $relative=$file.FullName.Substring($Source.Length).TrimStart('\','/')
        $target=Join-Path (Join-Path $Destination 'game') $relative
        New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
        Copy-Item -LiteralPath $file.FullName -Destination $target
        $copied+=$file.Length;Write-RBProgress 'Copying game files' $copied $bytes
        $index++
        if ($index % 100 -eq 0) { Write-Output ('Files: {0}/{1}' -f $index,$files.Count) }
    }
    Write-Output 'Generating code and building (this may take several minutes)...'
    Write-RBProgress 'Generating game code'
    & (Join-Path $Destination 'scripts/build.ps1') -SdkRoot $Sdk -Parallel 4
    if (!(Test-Path -LiteralPath (Join-Path $Destination 'build_cpp_release/snoopyflyingace.exe'))) { throw 'The build did not produce the game executable.' }
    if (!(Test-Path -LiteralPath (Join-Path $Destination 'build_cpp_release/redbaron_probe.exe'))) { throw 'The hardware detector is missing after the build.' }
    Write-RBProgress 'Finishing game installation'
    if($OnlineConfig) {
        if(!(Test-Path -LiteralPath (Join-Path $Destination 'build_cpp_release/EOSSDK-Win64-Shipping.dll'))){throw 'The online runtime is missing after the build.'}
        $onlineDirectory=Join-Path $Destination 'build_cpp_release/eos'
        New-Item -ItemType Directory -Path $onlineDirectory -Force | Out-Null
        Copy-Item -LiteralPath $OnlineConfig -Destination (Join-Path $onlineDirectory 'config.ini')
        Write-Output 'Online configuration installed. Matchmaking is available from the game menu.'
    }
    Copy-Item -LiteralPath $LauncherExe -Destination (Join-Path $Destination 'RedBaron_Launcher.exe')
    if(!$LauncherConfig){$LauncherConfig=$LauncherExe+'.config'}
    if(!(Test-Path -LiteralPath $LauncherConfig -PathType Leaf)){throw 'Launcher configuration resource is missing.'}
    Copy-Item -LiteralPath $LauncherConfig -Destination (Join-Path $Destination 'RedBaron_Launcher.exe.config')
    @{status='ready';titleId='584109B5';completed=(Get-Date).ToString('o')} | ConvertTo-Json | Set-Content -LiteralPath $marker -Encoding UTF8
    Write-Output 'Installation complete.'
} catch {
    @{status='failed';error=$_.Exception.Message} | ConvertTo-Json | Set-Content -LiteralPath $marker -Encoding UTF8
    throw
}
