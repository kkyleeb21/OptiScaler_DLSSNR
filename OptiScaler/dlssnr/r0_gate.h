#pragma once
#include <shaders/dlssnr/DlssNr_Common.h>
#include <cmath>

// S0 research adapter. This is ordinary, existing PHF, not a G0 algorithm.
// The caller retains its original DlssNr_Vk pipeline and the original NR lifecycle.
// Neither selection nor this gate creates a feature, resets history, or records a pass.
enum class S0VkMode { Original = 0, OldR0 = 1 };
struct S0VkSelection { bool selected; const char* reason; };
inline S0VkSelection SelectS0VkR0(DlssNrConstants& resolve, S0VkMode mode,
                                float ratioX, float ratioY, bool advanced = false)
{
    if (mode == S0VkMode::Original) return {false, "original"};
    if (resolve.Mode != DlssNrMode_Resolve) return {false, "not_resolve"};
    if (!std::isfinite(ratioX) || !std::isfinite(ratioY) || ratioX < .25f || ratioY < .25f ||
        ratioX > 1.f || ratioY > 1.f) return {false, "invalid_ratio"};
    if (ratioX >= .999f && ratioY >= .999f) return {false, "100_percent_original"};
    if (advanced || resolve.ExperimentalCompose != 0 || resolve.GuidedReconstruction != 0 ||
        resolve.RelativeColour != 0 || resolve.MotionAdaptive != 0 || resolve.HighlightEncoding != 0 ||
        resolve.PostSharpness != 0 || resolve.DebugView != 0 || resolve.CompareMode != 0)
        return {false, "unsupported_nonordinary"};
    resolve.PreserveHighFrequency = 1;
    resolve.NetworkRatioX = ratioX;
    resolve.NetworkRatioY = ratioY;
    return {true, "old_r0_same_pipeline"};
}
