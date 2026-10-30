[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][Alias('EosConfig')][string]$EosClientConfig,
    [string]$OutputDirectory='', [string]$OfflineToolsRoot=''
)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
if(!$OutputDirectory){$OutputDirectory=Join-Path $root 'dist/v0.10'}
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
& cmake --build (Join-Path $root 'build_cpp_release') --target snoopyflyingace redbaron_content_inspect --parallel 4
if($LASTEXITCODE){throw 'Build the game and package extractor before creating the installer.'}
Copy-Item -LiteralPath (Join-Path $root 'build_cpp_release/redbaron_content_inspect.exe') -Destination (Join-Path $root 'sdk_experimental/bin/redbaron_content_inspect.exe') -Force
$variant=if($OfflineToolsRoot){'offline'}else{'standard'}
$build=Join-Path $OutputDirectory ('build-'+$variant)
& (Join-Path $root 'launcher/build.ps1') -OutputDirectory $build -EosClientConfig $EosClientConfig -OfflineToolsRoot $OfflineToolsRoot
$folder=Join-Path (Join-Path $OutputDirectory ('stage-'+$variant)) 'RedBaron_Recomp-v0.10'
New-Item -ItemType Directory -Path $folder -Force | Out-Null
foreach($name in @('RedBaron_Launcher.exe','RedBaron_Launcher.exe.config')){
    Copy-Item -LiteralPath (Join-Path $build $name) -Destination $folder -Force
}
$guide=[IO.File]::ReadAllText((Join-Path $root 'docs/INSTALLATION.md'))
$guide=$guide.Replace('(../port/config/CONTROLS.txt)','(https://github.com/BTr-RJ/RedBaron_Recomp/blob/main/port/config/CONTROLS.txt)').Replace('(EOS.md)','(https://github.com/BTr-RJ/RedBaron_Recomp/blob/main/docs/EOS.md)')
[IO.File]::WriteAllText((Join-Path $folder 'INSTALLATION.md'),$guide)
Copy-Item -LiteralPath (Join-Path $root 'CREDITS.md') -Destination $folder -Force
if($OfflineToolsRoot) {
    $tools=Join-Path $folder 'Tools'
    $manifest=Get-Content -LiteralPath (Join-Path $OfflineToolsRoot 'manifest.json') -Raw | ConvertFrom-Json
    $expected=New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
    foreach($entry in $manifest.files){
        if($entry.path -notmatch '^(portable|vs)/' -or $entry.path -match '(^|/)\.{1,2}(/|$)|[:\\]|(?i)\.(xex|xexp|pkg|bnk|wem|wmv|stfs|bundle)$|(^|/)config\.ini$'){throw 'Unexpected offline tool entry.'}
        $source=Join-Path $OfflineToolsRoot $entry.path
        if((Get-Item -LiteralPath $source).Length -ne $entry.size -or (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -ine $entry.sha256){throw 'Offline tools changed after preparation.'}
        $target=Join-Path $tools $entry.path
        $null=$expected.Add($entry.path)
        if(Test-Path -LiteralPath $target){
            if((Get-Item -LiteralPath $target).Length -ne $entry.size -or (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -ine $entry.sha256){throw 'Offline package staging differs. Choose a fresh output directory.'}
            continue
        }
        New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
        Copy-Item -LiteralPath $source -Destination $target
    }
    foreach($entry in Get-ChildItem -LiteralPath $tools -Recurse -Force){
        if($entry.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Links are not supported in offline package staging.'}
        if(!$entry.PSIsContainer -and !$expected.Contains($entry.FullName.Substring($tools.TrimEnd('\').Length+1).Replace('\','/'))){throw 'Unexpected file in offline package staging.'}
    }
}
foreach($entry in Get-ChildItem -LiteralPath $folder -Force){
    if($OfflineToolsRoot -and $entry.Name -eq 'Tools' -and $entry.PSIsContainer -and !($entry.Attributes -band [IO.FileAttributes]::ReparsePoint)){continue}
    if($entry.PSIsContainer -or ($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) -or $entry.Name -notin @('RedBaron_Launcher.exe','RedBaron_Launcher.exe.config','INSTALLATION.md','CREDITS.md')){throw 'Unexpected file in package staging folder. Choose a new output directory.'}
}
$archive=Join-Path $OutputDirectory $(if($OfflineToolsRoot){'RedBaron_Recomp-v0.10-windows-x64-offline.zip'}else{'RedBaron_Recomp-v0.10-windows-x64.zip'})
Add-Type -AssemblyName System.IO.Compression.FileSystem
if(Test-Path -LiteralPath $archive){Remove-Item -LiteralPath $archive}
[IO.Compression.ZipFile]::CreateFromDirectory($folder,$archive,[IO.Compression.CompressionLevel]::Optimal,$true)
$digest=(Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
[IO.File]::WriteAllText(($archive+'.sha256'),$digest+'  '+[IO.Path]::GetFileName($archive)+[Environment]::NewLine)
Write-Output ('Release installer: '+$archive)
Write-Output ('SHA-256: '+$digest)
