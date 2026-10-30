using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

namespace RedBaron {
    public sealed class Flag { public string @default {get;set;} public string[] allowed {get;set;} }
    public sealed class Device { public string key {get;set;} public string name {get;set;} }
    public sealed class BuildTool { public string key {get;set;} public string name {get;set;} public bool available {get;set;} public string version {get;set;} public string path {get;set;} }
    public sealed class Gpu { public int index {get;set;} public string name {get;set;} public bool dlss {get;set;} }
    public sealed class Capabilities {
        public Dictionary<string,Flag> flags {get;set;}
        public List<Device> devices {get;set;}
        public List<Gpu> gpus {get;set;}
        public string inputError {get;set;}
        public bool hdrActive {get;set;}
        public bool eosAvailable {get;set;}
        public bool eosSocial {get;set;}
        public int refreshHz {get;set;}
        public int desktopWidth {get;set;}
        public int desktopHeight {get;set;}
        public bool DlssAvailable(int adapter) { return gpus.Any(g=>g.dlss && (adapter<0 || g.index==adapter)); }
        public Capabilities() { flags=new Dictionary<string,Flag>(); devices=new List<Device>(); gpus=new List<Gpu>(); }
        public bool Supports(string key,string value) { Flag flag; return flags.TryGetValue(key,out flag) && (flag.allowed==null || flag.allowed.Length==0 || flag.allowed.Contains(value)); }
    }
    public sealed class InstallPlan {
        public string Root {get;set;}
        public bool InstallGame {get;set;}
        public bool InstallDlc {get;set;}
    }
    public static class Services {
        public static bool HasOfflineTools() {
            using(var manifest=Assembly.GetExecutingAssembly().GetManifestResourceStream("OfflineTools.json"))return manifest!=null;
        }
        public static async Task<List<BuildTool>> CheckBuildTools(Settings s) {
            string job=InstallationCleanup.CreateJob(s.Root,"tool-check");
            string script=Path.Combine(job,"tool-inventory.ps1");ExtractResource("ToolInventory.ps1",script);
            string powershell=Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.System),"WindowsPowerShell","v1.0","powershell.exe");
            var output=new List<string>();object gate=new object();
            int code=await RunAsync(powershell,new[]{"-NoProfile","-ExecutionPolicy","Bypass","-File",script,"-AsJson"},s.Root,line=>{lock(gate)output.Add(line);},CancellationToken.None);
            if(code!=0)throw new IOException("Could not check installed build tools.");
            var result=Json.Serializer.Deserialize<List<BuildTool>>(String.Join(Environment.NewLine,output));
            InstallationCleanup.CleanJob(job);return result;
        }
        public static bool HasInstallation(string root) {
            try {return !String.IsNullOrWhiteSpace(root) && File.Exists(Path.Combine(root,"build_cpp_release","snoopyflyingace.exe")) && File.Exists(Path.Combine(root,"game","default.xex"));}
            catch(ArgumentException){return false;}
        }
        public static InstallPlan PlanInstallation(Settings s) {
            bool current=HasInstallation(s.Root);
            string target=current?s.Root:s.Destination;
            if(String.IsNullOrWhiteSpace(target))throw new InvalidDataException("Choose an installation folder.");
            target=Path.GetFullPath(target);
            bool game=!HasInstallation(target),dlc=!String.IsNullOrWhiteSpace(s.DlcSource);
            if(!game && !dlc)throw new InvalidDataException("The game is already installed. Select a DLC folder to add content.");
            if(game && String.IsNullOrWhiteSpace(s.Source))throw new InvalidDataException("Choose a game folder, default.xex or an STFS package.");
            if(dlc && !Directory.Exists(s.DlcSource))throw new InvalidDataException("The selected DLC folder does not exist.");
            return new InstallPlan {Root=target,InstallGame=game,InstallDlc=dlc};
        }
        public static async Task<Settings> InstallContent(Settings s,Action<string> log,CancellationToken token) {
            var plan=PlanInstallation(s);
            string logDirectory=Path.Combine(s.Root,"launcher-data","logs");Directory.CreateDirectory(logDirectory);
            string workflowLog=Path.Combine(logDirectory,"install-workflow-"+DateTime.Now.ToString("yyyyMMdd-HHmmss-fff")+".log");
            var display=log;object workflowGate=new object();
            log=line=>{lock(workflowGate)File.AppendAllText(workflowLog,line+Environment.NewLine,Encoding.UTF8);display(line);};
            string gameJob=null,dlcJob=null;
            if(plan.InstallGame){s.Destination=plan.Root;gameJob=await Install(s,log,token);}
            var installed=Settings.Load(plan.Root);
            if(plan.InstallGame){installed.Sdk=HasBundledSdk()?Path.Combine(plan.Root,"sdk_experimental"):s.Sdk;installed.DesktopShortcut=s.DesktopShortcut;}
            installed.DlcSource=s.DlcSource;
            Json.Write(installed.FileName,installed);
            if(plan.InstallGame && installed.DesktopShortcut)CreateShortcut(installed,Path.Combine(plan.Root,"RedBaron_Launcher.exe"));
            if(plan.InstallDlc)dlcJob=await InstallDlc(installed,s.DlcSource,log,token);
            await Task.Run(()=>{
                try {InstallationCleanup.Run(installed,log,token);}
                catch(IOException error){log("Game installed; some working files were preserved: "+error.Message);}
                foreach(string job in new[]{gameJob,dlcJob})try {InstallationCleanup.CleanJob(job);}catch(IOException error){log("Temporary job preserved: "+error.Message);}
            });
            string finalLogs=Path.Combine(installed.Root,"launcher-data","logs");Directory.CreateDirectory(finalLogs);
            string finalWorkflowLog=Path.Combine(finalLogs,Path.GetFileName(workflowLog));
            if(!String.Equals(Path.GetFullPath(workflowLog),Path.GetFullPath(finalWorkflowLog),StringComparison.OrdinalIgnoreCase))File.Copy(workflowLog,finalWorkflowLog,false);
            return installed;
        }
        public static bool HasBundledSdk() {
            using(var resource=Assembly.GetExecutingAssembly().GetManifestResourceStream("Sdk.zip"))return resource!=null;
        }
        public static string LauncherExe { get { return Assembly.GetExecutingAssembly().Location; } }
        public static string FindRoot(string[] args) {
            int option=Array.IndexOf(args,"--root");
            if(option>=0) {
                if(option+1==args.Length) throw new ArgumentException("--root requires a folder path.");
                return Path.GetFullPath(args[option+1]);
            }
            var dir=new DirectoryInfo(Path.GetDirectoryName(LauncherExe));
            while(dir!=null) {
                if(File.Exists(Path.Combine(dir.FullName,".redbaron-install.json")) || File.Exists(Path.Combine(dir.FullName,"port","snoopyflyingace_manifest.toml"))) return dir.FullName;
                dir=dir.Parent;
            }
            return Path.GetDirectoryName(LauncherExe);
        }
        // Windows CommandLineToArgvW quoting; arguments are never shell fragments.
        public static string QuoteArgument(string value) {
            var b=new StringBuilder("\""); int slashes=0;
            foreach(char c in value) {
                if(c=='\\') {slashes++;continue;}
                if(c=='"') {b.Append('\\',slashes*2+1); b.Append(c);}
                else {b.Append('\\',slashes); b.Append(c);}
                slashes=0;
            }
            b.Append('\\',slashes*2); b.Append('"'); return b.ToString();
        }
        public static ProcessStartInfo StartInfo(string exe,IEnumerable<string> args,string cwd) {
            return new ProcessStartInfo(exe,String.Join(" ",args.Select(QuoteArgument))) {
                WorkingDirectory=cwd, UseShellExecute=false, CreateNoWindow=true,
                RedirectStandardOutput=true, RedirectStandardError=true
            };
        }
        public static Task<int> RunAsync(string exe,IEnumerable<string> args,string cwd,Action<string> log,CancellationToken token) {
            return Task.Run(()=> {
                using(var process=new Process()) {
                    process.StartInfo=StartInfo(exe,args,cwd);
                    process.OutputDataReceived+=(s,e)=>{if(e.Data!=null) log(e.Data);};
                    process.ErrorDataReceived+=(s,e)=>{if(e.Data!=null) log(e.Data);};
                    token.ThrowIfCancellationRequested();
                    process.Start(); process.BeginOutputReadLine(); process.BeginErrorReadLine();
                    using(token.Register(()=> {
                        try {
                            if(!process.HasExited) {
                                // Kill only the owned install/probe process and its child compiler tree.
                                using(var kill=Process.Start(new ProcessStartInfo("taskkill.exe","/PID "+process.Id+" /T /F") {UseShellExecute=false,CreateNoWindow=true,RedirectStandardOutput=true,RedirectStandardError=true})) {
                                    kill.StandardOutput.ReadToEnd(); kill.StandardError.ReadToEnd(); kill.WaitForExit();
                                }
                            }
                        } catch(InvalidOperationException) {} catch(System.ComponentModel.Win32Exception) {}
                    })) {
                        process.WaitForExit(); token.ThrowIfCancellationRequested(); return process.ExitCode;
                    }
                }
            });
        }
        public static async Task<Capabilities> Probe(Settings s) {
            string exe=Path.Combine(s.Build,"redbaron_probe.exe");
            if(!File.Exists(exe)) throw new FileNotFoundException("Hardware detector missing. Build redbaron_probe or complete installation.");
            string directory=Path.Combine(s.Root,"launcher-data"); Directory.CreateDirectory(directory);
            string output=Path.Combine(directory,"capabilities-"+Guid.NewGuid().ToString("N")+".json");
            var errors=new StringBuilder();
            try {
                using(var timeout=new CancellationTokenSource(TimeSpan.FromSeconds(25))) {
                    int code=await RunAsync(exe,new[]{output},s.Build,line=>{lock(errors) errors.AppendLine(line);},timeout.Token);
                    if(code!=0) throw new IOException("Hardware detection failed ("+code+"). "+errors);
                }
                return Json.Read<Capabilities>(output);
            } finally {if(File.Exists(output)) File.Delete(output);}
        }
        public static bool IsGameRunning(Settings s) {
            foreach(var p in Process.GetProcessesByName("snoopyflyingace")) using(p) {
                try {if(String.Equals(p.MainModule.FileName,s.Exe,StringComparison.OrdinalIgnoreCase)) return true;}
                catch(System.ComponentModel.Win32Exception) {return true;}
                catch(InvalidOperationException) {}
            }
            return false;
        }
        public static void Save(Settings s,Capabilities caps) {
            s.Validate();
            if(s.EosEnabled && !caps.eosAvailable)throw new InvalidDataException("Update the game build to enable in-game EOS matchmaking.");
            if(s.EosEnabled && s.EosAccount && !caps.eosSocial)throw new InvalidDataException("Update the game and Epic online components to use this launcher.");
            if(IsGameRunning(s)) throw new InvalidOperationException("Close the game before saving settings.");
            if(s.LanEnabled && (!caps.flags.ContainsKey("redbaron_lan") || !caps.flags.ContainsKey("redbaron_lan_peer")))
                throw new InvalidDataException("Update the game runtime to use experimental LAN play.");
            if(!caps.Supports("present_effect",s.Upscaling) || !caps.Supports("swap_post_effect",s.Antialiasing))
                throw new InvalidDataException("The selected upscaling or antialiasing option is unavailable in this runtime. Refresh hardware detection.");
            if(!caps.flags.ContainsKey("present_vsync") || !caps.flags.ContainsKey("window_size_in_physical_pixels"))
                throw new InvalidDataException("This Video tab requires the updated RedBaron GPU runtime. Install the video SDK patch and rebuild the game first.");
            if(s.Fps!=0 && !caps.Supports("redbaron_fps",s.Fps.ToString()))
                throw new InvalidDataException("This game build does not support the selected FPS target. Rebuild the game and refresh hardware detection.");
            if(s.Upscaling=="dlss" && !caps.DlssAvailable(s.Adapter))
                throw new InvalidDataException("DLSS is unavailable on the selected GPU or driver. Choose another upscaler or refresh hardware detection.");
            if((s.ShowFps && !caps.flags.ContainsKey("redbaron_show_fps")) || (s.Hdr && !caps.flags.ContainsKey("present_hdr")))
                throw new InvalidDataException("Update the game runtime to use the FPS counter and HDR output.");
            if(!File.Exists(s.Exe)) { Json.Write(s.FileName,s); return; }
            if(s.Upscaling=="dlss") {
                if(!caps.Supports("present_fsr_quality_mode",s.DlssMode)) throw new InvalidDataException("Update the runtime to select DLSS quality modes.");
                int width=s.Fullscreen?caps.desktopWidth:Int32.Parse(s.Resolution.Split('x')[0]);
                int height=s.Fullscreen?caps.desktopHeight:Int32.Parse(s.Resolution.Split('x')[1]);
                s.Scale=s.DlssRenderScale(width,height);
            }
            string original=File.Exists(s.Toml)?File.ReadAllText(s.Toml):"";
            string updated=s.RuntimeText(original,caps.eosSocial);
            string json=Json.Serializer.Serialize(s);
            AtomicFile.Write(s.Toml,updated);
            try {AtomicFile.Write(s.FileName,json);}
            catch {AtomicFile.Write(s.Toml,original);throw;}
        }
        internal static string[] EosArguments(Settings s,bool hasWebRtc) {
            if(hasWebRtc)throw new InvalidDataException("Leave the WebRTC room before starting in-game matchmaking.");
            if(!File.Exists(s.EosConfig) || !File.Exists(Path.Combine(s.Build,"EOSSDK-Win64-Shipping.dll")))
                throw new InvalidDataException("Online components are missing. Repair this installation using the updated installer.");
            var arguments=new List<string>{"--redbaron_eos_config="+s.EosConfig,"--redbaron_online_players="+s.OnlineLocalPlayers};
            if(s.EosAccount)arguments.Add("--redbaron_eos_account=true");
            arguments.Add("--redbaron_eos_enabled=true");
            return arguments.ToArray();
        }
        static void EnsureEpicRuntime(Settings s) {
            string installer=Path.Combine(s.Build,"EpicOnlineServicesInstaller.exe");
            string service=Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86),"Epic Games","Epic Online Services","service","EpicOnlineServicesHost.exe");
            string marker=Path.Combine(s.Build,"eos","redistributable-installed.txt");
            string product=null;
            foreach(string line in File.ReadAllLines(s.EosConfig))
                if(line.StartsWith("product_id=",StringComparison.Ordinal))product=line.Substring(11).Trim();
            if(product==null || !System.Text.RegularExpressions.Regex.IsMatch(product,"^[0-9a-fA-F]{32}$"))
                throw new InvalidDataException("The local Epic product configuration is incomplete.");
            if(File.Exists(service) && File.Exists(marker) && File.ReadAllText(marker).Trim()==product)return;
            if(!File.Exists(installer))throw new FileNotFoundException("Epic social components are missing. Use the updated installer.");
            var setup=new ProcessStartInfo(installer,"/install productId="+product+" /quiet") {UseShellExecute=true,Verb="runas",WindowStyle=ProcessWindowStyle.Hidden};
            using(var process=Process.Start(setup)) {
                process.WaitForExit();
                if(process.ExitCode!=0 && process.ExitCode!=3010)throw new IOException("Epic Online Services setup failed (code "+process.ExitCode+"). Run Play again to retry setup.");
            }
            if(!File.Exists(service))throw new IOException("Epic Online Services setup did not complete. Restart Windows if requested, then try again.");
            AtomicFile.Write(marker,product);
        }
        public static Process Play(Settings s, IEnumerable<string> onlineArguments=null) {
            if(!File.Exists(s.Exe)) throw new FileNotFoundException("Game not installed. Open Install and select your local game files.");
            if(!File.Exists(Path.Combine(s.Root,"game","default.xex"))) throw new FileNotFoundException("Game files are missing from this installation.");
            if(IsGameRunning(s)) throw new InvalidOperationException("The game is already running.");
            var arguments=new List<string>{"--game_data_root="+Path.Combine(s.Root,"game")};
            arguments.AddRange(EosArguments(s,onlineArguments!=null));
            string logs=Path.Combine(s.Root,"launcher-data","logs");Directory.CreateDirectory(logs);
            string log=Path.Combine(logs,"game-"+DateTime.Now.ToString("yyyyMMdd-HHmmss-fff")+".log");
            arguments.Add("--log_file="+log);
            AtomicFile.Write(Path.Combine(logs,"last-session.json"),Json.Serializer.Serialize(new {version=Updates.CurrentVersion,startedUtc=DateTime.UtcNow.ToString("o"),eos=s.EosEnabled,gameLog=log}));
            if(onlineArguments!=null)arguments.AddRange(onlineArguments);
            string executable=s.Exe;
            if(s.EosEnabled && s.EosAccount) {
                executable=Path.Combine(s.Build,"RedBaron_Online.exe");
                if(!File.Exists(executable) || !File.Exists(Path.ChangeExtension(executable,".ini")))
                    throw new InvalidDataException("Epic social components are missing. Use the updated installer before enabling Epic account sign-in.");
                EnsureEpicRuntime(s);
                arguments.Add("--redbaron_eos_bootstrapped=true");
            }
            var info=new ProcessStartInfo(executable,String.Join(" ",arguments.Select(QuoteArgument))) {WorkingDirectory=s.Build,UseShellExecute=false};
            return Process.Start(info);
        }
        public static async Task WaitForGameWindow(Process game) {
            var deadline=DateTime.UtcNow.AddMinutes(6);
            while(DateTime.UtcNow<deadline) {
                game.Refresh();
                if(game.HasExited)throw new IOException("The game closed during startup. Check the game log and installation.");
                if(game.MainWindowHandle!=IntPtr.Zero)return;
                if(Path.GetFileName(game.StartInfo.FileName).Equals("RedBaron_Online.exe",StringComparison.OrdinalIgnoreCase)) {
                    foreach(var child in Process.GetProcessesByName("snoopyflyingace"))using(child) {
                        try {if(child.MainWindowHandle!=IntPtr.Zero && Path.GetDirectoryName(child.MainModule.FileName).Equals(game.StartInfo.WorkingDirectory,StringComparison.OrdinalIgnoreCase))return;}
                        catch(System.ComponentModel.Win32Exception){}
                    }
                }
                await Task.Delay(100);
            }
            throw new IOException("The game is still starting. Wait for its window or close it before trying again.");
        }
        static void ExtractResource(string name,string path) {
            using(var source=Assembly.GetExecutingAssembly().GetManifestResourceStream(name)) {
                if(source==null) throw new InvalidDataException("Missing installation resource: "+name);
                using(var output=File.Create(path)) source.CopyTo(output);
            }
        }
        public static bool HasInstallerPayload(){using(var source=Assembly.GetExecutingAssembly().GetManifestResourceStream("Source.zip"))return source!=null;}
        public static async Task<string> Install(Settings s,Action<string> log,CancellationToken token) {
            if(!HasInstallerPayload())throw new InvalidOperationException("Use the full installer to create or repair a game installation.");
            if(String.IsNullOrWhiteSpace(s.Destination)) throw new InvalidDataException("Choose a destination folder.");
            string job=InstallationCleanup.CreateJob(s.Root,"install");
            string archive=Path.Combine(job,"source.zip"), script=Path.Combine(job,"install.ps1");
            log("@@RBPROGRESS {\"stage\":\"Preparing installer\",\"completed\":0,\"total\":0}");
            ExtractResource("Source.zip",archive); ExtractResource("Install.ps1",script);
            ExtractResource("InstallProgress.ps1",Path.Combine(job,"install-progress.ps1"));
            string appConfig=Path.Combine(job,"launcher.config");ExtractResource("App.config",appConfig);
            string bootstrap=Path.Combine(job,"bootstrap-tools.ps1");ExtractResource("BootstrapTools.ps1",bootstrap);
            ExtractResource("ToolInventory.ps1",Path.Combine(job,"tool-inventory.ps1"));
            string sdkArchive="";
            using(var bundled=Assembly.GetExecutingAssembly().GetManifestResourceStream("Sdk.zip")) {
                if(bundled!=null) {sdkArchive=Path.Combine(job,"sdk.zip");using(var output=File.Create(sdkArchive))bundled.CopyTo(output);}
            }
            string powershell=Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.System),"WindowsPowerShell","v1.0","powershell.exe");
            string logfile=Path.Combine(job,"install.log");
            object gate=new object();
            Action<string> record=line=>{lock(gate) File.AppendAllText(logfile,line+Environment.NewLine,Encoding.UTF8);log(line);};
            record("Log: "+logfile);
            var arguments=new List<string>{"-NoProfile","-ExecutionPolicy","Bypass","-File",script,"-Source",s.Source,"-Destination",s.Destination,"-Sdk",s.Sdk,"-SourceArchive",archive,"-LauncherExe",LauncherExe,"-LauncherConfig",appConfig,"-BootstrapScript",bootstrap};
            if(sdkArchive.Length!=0){arguments.Add("-SdkArchive");arguments.Add(sdkArchive);}
            string runtime=Path.Combine(job,"installed-launcher.exe");ExtractResource("RuntimeLauncher.exe",runtime);
            arguments.Add("-RuntimeLauncher");arguments.Add(runtime);
            if(HasOfflineTools()) {
                string manifest=Path.Combine(job,"offline-tools.json");ExtractResource("OfflineTools.json",manifest);
                arguments.Add("-OfflineManifest");arguments.Add(manifest);
                arguments.Add("-OfflineToolsRoot");arguments.Add(Path.Combine(Path.GetDirectoryName(LauncherExe),"Tools"));
            }
            using(var online=Assembly.GetExecutingAssembly().GetManifestResourceStream("PrivateEosConfig.ini")) {
                if(online!=null) {
                    string config=Path.Combine(job,"eos-config.ini");
                    using(var output=File.Create(config))online.CopyTo(output);
                    arguments.Add("-OnlineConfig");arguments.Add(config);
                }
            }
            int code=await RunAsync(powershell,arguments,s.Root,record,token);
            if(code!=0) throw new IOException("Installation failed. See the log for details; your source files were preserved.");
            string installedLogs=Path.Combine(s.Destination,"launcher-data","logs");Directory.CreateDirectory(installedLogs);
            File.Copy(logfile,Path.Combine(installedLogs,"installation.log"),true);
            return job;
        }
        public static async Task<string> InstallDlc(Settings s,string source,Action<string> log,CancellationToken token) {
            if(IsGameRunning(s)) throw new InvalidOperationException("Close the game before installing downloadable content.");
            string inspector=Path.Combine(s.Build,"redbaron_content_inspect.exe");
            if(!File.Exists(inspector))throw new FileNotFoundException("Update the game installation to enable DLC installation.");
            string userRoot=File.Exists(s.Toml)?new Toml(File.ReadAllText(s.Toml)).String("user_data_root",""):"";
            if(String.IsNullOrWhiteSpace(userRoot))userRoot=Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments),"snoopyflyingace");
            else if(!Path.IsPathRooted(userRoot))userRoot=Path.GetFullPath(Path.Combine(s.Build,userRoot));
            string job=InstallationCleanup.CreateJob(s.Root,"dlc");
            string logs=Path.Combine(s.Root,"launcher-data","logs");Directory.CreateDirectory(logs);
            string logfile=Path.Combine(logs,"dlc-"+DateTime.Now.ToString("yyyyMMdd-HHmmss-fff")+".log");
            object gate=new object();Action<string> record=line=>{lock(gate)File.AppendAllText(logfile,line+Environment.NewLine,Encoding.UTF8);log(line);};
            string script=Path.Combine(job,"install-dlc.ps1");ExtractResource("InstallDlc.ps1",script);
            ExtractResource("InstallProgress.ps1",Path.Combine(job,"install-progress.ps1"));
            string powershell=Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.System),"WindowsPowerShell","v1.0","powershell.exe");
            int code=await RunAsync(powershell,new[]{"-NoProfile","-ExecutionPolicy","Bypass","-File",script,
                "-Source",source,"-UserRoot",userRoot,"-Inspector",inspector},s.Root,record,token);
            if(code!=0)throw new IOException("DLC installation failed. Check the installation log above; existing content was preserved.");
            return job;
        }
        public static void CreateShortcut(Settings s,string launcher) {
            string desktop=Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory);
            WriteShortcut(s,launcher,Path.Combine(desktop,"RedBaron_Recomp.lnk"));
        }
        internal static void WriteShortcut(Settings s,string launcher,string path) {
            var shellType=Type.GetTypeFromProgID("WScript.Shell");
            dynamic shell=Activator.CreateInstance(shellType);
            if(File.Exists(path)) File.Copy(path,path+".backup-"+DateTime.Now.ToString("yyyyMMdd_HHmmss_fff"));
            dynamic shortcut=shell.CreateShortcut(path);
            shortcut.TargetPath=launcher; shortcut.WorkingDirectory=s.Root;
            shortcut.Arguments="--root "+QuoteArgument(s.Root);
            shortcut.Description="RedBaron_Recomp — settings and game";
            shortcut.IconLocation=launcher+",0"; shortcut.Save();
            System.Runtime.InteropServices.Marshal.FinalReleaseComObject(shortcut);
            System.Runtime.InteropServices.Marshal.FinalReleaseComObject(shell);
        }
    }
}
