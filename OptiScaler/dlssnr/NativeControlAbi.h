#pragma once
#include <cstdint>
namespace DlssNrNative {
// Optional exports; keep the existing Settings/Status v2 layout compatible.
enum class JitterReason : uint32_t { Off, Waiting, Active, HistoryReset, MissingFlags,
 NotJittered, NotLowRes, RegionMismatch, MissingJitter, MissingScale, InvalidValues, DisabledMarker, NrUnavailable };
struct JitterStatus {
 uint32_t size=sizeof(JitterStatus),version=1;
 JitterReason reason=JitterReason::Waiting;
 uint32_t requested=0;
 uint64_t tick=0;
};
struct Settings {
 uint32_t size=sizeof(Settings),version=2,mode=0,diagnostics=0,capture=0;
 float intensity=1,localStructure=1,localTone=1,skinStructure=-1;
 uint32_t style=0,autoMask=1;
 float whitePoint=1,transferStrength=1,colourStrength=1,maxRatio=2;
 uint32_t transfer=1;
 float captureX=0.5f,captureY=0.537f;
 uint32_t captureSize=512;
 uint32_t preset=1,debugView=0,compare=0,compareSwap=0;
 float compareSplit=0.5f,compareZoom=1;
 float networkRatio=1;
 uint32_t linearResolve=0,linearColorInput=0,customFilter=0,catmullRom=0;
 uint32_t useExposure=0;
};
struct Status {
 uint32_t size=sizeof(Status),version=2,mode=0,failed=0;
 int32_t result=0;
 uint32_t frames=0,width=0,height=0;
 uint64_t tick=0;
 float exposure=0,preExposure=1;
};
}
