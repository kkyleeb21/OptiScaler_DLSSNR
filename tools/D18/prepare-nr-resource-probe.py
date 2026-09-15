"""Prepare an isolated, bounded two-pass NR resource and GPU timestamp probe."""
from pathlib import Path
import argparse,hashlib,json,shutil
p=argparse.ArgumentParser();p.add_argument('--baseline',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();assert not a.output.exists()
origin=a.baseline/'core-source/tools/D18/probe-shared-feedback-runtime.cpp';s=origin.read_text(encoding='utf-8-sig')
def rep(old,new):
 global s
 assert s.count(old)==1,old
 s=s.replace(old,new)
rep('require(argc==8,"forwarder runtime data-path driver-core mode output-directory style");',
'''require(argc==11,"forwarder runtime data-path driver-core mode output-directory style width height ratio");
    const UINT w=UINT(_wtoi(argv[8])),h=UINT(_wtoi(argv[9])),gw=w/2,gh=h/2;
    const float ratio=float(_wtof(argv[10]));
    require(((w==1920&&h==1080)||(w==3840&&h==2160))&&(ratio==.5f||ratio==1.f),"bounded dimensions/ratio");''')
rep('mode==L"shared_batch"||mode==L"shared_same_input"||mode==L"independent_batch"','mode==L"shared_batch"||mode==L"independent_batch"')
rep('const bool shared=mode!=L"independent_batch",sameInput=mode==L"shared_same_input";','const bool shared=mode==L"shared_batch";const unsigned featureCount=shared?1:2;')
rep('warmup=48 measured=16','warmup=16 measured=32')
rep('qd.Count=2;','qd.Count=4;')
rep('buffer(dev.Get(),2*sizeof(UINT64),true)','buffer(dev.Get(),4*sizeof(UINT64),true)')
rep('memory("before_features");','require(frequency>0,"timestamp frequency");printf("timestamp_frequency=%llu\\n",frequency);\n    memory("before_features");')
rep('>2ull*1024*1024*1024,"probe needs 2 GiB headroom"','>6ull*1024*1024*1024,"resource probe needs 6 GiB headroom"')
assert s.count('for(unsigned i=0;i<2;++i)')==2
s=s.replace('for(unsigned i=0;i<2;++i)','for(unsigned i=0;i<featureCount;++i)')
rep('    const UINT w=640,h=384,gw=320,gh=192;\n','')
rep('const float ratios[2]={1.f,1.f};','const float ratios[2]={ratio,ratio};')
start=s.index('        const auto name=outputDirectory/');end=s.index('        printf("PIXELS frame=',start)
s=s[:start]+'        D3D12_RANGE empty{};rb->Unmap(0,&empty);\n'+s[end:]
rep('    for(unsigned frame=0;frame<64;++frame){',
'''    FILE* timingFile=nullptr;require(_wfopen_s(&timingFile,(outputDirectory/L"timings.csv").c_str(),L"wb")==0&&timingFile,"timing output");
    require(fprintf(timingFile,"frame,pass1_ticks,pass2_ticks,frequency\\n")>0,"timing header");
    for(unsigned frame=0;frame<48;++frame){''')
rep('            const int result=eval(', '            cmd->EndQuery(timestamps.Get(),D3D12_QUERY_TYPE_TIMESTAMP,2*pass);\n            const int result=eval(')
rep('params[instance],sameInput?color.Get():previous,','params[instance],previous,')
rep('            require(result==1,"model evaluate rejected");','            require(result==1,"model evaluate rejected");\n            cmd->EndQuery(timestamps.Get(),D3D12_QUERY_TYPE_TIMESTAMP,2*pass+1);')
rep('            copyReadback(answer,pass==0?firstReadback.Get():secondReadback.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);','            if(frame==47)copyReadback(answer,pass==0?firstReadback.Get():secondReadback.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);')
rep('        submit();if(frame>=48){dump(firstReadback.Get(),L"pass1",frame);dump(secondReadback.Get(),L"pass2",frame);}fflush(stdout);',
'''        cmd->ResolveQueryData(timestamps.Get(),D3D12_QUERY_TYPE_TIMESTAMP,0,4,timingReadback.Get(),0);submit();
        UINT64* ticks=nullptr;D3D12_RANGE timingRange{0,4*sizeof(UINT64)};
        check(timingReadback->Map(0,&timingRange,reinterpret_cast<void**>(&ticks)));
        require(ticks[1]>=ticks[0]&&ticks[3]>=ticks[2],"timestamp order");
        const int written=fprintf(timingFile,"%u,%llu,%llu,%llu\\n",frame,ticks[1]-ticks[0],ticks[3]-ticks[2],frequency);
        D3D12_RANGE noWrite{};timingReadback->Unmap(0,&noWrite);require(written>0,"timing write");
        if(frame==15)memory("after_warmup");
        memory("frame_complete");
        if(frame==47){dump(firstReadback.Get(),L"pass1",frame);dump(secondReadback.Get(),L"pass2",frame);}fflush(stdout);''')
rep('    memory("after_probe");','    require(fclose(timingFile)==0,"timing close");\n    UINT64 endFrequency=0;check(queue->GetTimestampFrequency(&endFrequency));require(endFrequency==frequency,"frequency changed");\n    memory("after_probe");')
rep('for(auto* f:features)reinterpret_cast<void(*)(void*)>(symbol("dlssnr_call_release"))(f);','for(auto* f:features)if(f)reinterpret_cast<void(*)(void*)>(symbol("dlssnr_call_release"))(f);')
rep('if(destroy)for(auto* param:params)require(destroy(param)==1,"destroy capability block");','if(destroy)for(auto* param:params)if(param)require(destroy(param)==1,"destroy capability block");')
rep('printf("PASS real NR finite stage readbacks mode=%ls 640x384 64 frames (last 16 saved); synthetic input, no game acceptance\\n",mode.c_str());',
    'printf("PASS NR resource probe mode=%ls output=%ux%u ratio=%.3f features=%u frames=48; synthetic host, no game acceptance\\n",mode.c_str(),w,h,ratio,featureCount);')
a.output.mkdir(parents=True);shutil.copytree(a.baseline/'core-source',a.output/'core-source');shutil.copy2(a.baseline/'run-multipass-checks.py',a.output/'run-multipass-checks.py')
target=a.output/'core-source/tools/D18/probe-nr-resources.cpp';target.write_text(s,encoding='utf-8')
(a.output/'preparation.json').write_text(json.dumps({'baseline':str(a.baseline.resolve()),'source':str(origin.resolve()),'source_sha256':hashlib.sha256(origin.read_bytes()).hexdigest().upper(),'scope':'resource review host only; no production changes'},indent=2),encoding='utf-8');print(target)
