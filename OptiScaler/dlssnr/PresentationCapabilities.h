#pragma once
#include <State.h>
namespace DlssNr {
// Presentation capability is observable before any SR feature is created.
// Never write the global SR backend selector to make a menu available.
// Called only after QI confirms the game created a DX11 device.
inline bool PrepareDx11FgPresentation(const State& state) {
    return state.activeFgInput == FGInput::Upscaler && state.activeFgOutput != FGOutput::NoFG;
}
inline API PresentationApi(const State& state) {
    return state.swapchainInteropApi == SwapchainInteropApi::Dx11wDx12 ? API::DX11 : state.swapchainApi;
}
inline bool HasDx11Presentation(const State& state) {
    return PresentationApi(state) == API::DX11;
}
}
