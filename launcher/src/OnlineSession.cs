using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Threading.Tasks;

namespace RedBaron {
    public sealed class OnlineMember {
        public int id {get;set;} public string name {get;set;} public int players {get;set;}
    }
    public sealed class OnlineEvent {
        public string op {get;set;} public string message {get;set;} public string reason {get;set;}
        public string room {get;set;} public string bind {get;set;} public string host_address {get;set;}
        public int id {get;set;} public int @namespace {get;set;} public int capacity {get;set;}
        public OnlineMember member {get;set;} public OnlineMember[] members {get;set;}
    }
    public sealed class OnlineSession : IDisposable {
        readonly Process process=new Process();
        readonly TaskCompletionSource<bool> admitted=new TaskCompletionSource<bool>();
        readonly object gate=new object();
        readonly Dictionary<int,OnlineMember> members=new Dictionary<int,OnlineMember>();
        readonly Action<string> changed;
        string config;
        OnlineEvent welcome;
        int localPlayers=1;
        bool closed, ready, disposed;
        public string Room {get {lock(gate)return welcome==null?"":welcome.room;}}
        public bool Ready {get {lock(gate)return !closed && welcome!=null && (welcome.id==0 || ready);}}
        public OnlineSession(Action<string> onChanged) {changed=onChanged;}
        public async Task Start(Settings settings,string room) {
            Settings.ValidateOnlineServer(settings.OnlineServer,true);
            localPlayers=settings.OnlineLocalPlayers;
            string exe=Path.Combine(settings.Build,"redbaron_net.exe");
            if(!File.Exists(exe) || !File.Exists(Path.Combine(settings.Build,"datachannel.dll")))
                throw new FileNotFoundException("The online companion is missing. Update or repair this installation.");
            if(room.Length!=0 && !System.Text.RegularExpressions.Regex.IsMatch(room,@"\A[0-9A-F]{16}\z"))
                throw new InvalidDataException("Enter the 16-character room code supplied by the host.");
            string directory=Path.Combine(settings.Root,"launcher-data");Directory.CreateDirectory(directory);
            config=Path.Combine(directory,"online-"+Guid.NewGuid().ToString("N")+".json");
            Json.Write(config,new {server=settings.OnlineServer,name=settings.PlayerName,mode=settings.OnlineMode,
                players=settings.OnlineLocalPlayers,room=room,@namespace=new Random().Next(100,240),parent_pid=Process.GetCurrentProcess().Id});
            process.StartInfo=Services.StartInfo(exe,new[]{"--config",config},settings.Build);
            process.StartInfo.RedirectStandardInput=true;
            process.EnableRaisingEvents=true;
            process.OutputDataReceived+=(sender,e)=> {
                if(e.Data==null)return;
                try {Receive(Json.Serializer.Deserialize<OnlineEvent>(e.Data));}
                catch(Exception error){Fail("Invalid online response: "+error.Message);}
            };
            // Diagnostics stay out of the UI; structured errors arrive on stdout.
            process.ErrorDataReceived+=(sender,e)=>{};
            process.Exited+=(sender,e)=>Fail("Online connection ended. Create or join a room again.");
            process.Start();process.BeginOutputReadLine();process.BeginErrorReadLine();
            var timeout=Task.Delay(TimeSpan.FromSeconds(20));
            if(await Task.WhenAny(admitted.Task,timeout)!=admitted.Task)
                throw new IOException("The room server did not respond. Check its address and try again.");
            await admitted.Task;
        }
        void Fail(string reason) {
            lock(gate){if(disposed || closed)return;closed=true;}
            admitted.TrySetException(new IOException(reason));changed(reason);
        }
        void Receive(OnlineEvent e) {
            string state;
            lock(gate) {
                if(disposed)return;
                if(e.op=="error" || e.op=="closed") {Fail(e.message??e.reason??"Online connection closed.");return;}
                if(e.op=="welcome") {
                    welcome=e;foreach(var member in e.members)members[member.id]=member;
                    admitted.TrySetResult(true);
                } else if(e.op=="joined") members[e.member.id]=e.member;
                else if(e.op=="left") members.Remove(e.id);
                else if(e.op=="peer_ready") ready=true;
                if(welcome==null)return;
                state="Room "+welcome.room+" · "+members.Values.Sum(m=>m.players)+"/"+welcome.capacity+" players\n"+
                    String.Join(", ",members.Values.OrderBy(m=>m.id).Select(m=>m.name+(m.id==0?" (host)":"")))+"\n"+
                    (Ready?"Connected. Click Play to open the game.":"Connecting to the host…");
            }
            changed(state);
        }
        public string[] GameArguments() {
            lock(gate) {
                if(!Ready)throw new InvalidOperationException("Wait for the online connection before clicking Play.");
                return new[]{"--redbaron_lan=true","--redbaron_lan_bind="+welcome.bind,
                    "--redbaron_lan_peer="+welcome.host_address,"--redbaron_p2p_namespace="+welcome.@namespace,
                    "--redbaron_p2p_id="+welcome.id,"--redbaron_online_players="+localPlayers};
            }
        }
        public void Dispose() {
            lock(gate){if(disposed)return;disposed=true;closed=true;}
            try {
                if(!process.HasExited) {
                    process.StandardInput.WriteLine("{\"op\":\"stop\"}");process.StandardInput.Flush();
                    if(!process.WaitForExit(3000))process.Kill();
                }
            } catch(InvalidOperationException){} catch(IOException){} catch(System.ComponentModel.Win32Exception){}
            process.Dispose();
            if(config!=null && File.Exists(config))File.Delete(config);
        }
    }
}
