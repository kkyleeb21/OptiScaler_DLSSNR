#include "check.h"
#include <cstdio>
int wmain(int argc,wchar_t** argv){
    if(argc!=2){fputs("Usage: D18RuntimeCheck.exe <NR runtime DLL>\n",stderr);return 2;}
    const auto r=D18RuntimeGuard::CheckFile(argv[1]);
    printf("{\"rule\":\"%s\",\"accepted\":%s,\"reason\":\"%s\",\"region\":\"%s\",\"offset\":%u,\"profile\":\"%s\"}\n",D18RuntimeGuard::Rule,r.accepted?"true":"false",r.reason,r.region,r.offset,r.profile);
    return r.accepted?0:1;
}
