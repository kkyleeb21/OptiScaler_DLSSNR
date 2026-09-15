"""Create a complete successor for a persistent first pass and reset auxiliary instance."""
from pathlib import Path
import argparse,hashlib,json,shutil
p=argparse.ArgumentParser();p.add_argument('--baseline',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
assert not a.output.exists()
folder=a.baseline/'core-source/tools/D18'
source=(folder/'probe-anchored-history-runtime.cpp').read_text(encoding='utf-8-sig')
upload_source=(folder/'probe-feedback-isolation-runtime.cpp').read_text(encoding='utf-8-sig')
upload=upload_source[upload_source.index('    auto frozenInput=create('):upload_source.index('    // The model sees one input resource')]
def rep(old,new):
 global source
 assert source.count(old)==1,old
 source=source.replace(old,new)
rep('require(argc==8,"forwarder runtime data-path driver-core mode output-directory style");',
    'require(argc==10,"forwarder runtime data-path driver-core mode output-directory style alternate-input schedule");\n    const std::wstring schedule=argv[9];require(schedule==L"constant"||schedule==L"step48","input schedule");')
rep('mode==L"shared_batch"||mode==L"anchored_shared"||mode==L"independent_batch"',
    'mode==L"shared_batch"||mode==L"anchored_shared"||mode==L"independent_batch"||mode==L"anchored_reset_aux"')
rep('const bool shared=mode!=L"independent_batch",anchored=mode==L"anchored_shared";',
    'const bool resetAux=mode==L"anchored_reset_aux";\n    const bool shared=mode!=L"independent_batch",anchored=mode==L"anchored_shared"||resetAux;')
rep('    DlssNrAbi::Frame rect;',
    '    auto scratchDesc=texture(w,h,false);scratchDesc.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;\n'+upload+'    DlssNrAbi::Frame rect;')
rep('    for(unsigned frame=0;frame<64;++frame){',
    '    copyReadback(frozenInput.Get(),firstReadback.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);submit();dump(firstReadback.Get(),L"frozen",0);\n    for(unsigned frame=0;frame<64;++frame){')
rep('ID3D12Resource* previous=color.Get();',
    'ID3D12Resource* previous=schedule==L"step48"&&frame>=48?frozenInput.Get():color.Get();')
rep('const bool reset=frame==0&&(!shared||pass==0||(anchored&&pass==1));',
    'const bool reset=(frame==0&&(!shared||pass==0||(anchored&&pass==1)))||(resetAux&&pass>0);\n            if(frame==0||frame==48)printf("CALL frame=%u pass=%u feature=%u reset=%u inputB=%u\\n",frame,pass,instance,unsigned(reset),unsigned(schedule==L"step48"&&frame>=48));')
a.output.mkdir(parents=True);shutil.copytree(a.baseline/'core-source',a.output/'core-source')
shutil.copy2(a.baseline/'run-multipass-checks.py',a.output/'run-multipass-checks.py')
target=a.output/'core-source/tools/D18/probe-temporal-anchor-runtime.cpp';target.write_text(source,encoding='utf-8')
(a.output/'preparation.json').write_text(json.dumps({'baseline':str(a.baseline.resolve()),'scope':'host only; persistent first-pass state and reset auxiliary instance; no game changes','sources':{n:hashlib.sha256((folder/n).read_bytes()).hexdigest().upper() for n in ['probe-anchored-history-runtime.cpp','probe-feedback-isolation-runtime.cpp']}},indent=2),encoding='utf-8')
print(target)
