#pragma once
#include <array>
#include <cstdint>
namespace DlssNr::V8 {
enum Resource:int {DownX=0,LowA,LowB,FullP,Fields,TempP,TempFields,Count,SourceOriginal=-1,ProxyInput=-2,ModelOutput=-3,Destination=-4,Absent=-5};
struct Pass {unsigned mode;int source,model,original,coeff,target,keep;unsigned width,height;};
inline std::array<Pass,9> Passes(unsigned w,unsigned h){return {{
 {40,Absent,Absent,SourceOriginal,Absent,DownX,Absent,w/2,h},
 {41,DownX,ModelOutput,Absent,Absent,LowA,Absent,w/2,h/2},
 {42,LowA,Absent,Absent,Absent,LowB,Absent,w/2,h/2},
 {43,LowB,Absent,Absent,Absent,LowA,Absent,w/2,h/2},
 {44,LowA,Absent,Absent,Absent,LowB,Absent,w/2,h/2},
 {45,LowB,Absent,Absent,Absent,LowA,Absent,w/2,h/2},
 {46,ProxyInput,ModelOutput,SourceOriginal,LowA,FullP,Fields,w,h},
 {47,FullP,Fields,Absent,Absent,TempP,TempFields,w,h},
 {48,TempP,TempFields,Absent,Absent,Destination,Absent,w,h}
 }};}
inline unsigned ScratchWidth(int i,unsigned w){return i<=LowB?w/2:w;}
inline unsigned ScratchHeight(int i,unsigned h){return i==LowA||i==LowB?h/2:h;}
}
