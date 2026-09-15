#include <dlssnr/BuildProfile.h>
#pragma once
#include <atomic>
#include "SrDiagnosticStages.h"
#include "SrPresetState.h"
#include <hooks/D18ExecutionTrace.h>
#include <hooks/D18ContextTransaction.h>
namespace DlssNr::WildlandsSr {
inline std::atomic<bool> postReplay{false},nativeHandoff{false};
inline std::atomic<unsigned long long> nativeLayers{0};
inline std::atomic<unsigned long long> handoffs{0},handoffGpuCompleted{0},handoffGpuTick{0};
inline std::atomic<unsigned long long> postCleared{0},postComputed{0},postReplayed{0},postReady{0},postGpuFinished{0},postRejected{0},postAccepted{0};
inline std::atomic<bool> offscreenUpscale{false};
inline std::atomic<unsigned> outputWidth{0},outputHeight{0};
inline SrPresetState preset;
__declspec(noinline) inline void RequestPreset(unsigned hint) { preset.Request(hint); }
inline std::atomic<bool> stagePanel{false},stagePending{false};
inline std::atomic<unsigned> requestedStage{0},activeStage{0};
inline std::atomic<unsigned long long> stageCpu[DiagnosticStages::Count]{},stageGpu[DiagnosticStages::Count]{};
__declspec(noinline) inline void SetDiagnosticStage(unsigned stage){if(!BuildProfile::Diagnostic||stage>=DiagnosticStages::Count)return;D18ContextTransaction::Arm();D18ExecutionTrace::ArmStage();requestedStage=stage;D18ExecutionTrace::Point("sr_diagnostic_mode_requested",nullptr,0,stage,0);}
inline std::atomic<bool> evaluateOnly{false};
inline std::atomic<unsigned long long> evaluations{0};
inline std::atomic<unsigned> coverageReason{0};
inline std::atomic<bool> coverageBlocked{false};
inline std::atomic<bool> available{false},enabled{true},failed{false};
inline void SetEnabled(bool value){D18ExecutionTrace::ArmSr(value);enabled=value;}
inline std::atomic<unsigned long long> frames{0},lastTick{0};
inline std::atomic<unsigned long long> gpuCompleted{0},lastGpuTick{0};
inline std::atomic<unsigned> width{0},height{0};
inline std::atomic<long> code{0};
// Input gate observation, not an API or visual-success claim.
inline std::atomic<unsigned> inputGate{0};
inline std::atomic<unsigned long long> lastTargetTick{0};
}
