#pragma once
#include <bit>
#include <cstdint>
namespace DlssNr::HighResolution {
// Only pre-dispatch request rejection is recoverable by changing that request.
// Device loss and execution failures retain their separate explicit retry policy.
struct RejectedRequest {
    bool armed=false,high=false;
    float scale=1.25f;
    void Block(bool requested,float factor){armed=true;high=requested;scale=factor;}
    void Clear(){armed=false;}
    bool Changed(bool requested,float factor,bool deviceLost)const {
        return armed && !deviceLost && (high!=requested || (requested && std::bit_cast<uint32_t>(scale)!=std::bit_cast<uint32_t>(factor)));
    }
};
}
