#pragma once
#include "FgPauseSignal.h"
#include <Config.h>
#include <State.h>

namespace DlssNr::FgPause {
inline bool Enabled() {return Config::Instance()->DlssNrPauseWhenFgOff.value_or_default();}
inline const char* IneligibleReason() {
    const auto& s=State::Instance();const auto& c=*Config::Instance();
    if(s.api!=API::DX12 || s.swapchainApi==API::DX11 || s.swapchainApi==API::Vulkan)
        return "FG pause: unavailable; requires DX12";
    if(c.SkipStreamlineHooks.value_or_default())return "FG pause: unavailable; Streamline hooks are skipped";
    if(s.activeFgInput!=FGInput::NoFG || s.activeFgOutput!=FGOutput::NoFG)
        return "FG pause: unavailable; requires game-native DLSS FG";
    if(c.FGDLSSGOverrideInterpolationCount.has_value() || c.FGDLSSGOverrideForceDMFG.value_or_default() ||
       c.FGDLSSGFramerateTargetDMFG.has_value())return "FG pause: unavailable; D18 FG overrides are active";
    return nullptr;
}
inline bool Eligible() {return IneligibleReason()==nullptr;}
inline void ResetIfEnabled() {
    if(Enabled())Reset();
}
inline bool ObserveEnabled() {
    if(!Enabled())return false;
    if(Eligible())return true;
    Reset();return false;
}
inline Status Current() {
    const auto& c=*Config::Instance();
    const bool option=Enabled();
    const bool eligible=option && Eligible();
    if(option && !eligible)Reset();
    return Read(option,eligible,c.DlssNrEnabled.value_or_default());
}
inline const char* StatusText() {
    if(!Enabled())return "FG pause: option off";
    if(const auto* reason=IneligibleReason())return reason;
    if(MultipleViewports())return "Multiple frame-generation viewports detected; FG pause is unavailable";
    switch(installation.load(std::memory_order_acquire)) {
    case Installation::NotInstalled:return "FG pause: unavailable; observer is not installed";
    case Installation::Installing:return "FG pause: installing observer";
    case Installation::Busy:return "FG pause: installation busy; toggle off and on to retry";
    case Installation::NoModule:return "FG pause: unavailable; Streamline interposer is not loaded";
    case Installation::PathUnavailable:return "FG pause: unavailable; cannot read Streamline module path";
    case Installation::VersionUnavailable:return "FG pause: unavailable; cannot read Streamline version";
    case Installation::UnsupportedVersion:return "FG pause: unavailable; requires Streamline 2";
    case Installation::ExportsUnavailable:return "FG pause: unavailable; required Streamline lifecycle exports are missing";
    case Installation::LeaseFailed:return "FG pause: unavailable; cannot retain Streamline module";
    case Installation::ThreadFailed:return "FG pause: installation failed while preparing threads";
    case Installation::BeginFailed:return "FG pause: installation transaction could not begin";
    case Installation::UpdateFailed:return "FG pause: installation thread update failed";
    case Installation::AttachFailed:return "FG pause: installation hook attachment failed";
    case Installation::CommitFailed:return "FG pause: installation commit failed; changes rolled back";
    case Installation::Ready:break;
    }
    switch(Current()) {
    case Status::Paused:return "FG pause: paused because game FG is off";
    case Status::Running:return "FG pause: NR runs normally";
    default:return observedPaths.load(std::memory_order_relaxed)?
        "FG pause: enabled; waiting for the game's next successful FG-on request":
        "FG pause: enabled; waiting for the game's next successful FG-on request; neither observer path has seen a request";
    }
}
}
