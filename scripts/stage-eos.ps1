[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$EosSdk,
    [Parameter(Mandatory=$true)][string]$BuildDirectory,
    [Parameter(Mandatory=$true)][string]$RuntimeSdk
)
$ErrorActionPreference='Stop'
$EosSdk=[IO.Path]::GetFullPath($EosSdk)
$BuildDirectory=[IO.Path]::GetFullPath($BuildDirectory)
$RuntimeSdk=[IO.Path]::GetFullPath($RuntimeSdk)
foreach($name in @('redbaron_eos_service.lib','redbaron_eos_wire.lib','redbaron_eos_lobby.lib')) {
    $source=Join-Path $BuildDirectory $name
    if(!(Test-Path -LiteralPath $source -PathType Leaf)){throw "Build the online provider first: $name"}
    Copy-Item -LiteralPath $source -Destination (Join-Path $RuntimeSdk ('lib/'+$name)) -Force
}
Copy-Item -LiteralPath (Join-Path $EosSdk 'Lib/EOSSDK-Win64-Shipping.lib') -Destination (Join-Path $RuntimeSdk 'lib') -Force
Copy-Item -LiteralPath (Join-Path $EosSdk 'Bin/EOSSDK-Win64-Shipping.dll') -Destination (Join-Path $RuntimeSdk 'bin') -Force
$notices=Join-Path (Split-Path -Parent $EosSdk) 'ThirdPartyNotices/ThirdPartySoftwareNotice.txt'
if(!(Test-Path -LiteralPath $notices -PathType Leaf)){throw 'EOS third-party notices are missing from the official download.'}
$licenses=Join-Path $RuntimeSdk 'licenses/eos'
New-Item -ItemType Directory -Path $licenses -Force | Out-Null
Copy-Item -LiteralPath $notices -Destination $licenses -Force
Write-Output 'Online libraries and runtime staged. No product configuration or credentials were copied.'
