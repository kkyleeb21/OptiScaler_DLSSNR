#pragma once
#include <algorithm>
#include <cstdint>

namespace DlssNr::ColourCapture {
enum class Profile : unsigned {Full3,Center8,Full8};
inline const char* ProfileName(Profile p){switch(p){case Profile::Full3:return "full3";case Profile::Center8:return "center8";case Profile::Full8:return "full8";}return "unknown";}
inline const char* ProfileScope(Profile p){return p==Profile::Center8?"center_region":"full_frame";}
inline unsigned FrameTarget(Profile p){return p==Profile::Full3?3u:8u;}
struct Layout {
 bool valid=false,burst=false;unsigned target=0,slots=0;uint32_t width=0,height=0,x=0,y=0;
 uint64_t frameBytes=0,bytes=0;const char* reason="invalid_profile";
};
inline Layout CaptureLayout(Profile p,uint32_t w,uint32_t h){
 Layout l;if(p!=Profile::Full3&&p!=Profile::Center8&&p!=Profile::Full8)return l;
 l.target=FrameTarget(p);l.burst=p==Profile::Center8;l.slots=l.burst?8u:1u;
 if(!w||!h){l.reason="empty_dimensions";return l;}
 if(p==Profile::Full8&&(w!=3840u||h!=2160u)){l.reason="full8_requires_3840x2160";return l;}
 l.width=l.burst?std::min(w,512u):w;l.height=l.burst?std::min(h,512u):h;
 l.x=(w-l.width)/2;l.y=(h-l.height)/2;
 constexpr uint64_t limit=512ull*1024*1024;
 const uint64_t pixels=uint64_t(l.width)*l.height;
 if(pixels>limit/(48u*l.slots)){l.reason="readback_exceeds_512_mib";return l;}
 l.frameBytes=pixels*48;l.bytes=l.frameBytes*l.slots;l.valid=true;l.reason="accepted";return l;
}
}
