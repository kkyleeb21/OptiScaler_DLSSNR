#pragma once
#include <d3d11_1.h>
#include "D18ExecutionTrace.h"
#include <mutex>
// Serializes D18 context work only; does not claim to lock game commands.
// Nonblocking acquisition avoids inversion with existing input/present locks.
namespace D18ContextTransaction {
inline std::recursive_mutex mutex;
inline std::atomic<unsigned long long> sequence{0};
inline std::atomic<unsigned> budget[4]{},busyBudget[4]{},arms{0};
inline void Arm(){if(arms.fetch_add(1)<16)for(unsigned i=1;i<4;++i){budget[i]=24;busyBudget[i]=4;}}
inline bool Take(unsigned owner,bool busy=false){if(owner<1||owner>3)return false;auto& b=busy?busyBudget[owner]:budget[owner];auto n=b.load();while(n&&!b.compare_exchange_weak(n,n-1)){}return n!=0;}
class Scope {
 std::unique_lock<std::recursive_mutex> lock;
 ID3D11DeviceContext* context;
 unsigned long long id=0;
 bool traced=false;
 unsigned owner;
 public:
 Scope(ID3D11DeviceContext* c,unsigned owner):lock(mutex,std::try_to_lock),context(c),owner(owner){
  if(!lock){traced=Take(owner,true);if(traced){id=++sequence;D18ExecutionTrace::Point("context_transaction_busy",c,0,owner,id);}}else if(owner!=2)StartSampling();
 }
 void StartSampling(){if(!lock||traced)return;traced=Take(owner);if(traced){id=++sequence;D18ExecutionTrace::Point("context_transaction_enter",context,0,owner,id);}}
 void Step(const char* name){if(traced&&lock)D18ExecutionTrace::Point(name,context,0,owner,id);}
 ~Scope(){if(traced&&lock)D18ExecutionTrace::Point("context_transaction_exit",context,0,id,0);}
 void State(ID3DDeviceContextState* saved,ID3DDeviceContextState* active){if(traced&&lock)D18ExecutionTrace::Point("context_transaction_state",context,0,reinterpret_cast<unsigned long long>(saved),reinterpret_cast<unsigned long long>(active));}
 explicit operator bool()const{return lock.owns_lock();}
 Scope(const Scope&)=delete;Scope& operator=(const Scope&)=delete;
};
}
