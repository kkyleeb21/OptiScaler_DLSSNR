from pathlib import Path
r=Path('E:/DLSSNR/builds/D18_Wildlands_SRContractsV5_20260913/core-source/OptiScaler')
p=r/'hooks/D18WildlandsSr.inl';s=p.read_text(encoding='utf-8-sig')
s=s.replace('inline void Fail(const char* stage,long code){fault=true;', 'inline void Fail(const char* stage,long code){if(fault)return;fault=true;')
s=s.replace('unsigned done=0;', 'unsigned done=0,issued=0;')
s=s.replace('gpuSubmitted=0,gpuComplete=0,','gpuSubmitted=0,gpuComplete=0,gpuDrained=0,')
s=s.replace('while(f.done<3)', 'while(f.done<f.issued)')
s=s.replace('if(f.done==3){f.pending=false;++gpuComplete;Status::gpuCompleted=gpuComplete;Status::lastGpuTick=GetTickCount64();}', 'if(f.done==f.issued){f.pending=false;++gpuDrained;if(f.issued==3){++gpuComplete;Status::gpuCompleted=gpuComplete;Status::lastGpuTick=GetTickCount64();}}')
s=s.replace('f.done=0;return &f;', 'f.done=0;f.issued=0;return &f;')
s=s.replace('gpuComplete!=gpuSubmitted','gpuDrained!=gpuSubmitted')
for i in range(3):s=s.replace(f'c->End(pending->stage[{i}].Get());',f'c->End(pending->stage[{i}].Get());pending->issued={i+1};')
s=s.replace('inline void AfterDraw(ID3D11DeviceContext* c){','''
inline const void* presentOwner=nullptr;
inline bool AcceptPresent(const void* chain,bool primary){
 if(!chain)return false;
 if(!presentOwner){if(!primary)return false;presentOwner=chain;shadowValid=false;reset=true;Event("present_owner_selected");}
 return presentOwner==chain;
}
inline void ReleasePresent(const void* chain){
 std::lock_guard lock(guard);
 if(presentOwner!=chain)return;
 presentOwner=nullptr;shadowValid=false;reset=true;
 if(initialized)Fail("present_owner_released_restart_required",E_ABORT);
}
// Reject unproven partial coverage before allocating/evaluating SR.
inline bool FullCoverage(ID3D11DeviceContext* c,UINT w,UINT h){
 D3D11_PRIMITIVE_TOPOLOGY topology{};c->IAGetPrimitiveTopology(&topology);
 if(topology!=D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST)return false;
 ComPtr<ID3D11GeometryShader> gs;c->GSGetShader(&gs,nullptr,nullptr);if(gs)return false;
 ComPtr<ID3D11HullShader> hs;c->HSGetShader(&hs,nullptr,nullptr);if(hs)return false;
 ComPtr<ID3D11DomainShader> ds;c->DSGetShader(&ds,nullptr,nullptr);if(ds)return false;
 ComPtr<ID3D11RasterizerState> rs;c->RSGetState(&rs);
 if(rs){D3D11_RASTERIZER_DESC desc{};rs->GetDesc(&desc);if(desc.FillMode!=D3D11_FILL_SOLID)return false;
  if(desc.ScissorEnable){UINT count=1;D3D11_RECT rect{};c->RSGetScissorRects(&count,&rect);
   if(count!=1||rect.left>0||rect.top>0||rect.right<LONG(w)||rect.bottom<LONG(h))return false;}}
 ComPtr<ID3D11BlendState> blend;UINT sampleMask=0;FLOAT factor[4]{};c->OMGetBlendState(&blend,factor,&sampleMask);
 if(!(sampleMask&1))return false;
 if(blend){D3D11_BLEND_DESC desc{};blend->GetDesc(&desc);auto& rt=desc.RenderTarget[desc.IndependentBlendEnable?1:0];
  if(desc.AlphaToCoverageEnable||rt.BlendEnable||(rt.RenderTargetWriteMask&7)!=7)return false;}
 ComPtr<ID3D11DepthStencilView> dsv;c->OMGetRenderTargets(0,nullptr,&dsv);
 if(dsv){ComPtr<ID3D11DepthStencilState> state;UINT reference=0;c->OMGetDepthStencilState(&state,&reference);
  if(!state)return false;D3D11_DEPTH_STENCIL_DESC desc{};state->GetDesc(&desc);if(desc.DepthEnable||desc.StencilEnable)return false;}
 return true;
}
inline void AfterDraw(ID3D11DeviceContext* c,bool fullScreenDraw=false){''')
needle='StateScope scope;\n trace.Step("sr_state_enter");'
assert needle in s
# Run output gates before ReserveGpu/Initialize, not after feature creation.
gate=''' if(!presentOwner||!fullScreenDraw||!FullCoverage(c,w,h)){++waits;reset=true;Status::coverageBlocked=true;
  if(GetTickCount64()-lastLog>1000){lastLog=GetTickCount64();Event("output_coverage_rejected");}return;}
 Status::coverageBlocked=false;
'''
s=s.replace(' GpuFrame* pending=ReserveGpu(d.Get());',gate+' GpuFrame* pending=ReserveGpu(d.Get());')
p.write_text(s,encoding='utf-8')
p=r/'hooks/D18InputProbe.h';s=p.read_text(encoding='utf-8-sig').replace('inline void Frame(ID3D11Device* device) {','inline void Frame(ID3D11Device* device,const void* chain=nullptr,bool primary=false) {')
s=s.replace('    ID3D11DeviceContext* current=nullptr;device->GetImmediateContext(&current);','    if(Wildlands::enabled && (device!=observedDevice||!Wildlands::AcceptPresent(chain,primary)))return;\n    ID3D11DeviceContext* current=nullptr;device->GetImmediateContext(&current);')
s=s.replace('if(a)Wildlands::AfterDraw(c);}', 'if(a)Wildlands::AfterDraw(c,a==3&&b==0);}',1)
p.write_text(s,encoding='utf-8')
p=r/'wrapped/wrapped_swapchain.cpp';s=p.read_text(encoding='utf-8-sig').replace('D18InputProbe::Frame(probeDevice);','D18InputProbe::Frame(probeDevice,this,State::Instance().currentSwapchain==this);')
s=s.replace('        MenuOverlayDx::CleanupRenderTarget(true, _handle);','        D18InputProbe::Wildlands::ReleasePresent(this);\n        MenuOverlayDx::CleanupRenderTarget(true, _handle);',1)
# ReleasePresent must follow retirement, so it cannot latch fault before Retire.
s=s.replace('        D18InputProbe::Wildlands::ReleasePresent(this);\n','')
s=s.replace('        if (State::Instance().currentRealSwapchain == this)','        D18InputProbe::Wildlands::ReleasePresent(this);\n\n        if (State::Instance().currentRealSwapchain == this)',1)
p.write_text(s,encoding='utf-8')
p=r/'dlssnr/WildlandsSrStatus.h';s=p.read_text(encoding='utf-8-sig').replace('inline std::atomic<bool> available','inline std::atomic<bool> coverageBlocked{false};\ninline std::atomic<bool> available');p.write_text(s,encoding='utf-8')
p=r/'menu/menu_common.cpp';s=p.read_text(encoding='utf-8-sig').replace('        D18Ui::TextWrapped("Same-resolution DLSS preview.', '        if(W::coverageBlocked)D18Ui::TextWrapped("SR waiting: draw coverage is not supported; original rendering preserved.");\n        D18Ui::TextWrapped("Same-resolution DLSS preview.');p.write_text(s,encoding='utf-8')
print('V5 source staged')
