#pragma once
#include <cstdint>
// Four eight-frame bursts across >=4.5 seconds; bounded even with toggle held.
struct CaptureSchedule {
 bool armed=false,finished=false;unsigned selected=0,target=32;uint64_t start=0;
 void reset(){armed=false;finished=false;selected=0;start=0;target=32;}
 bool configure(unsigned count){if(armed||(count!=8&&count!=32))return false;target=count;return true;}
 bool select(bool enabled,uint64_t now){
  if(!enabled){reset();return false;}
  if(!armed){armed=true;start=now;}
  if(finished)return false;
  if(selected>=target||now-start>=(target==8?30000:15000)){finished=true;return false;}
  if(target==32&&now-start<uint64_t(selected/8)*1500)return false;
  ++selected;return true;
 }
};
