from pathlib import Path
r=Path('E:/DLSSNR/builds/D18_Wildlands_CoverageV6_20260913/core-source/OptiScaler')
p=r/'hooks/D18WildlandsSr.inl';s=p.read_text(encoding='utf-8-sig')
a=s.index('inline bool FullCoverage(');b=s.index('inline void AfterDraw(',a)
s=s[:a]+'''struct CoverageInfo {
 UINT topology=0,fill=0,sampleMask=0,writeMask=15,blend=0,alpha=0,depth=0,stencil=0,depthFunc=0,stencilFront=0,stencilBack=0;
 UINT scissor=0,rectCount=0;D3D11_RECT rect{};
};
inline unsigned Coverage(ID3D11DeviceContext* c,UINT w,UINT h,CoverageInfo& info){
 unsigned reason=0;D3D11_PRIMITIVE_TOPOLOGY topology{};c->IAGetPrimitiveTopology(&topology);info.topology=UINT(topology);
 if(topology!=D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST)reason|=4;
 ComPtr<ID3D11GeometryShader> gs;c->GSGetShader(&gs,nullptr,nullptr);if(gs)reason|=8;
 ComPtr<ID3D11HullShader> hs;c->HSGetShader(&hs,nullptr,nullptr);if(hs)reason|=16;
 ComPtr<ID3D11DomainShader> ds;c->DSGetShader(&ds,nullptr,nullptr);if(ds)reason|=32;
 ComPtr<ID3D11RasterizerState> rs;c->RSGetState(&rs);
 if(rs){D3D11_RASTERIZER_DESC desc{};rs->GetDesc(&desc);info.fill=desc.FillMode;info.scissor=desc.ScissorEnable;
  if(desc.FillMode!=D3D11_FILL_SOLID)reason|=64;
  if(desc.ScissorEnable){info.rectCount=1;c->RSGetScissorRects(&info.rectCount,&info.rect);
   if(info.rectCount!=1||info.rect.left>0||info.rect.top>0||info.rect.right<LONG(w)||info.rect.bottom<LONG(h))reason|=128;}}
 ComPtr<ID3D11BlendState> blend;FLOAT factor[4]{};c->OMGetBlendState(&blend,factor,&info.sampleMask);
 if(!(info.sampleMask&1))reason|=256;
 if(blend){D3D11_BLEND_DESC desc{};blend->GetDesc(&desc);auto& rt=desc.RenderTarget[desc.IndependentBlendEnable?1:0];
  info.alpha=desc.AlphaToCoverageEnable;info.blend=rt.BlendEnable;info.writeMask=rt.RenderTargetWriteMask;
  if(info.alpha||info.blend)reason|=512;if((info.writeMask&7)!=7)reason|=1024;}
 ComPtr<ID3D11DepthStencilView> dsv;c->OMGetRenderTargets(0,nullptr,&dsv);
 if(dsv){ComPtr<ID3D11DepthStencilState> state;UINT reference=0;c->OMGetDepthStencilState(&state,&reference);
  if(!state){reason|=2048;info.depth=1;info.depthFunc=D3D11_COMPARISON_LESS;}
  else {D3D11_DEPTH_STENCIL_DESC desc{};state->GetDesc(&desc);info.depth=desc.DepthEnable;info.stencil=desc.StencilEnable;info.depthFunc=desc.DepthFunc;info.stencilFront=desc.FrontFace.StencilFunc;info.stencilBack=desc.BackFace.StencilFunc;if(info.depth||info.stencil)reason|=2048;}}
 return reason;
}
inline void CoverageTrace(ID3D11DeviceContext* c,unsigned reason,UINT count,UINT first,UINT w,UINT h,const CoverageInfo& info){
 static unsigned records=0;static ULONGLONG last=0;const auto now=GetTickCount64();
 if(records>=8||(records&&now-last<1000))return;++records;last=now;
 auto pair=[](UINT a,UINT b){return (UINT64(a)<<32)|b;};
 D18ExecutionTrace::Point("sr_coverage_reason",c,0,reason,pair(w,h));
 D18ExecutionTrace::Point("sr_coverage_draw",c,0,count,first);
 D18ExecutionTrace::Point("sr_coverage_raster",c,0,pair(info.topology,info.fill),pair(info.scissor,info.rectCount));
 D18ExecutionTrace::Point("sr_coverage_scissor",c,0,pair(UINT(info.rect.left),UINT(info.rect.top)),pair(UINT(info.rect.right),UINT(info.rect.bottom)));
 D18ExecutionTrace::Point("sr_coverage_blend",c,0,pair(info.alpha,info.blend),pair(info.sampleMask,info.writeMask));
 D18ExecutionTrace::Point("sr_coverage_depth",c,0,pair(info.depth,info.stencil),pair(info.depthFunc,pair(info.stencilFront,info.stencilBack)&0xffffffff));
 D18ExecutionTrace::Point("sr_coverage_stencil_func",c,0,info.stencilFront,info.stencilBack);
}
''' +s[b:]
s=s.replace('bool fullScreenDraw=false){','bool fullScreenDraw=false,UINT drawCount=0,UINT drawFirst=0){')
needle=' if(!presentOwner||!fullScreenDraw||!FullCoverage(c,w,h)){'
assert needle in s
s=s.replace(needle,''' CoverageInfo coverageInfo;const unsigned coverage=(!presentOwner?1u:0u)|(!fullScreenDraw?2u:0u)|Coverage(c,w,h,coverageInfo);
 Status::coverageReason=coverage;
 if(coverage){CoverageTrace(c,coverage,drawCount,drawFirst,w,h,coverageInfo);''')
# The guard predicates are unchanged; only all reasons are evaluated and reported.
p.write_text(s,encoding='utf-8')
p=r/'hooks/D18InputProbe.h';s=p.read_text(encoding='utf-8-sig').replace('Wildlands::AfterDraw(c,a==3&&b==0);','Wildlands::AfterDraw(c,a==3&&b==0,a,b);');p.write_text(s,encoding='utf-8')
p=r/'dlssnr/WildlandsSrStatus.h';s=p.read_text(encoding='utf-8-sig').replace('inline std::atomic<bool> coverageBlocked','inline std::atomic<unsigned> coverageReason{0};\ninline std::atomic<bool> coverageBlocked');p.write_text(s,encoding='utf-8')
p=r/'menu/menu_common.cpp';s=p.read_text(encoding='utf-8-sig');needle='if(W::enabled && W::coverageBlocked)D18Ui::TextWrapped("SR waiting: draw coverage is not supported; original rendering preserved.");';assert needle in s
s=s.replace(needle,'''if(W::enabled && W::coverageBlocked){
            D18Ui::TextWrapped("SR waiting: draw coverage is not supported; original rendering preserved.");
            D18Ui::Text("Coverage reason: 0x%03x",W::coverageReason.load());
        }''');p.write_text(s,encoding='utf-8-sig')
print('V6 detailed coverage staged; no gate relaxation')
