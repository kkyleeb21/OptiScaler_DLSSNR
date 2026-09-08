#pragma once
#include <cmath>
#include <cstdint>
struct NrJitterSample {
 bool eligible=false,reset=false;float x=0,y=0,sx=1,sy=1;
 uint64_t call=0;unsigned epoch=0,rw=0,rh=0,mw=0,mh=0,ow=0,oh=0;
 const void* owner=nullptr;const void* context=nullptr;
};
struct NrJitterPlan {bool active=false,apply=false,reset=false;float dx=0,dy=0;};
struct NrJitterHistory {
 NrJitterSample previous{};bool valid=false,active=false;
 void clear(){valid=active=false;}
 NrJitterPlan plan(const NrJitterSample& s) const {
  const bool okay=s.eligible&&std::isfinite(s.x)&&std::isfinite(s.y)&&std::isfinite(s.sx)&&std::isfinite(s.sy)&&std::abs(s.sx)>1e-6f&&std::abs(s.sy)>1e-6f;
  NrJitterPlan p{};if(!okay){p.reset=active;return p;}p.active=true;
  const auto& b=previous;
  p.apply=valid&&!s.reset&&s.call==b.call+1&&s.epoch==b.epoch&&s.owner==b.owner&&s.context==b.context&&s.rw==b.rw&&s.rh==b.rh&&s.mw==b.mw&&s.mh==b.mh&&s.ow==b.ow&&s.oh==b.oh&&s.sx==b.sx&&s.sy==b.sy;
  p.reset=!p.apply;
  if(p.apply){p.dx=(s.x-b.x)/s.sx;p.dy=(s.y-b.y)/s.sy;
   if(!std::isfinite(p.dx)||!std::isfinite(p.dy)){p.apply=false;p.reset=true;p.dx=p.dy=0;}}
  return p;
 }
 void commit(const NrJitterSample& s,const NrJitterPlan& p){previous=s;valid=active=p.active;}
};
