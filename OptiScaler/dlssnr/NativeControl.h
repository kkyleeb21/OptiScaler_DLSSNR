#pragma once
#include "NativeControlAbi.h"
#include <Config.h>
#include <mutex>
#include <filesystem>
namespace DlssNr::NativeControl {
inline bool conversion=false,capture=false;
inline float captureX=0.5f,captureY=0.537f;
inline int captureSize=512;
inline std::mutex statusMutex;
inline DlssNrNative::Status status{};
inline DlssNrNative::JitterStatus jitterStatus{};
inline bool jitterSupported=false;
inline uint32_t JitterMode(){const auto& v=Config::Instance()->DlssNrJitterCorrection;return v.has_value()?(v.value()?2u:1u):0u;}
inline bool JitterEnabled(){
 static const bool nioh=[](){wchar_t name[32768]{};GetModuleFileNameW(nullptr,name,32768);return _wcsicmp(std::filesystem::path(name).filename().c_str(),L"nioh2.exe")==0;}();
 return Config::Instance()->DlssNrJitterCorrection.value_or(nioh);
}
inline DlssNrNative::Settings Settings(){
 auto& c=*Config::Instance();DlssNrNative::Settings s;
 s.mode=c.DlssNrEnabled.value_or_default()?(conversion?1u:2u):0u;
 s.diagnostics=c.DlssNrDiagnostics.value_or_default();s.capture=capture?1u:0u;s.captureX=captureX;s.captureY=captureY;s.captureSize=static_cast<uint32_t>(captureSize);
 s.intensity=c.DlssNrIntensity.value_or_default();s.localStructure=c.DlssNrLocalStructure.value_or_default();
 s.localTone=c.DlssNrLocalTone.value_or_default();s.skinStructure=c.DlssNrSkinStructure.value_or_default();
 s.style=c.DlssNrStyle.value_or_default();s.autoMask=c.DlssNrAutoMask.value_or_default()?1u:0u;
 s.whitePoint=c.DlssNrWhitePointScale.value_or_default();s.transferStrength=c.DlssNrTransferStrength.value_or_default();
 s.colourStrength=c.DlssNrColourStrength.value_or_default();s.maxRatio=c.DlssNrMaxRatio.value_or_default();s.transfer=c.DlssNrTransfer.value_or_default();
 s.preset=c.DlssNrPreset.value_or(1u);s.debugView=c.DlssNrDebugView.value_or_default();
 s.compare=c.DlssNrCompare.value_or_default();s.compareSwap=c.DlssNrCompareSwap.value_or_default()?1u:0u;
 s.compareSplit=c.DlssNrCompareSplit.value_or_default();s.compareZoom=c.DlssNrCompareZoom.value_or_default();
 s.networkRatio=c.DlssNrInternalScaling.value_or(false)?std::clamp(c.DlssNrInternalScalingRatio.value_or_default(),0.5f,1.0f):1.0f;
 s.customFilter=s.networkRatio<1 && c.DlssNrCustomColorFilter.value_or_default();
 s.catmullRom=s.customFilter && c.DlssNrCatmullRomInput.value_or_default();
 s.linearResolve=c.DlssNrLinearResolve.value_or_default();s.linearColorInput=!s.customFilter&&c.DlssNrLinearColorInput.value_or_default();
 s.useExposure=c.DlssNrWhitePointFromExposure.value_or(false);return s;
}
inline void Unavailable(int code){std::lock_guard lock(statusMutex);status.result=code;status.tick=GetTickCount64();status.frames=0;}
inline bool Apply(HMODULE module){
 using Configure=int(*)(const DlssNrNative::Settings*);
 if(auto fn=reinterpret_cast<Configure>(GetProcAddress(module,"D24Configure"))){auto s=Settings();if(fn(&s)){
   using SetJitter=int(*)(uint32_t);auto jitter=reinterpret_cast<SetJitter>(GetProcAddress(module,"D24ConfigureJitter"));
   const bool supported=jitter&&jitter(JitterMode());{std::lock_guard lock(statusMutex);jitterSupported=supported;}return true;
 }}
 Unavailable(-101);return false;
}
inline void Observe(HMODULE module){
 using Read=int(*)(DlssNrNative::Status*);DlssNrNative::Status s;
 if(auto fn=reinterpret_cast<Read>(GetProcAddress(module,"D24ReadStatus"));fn&&fn(&s)){std::lock_guard lock(statusMutex);status=s;}
 using ReadJitter=int(*)(DlssNrNative::JitterStatus*);DlssNrNative::JitterStatus j;
 if(auto fn=reinterpret_cast<ReadJitter>(GetProcAddress(module,"D24ReadJitterStatus"));fn&&fn(&j)){std::lock_guard lock(statusMutex);jitterStatus=j;}
}
inline bool HasJitterControl(){std::lock_guard lock(statusMutex);return jitterSupported;}
inline const char* JitterStatusText(){
 std::lock_guard lock(statusMutex);using R=DlssNrNative::JitterReason;
 if(!jitterSupported)return "Waiting for a compatible DX11 NR plugin; update D24Native.dll if needed.";
 if(!jitterStatus.tick||GetTickCount64()-jitterStatus.tick>1500)return "Waiting for a recent NR frame.";
 switch(jitterStatus.reason){
 case R::Off:return "Off";case R::Waiting:return "Waiting for the next NR frame.";
 case R::Active:return "Active - NR motion-vector jitter correction.";
 case R::HistoryReset:return "Initializing NR history; correction resumes with consecutive frames.";
 case R::MissingFlags:return "Not applied: game motion-vector flags are unavailable.";
 case R::NotJittered:return "Not applied: game does not declare jittered motion vectors.";
 case R::NotLowRes:return "Not applied: requires render-resolution motion vectors.";
 case R::RegionMismatch:return "Not applied: motion-vector and render regions do not match.";
 case R::MissingJitter:return "Not applied: game jitter offsets are unavailable.";
 case R::MissingScale:return "Not applied: motion-vector scale is unavailable.";
 case R::InvalidValues:return "Not applied: jitter or motion-vector scale is invalid.";
 case R::DisabledMarker:return "Disabled by D24NrJitterCorrection.disabled; remove it and restart to use this control.";
 case R::NrUnavailable:return "Not applied: NR is off, bypassed or did not complete.";
 default:return "Waiting for NR motion-vector data.";
 }
}
inline DlssNrNative::Status Read(){std::lock_guard lock(statusMutex);return status;}
inline const char* Reason(int code){switch(code){
 case -100:return "D24Native.dll is missing or could not load";
 case -101:return "Core and DX11 plugin control versions do not match";
 case -2:return "Deferred DX11 context is not supported";
 case -3:return "SR did not provide color, depth or motion vectors";
 case -4:return "Input resource is not a supported 2D texture";
 case -5:return "Output format, dimensions or sample count is unsupported";
 case -6:return "NR runtime could not initialize; check runtime file/version";
 case -7:return "Input belongs to a different DX11 device";
 case -8:return "NR model creation failed";
 case -26:return "NR resource allocation failed. Original SR/RR retained; restart to retry.";
 case -27:return "Graphics device unavailable; restart required.";
 case -9:return "Depth or motion-vector dimensions do not fit the input region";
 case -10:return "Native resource registration limit reached";
 case -11:return "Native dispatch preparation failed";
 case -12:case -14:return "GPU completion wait failed";
 case -13:return "NR model evaluation failed";
 case -15:return "Model input conversion failed";
 case -16:return "NR output composition failed";
 case -20:return "Conversion-only path failed";
 case -21:return "Nonzero input region offsets are not supported yet";
 case -22:return "Multisampled or array guide textures are unsupported";
 case -23:return "Motion-vector scale is not finite";
 case -24:return "Depth or motion-vector conversion failed";
 case -25:return "Previous model could not retire safely";
 case -99:return "Native backend stopped after an exception; restart required";
 default:return "No NR work completed for the latest SR frame";
}}
}
