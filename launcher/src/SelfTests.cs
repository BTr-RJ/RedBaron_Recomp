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
                s.Upscaling="dlss";Reject(()=>Services.Save(s,caps),"Unsupported DLSS accepted");
                Assert(File.ReadAllText(s.Toml)==original,"Failed save modified runtime config");
                s.Upscaling="bilinear";Services.Save(s,caps);
                var loaded=Settings.Load(s.Root);
                Assert(loaded.Players[0].Bindings[0].Keyboard=="J,LMB","Load did not roundtrip mappings");
                Assert(Directory.GetFiles(s.Build,"*.backup-*").Length>0,"No backup created");
                Assert(loaded.ShowLauncher,"Show launcher preference lost");
                loaded.ShowLauncher=false;Services.Save(loaded,caps);Assert(!Settings.Load(s.Root).ShowLauncher,"Direct-launch preference lost");
                results.Add("Save/load, backup, unsupported feature rejection and direct launch preference");
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
