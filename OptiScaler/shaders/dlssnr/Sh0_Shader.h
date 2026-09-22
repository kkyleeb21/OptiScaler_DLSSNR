#pragma once
namespace DlssNr::Sh0 {
inline constexpr const char ShaderSource[] = R"SH0(
cbuffer Parameters:register(b0){
 uint Width,Height,Mode,Debug;
 float Mid,Fine,White,Epsilon;
 float DarkLo,DarkHi,HighLo,HighHi;
 uint OriginX,OriginY,ValidWidth,ValidHeight;
};
Texture2D<float4> Colour:register(t0);
Texture2D<float4> Filtered:register(t1);
RWTexture2D<float4> Target:register(u0);
RWTexture2D<float4> Diagnostic:register(u1);
static const float3 Luma=float3(.2126,.7152,.0722);
static const float W08[7]={.00044074336693235636,.0219103141713648,.22831071645846546,.49867645200647487,.22831071645846546,.0219103141713648,.00044074336693235636};
static const float W20[17]={.00006691628957263553,.0004363490205067883,.002215963172596555,.00876430436278587,.026995957967298846,.06475993660472744,.12098748976534904,.17603575888479037,.199474647864745,.17603575888479037,.12098748976534904,.06475993660472744,.026995957967298846,.00876430436278587,.002215963172596555,.0004363490205067883,.00006691628957263553};
int2 bounded(int2 p){return clamp(p,int2(OriginX,OriginY),int2(OriginX+ValidWidth-1,OriginY+ValidHeight-1));}
[numthreads(8,8,1)]void Horizontal(uint3 id:SV_DispatchThreadID){
 if(id.x>=Width||id.y>=Height)return;
 float3 total=0;
 [unroll]for(int i=-8;i<=8;++i){
  float y=max(dot(Colour.Load(int3(bounded(int2(id.xy)+int2(i,0)),0)).rgb,Luma)/White,0);
  float l=log(max(y,Epsilon));
  total.y+=W20[i+8]*l;total.z+=W20[i+8]*y;
  if(i>=-3&&i<=3)total.x+=W08[i+3]*l;
 }
 Target[id.xy]=float4(total,0);
}
[numthreads(8,8,1)]void Vertical(uint3 id:SV_DispatchThreadID){
 if(id.x>=Width||id.y>=Height)return;
 float3 total=0;
 [unroll]for(int i=-8;i<=8;++i){
  float3 v=Filtered.Load(int3(bounded(int2(id.xy)+int2(0,i)),0)).rgb;
  total.yz+=W20[i+8]*v.yz;
  if(i>=-3&&i<=3)total.x+=W08[i+3]*v.x;
 }
 Target[id.xy]=float4(total,0);
}
[numthreads(8,8,1)]void Apply(uint3 id:SV_DispatchThreadID){
 if(id.x>=Width||id.y>=Height)return;
 float4 original=Colour.Load(int3(id.xy,0));
 float gain=1,raw=0,fade=0,logGain=0;
 bool valid=id.x>=OriginX&&id.y>=OriginY&&id.x<OriginX+ValidWidth&&id.y<OriginY+ValidHeight;
 // This branch deliberately does no multiply, exp, log, guard, or colour clamp.
 if(valid&&(Mid!=0||Fine!=0)){
  float y=max(dot(original.rgb,Luma)/White,0);
  if(y>Epsilon){
   float3 blurred=Filtered.Load(int3(id.xy,0)).rgb;
   fade=saturate((blurred.z-DarkLo)/(DarkHi-DarkLo));
   float l=log(max(y,Epsilon));
   float midBand=Mode==0?l-blurred.y:blurred.x-blurred.y;
   raw=fade*(Mid*midBand+Fine*(l-blurred.x));
   if(raw!=0){
    float high=max(max(original.r,original.g),original.b)/White;
    float delta=raw>0?raw*saturate((HighHi-high)/(HighHi-HighLo)):raw;
    gain=clamp(exp(delta),.5,2.0);
    gain=min(gain,max(1.0,HighHi/max(high,Epsilon)));
    logGain=log(gain);
   }
  }
 }
 Target[id.xy]=gain==1?original:float4(original.rgb*gain,original.a);
 if(Debug!=0 && (id.x%64)==0 && (id.y%64)==0)
  Diagnostic[id.xy/64]=float4(raw,fade,logGain,gain);
}
)SH0";
}
