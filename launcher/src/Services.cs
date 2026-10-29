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
    public sealed class Gpu { public int index {get;set;} public string name {get;set;} public bool dlss {get;set;} }
    public sealed class Capabilities {
        public Dictionary<string,Flag> flags {get;set;}
        public List<Device> devices {get;set;}
        public List<Gpu> gpus {get;set;}
        public string inputError {get;set;}
        public bool hdrActive {get;set;}
        public int refreshHz {get;set;}
        public int desktopWidth {get;set;}
        public int desktopHeight {get;set;}
        public bool DlssAvailable(int adapter) { return gpus.Any(g=>g.dlss && (adapter<0 || g.index==adapter)); }
        public Capabilities() { flags=new Dictionary<string,Flag>(); devices=new List<Device>(); gpus=new List<Gpu>(); }
        public bool Supports(string key,string value) { Flag flag; return flags.TryGetValue(key,out flag) && (flag.allowed==null || flag.allowed.Length==0 || flag.allowed.Contains(value)); }
    }
    public static class Services {
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
            string updated=s.RuntimeText(original);
            string json=Json.Serializer.Serialize(s);
            AtomicFile.Write(s.Toml,updated);
            try {AtomicFile.Write(s.FileName,json);}
            catch {AtomicFile.Write(s.Toml,original);throw;}
        }
        public static Process Play(Settings s, IEnumerable<string> onlineArguments=null) {
            if(!File.Exists(s.Exe)) throw new FileNotFoundException("Game not installed. Open Install and select your local game files.");
            if(!File.Exists(Path.Combine(s.Root,"game","default.xex"))) throw new FileNotFoundException("Game files are missing from this installation.");
            if(IsGameRunning(s)) throw new InvalidOperationException("The game is already running.");
            var arguments=new List<string>{"--game_data_root="+Path.Combine(s.Root,"game")};
            if(onlineArguments!=null)arguments.AddRange(onlineArguments);
            var info=new ProcessStartInfo(s.Exe,String.Join(" ",arguments.Select(QuoteArgument))) {WorkingDirectory=s.Build,UseShellExecute=false};
            return Process.Start(info);
        }
        static void ExtractResource(string name,string path) {
            using(var source=Assembly.GetExecutingAssembly().GetManifestResourceStream(name)) {
                if(source==null) throw new InvalidDataException("Missing installation resource: "+name);
                using(var output=File.Create(path)) source.CopyTo(output);
            }
        }
        public static async Task Install(Settings s,Action<string> log,CancellationToken token) {
            if(String.IsNullOrWhiteSpace(s.Destination)) throw new InvalidDataException("Choose a destination folder.");
            string job=Path.Combine(s.Root,"launcher-data","install-"+DateTime.Now.ToString("yyyyMMdd_HHmmss_fff"));
            Directory.CreateDirectory(job);
            string archive=Path.Combine(job,"source.zip"), script=Path.Combine(job,"install.ps1");
            ExtractResource("Source.zip",archive); ExtractResource("Install.ps1",script);
            string powershell=Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.System),"WindowsPowerShell","v1.0","powershell.exe");
            string logfile=Path.Combine(job,"install.log");
            object gate=new object();
            Action<string> record=line=>{lock(gate) File.AppendAllText(logfile,line+Environment.NewLine,Encoding.UTF8);log(line);};
            record("Log: "+logfile);
            int code=await RunAsync(powershell,new[]{"-NoProfile","-ExecutionPolicy","Bypass","-File",script,"-Source",s.Source,"-Destination",s.Destination,"-Sdk",s.Sdk,"-SourceArchive",archive,"-LauncherExe",LauncherExe},s.Root,record,token);
            if(code!=0) throw new IOException("Installation failed. See the log for details; your source files were preserved.");
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
