[CmdletBinding()]
param([string]$OutputDirectory = (Join-Path $PSScriptRoot 'out'))
$ErrorActionPreference = 'Stop'
$compiler = Join-Path $env:WINDIR 'Microsoft.NET/Framework64/v4.0.30319/csc.exe'
$wpf = Join-Path $env:WINDIR 'Microsoft.NET/Framework64/v4.0.30319/WPF'
if (!(Test-Path -LiteralPath $compiler)) { throw '.NET Framework 4.x compiler ausente.' }
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$projectRoot = Split-Path -Parent $PSScriptRoot
$archive = Join-Path $OutputDirectory 'source.zip'
Add-Type -AssemblyName System.IO.Compression.FileSystem
Add-Type -AssemblyName System.IO.Compression
if (Test-Path -LiteralPath $archive) { Remove-Item -LiteralPath $archive }
$zip = [IO.Compression.ZipFile]::Open($archive,[IO.Compression.ZipArchiveMode]::Create)
try {
    # Explicit source list: never embed generated code, game data or the SDK.
    $sourceFiles = @('port/CMakeLists.txt','port/snoopyflyingace_manifest.toml',
        'port/SnoopyFlyingAce_functions.toml','port/SnoopyFlyingAce_forced_landings.toml',
        'port/generated/rexglue.cmake','scripts/build.ps1')
    foreach ($folder in @('port/src','port/config','port/tests')) {
        $sourceFiles += @(Get-ChildItem -LiteralPath (Join-Path $projectRoot $folder) -File |
            Where-Object { $_.Extension -in @('.cpp','.h','.txt','.toml') } |
            ForEach-Object { $folder + '/' + $_.Name })
    }
    foreach ($name in $sourceFiles) {
        [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip,(Join-Path $projectRoot $name),$name) | Out-Null
    }
} finally { $zip.Dispose() }
$exe = Join-Path $OutputDirectory 'RedBaron_Launcher.exe'
# Original monogram generated at build time; no artwork extracted from the game.
Add-Type -AssemblyName System.Drawing
$bitmap = New-Object Drawing.Bitmap 64,64
$graphics = [Drawing.Graphics]::FromImage($bitmap)
$font = New-Object Drawing.Font 'Segoe UI',36,([Drawing.FontStyle]::Bold)
$format = New-Object Drawing.StringFormat
$format.Alignment = [Drawing.StringAlignment]::Center
$format.LineAlignment = [Drawing.StringAlignment]::Center
$iconPath = Join-Path $OutputDirectory 'RedBaron.ico'
try {
    $graphics.Clear([Drawing.Color]::FromArgb(186,52,61))
    $graphics.TextRenderingHint = [Drawing.Text.TextRenderingHint]::AntiAliasGridFit
    $graphics.DrawString('R',$font,[Drawing.Brushes]::White,(New-Object Drawing.RectangleF 0,0,64,64),$format)
    $icon = [Drawing.Icon]::FromHandle($bitmap.GetHicon())
    $stream = [IO.File]::Create($iconPath)
    try { $icon.Save($stream) } finally { $stream.Dispose(); $icon.Dispose() }
} finally { $format.Dispose(); $font.Dispose(); $graphics.Dispose(); $bitmap.Dispose() }
$arguments = @('/nologo', '/target:winexe', '/platform:x64', '/optimize+', '/utf8output',
    ('/out:' + $exe), ('/win32icon:' + $iconPath), '/r:System.dll', '/r:System.Core.dll', '/r:System.Web.Extensions.dll',
    '/r:System.Windows.Forms.dll', '/r:Microsoft.CSharp.dll', '/r:System.Xaml.dll',
    ('/r:' + (Join-Path $wpf 'WindowsBase.dll')),
    ('/r:' + (Join-Path $wpf 'PresentationCore.dll')),
    ('/r:' + (Join-Path $wpf 'PresentationFramework.dll')),
    ('/resource:' + (Join-Path $PSScriptRoot 'MainWindow.xaml') + ',MainWindow.xaml'),
    ('/resource:' + $archive + ',Source.zip'),
    ('/resource:' + (Join-Path $PSScriptRoot 'install.ps1') + ',Install.ps1'))
$arguments += @(Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot 'src') -Filter '*.cs' | ForEach-Object FullName)
& $compiler @arguments
if ($LASTEXITCODE -ne 0) { throw 'Falha ao compilar launcher.' }
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'App.config') -Destination ($exe + '.config')
Write-Output $exe
