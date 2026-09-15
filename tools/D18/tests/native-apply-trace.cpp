// One-shot hardware execution breakpoint. Does not invoke the native function.
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#include <dbgeng.h>
#include <wrl/client.h>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
using Microsoft::WRL::ComPtr;
struct Session {
 ComPtr<IDebugClient> client;
 ~Session(){if(client){client->DetachProcesses();client->EndSession(DEBUG_END_PASSIVE);}}
};
int wmain(int argc,wchar_t** argv){
 if(argc!=6)return 2;
 // First GRW live trial exited before any native Apply; retain fixture research
 // only. Block direct invocation too, not merely the Python wrapper.
 const wchar_t* name=wcsrchr(argv[3],L'\\');name=name?name+1:argv[3];
 if(_wcsicmp(name,L"native-apply-trace-fixture.exe")){fputs("Live attachment disabled; fixture-only.\n",stderr);return 12;}
 DWORD pid=wcstoul(argv[1],nullptr,10),seconds=wcstoul(argv[2],nullptr,10);
 ULONG64 rva=_wcstoui64(argv[4],nullptr,0);
 if(!pid||seconds<1||seconds>60||!rva||rva>0x40000000)return 3;
 HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|PROCESS_VM_READ,FALSE,pid);
 if(!process)return 4;
 wchar_t path[32768];DWORD size=32768;
 bool identity=QueryFullProcessImageNameW(process,0,path,&size)&&!_wcsicmp(path,argv[3]);
 CloseHandle(process);if(!identity)return 5;
 HANDLE snap=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,pid);
 if(snap==INVALID_HANDLE_VALUE)return 6;
 MODULEENTRY32W module{};module.dwSize=sizeof(module);ULONG64 base=0;
 if(Module32FirstW(snap,&module))do{if(!_wcsicmp(module.szExePath,path)&&rva<module.modBaseSize){base=reinterpret_cast<ULONG64>(module.modBaseAddr);break;}}while(Module32NextW(snap,&module));
 CloseHandle(snap);if(!base)return 7;
 FILE* file=nullptr;if(_wfopen_s(&file,argv[5],L"wb"))return 8;
 // Session destruction precedes closing the evidence file; detach on all exits.
 int result=[&](){
  Session session;ComPtr<IDebugControl> control;ComPtr<IDebugSystemObjects> system;
  ComPtr<IDebugRegisters> registers;ComPtr<IDebugSymbols> symbols;
  auto fail=[&](const char* step,HRESULT hr){fprintf(file,"{\"event\":\"trace_error\",\"step\":\"%s\",\"hresult\":%lu}\n",step,static_cast<ULONG>(hr));fflush(file);return 9;};
  HRESULT hr=DebugCreate(__uuidof(IDebugClient),reinterpret_cast<void**>(session.client.GetAddressOf()));if(FAILED(hr))return fail("create",hr);
  if(FAILED(hr=session.client.As(&control))||FAILED(hr=session.client.As(&system))||FAILED(hr=session.client.As(&registers))||FAILED(hr=session.client.As(&symbols)))return fail("interfaces",hr);
  symbols->SetSymbolPath("."); // No symbol server, addresses and module RVAs suffice.
  control->AddEngineOptions(DEBUG_ENGOPT_INITIAL_BREAK);
  if(FAILED(hr=session.client->AttachProcess(0,pid,DEBUG_ATTACH_DEFAULT)))return fail("attach",hr);
  if((hr=control->WaitForEvent(0,10000))!=S_OK)return fail("initial_event",hr);
  if(FAILED(hr=session.client->SetProcessOptions(DEBUG_PROCESS_DETACH_ON_EXIT)))return fail("detach_policy",hr);
  // DbgEng owns breakpoint interfaces; removal invalidates the pointer.
  IDebugBreakpoint* breakpoint=nullptr;
  if(FAILED(hr=control->AddBreakpoint(DEBUG_BREAKPOINT_DATA,DEBUG_ANY_ID,&breakpoint))||
     FAILED(hr=breakpoint->SetOffset(base+rva))||FAILED(hr=breakpoint->SetDataParameters(1,DEBUG_BREAK_EXECUTE))||
     FAILED(hr=breakpoint->AddFlags(DEBUG_BREAKPOINT_ENABLED|DEBUG_BREAKPOINT_ONE_SHOT)))return fail("breakpoint",hr);
  ULONG wanted=0;if(FAILED(hr=breakpoint->GetId(&wanted)))return fail("breakpoint_id",hr);
  fprintf(file,"{\"event\":\"trace_armed\",\"pid\":%lu,\"base\":%llu,\"rva\":%llu,\"limit_seconds\":%lu}\n",pid,base,rva,seconds);fflush(file);
  if(FAILED(hr=control->SetExecutionStatus(DEBUG_STATUS_GO)))return fail("continue",hr);
  const ULONGLONG start=GetTickCount64();bool hit=false;
  while(GetTickCount64()-start<seconds*1000ULL){
   hr=control->WaitForEvent(0,250);
   if(hr==S_FALSE)continue;
   if(FAILED(hr))return fail("wait",hr);
   ULONG type=0,processId=0,threadId=0,used=0;unsigned char extra[256]{};
   hr=control->GetLastEventInformation(&type,&processId,&threadId,extra,sizeof(extra),&used,nullptr,0,nullptr);
   if(FAILED(hr))return fail("event",hr);
   if(type==DEBUG_EVENT_BREAKPOINT&&used>=sizeof(DEBUG_LAST_EVENT_INFO_BREAKPOINT)&&reinterpret_cast<DEBUG_LAST_EVENT_INFO_BREAKPOINT*>(extra)->Id==wanted){
    ULONG tid=0,index=0;DEBUG_VALUE arg{};bool argRead=false;system->GetCurrentThreadSystemId(&tid);
    if(SUCCEEDED(registers->GetIndexByName("rcx",&index)))argRead=SUCCEEDED(registers->GetValue(index,&arg));
    DEBUG_STACK_FRAME frames[24]{};ULONG count=0;HRESULT stackResult=control->GetStackTrace(0,0,0,frames,24,&count);
    fprintf(file,"{\"event\":\"native_apply_hit\",\"tick\":%llu,\"thread_id\":%lu,\"rcx_available\":%s,\"rcx\":%llu,\"stack_hresult\":%lu,\"stack\":[",GetTickCount64(),tid,argRead?"true":"false",arg.I64,static_cast<ULONG>(stackResult));
    for(ULONG i=0;i<count;++i)fprintf(file,"%s%llu",i?",":"",frames[i].InstructionOffset);
    fputs("]}\n",file);fflush(file);hit=true;break;
   }
   // Preserve native exceptions. Do not swallow an exception or keep tracing a crash.
   if(type==DEBUG_EVENT_EXCEPTION){control->SetExecutionStatus(DEBUG_STATUS_GO_NOT_HANDLED);return fail("unrelated_exception",S_FALSE);}
   if(type==DEBUG_EVENT_EXIT_PROCESS)break;
   if(FAILED(hr=control->SetExecutionStatus(DEBUG_STATUS_GO)))return fail("continue_event",hr);
  }
  // Consume a hardware single-step event before detach; detaching at the event
  // itself can deliver STATUS_SINGLE_STEP to the target after the debugger leaves.
  if(hit){
   if(FAILED(hr=control->SetExecutionStatus(DEBUG_STATUS_GO)))return fail("resume_hit",hr);
   hr=control->WaitForEvent(0,100);
   if(hr!=S_FALSE)return fail("resume_hit_wait",hr);
  }
  // Stop only for removing a still-armed breakpoint on the timeout path.
  if(!hit){control->SetInterrupt(DEBUG_INTERRUPT_ACTIVE);control->WaitForEvent(0,5000);}
  // ONE_SHOT is removed by the engine when hit; otherwise remove it explicitly.
  if(!hit)control->RemoveBreakpoint(breakpoint);
  hr=session.client->DetachProcesses();
  fprintf(file,"{\"event\":\"trace_end\",\"hit\":%s,\"detach_hresult\":%lu}\n",hit?"true":"false",static_cast<ULONG>(hr));fflush(file);
  return FAILED(hr)?10:hit?0:11;
 }();
 fclose(file);return result;
}
