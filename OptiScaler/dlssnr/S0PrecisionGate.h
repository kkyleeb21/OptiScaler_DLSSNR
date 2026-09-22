#pragma once
#include "r0_gate.h"
#include <map>
#include <cstdint>

// Production selection/cache policy shared with the CPU-only host. Numeric Vk
// formats are explicit contract values; this helper issues no Vulkan commands.
namespace S0Precision {
enum class Variant : unsigned { Original=0, Manual=1 };
inline constexpr const char* OriginalSha="bdf9807b9dc30d09ebd1585e729afb92b6dc8755a435c72fd45903a67e53d276";
inline constexpr const char* ManualSha="48f0c45ee9e02ccf615f470271e0aba675383cfb9f82173e12ed8ef142afc0d1";
inline const char* Name(Variant v){return v==Variant::Manual?"manual_fp32_phf_taps":"original";}
inline const char* Sha(Variant v){return v==Variant::Manual?ManualSha:OriginalSha;}
struct Request {
 bool requested=false,r0Selected=false;
 uint32_t sourceFormat=0,modelFormat=0,sourceWidth=0,sourceHeight=0,modelWidth=0,modelHeight=0;
};
struct Selection {bool requested=false;Variant variant=Variant::Original;const char* reason="research_off";};
inline Selection Select(const DlssNrConstants& c,const Request& r,bool advanced,uint32_t target,uint32_t keep){
 auto no=[&](const char* why){return Selection{r.requested,Variant::Original,why};};
 if(!r.requested)return no("research_off");
 if(!r.r0Selected)return no("R0_not_selected");
 auto probe=c;auto ordinary=SelectS0VkR0(probe,S0VkMode::OldR0,c.NetworkRatioX,c.NetworkRatioY,advanced);
 if(!ordinary.selected)return no(ordinary.reason);
 if(c.NetworkRatioX!=0.5f||c.NetworkRatioY!=0.5f)return no("ratio_not_qualified");
 if(c.PreserveHighFrequency!=1)return no("actual_PHF_not_one");
 if(r.sourceFormat!=97||r.modelFormat!=97)return no("sample_format_not_97");
 if(target!=97&&target!=109&&target!=122)return no("output_format_not_qualified");
 if(keep!=97)return no("keep_format_not_qualified");
 if(!c.Width||!c.Height||r.sourceWidth!=c.Width||r.sourceHeight!=c.Height||r.modelWidth!=c.Width||r.modelHeight!=c.Height)
  return no("physical_texture_relation_not_qualified");
 return {true,Variant::Manual,"qualified_manual"};
}
inline uint64_t Key(Variant v,uint32_t target,uint32_t keep){
 // Qualified Vulkan formats are positive 31-bit values. Highest bit records the
 // shader variant; original keys remain exactly equal to the prior format key.
 return (uint64_t(v==Variant::Manual)<<63)|(uint64_t(target)<<32)|keep;
}
template<class Pipeline> struct Result {
 Pipeline pipeline{};Variant variant=Variant::Original;uint64_t key=0;
 bool requested=false,ready=false;const char* reason="pipeline_unavailable";
};
template<class Pipeline,class Create> Result<Pipeline> Resolve(std::map<uint64_t,Pipeline>& cache,
 bool& manualDisabled,Selection selection,uint32_t target,uint32_t keep,Create&& create){
 Result<Pipeline> out;out.requested=selection.requested;out.variant=selection.variant;out.reason=selection.reason;
 auto get=[&](Variant variant){const auto key=Key(variant,target,keep);auto it=cache.find(key);
  if(it!=cache.end())return it->second;
  Pipeline pipeline=create(variant,target,keep);if(pipeline)cache.emplace(key,pipeline);return pipeline;};
 if(out.variant==Variant::Manual){
  if(manualDisabled){out.variant=Variant::Original;out.reason="manual_disabled_original";}
  else {out.pipeline=get(Variant::Manual);if(!out.pipeline){manualDisabled=true;out.variant=Variant::Original;out.reason="manual_creation_failed_original";}}
 }
 if(out.variant==Variant::Original)out.pipeline=get(Variant::Original);
 out.key=Key(out.variant,target,keep);out.ready=bool(out.pipeline);
 if(!out.ready)out.reason="original_pipeline_unavailable";
 return out;
}
// Value copy locked to one successful Dispatch, suitable for immutable per-frame
// timing/capture metadata. Shader SHA is the frozen base SPIR-V before the existing
// deterministic target/keep storage-format specialization.
struct Evidence {
 bool known=false,requested=false;Variant variant=Variant::Original;uint64_t key=0;
 uintptr_t pipeline=0;uint32_t sourceFormat=0,modelFormat=0,targetFormat=0,keepFormat=0;
 float ratioX=0,ratioY=0;
 const char* reason="not_dispatched";
};
}
