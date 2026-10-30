[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$PortableRoot,
    [Parameter(Mandatory=$true)][string]$BuildToolsLayout,
    [Parameter(Mandatory=$true)][string]$Destination
)
$ErrorActionPreference='Stop'
$PortableRoot=[IO.Path]::GetFullPath($PortableRoot)
$BuildToolsLayout=[IO.Path]::GetFullPath($BuildToolsLayout)
$Destination=[IO.Path]::GetFullPath($Destination)
if(Test-Path -LiteralPath $Destination){throw 'Choose a new staging directory for the offline tools.'}
foreach($file in @('llvm/bin/clang++.exe','llvm/bin/lld-link.exe','cmake/bin/cmake.exe','ninja/ninja.exe','licenses/LLVM-LICENSE.txt','licenses/Ninja-LICENSE.txt','licenses/CMake-Copyright.txt')){
    if(!(Test-Path -LiteralPath (Join-Path $PortableRoot $file) -PathType Leaf)){throw "Missing portable component: $file"}
}
foreach($file in @('vs_BuildTools.exe','Catalog.json','ChannelManifest.json','Layout.json','Response.json','vs_installer.opc')){
    if(!(Test-Path -LiteralPath (Join-Path $BuildToolsLayout $file) -PathType Leaf)){throw "Incomplete Microsoft offline layout: $file"}
}
$signature=Get-AuthenticodeSignature -LiteralPath (Join-Path $BuildToolsLayout 'vs_BuildTools.exe')
if($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'O=Microsoft Corporation'){throw 'Microsoft offline bootstrap signature is invalid.'}
$response=Get-Content -LiteralPath (Join-Path $BuildToolsLayout 'Response.json') -Raw | ConvertFrom-Json
foreach($component in @('Microsoft.VisualStudio.Component.VC.Tools.x86.x64','Microsoft.VisualStudio.Component.Windows11SDK.26100')){
    if($component -notin $response.add){throw "Missing Microsoft layout component: $component"}
}
New-Item -ItemType Directory -Path $Destination | Out-Null
foreach($pair in @(@($PortableRoot,'portable'),@($BuildToolsLayout,'vs'))){
    foreach($file in Get-ChildItem -LiteralPath $pair[0] -Recurse -Force){
        if($file.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Offline source must not contain links.'}
    }
    Copy-Item -LiteralPath $pair[0] -Destination (Join-Path $Destination $pair[1]) -Recurse
}
$entries=@(foreach($file in Get-ChildItem -LiteralPath $Destination -Recurse -File | Sort-Object FullName){
    [pscustomobject]@{path=$file.FullName.Substring($Destination.TrimEnd('\').Length+1).Replace('\','/');size=$file.Length;sha256=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}
})
@{version=1;files=$entries} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $Destination 'manifest.json') -Encoding UTF8
Write-Output ('Offline tool bundle ready: '+$entries.Count+' files, '+[Math]::Round(($entries | Measure-Object size -Sum).Sum/1GB,2)+' GB')
