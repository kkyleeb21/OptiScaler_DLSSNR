#include <dlssnr/Dx11CommandListWrites.h>
#include <dlssnr/ResearchCapture.h>
#include <cstdio>
#pragma comment(lib,"d3d11.lib")
#pragma comment(lib,"dxgi.lib")
int wmain(int argc,wchar_t** argv){
 if(argc!=3)return 1;
 namespace M=DlssNr::Dx11CommandListWrites;
 const bool expected=argv[2][0]==L'1';
 M::researchTrace=DlssNr::BuildProfile::ResearchCaptureRequested(argv[1]);
 M::Writes w;
 for(unsigned i=0;i<4100;++i){w.CountOperation();w.Trace(13);}
 // Turning off research must not erase safety metadata or its overflow rejection.
 if(w.operations!=4097||!(w.reasons&M::Overflow))return 2;
 if(expected ? w.traceSize!=512 : (w.traceSize!=0||w.traceDropped!=0||w.trace.capacity()!=0))return 3;
 M::researchTrace=true; // Release cannot be armed even by a stale internal flag.
 M::Writes forced;
 if(bool(forced.Trace(13))!=DlssNr::BuildProfile::Diagnostic)return 4;
 puts("PASS: research gate and production operation/overflow metadata");
 return 0;
}
