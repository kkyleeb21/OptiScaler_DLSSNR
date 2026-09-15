"""Generate an explicitly diagnostic Reset control; never modifies game code."""
from pathlib import Path
import sys
folder=Path(sys.argv[1])/'core-source/tools/D18'
source=(folder/'probe-feedback-isolation-runtime.cpp').read_text(encoding='utf-8-sig')
def rep(old,new):
 global source
 assert source.count(old)==1,old
 source=source.replace(old,new)
rep('require(argc==9,"forwarder runtime data-path driver-core mode output-directory style frozen-input");',
    'require(argc==10,"forwarder runtime data-path driver-core mode output-directory style frozen-input reset-policy");\n    const std::wstring resetPolicy=argv[9];require(resetPolicy==L"initial"||resetPolicy==L"every_call","reset policy");\n    const bool resetEveryCall=resetPolicy==L"every_call";')
rep('const bool reset=frame==0&&(!shared||pass==0);',
    'const bool reset=resetEveryCall||(frame==0&&(!shared||pass==0));')
target=folder/'probe-reset-isolation-runtime.cpp';assert not target.exists();target.write_text(source,encoding='utf-8');print(target)
