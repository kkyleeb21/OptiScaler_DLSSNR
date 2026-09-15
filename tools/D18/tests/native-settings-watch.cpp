// Bounded, read-only settings snapshots; no injection, breakpoint, or process writes.
#include <windows.h>
#include <tlhelp32.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <cmath>
#pragma comment(lib,"user32.lib")
// Optional one-shot readback of the statically identified render-settings chain.
// This records window ownership, not the thread which called native Apply.
static void RenderState(HANDLE p,ULONG_PTR base,DWORD pid,FILE* f){
 auto read=[&](ULONG_PTR address,void* dst,SIZE_T size){SIZE_T got=0;return address&&ReadProcessMemory(p,reinterpret_cast<const void*>(address),dst,size,&got)&&got==size;};
 ULONG_PTR graphics=0,settings=0,window=0;unsigned char pending=0;float scale=0;
 const bool valid=read(base+0x4d5b030,&graphics,sizeof(graphics))&&graphics&&
  read(graphics+0x30,&settings,sizeof(settings))&&settings&&
  read(graphics+0x760,&window,sizeof(window))&&
  read(settings+0x1c,&scale,sizeof(scale))&&std::isfinite(scale)&&
  read(base+0x4d5b039,&pending,sizeof(pending));
 DWORD windowPid=0,thread=valid?GetWindowThreadProcessId(reinterpret_cast<HWND>(window),&windowPid):0;
 if(!valid){fputs("{\"event\":\"render_settings_probe\",\"available\":false}\n",f);return;}
 fprintf(f,"{\"event\":\"render_settings_probe\",\"available\":true,\"tick\":%llu,\"render_scale_field\":%.9g,\"pending_field\":%u,\"window_thread\":%lu,\"window_owned_by_process\":%s}\n",GetTickCount64(),static_cast<double>(scale),static_cast<unsigned>(pending),thread,thread&&windowPid==pid?"true":"false");
}
int wmain(int argc,wchar_t** argv){
 if(argc!=5&&argc!=6)return 2;
 if(argc==6&&wcscmp(argv[5],L"--render-state"))return 2;
 const DWORD pid=wcstoul(argv[1],nullptr,10),seconds=wcstoul(argv[2],nullptr,10);
 if(!pid||seconds<1||seconds>60)return 3;
 HANDLE p=OpenProcess(PROCESS_VM_READ|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
 if(!p){printf("{\"error\":\"open_process\",\"code\":%lu}\n",GetLastError());return 4;}
 wchar_t path[32768];DWORD count=32768;
 if(!QueryFullProcessImageNameW(p,0,path,&count)||_wcsicmp(path,argv[3])){CloseHandle(p);return 5;}
 HANDLE snap=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,pid);
 if(snap==INVALID_HANDLE_VALUE){CloseHandle(p);return 6;}
 MODULEENTRY32W m{};m.dwSize=sizeof(m);ULONG_PTR base=0;
 if(Module32FirstW(snap,&m)){do{if(!_wcsicmp(m.szExePath,path)){base=reinterpret_cast<ULONG_PTR>(m.modBaseAddr);break;}}while(Module32NextW(snap,&m));}
 CloseHandle(snap);if(!base){CloseHandle(p);return 7;}
 FILE* f=nullptr;if(_wfopen_s(&f,argv[4],L"wb")){CloseHandle(p);return 8;}
 // This executable revision only; the caller verifies full image SHA256 first.
 constexpr ULONG_PTR offsets[]={0x4d5b120,0x4d5ba00,0x4d5c2e0};
 unsigned char previous[3][0xd0]{};bool seen[3]{};unsigned events=0;
 const auto start=GetTickCount64();
 fprintf(f,"{\"event\":\"settings_watch_begin\",\"pid\":%lu,\"base\":%llu,\"tick\":%llu,\"limit_seconds\":%lu}\n",pid,static_cast<unsigned long long>(base),start,seconds);fflush(f);
 if(argc==6){RenderState(p,base,pid,f);fflush(f);}
 while(GetTickCount64()-start<seconds*1000ULL&&events<256){
  DWORD code=0;if(!GetExitCodeProcess(p,&code)||code!=STILL_ACTIVE)break;
  for(unsigned i=0;i<3;++i){unsigned char data[0xd0]{};SIZE_T read=0;
   if(!ReadProcessMemory(p,reinterpret_cast<const void*>(base+offsets[i]),data,sizeof(data),&read)||read!=sizeof(data)){
    fprintf(f,"{\"event\":\"read_failed\",\"slot\":%u,\"code\":%lu}\n",i,GetLastError());fclose(f);CloseHandle(p);return 9;}
   if(seen[i]&&!memcmp(data,previous[i],sizeof(data)))continue;
   unsigned width=0,height=0;float scale=0;memcpy(&width,data+0x3c,4);memcpy(&height,data+0x40,4);memcpy(&scale,data+0x6c,4);
   char scaleText[64]="null";if(std::isfinite(scale))sprintf_s(scaleText,"%.9g",static_cast<double>(scale));
   fprintf(f,"{\"event\":\"settings_snapshot\",\"tick\":%llu,\"slot\":%u,\"rva\":%llu,\"width_field\":%u,\"height_field\":%u,\"scale_field\":%s,\"bytes\":\"",GetTickCount64(),i,static_cast<unsigned long long>(offsets[i]),width,height,scaleText);
   for(auto b:data)fprintf(f,"%02x",b);fputs("\"}\n",f);fflush(f);memcpy(previous[i],data,sizeof(data));seen[i]=true;++events;
  }
  Sleep(25);
 }
 fprintf(f,"{\"event\":\"settings_watch_end\",\"tick\":%llu,\"snapshots\":%u,\"at_limit\":%s}\n",GetTickCount64(),events,events>=256?"true":"false");
 fclose(f);CloseHandle(p);return 0;
}
