#pragma once
#include "D18ContextTracking.h"
#include <atomic>
#include <filesystem>
#include <cstdio>
#include <share.h>

namespace D18ExecutionTrace {
inline std::filesystem::path Root(){
 static const auto root=[](){static char anchor;HMODULE module=nullptr;wchar_t name[32768]{};
  if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&anchor),&module)||!GetModuleFileNameW(module,name,32768))return std::filesystem::path{};
  return std::filesystem::path(name).parent_path();}();return root;
}
inline FILE* File(){static FILE* file=[](){auto root=Root();if(root.empty()||GetFileAttributesW((root/L"D18UiDiagnostics.enabled").c_str())==INVALID_FILE_ATTRIBUTES)return static_cast<FILE*>(nullptr);return _wfsopen((root/L"D18ExecutionTrace.jsonl").c_str(),L"wb",_SH_DENYNO);}();return file;}
inline std::atomic<unsigned> records{0},windows{0},uiBudget{2},srBudget{2},executeBudget{2};
inline void Point(const char* stage,ID3D11DeviceContext* c=nullptr,long code=0,unsigned long long a=0,unsigned long long b=0){
 auto f=File();if(!f||records.fetch_add(1)>=4096)return;
 fprintf(f,"{\"event\":\"d18_execution\",\"stage\":\"%s\",\"tick\":%llu,\"thread\":%lu,\"execute_depth\":%u,\"context\":\"%p\",\"context_type\":%u,\"code\":%ld,\"a\":%llu,\"b\":%llu}\n",stage,GetTickCount64(),GetCurrentThreadId(),D18InputProbe::executeDepth,c,c?unsigned(c->GetType()):~0u,code,a,b);fflush(f);
}
inline void Arm(bool opening){if(!File()||windows.fetch_add(1)>=6)return;uiBudget=8;srBudget=8;executeBudget=4;Point(opening?"ui_open_requested":"ui_close_requested");}
inline void ArmSr(bool enabling){if(!File()||windows.fetch_add(1)>=6)return;uiBudget=8;srBudget=8;executeBudget=4;Point(enabling?"sr_enable_requested":"sr_disable_requested");}
inline void ArmStage(){static std::atomic<unsigned> stageWindows{0};if(!File()||stageWindows.fetch_add(1)>=16)return;uiBudget=8;srBudget=8;executeBudget=4;}
inline bool Take(std::atomic<unsigned>& budget){if(!File())return false;auto n=budget.load();while(n&&!budget.compare_exchange_weak(n,n-1)){}return n!=0;}
struct Span {
 bool active;ID3D11DeviceContext* context;const char* end;
 Span(std::atomic<unsigned>& budget,const char* begin,const char* finish,ID3D11DeviceContext* c):active(Take(budget)),context(c),end(finish){if(active)Point(begin,c);}
 ~Span(){if(active)Point(end,context);}
 void Step(const char* name,long code=0,unsigned long long a=0,unsigned long long b=0){if(active)Point(name,context,code,a,b);}
};
}
