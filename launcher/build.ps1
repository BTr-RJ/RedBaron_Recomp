[CmdletBinding()]
param([string]$OutputDirectory = '', [string]$SdkRoot = '', [string]$SdkSourceRoot = '',
    [Alias('PrivateEosConfig')][string]$EosClientConfig = '', [string]$OfflineToolsRoot = '')
$ErrorActionPreference = 'Stop'
if(!$OutputDirectory){$OutputDirectory=Join-Path $PSScriptRoot 'out'}
$compiler = Join-Path $env:WINDIR 'Microsoft.NET/Framework64/v4.0.30319/csc.exe'
$wpf = Join-Path $env:WINDIR 'Microsoft.NET/Framework64/v4.0.30319/WPF'
if (!(Test-Path -LiteralPath $compiler)) { throw '.NET Framework 4.x compiler not found.' }
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$projectRoot = Split-Path -Parent $PSScriptRoot
if(!$SdkRoot){$SdkRoot=Join-Path $projectRoot 'sdk_experimental'}
if(!$SdkSourceRoot){$SdkSourceRoot=Join-Path $projectRoot 'prioridade4_video/sdk_source'}
$archive = Join-Path $OutputDirectory 'source.zip'
Add-Type -AssemblyName System.IO.Compression.FileSystem
Add-Type -AssemblyName System.IO.Compression
if (Test-Path -LiteralPath $archive) { Remove-Item -LiteralPath $archive }
$zip = [IO.Compression.ZipFile]::Open($archive,[IO.Compression.ZipArchiveMode]::Create)
try {
    # Explicit source list: never embed generated code, game data or the SDK.
    $sourceFiles = @('port/CMakeLists.txt','port/snoopyflyingace_manifest.toml',
        'port/SnoopyFlyingAce_functions.toml','port/SnoopyFlyingAce_forced_landings.toml',
        'port/generated/rexglue.cmake','scripts/build.ps1','scripts/prepare-game.ps1','scripts/build-network.py','scripts/make-icon.ps1')
    foreach ($file in Get-ChildItem -LiteralPath (Join-Path $projectRoot 'port/network') -File) {
        if ($file.Extension -in @('.cpp','.h','.txt','.json')) { $sourceFiles += 'port/network/' + $file.Name }
    }
    foreach ($folder in @('port/src','port/config','port/tests','port/eos')) {
        $sourceFiles += @(Get-ChildItem -LiteralPath (Join-Path $projectRoot $folder) -File |
            Where-Object { $_.Extension -in @('.cpp','.h','.txt','.toml','.cmake') } |
            ForEach-Object { $folder + '/' + $_.Name })
    }
    foreach ($name in $sourceFiles) {
        [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip,(Join-Path $projectRoot $name),$name) | Out-Null
    }
} finally { $zip.Dispose() }
$sdkArchive=Join-Path $OutputDirectory 'sdk.zip'
if(Test-Path -LiteralPath $sdkArchive){Remove-Item -LiteralPath $sdkArchive}
$sdkBundle=Test-Path -LiteralPath (Join-Path $SdkRoot 'bin/rexglue.exe') -PathType Leaf
if($sdkBundle) {
    if(!(Test-Path -LiteralPath (Join-Path $SdkRoot 'bin/redbaron_content_inspect.exe'))){throw 'Stage the redbaron_content_inspect executable in the SDK bin folder before packaging STFS support.'}
    $network=Join-Path $projectRoot 'network-work/build'
    foreach($file in @('redbaron_net.exe','datachannel.dll','licenses/dependencies.json')) {
        if(!(Test-Path -LiteralPath (Join-Path $network $file))){throw 'Build the online companion with scripts/build-network.py before bundling the SDK.'}
    }
    $zip=[IO.Compression.ZipFile]::Open($sdkArchive,[IO.Compression.ZipArchiveMode]::Create)
    try {
        foreach($folder in @('bin','include','lib','share','licenses','cmake')) {
            $base=Join-Path $SdkRoot $folder
            if(!(Test-Path -LiteralPath $base -PathType Container)){continue}
            foreach($file in Get-ChildItem -LiteralPath $base -Recurse -File) {
                if($file.Attributes -band [IO.FileAttributes]::ReparsePoint){throw "SDK bundle contains a link: $($file.FullName)"}
                $name=$file.FullName.Substring(([IO.Path]::GetFullPath($SdkRoot)).TrimEnd('\').Length+1).Replace('\','/')
                if($name -in @('bin/redbaron_net.exe','bin/datachannel.dll') -or $name.StartsWith('licenses/network/')){continue}
                if($name -match '(?i)snoopy|(^|/)game/|\.(xex|xexp|pkg|bnk|wem|wmv|xpr)$'){throw "SDK bundle contains game data: $name"}
                [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip,$file.FullName,$name) | Out-Null
            }
        }
        foreach($name in @('redbaron_net.exe','datachannel.dll')) {
            [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip,(Join-Path $network $name),('bin/'+$name)) | Out-Null
        }
        foreach($file in Get-ChildItem -LiteralPath (Join-Path $network 'licenses') -File) {
            [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip,$file.FullName,('licenses/network/'+$file.Name)) | Out-Null
        }
        $sdkLicense=Join-Path $SdkSourceRoot 'LICENSE'
        $thirdparty=Join-Path $SdkSourceRoot 'thirdparty'
        if(!(Test-Path -LiteralPath $sdkLicense -PathType Leaf) -or !(Test-Path -LiteralPath $thirdparty -PathType Container)) {
            throw 'Provide the matching SDK source tree to include its third-party license notices.'
        }
        [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip,$sdkLicense,'licenses/ReXGlue-LICENSE.txt') | Out-Null
        foreach($dependency in Get-ChildItem -LiteralPath $thirdparty -Directory) {
            foreach($license in Get-ChildItem -LiteralPath $dependency.FullName -File | Where-Object { $_.Name -match '^(?i:LICENSE|COPYING|NOTICE)(\..*)?$' }) {
                [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip,$license.FullName,('licenses/sdk-thirdparty/'+$dependency.Name+'/'+$license.Name)) | Out-Null
            }
        }
        $mspackLicense=Join-Path $thirdparty 'libmspack/libmspack/COPYING.LIB'
        if(!(Test-Path -LiteralPath $mspackLicense -PathType Leaf)){throw 'libmspack license is missing from the SDK source.'}
        [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip,$mspackLicense,'licenses/sdk-thirdparty/libmspack/COPYING.LIB') | Out-Null
    } finally {$zip.Dispose()}
}
$exe = Join-Path $OutputDirectory 'RedBaron_Launcher.exe'
$iconPath = Join-Path $OutputDirectory 'RedBaron.ico'
& (Join-Path $projectRoot 'scripts/make-icon.ps1') -Output $iconPath
$arguments = @('/nologo', '/target:winexe', '/platform:x64', '/optimize+', '/utf8output',
    ('/out:' + $exe), ('/win32icon:' + $iconPath), '/r:System.dll', '/r:System.Core.dll', '/r:System.Web.Extensions.dll',
    '/r:System.Windows.Forms.dll', '/r:Microsoft.CSharp.dll', '/r:System.Xaml.dll',
    ('/r:' + (Join-Path $wpf 'WindowsBase.dll')),
    ('/r:' + (Join-Path $wpf 'PresentationCore.dll')),
    ('/r:' + (Join-Path $wpf 'PresentationFramework.dll')),
    ('/resource:' + (Join-Path $PSScriptRoot 'MainWindow.xaml') + ',MainWindow.xaml'),
    ('/resource:' + $archive + ',Source.zip'),
    ('/resource:' + (Join-Path $PSScriptRoot 'install.ps1') + ',Install.ps1'),
    ('/resource:' + (Join-Path $PSScriptRoot 'install-progress.ps1') + ',InstallProgress.ps1'),
    ('/resource:' + (Join-Path $PSScriptRoot 'install-dlc.ps1') + ',InstallDlc.ps1'),
    ('/resource:' + (Join-Path $PSScriptRoot 'bootstrap-tools.ps1') + ',BootstrapTools.ps1'),
    ('/resource:' + (Join-Path $PSScriptRoot 'tool-inventory.ps1') + ',ToolInventory.ps1'),
    ('/resource:' + (Join-Path $PSScriptRoot 'App.config') + ',App.config'))
if($sdkBundle){$arguments+=('/resource:'+$sdkArchive+',Sdk.zip')}
if($OfflineToolsRoot){$arguments+=('/resource:'+([IO.Path]::GetFullPath((Join-Path $OfflineToolsRoot 'manifest.json')))+',OfflineTools.json')}
if($EosClientConfig) {
    if(!$sdkBundle -or !(Test-Path -LiteralPath (Join-Path $SdkRoot 'lib/redbaron_eos_service.lib'))){throw 'The online installer requires the EOS provider bundle.'}
    $EosClientConfig=[IO.Path]::GetFullPath($EosClientConfig)
    $values=@{}
    foreach($line in [IO.File]::ReadAllLines($EosClientConfig)) {
        if(!$line -or $line.StartsWith('#')){continue}
        if($line -notmatch '^(product_id|sandbox_id|deployment_id|client_id|client_secret)=([!-~]{1,256})$' -or $values.ContainsKey($Matches[1])){throw 'Invalid EOS game-client configuration.'}
        $values[$Matches[1]]=$true
    }
    if($values.Count -ne 5){throw 'Incomplete EOS game-client configuration.'}
    $arguments+=('/resource:'+$EosClientConfig+',EosClientConfig.ini')
    Write-Output 'Including EOS game-client settings. The player client policy must require a signed-in user.'
}
$sources=@(Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot 'src') -Filter '*.cs' | ForEach-Object FullName)
$runtime=Join-Path $OutputDirectory 'RedBaron_Runtime.exe'
$runtimeArgs=@($arguments | Where-Object {$_ -notlike '/out:*' -and $_ -notmatch ',(Source.zip|Sdk.zip|EosClientConfig.ini|OfflineTools.json)$'})
$runtimeArgs+=('/out:'+$runtime);$runtimeArgs+=$sources
& $compiler @runtimeArgs
if($LASTEXITCODE -ne 0){throw 'Installed launcher build failed.'}
$arguments+=('/resource:'+$runtime+',RuntimeLauncher.exe')
$arguments += $sources
& $compiler @arguments
if ($LASTEXITCODE -ne 0) { throw 'Launcher build failed.' }
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'App.config') -Destination ($exe + '.config')
Write-Output $exe
