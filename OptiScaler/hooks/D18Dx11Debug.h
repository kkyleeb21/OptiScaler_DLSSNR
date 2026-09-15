#include <dlssnr/BuildProfile.h>
#pragma once
#include <d3d11sdklayers.h>
#include <wrl/client.h>
#include <filesystem>
#include <cstdio>
#include <share.h>
#include <mutex>
#include <vector>
#include <string>
namespace D18Dx11Debug {
inline std::filesystem::path Root(){wchar_t s[32768]{};GetModuleFileNameW(nullptr,s,32768);return std::filesystem::path(s).parent_path();}
inline bool Enabled(){if constexpr(!DlssNr::BuildProfile::Diagnostic)return false;static bool on=GetFileAttributesW((Root()/L"D18Dx11Debug.enabled").c_str())!=INVALID_FILE_ATTRIBUTES;return on;}
inline std::mutex mutex;inline unsigned records=0;
inline void Record(const char* event,long code,const char* description){
 if(!Enabled())return;std::lock_guard lock(mutex);if(records++>=256)return;
 static FILE* f=_wfsopen((Root()/L"D18Dx11Debug.jsonl").c_str(),L"wb",_SH_DENYNO);if(!f)return;
 std::string escaped;for(const unsigned char ch:std::string(description)){if(ch=='"'||ch=='\\')escaped+='\\';if(ch<32)escaped+=' ';else escaped+=char(ch);}
 fprintf(f,"{\"event\":\"%s\",\"tick\":%llu,\"code\":%ld,\"description\":\"%s\"}\n",event,GetTickCount64(),code,escaped.c_str());fflush(f);
}
template<class F,class... Tail> HRESULT Create(F fn,IDXGIAdapter* adapter,D3D_DRIVER_TYPE type,HMODULE software,UINT flags,const D3D_FEATURE_LEVEL* levels,UINT count,UINT sdk,Tail... tail){
 if(!Enabled())return fn(adapter,type,software,flags,levels,count,sdk,tail...);
 const UINT requested=flags|D3D11_CREATE_DEVICE_DEBUG;
 Record("debug_create_requested",long(flags),"Caller feature levels preserved; debug flag added");
 auto hr=fn(adapter,type,software,requested,levels,count,sdk,tail...);
 if(hr==DXGI_ERROR_SDK_COMPONENT_MISSING&&!(flags&D3D11_CREATE_DEVICE_DEBUG)){Record("debug_layer_unavailable",hr,"Retrying exact caller flags without added debug layer");hr=fn(adapter,type,software,flags,levels,count,sdk,tail...);}
 Record("debug_create_result",hr,"Device creation result; InfoQueue availability reported separately");return hr;
}
inline void Poll(ID3D11Device* d){
 if(!Enabled()||!d)return;
 static std::mutex pollMutex;std::unique_lock lock(pollMutex,std::try_to_lock);if(!lock)return;
 static Microsoft::WRL::ComPtr<ID3D11InfoQueue> queue;static ID3D11Device* identity=nullptr;static UINT64 cursor=0;
 if(identity!=d){queue.Reset();cursor=0;identity=d;auto hr=d->QueryInterface(IID_PPV_ARGS(&queue));Record("info_queue_status",hr,SUCCEEDED(hr)?"InfoQueue available":"InfoQueue unavailable");}
 if(!queue)return;auto total=queue->GetNumStoredMessagesAllowedByRetrievalFilter();if(total<cursor)cursor=0;
 for(unsigned n=0;cursor<total&&n<32;++cursor,++n){SIZE_T size=0;if(FAILED(queue->GetMessage(cursor,nullptr,&size))||size>16384)continue;std::vector<unsigned char> bytes(size);auto m=reinterpret_cast<D3D11_MESSAGE*>(bytes.data());if(SUCCEEDED(queue->GetMessage(cursor,m,&size))&&m->Severity<=D3D11_MESSAGE_SEVERITY_WARNING)Record("info_queue_message",long(m->ID),m->pDescription?m->pDescription:"");}
 // Do not clear or replace filters on the game's shared InfoQueue.
}
}
