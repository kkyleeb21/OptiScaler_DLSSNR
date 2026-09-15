from pathlib import Path
r=Path('E:/DLSSNR/builds/D18_Wildlands_SRContractsV5_20260913')
out=r/'contract-test';out.mkdir(exist_ok=True)
s=(r.parent/'D18_Wildlands_SRContractReview_20260913/sr-host.cpp').read_text()
s=s.replace('clipDesc.ScissorEnable=TRUE;','clipDesc.ScissorEnable=wcscmp(argv[3],L"mask")!=0;')
s=s.replace(' const float sentinel[]=', ''' ComPtr<ID3D11BlendState> masked;
 D3D11_BLEND_DESC maskDesc{};maskDesc.IndependentBlendEnable=TRUE;maskDesc.RenderTarget[0].RenderTargetWriteMask=15;maskDesc.RenderTarget[1].RenderTargetWriteMask=9;
 if(FAILED(d->CreateBlendState(&maskDesc,&masked)))return 44;
 const float sentinel[]=''')
s=s.replace('  c->PSSetShader(ps.Get(),nullptr,0);if(predicated)', '  if(wcscmp(argv[3],L"mask")==0)c->OMSetBlendState(masked.Get(),nullptr,~0u);\n  c->PSSetShader(ps.Get(),nullptr,0);if(predicated)')
s=s.replace('if(!outsideChanged)return 43;', '''if(wcscmp(argv[3],L"mask")==0){unsigned changed=0;for(UINT pixel:finalPixels[1])if(((pixel>>10)&1023)!=716)++changed;printf("masked_green_changed=%u\\n",changed);if(changed)return 45;}
  else if(outsideChanged)return 43;''')
s=s.replace(' // This is an expected-defect reproducer, not a fixed-build acceptance test.', ' // Fixed-build acceptance: protected pixels must remain unchanged.')
(out/'sr-host.cpp').write_text(s)
runner=(r.parent/'D18_Wildlands_UIRepeatAudit_20260913/test-sr.py').read_text()
runner=runner.replace("r.parent/'D18_Wildlands_ExecutionV4_20260913/core-output", "r.parent/'core-output")
runner=runner.replace("r.parent/'D18_Wildlands_InputProbeV7_20260912", "r.parent.parent/'D18_Wildlands_InputProbeV7_20260912")
runner=runner.replace(" debug=(d/'debug.txt')", " assert not any(e.get('submitted',0) or e.get('composed',0) for e in events),'Rejected draw must not run SR'\n assert any(e['stage']=='output_coverage_rejected' for e in events)\n debug=(d/'debug.txt')")
(out/'test-sr.py').write_text(runner)
print(out)
