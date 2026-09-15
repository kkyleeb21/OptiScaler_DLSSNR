#define NOMINMAX
#include <dlssnr/WildlandsScaleQueue.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <limits>
#include <cassert>
namespace W=DlssNr::WildlandsScale;
using P=W::Phase;
std::array<unsigned char,W::SettingsSize> applied{},editing{};
unsigned char nativePending=0;float renderScale=.75f;
std::mutex jobsMutex;std::condition_variable wake;std::deque<W::Job> jobs;
bool stopping=false;std::atomic<bool> paused{false};std::atomic<unsigned> calls{0},originalCalls{0};
std::atomic<DWORD> workerId{0};
#ifdef D18_TEST_EDITOR_MERGE
std::atomic<bool> editDuringApply{false},wrongWindow{false},busyOnDispatch{false};
#endif
void Set(void* p,float ratio){assert(GetCurrentThreadId()==workerId);ratio=ratio<.5f?.5f:ratio>2?2:ratio;std::memcpy(static_cast<unsigned char*>(p)+0x6c,&ratio,4);}
void Apply(void* p){assert(GetCurrentThreadId()==workerId);nativePending=1;std::memcpy(applied.data(),p,applied.size());renderScale=W::Scale(p);++calls;
#ifdef D18_TEST_EDITOR_MERGE
 if(editDuringApply){float scale=.8f;unsigned detail=321;std::memcpy(editing.data()+0x6c,&scale,4);std::memcpy(editing.data()+0x44,&detail,4);}
#endif
 assert(PostMessageW(W::bindings.window,W::SettingsMessage,reinterpret_cast<WPARAM>(p),0));}
