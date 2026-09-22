#pragma once
namespace DlssNr::Sh0 {
inline constexpr const char HalfShaderSource[]=R"SH0(
cbuffer Parameters:register(b0){
 uint Width,Height,Mode,Debug;
 float Mid,Fine,White,Epsilon;
 float DarkLo,DarkHi,HighLo,HighHi;
 uint OriginX,OriginY,ValidWidth,ValidHeight;
};
Texture2D<float4> Colour:register(t0);
Texture2D<float4> Filtered:register(t1);
Texture2D<float4> Coarse:register(t2);
RWTexture2D<float4> Target:register(u0);
RWTexture2D<float4> Diagnostic:register(u1);
static const float3 Luma=float3(.2126,.7152,.0722);
static const float W08[7]={.00044074336693235636,.0219103141713648,.22831071645846546,.49867645200647487,.22831071645846546,.0219103141713648,.00044074336693235636};
static const float W1[9]={.00013383062461474175,.0044318616200312655,.05399112742070441,.24197144565660073,.39894346935609776,.24197144565660073,.05399112742070441,.0044318616200312655,.00013383062461474175};
int2 bounded(int2 p){return clamp(p,int2(OriginX,OriginY),int2(OriginX+ValidWidth-1,OriginY+ValidHeight-1));}
int2 halfBound(int2 p){return clamp(p,int2(0,0),int2((Width+1)/2-1,(Height+1)/2-1));}
[numthreads(8,8,1)]void Downsample(uint3 id:SV_DispatchThreadID){
 if(id.x>=(Width+1)/2||id.y>=(Height+1)/2)return;
 float2 total=0;
 [unroll]for(int y=0;y<2;y++)[unroll]for(int x=0;x<2;x++){
  int2 p=min(int2(id.xy)*2+int2(x,y),int2(Width-1,Height-1));
  float Y=max(dot(Colour.Load(int3(bounded(p),0)).rgb,Luma)/White,0);
  total+=float2(log(max(Y,Epsilon)),Y)*.25;
 }
 Target[id.xy]=float4(total,0,0);
}
[numthreads(8,8,1)]void HalfHorizontal(uint3 id:SV_DispatchThreadID){
 if(id.x>=(Width+1)/2||id.y>=(Height+1)/2)return;
 float2 total=0;[unroll]for(int i=-4;i<=4;i++)total+=W1[i+4]*Filtered.Load(int3(halfBound(int2(id.xy)+int2(i,0)),0)).xy;
 Target[id.xy]=float4(total,0,0);
}
[numthreads(8,8,1)]void HalfVertical(uint3 id:SV_DispatchThreadID){
 if(id.x>=(Width+1)/2||id.y>=(Height+1)/2)return;
 float2 total=0;[unroll]for(int i=-4;i<=4;i++)total+=W1[i+4]*Filtered.Load(int3(halfBound(int2(id.xy)+int2(0,i)),0)).xy;
 Target[id.xy]=float4(total,0,0);
}
[numthreads(8,8,1)]void FineHorizontal(uint3 id:SV_DispatchThreadID){
 if(id.x>=Width||id.y>=Height)return;
 float total=0;[unroll]for(int i=-3;i<=3;i++){
  float Y=max(dot(Colour.Load(int3(bounded(int2(id.xy)+int2(i,0)),0)).rgb,Luma)/White,0);
  total+=W08[i+3]*log(max(Y,Epsilon));
 }
 Target[id.xy]=float4(total,0,0,0);
}
float2 coarseAt(int2 p){
 float2 position=(float2(p)-.5)*.5;int2 base=int2(floor(position));float2 f=frac(position);
 float2 a=Coarse.Load(int3(halfBound(base),0)).xy,b=Coarse.Load(int3(halfBound(base+int2(1,0)),0)).xy;
 float2 c=Coarse.Load(int3(halfBound(base+int2(0,1)),0)).xy,d=Coarse.Load(int3(halfBound(base+int2(1,1)),0)).xy;
 return lerp(lerp(a,b,f.x),lerp(c,d,f.x),f.y);
}
[numthreads(8,8,1)]void ApplyHalf(uint3 id:SV_DispatchThreadID){
 if(id.x>=Width||id.y>=Height)return;
 float4 original=Colour.Load(int3(id.xy,0));float gain=1,raw=0,fade=0,logGain=0;
 bool valid=id.x>=OriginX&&id.y>=OriginY&&id.x<OriginX+ValidWidth&&id.y<OriginY+ValidHeight;
 if(valid&&(Mid!=0||Fine!=0)){
  float y=max(dot(original.rgb,Luma)/White,0);
  if(y>Epsilon){
   float fineLog=0;[unroll]for(int i=-3;i<=3;i++)fineLog+=W08[i+3]*Filtered.Load(int3(bounded(int2(id.xy)+int2(0,i)),0)).x;
   float2 coarse=coarseAt(int2(id.xy));fade=saturate((coarse.y-DarkLo)/(DarkHi-DarkLo));
   float l=log(max(y,Epsilon));float midBand=Mode==0?l-coarse.x:fineLog-coarse.x;
   raw=fade*(Mid*midBand+Fine*(l-fineLog));
   if(raw!=0){
    float high=max(max(original.r,original.g),original.b)/White;
    float delta=raw>0?raw*saturate((HighHi-high)/(HighHi-HighLo)):raw;
    gain=clamp(exp(delta),.5,2.0);gain=min(gain,max(1.0,HighHi/max(high,Epsilon)));logGain=log(gain);
   }
  }
 }
 Target[id.xy]=gain==1?original:float4(original.rgb*gain,original.a);
 if(Debug!=0&&(id.x%64)==0&&(id.y%64)==0)Diagnostic[id.xy/64]=float4(raw,fade,logGain,gain);
}
)SH0";
}
