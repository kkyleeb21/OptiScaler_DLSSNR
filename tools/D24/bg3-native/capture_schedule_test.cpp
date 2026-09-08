#include "capture_schedule.h"
#include <cassert>
#include <cstdio>
int main(){
 CaptureSchedule c;
 assert(!c.select(false,0));
 for(unsigned burst=0;burst<4;++burst){
  for(unsigned i=0;i<8;++i)assert(c.select(true,100+burst*1500+i*10));
  assert(!c.select(true,100+burst*1500+100));
 }
 assert(c.selected==32);assert(!c.select(true,100000));
 assert(!c.select(false,100001));assert(c.select(true,100002));assert(c.selected==1);
 assert(!c.select(true,115002));assert(c.finished);
 puts("PASS: 4x8 bursts; deadline; held-toggle cap; explicit rearm");
}
