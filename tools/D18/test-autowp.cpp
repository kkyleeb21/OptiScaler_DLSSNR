#include "dlssnr/AutoWhitePoint.h"
#include "shaders/dlssnr/AutoWhitePoint_Shader.h"
#include <d3dcompiler.h>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <vector>
using namespace DlssNr::AutoWhitePoint;
void check(bool condition,const char* name){if(!condition){std::printf("FAIL: %s\n",name);std::exit(1);}}
bool close(double a,double b,double epsilon=1e-6){return std::abs(a-b)<epsilon;}
int main(){
    std::vector<float> v(Cells,-12.3f);
    auto m=Measure(v.data(),v.size());Controller c;
    check(c.Accept(m,1,.05f,1,1),"first sample accepted");
    check(close(c.White(1,1,1),.003966076),"007 dark W about .004");
    v.assign(Cells,-10.6f);m=Measure(v.data(),v.size());
    Controller bright;check(bright.Accept(m,1,.05f,1,1),"bright accepted");
    check(close(bright.White(1,1,1),.012885),"007 bright W about .013");
    Controller p;check(p.Accept(m,2,.05f,1,1),"Ps=2 accepted");
    check(close(p.White(2,1,1),bright.White(1,1,1)),"matched sample/current pre-exposure");
    check(close(p.White(4,1,1),2*bright.White(1,1,1)),"current pre-exposure applies immediately");
    check(Pre(0)==1 && Pre(std::numeric_limits<float>::infinity())==1,"pre fallback finite");
    v.assign(Cells,-12.3f);for(size_t i=0;i<81;++i){v[i]=-50;v[Cells-1-i]=50;}
    check(close(Measure(v.data(),v.size()).raw,-12.3f),"both 2 percent tails removed");
    v.assign(Cells,LogFloor);check(!Measure(v.data(),v.size()).valid,"all black rejected");
    v.assign(Cells,-30);check(!Measure(v.data(),v.size()).valid,"mean below black threshold rejected");
    v.assign(Cells,LogFloor);for(size_t i=0;i<160;++i)v[i]=100;
    check(!Measure(v.data(),v.size()).valid,"over 95 percent floor rejected even with bright tail");
    v.assign(Cells,std::numeric_limits<float>::quiet_NaN());
    check(!Measure(v.data(),v.size()).valid,"all NaN rejected");
    for(size_t i=0;i<Cells/2;++i)v[i]=-12.3f;
    check(Measure(v.data(),v.size()).count==Cells/2 && Measure(v.data(),v.size()).valid,"mixed NaN discarded");
    const double initial=c.b;c.Frame(1);check(c.Accept(m,1,.05f,1.1,1.1),"new target accepted");c.Frame(1.1);
    check(close(c.b,initial+(1-std::exp(-.1/TauSeconds))*(c.target-initial)),"time smoothing");
    const double before=c.b;c.Frame(2.1);
    check(close(c.b,before+(1-std::exp(-.25/TauSeconds))*(c.target-before)),"dt capped at .25");
    const double held=c.b;c.Frame(4);
    check(c.Holding(4) && c.b==held && c.White(1,1,1)!=1,"no valid samples hold last b");
    check(!c.Accept(m,1,.05f,1,4) && c.b==held,"stale readback cannot resume");
    check(!c.Accept({},1,.05f,4,4) && c.b==held,"invalid sample leaves controller alone");
    c.ResetSample();check(c.Accept(m,2,.05f,4,4) && c.b==c.target,"reset snaps next valid sample");
    Controller wait;check(wait.White(1,1,.02f)==.02f,"waiting uses fixed white");
    check(wait.White(1,1,std::numeric_limits<float>::quiet_NaN())==1,"waiting rejects bad fallback");
    check(Key(0)==.005f && Key(10)==.5f && Trim(0)==.1f && Trim(20)==10,"config clamps");
    c.b=1000;check(c.White(1,1,1)==4096,"upper bound finite");
    c.b=-1000;check(close(c.White(1,1,1),1e-4),"lower bound finite");
    std::puts("PASS CPU: conversion, trimming, pre-exposure, smoothing, dt, reset, black, NaN, stale hold, clamps");
    ID3DBlob *code=nullptr,*error=nullptr;
    const auto hr=D3DCompile(ShaderSource,sizeof(ShaderSource)-1,"D18_AutoWhitePoint",nullptr,nullptr,
        "Measure","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&error);
    if(error){std::printf("%s\n",static_cast<const char*>(error->GetBufferPointer()));error->Release();}
    check(SUCCEEDED(hr) && code,"production shader compilation");
    std::printf("PASS shader: Measure cs_5_0, %zu bytes; no GPU execution\n",code->GetBufferSize());code->Release();
    return 0;
}
