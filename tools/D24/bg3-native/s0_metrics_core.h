#pragma once
#include <array>
#include <cstdint>

// Pure scheduling/validation logic; the addon and the host counterexamples use
// this exact helper. No API calls, waits, or implicit resource retirement.
namespace D18S0Metrics {
constexpr unsigned Slots=4, Target=32, Warmup=30, MaxArms=4, MaxAttempts=256;
constexpr uint64_t IntervalMs=100, DeadlineMs=30000;
enum class State { Free, Recording, Pending, Poisoned };
struct Frame {
 uint64_t frame=0,generation=0,feature=0,context=0;
 unsigned width=0,height=0,phf=0,prefilter=0;
};
struct Result {
 std::array<uint64_t,6> ticks{};
 uint64_t frequency=0;
 unsigned mask=0;
 bool disjoint=false,existingCompletion=false;
};
inline const char* validate(const Result& r){
 if(r.mask!=127)return "incomplete_getdata_mask";
 if(!r.existingCompletion)return "existing_completion_missing";
 if(r.disjoint)return "timestamp_disjoint";
 if(!r.frequency)return "zero_frequency";
 for(unsigned i=1;i<6;++i)if(r.ticks[i]<r.ticks[i-1])return "nonmonotonic_ticks";
 const auto span=r.ticks[5]-r.ticks[0];
 if(!span||span>=r.frequency)return "span_not_below_1000ms";
 return nullptr;
}
struct Slot {
 State state=State::Free;Frame frame{};
 unsigned arm=0,sample=0,written=0;
 bool existingCompletion=false;
};
struct Control {
 std::array<Slot,Slots> slots{};
 bool markerWasOn=false,active=false,fatal=false;
 unsigned arms=0,saved=0,total=0,warmup=0,attempts=0,fullSkips=0,ineligibleSkips=0;
 uint64_t started=0,lastSample=0;
 unsigned pending() const {unsigned n=0;for(const auto& s:slots)if(s.state!=State::Free)++n;return n;}
 template<class Sink> void stop(const char* phase,const char* reason,Sink& sink){
  if(active){active=false;sink.control(phase,reason,*this);}
 }
 template<class Sink> void observe(bool enabled,bool marker,uint64_t now,Sink& sink){
  const bool rising=marker&&!markerWasOn;markerWasOn=marker;
  if(active&&now-started>=DeadlineMs)stop("timed_out","arm_deadline",sink);
  if(active&&(!enabled||!marker))stop("stopped",enabled?"marker_removed":"diagnostics_disabled",sink);
  if(!rising||!enabled)return;
  if(fatal||arms>=MaxArms||total>=MaxArms*Target||pending()){
   sink.control("rejected",fatal?"diagnostic_circuit_breaker":arms>=MaxArms?"process_arm_budget":"pending_from_prior_arm",*this);return;
  }
  ++arms;saved=warmup=attempts=fullSkips=ineligibleSkips=0;started=now;lastSample=0;active=true;
  sink.control("armed","marker_rising_edge",*this);
 }
 template<class Sink> int acquire(bool eligible,uint64_t now,const Frame& f,Sink& sink){
  if(!active)return -1;
  if(now-started>=DeadlineMs){stop("timed_out","arm_deadline",sink);return -1;}
  if(!eligible){if(ineligibleSkips!=UINT32_MAX)++ineligibleSkips;return -1;}
  if(warmup<Warmup){if(++warmup==Warmup)sink.control("warmup","eligible_frames_complete",*this);return -1;}
  if(lastSample&&now-lastSample<IntervalMs)return -1;
  if(saved+pending()>=Target)return -1;
  if(attempts>=MaxAttempts){if(!pending())stop("stopped","attempt_budget",sink);return -1;}
  int index=-1;for(unsigned i=0;i<Slots;++i)if(slots[i].state==State::Free){index=int(i);break;}
  if(index<0){if(fullSkips!=UINT32_MAX)++fullSkips;return -1;}
  auto& s=slots[index];s={};s.state=State::Recording;s.frame=f;s.arm=arms;s.sample=++attempts;lastSample=now;return index;
 }
 template<class Sink> void ready(unsigned index,const Result& r,Sink& sink){
  auto& s=slots[index];const char* reason=validate(r);
  if(!reason&&(!active||s.arm!=arms))reason="arm_closed_before_readback";
  sink.timing(s,r,reason);
  if(!reason){++saved;++total;}
  s={};
  if(active&&saved==Target)stop("complete","target_completed",sink);
  else if(active&&attempts>=MaxAttempts&&!pending())stop("stopped","attempt_budget",sink);
 }
 template<class Sink> void poison(unsigned index,const char* reason,Sink& sink){
  auto& s=slots[index];Result r{};sink.timing(s,r,reason);s.state=State::Poisoned;fatal=true;
  stop("stopped","diagnostic_circuit_breaker",sink);
 }
};
}
