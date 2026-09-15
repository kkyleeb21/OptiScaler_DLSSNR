#include <dlssnr/ResearchCapture.h>
#pragma once
#include "Dx11CommandListWrites.h"
#include <cstdio>
#include <filesystem>
#include <share.h>
namespace DlssNr::Dx11CommandListTrace {
namespace M=Dx11CommandListWrites;
struct Journal {
 std::mutex mutex;FILE* file=nullptr;std::atomic<bool> selected{false};bool truncated=false;UINT records=0,sessions=0,executions=0,frameRecords=0,phaseRecords=0,summaryRecords=0;UINT64 frame=0,first=~0ULL;ID3D11DeviceContext* context=nullptr;
 ~Journal(){if(file)fclose(file);}
 UINT recordLimit=65532;long long byteLimit=64LL*1024*1024;
 bool Room(bool phase=false,bool summary=false){
  const auto bytes=_ftelli64(file);
  if(records>=recordLimit||bytes>byteLimit-4096){truncated=true;return false;}
  if(!phase&&!summary&&(records>=64500||bytes>63LL*1024*1024||frameRecords>=15000)){truncated=true;return false;}
  if(summary&&summaryRecords>=512){truncated=true;return false;}
  if(summary)++summaryRecords;
  if(phase&&phaseRecords>=128){truncated=true;return false;}
  if(phase)++phaseRecords;return true;
 }
 void Header(const char* event){++records;++frameRecords;fprintf(file,"{\"event\":\"%s\",\"sequence\":%u,\"frame\":%llu,\"tick\":%llu,\"thread\":%lu",event,records,frame,GetTickCount64(),GetCurrentThreadId());}
 void Resource(const M::ResourceInfo& r){fprintf(file,"{\"id\":%llu,\"dimension\":%u,\"width\":%u,\"height\":%u,\"depth\":%u,\"format\":%u,\"mips\":%u,\"array\":%u,\"samples\":%u,\"bind\":%u,\"bytes\":%u,\"stride\":%u,\"view_format\":%u,\"view_dimension\":%u,\"view_first\":%u,\"view_count\":%u}",r.id,r.dimension,r.width,r.height,r.depth,r.format,r.mips,r.array,r.samples,r.bind,r.bytes,r.stride,r.viewFormat,r.viewDimension,r.first,r.count);}
 void Begin(const std::filesystem::path& root,UINT64 f,ID3D11DeviceContext* c){if(!BuildProfile::ResearchCaptureRequested(root))return;std::lock_guard lock(mutex);
  if(selected.load()){Header("frame_end");fputs(",\"complete\":false,\"reason\":\"new_frame_before_frame_end\"}\n",file);selected=false;}
  if(first==~0ULL){first=f;file=_wfsopen((root/L"D18CommandLists.jsonl").c_str(),L"wb",_SH_DENYNO);}if(!file||sessions>=4||!Room(true))return;
  if(f<first||!((f-first)<3||(f-first)==1000))return;
  frame=f;context=c;executions=0;frameRecords=0;phaseRecords=0;summaryRecords=0;truncated=false;M::extendedTrace=true;++sessions;selected=true;Header("frame_begin");fputs(",\"schema\":\"d18-command-list-frame-v2\",\"start\":\"after_present\",\"payload_captured\":false,\"pipeline_state_capture\":\"temporary_extended_critical_lists_only\"}\n",file);fflush(file);
 }
 void Mark(ID3D11DeviceContext* c,const char* event,IDXGISwapChain* chain=nullptr){if(!selected.load())return;std::lock_guard lock(mutex);if(!selected.load()||c!=context||!Room(true))return;Header(event);
  if(chain){M::ComPtr<ID3D11Texture2D> back;auto hr=chain->GetBuffer(0,IID_PPV_ARGS(&back));fprintf(file,",\"buffer_status\":%ld,\"backbuffer\":",hr);Resource(M::Describe(back.Get()));DXGI_SWAP_CHAIN_DESC d{};auto dh=chain->GetDesc(&d);fprintf(file,",\"swapchain_status\":%ld,\"buffers\":%u,\"swap_effect\":%u,\"windowed\":%s",dh,d.BufferCount,UINT(d.SwapEffect),d.Windowed?"true":"false");}
  fputs("}\n",file);fflush(file);
 }
 void Execute(const M::List* list,UINT decision,BOOL restore,const void* caller){if(!selected.load())return;std::lock_guard lock(mutex);if(!selected.load()||!Room(false,true))return;++executions;
  char moduleName[MAX_PATH]="unknown";HMODULE module=nullptr;UINT64 rva=0;
  if(caller&&GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCSTR>(caller),&module)){char path[MAX_PATH]{};GetModuleFileNameA(module,path,MAX_PATH);auto base=strrchr(path,'\\');strncpy_s(moduleName,base?base+1:path,_TRUNCATE);rva=UINT64(caller)-UINT64(module);for(auto p=moduleName;*p;++p)if(*p=='"'||*p=='\\'||static_cast<unsigned char>(*p)<32||static_cast<unsigned char>(*p)>=127)*p='_';}
  Header("execute");fprintf(file,",\"list\":%llu,\"decision\":%u,\"restore\":%s,\"reasons\":%u,\"writes\":%u,\"operations\":%u,\"draws\":%u,\"dispatches\":%u,\"trace_records\":%u,\"trace_dropped\":%u,\"caller_module\":\"%s\",\"caller_rva\":\"%llx\"}\n",list?list->id:0,decision,restore?"true":"false",list?list->writes.reasons:32u,list?list->writes.size:0,list?list->writes.operations:0,list?list->writes.draws:0,list?list->writes.dispatches:0,list?list->writes.traceSize:0,list?list->writes.traceDropped:0,moduleName,rva);
  const bool details=decision==1||decision==2;
  if(Room(false,true)){Header("detail_policy");fprintf(file,",\"list\":%llu,\"operations_selected\":%s}\n",list?list->id:0,details?"true":"false");}
  if(list){auto& w=list->writes;for(UINT i=0;i<w.size;++i){if(!Room())break;Header("write");fprintf(file,",\"list\":%llu,\"resource\":",list->id);Resource(M::Describe(w.resources[i].Get()));fputs("}\n",file);}
   for(UINT i=0;details&&i<w.traceSize;++i){if(!Room())break;auto& op=w.trace[i];Header("operation");fprintf(file,",\"list\":%llu,\"ordinal\":%u,\"method\":%u,\"binding\":%u,\"record_tick\":%llu,\"record_thread\":%u,\"shader\":\"%016llx\",\"destination\":",list->id,op.ordinal,op.method,op.binding,op.tick,op.thread,op.shader);Resource(op.destination);fputs(",\"source\":",file);Resource(op.source);fputs(",\"args\":[",file);for(UINT j=0;j<12;++j)fprintf(file,"%s%u",j?",":"",op.args[j]);fputs("]}\n",file);}
  }fflush(file);
 }
 void End(UINT64 f,bool invalid,const char* scope="present_to_present_commands_and_phase_markers",bool resetExtended=true){if(!selected.load())return;std::lock_guard lock(mutex);if(!selected.load())return;Header("frame_end");fprintf(file,",\"complete\":%s,\"invalid\":%s,\"executions\":%u,\"scope\":\"%s\"}\n",(!truncated&&f==frame)?"true":"false",invalid?"true":"false",executions,scope);fflush(file);selected=false;if(resetExtended)M::extendedTrace=false;}
};
inline Journal journal;
// Opt-in, short-list failure snapshots, not a whole-frame capture. Call on the
// serialized immediate path. Existing frozen metadata provides the first snapshot;
// a one-second / three-frame lease supplies richer following recordings.
struct ConflictCapture {
 Journal output;UINT64 first=~0ULL,last=~0ULL;ULONGLONG deadline=0;UINT captures=0;
 void Tick(UINT64 f){if(first!=~0ULL&&(f>first+3||GetTickCount64()>=deadline||captures>=3))M::conflictTraceUntil=0;}
 void Capture(const std::filesystem::path& root,UINT64 f,ID3D11DeviceContext* c,const M::List* list,UINT decision,BOOL restore,const void* caller){
  if(!BuildProfile::ResearchCaptureRequested(root))return;
  Tick(f);if(!list||decision!=2||!list->writes.operations||list->writes.operations>1024||list->writes.draws>8||list->writes.dispatches>4||captures>=3||f==last)return;
  if(first==~0ULL){first=f;deadline=GetTickCount64()+1000;output.file=_wfsopen((root/L"D18CommandConflicts.jsonl").c_str(),L"wb",_SH_DENYNO);M::conflictTraceUntil=deadline;}
  if(!output.file||f>first+3||GetTickCount64()>=deadline)return;
  if(output.records>=16384||_ftelli64(output.file)>12LL*1024*1024){M::conflictTraceUntil=0;return;}
  output.recordLimit=16384;output.byteLimit=12LL*1024*1024;output.frame=f;output.context=c;output.executions=output.frameRecords=output.phaseRecords=output.summaryRecords=0;output.truncated=false;output.selected=true;
  output.Header("frame_begin");fprintf(output.file,",\"schema\":\"d18-command-list-frame-v2\",\"start\":\"conflicting_execute_only\",\"payload_captured\":false,\"capture_index\":%u,\"pipeline_state_capture\":\"bounded_shadow_bindings_not_full_pipeline\"}\n",captures);
  output.Execute(list,decision,restore,caller);output.End(f,true,"single_conflicting_execute",false);last=f;++captures;Tick(f);
 }
};
inline ConflictCapture conflicts;
}
