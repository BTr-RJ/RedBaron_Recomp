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
    [string]$OnlineConfig = ''
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
[Threading.Thread]::CurrentThread.CurrentUICulture = [Globalization.CultureInfo]::GetCultureInfo('en-US')

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
foreach ($item in @('default.xex','Config','Packages')) {
    if (!(Test-Path -LiteralPath (Join-Path $Source $item))) { throw "Incomplete source: missing $item" }
}
$header = [IO.File]::ReadAllBytes((Join-Path $Source 'default.xex'))
if ([Text.Encoding]::ASCII.GetString($header,0,4) -ne 'XEX2') { throw 'default.xex is not a valid XEX2 file.' }
$count = Read-BE32 $header 20
if ($count -gt 4096) { throw 'Invalid XEX header table.' }
$titleId = [uint32]0
for ($i=0; $i -lt $count; $i++) {
    if ((Read-BE32 $header (24+8*$i)) -eq 0x00040006) {
        $offset = Read-BE32 $header (28+8*$i)
        $titleId = Read-BE32 $header ([int]$offset+12)
    }
}
if ($titleId -ne 0x584109B5) { throw ('Incompatible title: {0:X8}; expected 584109B5.' -f $titleId) }
if(!$bundledSdk) {
    foreach ($file in @('bin/rexglue.exe','bin/rexruntime.dll','bin/rexgpu-xenos.dll','lib/cmake/rexglue/rexglueConfig.cmake')) {
        if (!(Test-Path -LiteralPath (Join-Path $Sdk $file))) { throw "Incomplete SDK: $file" }
    }
}

# Enumerate before creating the destination; never follow source links.
$files = New-Object 'System.Collections.Generic.List[System.IO.FileInfo]'
$queue = New-Object 'System.Collections.Generic.Queue[string]'
$queue.Enqueue($Source)
while ($queue.Count) {
    foreach ($item in Get-ChildItem -LiteralPath $queue.Dequeue() -Force) {
        if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Link found in source folder: $($item.FullName)" }
        if ($item.PSIsContainer) { $queue.Enqueue($item.FullName) } else { $files.Add($item) }
    }
}
$bytes = ($files | Measure-Object Length -Sum).Sum
$drive = New-Object IO.DriveInfo ([IO.Path]::GetPathRoot($Destination))
$workingSpace=if($BootstrapScript -and !(Get-Command clang++ -ErrorAction SilentlyContinue)){10GB}else{4GB}
if ($drive.AvailableFreeSpace -lt $bytes + $workingSpace) { throw 'Not enough free space. Allow room for the game, build files and the compiler download.' }
New-Item -ItemType Directory -Path $Destination -Force | Out-Null
$marker = Join-Path $Destination '.redbaron-install.json'
@{status='installing';titleId='584109B5';started=(Get-Date).ToString('o')} | ConvertTo-Json | Set-Content -LiteralPath $marker -Encoding UTF8
try {
    Write-Output 'Preparing port source files...'
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [IO.Compression.ZipFile]::ExtractToDirectory($SourceArchive,$Destination)
    if($bundledSdk) {
        Write-Output 'Unpacking the included ReXGlue SDK and online components...'
        [IO.Compression.ZipFile]::ExtractToDirectory($SdkArchive,$Sdk)
    }
    foreach ($file in @('bin/rexglue.exe','bin/rexruntime.dll','bin/rexgpu-xenos.dll','lib/cmake/rexglue/rexglueConfig.cmake')) {
        if (!(Test-Path -LiteralPath (Join-Path $Sdk $file))) { throw "Incomplete SDK: $file" }
    }
    if($BootstrapScript) {
        Write-Output 'Checking build tools...'
        $needPython=!(Test-Path -LiteralPath (Join-Path $Sdk 'bin/redbaron_net.exe') -PathType Leaf) -or !(Test-Path -LiteralPath (Join-Path $Sdk 'bin/datachannel.dll') -PathType Leaf)
        & $BootstrapScript -ToolRoot (Join-Path $Destination '.redbaron-tools') -RequirePython:$needPython
    }
    foreach ($tool in @('cmake','ninja','clang++')) { Get-Command $tool -ErrorAction Stop | Out-Null }
    Write-Output ('Copying {0} game files...' -f $files.Count)
    $index=0
    foreach ($file in $files) {
        $relative=$file.FullName.Substring($Source.Length).TrimStart('\','/')
        $target=Join-Path (Join-Path $Destination 'game') $relative
        New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
        Copy-Item -LiteralPath $file.FullName -Destination $target
        $index++
        if ($index % 100 -eq 0) { Write-Output ('Files: {0}/{1}' -f $index,$files.Count) }
    }
    Write-Output 'Generating code and building (this may take several minutes)...'
    & (Join-Path $Destination 'scripts/build.ps1') -SdkRoot $Sdk -Parallel 4
    if (!(Test-Path -LiteralPath (Join-Path $Destination 'build_cpp_release/snoopyflyingace.exe'))) { throw 'The build did not produce the game executable.' }
    if (!(Test-Path -LiteralPath (Join-Path $Destination 'build_cpp_release/redbaron_probe.exe'))) { throw 'The hardware detector is missing after the build.' }
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
