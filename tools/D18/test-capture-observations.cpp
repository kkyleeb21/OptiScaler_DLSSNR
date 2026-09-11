#include <Windows.h>
#include <dlssnr/CaptureEvidence.h>
#include <cassert>
#include <cstdio>
int main(int argc, char** argv) {
    assert(argc == 2);
    capture::FrameEvidence e{};
    e.frame=100;e.successfulSinceReset=40;
    e.rr=true;e.routeRr=false;e.ngxSourceObserved=true;
    e.inputWidth=7;e.inputHeight=5;e.networkWidth=4;e.networkHeight=3;
    e.rects.output={1,1,5,3};e.rects.depth={0,0,4,3};e.rects.motion={0,0,4,3};
    e.resolve.Width=7;e.resolve.Height=5;e.resolve.Transfer=1;
    e.resolve.WhitePoint=1;e.resolve.NetworkRatioX=0.5f;e.resolve.NetworkRatioY=0.5f;
    // The observed game case: half network, full physical proxy, legacy compose.
    assert(!capture::matchedResidualEligible(e));
    e.inputWidth=4;assert(capture::matchedResidualEligible(e));
    e.resolve.Transfer=0;assert(!capture::matchedResidualEligible(e));
    e.resolve.Transfer=1;e.inputWidth=7;e.resolve.ExperimentalCompose=1;
    assert(capture::matchedResidualEligible(e));
    e.resolve.NetworkRatioX=1;e.resolve.NetworkRatioY=1;
    assert(!capture::matchedResidualEligible(e));
    e.resolve.NetworkRatioY=0.5f;
    for(unsigned debug=1;debug<=3;++debug){e.resolve.DebugView=debug;assert(!capture::matchedResidualEligible(e));}
    e.resolve.DebugView=0;e.resolve.ExperimentalCompose=0;
    std::FILE* f=nullptr;assert(fopen_s(&f,argv[1],"wb")==0 && f);
    capture::writeEvidence(f,e);assert(std::fclose(f)==0);
    // Diagnostics source stays independent of resource-state/rect route.
    assert(e.rr && !e.routeRr);
    std::puts("PASS: branch gates and production v2 serializer; no GPU/model execution");
}
