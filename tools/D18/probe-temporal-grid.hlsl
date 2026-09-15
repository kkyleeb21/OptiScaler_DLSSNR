// Diagnostic samples only. Reproduce the observed input UV transform, not SR.
Texture2D<float4> Scene : register(t0);
Texture2D<float> Depth : register(t1);
Texture2D<float2> Motion : register(t2);
SamplerState InputSampler : register(s0);
cbuffer ViewParams : register(b1) { float4 View[161]; }
cbuffer TemporalParams : register(b2) { float4 Temporal[19]; }
RWStructuredBuffer<float4> Samples : register(u0);
[numthreads(8,4,1)]
void main(uint3 p : SV_DispatchThreadID) {
    if (p.x >= 64 || p.y >= 36) return;
    float2 uv = (float2(p.xy) + 0.5) / float2(64,36);
    float2 q = Temporal[11].zw * uv - Temporal[12].xy;
    q.x *= View[160].x;
    q *= Temporal[11].xy;
    uint base = (p.y * 64 + p.x) * 3;
    Samples[base] = Scene.SampleLevel(InputSampler, q, 0);
    Samples[base+1] = float4(Depth.SampleLevel(InputSampler,q,0), Motion.SampleLevel(InputSampler,q,0), 0);
    Samples[base+2] = float4(uv,q);
}
