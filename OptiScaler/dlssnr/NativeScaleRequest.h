#pragma once
#include <atomic>
#include <cmath>
#include <cstdint>

namespace DlssNr {
// Shared lifecycle for an engine-owned resolution change. No engine addresses,
// API version, resolution or rendering calls belong in this class.
class NativeScaleRequest {
public:
 enum class Phase : unsigned { Idle, Reserving, Draining, Queued, Applying, NativePending, AwaitingInput, EngineReady, Active, Rejected, Failed };
 std::atomic<Phase> phase{Phase::Idle};
 std::atomic<float> requested{1.0f},accepted{1.0f};
 std::atomic<std::uint64_t> generation{0},started{0};
 std::atomic<unsigned> callbackThread{0},inputWidth{0},inputHeight{0};

 bool Request(float ratio,std::uint64_t now) {
  if(!std::isfinite(ratio)||ratio<=0.0f||ratio>2.0f)return false;
  auto previous=phase.load();
  if(previous!=Phase::Idle&&previous!=Phase::AwaitingInput&&previous!=Phase::EngineReady&&previous!=Phase::Active&&previous!=Phase::Rejected)return false;
  if(!phase.compare_exchange_strong(previous,Phase::Reserving))return false;
  requested=ratio;started=now;inputWidth=0;inputHeight=0;++generation;
  phase=Phase::Draining;return true;
 }
 bool Queue(bool srDrained,bool postDrained) {
  if(!srDrained||!postDrained)return false;
  auto expected=Phase::Draining;return phase.compare_exchange_strong(expected,Phase::Queued);
 }
 bool BeginApply(unsigned thread) {
  auto expected=Phase::Queued;
  if(!phase.compare_exchange_strong(expected,Phase::Applying))return false;
  callbackThread=thread;return true;
 }
 bool NativeSubmitted(float ratio) {
  if(!std::isfinite(ratio)||ratio<=0.0f||ratio>2.0f)return false;
  // Only the native callback owns accepted while Applying.
  if(phase!=Phase::Applying)return false;
  accepted=ratio;
  auto expected=Phase::Applying;return phase.compare_exchange_strong(expected,Phase::NativePending);
 }
 bool NativeFinished() {
  auto expected=Phase::NativePending;return phase.compare_exchange_strong(expected,Phase::AwaitingInput);
 }
 bool ObserveInput(unsigned w,unsigned h,unsigned outputW,unsigned outputH,unsigned alignment) {
  if(phase!=Phase::AwaitingInput||!w||!h||!outputW||!outputH||alignment>64)return false;
  const float ratio=accepted.load();
  if(std::abs(double(w)-double(outputW)*ratio)>double(alignment)+1.0||
     std::abs(double(h)-double(outputH)*ratio)>double(alignment)+1.0)return false;
  inputWidth=w;inputHeight=h;
  auto expected=Phase::AwaitingInput;return phase.compare_exchange_strong(expected,Phase::EngineReady);
 }
 // The caller must supply the completed SR generation and actual input size.
 bool SrCompleted(std::uint64_t serial,unsigned w,unsigned h) {
  if(serial!=generation.load()||!w||!h||w!=inputWidth||h!=inputHeight)return false;
  auto expected=Phase::EngineReady;return phase.compare_exchange_strong(expected,Phase::Active);
 }
 bool Expire(std::uint64_t now,std::uint64_t limit=10000) {
  auto current=phase.load();
  if(current==Phase::Idle||current==Phase::AwaitingInput||current==Phase::EngineReady||current==Phase::Active||current==Phase::Rejected||current==Phase::Failed||current==Phase::Reserving)return false;
  if(now<started||now-started<limit)return false;
  return phase.compare_exchange_strong(current,Phase::Failed);
 }
 bool PauseSr() const {
  const auto value=phase.load();
  return value!=Phase::Idle&&value!=Phase::Active&&value!=Phase::Rejected&&value!=Phase::AwaitingInput&&value!=Phase::EngineReady;
 }
 // Rejection is legal only before changing native settings. After submission,
 // uncertain completion fails closed and keeps the borrowed payload frozen.
 bool Reject() {auto expected=Phase::Applying;return phase.compare_exchange_strong(expected,Phase::Rejected);}
};
}
