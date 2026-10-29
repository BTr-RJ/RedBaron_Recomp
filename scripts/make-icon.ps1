[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$Output)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Drawing
if(-not ('RedBaronIconHandle' -as [type])) {
    Add-Type -TypeDefinition 'using System; using System.Runtime.InteropServices; public static class RedBaronIconHandle { [DllImport("user32.dll")] public static extern bool DestroyIcon(IntPtr icon); }'
}
# Original project monogram, generated from text; contains no game artwork.
$bitmap=New-Object Drawing.Bitmap 64,64
$graphics=[Drawing.Graphics]::FromImage($bitmap)
$font=New-Object Drawing.Font 'Segoe UI',36,([Drawing.FontStyle]::Bold)
$format=New-Object Drawing.StringFormat
$format.Alignment=[Drawing.StringAlignment]::Center
$format.LineAlignment=[Drawing.StringAlignment]::Center
$handle=[IntPtr]::Zero
try {
    $graphics.Clear([Drawing.Color]::FromArgb(186,52,61))
    $graphics.TextRenderingHint=[Drawing.Text.TextRenderingHint]::AntiAliasGridFit
    $graphics.DrawString('R',$font,[Drawing.Brushes]::White,(New-Object Drawing.RectangleF 0,0,64,64),$format)
    $handle=$bitmap.GetHicon()
    $icon=[Drawing.Icon]::FromHandle($handle)
    $stream=[IO.File]::Create([IO.Path]::GetFullPath($Output))
    try {$icon.Save($stream)} finally {$stream.Dispose();$icon.Dispose()}
} finally {
    if($handle -ne [IntPtr]::Zero){[RedBaronIconHandle]::DestroyIcon($handle) | Out-Null}
    $format.Dispose();$font.Dispose();$graphics.Dispose();$bitmap.Dispose()
}
