// Offline byte admission checker. No LoadLibrary, host, GPU or Runtime execution.
#include "../../../OptiScaler/dlssnr/RuntimeProfile.h"
#include <cstdio>
int wmain(int argc,wchar_t** argv) {
    if(argc!=2)return 2;
    const auto* p=DlssNr::Runtime::FromFile(argv[1]);
    if(!p){puts("{\"accepted\":false,\"profile\":\"unknown\"}");return 1;}
    printf("{\"accepted\":true,\"profile\":\"%s\",\"grade\":%s,\"sampler\":%s,\"dx11\":%s,\"network_3440_50\":%u}\n",
        p->id,p->GradeSupported()?"true":"false",p->SamplerSupported()?"true":"false",p->Dx11Supported()?"true":"false",
        DlssNr::Runtime::Network(p,3440,.5f,16));
    return 0;
}
