using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Security.Cryptography;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;

namespace RedBaron {
    public sealed class UpdateEntry {
        public string path {get;set;}
        public bool existed {get;set;}
        public string hash {get;set;}
    }
    public sealed class UpdateJournal {
        public string owner {get;set;}
        public string root {get;set;}
        public string version {get;set;}
        public string status {get;set;}
        public List<UpdateEntry> entries {get;set;}
    }
    // The old installation stays playable throughout compilation. Only a complete,
    // probed staging build can enter the journalled file replacement transaction.
    public static class AutomaticUpdate {
        const string Owner="RedBaronUpdater/v1";
        const string Pending="launcher-data/update-pending.json";
        internal static string Full(string path) {return Path.GetFullPath(path).TrimEnd('\\','/');}
        internal static bool Within(string path,string parent) {return Full(path).StartsWith(Full(parent)+"\\",StringComparison.OrdinalIgnoreCase);}
        internal static void NoLinks(string path) {
            for(string current=Full(path);!String.IsNullOrEmpty(current);current=Path.GetDirectoryName(current))
                if((File.Exists(current)||Directory.Exists(current)) && (File.GetAttributes(current)&FileAttributes.ReparsePoint)!=0)
                    throw new InvalidDataException("Updates cannot follow symbolic links or junctions: "+current);
        }
        internal static string Hash(string path) {
            using(var sha=SHA256.Create())using(var input=File.OpenRead(path))return BitConverter.ToString(sha.ComputeHash(input)).Replace("-","");
        }
        internal static string MutexName(string root) {
            using(var sha=SHA256.Create())return "Local\\RedBaron_Launcher_"+BitConverter.ToString(sha.ComputeHash(System.Text.Encoding.UTF8.GetBytes(Full(root).ToLowerInvariant()))).Replace("-","");
        }
        public static void ValidateInstallation(string root) {
            root=Full(root);NoLinks(root);
            if(Directory.Exists(Path.Combine(root,".git")) || File.Exists(Path.Combine(root,".git")))
                throw new InvalidDataException("Automatic updates are for installed games. Update the development checkout from source.");
            string marker=Path.Combine(root,".redbaron-install.json");NoLinks(marker);
            if(!Services.HasInstallation(root)||!File.Exists(marker))throw new InvalidDataException("Select an installed game before updating.");
            var data=Json.Read<Dictionary<string,object>>(marker);object owner,title,status;
            if(!data.TryGetValue("owner",out owner)||!Object.Equals(owner,"RedBaronInstaller/v1") ||
                !data.TryGetValue("titleId",out title)||!Object.Equals(title,"584109B5") ||
                !data.TryGetValue("status",out status)||!Object.Equals(status,"ready"))
                throw new InvalidDataException("The installation is incomplete. Complete installation before updating.");
            if(Services.IsGameRunning(Settings.Load(root)))throw new InvalidDataException("Close the game before updating.");
            NoLinks(Path.Combine(root,"game"));NoLinks(Path.Combine(root,"game","default.xex"));
        }
        internal static void ValidateJob(string root,string job) {
            NoLinks(job);
            if(!String.Equals(Full(Path.GetDirectoryName(job)),Full(Path.Combine(root,"launcher-data")),StringComparison.OrdinalIgnoreCase) ||
                !Regex.IsMatch(Path.GetFileName(job),@"\Aupdate-[0-9a-f]{32}\z") ||
                !File.Exists(Path.Combine(job,".installer-job")) || File.ReadAllText(Path.Combine(job,".installer-job"))!="RedBaronInstaller/v1")
                throw new InvalidDataException("Invalid update working folder.");
        }
        // Extract only the standard installer package, never arbitrary archive paths.
        public static string ExtractPackage(string archive,ReleaseAsset asset,string job) {
            NoLinks(archive);NoLinks(job);
            if(new FileInfo(archive).Length!=asset.size || !String.Equals("sha256:"+Hash(archive),asset.digest,StringComparison.OrdinalIgnoreCase))
                throw new InvalidDataException("The verified update package changed. Download it again.");
            string folder=asset.name.Substring(0,asset.name.Length-"-windows-x64.zip".Length);
            var allowed=new HashSet<string>(StringComparer.OrdinalIgnoreCase){"RedBaron_Launcher.exe","RedBaron_Launcher.exe.config","INSTALLATION.md","CREDITS.md"};
            string destination=Path.Combine(job,"package");NoLinks(destination);
            if(Directory.Exists(destination))throw new InvalidDataException("The update extraction folder already exists.");
            using(var zip=ZipFile.OpenRead(archive)) {
                long size=0;var found=new HashSet<string>(StringComparer.OrdinalIgnoreCase);
                foreach(var entry in zip.Entries) {
                    string name=entry.FullName.Replace('\\','/');
                    if(name==folder+"/" && entry.Length==0)continue;
                    string prefix=folder+"/";
                    if(!name.StartsWith(prefix,StringComparison.Ordinal) || !allowed.Contains(name.Substring(prefix.Length)) ||
                        !found.Add(name.Substring(prefix.Length)) || ((entry.ExternalAttributes>>16)&0xF000)==0xA000)
                        throw new InvalidDataException("Unexpected file in update package.");
                    size+=entry.Length;
                    if(entry.Length<0 || size>3L*1024*1024*1024)throw new InvalidDataException("The extracted update is too large.");
                }
                if(!allowed.SetEquals(found))throw new InvalidDataException("The update installer is incomplete.");
                var drive=new DriveInfo(Path.GetPathRoot(job));
                if(drive.AvailableFreeSpace<size+256L*1024*1024)throw new IOException("Not enough free space to unpack the update.");
                Directory.CreateDirectory(destination);
                foreach(var entry in zip.Entries) {
                    string name=entry.FullName.Replace('\\','/').Substring(folder.Length+1);
                    if(name.Length>0)entry.ExtractToFile(Path.Combine(destination,name));
                }
            }
            return Path.Combine(destination,"RedBaron_Launcher.exe");
        }
        public static Task<string> Prepare(ReleaseAsset asset,string archive,string root) {
            return Task.Run(()=>{
                ValidateInstallation(root);
                string job=InstallationCleanup.CreateJob(root,"update");
                ExtractPackage(archive,asset,job);return job;
            });
        }
        public static void Start(string root,string job) {
            ValidateInstallation(root);ValidateJob(root,job);
            StartHost(root,job,"--apply-update");
        }
        public static string PrepareLocal(string root) {
            ValidateInstallation(root);
            if(!Services.HasInstallerPayload()||!Services.HasBundledSdk())throw new InvalidDataException("Use the full installer to update an existing installation.");
            string job=InstallationCleanup.CreateJob(root,"update"),package=Path.Combine(job,"package");Directory.CreateDirectory(package);
            File.Copy(Services.LauncherExe,Path.Combine(package,"RedBaron_Launcher.exe"));
            using(var resource=System.Reflection.Assembly.GetExecutingAssembly().GetManifestResourceStream("App.config"))
            using(var output=File.Create(Path.Combine(package,"RedBaron_Launcher.exe.config")))resource.CopyTo(output);
            if(Services.HasOfflineTools())File.WriteAllText(Path.Combine(job,"offline-tools-root.txt"),Path.Combine(Path.GetDirectoryName(Services.LauncherExe),"Tools"));
            return job;
        }
        public static bool StartRecovery(string root) {
            string pointer=Path.Combine(root,Pending);NoLinks(pointer);if(!File.Exists(pointer))return false;
            var data=Json.Read<Dictionary<string,string>>(pointer);string job;
            if(!data.TryGetValue("job",out job))throw new InvalidDataException("Missing update recovery folder.");
            ValidateJob(root,job);StartHost(root,job,"--recover-update");return true;
        }
        static void StartHost(string root,string job,string mode) {
            using(var parent=Process.GetCurrentProcess()) {
                string exe=Path.Combine(job,"package","RedBaron_Launcher.exe");
                var info=new ProcessStartInfo(exe,String.Join(" ",new[]{mode,root,job,parent.Id.ToString(),parent.StartTime.ToUniversalTime().Ticks.ToString()}.Select(Services.QuoteArgument))) {
                    UseShellExecute=false,WorkingDirectory=job
                };
                using(var child=Process.Start(info))if(child==null)throw new IOException("Could not start the updater. Try again.");
            }
        }
        static List<string> Tree(string directory) {
            NoLinks(directory);var result=new List<string>();var queue=new Stack<string>();queue.Push(directory);
            while(queue.Count>0)foreach(var path in Directory.GetFileSystemEntries(queue.Pop())) {
                NoLinks(path);if(Directory.Exists(path))queue.Push(path);else result.Add(path);
            }
            return result;
        }
        static bool Managed(string relative) {
            string path=relative.Replace('\\','/');
            if(path.Contains(":")||path.Split('/').Any(p=>p=="."||p==".."||p.Length==0))return false;
            return path=="RedBaron_Launcher.exe"||path=="RedBaron_Launcher.exe.config"||path==".redbaron-install.json" ||
                path.StartsWith("licenses/",StringComparison.Ordinal) ||
                path.StartsWith("build_cpp_release/",StringComparison.Ordinal);
        }
        static bool PersonalSettings(string path) {
            return path.EndsWith(".toml",StringComparison.OrdinalIgnoreCase)||path.EndsWith(".ini",StringComparison.OrdinalIgnoreCase);
        }
        static void CopyAtomic(string source,string target) {
            NoLinks(source);NoLinks(target);Directory.CreateDirectory(Path.GetDirectoryName(target));
            string temp=Path.Combine(Path.GetDirectoryName(target),".rb-"+Guid.NewGuid().ToString("N"));
            try {
                using(var input=File.OpenRead(source))using(var output=new FileStream(temp,FileMode.CreateNew,FileAccess.Write,FileShare.None)) {input.CopyTo(output);output.Flush(true);}
                if(File.Exists(target))File.Replace(temp,target,null);else File.Move(temp,target);
            } finally {if(File.Exists(temp))File.Delete(temp);}
        }
        static string JournalPath(string job) {return Path.Combine(job,"transaction.json");}
        static void WriteDurable(string path,object value) {
            NoLinks(path);Directory.CreateDirectory(Path.GetDirectoryName(path));
            string temp=Path.Combine(Path.GetDirectoryName(path),".rb-"+Guid.NewGuid().ToString("N"));
            try {
                byte[] bytes=System.Text.Encoding.UTF8.GetBytes(Json.Serializer.Serialize(value));
                using(var output=new FileStream(temp,FileMode.CreateNew,FileAccess.Write,FileShare.None)){output.Write(bytes,0,bytes.Length);output.Flush(true);}
                if(File.Exists(path))File.Replace(temp,path,null);else File.Move(temp,path);
            }finally {if(File.Exists(temp))File.Delete(temp);}
        }
        static void SaveJournal(string job,UpdateJournal journal) {WriteDurable(JournalPath(job),journal);}
        internal static void Deploy(string root,string job,string stage,Action<string> log,Action<int> afterReplace=null) {
            ValidateInstallation(root);ValidateJob(root,job);NoLinks(stage);
            if(Full(stage)!=Full(Path.Combine(job,"stage")))throw new InvalidDataException("Invalid update staging folder.");
            foreach(string name in new[]{"RedBaron_Launcher.exe","RedBaron_Launcher.exe.config","build_cpp_release/snoopyflyingace.exe","build_cpp_release/rexruntime.dll","build_cpp_release/rexgpu-xenos.dll","build_cpp_release/redbaron_probe.exe"})
                if(!File.Exists(Path.Combine(stage,name)))throw new InvalidDataException("Incomplete update build: "+name);
            string toml=Path.Combine(root,"build_cpp_release","snoopyflyingace.toml");
            string user=File.Exists(toml)?new Toml(File.ReadAllText(toml)).String("user_data_root",""):"";
            if(!String.IsNullOrWhiteSpace(user))user=Path.IsPathRooted(user)?Full(user):Full(Path.Combine(root,"build_cpp_release",user));
            var files=Tree(Path.Combine(stage,"build_cpp_release")).Concat(Tree(Path.Combine(stage,"licenses")))
                .Concat(new[]{Path.Combine(stage,"RedBaron_Launcher.exe"),Path.Combine(stage,"RedBaron_Launcher.exe.config"),Path.Combine(stage,".redbaron-install.json")})
                .Select(file=>file.Substring(Full(stage).Length+1)).Where(p=>Managed(p) && !(PersonalSettings(p)&&File.Exists(Path.Combine(root,p))))
                .OrderBy(p=>p=="RedBaron_Launcher.exe"?0:p=="RedBaron_Launcher.exe.config"?1:p==".redbaron-install.json"?3:2).ThenBy(p=>p,StringComparer.Ordinal).ToList();
            foreach(string relative in files) {
                string target=Path.Combine(root,relative);NoLinks(target);
                if(!String.IsNullOrWhiteSpace(user) && (Full(target)==Full(user)||Within(target,user)))
                    throw new InvalidDataException("The configured save folder overlaps update files. Move it before updating.");
            }
            long backupBytes=files.Select(p=>Path.Combine(root,p)).Where(File.Exists).Sum(p=>new FileInfo(p).Length);
            long largest=files.Select(p=>new FileInfo(Path.Combine(stage,p)).Length).DefaultIfEmpty(0).Max();
            if(new DriveInfo(Path.GetPathRoot(root)).AvailableFreeSpace<backupBytes+largest+128L*1024*1024)
                throw new IOException("Not enough free space to keep the previous version and safely apply this update.");
            string pointer=Path.Combine(root,Pending);NoLinks(pointer);
            if(File.Exists(pointer))throw new InvalidDataException("An interrupted update must be recovered before starting another one.");
            var journal=new UpdateJournal{owner=Owner,root=Full(root),version=Updates.CurrentVersion,status="applying",entries=new List<UpdateEntry>()};
            SaveJournal(job,journal);WriteDurable(pointer,new {job=Full(job)});
            try {
                int count=0;
                foreach(string relative in files) {
                    string source=Path.Combine(stage,relative),target=Path.Combine(root,relative),backup=Path.Combine(job,"backup",relative);
                    bool existed=File.Exists(target);
                    if(existed) {NoLinks(backup);Directory.CreateDirectory(Path.GetDirectoryName(backup));CopyAtomic(target,backup);}
                    journal.entries.Add(new UpdateEntry{path=relative,existed=existed,hash=existed?Hash(backup):""});
                    SaveJournal(job,journal); // Record undo before touching a live file.
                    CopyAtomic(source,target);
                    if(afterReplace!=null)afterReplace(++count);else count++;
                    log("@@RBPROGRESS "+Json.Serializer.Serialize(new InstallProgress{stage="Applying update",completed=count,total=files.Count}));
                }
                journal.status="complete";SaveJournal(job,journal);File.Delete(pointer);
            } catch {
                Rollback(root,job,journal,log);throw;
            }
        }
        static void Rollback(string root,string job,UpdateJournal journal,Action<string> log) {
            ValidateJob(root,job);
            if(journal==null||journal.owner!=Owner||Full(journal.root)!=Full(root)||journal.entries==null)
                throw new InvalidDataException("Update recovery metadata is invalid; the backup was preserved.");
            // Validate the entire undo set before changing any file.
            foreach(var entry in journal.entries) {
                if(!Managed(entry.path)||(PersonalSettings(entry.path)&&entry.existed))throw new InvalidDataException("Invalid recovery path.");
                string target=Path.Combine(root,entry.path),backup=Path.Combine(job,"backup",entry.path);NoLinks(target);NoLinks(backup);
                if(entry.existed && (!File.Exists(backup)||Hash(backup)!=entry.hash))throw new InvalidDataException("Update backup verification failed. Keep the backup folder for recovery.");
            }
            log("Restoring the previous version…");
            foreach(var entry in Enumerable.Reverse(journal.entries)) {
                string target=Path.Combine(root,entry.path);
                if(entry.existed) {
                    // A locked destination may have rejected replacement entirely.
                    // Leave its already-original bytes alone while undoing earlier files.
                    if(!File.Exists(target)||Hash(target)!=entry.hash)CopyAtomic(Path.Combine(job,"backup",entry.path),target);
                }
                else if(File.Exists(target))File.Delete(target);
            }
            journal.status="rolled-back";SaveJournal(job,journal);File.Delete(Path.Combine(root,Pending));
        }
        public static void Recover(string root,Action<string> log) {
            string pointer=Path.Combine(root,Pending);NoLinks(pointer);if(!File.Exists(pointer))return;
            var record=Json.Read<Dictionary<string,string>>(pointer);string job;
            if(!record.TryGetValue("job",out job))throw new InvalidDataException("Update recovery metadata is missing.");
            ValidateJob(root,job);NoLinks(JournalPath(job));var journal=Json.Read<UpdateJournal>(JournalPath(job));
            if(journal.owner!=Owner||Full(journal.root)!=Full(root))throw new InvalidDataException("Invalid update recovery metadata.");
            if(journal.status=="complete"||journal.status=="rolled-back") {File.Delete(pointer);return;}
            Rollback(root,job,journal,log);
        }
        public static async Task Apply(string root,string job,Action<string> log,CancellationToken token) {
            token.ThrowIfCancellationRequested();
            ValidateInstallation(root);ValidateJob(root,job);Recover(root,log);
            if(!Services.HasInstallerPayload()||!Services.HasBundledSdk())throw new InvalidDataException("The downloaded package does not contain a full update installer.");
            string stage=Path.Combine(job,"stage");
            var settings=new Settings{Root=job,Source=Path.Combine(root,"game"),Destination=stage,Sdk=Path.Combine(stage,"sdk_experimental"),DlcSource="",DesktopShortcut=false};
            log("Preparing the new version. Your current game remains unchanged until the build is ready.");
            string offlineFile=Path.Combine(job,"offline-tools-root.txt"),offline=null;NoLinks(offlineFile);
            if(File.Exists(offlineFile)){offline=File.ReadAllText(offlineFile);NoLinks(offline);}
            await Task.Run(()=>Services.InstallContent(settings,log,token,offline));
            token.ThrowIfCancellationRequested();
            log("Checking the updated runtime…");
            await Services.Probe(Settings.Load(stage));
            token.ThrowIfCancellationRequested();
            await Task.Run(()=>Deploy(root,job,stage,log));
            log("Update installed. Settings, saves and DLCs preserved. Reopening the launcher…");
            // Stage includes a redundant game copy; remove only this owned child after commit.
            try {RemoveStage(root,job);}catch(IOException error){log("Temporary update files kept: "+error.Message);}
        }
        internal static void RemoveStage(string root,string job) {
            ValidateJob(root,job);string stage=Path.Combine(job,"stage");
            if(Directory.Exists(stage)) {Tree(stage);Directory.Delete(stage,true);}
        }
        public static void Reopen(string root) {
            using(var process=Process.Start(new ProcessStartInfo(Path.Combine(root,"RedBaron_Launcher.exe"),"--settings --root "+Services.QuoteArgument(root)){UseShellExecute=false,WorkingDirectory=root})) {}
        }
    }
}
