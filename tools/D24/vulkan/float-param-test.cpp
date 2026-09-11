#include <windows.h>
#include "../dx11_probe_parameters.h"
#include <cstdio>
int main(){
 auto m=LoadLibraryW(L"E:\\DLSSNR\\builds\\D24_Vulkan\\nvngx.dll_dlssnr.dll");if(!m)return 1;
 auto probe=(void(*)(void*,const char*,float,int))GetProcAddress(m,"dlssnr_call_probe_float");if(!probe)return 2;
 ProbeParameters p;float v=0;probe(&p,"test",0.375f,1);
 auto r=p.Get("test",&v);printf("legacy_slot1 result=%x value=%f\n",unsigned(r),v);
 NVSDK_NGX_Parameter* typed=&p;typed->Set("typed",0.375f);v=0;r=typed->Get("typed",&v);
 printf("typed_set result=%x value=%f\n",unsigned(r),v);
 return r==NVSDK_NGX_Result_Success&&v==0.375f?0:3;
}
