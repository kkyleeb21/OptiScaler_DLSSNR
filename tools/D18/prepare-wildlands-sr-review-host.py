from pathlib import Path
import shutil
r=Path('E:/DLSSNR/builds/D18_Wildlands_SRContractReview_20260913')
r.mkdir(exist_ok=True)
base=r.parent/'D18_Wildlands_ExecutionV4_20260913'
s=(base/'sr-host.cpp').read_text()
s=s.replace('unsigned menuToggles=0,presentStateChecks=0;', '''
 ComPtr<ID3D11RasterizerState> clipped;
 D3D11_RASTERIZER_DESC clipDesc{};clipDesc.FillMode=D3D11_FILL_SOLID;clipDesc.CullMode=D3D11_CULL_NONE;clipDesc.DepthClipEnable=TRUE;clipDesc.ScissorEnable=TRUE;
 if(FAILED(d->CreateRasterizerState(&clipDesc,&clipped)))return 42;
 const float sentinel[]={.8f,.7f,.6f,1.0f/3};
 for(auto view:rt)c->ClearRenderTargetView(view,sentinel);
 unsigned menuToggles=0,presentStateChecks=0;''')
s=s.replace('  c->PSSetShader(ps.Get(),nullptr,0);if(predicated)', '''  D3D11_RECT clipRect{0,0,LONG(tw/2),LONG(th)};c->RSSetState(clipped.Get());c->RSSetScissorRects(1,&clipRect);
  c->PSSetShader(ps.Get(),nullptr,0);if(predicated)''')
s=s.replace('if(mean[0]<.20||mean[0]>.30||mean[1]<.10||mean[1]>.40)return 35;', '''
  unsigned outsideChanged=0;for(UINT y=0;y<th;++y)for(UINT x=tw/2;x<tw;++x)if((finalPixels[0][y*tw+x]&0x3fffffff)!=(finalPixels[1][y*tw+x]&0x3fffffff))++outsideChanged;
  printf("outside_scissor_changed=%u outside_pixels=%u\\n",outsideChanged,(tw/2)*th);
  if(!outsideChanged)return 43; // This is an expected-defect reproducer, not a fixed-build acceptance test.
''')
(r/'sr-host.cpp').write_text(s)
runner=(r.parent/'D18_Wildlands_UIRepeatAudit_20260913/test-sr.py').read_text()
(r/'test-sr.py').write_text(runner)
print(r)
