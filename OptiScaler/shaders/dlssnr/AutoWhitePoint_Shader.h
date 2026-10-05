#pragma once
namespace DlssNr::AutoWhitePoint {
inline constexpr char ShaderSource[]=R"hlsl(
Texture2D<float4> Input : register(t0);
RWStructuredBuffer<float> Cells : register(u0);
cbuffer Rect : register(b0) { uint originX, originY, width, height; };
// One thread per cell; 4x4 stratified samples per cell (65536 loads total).
[numthreads(8,8,1)]
void Measure(uint3 id : SV_DispatchThreadID) {
    if(any(id.xy>=64)) return;
    float sum=0;
    [unroll] for(uint y=0;y<4;++y) [unroll] for(uint x=0;x<4;++x) {
        uint2 p=uint2(originX,originY)+min(uint2(width-1,height-1),
            uint2((id.x*8+2*x+1)*(float)width/512.0,
                  (id.y*8+2*y+1)*(float)height/512.0));
        float3 rgb=Input.Load(int3(p,0)).rgb;
        float luma=dot(rgb,float3(.2126,.7152,.0722));
        // Preserve invalid cells for CPU finite filtering; never silently turn NaN into black.
        if(!all(isfinite(rgb)) || !isfinite(luma)) { Cells[id.y*64+id.x]=asfloat(0x7fc00000); return; }
        sum+=log2(max(luma,1e-6));
    }
    Cells[id.y*64+id.x]=sum/16.0;
}
)hlsl";
}
