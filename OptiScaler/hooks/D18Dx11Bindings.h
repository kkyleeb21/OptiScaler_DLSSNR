#pragma once
#include <d3d11_1.h>
// Only call after a complete restorable snapshot has been captured.
// Null UAV bindings preserve resource counters (no initial-count reset).
namespace D18Dx11Bindings {
inline void Reset(ID3D11DeviceContext* c,UINT uavSlots){
 ID3D11ShaderResourceView* views[128]{};ID3D11SamplerState* samplers[16]{};ID3D11Buffer* cb[14]{};
 ID3D11UnorderedAccessView* uavs[64]{};
 c->OMSetRenderTargetsAndUnorderedAccessViews(0,nullptr,nullptr,0,uavSlots,uavs,nullptr);
 c->CSSetUnorderedAccessViews(0,uavSlots,uavs,nullptr);
#define D18_RESET_STAGE(S) c->S##SetShader(nullptr,nullptr,0);c->S##SetShaderResources(0,128,views);c->S##SetSamplers(0,16,samplers);c->S##SetConstantBuffers(0,14,cb);
 D18_RESET_STAGE(VS) D18_RESET_STAGE(HS) D18_RESET_STAGE(DS) D18_RESET_STAGE(GS) D18_RESET_STAGE(PS) D18_RESET_STAGE(CS)
#undef D18_RESET_STAGE
 ID3D11Buffer* vb[32]{};UINT zero[32]{};
 c->IASetInputLayout(nullptr);c->IASetVertexBuffers(0,32,vb,zero,zero);c->IASetIndexBuffer(nullptr,DXGI_FORMAT_UNKNOWN,0);c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED);
 c->SOSetTargets(0,nullptr,nullptr);c->RSSetState(nullptr);c->RSSetViewports(0,nullptr);c->RSSetScissorRects(0,nullptr);
 const FLOAT blend[4]={1,1,1,1};c->OMSetBlendState(nullptr,blend,~0u);c->OMSetDepthStencilState(nullptr,0);c->SetPredication(nullptr,FALSE);
}
}
