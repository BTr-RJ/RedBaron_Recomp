[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$ToolRoot,[switch]$RequirePython)
$ErrorActionPreference='Stop'
$ProgressPreference='SilentlyContinue'
Set-StrictMode -Version Latest
if(![Environment]::Is64BitOperatingSystem){throw 'A 64-bit Windows installation is required.'}
$ToolRoot=[IO.Path]::GetFullPath($ToolRoot)
New-Item -ItemType Directory -Path $ToolRoot -Force | Out-Null
[Net.ServicePointManager]::SecurityProtocol=[Net.SecurityProtocolType]::Tls12
function Download([string]$url,[string]$hash,[string]$name) {
    $path=Join-Path $ToolRoot $name
    if((Test-Path -LiteralPath $path) -and (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $hash){throw "Unexpected existing download: $path"}
    if(!(Test-Path -LiteralPath $path)) {
        $partial=$path+'.partial'
        Write-Host "Downloading $name..."
        Invoke-WebRequest -UseBasicParsing -TimeoutSec 1800 -Uri $url -OutFile $partial
        if((Get-FileHash -LiteralPath $partial -Algorithm SHA256).Hash -ne $hash){throw "Checksum mismatch for $name; file was not used."}
        [IO.File]::Move($partial,$path)
    }
    return $path
}
function Have([string]$name) {return $null -ne (Get-Command $name -ErrorAction SilentlyContinue)}
$cmakeOk=$false
if(Have 'cmake') {
    $version=@(& cmake --version)[0]
    if($version -match '(\d+\.\d+\.\d+)'){$cmakeOk=([version]$Matches[1] -ge [version]'3.25.0')}
}
if(!$cmakeOk) {
    $zip=Download 'https://github.com/Kitware/CMake/releases/download/v4.4.5/cmake-4.4.5-windows-x86_64.zip' '326C826486D46A23CF9D5A1C9EE3A567D427BDD02C4A34071937A2A675CFF004' 'cmake.zip'
    $target=Join-Path $ToolRoot 'cmake'
    if(!(Test-Path -LiteralPath (Join-Path $target 'cmake-4.4.5-windows-x86_64/bin/cmake.exe'))) {
        if(Test-Path -LiteralPath $target){throw "Incomplete tool folder preserved: $target"}
        Expand-Archive -LiteralPath $zip -DestinationPath $target
    }
    $env:PATH=(Join-Path $target 'cmake-4.4.5-windows-x86_64/bin')+';'+$env:PATH
}
if(!(Have 'ninja')) {
    $zip=Download 'https://github.com/ninja-build/ninja/releases/download/v1.13.2/ninja-win.zip' '07FC8261B42B20E71D1720B39068C2E14FFCEE6396B76FB7A795FB460B78DC65' 'ninja.zip'
    $target=Join-Path $ToolRoot 'ninja'
    if(!(Test-Path -LiteralPath (Join-Path $target 'ninja.exe'))) {
        if(Test-Path -LiteralPath $target){throw "Incomplete tool folder preserved: $target"}
        Expand-Archive -LiteralPath $zip -DestinationPath $target
    }
    $env:PATH=$target+';'+$env:PATH
}
$clangOk=$false
if(Have 'clang++') {
    $version=@(& clang++ --version)[0]
    if($version -match 'clang version (\d+)'){$clangOk=([int]$Matches[1] -ge 20)}
}
if(!$clangOk) {
    $archive=Download 'https://github.com/llvm/llvm-project/releases/download/llvmorg-23.1.4/clang%2Bllvm-23.1.4-x86_64-pc-windows-msvc.tar.xz' '854BFFCFC84528E0EE92490FF5C178920ABD358A79C3E98F053E41E3E8E4C6D2' 'llvm.tar.xz'
    $target=Join-Path $ToolRoot 'llvm'
    $bin=Join-Path $target 'clang+llvm-23.1.4-x86_64-pc-windows-msvc/bin'
    if(!(Test-Path -LiteralPath (Join-Path $bin 'clang++.exe'))) {
        if(Test-Path -LiteralPath $target){throw "Incomplete tool folder preserved: $target"}
        New-Item -ItemType Directory -Path $target | Out-Null
        & tar.exe -xf $archive -C $target
        if($LASTEXITCODE -ne 0 -or !(Test-Path -LiteralPath (Join-Path $bin 'clang++.exe'))){throw "LLVM extraction failed; work retained at $target"}
    }
    $env:PATH=$bin+';'+$env:PATH
}
if($RequirePython) {
    $pythonOk=$false
    $pythonCommand=Get-Command python -ErrorAction SilentlyContinue
    # Windows Store aliases may open an interactive store page instead of Python.
    if($pythonCommand -and $pythonCommand.Source -notmatch '(?i)\\WindowsApps\\') {
        try {
            $output=& python -c 'import sys; print(sys.version_info >= (3, 12))' 2>$null
            $pythonOk=$LASTEXITCODE -eq 0 -and $output -eq 'True'
        } catch {$pythonOk=$false}
    }
    if(!$pythonOk) {
        $zip=Download 'https://www.python.org/ftp/python/3.14.7/python-3.14.7-amd64.zip' 'AC1A727A71738E11DE80B76E975F9B8A258AEA6412BFC31696B929D59C6AAFD0' 'python.zip'
        $target=Join-Path $ToolRoot 'python'
        if(!(Test-Path -LiteralPath (Join-Path $target 'python.exe'))) {
            if(Test-Path -LiteralPath $target){throw "Incomplete tool folder preserved: $target"}
            Expand-Archive -LiteralPath $zip -DestinationPath $target
        }
        $env:PATH=$target+';'+$env:PATH
    }
}
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$compiler=''
if(Test-Path -LiteralPath $vswhere) {
    $compiler=(& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1)
}
$kitInfo=Get-ItemProperty -Path 'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows Kits\Installed Roots' -Name KitsRoot10 -ErrorAction SilentlyContinue
$kit=if($kitInfo){$kitInfo.KitsRoot10}else{''}
$sdkFound=$kit -and (Get-ChildItem -LiteralPath (Join-Path $kit 'Include') -Filter '10.*' -Directory -ErrorAction SilentlyContinue | Where-Object {Test-Path -LiteralPath (Join-Path $_.FullName 'um/Windows.h')} | Select-Object -First 1)
if(!$compiler -or !$sdkFound) {
    $bootstrap=Join-Path $ToolRoot 'vs_BuildTools.exe'
    if(!(Test-Path -LiteralPath $bootstrap)) {
        Write-Output 'Downloading Microsoft Visual C++ Build Tools...'
        Invoke-WebRequest -UseBasicParsing -TimeoutSec 180 -Uri 'https://aka.ms/vs/17/release/vs_BuildTools.exe' -OutFile $bootstrap
    }
    $signature=Get-AuthenticodeSignature -LiteralPath $bootstrap
    if($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'O=Microsoft Corporation') {throw 'Microsoft Build Tools signature verification failed.'}
    Write-Output 'Installing Visual C++ Build Tools and Windows SDK; Windows may request administrator approval.'
    $process=Start-Process -FilePath $bootstrap -ArgumentList @('--quiet','--wait','--norestart','--add','Microsoft.VisualStudio.Workload.VCTools','--includeRecommended') -Verb RunAs -WindowStyle Hidden -Wait -PassThru
    if($process.ExitCode -notin @(0,3010)){throw "Microsoft Build Tools installation failed: $($process.ExitCode)"}
    if($process.ExitCode -eq 3010){throw 'Windows requires a restart to finish installing Build Tools. Restart and run installation again.'}
}
$compiler=(& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1)
if(!$compiler){throw 'Visual C++ Build Tools could not be located after installation.'}
$devCommand=Join-Path $compiler 'Common7/Tools/VsDevCmd.bat'
if(!(Test-Path -LiteralPath $devCommand)){throw 'The Visual C++ build environment is missing.'}
$selectedToolBins=@('cmake','ninja','clang++') | ForEach-Object {Split-Path -Parent (Get-Command $_ -ErrorAction Stop).Source} | Select-Object -Unique
# Import the environment into this installation process. Ninja needs rc.exe,
# headers and libraries even when clang is already present on the machine.
$environmentProcess=New-Object Diagnostics.Process
$environmentProcess.StartInfo=New-Object Diagnostics.ProcessStartInfo
$environmentProcess.StartInfo.FileName=$env:ComSpec
$environmentProcess.StartInfo.Arguments='/d /s /c ""'+$devCommand+'" -no_logo -arch=x64 -host_arch=x64 >nul && set"'
$environmentProcess.StartInfo.UseShellExecute=$false
$environmentProcess.StartInfo.CreateNoWindow=$true
$environmentProcess.StartInfo.RedirectStandardOutput=$true
$environmentProcess.Start() | Out-Null
$environmentText=$environmentProcess.StandardOutput.ReadToEnd()
$environmentProcess.WaitForExit()
if($environmentProcess.ExitCode -ne 0){throw 'Unable to prepare the Visual C++ build environment.'}
$environmentProcess.Dispose()
foreach($line in $environmentText.Split([char]10)){
    $line=$line.TrimEnd([char]13)
    $split=$line.IndexOf('=')
    if($split -gt 0){[Environment]::SetEnvironmentVariable($line.Substring(0,$split),$line.Substring($split+1),'Process')}
}
$env:PATH=($selectedToolBins -join ';')+';'+$env:PATH
foreach($tool in @('cmake','ninja','clang++','rc.exe')){Get-Command $tool -ErrorAction Stop | Out-Null}
Write-Output 'Build tools are ready.'
