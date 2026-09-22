#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>

namespace capture::control {
enum class Api {Dx12,Dx11,Vulkan};
enum class Phase : uint32_t {Idle,WaitingNr,WaitingMenu,Delay,Recording,WaitingGpu,Writing,Draining,Complete,Cancelled,Failed,TimedOut};
inline const char* Name(Phase p){switch(p){
 case Phase::Idle:return "idle";case Phase::WaitingNr:return "waiting_nr";case Phase::WaitingMenu:return "waiting_menu";
 case Phase::Delay:return "delay";case Phase::Recording:return "recording";case Phase::WaitingGpu:return "waiting_gpu";
 case Phase::Writing:return "writing";case Phase::Draining:return "draining";case Phase::Complete:return "complete";
 case Phase::Cancelled:return "cancelled";case Phase::Failed:return "failed";case Phase::TimedOut:return "timed_out";}return "unknown";}
struct Snapshot {Phase phase=Phase::Idle;uint64_t request=0,requestedAt=0,closedAt=0,revision=0;uint32_t target=0,saved=0;
 bool active=false,started=false,stop=false;Phase stopResult=Phase::Cancelled;std::string reason;};
using Observer=void(*)(Api,const Snapshot&);
inline std::atomic<Observer> observer{nullptr};
class Session {
 std::mutex mutex;Snapshot s;uint64_t next=0;Api api;
 void emit(){if(auto fn=observer.load())fn(api,s);}
 void set(Phase p,const char* why){if(s.phase!=p||s.reason!=why){s.phase=p;s.reason=why;++s.revision;emit();}}
 // Even an unstarted request waits for its owner to clear the adapter's arm flag.
 // Otherwise a new request could race old-request cancellation and lose its arm.
 void stop(Phase p,const char* why){s.stop=true;s.stopResult=p;set(Phase::Draining,why);}
 void refresh(uint64_t now,bool nr,bool menu){
  if(!s.active||s.stop)return;
  if(now-s.requestedAt>=60000){stop(Phase::TimedOut,"request_deadline_60s");return;}
  if(s.started){if(!nr)stop(Phase::Cancelled,"nr_disabled");else if(menu)stop(Phase::Cancelled,"menu_reopened");return;}
  if(!nr){s.closedAt=0;set(Phase::WaitingNr,"enable_nr");}
  else if(menu){s.closedAt=0;set(Phase::WaitingMenu,"close_menu");}
  else {if(!s.closedAt)s.closedAt=now;set(Phase::Delay,now-s.closedAt<3000?"settling_after_menu":"waiting_backend_callback");}
 }
public:
 explicit Session(Api id=Api::Dx12):api(id){}
 bool Request(uint32_t target,uint64_t now){std::lock_guard l(mutex);if(s.active||!target)return false;auto rev=s.revision;s={};s.request=++next;s.revision=rev+1;s.target=target;s.requestedAt=now;s.active=true;s.phase=Phase::WaitingNr;s.reason="waiting_backend";emit();return true;}
 bool Permit(uint64_t now,bool nr,bool menu){std::lock_guard l(mutex);refresh(now,nr,menu);if(!s.active||s.stop)return false;
  if(!s.started){if(!nr||menu||!s.closedAt||now-s.closedAt<3000)return false;s.started=true;set(Phase::Recording,"backend_started");}return true;}
 Snapshot Read(uint64_t now,bool nr,bool menu){std::lock_guard l(mutex);refresh(now,nr,menu);return s;}
 Snapshot Peek(){std::lock_guard l(mutex);return s;}
 void Cancel(){std::lock_guard l(mutex);if(s.active&&!s.stop)stop(Phase::Cancelled,"user_cancelled");}
 void Abort(const char* reason){std::lock_guard l(mutex);if(s.active&&!s.stop)stop(Phase::Failed,reason);}
 bool Stopping(){std::lock_guard l(mutex);return s.stop;}
 void Target(uint32_t target){std::lock_guard l(mutex);if(s.active&&target){s.target=target;++s.revision;emit();}}
 void Progress(Phase p,uint32_t saved=0){std::lock_guard l(mutex);if(!s.active)return;const bool changed=s.saved!=saved;s.saved=saved;
  if(!s.stop&&(s.phase!=p||s.reason!=Name(p)))set(p,Name(p));else if(changed){++s.revision;emit();}}
 void Finish(Phase result,const char* reason,uint32_t saved=0){std::lock_guard l(mutex);if(!s.active)return;
  s.saved=saved;s.active=false;const auto why=s.stop?s.reason:std::string(reason);set(s.stop?s.stopResult:result,why.c_str());}
};
inline std::array<Session,3> sessions{Session{Api::Dx12},Session{Api::Dx11},Session{Api::Vulkan}};
inline std::atomic<bool> menuVisible{false};
inline Session& For(Api api){return sessions[static_cast<unsigned>(api)];}
}
