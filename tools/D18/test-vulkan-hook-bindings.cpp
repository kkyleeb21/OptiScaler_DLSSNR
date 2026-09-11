#include "../../OptiScaler/hooks/VulkanHookBindings.h"
#include <cassert>
#include <cstdio>
#include <thread>
volatile int salt=13;
__declspec(noinline) int TargetA(int n){return n+salt+salt+salt;}
__declspec(noinline) int TargetB(int n){return n-salt-salt-salt;}
int(*originalA)(int)=TargetA;
int(*originalB)(int)=TargetB;
int ReplacementA(int n){return originalA(n)+100;}
int ReplacementB(int n){return originalB(n)+200;}
int main(){
    std::array<VulkanHookBinding,2> bindings{{
        {reinterpret_cast<PVOID*>(&originalA),(PVOID)ReplacementA,true},
        {reinterpret_cast<PVOID*>(&originalB),(PVOID)ReplacementB,true}}};
    for(int cycle=0;cycle<3;++cycle){
        assert(ChangeVulkanBindings(bindings,true));assert(TargetA(1)==140&&TargetB(1)==162);
        // A runtime caches these native addresses and then calls from its worker.
        // A thread-local bypass on the setup thread would not protect this call.
        auto nativeA=reinterpret_cast<decltype(originalA)>(ResolveVulkanOriginal(bindings,(PVOID)TargetA));
        auto nativeB=reinterpret_cast<decltype(originalB)>(ResolveVulkanOriginal(bindings,(PVOID)TargetB));
        assert(nativeA==originalA && nativeB==originalB);
        assert(ResolveVulkanOriginal(bindings,nullptr)==nullptr);
        assert(ResolveVulkanOriginal(bindings,(PVOID)ReplacementA)==(PVOID)ReplacementA);
        int workerA=0,workerB=0;
        std::thread worker([&]{workerA=nativeA(1);workerB=nativeB(1);});
        worker.join();assert(workerA==40&&workerB==-38);
        PVOID absent=nullptr;
        auto failing=bindings;failing[1].original=&absent;
        const auto rejected=ChangeVulkanBindings(failing,false);
        assert(!rejected);assert(TargetA(1)==140&&TargetB(1)==162); // First detach rolled back.
        assert(ChangeVulkanBindings(bindings,false));assert(TargetA(1)==40&&TargetB(1)==-38);
        assert(originalA(1)==40&&originalB(1)==-38); // Retained original pointers remain callable.
    }
    PVOID absent=nullptr;auto failing=bindings;failing[1].original=&absent;
    assert(!ChangeVulkanBindings(failing,true));assert(TargetA(1)==40&&TargetB(1)==-38);
    puts("PASS: live grouped attach/detach/re-attach, failed detach retention, partial attach rollback, callable original addresses, cached native dispatch on another thread");
}
