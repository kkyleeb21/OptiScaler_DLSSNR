#pragma once
namespace DlssNr::Sh0Native {inline constexpr const char Shader[]=R"SH0N(

#ifdef VK_MODE
[[vk::binding(0, 0)]]
cbuffer Params : register(b0, space0)
#else
cbuffer Params : register(b0)
#endif
{
    uint  gMode;
    float gWhitePoint;
    uint  gWidth;
    uint  gHeight;
    float gTransferStrength;
    float gColourStrength;
    uint  gDebugView;
    float gMaxRatio;
    uint  gPassthrough;
    float gMvScaleX;     // motion vector units -> pixels of this dispatch
    float gMvScaleY;
    uint  gGuideWidth;   // the motion texture's valid region
    uint  gGuideHeight;
    uint  gCompareMode;  // 0 off, 1 side by side, 2 wipe
    float gCompareSplit; // where the wipe cuts, 0..1
    float gCompareZoom;  // side by side: 1 fits the frame, 2 fills the half
    uint  gCompareSwap;  // put the edited frame on the other side
    uint  gTransfer;     // 0 classic, 1 matched residual -- how a below-size model comes back
    float gDebugScale;   // what the debug views are scaled by, held still while the meter moves
    uint  gPreserveHighFrequency;
    float gNetworkRatioX;
    float gNetworkRatioY;
    uint  gMotionAdaptive;
    float gMotionStart;
    float gMotionEnd;
    float gMismatchStart;
    float gMismatchEnd;
    float gFrequencyRadius;
    float gLumaTrust;
    float gChromaTrust;
    uint  gSourceWidth;
    uint  gSourceHeight;
    uint gGuidedReconstruction;
    float gPostSharpness;
    uint gCatmullRomInput;
    uint gExperimentalCompose;
    uint gValidX; uint gValidY; uint gValidWidth; uint gValidHeight;
    uint gMotionX; uint gMotionY;
#if defined(VK_MODE) || defined(DX12_HIGHLIGHT_ENCODING)
    uint gHighlightEncoding;
#ifdef D18_HIGHRES
    uint gHighResolution; // DX12 uses the otherwise Vulkan-only RelativeColour slot.
#endif
#endif
#ifdef VK_MODE
#ifndef D18_HIGHRES
    uint gRelativeColour;
#else
#define gRelativeColour 0
#endif
#endif
};
#ifdef VK_MODE
[[vk::binding(1,0)]] Texture2D<float4> gSource;
[[vk::binding(2,0)]] Texture2D<float4> gModel;
[[vk::binding(3,0)]] Texture2D<float4> gOriginal;
[[vk::binding(5,0)]] RWTexture2D<float4> gTarget;
[[vk::binding(6,0)]] RWTexture2D<float4> gKeep;
#else
Texture2D<float4> gSource:register(t0),gModel:register(t1),gOriginal:register(t2);
RWTexture2D<float4> gTarget:register(u0),gKeep:register(u1);
#endif
static const float3 Luma=float3(.2126,.7152,.0722);
static const float W08[7]={.00044074336693235636,.0219103141713648,.22831071645846546,.49867645200647487,.22831071645846546,.0219103141713648,.00044074336693235636};
static const float W20[17]={.00006691628957263553,.0004363490205067883,.002215963172596555,.00876430436278587,.026995957967298846,.06475993660472744,.12098748976534904,.17603575888479037,.199474647864745,.17603575888479037,.12098748976534904,.06475993660472744,.026995957967298846,.00876430436278587,.002215963172596555,.0004363490205067883,.00006691628957263553};
int2 fullPixelBound(int2 p){return clamp(p,int2(gValidX,gValidY),int2(gValidX+gValidWidth-1,gValidY+gValidHeight-1));}
void Horizontal(uint3 id:SV_DispatchThreadID){
 if(id.x>=gWidth||id.y>=gHeight)return;
 float3 total=0;
 [unroll]for(int i=-8;i<=8;++i){
  float y=max(dot(gSource.Load(int3(fullPixelBound(int2(id.xy)+int2(i,0)),0)).rgb,Luma)/gWhitePoint,0);
  float l=log(max(y,gMaxRatio));
  total.y+=W20[i+8]*l;total.z+=W20[i+8]*y;
  if(i>=-3&&i<=3)total.x+=W08[i+3]*l;
 }
 gTarget[id.xy]=float4(total,0);
}
void Vertical(uint3 id:SV_DispatchThreadID){
 if(id.x>=gWidth||id.y>=gHeight)return;
 float3 total=0;
 [unroll]for(int i=-8;i<=8;++i){
  float3 v=gModel.Load(int3(fullPixelBound(int2(id.xy)+int2(0,i)),0)).rgb;
  total.yz+=W20[i+8]*v.yz;
  if(i>=-3&&i<=3)total.x+=W08[i+3]*v.x;
 }
 gTarget[id.xy]=float4(total,0);
}
void Apply(uint3 id:SV_DispatchThreadID){
 if(id.x>=gWidth||id.y>=gHeight)return;
 float4 original=gSource.Load(int3(id.xy,0));
 float gain=1,raw=0,fade=0,logGain=0;
 bool valid=id.x>=gValidX&&id.y>=gValidY&&id.x<gValidX+gValidWidth&&id.y<gValidY+gValidHeight;
 // This branch deliberately does no multiply, exp, log, guard, or colour clamp.
 if(valid&&(gTransferStrength!=0||gColourStrength!=0)){
  float y=max(dot(original.rgb,Luma)/gWhitePoint,0);
  if(y>gMaxRatio){
   float3 blurred=gModel.Load(int3(id.xy,0)).rgb;
   fade=saturate((blurred.z-gMvScaleX)/(gMvScaleY-gMvScaleX));
   float l=log(max(y,gMaxRatio));
   float midBand=gTransfer==0?l-blurred.y:blurred.x-blurred.y;
   raw=fade*(gTransferStrength*midBand+gColourStrength*(l-blurred.x));
   if(raw!=0){
    float high=max(max(original.r,original.g),original.b)/gWhitePoint;
    float delta=raw>0?raw*saturate((gCompareZoom-high)/(gCompareZoom-gCompareSplit)):raw;
    gain=clamp(exp(delta),.5,2.0);
    gain=min(gain,max(1.0,gCompareZoom/max(high,gMaxRatio)));
    logGain=log(gain);
   }
  }
 }
 gTarget[id.xy]=gain==1?original:float4(original.rgb*gain,original.a);
 if(gDebugView!=0 && (id.x%64)==0 && (id.y%64)==0)
  gKeep[id.xy/64]=float4(raw,fade,logGain,gain);
}
static const float W1[9]={.00013383062461474175,.0044318616200312655,.05399112742070441,.24197144565660073,.39894346935609776,.24197144565660073,.05399112742070441,.0044318616200312655,.00013383062461474175};
int2 halfPixelBound(int2 p){return clamp(p,int2(gValidX,gValidY),int2(gValidX+gValidWidth-1,gValidY+gValidHeight-1));}
int2 halfBound(int2 p){return clamp(p,int2(0,0),int2((gWidth+1)/2-1,(gHeight+1)/2-1));}
void Downsample(uint3 id:SV_DispatchThreadID){
 if(id.x>=(gWidth+1)/2||id.y>=(gHeight+1)/2)return;
 float2 total=0;
 [unroll]for(int y=0;y<2;y++)[unroll]for(int x=0;x<2;x++){
  int2 p=min(int2(id.xy)*2+int2(x,y),int2(gWidth-1,gHeight-1));
  float Y=max(dot(gSource.Load(int3(halfPixelBound(p),0)).rgb,Luma)/gWhitePoint,0);
  total+=float2(log(max(Y,gMaxRatio)),Y)*.25;
 }
 gTarget[id.xy]=float4(total,0,0);
}
void HalfHorizontal(uint3 id:SV_DispatchThreadID){
 if(id.x>=(gWidth+1)/2||id.y>=(gHeight+1)/2)return;
 float2 total=0;[unroll]for(int i=-4;i<=4;i++)total+=W1[i+4]*gModel.Load(int3(halfBound(int2(id.xy)+int2(i,0)),0)).xy;
 gTarget[id.xy]=float4(total,0,0);
}
void HalfVertical(uint3 id:SV_DispatchThreadID){
 if(id.x>=(gWidth+1)/2||id.y>=(gHeight+1)/2)return;
 float2 total=0;[unroll]for(int i=-4;i<=4;i++)total+=W1[i+4]*gModel.Load(int3(halfBound(int2(id.xy)+int2(0,i)),0)).xy;
 gTarget[id.xy]=float4(total,0,0);
}
void FineHorizontal(uint3 id:SV_DispatchThreadID){
 if(id.x>=gWidth||id.y>=gHeight)return;
 float total=0;[unroll]for(int i=-3;i<=3;i++){
  float Y=max(dot(gSource.Load(int3(halfPixelBound(int2(id.xy)+int2(i,0)),0)).rgb,Luma)/gWhitePoint,0);
  total+=W08[i+3]*log(max(Y,gMaxRatio));
 }
 gTarget[id.xy]=float4(total,0,0,0);
}
float2 coarseAt(int2 p){
 float2 position=(float2(p)-.5)*.5;int2 base=int2(floor(position));float2 f=frac(position);
 float2 a=gOriginal.Load(int3(halfBound(base),0)).xy,b=gOriginal.Load(int3(halfBound(base+int2(1,0)),0)).xy;
 float2 c=gOriginal.Load(int3(halfBound(base+int2(0,1)),0)).xy,d=gOriginal.Load(int3(halfBound(base+int2(1,1)),0)).xy;
 return lerp(lerp(a,b,f.x),lerp(c,d,f.x),f.y);
}
void ApplyHalf(uint3 id:SV_DispatchThreadID){
 if(id.x>=gWidth||id.y>=gHeight)return;
 float4 original=gSource.Load(int3(id.xy,0));float gain=1,raw=0,fade=0,logGain=0;
 bool valid=id.x>=gValidX&&id.y>=gValidY&&id.x<gValidX+gValidWidth&&id.y<gValidY+gValidHeight;
 if(valid&&(gTransferStrength!=0||gColourStrength!=0)){
  float y=max(dot(original.rgb,Luma)/gWhitePoint,0);
  if(y>gMaxRatio){
   float fineLog=0;[unroll]for(int i=-3;i<=3;i++)fineLog+=W08[i+3]*gModel.Load(int3(halfPixelBound(int2(id.xy)+int2(0,i)),0)).x;
   float2 coarse=coarseAt(int2(id.xy));fade=saturate((coarse.y-gMvScaleX)/(gMvScaleY-gMvScaleX));
   float l=log(max(y,gMaxRatio));float midBand=gTransfer==0?l-coarse.x:fineLog-coarse.x;
   raw=fade*(gTransferStrength*midBand+gColourStrength*(l-fineLog));
   if(raw!=0){
    float high=max(max(original.r,original.g),original.b)/gWhitePoint;
    float delta=raw>0?raw*saturate((gCompareZoom-high)/(gCompareZoom-gCompareSplit)):raw;
    gain=clamp(exp(delta),.5,2.0);gain=min(gain,max(1.0,gCompareZoom/max(high,gMaxRatio)));logGain=log(gain);
   }
  }
 }
 gTarget[id.xy]=gain==1?original:float4(original.rgb*gain,original.a);
 if(gDebugView!=0&&(id.x%64)==0&&(id.y%64)==0)gKeep[id.xy/64]=float4(raw,fade,logGain,gain);
}

[numthreads(8,8,1)] void CSMain(uint3 id:SV_DispatchThreadID){
 if(gMode==20) Horizontal(id);else if(gMode==21) Vertical(id);else if(gMode==22) Apply(id);
 else if(gMode==23) Downsample(id);else if(gMode==24) HalfHorizontal(id);else if(gMode==25) HalfVertical(id);
 else if(gMode==26) FineHorizontal(id);else if(gMode==27) ApplyHalf(id);
 else if(gMode==28 && id.x<gWidth && id.y<gHeight)gTarget[id.xy]=gSource.Load(int3(id.xy,0));
}

)SH0N";}
