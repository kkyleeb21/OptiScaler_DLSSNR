#pragma once
#include "D18Localization.h"
#include <imgui/imgui_internal.h>

// Only the NR panel calls these wrappers. Annotation items must not replace
// LastItemData: deferred sliders commit on IsItemDeactivatedAfterEdit().
namespace D18NrUi {
struct Hint { const char* label; const char* text; };
inline constexpr Hint hints[] = {
    {"Detail strength", "Texture enhancement"},
    {"Enlargement", "Reconstruction method"},
    {"Linear network output sampling", "Smooth output sampling"},
    {"Linear model Color input", "Smooth input sampling"},
    {"Custom model Color prefilter", "Filter colour before NR"},
    {"Catmull-Rom input kernel (A/B)", "Sharper input filter"},
    {"Preserve original high frequencies", "Keep fine SR detail"},
    {"Experimental low-ratio compose", "Alternative low-ratio blend"},
    {"Guided network reconstruction", "Guide detail recovery"},
    {"Area + gain-first reconstruction (50% A/B)", "Alternative 50% reconstruction"},
    {"Motion-adaptive low-frequency transfer", "Protect moving detail"},
    {"Motion protection starts", "Motion threshold"},
    {"Motion protection reaches full", "Full protection threshold"},
    {"Mismatch protection starts", "Mismatch threshold"},
    {"Mismatch protection reaches full", "Full mismatch protection"},
    {"Frequency radius", "Detail separation scale"},
    {"Luma trust", "Model brightness contribution"},
    {"Chroma trust", "Model chroma contribution"},
    {"Colour strength", "Amount of model recolouring"},
    {"Transfer only model colour changes (experimental)", "Anchor to original colour"},
    {"Highlight encoding", "Prepare bright input for NR"},
    {"Use game exposure", "Follow game exposure"},
    {"Paper white", "Input brightness reference"},
    {"Paper white (x exposure)", "Exposure-relative reference"},
    {"Highlight guard", "Limit brightness gain"},
    {"Model preset", "Model parameter profile"},
    {"NR style", "Model rendering style"},
    {"Intensity", "Overall model intensity"},
    {"Local structure", "Local shape and detail"},
    {"Local tone", "Local tonal treatment"},
    {"Skin structure", "Skin detail treatment"},
    {"Auto skin mask", "Automatic skin selection"},
    {"Reduce detail flicker (NR jitter correction)", "Stabilize jittered NR input"},
    {"Compare", "Compare original and NR"},
    {"Swap sides", "Reverse comparison sides"},
    {"Label the sides", "Identify original and NR"},
    {"Label size", "Comparison text size"},
    {"Zoom", "Inspect a closer view"},
    {"Wipe split", "Comparison boundary"},
    {"Diagnostic mode", "Log detail level"},
    {"Debug view", "Inspect pipeline stages"},
    {"Capture 8 frames", "Capture DX12 evidence"},
    {"Capture 3 full frames (colour)", "Capture colour stages"},
    {"Conversion only (skip model)", "Isolate format conversion"},
    {"Capture four-stage regions", "Compare processing stages"},
    {"Capture X", "Horizontal sample position"},
    {"Capture Y", "Vertical sample position"},
    {"Capture size", "Sample region size"},
};
inline void Annotate(const char* label) {
    std::string_view key(label); key=key.substr(0,key.find("##"));
    const char* hint=nullptr;
    for (const auto& entry:hints) if(key==entry.label){hint=D18Ui::Tr(entry.text);break;}
    if(!hint)return;
    auto& context=*ImGui::GetCurrentContext();
    const auto original=context.LastItemData;
    const auto* window=context.CurrentWindow;
    const float right=window->WorkRect.Max.x;
    const float start=original.Rect.Max.x+ImGui::GetStyle().ItemSpacing.x;
    const float width=ImGui::CalcTextSize(hint).x+ImGui::GetFontSize();
    // Leave room for the existing (?) tooltip marker as well.
    if(start+width+ImGui::GetFontSize()*3.0f<=right)ImGui::SameLine();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("\xc2\xb7 %s",hint);
    ImGui::PopTextWrapPos();
    context.LastItemData=original;
}
#define D18_NR_HINT_WIDGET(name) template<class... A> inline bool name(const char* label,A&&... args) { \
    const bool changed=D18Ui::name(label,std::forward<A>(args)...); Annotate(label); return changed; }
D18_NR_HINT_WIDGET(Checkbox)
D18_NR_HINT_WIDGET(SliderFloat)
D18_NR_HINT_WIDGET(SliderInt)
D18_NR_HINT_WIDGET(Combo)
D18_NR_HINT_WIDGET(Button)
#undef D18_NR_HINT_WIDGET
}
