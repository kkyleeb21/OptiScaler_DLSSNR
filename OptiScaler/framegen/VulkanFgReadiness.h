#pragma once
namespace VulkanFg
{
struct Readiness
{
    static constexpr unsigned required=32;
    unsigned consecutive=0;
    void Reset(){consecutive=0;}
    bool Observe(bool valid)
    {
        if(!valid){Reset();return false;}
        if(consecutive<required)++consecutive;
        return consecutive==required;
    }
};
}
