#pragma once
namespace DlssNr::InputDiscovery {
struct Policy { bool allowed, nativeRendering, numericCapture; };
// Unknown applications may opt into observation, never a known game's renderer.
constexpr Policy Resolve(bool diagnostic, bool knownAdapter, bool nativeMarker,
                         bool observationMarker, bool numericMarker) {
    const bool native = knownAdapter && nativeMarker;
    const bool observe = diagnostic && observationMarker && !native;
    return {native || observe, native, observe && knownAdapter && numericMarker};
}
}
