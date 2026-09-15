#define NOMINMAX
#include <windows.h>
#include <string>
#include <cstdio>
__declspec(noinline) unsigned Target(volatile unsigned* count){return ++*count;}
int wmain(int argc,wchar_t** argv){
 if(argc!=4)return 2;
 wchar_t path[32768];if(!GetModuleFileNameW(nullptr,path,32768))return 3;
 auto base=reinterpret_cast<ULONG_PTR>(GetModuleHandleW(nullptr));
 auto rva=reinterpret_cast<ULONG_PTR>(&Target)-base;
 std::wstring command=L"\""+std::wstring(argv[1])+L"\" "+std::to_wstring(GetCurrentProcessId())+L" 2 \""+path+L"\" "+std::to_wstring(rva)+L" \""+argv[2]+L"\"";
 STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
 if(!CreateProcessW(argv[1],command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process))return 4;
 volatile unsigned calls=0;bool hit=wcscmp(argv[3],L"hit")==0;
 // Attaching briefly stops the fixture; after resume both hit and timeout paths run.
 ULONGLONG start=GetTickCount64();
 while(GetTickCount64()-start<8000&&WaitForSingleObject(process.hProcess,50)==WAIT_TIMEOUT){if(hit)Target(&calls);}
 DWORD code=99;bool done=WaitForSingleObject(process.hProcess,5000)==WAIT_OBJECT_0;
 GetExitCodeProcess(process.hProcess,&code);CloseHandle(process.hThread);CloseHandle(process.hProcess);
 Target(&calls); // A leaked hardware breakpoint must fail even on the timeout path.
 BOOL attached=TRUE;CheckRemoteDebuggerPresent(GetCurrentProcess(),&attached);
 printf("done=%d code=%lu attached=%d calls=%u\n",done,code,attached,static_cast<unsigned>(calls));
 return done&&!attached&&code==(hit?0u:11u)?0:5;
}
