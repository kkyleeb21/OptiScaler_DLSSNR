#pragma once
#include <array>
#include <cstdio>
#include <cstdarg>
#include <cstring>

// Serialized by the Session mutex. Every native text write uses this budget,
// including capture metadata. Keep the FILE alive until process shutdown.
static constexpr unsigned long long nativeLogBudget=16ull*1024*1024;
static unsigned long long nativeLogBytes=0,nativeLogDropped=0;
static bool nativeLogClosed=false;
static int logPrint(FILE* file,const char* format,...){
 if(!file)return 0;
 va_list args;va_start(args,format);va_list countArgs;va_copy(countArgs,args);
 const int size=_vscprintf(format,countArgs);va_end(countArgs);
 if(size<0||nativeLogClosed||nativeLogBytes+static_cast<unsigned>(size)>nativeLogBudget-128){
  ++nativeLogDropped;va_end(args);
  if(!nativeLogClosed){nativeLogClosed=true;const int written=fprintf(file,"\n{\"event\":\"native_log_budget_reached\",\"limit_bytes\":%llu}\n",nativeLogBudget);
   if(written>0)nativeLogBytes+=static_cast<unsigned>(written);fflush(file);}
  return 0;}
 const int written=vfprintf(file,format,args);va_end(args);
 if(written>0)nativeLogBytes+=static_cast<unsigned>(written);
 return written;
}
struct NativeEventLimit { char name[96]{};unsigned count=0;unsigned long long tick=0; };
static std::array<NativeEventLimit,128> nativeEventLimits{};
static bool allowNativeEvent(const char* name,unsigned long long now){
 for(auto& slot:nativeEventLimits){
  if(!slot.name[0]){strncpy_s(slot.name,name,_TRUNCATE);slot.count=1;slot.tick=now;return true;}
  if(!strcmp(slot.name,name)){
   if(slot.count<4){++slot.count;slot.tick=now;return true;}
   if(now-slot.tick>=5000){slot.tick=now;return true;}
   return false;
  }
 }
 return false;
}
