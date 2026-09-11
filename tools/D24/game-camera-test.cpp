#include <windows.h>
#include <cassert>
#include <limits>
#include <dlssnr/GameCameraCapture.h>
int main()
{
    using namespace DlssNr::GameCamera;
    sl::Constants c {};
    c.cameraFOV = 1; c.cameraAspectRatio = 1.777f;
    for (auto* m : {&c.cameraViewToClip,&c.clipToCameraView,&c.clipToPrevClip,&c.prevClipToClip})
    {
        auto* f=reinterpret_cast<float*>(m);
        for(int i=0;i<16;++i) f[i]=(i%5==0)?1.f:0.f;
    }
    sl::ViewportHandle viewport{7}, other{8};
    const sl::BaseStructure* input[]={&viewport};
    Capture(c,42,7);
    { Scope s(true,42,input,1); assert(evaluating && evaluating->frame==42);
      {Scope nested(false,42,input,1);assert(!evaluating);} assert(evaluating); }
    assert(!evaluating);
    {Scope s(true,43,input,1);assert(!evaluating);}
    const sl::BaseStructure* wrong[]={&other};
    {Scope s(true,42,wrong,1);assert(!evaluating);}
    const sl::BaseStructure* ambiguous[]={&viewport,&viewport};
    {Scope s(true,42,ambiguous,2);assert(!evaluating);}
    {Scope s(true,42,nullptr,0);assert(!evaluating);}
    Capture(c,44,7); samples[(next-1)%samples.size()]->time-=std::chrono::seconds(3);
    {Scope s(true,44,input,1);assert(!evaluating);}
    c.cameraFOV=std::numeric_limits<float>::quiet_NaN(); Capture(c,45,7);
    {Scope s(true,45,input,1);assert(!evaluating);}
    return 0;
}
