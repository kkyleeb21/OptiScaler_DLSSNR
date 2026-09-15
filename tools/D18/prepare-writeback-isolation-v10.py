from pathlib import Path
r=Path('E:/DLSSNR/builds/D18_Wildlands_WritebackIsolationV10_20260913');src=r/'core-source/OptiScaler'
p=src/'dlssnr/SrDiagnosticStages.h';s=p.read_text();s=s.replace('OriginalWriteback, Full, Count','OriginalWriteback, Full, CopyOnly, OffscreenReplay, Count');s=s.replace('case Full:return "6 - Full SR and writeback";', 'case Full:return "6 - Full SR and writeback";case CopyOnly:return "7 - Copy original only (no draw)";case OffscreenReplay:return "8 - Replay offscreen (no game writeback)";');p.write_text(s)
p=src/'hooks/D18WildlandsSr.inl';s=p.read_text()
s=s.replace('inline ComPtr<ID3D11RasterizerState> raster;', 'inline ComPtr<ID3D11RenderTargetView> offscreenRT;\ninline ComPtr<ID3D11RasterizerState> raster;')
s=s.replace('if(f.diagnosticStage!=Stages::OriginalWriteback)++gpuStages[f.done];', 'if(f.diagnosticStage!=Stages::OriginalWriteback&&f.diagnosticStage!=Stages::CopyOnly&&f.diagnosticStage!=Stages::OffscreenReplay)++gpuStages[f.done];')
s=s.replace(' if(mode==Stages::OriginalWriteback){', ''' if(mode==Stages::OriginalWriteback||mode==Stages::CopyOnly||mode==Stages::OffscreenReplay){
  if(mode==Stages::OffscreenReplay&&!offscreenRT){
   // Private target is created only for this diagnostic stage. No change to
   // the normal SR output texture descriptor or production allocation path.
   D3D11_TEXTURE2D_DESC td{};tex[4]->GetDesc(&td);td.BindFlags=D3D11_BIND_RENDER_TARGET;
   ComPtr<ID3D11Texture2D> target;auto hr=d->CreateTexture2D(&td,nullptr,&target);
   if(SUCCEEDED(hr))hr=d->CreateRenderTargetView(target.Get(),nullptr,&offscreenRT);
   if(FAILED(hr)){pending->pending=false;--gpuSubmitted;Fail("diagnostic_offscreen_allocation",hr);return;}
  }''')
needle='c->CopyResource(tex[4].Get(),outputResource[1].Get());geometry.Apply(context.Get());auto rt=outputRT[1].Get();'
replacement='''c->CopyResource(tex[4].Get(),outputResource[1].Get());
  if(mode==Stages::CopyOnly){c->End(pending->stage[0].Get());pending->issued=1;StageCpu(mode);return;}
  geometry.Apply(context.Get());auto rt=mode==Stages::OffscreenReplay?offscreenRT.Get():outputRT[1].Get();'''
assert needle in s;s=s.replace(needle,replacement)
p.write_text(s)
p=src/'menu/menu_common.cpp';s=p.read_text(encoding='utf-8-sig');s=s.replace('Stages 0-5 preserve the original image. Stage 5 replays only the original image; stage 6 writes SR.', 'Only stage 6 writes SR. Stage 5 writes the original image to the game target; stage 7 only copies it; stage 8 draws into a private target.');p.write_text(s,encoding='utf-8-sig')
p=r/'sr-host.cpp';s=p.read_text().replace('unsigned modes[]={0,1,2,3,4,5,6,0,4}', 'unsigned modes[]={0,7,8,5,7,8,5,0,7}');p.write_text(s)
p=r/'test-stages.py';s=p.read_text().replace("['stage0','stage1','stage2','stage3','stage4','stage5','stage6','stage-cycle']", "['stage7','stage8','stage5','stage-cycle']");s=s.replace('range(7)', 'range(9)');s=s.replace('applied==[1,2,3,4,5,6,0,4]', 'applied==[7,8,5,7,8,5,0,7]');s=s.replace('all(cpu[i]>0 for i in range(9))', 'all(cpu[i]>0 for i in (0,5,7,8))');s=s.replace('all(gpu[i]>0 for i in range(2,7))', 'all(gpu[i]>0 for i in (5,7,8))');s=s.replace('if stage in (0,1,2,5):', 'if stage in (0,1,2,5,7,8):');s=s.replace('if stage<4 or stage==5:', 'if stage<4 or stage in (5,7,8):');p.write_text(s)
