"""Generate the bounded C8 control that fixes the model's input resource identity."""
from pathlib import Path
import sys
build=Path(sys.argv[1]);folder=build/'core-source/tools/D18'
source=(folder/'probe-shared-feedback-runtime.cpp').read_text(encoding='utf-8-sig')
def replace(old,new):
 global source
 assert source.count(old)==1,old
 source=source.replace(old,new)
replace('mode==L"shared_batch"||mode==L"shared_same_input"||mode==L"independent_batch"',
        'mode==L"shared_chain_fixed"||mode==L"shared_same_input_fixed"||mode==L"independent_fixed"')
replace('const bool shared=mode!=L"independent_batch",sameInput=mode==L"shared_same_input";',
        'const bool shared=mode!=L"independent_fixed",sameInput=mode==L"shared_same_input_fixed";')
replace('DlssNrAbi::Frame rect;', '''auto scratchDesc=texture(w,h,false);scratchDesc.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;
    auto fixedInput=create(dev.Get(),scratchDesc,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    // The model sees one input resource in every pass/frame in all three modes.
    // All modes issue exactly one full copy per model call; only copied pixels differ.
    auto setFixedInput=[&](ID3D12Resource* source){
        barrier(cmd.Get(),source,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE);
        barrier(cmd.Get(),fixedInput.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_DEST);
        cmd->CopyResource(fixedInput.Get(),source);
        barrier(cmd.Get(),fixedInput.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        barrier(cmd.Get(),source,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    };
    DlssNrAbi::Frame rect;''')
replace('const int result=eval(cmd.Get(),features[instance],params[instance],sameInput?color.Get():previous,',
        'setFixedInput(sameInput?color.Get():previous);\n            const int result=eval(cmd.Get(),features[instance],params[instance],fixedInput.Get(),')
target=folder/'probe-fixed-input-runtime.cpp'
assert not target.exists()
target.write_text(source,encoding='utf-8')
print(target)
