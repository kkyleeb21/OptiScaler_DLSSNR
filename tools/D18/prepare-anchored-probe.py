"""Host-only three-pass architecture comparison. No game renderer changes."""
from pathlib import Path
import argparse
p=argparse.ArgumentParser();p.add_argument('build',type=Path);a=p.parse_args()
d=a.build/'core-source/tools/D18';s=(d/'probe-shared-feedback-runtime.cpp').read_text(encoding='utf-8-sig')
target=d/'probe-anchored-history-runtime.cpp';assert not target.exists()
def rep(x,y):
 global s
 assert x in s,x;s=s.replace(x,y)
rep('mode==L"shared_same_input"','mode==L"anchored_shared"')
rep('sameInput=mode==L"shared_same_input"','anchored=mode==L"anchored_shared"') if 'sameInput=mode==L"shared_same_input"' in s else rep('sameInput=mode==L"anchored_shared"','anchored=mode==L"anchored_shared"')
rep('params[2]{};void* features[2]{}','params[3]{};void* features[3]{}')
rep('for(unsigned i=0;i<2;++i)','for(unsigned i=0;i<3;++i)')
rep('ratios[2]={1.f,1.f}','ratios[3]={1.f,1.f,1.f}')
rep('auto second=create(dev.Get(),outputDesc,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);submit();',
    'auto second=create(dev.Get(),outputDesc,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);\n    auto third=create(dev.Get(),outputDesc,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);submit();')
rep('auto firstReadback=buffer(dev.Get(),bytes,true),secondReadback=buffer(dev.Get(),bytes,true);','auto firstReadback=buffer(dev.Get(),bytes,true),secondReadback=buffer(dev.Get(),bytes,true),thirdReadback=buffer(dev.Get(),bytes,true);')
rep('pass<2','pass<3')
rep('const unsigned instance=shared?0:pass;auto* answer=pass==0?output.Get():second.Get();','const unsigned instance=anchored?(pass==0?0:1):shared?0:pass;auto* answer=pass==0?output.Get():pass==1?second.Get():third.Get();')
rep('const bool reset=frame==0&&(!shared||pass==0);','const bool reset=frame==0&&(!shared||pass==0||(anchored&&pass==1));')
rep('sameInput?color.Get():previous','previous')
rep('shared&&pass>0?zeroMotion.Get():motion.Get()','shared&&pass>(anchored?1u:0u)?zeroMotion.Get():motion.Get()')
rep('pass==0?firstReadback.Get():secondReadback.Get()','pass==0?firstReadback.Get():pass==1?secondReadback.Get():thirdReadback.Get()')
rep('        submit();if(frame>=48)', '        barrier(cmd.Get(),third.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);\n        submit();if(frame>=48)')
rep('dump(secondReadback.Get(),L"pass2",frame);','dump(secondReadback.Get(),L"pass2",frame);dump(thirdReadback.Get(),L"pass3",frame);')
target.write_text(s,encoding='utf-8');print(target)
