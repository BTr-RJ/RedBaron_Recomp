using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Net;
using System.Net.Sockets;
using System.Security.Cryptography;
using System.Text;
using System.Threading.Tasks;

namespace RedBaron {
    static class SelfTests {
        [DllImport("shell32.dll",SetLastError=true)] static extern IntPtr CommandLineToArgvW([MarshalAs(UnmanagedType.LPWStr)] string line,out int count);
        [DllImport("kernel32.dll")] static extern IntPtr LocalFree(IntPtr memory);
        static void Assert(bool value,string message) {if(!value)throw new Exception(message);}
        static void Reject(Action action,string message) {try {action();} catch(InvalidDataException) {return;}throw new Exception(message);}
        static string DownloadTest(ReleaseAsset asset,string root,byte[] bytes) {
            var listener=new TcpListener(IPAddress.Loopback,0);listener.Start();
            int port=((IPEndPoint)listener.LocalEndpoint).Port;
            var server=Task.Run(()=>{
                using(var client=listener.AcceptTcpClient())using(var stream=client.GetStream()) {
                    var request=new byte[4096];stream.Read(request,0,request.Length);
                    byte[] header=Encoding.ASCII.GetBytes("HTTP/1.1 200 OK\r\nContent-Length: "+bytes.Length+"\r\nConnection: close\r\n\r\n");
                    stream.Write(header,0,header.Length);stream.Write(bytes,0,bytes.Length);
                }
            });
            try {return Updates.DownloadFromLocalTest(asset,root,"http://127.0.0.1:"+port+"/").GetAwaiter().GetResult();}
            finally {listener.Stop();server.GetAwaiter().GetResult();}
        }
        public static int Run(string[] args) {
            int index=Array.IndexOf(args,"--self-test");
            string directory=index+1<args.Length?Path.GetFullPath(args[index+1]):Path.Combine(Path.GetTempPath(),"RedBaron-tests-"+Guid.NewGuid().ToString("N"));
            Directory.CreateDirectory(directory);
            var results=new List<string>();
            try {
                InstallProgress progress;
                Assert(InstallProgress.TryParse("@@RBPROGRESS {\"stage\":\"Downloading\",\"completed\":25,\"total\":100}",out progress) && progress.Known && progress.Percent==25,"Measured download progress rejected");
                Assert(InstallProgress.TryParse("[90/180] Building CXX object",out progress) && progress.Percent==50 && progress.stage=="Compiling","Ninja progress not recognized");
                Assert(InstallProgress.TryParse("@@RBPROGRESS {\"stage\":\"Installing Microsoft tools\",\"completed\":0,\"total\":0}",out progress) && !progress.Known,"Unknown work received a fabricated percentage");
                foreach(var invalidProgress in new[]{"ordinary log","@@RBPROGRESS broken","@@RBPROGRESS {\"stage\":\"x\",\"completed\":11,\"total\":10}","[7/0] broken","@@RBPROGRESS {\"stage\":\"x\",\"completed\":-1,\"total\":10}"})Assert(!InstallProgress.TryParse(invalidProgress,out progress),"Malformed progress accepted");
                results.Add("Installation progress: measured byte counts, Ninja steps, unknown phases and malformed input");
                var install=new Settings {Root=Path.Combine(directory,"installer"),Destination=Path.Combine(directory,"new game"),Source="package"};
                var plan=Services.PlanInstallation(install);Assert(plan.InstallGame && !plan.InstallDlc,"Optional DLC became mandatory");
                install.DlcSource=Path.Combine(directory,"DLC packages");Directory.CreateDirectory(install.DlcSource);
                plan=Services.PlanInstallation(install);Assert(plan.InstallGame && plan.InstallDlc,"Combined installation was not planned");
                Directory.CreateDirectory(Path.Combine(install.Destination,"game"));Directory.CreateDirectory(Path.Combine(install.Destination,"build_cpp_release"));
                File.WriteAllText(Path.Combine(install.Destination,"game","default.xex"),"fixture");File.WriteAllText(Path.Combine(install.Destination,"build_cpp_release","snoopyflyingace.exe"),"fixture");
                plan=Services.PlanInstallation(install);Assert(!plan.InstallGame && plan.InstallDlc && plan.Root==install.Destination,"Existing destination would be rebuilt");
                install.Root=install.Destination;install.Source="";install.Destination="";
                plan=Services.PlanInstallation(install);Assert(!plan.InstallGame && plan.InstallDlc,"DLC-only operation required game source or destination");
                install.DlcSource="";Reject(()=>Services.PlanInstallation(install),"Installed game would be reinstalled with no DLC selected");
                install.DlcSource=Path.Combine(directory,"missing DLC");Reject(()=>Services.PlanInstallation(install),"Missing DLC source accepted");
                results.Add("Unified installation: game only, game plus DLCs, existing destination and DLC-only without rebuilding");
                var s=new Settings {Root=Path.Combine(directory,"installation with spaces"),Sdk="sdk",Source="game",Destination=""};
                string original="# user comment\nrender_target_path_d3d12 = \"rov\"\ncustom_option = 42\nkeybind_a = \"Space\" # turbo\n[custom]\nvalue = \"preserved\"\n";
                s.Players[0].Bindings[0].Keyboard="J,LMB";
                string updated=s.RuntimeText(original);
                var parsed=new Toml(updated);
                Assert(parsed.String("keybind_a","")=="J,LMB","Keyboard binding not persisted");
                Assert(parsed.String("render_target_path_d3d12","")=="rov","ROV lost");
                Assert(updated.Contains("custom_option = 42") && updated.Contains("[custom]\r\nvalue = \"preserved\""),"Unknown TOML content lost");
                Assert(updated.IndexOf("redbaron_pad_slots")<updated.IndexOf("[custom]"),"New settings ended up in a table");
                Assert(s.RuntimeText(updated)==updated,"Saving twice is not idempotent");
                results.Add("TOML: preserve unknown settings/tables, bind roundtrip, idempotence, ROV");
                s.Resolution="7680x4320";s.Scale=6;s.Vsync=false;s.FsrSharpness=50;
                var video=new Toml(s.RuntimeText("draw_resolution_scale_x = 2\ndraw_resolution_scale_y = 3\nvsync = false\n"));
                Assert(video.Int("window_width",0)==7680 && video.Int("window_height",0)==4320,"8K output not saved");
                Assert(video.Int("video_mode_width",0)==1280 && video.Int("video_mode_height",0)==720,"Guest layout changed");
                Assert(video.Int("resolution_scale",0)==6 && video.Int("draw_resolution_scale_x",0)==6 && video.Int("draw_resolution_scale_y",0)==6,"Render scale axis overrides survived");
                Assert(video.Bool("vsync",false) && !video.Bool("present_vsync",true),"Monitor VSync changed guest timing");
                Assert(video.Number("present_fsr_sharpness_reduction",-1)==1.0,"FSR sharpness conversion failed");
                s.Scale=7;Reject(()=>s.Validate(),"Unbounded render scale accepted");s.Scale=2;
                results.Add("Video: 8K output, internal scaling, stable guest layout, separate monitor VSync and FSR sharpness");
                Reject(()=>new Toml("vsync=true\nvsync=false"),"Duplicate root keys accepted");
                Reject(()=>Actions.ValidateKeyboard("W\"\nfullscreen=true"),"Injected key binding accepted");
                Actions.ValidateKeyboard("Ctrl+W,LMB");Actions.ValidateKeyboard("");
                foreach(var pair in new Dictionary<System.Windows.Input.Key,string> {
                    {System.Windows.Input.Key.OemComma,"Comma"},{System.Windows.Input.Key.OemPeriod,"Period"},
                    {System.Windows.Input.Key.OemMinus,"Minus"},{System.Windows.Input.Key.OemPlus,"Plus"},
                    {System.Windows.Input.Key.Oem1,"Semicolon"},{System.Windows.Input.Key.Oem2,"Slash"},
                    {System.Windows.Input.Key.Oem3,"Backtick"},{System.Windows.Input.Key.Oem4,"LBracket"},
                    {System.Windows.Input.Key.Oem5,"Backslash"},{System.Windows.Input.Key.Oem6,"RBracket"},
                    {System.Windows.Input.Key.Oem7,"Quote"}}) {
                    string name=KeyboardNames.FromKey(pair.Key,pair.Key.ToString());
                    Assert(name==pair.Value,"OEM key capture name mismatch");Actions.ValidateKeyboard(name);
                }
                results.Add("OEM punctuation capture maps to SDK key names");
                results.Add("TOML and input validation reject malformed/duplicate values");
                s.PlayerName="ABCDEFGHIJKLMNOP";Reject(()=>s.Validate(),"Long name accepted");
                s.PlayerName="Player@";Reject(()=>s.Validate(),"Invalid symbol accepted");
                s.PlayerName="Player 2";s.Validate();
                string device="0300fa675e040000ff02000000007801#0";
                s.Players[0].Device=device;s.Players[1].Device=device;Reject(()=>s.Validate(),"Controller assigned twice");s.Players[1].Device="auto";
                s.KeyboardPlayer=1;Reject(()=>s.Validate(),"Exclusive keyboard slot also got a pad");s.KeyboardPlayer=0;
                results.Add("Name limits, duplicate controllers and reserved keyboard slot validated");
                Directory.CreateDirectory(s.Build);
                File.WriteAllText(s.Exe,"test sentinel, not executable");
                File.WriteAllText(s.Toml,original);
                var caps=new Capabilities();caps.flags["present_effect"]=new Flag{allowed=new[]{"bilinear"}};caps.flags["swap_post_effect"]=new Flag{allowed=new[]{"none","fxaa"}};
                caps.flags["present_vsync"]=new Flag();
                caps.flags["window_size_in_physical_pixels"]=new Flag();
                s.Upscaling="dlss";Reject(()=>Services.Save(s,caps),"Unsupported DLSS accepted");
                Assert(File.ReadAllText(s.Toml)==original,"Failed save modified runtime config");
                s.Upscaling="bilinear";Services.Save(s,caps);
                var loaded=Settings.Load(s.Root);
                Assert(loaded.PlayerName=="Player 2" && new Toml(File.ReadAllText(s.Toml)).String("redbaron_player_name","")=="Player 2","Player name did not reach runtime settings");
                loaded.LanEnabled=true;loaded.LanPeer="192.168.1.20";
                string beforeLan=File.ReadAllText(s.Toml);
                Reject(()=>Services.Save(loaded,caps),"Old runtime accepted LAN play");
                Assert(File.ReadAllText(s.Toml)==beforeLan,"Rejected LAN save changed the configuration");
                caps.flags["redbaron_lan"]=new Flag();caps.flags["redbaron_lan_peer"]=new Flag();
                Services.Save(loaded,caps);
                Assert(Settings.Load(s.Root).LanEnabled && Settings.Load(s.Root).LanPeer=="192.168.1.20","LAN settings did not roundtrip");
                foreach(string invalid in new[]{"localhost","1.2.3","999.1.2.3","1.02.3.4","0.0.0.0","1.2.3.4\n"}) {
                    loaded.LanPeer=invalid;Reject(()=>loaded.Validate(),"Invalid LAN IPv4 accepted");
                }
                loaded.LanPeer="255.255.255.255";loaded.LanEnabled=false;Services.Save(loaded,caps);
                results.Add("LAN: runtime gating, IPv4 validation, persistence and rejected-save preservation");
                foreach(string invalid in new[]{"http://example.org","ws://example.org:8787","wss://user:pass@example.org","wss://example.org/#fragment"})
                    Reject(()=>Settings.ValidateOnlineServer(invalid,true),"Invalid room server accepted");
                Reject(()=>Settings.ValidateOnlineServer("",true),"Missing room server accepted");
                loaded.OnlineServer="wss://play.example.org/rooms";loaded.OnlineMode="coop";loaded.OnlineLocalPlayers=2;
                Services.Save(loaded,caps);
                var onlineLoaded=Settings.Load(s.Root);
                Assert(onlineLoaded.OnlineServer==loaded.OnlineServer && onlineLoaded.OnlineMode=="coop" && onlineLoaded.OnlineLocalPlayers==2,"Online room settings did not roundtrip");
                Assert(!File.ReadAllText(s.Toml).Contains("redbaron_p2p_namespace"),"Temporary room identity leaked into persistent game settings");
                string invite=OnlineSession.Invitation("wss://play.example.org/rooms?region=sa","0123456789ABCDEF"), inviteServer;
                Assert(OnlineSession.ParseInvitation(invite,out inviteServer)=="0123456789ABCDEF" && inviteServer=="wss://play.example.org/rooms?region=sa","Invitation did not roundtrip");
                Assert(OnlineSession.ParseInvitation("0123456789abcdef",out inviteServer)=="0123456789ABCDEF" && inviteServer==null,"Plain room code did not roundtrip");
                foreach(string invalidInvite in new[]{"", "redbaron://join/0123456789ABCDEF?server=ws%3A%2F%2Fevil.example", "redbaron://join/0123456789ABCDEF?server=wss%3A%2F%2Fexample.org&extra=1", "redbaron://join/invalid?server=wss%3A%2F%2Fexample.org"})
                    Reject(()=>{string ignored;OnlineSession.ParseInvitation(invalidInvite,out ignored);},"Invalid invitation accepted");
                results.Add("Online invitations: server and room roundtrip; empty and unsafe inputs rejected");
                results.Add("Online: TLS URL validation, room preferences and temporary identity separation");
                loaded.EosEnabled=true;
                Reject(()=>Services.Save(loaded,caps),"Unsupported EOS build accepted");
                caps.eosAvailable=true;
                Reject(()=>Services.EosArguments(loaded,false),"Missing EOS installation accepted");
                Directory.CreateDirectory(Path.GetDirectoryName(loaded.EosConfig));
                File.WriteAllText(loaded.EosConfig,"test fixture, not credentials");
                File.WriteAllText(Path.Combine(loaded.Build,"EOSSDK-Win64-Shipping.dll"),"test fixture");
                Services.Save(loaded,caps);
                Assert(Settings.Load(loaded.Root).EosEnabled,"EOS preference did not roundtrip");
                var eosArgs=Services.EosArguments(loaded,false);
                Assert(eosArgs.Length==2 && eosArgs[0]=="--redbaron_eos_config="+loaded.EosConfig && eosArgs[1]=="--redbaron_online_players=2","EOS launch arguments are incorrect");
                Reject(()=>Services.EosArguments(loaded,true),"Concurrent EOS and WebRTC providers accepted");
                loaded.EosEnabled=false;Assert(Services.EosArguments(loaded,false).Length==0,"Disabled EOS emitted launch arguments");
                var freshOnline=new Settings {Root=Path.Combine(directory,"fresh online installation")};
                Directory.CreateDirectory(Path.GetDirectoryName(freshOnline.EosConfig));File.WriteAllText(freshOnline.EosConfig,"fixture");
                Assert(Settings.Load(freshOnline.Root).EosEnabled,"Fresh online installation did not enable EOS");
                freshOnline.EosEnabled=false;Json.Write(freshOnline.FileName,freshOnline);
                Assert(!Settings.Load(freshOnline.Root).EosEnabled,"Existing offline preference was overwritten");
                Services.Save(loaded,caps);
                results.Add("EOS: capability gating, missing components, saved preference, launch arguments and provider exclusivity");
                var release=new ReleaseInfo{tag_name="v0.11",assets=new[]{new ReleaseAsset {
                    name="RedBaron_Recomp-v0.11-windows-x64.zip",size=1234,digest="sha256:"+new string('a',64),
                    browser_download_url="https://github.com/BTr-RJ/RedBaron_Recomp/releases/download/v0.11/RedBaron_Recomp-v0.11-windows-x64.zip"}}};
                Assert(Updates.Parse(Json.Serializer.Serialize(release))!=null,"Valid release rejected");
                release.assets[0].browser_download_url="https://example.org/update.zip";
                Reject(()=>Updates.Parse(Json.Serializer.Serialize(release)),"Untrusted update host accepted");
                release.assets[0].browser_download_url="https://github.com/BTr-RJ/RedBaron_Recomp/releases/download/v0.11/RedBaron_Recomp-v0.11-windows-x64.zip";
                release.assets[0].digest="sha256:bad";
                Reject(()=>Updates.Parse(Json.Serializer.Serialize(release)),"Unverified update accepted");
                release.assets[0].digest="sha256:"+new string('a',64);release.assets[0].size=1600L*1024*1024;
                Assert(Updates.Parse(Json.Serializer.Serialize(release))!=null,"Offline tool package rejected by update size limit");
                release.assets[0].size=3L*1024*1024*1024;
                Reject(()=>Updates.Parse(Json.Serializer.Serialize(release)),"Oversized update accepted");
                byte[] payload=Encoding.UTF8.GetBytes("test update archive bytes");
                release.assets[0].size=payload.Length;
                using(var hash=SHA256.Create())release.assets[0].digest="sha256:"+BitConverter.ToString(hash.ComputeHash(payload)).Replace("-","");
                string downloaded=DownloadTest(release.assets[0],s.Root,payload);
                Assert(File.ReadAllBytes(downloaded).SequenceEqual(payload),"Verified update download differs");
                release.assets[0].digest="sha256:"+new string('0',64);
                Reject(()=>DownloadTest(release.assets[0],s.Root,payload),"Bad update checksum accepted");
                Assert(!Directory.GetFiles(Path.GetDirectoryName(downloaded),"*.partial").Any(),"Partial update was not cleaned up");
                results.Add("Update metadata: release version, trusted host, digest and package size");
                Assert(loaded.Scale==2 && loaded.Resolution=="7680x4320" && !loaded.Vsync && loaded.FsrSharpness==50,"Video settings did not roundtrip");
                loaded.Fps=120;Reject(()=>Services.Save(loaded,caps),"Old game build accepted an unsupported FPS target");
                caps.flags["redbaron_fps"]=new Flag{allowed=Settings.FrameRates.Select(fps=>fps.ToString()).ToArray()};
                Services.Save(loaded,caps);
                var timed=new Toml(File.ReadAllText(s.Toml));
                Assert(timed.Int("redbaron_fps",0)==120 && timed.Int("video_mode_refresh_rate",0)==120,"Engine and virtual display timing diverged");
                Assert(!timed.Bool("present_vsync",true),"FPS target changed monitor VSync");
                Assert(Settings.Load(s.Root).Fps==120,"FPS target did not roundtrip");
                loaded.Fps=0;Services.Save(loaded,caps);
                Assert(new Toml(File.ReadAllText(s.Toml)).Int("video_mode_refresh_rate",0)==60,"Original virtual refresh not restored");
                results.Add("FPS: capability gating, persistence, virtual display timing and default restoration");
                loaded.Upscaling="fsr3";
                string beforeFsr=File.ReadAllText(s.Toml);
                Reject(()=>Services.Save(loaded,caps),"Spatial-only runtime accepted temporal FSR");
                Assert(File.ReadAllText(s.Toml)==beforeFsr,"Rejected FSR save modified settings");
                caps.flags["present_effect"].allowed=new[]{"bilinear","fsr","fsr3"};
                File.WriteAllText(s.Toml,new Toml(beforeFsr).Patch(new Dictionary<string,string>{{"present_fsr_quality_mode","\"performance\""}}));
                Services.Save(loaded,caps);
                var temporal=new Toml(File.ReadAllText(s.Toml));
                Assert(temporal.String("present_effect","")=="fsr3" && temporal.String("present_fsr_quality_mode","")=="auto","Temporal FSR did not preserve explicit render scale");
                Assert(Settings.Load(s.Root).Upscaling=="fsr3","FSR 3.1.5 did not roundtrip");
                loaded.Upscaling="bilinear";Services.Save(loaded,caps);
                results.Add("FSR 3.1.5: runtime gating, rejected-save preservation, render scale selection and settings roundtrip");
                string beforeVideo=File.ReadAllText(s.Toml);
                loaded.ShowFps=true;loaded.Hdr=true;
                Reject(()=>Services.Save(loaded,caps),"Old runtime accepted FPS overlay/HDR");
                Assert(File.ReadAllText(s.Toml)==beforeVideo,"Unsupported display feature changed the TOML");
                caps.flags["redbaron_show_fps"]=new Flag();caps.flags["present_hdr"]=new Flag();
                Services.Save(loaded,caps);
                var display=Settings.Load(s.Root);
                Assert(display.ShowFps && display.Hdr,"FPS counter and HDR did not roundtrip");
                // HDR preference may be saved on SDR hardware; runtime must fall back safely.
                Assert(!caps.hdrActive,"Test must exercise an inactive HDR display");
                caps.flags["present_effect"].allowed=new[]{"bilinear","fsr","fsr3","dlss"};
                caps.gpus.Add(new Gpu{index=0,name="Unsupported test GPU",dlss=false});
                caps.gpus.Add(new Gpu{index=1,name="Supported test GPU",dlss=true});
                loaded.Upscaling="dlss";loaded.Adapter=0;
                string beforeDlss=File.ReadAllText(s.Toml);
                Reject(()=>Services.Save(loaded,caps),"Unsupported GPU accepted DLSS");
                Assert(File.ReadAllText(s.Toml)==beforeDlss,"Rejected DLSS save modified settings");
                caps.flags["present_fsr_quality_mode"]=new Flag{allowed=Settings.DlssModes};
                caps.desktopWidth=3840;caps.desktopHeight=2160;
                loaded.Adapter=1;loaded.Fullscreen=true;Services.Save(loaded,caps);
                Assert(Settings.Load(s.Root).Upscaling=="dlss","DLSS selection did not roundtrip");
                int[] expectedScales={3,2,2,2,1};
                for(int mode=0;mode<Settings.DlssModes.Length;mode++) {
                    loaded.DlssMode=Settings.DlssModes[mode];Services.Save(loaded,caps);
                    var dlss=Settings.Load(s.Root);
                    Assert(dlss.Scale==expectedScales[mode] && dlss.DlssMode==loaded.DlssMode,"DLSS mode or render resolution did not roundtrip");
                }
                loaded.Fullscreen=false;loaded.Resolution="1920x1080";loaded.DlssMode="quality";
                Services.Save(loaded,caps);Assert(Settings.Load(s.Root).Scale==1,"Windowed DLSS used desktop dimensions");
                loaded.DlssMode="invalid";Reject(()=>loaded.Validate(),"Invalid DLSS mode accepted");loaded.DlssMode="quality";
                results.Add("DLSS: five NGX modes, automatic 4K render scales and windowed resolution selection");
                loaded.Upscaling="bilinear";loaded.Adapter=-1;Services.Save(loaded,caps);
                results.Add("FPS overlay/HDR: capability gating and persistence; DLSS: actual GPU capability selection and rejected-save preservation");
                Assert(loaded.Players[0].Bindings[0].Keyboard=="J,LMB","Load did not roundtrip mappings");
                Assert(Directory.GetFiles(s.Build,"*.backup-*").Length>0,"No backup created");
                Assert(loaded.ShowLauncher,"Show launcher preference lost");
                loaded.ShowLauncher=false;Services.Save(loaded,caps);Assert(!Settings.Load(s.Root).ShowLauncher,"Direct-launch preference lost");
                results.Add("Save/load, backup, unsupported feature rejection and direct launch preference");
                var legacy=new Settings {Root=Path.Combine(directory,"legacy profiles"),PlayerName="ExistingPlayer",ShowLauncher=false};
                for(int p=0;p<4;p++) for(int b=0;b<Actions.Keys.Length;b++) {
                    var binding=legacy.Players[p].Bindings[b];
                    binding.Action="Funcao antiga";
                    binding.Keyboard="Ctrl+G";
                    binding.Pad=(b+p+1)%Actions.Keys.Length;
                }
                legacy.Players[0].Bindings[0].Action="Turbo / confirmar";
                legacy.Players[0].Device=device;
                Json.Write(legacy.FileName,legacy);
                string legacyJson=File.ReadAllText(legacy.FileName);
                var english=Settings.Load(legacy.Root);
                Assert(File.ReadAllText(legacy.FileName)==legacyJson,"Loading translated labels wrote to user settings");
                Assert(english.PlayerName==legacy.PlayerName && !english.ShowLauncher && english.Players[0].Device==device,"Translation changed user preferences");
                for(int p=0;p<4;p++) for(int b=0;b<Actions.Keys.Length;b++) {
                    var binding=english.Players[p].Bindings[b];
                    Assert(binding.Action==Actions.Labels[b],"Old display language survived loading");
                    Assert(binding.Key==Actions.Keys[b] && binding.Keyboard=="Ctrl+G" && binding.Pad==(b+p+1)%Actions.Keys.Length,"Translation changed a saved binding");
                }
                Assert(System.Globalization.CultureInfo.CurrentUICulture.Name=="en-US","Launcher UI culture is not English");
                results.Add("English labels replace old saved labels across four profiles without changing bindings or writing files");
                string[] paths={"plain","C:\\a path\\","C:\\game & files\\a.ps1","value\"quoted",""};
                string line="launcher.exe "+String.Join(" ",paths.Select(Services.QuoteArgument));
                int count;IntPtr argv=CommandLineToArgvW(line,out count);
                try {
                    Assert(count==paths.Length+1,"Argument count changed");
                    for(int i=0;i<paths.Length;i++)Assert(Marshal.PtrToStringUni(Marshal.ReadIntPtr(argv,(i+1)*IntPtr.Size))==paths[i],"Argument quoting failed");
                } finally {LocalFree(argv);}
                results.Add("Windows argument quoting: spaces, shell characters, quotes and trailing backslashes");
                string shortcutPath=Path.Combine(directory,"RedBaron test.lnk");
                Services.WriteShortcut(s,Services.LauncherExe,shortcutPath);
                dynamic shell=Activator.CreateInstance(Type.GetTypeFromProgID("WScript.Shell"));
                dynamic shortcut=shell.CreateShortcut(shortcutPath);
                try {
                    Assert(String.Equals((string)shortcut.TargetPath,Services.LauncherExe,StringComparison.OrdinalIgnoreCase),"Shortcut target changed");
                    Assert((string)shortcut.Arguments=="--root "+Services.QuoteArgument(s.Root),"Shortcut arguments changed");
                    Assert(((string)shortcut.IconLocation).StartsWith(Services.LauncherExe,StringComparison.OrdinalIgnoreCase),"Shortcut icon missing");
                } finally {Marshal.FinalReleaseComObject(shortcut);Marshal.FinalReleaseComObject(shell);}
                results.Add("Windows shortcut target, root argument and original launcher icon");
                Json.Write(Path.Combine(directory,"results.json"),new {passed=true,checks=results});return 0;
            } catch(Exception error) {Json.Write(Path.Combine(directory,"results.json"),new {passed=false,checks=results,error=error.ToString()});return 1;}
        }
    }
}
