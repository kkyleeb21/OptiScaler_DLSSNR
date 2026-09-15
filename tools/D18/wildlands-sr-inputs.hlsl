// Profile 34a304d07b55d993: preserve raw jittered colour; build unjittered backward MV.
Texture2D<float4> Scene : register(t0);
Texture2D<float> Depth : register(t1);
Texture2D<float2> Motion : register(t2);
cbuffer TemporalParams : register(b0) { float4 T[20]; }
RWTexture2D<float4> ColourOut : register(u0);
RWTexture2D<float> DepthOut : register(u1);
RWTexture2D<float2> MotionOut : register(u2);
[numthreads(8,8,1)]
void main(uint3 p:SV_DispatchThreadID) {
 uint w,h;ColourOut.GetDimensions(w,h);if(p.x>=w||p.y>=h)return;
 float2 uv=(float2(p.xy)+.5+T[12].xy)/float2(w,h);
 float4 c=Scene.Load(int3(p.xy,0));float z=Depth.Load(int3(p.xy,0));
 // R32 depth may contain sky/invalid values. Keep the DLSS input finite in [0,1].
 z=isfinite(z)?saturate(z):0;
 float2 mv=0;
 if(z>=.005&&c.a>0) mv=.5*Motion.Load(int3(p.xy,0))+T[12].zw;
 else {
  float4 clip=float4(uv*float2(2,-2)+float2(-1,1),z,1);
  float hw=dot(clip,T[16]);
  if(abs(hw)>1e-8){float2 hxy=float2(dot(clip,T[13]),dot(clip,T[14]))/hw;mv=hxy*float2(.5,-.5)+.5-uv;}
 }
 if(!all(isfinite(mv)))mv=0;
 // Bound AFTER conversion to pixel units, BEFORE the FP16 store. A finite
 // normalized vector can overflow half precision when multiplied by dimensions.
 // Beyond a full viewport the history sample is outside the image either way.
 float2 pixelMotion=mv*float2(w,h);
 pixelMotion=all(isfinite(pixelMotion))?clamp(pixelMotion,-float2(w,h),float2(w,h)):0;
 ColourOut[p.xy]=float4(c.rgb,1);DepthOut[p.xy]=z;MotionOut[p.xy]=pixelMotion;
}
