using System;
using System.IO;
using System.Linq;
using System.Net;
using System.Security.Cryptography;
using System.Text.RegularExpressions;
using System.Threading.Tasks;
using System.Web.Script.Serialization;

namespace RedBaron {
    public sealed class ReleaseAsset {
        public string name {get;set;} public string browser_download_url {get;set;}
        public string digest {get;set;} public long size {get;set;}
    }
    public sealed class ReleaseInfo {
        public string tag_name {get;set;} public bool prerelease {get;set;} public bool draft {get;set;}
        public ReleaseAsset[] assets {get;set;}
    }
    public static class Updates {
        public const string CurrentVersion="0.10.0";
        const long MaxBytes=512L*1024*1024;
        const string Endpoint="https://api.github.com/repos/BTr-RJ/RedBaron_Recomp/releases/latest";
        static bool ValidAsset(ReleaseAsset asset) {
            if(asset==null || asset.size<=0 || asset.size>MaxBytes || !Regex.IsMatch(asset.digest??"",@"\Asha256:[0-9a-fA-F]{64}\z"))return false;
            var match=Regex.Match(asset.name??"",@"\ARedBaron_Recomp-(v\d+\.\d+(?:\.\d+)?)-windows-x64\.zip\z");
            Uri uri;
            return match.Success && Uri.TryCreate(asset.browser_download_url,UriKind.Absolute,out uri) && uri.Scheme=="https" &&
                String.Equals(uri.Host,"github.com",StringComparison.OrdinalIgnoreCase) && String.IsNullOrEmpty(uri.UserInfo) &&
                String.IsNullOrEmpty(uri.Query) && String.IsNullOrEmpty(uri.Fragment) &&
                uri.AbsolutePath=="/BTr-RJ/RedBaron_Recomp/releases/download/"+match.Groups[1].Value+"/"+asset.name;
        }
        static HttpWebRequest Request(string url) {
            ServicePointManager.SecurityProtocol=SecurityProtocolType.Tls12;
            var request=(HttpWebRequest)WebRequest.Create(url);
            request.UserAgent="RedBaron_Recomp/"+CurrentVersion;request.Timeout=15000;request.ReadWriteTimeout=30000;
            request.Accept="application/vnd.github+json";return request;
        }
        public static ReleaseAsset Parse(string json) {
            var release=new JavaScriptSerializer().Deserialize<ReleaseInfo>(json);
            Version version;
            if(release==null || release.draft || release.prerelease ||
               String.IsNullOrEmpty(release.tag_name) || !Regex.IsMatch(release.tag_name,@"\Av\d+\.\d+(\.\d+)?\z") ||
               !Version.TryParse(release.tag_name.Substring(1),out version)) throw new InvalidDataException("Invalid release metadata.");
            if(version<=new Version(CurrentVersion))return null;
            string expected="RedBaron_Recomp-"+release.tag_name+"-windows-x64.zip";
            var candidates=(release.assets??new ReleaseAsset[0]).Where(a=>a!=null && a.name==expected).ToArray();
            if(candidates.Length!=1)throw new InvalidDataException("The release does not contain the expected Windows package.");
            var asset=candidates[0];
            if(!ValidAsset(asset))
                throw new InvalidDataException("The release package address, size or SHA-256 is invalid.");
            return asset;
        }
        public static Task<ReleaseAsset> Check() {
            return Task.Run(()=>{
                try {
                    using(var response=(HttpWebResponse)Request(Endpoint).GetResponse()) {
                        if(response.ContentLength>1024*1024)throw new InvalidDataException("Release metadata is too large.");
                        using(var input=response.GetResponseStream()) using(var memory=new MemoryStream()) {
                            byte[] buffer=new byte[8192];int count;
                            while((count=input.Read(buffer,0,buffer.Length))>0) {
                                if(memory.Length+count>1024*1024)throw new InvalidDataException("Release metadata is too large.");
                                memory.Write(buffer,0,count);
                            }
                            return Parse(System.Text.Encoding.UTF8.GetString(memory.ToArray()));
                        }
                    }
                } catch(WebException error) {
                    var response=error.Response as HttpWebResponse;
                    if(response!=null && response.StatusCode==HttpStatusCode.NotFound){response.Dispose();return null;}
                    throw new IOException("Could not reach GitHub. Check your internet connection and try again.",error);
                }
            });
        }
        public static Task<string> Download(ReleaseAsset asset,string root) {
            return DownloadCore(asset,root,Request);
        }
        internal static Task<string> DownloadFromLocalTest(ReleaseAsset asset,string root,string localUrl) {
            Uri local;
            if(!Uri.TryCreate(localUrl,UriKind.Absolute,out local) || local.Scheme!="http" || local.Host!="127.0.0.1")
                throw new InvalidDataException("Invalid local test server.");
            return DownloadCore(asset,root,ignored=>{var request=Request(localUrl);request.Proxy=null;return request;});
        }
        static Task<string> DownloadCore(ReleaseAsset asset,string root,Func<string,HttpWebRequest> requestFactory) {
            return Task.Run(()=>{
                // Revalidate the URL and metadata immediately before starting the transfer.
                if(!ValidAsset(asset))
                    throw new InvalidDataException("Invalid update package metadata.");
                var url=new Uri(asset.browser_download_url);
                string directory=Path.Combine(root,"launcher-data","updates");Directory.CreateDirectory(directory);
                string partial=Path.Combine(directory,Guid.NewGuid().ToString("N")+".partial");
                try {
                    using(var response=(HttpWebResponse)requestFactory(url.AbsoluteUri).GetResponse())
                    using(var input=response.GetResponseStream()) using(var file=new FileStream(partial,FileMode.CreateNew,FileAccess.Write,FileShare.None)) {
                        if(response.ContentLength>asset.size)throw new InvalidDataException("Update package is larger than expected.");
                        byte[] buffer=new byte[65536];int count;long total=0;
                        while((count=input.Read(buffer,0,buffer.Length))>0) {
                            total+=count;if(total>asset.size)throw new InvalidDataException("Update package is larger than expected.");
                            file.Write(buffer,0,count);
                        }
                        if(total!=asset.size)throw new InvalidDataException("Update package download is incomplete.");
                    }
                    string actual;
                    using(var sha=SHA256.Create())using(var file=File.OpenRead(partial))actual=BitConverter.ToString(sha.ComputeHash(file)).Replace("-","");
                    if(!String.Equals(actual,asset.digest.Substring(7),StringComparison.OrdinalIgnoreCase))throw new InvalidDataException("Update package SHA-256 does not match GitHub.");
                    string destination=Path.Combine(directory,Guid.NewGuid().ToString("N")+"-"+asset.name);
                    File.Move(partial,destination);return destination;
                } finally {if(File.Exists(partial))File.Delete(partial);}
            });
        }
    }
}
