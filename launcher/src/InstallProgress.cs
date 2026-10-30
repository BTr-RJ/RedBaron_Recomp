using System;
using System.Globalization;
using System.Text.RegularExpressions;

namespace RedBaron {
    // Stage percentages describe measured work, not an estimated installation time.
    public sealed class InstallProgress {
        public string stage {get;set;}
        public long completed {get;set;}
        public long total {get;set;}
        public bool Known {get {return total>0;}}
        public double Percent {get {return Known?100.0*completed/total:0;}}
        public static bool TryParse(string line,out InstallProgress value) {
            value=null;if(String.IsNullOrEmpty(line) || line.Length>4096)return false;
            if(line.StartsWith("@@RBPROGRESS ",StringComparison.Ordinal)) {
                try {value=Json.Serializer.Deserialize<InstallProgress>(line.Substring(13));}
                catch {return false;}
                if(value==null || String.IsNullOrWhiteSpace(value.stage) || value.stage.Length>120 || value.completed<0 || value.total<0 || (value.total>0 && value.completed>value.total)){value=null;return false;}
                return true;
            }
            var step=Regex.Match(line,@"^\[(\d+)/(\d+)\]\s+.+$");
            long done,total;
            if(!step.Success || !Int64.TryParse(step.Groups[1].Value,out done) || !Int64.TryParse(step.Groups[2].Value,out total) || total<=0 || done>total)return false;
            value=new InstallProgress{stage="Compiling",completed=done,total=total};return true;
        }
        public override string ToString(){return stage+(Known?" — "+Math.Floor(Percent).ToString("0",CultureInfo.InvariantCulture)+"%":"…");}
    }
}
