using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;

namespace RedBaron {
    static class SelfTests {
        [DllImport("shell32.dll",SetLastError=true)] static extern IntPtr CommandLineToArgvW([MarshalAs(UnmanagedType.LPWStr)] string line,out int count);
        [DllImport("kernel32.dll")] static extern IntPtr LocalFree(IntPtr memory);
        static void Assert(bool value,string message) {if(!value)throw new Exception(message);}
        static void Reject(Action action,string message) {try {action();} catch(InvalidDataException) {return;}throw new Exception(message);}
        public static int Run(string[] args) {
            int index=Array.IndexOf(args,"--self-test");
            string directory=index+1<args.Length?Path.GetFullPath(args[index+1]):Path.Combine(Path.GetTempPath(),"RedBaron-tests-"+Guid.NewGuid().ToString("N"));
            Directory.CreateDirectory(directory);
            var results=new List<string>();
            try {
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
