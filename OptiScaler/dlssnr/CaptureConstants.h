#pragma once
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <shaders/dlssnr/DlssNr_Common.h>
namespace capture {
// Generated from the named shader ABI; no allocation, pointers, trailing padding or GPU wait.
inline constexpr size_t kNamedConstantBytes = 176;
static_assert(offsetof(DlssNrConstants, Mode) == 0, "Capture ABI changed: regenerate metadata table");
static_assert(offsetof(DlssNrConstants, WhitePoint) == 4, "Capture ABI changed: regenerate metadata table");
static_assert(offsetof(DlssNrConstants, Width) == 8, "Capture ABI changed: regenerate metadata table");
static_assert(offsetof(DlssNrConstants, Height) == 12, "Capture ABI changed: regenerate metadata table");
static_assert(offsetof(DlssNrConstants, TransferStrength) == 16, "Capture ABI changed: regenerate metadata table");
static_assert(offsetof(DlssNrConstants, ColourStrength) == 20, "Capture ABI changed: regenerate metadata table");
static_assert(offsetof(DlssNrConstants, DebugView) == 24, "Capture ABI changed: regenerate metadata table");
static_assert(offsetof(DlssNrConstants, MaxRatio) == 28, "Capture ABI changed: regenerate metadata table");
static_assert(offsetof(DlssNrConstants, Passthrough) == 32, "Capture ABI changed: regenerate metadata table");
static_assert(offsetof(DlssNrConstants, MvScaleX) == 36, "Capture ABI changed: regenerate metadata table");
static_assert(offsetof(DlssNrConstants, MvScaleY) == 40, "Capture ABI changed: regenerate metadata table");
static_assert(offsetof(DlssNrConstants, GuideWidth) == 44, "Capture ABI changed: regenerate metadata table");
static_assert(offsetof(DlssNrConstants, GuideHeight) == 48, "Capture ABI changed: regenerate metadata table");
static_assert(offsetof(DlssNrConstants, CompareMode) == 52, "Capture ABI changed: regenerate metadata table");
static_assert(offsetof(DlssNrConstants, CompareSplit) == 56, "Capture ABI changed: regenerate metadata table");
static_assert(offsetof(DlssNrConstants, CompareZoom) == 60, "Capture ABI changed: regenerate metadata table");
static_assert(offsetof(DlssNrConstants, CompareSwap) == 64, "Capture ABI changed: regenerate metadata table");
static_assert(offsetof(DlssNrConstants, Transfer) == 68, "Capture ABI changed: regenerate metadata table");
static_assert(offsetof(DlssNrConstants, DebugScale) == 72, "Capture ABI changed: regenerate metadata table");
static_assert(offsetof(DlssNrConstants, PreserveHighFrequency) == 76, "Capture ABI changed: regenerate metadata table");
static_assert(offsetof(DlssNrConstants, NetworkRatioX) == 80, "Capture ABI changed: regenerate metadata table");
static_assert(offsetof(DlssNrConstants, NetworkRatioY) == 84, "Capture ABI changed: regenerate metadata table");
static_assert(offsetof(DlssNrConstants, MotionAdaptive) == 88, "Capture ABI changed: regenerate metadata table");
static_assert(offsetof(DlssNrConstants, MotionStart) == 92, "Capture ABI changed: regenerate metadata table");
static_assert(offsetof(DlssNrConstants, MotionEnd) == 96, "Capture ABI changed: regenerate metadata table");
static_assert(offsetof(DlssNrConstants, MismatchStart) == 100, "Capture ABI changed: regenerate metadata table");
static_assert(offsetof(DlssNrConstants, MismatchEnd) == 104, "Capture ABI changed: regenerate metadata table");
static_assert(offsetof(DlssNrConstants, FrequencyRadius) == 108, "Capture ABI changed: regenerate metadata table");
static_assert(offsetof(DlssNrConstants, LumaTrust) == 112, "Capture ABI changed: regenerate metadata table");
static_assert(offsetof(DlssNrConstants, ChromaTrust) == 116, "Capture ABI changed: regenerate metadata table");
static_assert(offsetof(DlssNrConstants, SourceWidth) == 120, "Capture ABI changed: regenerate metadata table");
static_assert(offsetof(DlssNrConstants, SourceHeight) == 124, "Capture ABI changed: regenerate metadata table");
static_assert(offsetof(DlssNrConstants, GuidedReconstruction) == 128, "Capture ABI changed: regenerate metadata table");
static_assert(offsetof(DlssNrConstants, PostSharpness) == 132, "Capture ABI changed: regenerate metadata table");
static_assert(offsetof(DlssNrConstants, CatmullRomInput) == 136, "Capture ABI changed: regenerate metadata table");
static_assert(offsetof(DlssNrConstants, ExperimentalCompose) == 140, "Capture ABI changed: regenerate metadata table");
static_assert(offsetof(DlssNrConstants, ValidX) == 144, "Capture ABI changed: regenerate metadata table");
static_assert(offsetof(DlssNrConstants, ValidY) == 148, "Capture ABI changed: regenerate metadata table");
static_assert(offsetof(DlssNrConstants, ValidWidth) == 152, "Capture ABI changed: regenerate metadata table");
static_assert(offsetof(DlssNrConstants, ValidHeight) == 156, "Capture ABI changed: regenerate metadata table");
static_assert(offsetof(DlssNrConstants, MotionX) == 160, "Capture ABI changed: regenerate metadata table");
static_assert(offsetof(DlssNrConstants, MotionY) == 164, "Capture ABI changed: regenerate metadata table");
static_assert(offsetof(DlssNrConstants, HighlightEncoding) == 168, "Capture ABI changed: regenerate metadata table");
static_assert(offsetof(DlssNrConstants, RelativeColour) == 172, "Capture ABI changed: regenerate metadata table");
inline void writeNamedConstants(std::FILE* file, const DlssNrConstants& c) {
    std::fprintf(file, ",\"resolve_constants_complete\":true,\"resolve_constants_bytes\":%zu,\"constants_evidence\":\"cpu_constants_not_gpu_branch_trace\",\"resolve_constants_named\":{", kNamedConstantBytes);
    std::fprintf(file, "\"Mode\":%u", c.Mode);
    if (std::isfinite(c.WhitePoint)) std::fprintf(file, ",\"WhitePoint\":%.9g", c.WhitePoint); else std::fprintf(file, ",\"WhitePoint\":null");
    std::fprintf(file, ",\"Width\":%u", c.Width);
    std::fprintf(file, ",\"Height\":%u", c.Height);
    if (std::isfinite(c.TransferStrength)) std::fprintf(file, ",\"TransferStrength\":%.9g", c.TransferStrength); else std::fprintf(file, ",\"TransferStrength\":null");
    if (std::isfinite(c.ColourStrength)) std::fprintf(file, ",\"ColourStrength\":%.9g", c.ColourStrength); else std::fprintf(file, ",\"ColourStrength\":null");
    std::fprintf(file, ",\"DebugView\":%u", c.DebugView);
    if (std::isfinite(c.MaxRatio)) std::fprintf(file, ",\"MaxRatio\":%.9g", c.MaxRatio); else std::fprintf(file, ",\"MaxRatio\":null");
    std::fprintf(file, ",\"Passthrough\":%u", c.Passthrough);
    if (std::isfinite(c.MvScaleX)) std::fprintf(file, ",\"MvScaleX\":%.9g", c.MvScaleX); else std::fprintf(file, ",\"MvScaleX\":null");
    if (std::isfinite(c.MvScaleY)) std::fprintf(file, ",\"MvScaleY\":%.9g", c.MvScaleY); else std::fprintf(file, ",\"MvScaleY\":null");
    std::fprintf(file, ",\"GuideWidth\":%u", c.GuideWidth);
    std::fprintf(file, ",\"GuideHeight\":%u", c.GuideHeight);
    std::fprintf(file, ",\"CompareMode\":%u", c.CompareMode);
    if (std::isfinite(c.CompareSplit)) std::fprintf(file, ",\"CompareSplit\":%.9g", c.CompareSplit); else std::fprintf(file, ",\"CompareSplit\":null");
    if (std::isfinite(c.CompareZoom)) std::fprintf(file, ",\"CompareZoom\":%.9g", c.CompareZoom); else std::fprintf(file, ",\"CompareZoom\":null");
    std::fprintf(file, ",\"CompareSwap\":%u", c.CompareSwap);
    std::fprintf(file, ",\"Transfer\":%u", c.Transfer);
    if (std::isfinite(c.DebugScale)) std::fprintf(file, ",\"DebugScale\":%.9g", c.DebugScale); else std::fprintf(file, ",\"DebugScale\":null");
    std::fprintf(file, ",\"PreserveHighFrequency\":%u", c.PreserveHighFrequency);
    if (std::isfinite(c.NetworkRatioX)) std::fprintf(file, ",\"NetworkRatioX\":%.9g", c.NetworkRatioX); else std::fprintf(file, ",\"NetworkRatioX\":null");
    if (std::isfinite(c.NetworkRatioY)) std::fprintf(file, ",\"NetworkRatioY\":%.9g", c.NetworkRatioY); else std::fprintf(file, ",\"NetworkRatioY\":null");
    std::fprintf(file, ",\"MotionAdaptive\":%u", c.MotionAdaptive);
    if (std::isfinite(c.MotionStart)) std::fprintf(file, ",\"MotionStart\":%.9g", c.MotionStart); else std::fprintf(file, ",\"MotionStart\":null");
    if (std::isfinite(c.MotionEnd)) std::fprintf(file, ",\"MotionEnd\":%.9g", c.MotionEnd); else std::fprintf(file, ",\"MotionEnd\":null");
    if (std::isfinite(c.MismatchStart)) std::fprintf(file, ",\"MismatchStart\":%.9g", c.MismatchStart); else std::fprintf(file, ",\"MismatchStart\":null");
    if (std::isfinite(c.MismatchEnd)) std::fprintf(file, ",\"MismatchEnd\":%.9g", c.MismatchEnd); else std::fprintf(file, ",\"MismatchEnd\":null");
    if (std::isfinite(c.FrequencyRadius)) std::fprintf(file, ",\"FrequencyRadius\":%.9g", c.FrequencyRadius); else std::fprintf(file, ",\"FrequencyRadius\":null");
    if (std::isfinite(c.LumaTrust)) std::fprintf(file, ",\"LumaTrust\":%.9g", c.LumaTrust); else std::fprintf(file, ",\"LumaTrust\":null");
    if (std::isfinite(c.ChromaTrust)) std::fprintf(file, ",\"ChromaTrust\":%.9g", c.ChromaTrust); else std::fprintf(file, ",\"ChromaTrust\":null");
    std::fprintf(file, ",\"SourceWidth\":%u", c.SourceWidth);
    std::fprintf(file, ",\"SourceHeight\":%u", c.SourceHeight);
    std::fprintf(file, ",\"GuidedReconstruction\":%u", c.GuidedReconstruction);
    if (std::isfinite(c.PostSharpness)) std::fprintf(file, ",\"PostSharpness\":%.9g", c.PostSharpness); else std::fprintf(file, ",\"PostSharpness\":null");
    std::fprintf(file, ",\"CatmullRomInput\":%u", c.CatmullRomInput);
    std::fprintf(file, ",\"ExperimentalCompose\":%u", c.ExperimentalCompose);
    std::fprintf(file, ",\"ValidX\":%u", c.ValidX);
    std::fprintf(file, ",\"ValidY\":%u", c.ValidY);
    std::fprintf(file, ",\"ValidWidth\":%u", c.ValidWidth);
    std::fprintf(file, ",\"ValidHeight\":%u", c.ValidHeight);
    std::fprintf(file, ",\"MotionX\":%u", c.MotionX);
    std::fprintf(file, ",\"MotionY\":%u", c.MotionY);
    std::fprintf(file, ",\"HighlightEncoding\":%u", c.HighlightEncoding);
    std::fprintf(file, ",\"RelativeColour\":%u", c.RelativeColour);
    std::fprintf(file, "}");
}
}
