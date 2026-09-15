#pragma once
#include "BuildProfile.h"
namespace DlssNr::NativeSrProfile {
struct Modes { bool stagePanel,postReplay,upscale,evaluateOnly,nativeHandoff; };
constexpr Modes Resolve(bool enabled,bool panel,bool post,bool upscale,bool evaluate,bool handoff){
 if(!BuildProfile::Diagnostic)return {false,enabled,enabled,enabled,enabled};
 if(post)upscale=true;
 handoff=post&&handoff;
 if(upscale){panel=false;evaluate=true;}
 return {panel,post,upscale,evaluate,handoff};
}
}
