#pragma once
#include "NativeScaleRequest.h"
#include "WildlandsScaleProfile.h"
#include <windows.h>
#include <array>
#include <cstring>
#include <cstdio>
#include <share.h>
#include <string>

namespace DlssNr::WildlandsScale {
using Phase=NativeScaleRequest::Phase;
using Callback=void(*)(void*);
using Setter=void(*)(void*,float);
using Apply=void(*)(void*);
// This adapter owns no engine allocation. The native WndProc copies this exact
// payload into its own queue; the native callback receives vtable + payload.
struct Job {void* vtable;void* settings;HWND window;UINT message;UINT padding;LPARAM parameter;};
static_assert(offsetof(Job,settings)==8&&offsetof(Job,window)==16&&offsetof(Job,message)==24&&offsetof(Job,parameter)==32&&sizeof(Job)==40);
inline constexpr UINT SettingsMessage=0x8000;
inline constexpr LPARAM RequestTag=0x4431385352434847LL;
inline constexpr std::size_t SettingsSize=0x8e0;
struct Bindings {
 unsigned char* applied=nullptr;
 unsigned char* editing=nullptr;
 const unsigned char* nativePending=nullptr;
 const float* renderScale=nullptr;
 HWND window=nullptr;
 Setter setter=nullptr;
 Apply apply=nullptr;
 Callback original=nullptr;
};
inline Bindings bindings;
inline NativeScaleRequest request;
alignas(16) inline std::array<unsigned char,SettingsSize> payload{};
// Only the editor's scale may be synchronized; unrelated pending fields are
// never submitted or overwritten by this adapter.
inline std::array<unsigned char,12> editorAnchor{};
inline bool editorScaleUnchanged=false;
inline std::atomic<bool> editorScalePreserved{false};
inline std::atomic<bool> available{false};
inline std::atomic<unsigned> failure{0};
inline std::atomic<float> actualScale{1.0f};
inline std::atomic<float> minimumScale{1.0f},maximumScale{1.0f};
inline std::atomic<unsigned> windowThread{0};
inline FILE* events=nullptr;
inline SRWLOCK eventLock=SRWLOCK_INIT;
inline unsigned eventCount=0;
inline void* volatile* callbackSlot=nullptr;
inline bool identityChecked=false,identityValid=false;
inline float Scale(const void* settings){float f=0;std::memcpy(&f,static_cast<const unsigned char*>(settings)+0x6c,4);return f;}
inline void Event(const char* stage){
 if(!events)return;
 AcquireSRWLockExclusive(&eventLock);
 if(eventCount++<128){std::fprintf(events,"{\"event\":\"native_scale\",\"stage\":\"%s\",\"tick\":%llu,\"thread\":%lu,\"window_thread\":%u,\"callback_thread\":%u,\"generation\":%llu,\"phase\":%u,\"requested\":%.9g,\"accepted\":%.9g,\"input_width\":%u,\"input_height\":%u,\"failure\":%u}\n",stage,GetTickCount64(),GetCurrentThreadId(),windowThread.load(),request.callbackThread.load(),static_cast<unsigned long long>(request.generation.load()),static_cast<unsigned>(request.phase.load()),static_cast<double>(request.requested.load()),static_cast<double>(request.accepted.load()),request.inputWidth.load(),request.inputHeight.load(),failure.load());std::fflush(events);}
 ReleaseSRWLockExclusive(&eventLock);
}
inline void Fail(unsigned code){failure=code;request.phase=Phase::Failed;Event("failed");}
inline bool Readable(const void* pointer,SIZE_T bytes){
 auto address=reinterpret_cast<ULONG_PTR>(pointer);if(!address||address+bytes<address)return false;
 while(bytes){MEMORY_BASIC_INFORMATION info{};if(!VirtualQuery(reinterpret_cast<void*>(address),&info,sizeof(info))||info.State!=MEM_COMMIT||(info.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return false;
  const auto end=reinterpret_cast<ULONG_PTR>(info.BaseAddress)+info.RegionSize;
  if(end<=address)return false;const auto take=(bytes<end-address)?bytes:end-address;address+=take;bytes-=take;
 }return true;
}
enum class SettingsState : unsigned { Ready, Unreadable, NativePending, InvalidDimensions, InvalidScale };
inline SettingsState CheckSettings(){
 if(!Readable(bindings.applied,SettingsSize)||!Readable(bindings.editing,SettingsSize)||!Readable(bindings.nativePending,1)||!Readable(bindings.renderScale,4))return SettingsState::Unreadable;
 const float f=Scale(bindings.applied);unsigned width=0,height=0;
 std::memcpy(&width,bindings.applied+0x3c,4);std::memcpy(&height,bindings.applied+0x40,4);
 if(*bindings.nativePending)return SettingsState::NativePending;
 if(!width||!height||width>16384||height>16384)return SettingsState::InvalidDimensions;
 if(!std::isfinite(f)||f<.25f||f>2.0f)return SettingsState::InvalidScale;
 return SettingsState::Ready;
}
inline bool SettingsReady(){return CheckSettings()==SettingsState::Ready;}
inline void RefreshBounds(){
 if(!SettingsReady())return;
 unsigned w=0,h=0;std::memcpy(&w,bindings.applied+0x3c,4);std::memcpy(&h,bindings.applied+0x40,4);
 // Native setter arithmetic for this profile, including the 10-percent bound
 // rounding. The selected scale itself is continuous, not rounded to a preset.
 unsigned lower=static_cast<unsigned>(std::sqrt(2073600.0f/static_cast<float>(w*h))*50.0f);
 if(lower%10)lower+=10-lower%10;lower=lower<50?50:lower>100?100:lower;
 unsigned upper=819200/(w>h?w:h);if(upper>200)upper=200;
 minimumScale=static_cast<float>(lower)*.01f;maximumScale=static_cast<float>(upper)*.01f;
 actualScale=Scale(bindings.applied);
}
inline bool SameSettingWord(const unsigned char* a,const unsigned char* b){
 unsigned x=0,y=0;std::memcpy(&x,a,4);std::memcpy(&y,b,4);if(x==y)return true;
 float u=0,v=0;std::memcpy(&u,a,4);std::memcpy(&v,b,4);
 // Existing captures contain one-ULP float save/parse differences. Small enums,
 // packed booleans and meaningful slider changes must still compare exactly.
 return std::isfinite(u)&&std::isfinite(v)&&std::abs(u)>=0.0001f&&std::abs(v)>=0.0001f&&
  std::abs(u)<=1000.0f&&std::abs(v)<=1000.0f&&((x>y?x-y:y-x)<=2);
}
inline bool EditorScaleMatchesAnchor(){
 return !std::memcmp(bindings.editing+0x3c,editorAnchor.data(),8)&&
  SameSettingWord(bindings.editing+0x6c,editorAnchor.data()+8);
}
inline void ContextEvent(const Job* job,const char* stage){
 if(!events)return;
 const auto state=CheckSettings();unsigned count=0,first=0,a=0,b=0;
 if(state!=SettingsState::Unreadable){
  for(unsigned i=4;i<0xc4;i+=4)if(!SameSettingWord(bindings.applied+i,bindings.editing+i)){
   if(!count){first=i;std::memcpy(&a,bindings.applied+i,4);std::memcpy(&b,bindings.editing+i,4);}++count;
  }
 }
 AcquireSRWLockExclusive(&eventLock);
 if(eventCount++<128){std::fprintf(events,"{\"event\":\"native_scale_context\",\"stage\":\"%s\",\"tick\":%llu,\"generation\":%llu,\"settings_state\":%u,\"window_checked\":%s,\"window_matches\":%s,\"pending\":%u,\"different_words\":%u,\"first_offset\":%u,\"applied_bits\":%u,\"editing_bits\":%u}\n",stage,GetTickCount64(),static_cast<unsigned long long>(request.generation.load()),static_cast<unsigned>(state),job?"true":"false",job&&job->window==bindings.window?"true":"false",state==SettingsState::Unreadable?0u:static_cast<unsigned>(*bindings.nativePending),count,first,a,b);std::fflush(events);}
 ReleaseSRWLockExclusive(&eventLock);
}
inline void OnNativeJob(void* opaque){
 auto* job=static_cast<Job*>(opaque);
 if(job->settings!=payload.data()||job->message!=SettingsMessage){bindings.original(opaque);return;}
 if(job->parameter==RequestTag){
  // A late private message is consumed without applying or reusing its payload.
  if(!request.BeginApply(GetCurrentThreadId())){Event("late_request_ignored");return;}
  ContextEvent(job,"before_apply");
  if(job->window!=bindings.window){failure=6;request.Reject();Event("window_mismatch");return;}
  const auto settingsState=CheckSettings();
  if(settingsState!=SettingsState::Ready){
   failure=6+static_cast<unsigned>(settingsState);request.Reject();Event("native_settings_not_ready");return;
  }
  // Start from applied settings, not the editor. Pending changes in the editor
  // therefore cannot leak into Apply and need not block a scale-only request.
  std::memcpy(payload.data(),bindings.applied,SettingsSize);
  std::memcpy(editorAnchor.data(),payload.data()+0x3c,8);
  std::memcpy(editorAnchor.data()+8,payload.data()+0x6c,4);
  editorScaleUnchanged=EditorScaleMatchesAnchor();
  bindings.setter(payload.data(),request.requested.load());
  const float accepted=Scale(payload.data());
  // The adapter respects native limits. A clamped request is rejected, not
  // advertised as an engine quality level which the engine did not accept.
  if(!std::isfinite(accepted)||std::abs(accepted-request.requested.load())>0.0001f){failure=2;request.Reject();Event("native_scale_rejected");return;}
  if(!request.NativeSubmitted(accepted))return;
  Event("native_apply_begin");
  bindings.apply(payload.data()); // Runs in the original engine callback queue.
  Event("native_apply_returned");
  return; // Apply itself posts the normal 0x8000 completion message (lParam=0).
 }
 // The second native message uses the same process-lifetime settings storage.
 // Complete the original callback before considering the native phase finished.
 bindings.original(opaque);
 if(job->parameter==0&&request.phase==Phase::NativePending){
  if(!SettingsReady()||std::abs(Scale(bindings.applied)-request.accepted.load())>0.0001f||
     std::abs(*bindings.renderScale-request.accepted.load())>0.0001f){Fail(3);return;}
  // Merge only an unchanged editor scale with matching display dimensions. A
  // pending or newly edited scale/resolution remains exactly as the user left it.
  const bool sync=editorScaleUnchanged&&EditorScaleMatchesAnchor();
  if(sync)bindings.setter(bindings.editing,request.accepted.load());
  editorScalePreserved=!sync;Event(sync?"editor_scale_synchronized":"pending_editor_scale_preserved");
  actualScale=Scale(bindings.applied);request.NativeFinished();Event("native_callback_finished");
 }
}
inline bool Request(float ratio){
 if(!available||!request.Request(ratio,GetTickCount64()))return false;
 failure=0;editorScalePreserved=false;Event("requested");return true;
}
inline void Tick(bool srDrained,bool postDrained){
 if(!available)return;
 if(request.Expire(GetTickCount64())){failure=4;ContextEvent(nullptr,"request_timeout");Event("timeout_payload_frozen");return;}
 const auto phase=request.phase.load();
 if(phase==Phase::Idle||phase==Phase::Active||phase==Phase::EngineReady||phase==Phase::Rejected||phase==Phase::Draining)RefreshBounds();
 if(request.phase!=Phase::Draining||!srDrained||!postDrained)return;
 if(!SettingsReady())return;
 // The payload remains valid even if a different mod replaces the callback.
 // Refresh the authoritative snapshot again on the engine thread before Apply.
 std::memcpy(payload.data(),bindings.applied,SettingsSize);
 if(!request.Queue(srDrained,postDrained))return;
 if(!PostMessageW(bindings.window,SettingsMessage,reinterpret_cast<WPARAM>(payload.data()),RequestTag)){Fail(5);return;}
 Event("queued_after_gpu_drain");
}
inline void ObserveInput(unsigned w,unsigned h,unsigned ow,unsigned oh){
 // Alignment is an adapter bound, not a replacement size. Report real dimensions.
 if(request.ObserveInput(w,h,ow,oh,16))Event("engine_input_observed");
}
inline void SrCompleted(std::uint64_t serial,unsigned w,unsigned h){if(request.SrCompleted(serial,w,h))Event("sr_gpu_completed");}
inline void TryInstall(const wchar_t* logPath){
 if(available)return;
 auto* base=reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
 if(!identityChecked){
  identityChecked=true;wchar_t path[MAX_PATH]{};
  if(!GetModuleFileNameW(nullptr,path,MAX_PATH))return;
  const wchar_t* name=wcsrchr(path,L'\\');if(!name||_wcsicmp(name+1,L"GRW.exe"))return;
  if(!Readable(base,sizeof(IMAGE_DOS_HEADER)))return;
  auto* dos=reinterpret_cast<IMAGE_DOS_HEADER*>(base);if(dos->e_magic!=IMAGE_DOS_SIGNATURE||dos->e_lfanew<0||dos->e_lfanew>0x100000)return;
  auto* nt=reinterpret_cast<IMAGE_NT_HEADERS64*>(base+dos->e_lfanew);
  if(!Readable(nt,sizeof(*nt))||nt->Signature!=IMAGE_NT_SIGNATURE||nt->OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC||
   nt->FileHeader.TimeDateStamp!=WildlandsScaleProfile::TimeStamp||nt->OptionalHeader.SizeOfImage!=WildlandsScaleProfile::ImageSize)return;
  for(const auto& f:WildlandsScaleProfile::Fragments){
   if(!Readable(base+f.rva,f.size))return;
   unsigned long long hash=14695981039346656037ULL;
   for(unsigned i=0;i<f.size;++i)hash=(hash^base[f.rva+i])*1099511628211ULL;
   if(hash!=f.hash)return;
  }
  identityValid=true;
 }
 if(!identityValid||!Readable(base+0x4d5b030,sizeof(void*)))return;
 unsigned char* graphics=nullptr;std::memcpy(&graphics,base+0x4d5b030,sizeof(graphics));
 if(!Readable(graphics,0x768))return;
 unsigned char* render=nullptr;HWND window=nullptr;
 std::memcpy(&render,graphics+0x30,sizeof(render));std::memcpy(&window,graphics+0x760,sizeof(window));
 DWORD pid=0;const DWORD tid=GetWindowThreadProcessId(window,&pid);
 if(!Readable(render,0x20)||!tid||pid!=GetCurrentProcessId())return;
 auto* slot=reinterpret_cast<void* volatile*>(base+0x38bc808);
 if(!Readable(const_cast<void**>(slot),sizeof(void*))||*slot!=base+0x154b20)return;
 bindings={base+0x4d5b120,base+0x4d5ba00,base+0x4d5b039,reinterpret_cast<float*>(render+0x1c),window,
  reinterpret_cast<Setter>(base+0x1389e00),reinterpret_cast<Apply>(base+0x1372170),reinterpret_cast<Callback>(base+0x154b20)};
 if(!SettingsReady())return;
 // Keep borrowed settings and callback storage alive through delayed messages.
 HMODULE self=nullptr;
 if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&OnNativeJob),&self))return;
 DWORD protection=0;if(!VirtualProtect(const_cast<void**>(slot),sizeof(void*),PAGE_READWRITE,&protection))return;
 void* previous=InterlockedCompareExchangePointer(slot,reinterpret_cast<void*>(&OnNativeJob),reinterpret_cast<void*>(bindings.original));
 DWORD ignored=0;const bool restored=VirtualProtect(const_cast<void**>(slot),sizeof(void*),protection,&ignored)!=0;
 if(previous!=reinterpret_cast<void*>(bindings.original)||!restored){
  if(previous==reinterpret_cast<void*>(bindings.original))InterlockedCompareExchangePointer(slot,previous,reinterpret_cast<void*>(&OnNativeJob));
  return;
 }
 callbackSlot=slot;windowThread=tid;RefreshBounds();
 std::wstring marker=logPath;const auto suffix=marker.rfind(L'.');
 if(suffix!=std::wstring::npos)marker.resize(suffix);marker+=L".enabled";
 const DWORD attributes=GetFileAttributesW(marker.c_str());
 if(attributes!=INVALID_FILE_ATTRIBUTES&&!(attributes&FILE_ATTRIBUTE_DIRECTORY))events=_wfsopen(logPath,L"wb",_SH_DENYNO);
 available=true;Event("engine_queue_adapter_ready");
}
inline void Remove(){
 // The module stays pinned so already-dispatched callbacks and payloads remain
 // valid. Pending native work must be allowed to finish through the wrapper.
 if(!available||request.PauseSr()||request.phase==Phase::AwaitingInput)return;
 DWORD protection=0;
 if(callbackSlot&&VirtualProtect(const_cast<void**>(callbackSlot),sizeof(void*),PAGE_READWRITE,&protection)){
  InterlockedCompareExchangePointer(callbackSlot,reinterpret_cast<void*>(bindings.original),reinterpret_cast<void*>(&OnNativeJob));
  DWORD ignored=0;VirtualProtect(const_cast<void**>(callbackSlot),sizeof(void*),protection,&ignored);
  available=false;
 }
}
}
