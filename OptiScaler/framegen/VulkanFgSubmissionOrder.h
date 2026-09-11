#pragma once
#include <cstdint>

namespace VulkanFg
{
// Only actual queue submission order establishes the relationship between
// independently recorded depth and SR commands. CPU recording order does not.
struct SubmissionOrder
{
    unsigned depthCount=0,srCount=0;
    uintptr_t depthQueue=0,srQueue=0;
    bool invalid=false;
    void Submit(uintptr_t queue,bool depth,bool sr,bool success)
    {
        if(!depth && !sr)return;
        invalid|=!success;
        if(depth){++depthCount;depthQueue=queue;invalid|=depthCount!=1 || srCount!=0;}
        if(sr){++srCount;srQueue=queue;invalid|=srCount!=1 || depthCount!=1 || depthQueue!=queue;}
    }
    bool Ready(uintptr_t presentQueue)const
    {return !invalid && depthCount==1 && srCount==1 && depthQueue==presentQueue && srQueue==presentQueue;}
};
}
