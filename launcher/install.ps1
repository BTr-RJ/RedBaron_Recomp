[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$Source,
    [Parameter(Mandatory=$true)][string]$Destination,
    [Parameter(Mandatory=$true)][string]$Sdk,
    [Parameter(Mandatory=$true)][string]$SourceArchive,
    [Parameter(Mandatory=$true)][string]$LauncherExe
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

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
                throw "Links e juncoes nao sao aceitos neste processo: $current"
            }
        }
        $parent = Split-Path -Parent $current
        if ($parent -eq $current) { break }
        $current = $parent
    }
}
function Read-BE32([byte[]]$Data,[int]$Offset) {
    if ($Offset -lt 0 -or $Offset + 4 -gt $Data.Length) { throw 'Cabecalho XEX incompleto.' }
    [uint32](([uint64]$Data[$Offset] -shl 24) -bor ([uint64]$Data[$Offset+1] -shl 16) -bor ([uint64]$Data[$Offset+2] -shl 8) -bor $Data[$Offset+3])
}

$Source = Full-Path $Source
$Destination = Full-Path $Destination
$Sdk = Full-Path $Sdk
if ((Is-Within $Destination $Source) -or (Is-Within $Source $Destination) -or
    (Is-Within $Destination $Sdk) -or (Is-Within $Sdk $Destination)) {
    throw 'Escolha uma pasta de destino separada dos dados de origem e do SDK.'
}
Assert-NoReparse $Source
Assert-NoReparse $Destination
if ((Test-Path -LiteralPath $Destination) -and @(Get-ChildItem -LiteralPath $Destination -Force).Count) {
    throw 'O destino precisa estar vazio. Use outra pasta para preservar instalacoes e saves existentes.'
}
foreach ($item in @('default.xex','Config','Packages')) {
    if (!(Test-Path -LiteralPath (Join-Path $Source $item))) { throw "Origem incompleta: falta $item" }
}
$header = [IO.File]::ReadAllBytes((Join-Path $Source 'default.xex'))
if ([Text.Encoding]::ASCII.GetString($header,0,4) -ne 'XEX2') { throw 'default.xex nao e um XEX2 valido.' }
$count = Read-BE32 $header 20
if ($count -gt 4096) { throw 'Tabela XEX invalida.' }
$titleId = [uint32]0
for ($i=0; $i -lt $count; $i++) {
    if ((Read-BE32 $header (24+8*$i)) -eq 0x00040006) {
        $offset = Read-BE32 $header (28+8*$i)
        $titleId = Read-BE32 $header ([int]$offset+12)
    }
}
if ($titleId -ne 0x584109B5) { throw ('Titulo incompativel: {0:X8}; esperado 584109B5.' -f $titleId) }
foreach ($tool in @('cmake','ninja','clang++')) { Get-Command $tool -ErrorAction Stop | Out-Null }
foreach ($file in @('bin/rexglue.exe','bin/rexruntime.dll','bin/rexgpu-xenos.dll','lib/cmake/rexglue/rexglueConfig.cmake')) {
    if (!(Test-Path -LiteralPath (Join-Path $Sdk $file))) { throw "SDK incompleto: $file" }
}

# Enumerate before creating the destination; never follow source links.
$files = New-Object 'System.Collections.Generic.List[System.IO.FileInfo]'
$queue = New-Object 'System.Collections.Generic.Queue[string]'
$queue.Enqueue($Source)
while ($queue.Count) {
    foreach ($item in Get-ChildItem -LiteralPath $queue.Dequeue() -Force) {
        if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Link na origem: $($item.FullName)" }
        if ($item.PSIsContainer) { $queue.Enqueue($item.FullName) } else { $files.Add($item) }
    }
}
$bytes = ($files | Measure-Object Length -Sum).Sum
$drive = New-Object IO.DriveInfo ([IO.Path]::GetPathRoot($Destination))
if ($drive.AvailableFreeSpace -lt $bytes + 4GB) { throw 'Espaco insuficiente. Reserve os dados do jogo mais 4 GB para compilacao.' }
New-Item -ItemType Directory -Path $Destination -Force | Out-Null
$marker = Join-Path $Destination '.redbaron-install.json'
@{status='installing';titleId='584109B5';started=(Get-Date).ToString('o')} | ConvertTo-Json | Set-Content -LiteralPath $marker -Encoding UTF8
try {
    Write-Output 'Preparando os fontes do port...'
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [IO.Compression.ZipFile]::ExtractToDirectory($SourceArchive,$Destination)
    Write-Output ('Copiando {0} arquivos do jogo...' -f $files.Count)
    $index=0
    foreach ($file in $files) {
        $relative=$file.FullName.Substring($Source.Length).TrimStart('\','/')
        $target=Join-Path (Join-Path $Destination 'game') $relative
        New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
        Copy-Item -LiteralPath $file.FullName -Destination $target
        $index++
        if ($index % 100 -eq 0) { Write-Output ('Dados: {0}/{1}' -f $index,$files.Count) }
    }
    Write-Output 'Gerando codigo e compilando (esta etapa pode levar varios minutos)...'
    & (Join-Path $Destination 'scripts/build.ps1') -SdkRoot $Sdk -Parallel 4
    if (!(Test-Path -LiteralPath (Join-Path $Destination 'build_cpp_release/snoopyflyingace.exe'))) { throw 'Build nao produziu o executavel.' }
    if (!(Test-Path -LiteralPath (Join-Path $Destination 'build_cpp_release/redbaron_probe.exe'))) { throw 'Detector de controles ausente apos o build.' }
    Copy-Item -LiteralPath $LauncherExe -Destination (Join-Path $Destination 'RedBaron_Launcher.exe')
    Copy-Item -LiteralPath ($LauncherExe+'.config') -Destination (Join-Path $Destination 'RedBaron_Launcher.exe.config')
    @{status='ready';titleId='584109B5';completed=(Get-Date).ToString('o')} | ConvertTo-Json | Set-Content -LiteralPath $marker -Encoding UTF8
    Write-Output 'Instalacao concluida.'
} catch {
    @{status='failed';error=$_.Exception.Message} | ConvertTo-Json | Set-Content -LiteralPath $marker -Encoding UTF8
    throw
}
