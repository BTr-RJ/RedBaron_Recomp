[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$EosSdk,[Parameter(Mandatory=$true)][string]$SdkRoot)
$ErrorActionPreference='Stop'
$bin=Join-Path ([IO.Path]::GetFullPath($SdkRoot)) 'bin'
New-Item -ItemType Directory -Path $bin -Force | Out-Null
foreach($name in @('EOSBootstrapper.exe','EpicOnlineServicesInstaller.exe')) {
    $source=Join-Path $EosSdk ('Tools/'+$name)
    if(!(Test-Path -LiteralPath $source -PathType Leaf)){throw "Official EOS component missing: $name"}
    if($name -eq 'EOSBootstrapper.exe') {
        # Epic ships this customizable bootstrapper unsigned in SDK 1.19.2.1.
        if((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -ne '1CCEC16DB625A1E765ECED6AA7E75F4178CE48FF9F8DDD832002E55B784AC6E7') {
            throw 'EOS bootstrapper differs from the pinned official SDK 1.19.2.1.'
        }
    } else {
        $signature=Get-AuthenticodeSignature -LiteralPath $source
        if($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'Epic Games'){throw "Official EOS signature validation failed: $name"}
    }
    $target=Join-Path $bin $(if($name -eq 'EOSBootstrapper.exe'){'RedBaron_Online.exe'}else{$name})
    Copy-Item -LiteralPath $source -Destination $target -Force
}
[IO.File]::WriteAllText((Join-Path $bin 'RedBaron_Online.ini'),"ApplicationPath=snoopyflyingace.exe`r`nWorkingDirectory=`r`nNoOperation=0`r`nWaitForExit=1`r`n")
Write-Output 'Official Epic bootstrapper and redistributable staged; no product credentials embedded.'
