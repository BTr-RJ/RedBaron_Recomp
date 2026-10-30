param([switch]$AsJson,[string]$CatalogFile='')
if(!$CatalogFile){$CatalogFile=Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'RedBaron_Recomp/build-tools.json'}
function Get-RedBaronToolInventory {
    $cached=@{}
    if((Test-Path -LiteralPath $CatalogFile -PathType Leaf) -and (Get-Item -LiteralPath $CatalogFile).Length -lt 65536){
        try {$stored=Get-Content -LiteralPath $CatalogFile -Raw | ConvertFrom-Json;foreach($entry in $stored){$cached[$entry.key]=$entry.path}}catch{}
    }
    $result=New-Object 'System.Collections.Generic.List[object]'
    foreach($definition in @(@('cmake','CMake','cmake','cmake version (\d+\.\d+(?:\.\d+)?)','3.25'),@('ninja','Ninja','ninja','^(\d+\.\d+(?:\.\d+)?)','1.10'),@('clang','Clang','clang++','clang version (\d+\.\d+(?:\.\d+)?)','20.0'))) {
        $path='';$version='';$ready=$false
        $command=Get-Command $definition[2] -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
        $candidates=@();if($command){$candidates+=$command.Source};if($cached.ContainsKey($definition[0])){$candidates+=$cached[$definition[0]]}
        foreach($candidate in $candidates | Select-Object -Unique){
            if(!(Test-Path -LiteralPath $candidate -PathType Leaf) -or $candidate -match '(?i)\\WindowsApps\\'){continue}
            try {$line=(@(& $candidate --version 2>$null) -join ' ');if($LASTEXITCODE -eq 0 -and $line -match $definition[3]){$version=$Matches[1];$path=$candidate;$ready=([version]$version -ge [version]$definition[4]);if($ready){break}}}catch{}
        }
        $result.Add([pscustomobject]@{key=$definition[0];name=$definition[1];available=$ready;version=$version;path=$path})
    }
    $vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $vs='';$vcVersion=''
    if(Test-Path -LiteralPath $vswhere){$vs=(& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1)}
    if($vs){$versionFile=Join-Path $vs 'VC/Auxiliary/Build/Microsoft.VCToolsVersion.default.txt';if(Test-Path -LiteralPath $versionFile){$vcVersion=([IO.File]::ReadAllText($versionFile)).Trim()}}
    $vcReady=[bool]$vs -and [bool]$vcVersion -and [version]$vcVersion -ge [version]'14.44'
    if($vcReady){foreach($part in @('bin/Hostx64/x64/cl.exe','include/vector','lib/x64/msvcprt.lib')){if(!(Test-Path -LiteralPath (Join-Path $vs ('VC/Tools/MSVC/'+$vcVersion+'/'+$part)))){$vcReady=$false}}}
    $result.Add([pscustomobject]@{key='visualCpp';name='Visual C++ Build Tools';available=$vcReady;version=$vcVersion;path=$vs})
    $kitInfo=Get-ItemProperty -Path 'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows Kits\Installed Roots' -Name KitsRoot10 -ErrorAction SilentlyContinue
    $sdk=$null;$sdkRoot=''
    if($kitInfo){$sdkRoot=$kitInfo.KitsRoot10;$sdk=Get-ChildItem -LiteralPath (Join-Path $sdkRoot 'Include') -Filter '10.*' -Directory -ErrorAction SilentlyContinue | Where-Object {(Test-Path -LiteralPath (Join-Path $_.FullName 'um/Windows.h')) -and (Test-Path -LiteralPath (Join-Path $sdkRoot ('Lib/'+$_.Name+'/um/x64/kernel32.lib'))) -and (Test-Path -LiteralPath (Join-Path $sdkRoot ('bin/'+$_.Name+'/x64/rc.exe')))} | Sort-Object Name -Descending | Select-Object -First 1}
    $result.Add([pscustomobject]@{key='windowsSdk';name='Windows SDK';available=([bool]$sdk -and [version]$sdk.Name -ge [version]'10.0.26100.0' -and (Test-Path -LiteralPath (Join-Path $sdkRoot ('Lib/'+$sdk.Name+'/ucrt/x64/ucrt.lib'))));version=$(if($sdk){$sdk.Name}else{''});path=$sdkRoot})
    return $result.ToArray()
}
if($AsJson){ConvertTo-Json -InputObject @(Get-RedBaronToolInventory) -Depth 3 -Compress}
