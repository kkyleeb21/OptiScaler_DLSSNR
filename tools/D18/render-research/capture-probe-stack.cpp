#define NOMINMAX
#include <windows.h>
#include <dbghelp.h>
#include <cstdio>
#include <cstdlib>
#include <string>
int wmain(int argc,wchar_t** argv){
 if(argc!=4)return 2;
 const DWORD pid=wcstoul(argv[1],nullptr,10),tid=wcstoul(argv[2],nullptr,10);
 HANDLE process=OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ,FALSE,pid);if(!process)return 3;
 wchar_t path[32768];DWORD n=32768;
 if(!QueryFullProcessImageNameW(process,0,path,&n)||_wcsicmp(path,argv[3])){CloseHandle(process);return 4;}
 SymSetOptions(SYMOPT_UNDNAME|SYMOPT_DEFERRED_LOADS|SYMOPT_FAIL_CRITICAL_ERRORS);
 if(!SymInitialize(process,nullptr,TRUE)){CloseHandle(process);return 5;}
 HANDLE thread=OpenThread(THREAD_SUSPEND_RESUME|THREAD_GET_CONTEXT|THREAD_QUERY_INFORMATION,FALSE,tid);
 if(!thread||GetProcessIdOfThread(thread)!=pid){if(thread)CloseHandle(thread);SymCleanup(process);CloseHandle(process);return 6;}
 if(SuspendThread(thread)==DWORD(-1)){CloseHandle(thread);SymCleanup(process);CloseHandle(process);return 7;}
 CONTEXT context{};context.ContextFlags=CONTEXT_FULL;bool ok=GetThreadContext(thread,&context)!=0;
 if(ok){
  STACKFRAME64 frame{};frame.AddrPC.Offset=context.Rip;frame.AddrPC.Mode=AddrModeFlat;
  frame.AddrFrame.Offset=context.Rbp;frame.AddrFrame.Mode=AddrModeFlat;frame.AddrStack.Offset=context.Rsp;frame.AddrStack.Mode=AddrModeFlat;
  for(int i=0;i<48;++i){
   const auto pc=frame.AddrPC.Offset;if(!pc)break;
   IMAGEHLP_MODULE64 module{};module.SizeOfStruct=sizeof(module);
   SymGetModuleInfo64(process,pc,&module);
   printf("%02d pc=%llx module=%s rva=%llx image=%s\n",i,pc,module.ModuleName,pc-module.BaseOfImage,module.LoadedImageName);
   if(!StackWalk64(IMAGE_FILE_MACHINE_AMD64,process,thread,&frame,&context,nullptr,SymFunctionTableAccess64,SymGetModuleBase64,nullptr))break;
  }
 }
 ResumeThread(thread);CloseHandle(thread);SymCleanup(process);CloseHandle(process);return ok?0:8;
}
