using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Reflection;
using System.Web.Script.Serialization;
using System.Windows;
using System.Runtime.Versioning;
[assembly:AssemblyTitle("D18Setup")]
[assembly:AssemblyProduct("DLSSNR D18")]
[assembly:AssemblyVersion("0.3.0.0")]
[assembly:AssemblyFileVersion("0.3.0.0")]
[assembly:AssemblyInformationalVersion("0.3.0")]
[assembly:TargetFramework(".NETFramework,Version=v4.8")]
namespace D18 {
 public static class Json {
  public static readonly JavaScriptSerializer Serializer=new JavaScriptSerializer{MaxJsonLength=int.MaxValue,RecursionLimit=100};
  public static Dictionary<string,object> Read(string p){return Serializer.Deserialize<Dictionary<string,object>>(File.ReadAllText(p));}
  public static void Write(string p,object v){File.WriteAllText(p,Serializer.Serialize(v),new System.Text.UTF8Encoding(false));}
  public static string S(IDictionary<string,object> d,string key){object v;return d.TryGetValue(key,out v)&&v!=null?Convert.ToString(v):"";}
  public static bool B(IDictionary<string,object> d,string key){object v;return d.TryGetValue(key,out v)&&v!=null&&Convert.ToBoolean(v);}
  public static Dictionary<string,object> D(IDictionary<string,object> d,string key){object v;return d.TryGetValue(key,out v)&&v is Dictionary<string,object>?(Dictionary<string,object>)v:new Dictionary<string,object>();}
  public static object[] A(object value){var e=value as System.Collections.IEnumerable;return e==null||value is string?new object[0]:e.Cast<object>().ToArray();}
 }
 public sealed class Context {
  public string Root,Bundle,Session; public bool Offline;public Dictionary<string,object> Settings;
  public Context(string root,bool offline) {
   Root=Path.GetFullPath(root);Offline=offline;Directory.CreateDirectory(Root);
   Settings=File.Exists(Path.Combine(Root,"settings.json"))?Json.Read(Path.Combine(Root,"settings.json")):new Dictionary<string,object>();
   Session=Path.Combine(Root,"sessions",Guid.NewGuid().ToString("N"));Bundle=Path.Combine(Session,"backend");Directory.CreateDirectory(Bundle);
   using(var stream=Assembly.GetExecutingAssembly().GetManifestResourceStream("D18.bundle"))
   using(var zip=new ZipArchive(stream,ZipArchiveMode.Read)) {
    foreach(var e in zip.Entries){var p=Path.GetFullPath(Path.Combine(Bundle,e.FullName));if(!p.StartsWith(Bundle+Path.DirectorySeparatorChar,StringComparison.OrdinalIgnoreCase))throw new InvalidDataException("Unsafe embedded bundle");Directory.CreateDirectory(Path.GetDirectoryName(p));using(var src=e.Open())using(var dst=File.Create(p))src.CopyTo(dst);}
   }
  }
  public void Save(){Json.Write(Path.Combine(Root,"settings.json"),Settings);}
  public Dictionary<string,object> Invoke(Dictionary<string,object> req) {
   var job=Path.Combine(Session,"jobs",Guid.NewGuid().ToString("N"));Directory.CreateDirectory(job);
   var input=Path.Combine(job,"request.json");var output=Path.Combine(job,"result.json");
   req["cache"]=Path.Combine(Root,"DownloadCache");
   Json.Write(input,req);
   var r=SetupHost.Run(Path.Combine(Bundle,"D18-InstallerBridge.ps1"),new Hashtable{{"RequestPath",input},{"ResultPath",output},{"Offline",Offline}});
   File.WriteAllText(Path.Combine(job,"host.log"),r.Log);
   if(!File.Exists(output))throw new InvalidOperationException(r.Log);
   var result=Json.Read(output);result["job"]=job;
   return result;
  }
 }
 public static class Program {
  [STAThread]public static int Main(string[] argv) {
   var args=new Dictionary<string,string>(StringComparer.OrdinalIgnoreCase);
   for(int i=0;i<argv.Length;i++){if(!argv[i].StartsWith("--"))return 2;var k=argv[i].Substring(2);args[k]=i+1<argv.Length&&!argv[i+1].StartsWith("--")?argv[++i]:"true";}
   string root=args.ContainsKey("data-root")?args["data-root"]:Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),"D18","Setup");
   try {
    var c=new Context(root,args.ContainsKey("offline"));
    if(args.ContainsKey("request")) {
     var result=c.Invoke(Json.Read(args["request"]));if(args.ContainsKey("result"))Json.Write(args["result"],result);
     return Json.B(result,"success")?0:1;
    }
    var app=new Application();app.Run(new SetupWindow(c,args.ContainsKey("lang")?args["lang"]:Json.S(c.Settings,"language"),""));return 0;
   }catch(Exception e){
    try{Directory.CreateDirectory(root);File.WriteAllText(Path.Combine(root,"fatal.log"),e.ToString());}catch{}
    if(!args.ContainsKey("request"))MessageBox.Show(e.Message,"D18Setup");return 1;
   }
  }
 }
}