void Original(void* p){assert(GetCurrentThreadId()==workerId);auto* j=static_cast<W::Job*>(p);if(j->settings==W::payload.data()&&j->parameter==0)nativePending=0;++originalCalls;}
LRESULT CALLBACK Window(HWND h,UINT m,WPARAM w,LPARAM l){
 if(m==W::SettingsMessage){std::lock_guard lock(jobsMutex);
#ifdef D18_TEST_EDITOR_MERGE
  if(busyOnDispatch)nativePending=1;
  jobs.push_back({nullptr,reinterpret_cast<void*>(w),wrongWindow?nullptr:h,m,0,l});
#else
  jobs.push_back({nullptr,reinterpret_cast<void*>(w),h,m,0,l});
#endif
  wake.notify_one();return 0;}
 return DefWindowProcW(h,m,w,l);
}
void Pump(){MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}}
void Until(P phase){const auto start=GetTickCount64();while(W::request.phase!=phase&&GetTickCount64()-start<3000){Pump();Sleep(1);}assert(W::request.phase==phase);}
void Input(unsigned w,unsigned h){W::ObserveInput(w,h,3840,2160);assert(W::request.phase==P::EngineReady);}
int main(int argc,char** argv){
 assert(argc==2);W::events=_fsopen(argv[1],"wb",_SH_DENYNO);assert(W::events);
 unsigned width=3840,height=2160;float scale=.75f,detail=.15f;
 std::memcpy(applied.data()+0x3c,&width,4);std::memcpy(applied.data()+0x40,&height,4);std::memcpy(applied.data()+0x6c,&scale,4);std::memcpy(applied.data()+0x88,&detail,4);editing=applied;
 unsigned word=0;std::memcpy(&word,editing.data()+0x88,4);--word;std::memcpy(editing.data()+0x88,&word,4);
 WNDCLASSW wc{};wc.lpfnWndProc=Window;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"D18 Native queue fixture";assert(RegisterClassW(&wc));
 HWND window=CreateWindowExW(0,wc.lpszClassName,L"",0,0,0,0,0,HWND_MESSAGE,nullptr,wc.hInstance,nullptr);assert(window);
 W::bindings={applied.data(),editing.data(),&nativePending,&renderScale,window,Set,Apply,Original};W::available=true;
 W::RefreshBounds();assert(W::minimumScale==.5f&&W::maximumScale==2.0f);
 width=1280;height=720;std::memcpy(applied.data()+0x3c,&width,4);std::memcpy(applied.data()+0x40,&height,4);
 W::RefreshBounds();assert(std::abs(W::minimumScale-.8f)<.0001f);
 width=3840;height=2160;std::memcpy(applied.data()+0x3c,&width,4);std::memcpy(applied.data()+0x40,&height,4);W::RefreshBounds();
 std::thread worker([]{workerId=GetCurrentThreadId();for(;;){W::Job job{};{
  std::unique_lock lock(jobsMutex);wake.wait(lock,[]{return stopping||(!paused&&!jobs.empty());});if(stopping)break;job=jobs.front();jobs.pop_front();}
  W::OnNativeJob(&job);
 }});
 while(!workerId)Sleep(1);
 assert(!W::Request(std::numeric_limits<float>::quiet_NaN()));
 assert(W::Request(2.0f/3.0f));assert(!W::Request(.5f));
 W::Tick(false,true);Pump();assert(calls==0&&W::request.phase==P::Draining);
 W::Tick(true,false);assert(W::request.phase==P::Draining);
 W::Tick(true,true);Until(P::AwaitingInput);
 assert(calls==1&&originalCalls==1&&W::request.callbackThread==workerId&&workerId!=GetCurrentThreadId());
 assert(std::abs(W::Scale(editing.data())-2.0f/3.0f)<.0001f);
 W::ObserveInput(2880,1620,3840,2160);assert(W::request.phase==P::AwaitingInput);
 Input(2560,1440);auto generation=W::request.generation.load();
 W::SrCompleted(generation-1,2560,1440);assert(W::request.phase==P::EngineReady);
 W::SrCompleted(generation,2560,1440);assert(W::request.phase==P::Active);
 assert(W::Request(.5f));W::Tick(true,true);Until(P::AwaitingInput);Input(1920,1080);
 // A scale can be changed again with SR disabled: engine success is independent.
 assert(W::Request(.58f));W::Tick(true,true);Until(P::AwaitingInput);
 assert(!W::request.Expire(W::request.started+60000));
 // A settings menu can apply another mode without ever producing scene inputs.
 assert(W::Request(.58f));W::Tick(true,true);Until(P::AwaitingInput);Input(2224,1248);
 assert(calls==4);
 assert(calls==4);
 unsigned changed=123;std::memcpy(editing.data()+0x44,&changed,4);
#ifdef D18_TEST_EDITOR_MERGE
 auto appliedBefore=applied;
 assert(W::Request(.75f));W::Tick(true,true);Until(P::AwaitingInput);assert(calls==5);
 for(unsigned i=0;i<W::SettingsSize;++i)if(i<0x6c||i>=0x70)assert(applied[i]==appliedBefore[i]);
 unsigned kept=0;std::memcpy(&kept,editing.data()+0x44,4);assert(kept==123&&!W::editorScalePreserved&&W::Scale(editing.data())==.75f);
 // A pending scale and resolution remain byte-for-byte intact.
 float pendingScale=.9f;unsigned pendingWidth=1920;std::memcpy(editing.data()+0x6c,&pendingScale,4);std::memcpy(editing.data()+0x3c,&pendingWidth,4);
 auto editorBefore=editing;assert(W::Request(.5f));W::Tick(true,true);Until(P::AwaitingInput);
 assert(calls==6&&editing==editorBefore&&W::editorScalePreserved&&W::Scale(applied.data())==.5f);
 // A new edit arriving between Apply and native completion must also survive.
 editing=applied;editDuringApply=true;assert(W::Request(.58f));W::Tick(true,true);Until(P::AwaitingInput);editDuringApply=false;
 std::memcpy(&kept,editing.data()+0x44,4);assert(calls==7&&kept==321&&W::Scale(editing.data())==.8f&&W::editorScalePreserved);
 editing=applied;wrongWindow=true;assert(W::Request(.75f));W::Tick(true,true);Until(P::Rejected);wrongWindow=false;assert(calls==7&&W::failure==6);
 busyOnDispatch=true;assert(W::Request(.75f));W::Tick(true,true);Until(P::Rejected);busyOnDispatch=false;assert(calls==7&&W::failure==8);nativePending=0;
 constexpr unsigned successfulCalls=7;
#else
 assert(W::Request(.75f));W::Tick(true,true);Until(P::Rejected);assert(calls==4);
 constexpr unsigned successfulCalls=4;
#endif
 editing=applied;
 assert(W::Request(1.0f/3.0f));W::Tick(true,true);Until(P::Rejected);assert(calls==successfulCalls&&W::failure==2);
 // Unrelated native messages still reach the original callback once.
 auto old=originalCalls.load();assert(PostMessageW(window,W::SettingsMessage,reinterpret_cast<WPARAM>(applied.data()),0));
 for(unsigned i=0;i<100&&originalCalls==old;++i){Pump();Sleep(1);}assert(originalCalls==old+1);
 paused=true;assert(W::Request(.75f));W::Tick(true,true);Pump();auto frozen=W::payload;
 assert(W::request.Expire(W::request.started+10001));assert(!W::Request(.5f));
 paused=false;wake.notify_one();Sleep(30);assert(calls==successfulCalls&&W::payload==frozen&&W::request.phase==P::Failed);
 {std::lock_guard lock(jobsMutex);stopping=true;}wake.notify_one();worker.join();DestroyWindow(window);
 std::fclose(W::events);W::events=nullptr;
 std::puts("PASS: queued engine Apply, native completion, GPU drain, independent engine/SR status, stale generation, unsaved settings, native clamp, unrelated callback and late timeout");
}

