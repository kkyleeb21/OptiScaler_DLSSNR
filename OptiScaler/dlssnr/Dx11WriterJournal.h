#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <cstdio>
#include <filesystem>
#include <atomic>
#include <cstring>
#include <share.h>

// Bounded CPU metadata only. Does not hold game resources, read pixels or submit GPU work.
namespace DlssNr::Dx11WriterJournal {
using Microsoft::WRL::ComPtr;
inline const GUID resourceTag={0x8101832a,0xa397,0x4221,{0x8c,0xb3,0x48,0x72,0xf0,0x7c,0x71,0x19}};
inline std::atomic<UINT64> nextId{1};
inline UINT64 Id(ID3D11Resource* r){
 if(!r)return 0;UINT64 id=0;UINT size=sizeof(id);
 if(SUCCEEDED(r->GetPrivateData(resourceTag,&size,&id))&&size==sizeof(id)&&id)return id;
 id=nextId++;return SUCCEEDED(r->SetPrivateData(resourceTag,sizeof(id),&id))?id:0;
}
struct Journal {
 FILE* file=nullptr;UINT records=0;UINT64 firstFrame=~0ULL;
 ~Journal(){if(file)fclose(file);}
 void Start(const std::filesystem::path& root,UINT64 frame){
  if(firstFrame!=~0ULL)return;firstFrame=frame;
  file=_wfsopen((root/L"D18PostReplay.jsonl").c_str(),L"wb",_SH_DENYNO);
 }
 bool Selected(UINT64 frame)const{return file&&records<256&&frame>=firstFrame&&((frame-firstFrame)<3||(frame-firstFrame)%1000==0);}
 void Resource(ID3D11Resource* r){
  D3D11_TEXTURE2D_DESC td{};ComPtr<ID3D11Texture2D> t;if(r&&SUCCEEDED(r->QueryInterface(IID_PPV_ARGS(&t))))t->GetDesc(&td);
  fprintf(file,"{\"id\":%llu,\"width\":%u,\"height\":%u,\"format\":%u,\"mips\":%u,\"array\":%u,\"samples\":%u,\"bind\":%u}",Id(r),td.Width,td.Height,UINT(td.Format),td.MipLevels,td.ArraySize,td.SampleDesc.Count,td.BindFlags);
 }
 void Record(UINT64 frame,bool invalid,const char* kind,ID3D11Resource* destination,ID3D11Resource* source=nullptr,ID3D11View* view=nullptr,const UINT* values=nullptr,const void* caller=nullptr,UINT64 shader=0,UINT slot=0,const UINT* range=nullptr){
  if(!Selected(frame))return;++records;
  char moduleName[MAX_PATH]="unknown";HMODULE module=nullptr;UINT64 offset=0;
  if(caller&&GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCSTR>(caller),&module)){
   char full[MAX_PATH]{};GetModuleFileNameA(module,full,MAX_PATH);const char* base=strrchr(full,'\\');strncpy_s(moduleName,base?base+1:full,_TRUNCATE);offset=UINT64(caller)-UINT64(module);
   for(char* p=moduleName;*p;++p)if(*p=='"'||*p=='\\'||static_cast<unsigned char>(*p)<32||static_cast<unsigned char>(*p)>=127)*p='_';
  }
  fprintf(file,"{\"event\":\"dx11_writer\",\"sequence\":%u,\"frame\":%llu,\"tick\":%llu,\"kind\":\"%s\",\"invalid\":%s,\"shader\":\"%016llx\",\"slot\":%u,\"caller_module\":\"%s\",\"caller_rva\":\"%llx\",\"destination\":",records,frame,GetTickCount64(),kind,invalid?"true":"false",shader,slot,moduleName,offset);
  Resource(destination);fprintf(file,",\"source\":");Resource(source);
  UINT format=0,dimension=0,mip=0,first=0,count=0;const char* type="none";
  ComPtr<ID3D11UnorderedAccessView> u;ComPtr<ID3D11RenderTargetView> rt;ComPtr<ID3D11ShaderResourceView> sv;
  if(view&&SUCCEEDED(view->QueryInterface(IID_PPV_ARGS(&u)))){D3D11_UNORDERED_ACCESS_VIEW_DESC d{};u->GetDesc(&d);type="uav";format=d.Format;dimension=d.ViewDimension;if(d.ViewDimension==D3D11_UAV_DIMENSION_TEXTURE2D){mip=d.Texture2D.MipSlice;count=1;}else if(d.ViewDimension==D3D11_UAV_DIMENSION_TEXTURE2DARRAY){mip=d.Texture2DArray.MipSlice;first=d.Texture2DArray.FirstArraySlice;count=d.Texture2DArray.ArraySize;}}
  else if(view&&SUCCEEDED(view->QueryInterface(IID_PPV_ARGS(&rt)))){D3D11_RENDER_TARGET_VIEW_DESC d{};rt->GetDesc(&d);type="rtv";format=d.Format;dimension=d.ViewDimension;if(d.ViewDimension==D3D11_RTV_DIMENSION_TEXTURE2D){mip=d.Texture2D.MipSlice;count=1;}else if(d.ViewDimension==D3D11_RTV_DIMENSION_TEXTURE2DARRAY){mip=d.Texture2DArray.MipSlice;first=d.Texture2DArray.FirstArraySlice;count=d.Texture2DArray.ArraySize;}}
  else if(view&&SUCCEEDED(view->QueryInterface(IID_PPV_ARGS(&sv)))){D3D11_SHADER_RESOURCE_VIEW_DESC d{};sv->GetDesc(&d);type="srv";format=d.Format;dimension=d.ViewDimension;if(d.ViewDimension==D3D11_SRV_DIMENSION_TEXTURE2D){mip=d.Texture2D.MostDetailedMip;count=d.Texture2D.MipLevels;}}
  fprintf(file,",\"view\":{\"type\":\"%s\",\"format\":%u,\"dimension\":%u,\"mip\":%u,\"first_slice\":%u,\"count\":%u},\"values_bits\":",type,format,dimension,mip,first,count);
  if(values)fprintf(file,"[%u,%u,%u,%u]",values[0],values[1],values[2],values[3]);else fputs("null",file);
  fputs(strcmp(kind,"dispatch")==0?",\"dispatch_details\":":",\"copy_range\":",file);if(range){fputc('[',file);for(UINT i=0;i<12;++i)fprintf(file,"%s%u",i?",":"",range[i]);fputc(']',file);}else fputs("null",file);
  fputs("}\n",file);fflush(file);
 }
};
}
