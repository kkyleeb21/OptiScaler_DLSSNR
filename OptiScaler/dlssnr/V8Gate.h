#pragma once
#include <shaders/dlssnr/DlssNr_Common.h>
#include <cmath>
namespace DlssNr::V8 {
struct Selection {int mode;const char* reason;};
inline Selection Select(int requested,const DlssNrConstants& c,bool ordinary,bool linearInput,bool linearOutput,bool prefilter,unsigned targetFormat){
 if(requested==0)return {0,"default"};
 if(requested!=1&&requested!=2)return {0,"invalid_mode"};
 if(c.NetworkRatioX==1.f&&c.NetworkRatioY==1.f)return {0,"ratio100_identity"};
 if(!ordinary)return {0,"requires_standard_single_pass"};
 if(c.Width!=3840||c.Height!=2160||c.NetworkRatioX!=.5f||c.NetworkRatioY!=.5f)return {0,"requires_4k_ratio50"};
 if(!linearInput||linearOutput||prefilter)return {0,"requires_linear_input_point_output_no_prefilter"};
 if(targetFormat!=97)return {0,"requires_fp16_output"};
 if(!std::isfinite(c.WhitePoint)||c.WhitePoint<=0)return {0,"invalid_whitepoint"};
 if(c.Mode!=1||c.Passthrough||c.Transfer!=1||c.TransferStrength!=1||c.ColourStrength!=1||c.MaxRatio!=2||c.DebugView||c.CompareMode||c.HighlightEncoding||c.RelativeColour||c.ExperimentalCompose||c.GuidedReconstruction||c.MotionAdaptive||c.PostSharpness)return {0,"unsupported_compose_controls"};
 return {requested,requested==1?"r0_reference":"v8_fixed"};
}
}
