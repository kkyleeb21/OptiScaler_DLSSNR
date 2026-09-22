#pragma once
// S0 only: opt-in old PHF in the existing resolve; no new GPU pass or model parameter.
// Disabled/100%/unsupported branches keep every original byte, including PHF state.
inline bool d18S0ApplyR0(DlssNrConstants& c, bool requested) noexcept {
    if (!requested || c.Mode != 1 || c.ExperimentalCompose || c.GuidedReconstruction ||
        c.RelativeColour || c.HighlightEncoding || c.PostSharpness != 0 || c.MotionAdaptive ||
        c.DebugView || c.CompareMode ||
        !(c.NetworkRatioX >= .25f && c.NetworkRatioY >= .25f &&
          c.NetworkRatioX <= 1.f && c.NetworkRatioY <= 1.f &&
          (c.NetworkRatioX < .999f || c.NetworkRatioY < .999f))) return false;
    // Existing PHF is the only PHF; setting its bit never submits a second dispatch.
    c.PreserveHighFrequency = 1;
    return true;
}
