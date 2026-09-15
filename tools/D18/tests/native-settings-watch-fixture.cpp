#include <windows.h>
#include <string>
#include <cstring>
#pragma comment(lib,"user32.lib")
int wmain(int argc,wchar_t**argv){
 if(argc!=3)return 2;
 auto base=reinterpret_cast<ULONG_PTR>(GetModuleHandleW(nullptr));
 void* allocation=VirtualAlloc(reinterpret_cast<void*>(base+0x4d50000),0x10000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
 if(!allocation)return 3;
 constexpr ULONG_PTR offsets[]={0x4d5b120,0x4d5ba00,0x4d5c2e0};
 for(auto offset:offsets){auto ptr=reinterpret_cast<unsigned char*>(base+offset);unsigned w=3840,h=2160;float f=.75f;memcpy(ptr+0x3c,&w,4);memcpy(ptr+0x40,&h,4);memcpy(ptr+0x6c,&f,4);}
 unsigned char graphics[0x800]{},render[0x40]{};float renderScale=.625f;
 auto graphicsPtr=reinterpret_cast<ULONG_PTR>(graphics),renderPtr=reinterpret_cast<ULONG_PTR>(render);
 HWND window=CreateWindowExW(0,L"STATIC",L"D18 fixture",0,0,0,0,0,HWND_MESSAGE,nullptr,GetModuleHandleW(nullptr),nullptr);
 if(!window)return 7;
 memcpy(reinterpret_cast<void*>(base+0x4d5b030),&graphicsPtr,sizeof(graphicsPtr));
 memcpy(graphics+0x30,&renderPtr,sizeof(renderPtr));memcpy(graphics+0x760,&window,sizeof(window));memcpy(render+0x1c,&renderScale,4);
 wchar_t path[32768];if(!GetModuleFileNameW(nullptr,path,32768))return 4;
 std::wstring cmd=L"\""+std::wstring(argv[1])+L"\" "+std::to_wstring(GetCurrentProcessId())+L" 2 \""+path+L"\" \""+argv[2]+L"\" --render-state";
 STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
 if(!CreateProcessW(argv[1],cmd.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process))return 5;
 Sleep(500);float changed=.5f;memcpy(reinterpret_cast<void*>(base+offsets[1]+0x6c),&changed,4);
 Sleep(300);memcpy(reinterpret_cast<void*>(base+offsets[0]+0x6c),&changed,4);
 const DWORD wait=WaitForSingleObject(process.hProcess,5000);DWORD code=99;GetExitCodeProcess(process.hProcess,&code);CloseHandle(process.hThread);CloseHandle(process.hProcess);
 DestroyWindow(window);VirtualFree(allocation,0,MEM_RELEASE);return wait==WAIT_OBJECT_0?int(code):6;
}
