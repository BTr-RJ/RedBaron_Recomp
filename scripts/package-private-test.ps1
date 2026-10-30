[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$EosConfig,
    [string]$OutputDirectory=''
)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
if(!$OutputDirectory){$OutputDirectory=Join-Path $root 'prioridade5_launcher/private_distribution'}
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$build=Join-Path $OutputDirectory 'build'
& (Join-Path $root 'launcher/build.ps1') -OutputDirectory $build -PrivateEosConfig $EosConfig
$folder=Join-Path $OutputDirectory 'RedBaron_Recomp-0.10-test'
New-Item -ItemType Directory -Path $folder -Force | Out-Null
foreach($name in @('RedBaron_Launcher.exe','RedBaron_Launcher.exe.config')){
    Copy-Item -LiteralPath (Join-Path $build $name) -Destination $folder -Force
}
Copy-Item -LiteralPath (Join-Path $root 'docs/PRIVATE_TEST.md') -Destination (Join-Path $folder 'LEIA-ME-TESTE.md') -Force
foreach($entry in Get-ChildItem -LiteralPath $folder -Force){
    if($entry.PSIsContainer -or ($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) -or $entry.Name -notin @('RedBaron_Launcher.exe','RedBaron_Launcher.exe.config','LEIA-ME-TESTE.md')){throw 'Unexpected file in package staging folder. Choose a new output directory.'}
}
$archive=Join-Path $OutputDirectory 'RedBaron_Recomp-v0.10-windows-x64-private-test.zip'
Add-Type -AssemblyName System.IO.Compression.FileSystem
if(Test-Path -LiteralPath $archive){Remove-Item -LiteralPath $archive}
[IO.Compression.ZipFile]::CreateFromDirectory($folder,$archive,[IO.Compression.CompressionLevel]::Optimal,$true)
$digest=(Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
[IO.File]::WriteAllText(($archive+'.sha256'),$digest+'  '+[IO.Path]::GetFileName($archive)+[Environment]::NewLine)
Write-Output ('Private test installer: '+$archive)
Write-Output ('SHA-256: '+$digest)
