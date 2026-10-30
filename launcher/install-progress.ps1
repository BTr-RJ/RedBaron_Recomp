$script:rbProgressLast=''
function Write-RBProgress([string]$Stage,[long]$Completed=0,[long]$Total=0) {
    $stamp=$Stage+'|'+$(if($Total){[Math]::Floor(100.0*$Completed/$Total)}else{'?'})
    if($script:rbProgressLast -eq $stamp){return};$script:rbProgressLast=$stamp
    # Host output is visible to the launcher but never contaminates function return values.
    Write-Host ('@@RBPROGRESS '+(@{stage=$Stage;completed=$Completed;total=$Total} | ConvertTo-Json -Compress))
}
function Expand-RBArchive([string]$Archive,[string]$Destination,[string]$Stage) {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $base=[IO.Path]::GetFullPath($Destination).TrimEnd('\')+'\'
    $zip=[IO.Compression.ZipFile]::OpenRead([IO.Path]::GetFullPath($Archive))
    try {
        [long]$total=0;foreach($entry in $zip.Entries){$total+=$entry.Length};[long]$done=0;$last=-1
        Write-RBProgress $Stage 0 $total
        foreach($entry in $zip.Entries) {
            $target=[IO.Path]::GetFullPath((Join-Path $base $entry.FullName))
            if(!$target.StartsWith($base,[StringComparison]::OrdinalIgnoreCase)){throw 'Unsafe archive path.'}
            if(!$entry.Name){New-Item -ItemType Directory -Path $target -Force | Out-Null;continue}
            New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
            $inputStream=$entry.Open();$outputStream=[IO.File]::Open($target,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write)
            try {
                $buffer=New-Object byte[] 65536
                while(($count=$inputStream.Read($buffer,0,$buffer.Length)) -gt 0){
                    $outputStream.Write($buffer,0,$count);$done+=$count
                    $percent=if($total){[int](100.0*$done/$total)}else{0}
                    if($percent -ne $last){Write-RBProgress $Stage $done $total;$last=$percent}
                }
            }finally{$outputStream.Dispose();$inputStream.Dispose()}
        }
        Write-RBProgress $Stage 1 1
    }finally{$zip.Dispose()}
}
function Receive-RBDownload([string]$Url,[string]$Destination,[string]$Stage) {
    $request=[Net.HttpWebRequest]::Create($Url);$request.UserAgent='RedBaron_Recomp/0.10';$request.Timeout=60000;$request.ReadWriteTimeout=120000
    $response=$request.GetResponse()
    try {
        [long]$total=[Math]::Max(0,$response.ContentLength);[long]$done=0;$last=-1
        Write-RBProgress $Stage 0 $total
        $inputStream=$response.GetResponseStream();$outputStream=[IO.File]::Create($Destination)
        try {
            $buffer=New-Object byte[] 65536;$timer=[Diagnostics.Stopwatch]::StartNew()
            while(($count=$inputStream.Read($buffer,0,$buffer.Length)) -gt 0) {
                $outputStream.Write($buffer,0,$count);$done+=$count
                $percent=if($total){[int](100.0*$done/$total)}else{-1}
                if(($total -and $percent -ne $last) -or $timer.ElapsedMilliseconds -ge 1000){Write-RBProgress $Stage $done $total;$last=$percent;$timer.Restart()}
            }
            if($total -and $done -ne $total){throw 'The download was incomplete.'}
            Write-RBProgress $Stage $done $done
        }finally{$outputStream.Dispose();$inputStream.Dispose()}
    }finally{$response.Dispose()}
}
