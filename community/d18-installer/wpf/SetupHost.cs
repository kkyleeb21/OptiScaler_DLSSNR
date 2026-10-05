using System;
using System.Collections;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Globalization;
using System.IO;
using System.Management.Automation;
using System.Management.Automation.Host;
using System.Management.Automation.Runspaces;
using System.Text;

namespace D18 {
 public sealed class ScriptResult {
  public int ExitCode; public string Log; public Collection<PSObject> Output;
 }
 public static class SetupHost {
  public static ScriptResult Run(string script, Hashtable args) {
   var host=new EmbeddedHost();
   using(var rs=RunspaceFactory.CreateRunspace(host)) {
    rs.ApartmentState=System.Threading.ApartmentState.STA; rs.ThreadOptions=PSThreadOptions.ReuseThread; rs.Open();
    using(var ps=PowerShell.Create()) {
     ps.Runspace=rs;
     ps.AddScript("$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';$global:LASTEXITCODE=0",false).Invoke();ps.Commands.Clear();
     ps.AddCommand(script); foreach(DictionaryEntry a in args) ps.AddParameter((string)a.Key,a.Value);
     Collection<PSObject> output=new Collection<PSObject>();
     try {output=ps.Invoke();} catch(Exception e) {host.Code=1;host.Buffer.AppendLine(e.ToString());}
     foreach(var e in ps.Streams.Error) host.Buffer.AppendLine(e.ToString());
     foreach(var e in ps.Streams.Warning) host.Buffer.AppendLine(e.ToString());
     foreach(var e in ps.Streams.Information) host.Buffer.AppendLine(e.ToString());
     if(ps.HadErrors && host.Code==0)host.Code=1;
     return new ScriptResult {ExitCode=host.Code,Log=host.Buffer.ToString(),Output=output};
    }
   }
  }
 }
 sealed class EmbeddedHost:PSHost {
  readonly Guid id=Guid.NewGuid(); readonly HostUI ui; public int Code; public readonly StringBuilder Buffer=new StringBuilder();
  public EmbeddedHost(){ui=new HostUI(Buffer);}
  public override Guid InstanceId{get{return id;}} public override string Name{get{return "D18Setup";}}
  public override Version Version{get{return new Version(1,0);}} public override PSHostUserInterface UI{get{return ui;}}
  public override CultureInfo CurrentCulture{get{return CultureInfo.CurrentCulture;}}
  public override CultureInfo CurrentUICulture{get{return CultureInfo.CurrentUICulture;}}
  public override void SetShouldExit(int code){Code=code;}
  public override void EnterNestedPrompt(){throw new InvalidOperationException("Interactive backend prompt is unsupported.");}
  public override void ExitNestedPrompt(){} public override void NotifyBeginApplication(){} public override void NotifyEndApplication(){}
 }
 sealed class HostUI:PSHostUserInterface {
  readonly StringBuilder b; public HostUI(StringBuilder buffer){b=buffer;}
  public override PSHostRawUserInterface RawUI{get{return null;}}
  public override void Write(string s){b.Append(s);} public override void Write(ConsoleColor f,ConsoleColor bg,string s){b.Append(s);}
  public override void WriteLine(string s){b.AppendLine(s);} public override void WriteErrorLine(string s){b.AppendLine(s);}
  public override void WriteDebugLine(string s){b.AppendLine(s);} public override void WriteVerboseLine(string s){b.AppendLine(s);}
  public override void WriteWarningLine(string s){b.AppendLine(s);} public override void WriteProgress(long id,ProgressRecord r){}
  public override string ReadLine(){throw new InvalidOperationException("Noninteractive operation requires explicit choices.");}
  public override System.Security.SecureString ReadLineAsSecureString(){throw new InvalidOperationException();}
  public override Dictionary<string,PSObject> Prompt(string caption,string message,Collection<FieldDescription> d){throw new InvalidOperationException();}
  public override int PromptForChoice(string caption,string message,Collection<ChoiceDescription> c,int i){throw new InvalidOperationException();}
  public override PSCredential PromptForCredential(string a,string b,string c,string d){throw new InvalidOperationException();}
  public override PSCredential PromptForCredential(string a,string b,string c,string d,PSCredentialTypes e,PSCredentialUIOptions f){throw new InvalidOperationException();}
 }
}
