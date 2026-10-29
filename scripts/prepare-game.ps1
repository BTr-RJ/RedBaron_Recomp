[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$GameRoot)
$ErrorActionPreference='Stop'
$folder=Join-Path ([IO.Path]::GetFullPath($GameRoot)) 'Localization'
if(!(Test-Path -LiteralPath $folder -PathType Container)) {throw 'The extracted game Localization folder is missing.'}
$changed=0
foreach($file in Get-ChildItem -LiteralPath $folder -Filter 'FlyingAce.*.loc' -File) {
    # Change only the existing menu string; the game still draws its own font,
    # style, animations and menu object. No extracted assets enter the repository.
    $bytes=[IO.File]::ReadAllBytes($file.FullName)
    if($bytes.Length -lt 2 -or $bytes[0] -ne 255 -or $bytes[1] -ne 254) {throw ('Unexpected localization encoding: '+$file.Name)}
    $text=[Text.Encoding]::Unicode.GetString($bytes,2,$bytes.Length-2)
    $pattern='(?m)^(XBOX Live\[30\]=)"[^"\r\n]*"'
    if(![regex]::IsMatch($text,$pattern)) {throw ('Main-menu localization key missing: '+$file.Name)}
    $updated=[regex]::Replace($text,$pattern,'${1}"Online"')
    if($updated -eq $text) {continue}
    $backup=Join-Path $folder '.redbaron-backup'
    New-Item -ItemType Directory -Path $backup -Force | Out-Null
    $original=Join-Path $backup $file.Name
    if(!(Test-Path -LiteralPath $original)) {Copy-Item -LiteralPath $file.FullName -Destination $original}
    [IO.File]::WriteAllText($file.FullName,$updated,[Text.Encoding]::Unicode)
    ++$changed
}
Write-Output "Online menu label prepared in $changed language file(s); original font and menu behavior preserved."
