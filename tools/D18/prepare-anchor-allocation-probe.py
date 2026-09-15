"""Add an allocation-parity control to the temporal-anchor host only."""
from pathlib import Path
import sys
folder=Path(sys.argv[1])/'core-source/tools/D18'
source=(folder/'probe-temporal-anchor-runtime.cpp').read_text(encoding='utf-8-sig')
def rep(old,new):
 global source
 assert source.count(old)==1,old
 source=source.replace(old,new)
rep('require(argc==10,"forwarder runtime data-path driver-core mode output-directory style alternate-input schedule");',
    'require(argc==11,"forwarder runtime data-path driver-core mode output-directory style alternate-input schedule allocation");\n    const std::wstring allocation=argv[10];require(allocation==L"parity"||allocation==L"active","allocation policy");')
rep('const bool shared=mode!=L"independent_batch",anchored=mode==L"anchored_shared"||resetAux;',
    'const bool shared=mode!=L"independent_batch",anchored=mode==L"anchored_shared"||resetAux;\n    const unsigned featureCount=allocation==L"parity"?3:anchored?2:shared?1:3;\n    printf("created_feature_count=%u\\n",featureCount);')
assert source.count('for(unsigned i=0;i<3;++i)')==2
source=source.replace('for(unsigned i=0;i<3;++i)','for(unsigned i=0;i<featureCount;++i)')
rep('for(auto* f:features)reinterpret_cast<void(*)(void*)>(symbol("dlssnr_call_release"))(f);',
    'for(auto* f:features)if(f)reinterpret_cast<void(*)(void*)>(symbol("dlssnr_call_release"))(f);')
rep('if(destroy)for(auto* param:params)require(destroy(param)==1,"destroy capability block");',
    'if(destroy)for(auto* param:params)if(param)require(destroy(param)==1,"destroy capability block");')
target=folder/'probe-temporal-anchor-allocation-runtime.cpp';assert not target.exists();target.write_text(source,encoding='utf-8');print(target)
