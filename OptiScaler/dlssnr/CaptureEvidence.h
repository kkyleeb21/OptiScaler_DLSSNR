#pragma once
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <shaders/dlssnr/DlssNr_Common.h>

namespace capture {
// Successful evaluate calls since the last actual model reset/failure. This is
// an observed count, not a promise that a model's history has converged.
struct HistoryEvidence {
    uint64_t successful = 0;
    void observe(bool reset, bool success) {
        if (reset || !success) successful = 0;
        if (success && successful != UINT64_MAX) ++successful;
    }
};
struct FrameEvidence {
    uint64_t frame = 0, successfulSinceReset = 0;
    bool reset = false, rr = false;
    bool routeRr = false, ngxSourceObserved = false;
    uint32_t inputWidth = 0, inputHeight = 0, inputKernel = 0;
    uint32_t networkWidth = 0, networkHeight = 0;
    DlssNrAbi::Frame rects;
    float preExposure = 1, intensity = 0, localStructure = 0, localTone = 0, skinStructure = 0;
    uint32_t style = 0, preset = 0, autoMask = 0;
    DlssNrConstants resolve {};
};
// Mirrors the current DX12 shader gate, not a claim of GPU branch instrumentation.
inline bool matchedResidualEligible(const FrameEvidence& e) {
    const auto& c = e.resolve;
    const bool modelRanSmall = c.ExperimentalCompose != 0
        ? (c.NetworkRatioX < 0.999f || c.NetworkRatioY < 0.999f)
        : (e.inputWidth != c.Width || e.inputHeight != c.Height);
    return c.Transfer == 1 && modelRanSmall && !(c.DebugView >= 1 && c.DebugView <= 3);
}
inline void writeEvidence(std::FILE* file, const FrameEvidence& e)
{
    std::fprintf(file, "{\"schema\":\"d18-capture-evidence-v2\",\"api\":\"d3d12\","
        "\"frame\":%llu,\"successful_since_reset\":%llu,\"reset\":%s,\"rr\":%s,"
        "\"guides_captured\":false,\"exact_cross_run_replay\":false,"
        "\"network\":[%u,%u],\"pre_exposure\":%.9g,\"model\":[%.9g,%.9g,%.9g,%.9g,%u,%u,%u],",
        static_cast<unsigned long long>(e.frame), static_cast<unsigned long long>(e.successfulSinceReset),
        e.reset ? "true" : "false", e.rr ? "true" : "false", e.networkWidth, e.networkHeight,
        e.preExposure, e.intensity, e.localStructure, e.localTone, e.skinStructure, e.style, e.preset, e.autoMask);
    std::fprintf(file, "\"rr_source\":\"%s\",\"route_rr\":%s,\"model_input_extent\":[%u,%u],"
        "\"input_kernel\":%u,\"highlight_encoding\":%u,\"resolve_branches\":{\"evidence\":\"cpu_constants_not_gpu_trace\","
        "\"matched_residual_eligible\":%s},",
        e.ngxSourceObserved ? "ngx_feature" : "route_contract", e.routeRr ? "true" : "false",
        e.inputWidth, e.inputHeight, e.inputKernel, e.resolve.HighlightEncoding, matchedResidualEligible(e) ? "true" : "false");
    const auto rect = [&](const char* key, const DlssNrAbi::Rect& r) {
        std::fprintf(file, "\"%s\":[%u,%u,%u,%u],", key,r.x,r.y,r.width,r.height);
    };
    rect("output_rect",e.rects.output); rect("depth_rect",e.rects.depth); rect("motion_rect",e.rects.motion);
    const auto& c = e.resolve;
    std::fprintf(file,"\"white_point\":%.9g,\"transfer_strength\":%.9g,\"colour_strength\":%.9g,"
        "\"passthrough\":%u,\"transfer\":%u,\"debug_view\":%u,\"compare_mode\":%u,"
        "\"mv_scale\":[%.9g,%.9g],\"resolve_constants_hex\":\"",
        c.WhitePoint,c.TransferStrength,c.ColourStrength,c.Passthrough,c.Transfer,c.DebugView,c.CompareMode,
        c.MvScaleX,c.MvScaleY);
    // Only named shader fields, no trailing alignment padding or process addresses.
    const auto* bytes = reinterpret_cast<const unsigned char*>(&c);
    for (size_t i=0; i<offsetof(DlssNrConstants, RelativeColour); ++i) std::fprintf(file,"%02x",bytes[i]);
    std::fprintf(file,"\"}\n");
}
}
