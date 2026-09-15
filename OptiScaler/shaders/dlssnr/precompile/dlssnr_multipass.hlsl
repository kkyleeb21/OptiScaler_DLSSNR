// Matched residual accumulation and shared-history zero-MV preparation.
#define D18_SHADER_LIBRARY 1
#define DX12_HIGHLIGHT_ENCODING 1
#define D18_HIGHRES 1
#define CSMain D18SinglePassMain
#include "dlssnr.hlsl"
#undef CSMain

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if(gMode==9) {if(id.x<gWidth && id.y<gHeight)gTarget[id.xy]=0;return;}
    if (gMode != 7 && gMode != 8) { D18SinglePassMain(id); return; }
    if (id.x >= gWidth || id.y >= gHeight) return;
    if (id.x < gValidX || id.y < gValidY || id.x - gValidX >= gValidWidth || id.y - gValidY >= gValidHeight)
    { gTarget[id.xy] = 0; return; }
    // Source is this pass's actual filtered model input, never a re-encoded approximation.
    float3 p = gSource.Load(int3(id.xy, 0)).rgb;
    float3 m = gModel.Load(int3(id.xy, 0)).rgb;
    if (gPassthrough == 0) { p = SrgbToLinear(p); m = SrgbToLinear(m); }
    float3 delta = SanitizeFinite3(m - p, 0);
    if (gMode == 8) delta += SanitizeFinite3(gResidual.Load(int3(id.xy, 0)).rgb, 0);
    // No strength, colour, clipping or composition here: apply these once at final resolve.
    gTarget[id.xy] = float4(SanitizeFinite3(delta, 0), 0);
}
