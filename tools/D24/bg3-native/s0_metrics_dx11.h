#pragma once
#include "s0_metrics_core.h"

// Included after the addon's logging and Windows/D3D declarations. Query objects
// live in the existing process-lifetime Session; pending/poisoned queries are
// never released or reused on timeout, cancellation, reset, or feature release.
struct S0MetricsDx11 {
 struct Queries {ComPtr<ID3D11Query> disjoint;std::array<ComPtr<ID3D11Query>,6> ticks;};
 std::array<Queries,D18S0Metrics::Slots> queries;
 D18S0Metrics::Control state;
 uint64_t creation=0,lastMarkerPoll=0;DWORD pid=0;
 bool identityChecked=false,marker=false;
 int current=-1;unsigned controlRecords=0,timingRecords=0,lifecycleRecords=0;
 void identity(){if(identityChecked)return;identityChecked=true;pid=GetCurrentProcessId();
  FILETIME c{},e{},k{},u{};if(GetProcessTimes(GetCurrentProcess(),&c,&e,&k,&u))creation=(uint64_t(c.dwHighDateTime)<<32)|c.dwLowDateTime;}
 void control(const char* phase,const char* reason,const D18S0Metrics::Control& c){
  if(!logFile||controlRecords++>=128)return;
  logPrint(logFile,"{\"event\":\"s0_metrics_control\",\"schema\":1,\"api\":\"DX11\",\"pid\":%lu,\"creation\":\"%llu\",\"identity_valid\":%u,\"tick\":%llu,\"arm\":%u,\"phase\":\"%s\",\"reason\":\"%s\",\"saved\":%u,\"target\":32,\"total_saved\":%u,\"warmup\":%u,\"attempts\":%u,\"pending\":%u,\"full_skips\":%u,\"ineligible_skips\":%u}\n",
   pid,creation,unsigned(creation!=0),GetTickCount64(),c.arms,phase,reason,c.saved,c.total,c.warmup,c.attempts,c.pending(),c.fullSkips,c.ineligibleSkips);fflush(logFile);
 }
 void timing(const D18S0Metrics::Slot& s,const D18S0Metrics::Result& r,const char* failure){
  if(!logFile||timingRecords++>=1028)return;
  logPrint(logFile,"{\"event\":\"s0_gpu_timing\",\"schema\":1,\"api\":\"DX11\",\"pid\":%lu,\"creation\":\"%llu\",\"identity_valid\":%u,\"tick\":%llu,\"arm\":%u,\"sample\":%u,\"frame\":%llu,\"generation\":%llu,\"feature\":\"%p\",\"context\":\"%p\",\"queue\":\"unknown\",\"phf\":%u,\"width\":%u,\"height\":%u,\"prefilter\":%u,\"phase\":\"%s\",\"reason\":\"%s\",\"reset\":0,\"capture_active\":0,\"menu_visible\":\"unknown\",\"menu_observation\":\"not_available_in_addon\"",
   pid,creation,unsigned(creation!=0),GetTickCount64(),s.arm,s.sample,s.frame.frame,s.frame.generation,reinterpret_cast<void*>(s.frame.feature),reinterpret_cast<void*>(s.frame.context),s.frame.phf,s.frame.width,s.frame.height,s.frame.prefilter,failure?"failed":"complete",failure?failure:"query_ready");
  if(!failure)logPrint(logFile,",\"domain\":\"gpu_ticks\",\"ticks0\":%llu,\"ticks1\":%llu,\"ticks2\":%llu,\"ticks3\":%llu,\"ticks4\":%llu,\"ticks5\":%llu,\"frequency_hz\":%llu,\"timestamp_bits\":64,\"disjoint\":0,\"getdata_mask\":127,\"existing_completion\":1,\"completion\":\"dx11_disjoint_timestamps_and_existing_completion\"",r.ticks[0],r.ticks[1],r.ticks[2],r.ticks[3],r.ticks[4],r.ticks[5],r.frequency);
  logPrint(logFile,"}\n");fflush(logFile);
 }
 void lifecycle(bool enabled,const char* action,const char* reason,uint64_t frame,unsigned generation,void* feature,unsigned width,unsigned height,int retired,const char* scope){
  if(!enabled||!logFile||lifecycleRecords>=128)return;identity();++lifecycleRecords;
  logPrint(logFile,"{\"event\":\"s0_lifecycle\",\"schema\":1,\"api\":\"DX11\",\"pid\":%lu,\"creation\":\"%llu\",\"identity_valid\":%u,\"tick\":%llu,\"action\":\"%s\",\"reason\":\"%s\",\"frame\":%llu,\"generation\":%u,\"feature\":\"%p\",\"width\":%u,\"height\":%u,\"gpu_retired\":%s,\"completion_scope\":\"%s\"}\n",pid,creation,unsigned(creation!=0),GetTickCount64(),action,reason,frame,generation,feature,width,height,retired<0?"null":retired?"1":"0",scope);fflush(logFile);
 }
 void entry(bool enabled,const std::filesystem::path& root,ID3D11DeviceContext* context,uint64_t now){
  // When fully off this performs no file, identity, or GPU query work.
  if(!enabled&&!state.active&&!state.pending()&&!marker)return;
  identity();
  if(!lastMarkerPoll||now-lastMarkerPoll>=100){lastMarkerPoll=now;
   const auto path=root/(L"D18_S0_METRICS_"+std::to_wstring(pid)+L"_"+std::to_wstring(creation)+L".on");
   const auto a=enabled&&creation?GetFileAttributesW(path.c_str()):INVALID_FILE_ATTRIBUTES;
   marker=a!=INVALID_FILE_ATTRIBUTES&&!(a&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT));
  }
  state.observe(enabled,marker,now,*this);
  poll(context);
 }
 template<class Context> void poll(Context* context){
  if(!context||state.fatal)return;
  for(unsigned i=0;i<D18S0Metrics::Slots;++i){auto& s=state.slots[i];if(s.state!=D18S0Metrics::State::Pending)continue;
   if(reinterpret_cast<uint64_t>(context)!=s.frame.context){state.poison(i,"context_changed_before_readback",*this);return;}
   D18S0Metrics::Result r{};r.existingCompletion=s.existingCompletion;bool pending=false;
   D3D11_QUERY_DATA_TIMESTAMP_DISJOINT d{};
   const auto hr=context->GetData(queries[i].disjoint.Get(),&d,sizeof(d),D3D11_ASYNC_GETDATA_DONOTFLUSH);
   if(FAILED(hr)){state.poison(i,"disjoint_getdata_failed",*this);return;}
   if(hr==S_OK){r.mask|=64;r.frequency=d.Frequency;r.disjoint=d.Disjoint!=FALSE;}else pending=true;
   for(unsigned j=0;j<6;++j){const auto h=context->GetData(queries[i].ticks[j].Get(),&r.ticks[j],sizeof(uint64_t),D3D11_ASYNC_GETDATA_DONOTFLUSH);
    if(FAILED(h)){state.poison(i,"timestamp_getdata_failed",*this);return;}
    if(h==S_OK)r.mask|=1u<<j;else pending=true;
   }
   if(!pending)state.ready(i,r,*this);
  }
 }
 void begin(ID3D11Device* device,ID3D11DeviceContext* context,bool eligible,const D18S0Metrics::Frame& frame){
  current=state.acquire(eligible&&device&&context&&creation,GetTickCount64(),frame,*this);if(current<0)return;
  auto& q=queries[current];
  if(!q.disjoint){D3D11_QUERY_DESC d{D3D11_QUERY_TIMESTAMP_DISJOINT,0};
   if(FAILED(device->CreateQuery(&d,&q.disjoint))){state.poison(current,"query_allocation_failed",*this);current=-1;return;}
   d.Query=D3D11_QUERY_TIMESTAMP;
   for(auto& t:q.ticks)if(FAILED(device->CreateQuery(&d,&t))){state.poison(current,"query_allocation_failed",*this);current=-1;return;}
  }
  context->Begin(q.disjoint.Get());stamp(context,0);
 }
 void stamp(ID3D11DeviceContext* context,unsigned index){if(current<0)return;context->End(queries[current].ticks[index].Get());state.slots[current].written|=1u<<index;}
 void phf(unsigned value){if(current>=0)state.slots[current].frame.phf=value;}
 void finish(ID3D11DeviceContext* context){if(current<0)return;stamp(context,5);context->End(queries[current].disjoint.Get());state.slots[current].state=D18S0Metrics::State::Pending;}
 void completed(bool okay){if(current<0)return;state.slots[current].existingCompletion=okay;
  if(!okay)state.poison(current,"existing_completion_failed",*this);current=-1;}
 void leave(){if(current<0)return;
  // An early return or SEH did not reach the final End. Do not inject commands
  // into a failed path, release/recycle its queries, or claim retirement.
  state.poison(current,"incomplete_frame_no_query_reuse",*this);current=-1;
 }
};
