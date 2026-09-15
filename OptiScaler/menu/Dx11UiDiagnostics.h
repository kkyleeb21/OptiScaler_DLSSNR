#pragma once
#include <d3d11.h>
#include <filesystem>
#include <cstdio>
#include <share.h>
#include <hooks/D18ExecutionTrace.h>

// Opt-in, bounded UI evidence shared by both DX11 menu paths. Counts describe
// CPU rendering submissions; SR GPU completion remains a separate event.
namespace Dx11UiDiagnostics {
inline void Record(const std::filesystem::path&,ID3D11DeviceContext* context,bool visible,int vertices,int indices,HRESULT code){
 static bool checked=false;static FILE* file=nullptr;
 static unsigned progress=0,failures=0;static unsigned long long frames=0,lastTick=0;static bool lastVisible=false;
 if(!checked){checked=true;const auto root=D18ExecutionTrace::Root();if(!root.empty()&&GetFileAttributesW((root/L"D18UiDiagnostics.enabled").c_str())!=INVALID_FILE_ATTRIBUTES)file=_wfsopen((root/L"D18UiDiagnostics.jsonl").c_str(),L"wb",_SH_DENYNO);}
 if(!file)return;++frames;const auto now=GetTickCount64();
 if(FAILED(code)){if(failures++>=16)return;}
 else {if(progress>=128|| (progress&&visible==lastVisible&&now-lastTick<1000))return;++progress;}
 lastTick=now;lastVisible=visible;
 fprintf(file,"{\"event\":\"dx11_ui\",\"tick\":%llu,\"thread\":%lu,\"frame\":%llu,\"visible\":%s,\"vertices\":%d,\"indices\":%d,\"code\":%ld,\"context\":\"%p\"}\n",now,GetCurrentThreadId(),frames,visible?"true":"false",vertices,indices,long(code),context);fflush(file);
}
}
