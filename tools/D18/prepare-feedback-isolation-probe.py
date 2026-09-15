"""Create a complete successor with a frozen-input/parameter-block NR host control."""
from pathlib import Path
import argparse,hashlib,json,shutil
p=argparse.ArgumentParser();p.add_argument('--baseline',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
assert not a.output.exists()
origin=a.baseline/'core-source/tools/D18/probe-fixed-input-runtime.cpp'
source=origin.read_text(encoding='utf-8-sig')
def rep(old,new):
 global source
 assert source.count(old)==1,old
 source=source.replace(old,new)
rep('require(argc==8,"forwarder runtime data-path driver-core mode output-directory style");',
    'require(argc==9,"forwarder runtime data-path driver-core mode output-directory style frozen-input");')
rep('require(mode==L"shared_chain_fixed"||mode==L"shared_same_input_fixed"||mode==L"independent_fixed","unsupported probe mode");',
    'require(mode==L"feedback_live_shared"||mode==L"feedback_live_independent"||mode==L"feedback_frozen_shared"||mode==L"feedback_frozen_independent"||mode==L"feedback_live_split_params","unsupported probe mode");')
rep('const bool shared=mode!=L"independent_fixed",sameInput=mode==L"shared_same_input_fixed";',
    'const bool shared=mode!=L"feedback_live_independent"&&mode!=L"feedback_frozen_independent";\n    const bool frozen=mode==L"feedback_frozen_shared"||mode==L"feedback_frozen_independent",splitParams=mode==L"feedback_live_split_params";')
rep('auto fixedInput=create(dev.Get(),scratchDesc,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);', '''auto fixedInput=create(dev.Get(),scratchDesc,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    auto frozenInput=create(dev.Get(),scratchDesc,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COPY_DEST);
    {
        FILE* f=nullptr;require(_wfopen_s(&f,argv[8],L"rb")==0&&f,"open frozen input");
        std::vector<unsigned char> raw(size_t(w)*h*8);const auto count=fread(raw.data(),1,raw.size(),f);
        const int extra=fgetc(f);const bool readError=ferror(f)!=0;const int close=fclose(f);
        require(count==raw.size()&&extra==EOF&&!readError&&close==0,"frozen input size/read");
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT frozenFp{};UINT64 frozenBytes=0;
        dev->GetCopyableFootprints(&scratchDesc,0,1,0,&frozenFp,nullptr,nullptr,&frozenBytes);
        auto up=buffer(dev.Get(),frozenBytes,false);unsigned char* mapped=nullptr;D3D12_RANGE empty{};
        check(up->Map(0,&empty,reinterpret_cast<void**>(&mapped)));
        for(UINT y=0;y<h;++y)memcpy(mapped+y*frozenFp.Footprint.RowPitch,raw.data()+size_t(y)*w*8,w*8);
        up->Unmap(0,nullptr);auto from=location(up.Get()),to=location(frozenInput.Get());
        from.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;from.PlacedFootprint=frozenFp;
        cmd->CopyTextureRegion(&to,0,0,0,&from,nullptr);
        barrier(cmd.Get(),frozenInput.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        uploads.push_back(up);submit();
    }''')
rep('for(unsigned frame=0;frame<64;++frame){',
    'copyReadback(frozenInput.Get(),firstReadback.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);submit();dump(firstReadback.Get(),L"frozen",0);\n    for(unsigned frame=0;frame<64;++frame){')
rep('setFixedInput(sameInput?color.Get():previous);',
    'setFixedInput(frozen&&pass==1?frozenInput.Get():previous);\n            const unsigned paramIndex=splitParams?pass:instance;\n            if(frame==0)printf("CALL pass=%u feature=%u params=%u reset=%u frozen=%u\\n",pass,instance,paramIndex,unsigned(reset),unsigned(frozen&&pass==1));')
rep('features[instance],params[instance],fixedInput.Get()',
    'features[instance],params[paramIndex],fixedInput.Get()')
a.output.mkdir(parents=True)
shutil.copytree(a.baseline/'core-source',a.output/'core-source')
shutil.copy2(a.baseline/'run-multipass-checks.py',a.output/'run-multipass-checks.py')
target=a.output/'core-source/tools/D18/probe-feedback-isolation-runtime.cpp';target.write_text(source,encoding='utf-8')
(a.output/'preparation.json').write_text(json.dumps({'baseline':str(a.baseline.resolve()),'probe_source':str(origin.resolve()),'probe_source_sha256':hashlib.sha256(origin.read_bytes()).hexdigest().upper(),'scope':'host only; frozen second input and split parameter blocks; production unchanged'},indent=2),encoding='utf-8')
print(target)
