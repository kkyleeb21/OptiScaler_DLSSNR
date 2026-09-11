#include "../../OptiScaler/dlssnr/PerformanceMonitor.h"
#include <cassert>
#include <iostream>
int main()
{
    auto s = new D18Monitor::Samples;
    double t = 1; s->add(t, 1, true);
    for(int i=0;i<600;++i) s->add(t+=1.0/60,1,true);
    assert(std::abs(s->fps(t)-60)<.01 && std::abs(s->low(t)-60)<.01);
    s->add(t+=.2,1,true);
    assert(s->low(t)<30); // a real stutter must affect the slow tail
    assert(s->fps(t+3)<0 && s->low(t+3)<0);
    s->add(t+=.02,1,false); assert(s->fps(t)<0 && s->low(t)<0);
    s->reset(); s->add(1,3,true);
    for(int i=1;i<=60;++i) s->add(1+i/60.0,3,true);
    assert(std::abs(s->fps(2)-180)<.01);
    s->reset(); assert(s->fps(2)<0 && s->low(2)<0);
    for(int i=0;i<40000;++i) s->add(1+i*.001,1,true);
    assert(s->size==32768 && std::abs(s->fps(40.999)-1000)<.01);
    delete s;
    std::cout << "statistics: steady, stutter, stale, failed query, 3x count, reset, bounded ring PASS\n";
    D18Monitor::read("NVIDIA GeForce RTX 5090");
    Sleep(900);
    auto v=D18Monitor::read("NVIDIA GeForce RTX 5090");
    std::cout << "NVML actual: GPU=" << v.gpu << " power=" << v.watts << " W\n";
    assert(v.gpu>=0 && v.gpu<=100 && v.watts>0);
    Sleep(3000);
    { auto& d=D18Monitor::data(); std::lock_guard lock(d.mutex); assert(!d.running); }
    std::cout << "worker idle shutdown PASS\n";
}
