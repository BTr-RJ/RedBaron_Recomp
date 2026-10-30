[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$Source,
    [Parameter(Mandatory=$true)][string]$UserRoot,
    [Parameter(Mandatory=$true)][string]$Inspector
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
function NoLinks([string]$path) {
    $current=[IO.Path]::GetFullPath($path)
    while($current) {
        if((Test-Path -LiteralPath $current) -and ((Get-Item -LiteralPath $current -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw "Links and junctions are not supported: $current"
        }
        $next=Split-Path -Parent $current;if($next -eq $current){break};$current=$next
    }
}
$Source=[IO.Path]::GetFullPath($Source)
$UserRoot=[IO.Path]::GetFullPath($UserRoot)
$Inspector=[IO.Path]::GetFullPath($Inspector)
NoLinks $Source;NoLinks $UserRoot
if(!(Test-Path -LiteralPath $Source -PathType Container)){throw 'Choose the folder containing your DLC packages.'}
if(!(Test-Path -LiteralPath $Inspector -PathType Leaf)){throw 'Update the game installation to enable DLC installation.'}
$queue=New-Object 'System.Collections.Generic.Queue[string]';$queue.Enqueue($Source)
$packages=New-Object 'System.Collections.Generic.List[System.IO.FileInfo]'
$seen=New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
while($queue.Count) {
    foreach($item in Get-ChildItem -LiteralPath $queue.Dequeue() -Force) {
        if($item.Attributes -band [IO.FileAttributes]::ReparsePoint){throw "Link found in source: $($item.FullName)"}
        if($item.PSIsContainer){$queue.Enqueue($item.FullName);continue}
        if($item.Name -notmatch '^[0-9A-Fa-f]{32,42}$'){continue}
        $output=@()
        for($attempt=0;$attempt -lt 3;$attempt++) {
            $output=@(& $Inspector $item.FullName 2>$null)
            if($LASTEXITCODE -ne 0 -or $output.Count){break}
        }
        if($LASTEXITCODE -ne 0){continue}
        if(!$output.Count){throw "Could not read package metadata: $($item.Name). Update the package extractor and try again."}
        if(($output -join ' ') -notmatch 'title=584109B5 type=00000002'){continue}
        if(!$seen.Add($item.Name)){throw "Duplicate package name: $($item.Name)"}
        $packages.Add($item)
    }
}
if(!$packages.Count){throw 'No compatible game DLC packages were found in that folder.'}
$content=Join-Path $UserRoot '0000000000000000/584109B5/00000002'
$headers=Join-Path $UserRoot '0000000000000000/584109B5/Headers/00000002'
NoLinks $content;NoLinks $headers
New-Item -ItemType Directory -Path $content,$headers -Force | Out-Null
$work=Join-Path $UserRoot ('.redbaron-dlc-'+[Guid]::NewGuid().ToString('N'))
$work=[IO.Path]::GetFullPath($work)
if(!$work.StartsWith($UserRoot.TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)) {throw 'Invalid staging location.'}
New-Item -ItemType Directory -Path $work | Out-Null
$installed=0;$skipped=0
try {
    foreach($package in $packages) {
        $target=Join-Path $content $package.Name
        $targetHeader=Join-Path $headers ($package.Name+'.header')
        if((Test-Path -LiteralPath $target) -and (Test-Path -LiteralPath $targetHeader)) {
            Write-Output "Already installed: $($package.Name)";++$skipped;continue
        }
        if((Test-Path -LiteralPath $target) -or (Test-Path -LiteralPath $targetHeader)) {
            throw "Incomplete existing installation preserved: $($package.Name)"
        }
        $staged=Join-Path $work $package.Name
        $stagedHeader=Join-Path $work ($package.Name+'.header')
        & $Inspector --extract $staged $package.FullName
        if($LASTEXITCODE -ne 0){throw "Could not extract $($package.Name)"}
        & $Inspector --header $stagedHeader $package.FullName
        if($LASTEXITCODE -ne 0){throw "Could not prepare metadata for $($package.Name)"}
        # An unpaired header is invisible to the game's directory enumeration.
        [IO.File]::Move($stagedHeader,$targetHeader)
        try {[IO.Directory]::Move($staged,$target)} catch {
            [IO.File]::Delete($targetHeader)
            throw
        }
        ++$installed;Write-Output "Installed: $($package.Name)"
    }
    Write-Output "DLC installation complete: $installed installed, $skipped already present."
} finally {
    # Work folder was freshly created by this process under the checked user root.
    if(Test-Path -LiteralPath $work){NoLinks $work;Remove-Item -LiteralPath $work -Recurse -Force}
}
