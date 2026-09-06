#pragma once
#include <array>
#include <cstdint>
namespace DlssNr::Re9Barrier {
// Only describes transitions observed inside one synchronous native RR call, never global GPU state.
struct State {
    struct Sub {bool known=false,pending=false;uint32_t value=0,before=0,after=0;};
    std::array<Sub,64> sub{};
    uint32_t count=0,transitions=0,uavs=0;
    const char* rejected=nullptr;
    void Reject(const char* reason){if(!rejected)rejected=reason;}
    void Transition(uint32_t index,uint32_t before,uint32_t after,unsigned flags){
        ++transitions;
        if(!count || count>64){Reject("unknown-subresource-count");return;}
        if(index!=UINT32_MAX && index>=count){Reject("subresource-out-of-range");return;}
        if(flags>2){Reject("unsupported-flags");return;}
        const auto begin=index==UINT32_MAX?0:index,end=index==UINT32_MAX?count:index+1;
        for(auto i=begin;i<end;++i){auto& s=sub[i];
            if(flags==1){
                if(s.pending || (s.known && s.value!=before))Reject("split-begin-conflict");
                s.pending=true;s.before=before;s.after=after;
            }else if(flags==2){
                if(!s.pending || s.before!=before || s.after!=after)Reject("unmatched-split-end");
                s.pending=false;s.known=true;s.value=after;
            }else{
                if(s.pending || (s.known && s.value!=before))Reject("transition-conflict");
                s.known=true;s.value=after;
            }
        }
    }
    uint64_t KnownMask()const{
        if(rejected)return 0;
        uint64_t mask=0;for(unsigned i=0;i<count && i<64;++i)if(sub[i].known && !sub[i].pending)mask|=uint64_t{1}<<i;
        return mask;
    }
    bool Uniform(uint32_t& value)const{
        if(rejected || !count || count>64)return false;
        value=sub[0].value;
        for(unsigned i=0;i<count;++i)if(!sub[i].known || sub[i].pending || sub[i].value!=value)return false;
        return true;
    }
    const char* Reason()const{
        if(rejected)return rejected;
        if(!count || count>64)return "unknown-subresource-count";
        for(unsigned i=0;i<count;++i)if(sub[i].pending)return "split-pending";
        for(unsigned i=0;i<count;++i)if(!sub[i].known)return "unobserved-subresource";
        uint32_t value=0;return Uniform(value)?"uniform-observed":"mixed-subresource-states";
    }
};
}
