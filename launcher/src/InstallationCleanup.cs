using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading;

namespace RedBaron {
    // Only installer-owned trees are eligible. Runtime/data directories are never pruned.
    public static class InstallationCleanup {
        const string Owner="RedBaronInstaller/v1";
        static string Full(string path){return Path.GetFullPath(path).TrimEnd(Path.DirectorySeparatorChar);}
        static bool Within(string path,string parent){return Full(path).StartsWith(Full(parent)+Path.DirectorySeparatorChar,StringComparison.OrdinalIgnoreCase);}
        static void NoLinks(string path) {
            for(string current=Full(path);!String.IsNullOrEmpty(current);) {
                if((Directory.Exists(current)||File.Exists(current)) && (File.GetAttributes(current)&FileAttributes.ReparsePoint)!=0)throw new InvalidDataException("Cleanup does not follow links.");
                string next=Path.GetDirectoryName(current);if(next==current)break;current=next;
            }
        }
        static List<string> Files(string path) {
            NoLinks(path);var files=new List<string>();if(!Directory.Exists(path))return files;
            var pending=new Stack<string>();pending.Push(path);
            while(pending.Count>0)foreach(string item in Directory.GetFileSystemEntries(pending.Pop())) {
                if((File.GetAttributes(item)&FileAttributes.ReparsePoint)!=0)throw new InvalidDataException("Cleanup does not follow links.");
                if(Directory.Exists(item))pending.Push(item);else files.Add(item);
            }
            return files;
        }
        static long Remove(string path,string ownerRoot) {
            if(!Within(path,ownerRoot))throw new InvalidDataException("Cleanup target is outside its installation directory.");
            NoLinks(path);long bytes=0;
            if(Directory.Exists(path)) {
                var files=Files(path);foreach(string file in files){bytes+=new FileInfo(file).Length;File.SetAttributes(file,File.GetAttributes(file)&~FileAttributes.ReadOnly);}
                Directory.Delete(path,true);
            }else if(File.Exists(path)){bytes=new FileInfo(path).Length;File.SetAttributes(path,File.GetAttributes(path)&~FileAttributes.ReadOnly);File.Delete(path);}
            return bytes;
        }
        public static string CreateJob(string root,string kind) {
            string parent=Path.Combine(Full(root),"launcher-data");NoLinks(parent);
            string job=Path.Combine(parent,kind+"-"+Guid.NewGuid().ToString("N"));Directory.CreateDirectory(job);
            File.WriteAllText(Path.Combine(job,".installer-job"),Owner);return job;
        }
        public static void CleanJob(string job) {
            if(String.IsNullOrEmpty(job)||!Directory.Exists(job))return;
            string parent=Path.GetDirectoryName(Full(job));
            if(Path.GetFileName(parent)!="launcher-data")throw new InvalidDataException("Invalid installer job directory.");
            NoLinks(job);
            if(!File.Exists(Path.Combine(job,".installer-job"))||File.ReadAllText(Path.Combine(job,".installer-job"))!=Owner)throw new InvalidDataException("Installer job ownership is missing.");
            Remove(job,parent);
        }
        static void PreserveTools(string root,string cacheRoot,string catalogFile,Action<string> log) {
            string source=Path.Combine(root,".redbaron-tools");if(!Directory.Exists(source))return;
            var files=Files(source);if(files.Count==0){Remove(source,root);return;}
            foreach(string name in new[]{"cmake.zip","ninja.zip","llvm.tar.xz","python.zip","vs_BuildTools.exe"}) {
                Remove(Path.Combine(source,name),root);Remove(Path.Combine(source,name+".partial"),root);
            }
            files=Files(source);if(files.Count==0){Remove(source,root);return;}
            cacheRoot=Full(cacheRoot);NoLinks(cacheRoot);
            if(Within(cacheRoot,root)||Within(root,cacheRoot)||String.Equals(cacheRoot,root,StringComparison.OrdinalIgnoreCase))throw new InvalidDataException("Shared tools must be separate from the game installation.");
            Directory.CreateDirectory(cacheRoot);string target=Path.Combine(cacheRoot,"installed-"+Guid.NewGuid().ToString("N"));
            NoLinks(catalogFile);
            var catalog=File.Exists(catalogFile)?Json.Read<List<BuildTool>>(catalogFile):new List<BuildTool>();
            foreach(var entry in catalog)if(!String.IsNullOrWhiteSpace(entry.path)&&Within(entry.path,source))entry.path=Path.Combine(target,Full(entry.path).Substring(Full(source).Length+1));
            // Copy across volumes too; the old cache remains usable until the catalog is committed.
            Directory.CreateDirectory(target);
            try {
                foreach(string file in files){string destination=Path.Combine(target,Full(file).Substring(Full(source).Length+1));Directory.CreateDirectory(Path.GetDirectoryName(destination));File.Copy(file,destination,false);}
                NoLinks(catalogFile);Directory.CreateDirectory(Path.GetDirectoryName(catalogFile));AtomicFile.Write(catalogFile,Json.Serializer.Serialize(catalog));
            }catch {Remove(target,cacheRoot);throw;}
            Remove(source,root);log("Reusable build tools kept in the shared user cache.");
        }
        public static void Run(Settings s,Action<string> log,CancellationToken token,string sharedTools=null,string catalogFile=null) {
            string root=Full(s.Root),marker=Path.Combine(root,".redbaron-install.json");
            if(root.Length<=Path.GetPathRoot(root).Length || Directory.Exists(Path.Combine(root,".git")) || File.Exists(Path.Combine(root,".git")) || !File.Exists(marker))return;
            NoLinks(root);NoLinks(marker);
            var state=Json.Read<Dictionary<string,object>>(marker);
            object owner,pending,title,status;
            if(!state.TryGetValue("owner",out owner)||!Object.Equals(owner,Owner)||!state.TryGetValue("titleId",out title)||!Object.Equals(title,"584109B5")||!state.TryGetValue("status",out status)||!Object.Equals(status,"ready")||!state.TryGetValue("cleanupPending",out pending)||!Object.Equals(pending,true))return;
            token.ThrowIfCancellationRequested();
            string[] dirs={"port","scripts","sdk_experimental",".redbaron-tools","build_cpp_release/CMakeFiles","build_cpp_release/.cmake"};
            foreach(string dir in dirs) {
                string target=Path.Combine(root,dir);Files(target);
                foreach(string original in new[]{s.Source,s.DlcSource})if(!String.IsNullOrWhiteSpace(original)) {
                    string input=Path.IsPathRooted(original)?original:Path.Combine(root,original);
                    if(Full(input)==Full(target)||Within(input,target)||Within(target,input))throw new InvalidDataException("Cleanup would overlap original input files; working files were preserved.");
                }
                string user=File.Exists(s.Toml)?new Toml(File.ReadAllText(s.Toml)).String("user_data_root",""):"";
                if(!String.IsNullOrWhiteSpace(user)) {
                    if(!Path.IsPathRooted(user))user=Path.Combine(s.Build,user);
                    if(Full(user)==Full(target)||Within(user,target)||Within(target,user))throw new InvalidDataException("Cleanup would overlap user data; working files were preserved.");
                }
            }
            log("@@RBPROGRESS {\"stage\":\"Cleaning installation files\",\"completed\":0,\"total\":0}");
            string local=Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),"RedBaron_Recomp");
            PreserveTools(root,sharedTools??Path.Combine(local,"Tools"),catalogFile??Path.Combine(local,"build-tools.json"),log);
            string licenses=Path.Combine(root,"sdk_experimental","licenses");
            if(Directory.Exists(licenses)) {
                string target=Path.Combine(root,"licenses","sdk");NoLinks(target);
                if(Directory.Exists(target))throw new IOException("License destination exists; SDK files were preserved.");
                Directory.CreateDirectory(Path.GetDirectoryName(target));Directory.Move(licenses,target);
            }
            long removed=0;
            foreach(string dir in dirs){token.ThrowIfCancellationRequested();removed+=Remove(Path.Combine(root,dir),root);}
            foreach(string name in new[]{".ninja_deps",".ninja_log","build.ninja","CMakeCache.txt","cmake_install.cmake","CTestTestfile.cmake","DartConfiguration.tcl","redbaron_brand.rc","RedBaron.ico"})removed+=Remove(Path.Combine(s.Build,name),root);
            foreach(string file in Directory.GetFiles(s.Build).Where(f=>new[]{".obj",".lib",".exp",".pdb"}.Contains(Path.GetExtension(f).ToLowerInvariant())))removed+=Remove(file,root);
            state["cleanupPending"]=false;state["cleanedUtc"]=DateTime.UtcNow.ToString("o");AtomicFile.Write(marker,Json.Serializer.Serialize(state));
            log("Installation cleanup complete: removed "+Math.Round(removed/1048576.0,1)+" MB of build files. Runtime, licenses, settings and saves preserved.");
        }
    }
}
