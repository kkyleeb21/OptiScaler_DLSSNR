#include <dlssnr/ResearchCapture.h>
#pragma once
#include <d3d11shader.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <filesystem>
#include <unordered_set>
#include <cstdio>
#include <share.h>
#include <string>
#include <sstream>
#include <vector>
#include <atomic>
#include <mutex>
namespace DlssNr::Dx11ComputeIdentity {
using Microsoft::WRL::ComPtr;
inline const GUID tag={0x89c3aff7,0xb1a5,0x4927,{0x9a,0x50,0x4d,0x27,0x52,0x2c,0x1a,0x20}};
struct Info {UINT64 hash=0,uav=0,srv[2]{};UINT group[3]{},known=0,cbMask=0,cbVectors[14]{};};
// Read executable declarations, not RDEF reflection metadata (which games may strip).
inline Info Describe(const void* data,SIZE_T size,UINT64 hash){
 Info out;out.hash=hash;ComPtr<ID3DBlob> assembly;
 if(!data||!size||size>256*1024||FAILED(D3DDisassemble(data,size,0,nullptr,&assembly))||assembly->GetBufferSize()>4*1024*1024)return out;
 std::string text(static_cast<const char*>(assembly->GetBufferPointer()),assembly->GetBufferSize());std::istringstream stream(text);std::string line;bool compute=false,group=false;
 while(std::getline(stream,line)){
  if(line.rfind("cs_",0)==0)compute=true;
  if(line.rfind("dcl_thread_group ",0)==0){if(sscanf_s(line.c_str(),"dcl_thread_group %u, %u, %u",&out.group[0],&out.group[1],&out.group[2])!=3)return Info{hash};group=true;}
  if(line.rfind("dcl_constantbuffer ",0)==0){UINT slot=0,count=0;if(sscanf_s(line.c_str(),"dcl_constantbuffer CB%u[%u]",&slot,&count)!=2||slot>=14||!count||count>4096)return Info{hash};out.cbMask|=1u<<slot;out.cbVectors[slot]=count;}
  const bool uav=line.rfind("dcl_uav_",0)==0;const bool srv=line.rfind("dcl_resource_",0)==0;
  if(!uav&&!srv)continue;
  auto pos=line.find(uav?" u":" t");if(pos==std::string::npos)return Info{hash};pos+=2;
  UINT slot=0;const auto first=pos;while(pos<line.size()&&line[pos]>='0'&&line[pos]<='9'){slot=slot*10+UINT(line[pos++]-'0');if(slot>=128)return Info{hash};}
  if(pos==first||(pos<line.size()&&line[pos]!=' '&&line[pos]!=','&&line[pos]!='\r'))return Info{hash};
  if(uav){if(slot>=64)return Info{hash};out.uav|=1ULL<<slot;}else out.srv[slot/64]|=1ULL<<(slot%64);
 }
 if(!compute||!group||!out.group[0]||!out.group[1]||!out.group[2]||UINT64(out.group[0])*out.group[1]*out.group[2]>1024)return Info{hash};
 out.known=1;return out;
}
inline const GUID codeTag={0x123f0d92,0xacc5,0x4fc9,{0xa1,0x29,0x51,0x84,0xca,0x17,0x25,0x21}};
inline std::atomic<UINT64> cacheBudget{0};
inline void Cache(ID3D11DeviceChild* shader,const void* code,UINT size){
 if(!shader||!code||!size||size>256*1024)return;
 auto used=cacheBudget.load();do{if(used+size>64ULL*1024*1024)return;}while(!cacheBudget.compare_exchange_weak(used,used+size));
 shader->SetPrivateData(codeTag,size,code);
}
inline Info Read(ID3D11ComputeShader* shader){Info info;UINT bytes=sizeof(info);if(shader&&(FAILED(shader->GetPrivateData(tag,&bytes,&info))||bytes!=sizeof(info)))return {};return info;}
struct Archive {
 FILE* data=nullptr;FILE* index=nullptr;UINT64 bytes=0;std::unordered_set<UINT64> seen;bool opened=false;std::mutex mutex;
 ~Archive(){if(data)fclose(data);if(index)fclose(index);}
 void Open(const std::filesystem::path& root){
  if(!BuildProfile::ResearchCaptureRequested(root))return;
  std::lock_guard lock(mutex);if(opened)return;opened=true;data=_wfsopen((root/L"D18PostCompute.bin").c_str(),L"wb",_SH_DENYNO);index=_wfsopen((root/L"D18PostCompute.jsonl").c_str(),L"wb",_SH_DENYNO);
 }
 bool Capture(const std::filesystem::path& root,ID3D11ComputeShader* shader,const Info& info){
  if(!BuildProfile::ResearchCaptureRequested(root))return false;
  {std::lock_guard lock(mutex);if(seen.count(info.hash))return true;}
  UINT size=0;if(FAILED(shader->GetPrivateData(codeTag,&size,nullptr))||!size||size>256*1024)return false;
  std::vector<unsigned char> payload(size);if(FAILED(shader->GetPrivateData(codeTag,&size,payload.data())))return false;
  Add(root,info,payload.data(),size);std::lock_guard lock(mutex);return seen.count(info.hash)!=0;
 }
 void Add(const std::filesystem::path& root,const Info& info,const void* code,SIZE_T size){
  if(!BuildProfile::ResearchCaptureRequested(root))return;
  Open(root);std::lock_guard lock(mutex);
  if(seen.size()>=256||size>256*1024||bytes+size>16*1024*1024||!seen.insert(info.hash).second)return;
  if(!opened){opened=true;data=_wfsopen((root/L"D18PostCompute.bin").c_str(),L"wb",_SH_DENYNO);index=_wfsopen((root/L"D18PostCompute.jsonl").c_str(),L"wb",_SH_DENYNO);}
  if(!data||!index)return;
  const auto written=fwrite(code,1,size,data);fflush(data);
  fprintf(index,"{\"event\":\"compute_shader\",\"hash\":\"%016llx\",\"known\":%u,\"uav_mask\":\"%016llx\",\"srv_masks\":[\"%016llx\",\"%016llx\"],\"group\":[%u,%u,%u],\"offset\":%llu,\"bytes\":%llu,\"complete\":%s}\n",info.hash,info.known,info.uav,info.srv[0],info.srv[1],info.group[0],info.group[1],info.group[2],bytes,UINT64(size),written==size?"true":"false");fflush(index);bytes+=written;
 }
};
}
