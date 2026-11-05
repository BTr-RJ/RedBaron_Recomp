using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Linq;

namespace RedBaron {
    static class AutomaticUpdateTests {
        static void Assert(bool value,string message){if(!value)throw new Exception(message);}
        static void Write(string root,string name,string value){string file=Path.Combine(root,name);Directory.CreateDirectory(Path.GetDirectoryName(file));File.WriteAllText(file,value);}
        static string Fixture(string directory,string name) {
            string root=Path.Combine(directory,name);Directory.CreateDirectory(root);
            Write(root,".redbaron-install.json",Json.Serializer.Serialize(new {owner="RedBaronInstaller/v1",titleId="584109B5",status="ready",cleanupPending=false}));
            Write(root,"game/default.xex","original game");Write(root,"build_cpp_release/snoopyflyingace.exe","old game runtime");
            Write(root,"build_cpp_release/rexruntime.dll","old runtime");Write(root,"build_cpp_release/rexgpu-xenos.dll","old GPU");
            Write(root,"build_cpp_release/snoopyflyingace.toml","user_data_root = 'saves'\r\nuser_language = 9\r\n");
            Write(root,"build_cpp_release/eos/config.ini","personal config");Write(root,"build_cpp_release/saves/profile.dat","save data");
            Write(root,"RedBaron_Launcher.exe","old launcher");Write(root,"RedBaron_Launcher.exe.config","old launcher config");
            Write(root,"launcher-settings.json",Json.Serializer.Serialize(new Settings{Root=root}));Write(root,"dlc/owned.pkg","original DLC");return root;
        }
        static string Stage(string root,out string job) {
            job=InstallationCleanup.CreateJob(root,"update");string stage=Path.Combine(job,"stage");
            foreach(string name in new[]{"RedBaron_Launcher.exe","RedBaron_Launcher.exe.config","build_cpp_release/snoopyflyingace.exe","build_cpp_release/rexruntime.dll","build_cpp_release/rexgpu-xenos.dll","build_cpp_release/redbaron_probe.exe","build_cpp_release/added.dll","licenses/new.txt"})Write(stage,name,"new "+name);
            Write(stage,".redbaron-install.json",File.ReadAllText(Path.Combine(root,".redbaron-install.json")));
            Write(stage,"build_cpp_release/snoopyflyingace.toml","new default settings");Write(stage,"build_cpp_release/eos/config.ini","new default EOS config");
            Write(stage,"build_cpp_release/new-component.ini","new component defaults");
            return stage;
        }
        static Dictionary<string,string> Snapshot(string root) {
            return Directory.GetFiles(root,"*",SearchOption.AllDirectories).Where(p=>!AutomaticUpdate.Within(p,Path.Combine(root,"launcher-data"))).ToDictionary(p=>p.Substring(root.Length+1),AutomaticUpdate.Hash);
        }
        static void Same(string root,Dictionary<string,string> expected) {
            var actual=Snapshot(root);Assert(expected.Count==actual.Count && expected.All(p=>actual.ContainsKey(p.Key)&&p.Value==actual[p.Key]),"Update failure did not restore original files byte for byte");
        }
        public static void Run(string directory,List<string> results) {
            string area=Path.Combine(directory,"automatic-updates");Directory.CreateDirectory(area);string job;
            string root=Fixture(area,"successful update"),stage=Stage(root,out job);var original=Snapshot(root);
            AutomaticUpdate.Deploy(root,job,stage,line=>{});
            foreach(string protectedFile in new[]{"game/default.xex","dlc/owned.pkg","launcher-settings.json","build_cpp_release/snoopyflyingace.toml","build_cpp_release/eos/config.ini","build_cpp_release/saves/profile.dat"})
                Assert(AutomaticUpdate.Hash(Path.Combine(root,protectedFile))==original[protectedFile.Replace('/',Path.DirectorySeparatorChar)],"Update overwrote personal data: "+protectedFile);
            Assert(File.ReadAllText(Path.Combine(root,"build_cpp_release/rexruntime.dll"))=="new build_cpp_release/rexruntime.dll" && File.ReadAllText(Path.Combine(root,"build_cpp_release/rexgpu-xenos.dll"))=="new build_cpp_release/rexgpu-xenos.dll","Runtime/GPU pair not updated");
            Assert(File.Exists(Path.Combine(job,"backup/RedBaron_Launcher.exe"))&&!File.Exists(Path.Combine(root,"launcher-data/update-pending.json")),"Backup/commit marker missing");
            Assert(File.Exists(Path.Combine(root,"build_cpp_release/new-component.ini")),"New component configuration was not installed");
            results.Add("Automatic update: complete runtime replacement preserves original game, DLCs, saves, TOML, EOS and launcher settings");

            root=Fixture(area,"rollback");stage=Stage(root,out job);original=Snapshot(root);bool failed=false;
            try {AutomaticUpdate.Deploy(root,job,stage,line=>{},count=>{if(count==5)throw new IOException("Injected replacement failure");});}catch(IOException){failed=true;}
            Assert(failed,"Failure was not injected");Same(root,original);
            Assert(!File.Exists(Path.Combine(root,"launcher-data/update-pending.json")),"Rollback left a pending marker");
            results.Add("Automatic update: replacement failure restores changed files and removes newly added files");

            root=Fixture(area,"locked runtime");stage=Stage(root,out job);original=Snapshot(root);failed=false;
            using(var locked=File.Open(Path.Combine(root,"build_cpp_release/rexruntime.dll"),FileMode.Open,FileAccess.Read,FileShare.Read))
                try{AutomaticUpdate.Deploy(root,job,stage,line=>{});}catch(IOException){failed=true;}
            Assert(failed,"Locked runtime was overwritten");Same(root,original);
            results.Add("Automatic update: locked runtime fails safely and restores the previous launcher");

            root=Fixture(area,"power interruption");stage=Stage(root,out job);original=Snapshot(root);
            string relative="build_cpp_release/rexruntime.dll",backup=Path.Combine(job,"backup",relative);Write(job,"backup/"+relative,File.ReadAllText(Path.Combine(root,relative)));
            var journal=new UpdateJournal{owner="RedBaronUpdater/v1",root=root,status="applying",entries=new List<UpdateEntry>{new UpdateEntry{path=relative,existed=true,hash=AutomaticUpdate.Hash(backup)}}};
            Write(job,"transaction.json",Json.Serializer.Serialize(journal));Write(root,"launcher-data/update-pending.json",Json.Serializer.Serialize(new {job=job}));Write(root,relative,"partly updated runtime");
            AutomaticUpdate.Recover(root,line=>{});AutomaticUpdate.Recover(root,line=>{});Same(root,original);
            results.Add("Automatic update: interrupted transaction recovery is persistent and idempotent");

            Write(job,"transaction.json",Json.Serializer.Serialize(journal));Write(root,"launcher-data/update-pending.json",Json.Serializer.Serialize(new {job=job}));Write(job,"backup/"+relative,"corrupted backup");failed=false;
            try{AutomaticUpdate.Recover(root,line=>{});}catch(InvalidDataException){failed=true;}
            Assert(failed && File.Exists(Path.Combine(root,"launcher-data/update-pending.json")),"Corrupt recovery backup was accepted");Same(root,original);
            results.Add("Automatic update: corrupted backups block recovery without modifying live files");

            root=Fixture(area,"save overlap");stage=Stage(root,out job);Write(root,"build_cpp_release/snoopyflyingace.toml","user_data_root = '.'\r\n");original=Snapshot(root);failed=false;
            try{AutomaticUpdate.Deploy(root,job,stage,line=>{});}catch(InvalidDataException){failed=true;}
            Assert(failed,"Save directory overlap accepted");Same(root,original);
            Directory.CreateDirectory(Path.Combine(root,".git"));failed=false;try{AutomaticUpdate.ValidateInstallation(root);}catch(InvalidDataException){failed=true;}
            Assert(failed,"Developer checkout accepted");
            results.Add("Automatic update: custom save overlap and developer checkouts rejected before replacement");

            string package=Path.Combine(area,"package.zip");
            using(var zip=ZipFile.Open(package,ZipArchiveMode.Create))foreach(string name in new[]{"RedBaron_Launcher.exe","RedBaron_Launcher.exe.config","INSTALLATION.md","CREDITS.md"})
                using(var output=new StreamWriter(zip.CreateEntry("RedBaron_Recomp-v99.1/"+name).Open()))output.Write(name);
            var asset=new ReleaseAsset{name="RedBaron_Recomp-v99.1-windows-x64.zip",size=new FileInfo(package).Length,digest="sha256:"+AutomaticUpdate.Hash(package)};
            string extracted=AutomaticUpdate.ExtractPackage(package,asset,Path.Combine(area,"extract good"));Assert(File.Exists(extracted),"Valid update ZIP rejected");
            using(var zip=ZipFile.Open(package,ZipArchiveMode.Update))using(var output=new StreamWriter(zip.CreateEntry("RedBaron_Recomp-v99.1/../../escape.exe").Open()))output.Write("bad");
            asset.size=new FileInfo(package).Length;asset.digest="sha256:"+AutomaticUpdate.Hash(package);failed=false;
            try{AutomaticUpdate.ExtractPackage(package,asset,Path.Combine(area,"extract bad"));}catch(InvalidDataException){failed=true;}
            Assert(failed && !File.Exists(Path.Combine(area,"escape.exe")),"ZIP traversal accepted");
            asset.digest="sha256:"+new string('0',64);failed=false;try{AutomaticUpdate.ExtractPackage(package,asset,Path.Combine(area,"changed package"));}catch(InvalidDataException){failed=true;}
            Assert(failed,"Changed update ZIP accepted");
            string windowsPackage=Path.Combine(area,"windows-package.zip");
            using(var zip=ZipFile.Open(windowsPackage,ZipArchiveMode.Create))foreach(string name in new[]{"RedBaron_Launcher.exe","RedBaron_Launcher.exe.config","INSTALLATION.md","CREDITS.md"})
                using(var output=new StreamWriter(zip.CreateEntry("RedBaron_Recomp-v99.1\\"+name).Open()))output.Write(name);
            asset.size=new FileInfo(windowsPackage).Length;asset.digest="sha256:"+AutomaticUpdate.Hash(windowsPackage);
            Assert(File.Exists(AutomaticUpdate.ExtractPackage(windowsPackage,asset,Path.Combine(area,"extract windows"))),"Windows-created update ZIP rejected");
            results.Add("Automatic update: Windows/Unix ZIP names, content allowlist, traversal rejection and hash revalidation before extraction");
        }
    }
}
