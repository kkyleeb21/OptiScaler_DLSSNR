#include "pch.h"
#include "DlssNrFeature_Vk.h"
#include "Diagnostics.h"
#include "NativeControl.h"
#include <menu/D18NrHints.h>
#include "VkColourCapture.h"

#include "DlssNr.h"
#include <Config.h>
#include <menu/menu_common.h>

#include <imgui/imgui.h>

#include <string>
#include <unordered_map>
#include <algorithm>

namespace DlssNr
{

// The "(?)" marker every control carries, matching the rest of the menu.
static void HelpMarker(const char* tip)
{
    ImGui::SameLine();
    D18Ui::TextDisabled("(?)");

    if (ImGui::IsItemHovered())
    {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 40.0f);
        D18Ui::TextUnformatted(tip);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

// A slider that only writes its value when the handle is released.
//
// Some controls -- intensity, the structure and tone strengths -- are read by the model once, when
// the feature is built, so changing one rebuilds the whole feature. Writing on every pixel of a drag
// meant a rebuild per frame, felt as the picture hitching while you scrub. The slider still tracks
// live under the cursor; only the commit that triggers the rebuild waits for release. Cheap controls
// that are just shader constants (detail, colour, paper white) do not use this -- they can afford to
// apply live.
static bool DeferredSlider(const char* label, CustomOptional<float>* opt, float mn, float mx,
                           const char* fmt = "%.2f")
{
    static std::unordered_map<std::string, float> pending;

    auto it = pending.find(label);
    float value = it != pending.end() ? it->second : opt->value_or_default();

    if (D18NrUi::SliderFloat(label, &value, mn, mx, fmt))
        pending[label] = value;

    if (ImGui::IsItemDeactivatedAfterEdit())
    {
        auto committed = pending.find(label);

        if (committed != pending.end())
        {
            *opt = std::clamp(committed->second, mn, mx);
            pending.erase(committed);
            return true;
        }
    }

    return false;
}

// The single D18 NR panel. Extend this entry point for every backend.
void RenderD18Menu(Config* config, float menuResScale)
{
    const bool vulkan = State::Instance().api == API::Vulkan;
    const bool dx11 = State::Instance().api == API::DX11;
    const bool native = vulkan || dx11;
    ImGui::Spacing();
    if (auto ch = ScopedCollapsingHeader("DLSS NR", ImGuiTreeNodeFlags_DefaultOpen); ch.IsHeaderOpen())
    {
        ScopedIndent indent {};
        ImGui::Spacing();
        ImGui::PushItemWidth(220.0f * menuResScale);

        bool enabled = config->DlssNrEnabled.value_or_default();
        if (D18NrUi::Checkbox("Enable Neural Rendering", &enabled))
            config->DlssNrEnabled = enabled;

        HelpMarker("Runs DLSS Neural Rendering immediately after DLSS SR and before frame generation.");

        if (native)
        {
            if(!vulkan){auto s=NativeControl::Read();
                const bool fresh=s.tick && GetTickCount64()-s.tick<1500;
                const char* label=!enabled?"Off":s.result<0?"Blocked":!fresh?"Waiting":s.mode==1?"Conversion only":s.result==1?"Running":"Waiting";
                const char* reason=!enabled?"NR switch is disabled":s.result<0?NativeControl::Reason(s.result):!fresh?"No recent DLSS SR frame received":s.mode==1?"Diagnostic mode bypasses the NR model":s.result==1?"NR completed after DLSS SR":"No NR work completed for the latest SR frame";
                D18Ui::TextColored(s.result<0&&enabled?ImVec4(1,0.5f,0.25f,1):ImVec4(0.65f,0.85f,0.75f,1),"%s",label);
                D18Ui::TextWrapped("%s",reason);
                if(D18Ui::TreeNode("Technical details")){D18Ui::Text("%u frames | %ux%u | code %d",s.frames,s.width,s.height,s.result);if(s.failed)D18Ui::TextWrapped("Backend stopped for this session; restart to retry.");ImGui::TreePop();}
            }else{
                const char* failure=FailureReasonVk();
                D18Ui::Text("%s",!enabled?"Off":failure[0]?"Blocked":NativeControl::conversion?"Conversion only":IsRunningVk()?"Running":"Waiting");
                D18Ui::TextWrapped("%s",!enabled?"NR switch is disabled":failure[0]?failure:NativeControl::conversion?"Diagnostic mode bypasses the NR model":IsRunningVk()?"NR recorded after DLSS SR; gameplay verification remains separate":"No active native Vulkan NR frame; check SR and runtime setup");
                if(D18Ui::TreeNode("Technical details")){D18Ui::Text("%llu recorded frames",FramesVk());ImGui::TreePop();}
            }
        }
        else
        {

        const bool running = DlssNr::IsRunning() || vulkan;
        if (running)
        {
            const auto ms = vulkan ? DlssNr::LastGpuTimeVk() : DlssNr::LastGpuTime();
            if (ms.has_value())
                D18Ui::TextColored(ImVec4(0.35f, 0.92f, 0.55f, 1.0f), "Active - %.2f ms", ms.value());
            else
                D18Ui::TextColored(ImVec4(0.35f, 0.92f, 0.55f, 1.0f), "Active - timing pending");
        }
        else if (const char* reason = vulkan ? DlssNr::FailureReasonVk() : DlssNr::FailureReason(); reason[0] != 0)
        {
            D18Ui::TextColored(ImVec4(1.0f, 0.38f, 0.32f, 1.0f), "NR unavailable");
            D18Ui::TextWrapped("%s", reason);
            ImGui::SameLine();
            ImGui::BeginDisabled(!DlssNr::CanRetryAfterFailure());
            if (D18Ui::SmallButton("Retry NR"))
                DlssNr::RetryAfterFailure();
            ImGui::EndDisabled();
        }
        else if (enabled)
        {
            D18Ui::TextColored(ImVec4(0.95f, 0.72f, 0.25f, 1.0f), "Waiting for a DLSS frame");
        }
        else
        {
            D18Ui::TextDisabled("Off");
        }

        }

        if (!native && DlssNr::ResourceWarning()[0]) D18Ui::TextWrapped("%s", DlssNr::ResourceWarning());
        D18Ui::SeparatorText("NR ratio");
        bool internalScaling = native ? config->DlssNrInternalScaling.value_or(false) : config->DlssNrInternalScaling.value_or_default();
        if (D18NrUi::Checkbox("Internal network scaling", &internalScaling))
            config->DlssNrInternalScaling = internalScaling;
        const bool effectiveInternalScaling = internalScaling;

        HelpMarker("Keeps Color and final Output at full resolution while reducing the internal network lattice. Native backends default to 100% until explicitly enabled.");

        static float pendingRatio = -1.0f;
        float ratio = pendingRatio >= 0.0f ? pendingRatio : config->DlssNrInternalScalingRatio.value_or_default();
        ImGui::BeginDisabled(!effectiveInternalScaling);
        if (D18NrUi::SliderFloat("Network ratio", &ratio, 0.5f, 1.0f, "%.3f"))
            pendingRatio = ratio;
        if (ImGui::IsItemDeactivatedAfterEdit() && pendingRatio >= 0.0f)
        {
            config->DlssNrInternalScalingRatio = std::clamp(pendingRatio, 0.5f, 1.0f);
            pendingRatio = -1.0f;
        }

        const struct { const char* label; float ratio; } presets[] = {
            { "50%##d18_nr", 0.5f }, { "66.7%##d18_nr", 2.0f / 3.0f },
            { "75%##d18_nr", 0.75f }, { "100%##d18_nr", 1.0f },
        };
        for (int i = 0; i < IM_ARRAYSIZE(presets); ++i)
        {
            if (i != 0)
                ImGui::SameLine();
            if (D18Ui::SmallButton(presets[i].label))
            {
                config->DlssNrInternalScalingRatio = presets[i].ratio;
                pendingRatio = -1.0f;
            }
        }
        ImGui::EndDisabled();

        if (!effectiveInternalScaling)
        {
            D18Ui::TextDisabled("Full-resolution Color / Output; physical WorkingScale is disabled.");
        }


        if (D18Ui::TreeNodeEx("Clarity and detail", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth))
        {
            D18Ui::TextWrapped("Soft image? Start with Detail strength; sampling controls are under Advanced.");
        float detail = config->DlssNrTransferStrength.value_or_default();
        if (D18NrUi::SliderFloat("Detail strength", &detail, 0.0f, 2.0f, "%.2f"))
            config->DlssNrTransferStrength = detail;

        if (D18Ui::TreeNode("Advanced detail controls"))
        {
        const bool reducedPhysical = !native && effectiveInternalScaling &&
                                     config->DlssNrExperimentalCompose.value_or_default() &&
                                     config->DlssNrInternalScalingRatio.value_or_default() < 0.999f;
        static const char* enlargementNames[] = { "Classic", "Matched residual" };
        int enlargement = config->DlssNrTransfer.value_or_default() == 1 ? 1 : 0;
        ImGui::BeginDisabled(!reducedPhysical);
        if (D18NrUi::Combo("Enlargement##d18", &enlargement, enlargementNames,
                         IM_ARRAYSIZE(enlargementNames)))
            config->DlssNrTransfer = (uint32_t) enlargement;
        ImGui::EndDisabled();

        ImGui::BeginDisabled(false);

        bool linearResolve = config->DlssNrLinearResolve.value_or_default();
        if (D18NrUi::Checkbox("Linear network output sampling##d18", &linearResolve))
            config->DlssNrLinearResolve = linearResolve;

        bool linearColorInput = config->DlssNrLinearColorInput.value_or_default();
        if (D18NrUi::Checkbox("Linear model Color input##d18", &linearColorInput))
            config->DlssNrLinearColorInput = linearColorInput;

        bool customFilter = config->DlssNrCustomColorFilter.value_or_default();
        ImGui::BeginDisabled(!effectiveInternalScaling);
        if (D18NrUi::Checkbox("Custom model Color prefilter##d18", &customFilter))
            config->DlssNrCustomColorFilter = customFilter;
        ImGui::EndDisabled();

        if (customFilter && effectiveInternalScaling)
            D18Ui::TextDisabled("Effective Runtime Color sampler: POINT (custom prefilter active)");
        bool catmull = config->DlssNrCatmullRomInput.value_or_default();
        ImGui::BeginDisabled(!customFilter || !effectiveInternalScaling);
        if (D18NrUi::Checkbox("Catmull-Rom input kernel (A/B)##d18", &catmull))
            config->DlssNrCatmullRomInput = catmull;
        ImGui::EndDisabled();
        HelpMarker("Custom prefilter OFF: Runtime baseline. ON: Mitchell; with this option: Catmull-Rom."
                   " Same grid and anti-ringing clamp. Change only one control per capture.");

        HelpMarker("Mitchell phase-aligns the full-resolution Color input to the reduced network grid."
                   "\nIt overrides Linear Color input so two low-pass filters never stack."
                   "\nThe two LINEAR switches remain available for direct Runtime A/B testing.");

        ImGui::EndDisabled();

        if (native) D18Ui::TextDisabled("Frequency reconstruction requires the DX12 scaling path.");
        ImGui::BeginDisabled(native);
        bool preserve = config->DlssNrPreserveHighFrequency.value_or_default();
        if (D18NrUi::Checkbox("Preserve original high frequencies", &preserve))
            config->DlssNrPreserveHighFrequency = preserve;

        bool experimentalCompose = config->DlssNrExperimentalCompose.value_or_default();
        ImGui::BeginDisabled(vulkan);
        if (D18NrUi::Checkbox("Experimental low-ratio compose##d18", &experimentalCompose))
            config->DlssNrExperimentalCompose = experimentalCompose;
        ImGui::EndDisabled();
        HelpMarker("Default Off preserves original D18 composition. DX12 backend only; not a promise of 100% quality at 50%.");
        bool guided = config->DlssNrGuidedReconstruction.value_or_default();
        ImGui::BeginDisabled(vulkan || !experimentalCompose || !effectiveInternalScaling || !preserve);
        if (D18NrUi::Checkbox("Guided network reconstruction##d18", &guided))
            config->DlssNrGuidedReconstruction = guided;
        ImGui::BeginDisabled(!guided);
        bool gainFirst = config->DlssNrGainFirstReconstruction.value_or_default();
        if (D18NrUi::Checkbox("Area + gain-first reconstruction (50% A/B)##d18", &gainFirst))
            config->DlssNrGainFirstReconstruction = gainFirst;
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        HelpMarker("Reconstructs the reduced model verdict between network-cell centres and uses"
                   " full-resolution SR luminance to keep gain changes on the correct side of edges."
                   " Disable for the ratio-aware low-pass baseline.");

        bool motionAdaptive = config->DlssNrMotionAdaptive.value_or_default();
        if (D18NrUi::Checkbox("Motion-adaptive low-frequency transfer##d18", &motionAdaptive))
            config->DlssNrMotionAdaptive = motionAdaptive;

        if (motionAdaptive)
        {
            float motionStart = config->DlssNrMotionStart.value_or_default();
            float motionEnd = config->DlssNrMotionEnd.value_or_default();
            float mismatchStart = config->DlssNrMismatchStart.value_or_default();
            float mismatchEnd = config->DlssNrMismatchEnd.value_or_default();
            if (D18NrUi::SliderFloat("Motion protection starts##d18", &motionStart, 0.0f, 16.0f, "%.1f px"))
                config->DlssNrMotionStart = motionStart;
            if (D18NrUi::SliderFloat("Motion protection reaches full##d18", &motionEnd, 2.0f, 64.0f,
                                   "%.1f px"))
                config->DlssNrMotionEnd = motionEnd;
            if (D18NrUi::SliderFloat("Mismatch protection starts##d18", &mismatchStart, 0.0f, 0.20f,
                                   "%.3f"))
                config->DlssNrMismatchStart = mismatchStart;
            if (D18NrUi::SliderFloat("Mismatch protection reaches full##d18", &mismatchEnd, 0.01f, 0.40f,
                                   "%.3f"))
                config->DlssNrMismatchEnd = mismatchEnd;
        }

        ImGui::EndDisabled();

            ImGui::BeginDisabled(native || !config->DlssNrExperimentalCompose.value_or_default());
            float frequencyRadius = config->DlssNrFrequencyRadius.value_or_default();
            float lumaTrust = config->DlssNrLumaTrust.value_or_default();
            float chromaTrust = config->DlssNrChromaTrust.value_or_default();
            if (D18NrUi::SliderFloat("Frequency radius##d18", &frequencyRadius, 0.5f, 8.0f,
                                   "%.2f network px"))
                config->DlssNrFrequencyRadius = frequencyRadius;
            if (D18NrUi::SliderFloat("Luma trust##d18", &lumaTrust, 0.0f, 2.0f, "%.2f"))
                config->DlssNrLumaTrust = lumaTrust;
            if (D18NrUi::SliderFloat("Chroma trust##d18", &chromaTrust, 0.0f, 2.0f, "%.2f"))
                config->DlssNrChromaTrust = chromaTrust;
            HelpMarker("Live experimental controls. Enable Experimental low-ratio compose first."
                       "\nFrequency radius moves the SR/model band split."
                       "\nLuma and chroma trust change only their respective model verdicts.");

            ImGui::EndDisabled();
            ImGui::TreePop();
        }
            ImGui::TreePop();
        }

        if (D18Ui::TreeNodeEx("Colour and brightness", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth))
        {
            D18Ui::TextWrapped("Colour shift? Start with Colour strength or transfer only model colour changes.");
        float colour = config->DlssNrColourStrength.value_or_default();
        if (D18NrUi::SliderFloat("Colour strength", &colour, 0.0f, 1.0f, "%.2f"))
            config->DlssNrColourStrength = colour;

        ImGui::BeginDisabled(!vulkan);
        bool relativeColour=config->DlssNrRelativeColour.value_or_default();
        if(D18NrUi::Checkbox("Transfer only model colour changes (experimental)",&relativeColour))
            config->DlssNrRelativeColour=relativeColour;
        ImGui::EndDisabled();
        HelpMarker("Vulkan only. Anchors colour to the SR original and transfers the model's change"
                   " relative to its encoded input. Keeps model detail and the existing brightness guard."
                   " Live switch; default Off. Colour strength still controls the amount."
                   " Input normalization remains the separate Paper white control.");

        if (D18Ui::TreeNode("Advanced colour and brightness"))
        {
            ImGui::BeginDisabled(!vulkan);
            const char* encodingNames[] = { "Classic (default)", "Hybrid (experimental)", "Neutwo (experimental)" };
            const auto configuredEncoding = config->DlssNrHighlightEncoding.value_or_default();
            int encoding = configuredEncoding <= 2 ? (int)configuredEncoding : 0;
            if (D18NrUi::Combo("Highlight encoding", &encoding, encodingNames, IM_ARRAYSIZE(encodingNames)))
                config->DlssNrHighlightEncoding = (uint32_t)encoding;
            ImGui::EndDisabled();
            HelpMarker("Vulkan linear HDR inputs only. Changes apply on the next NR frame and reset model history."
                       " Hybrid preserves midtones; Neutwo compresses the full range."
                       " Both keep the existing composition and highlight guard; neither uses raw replacement."
                       " Save Settings stores the selection for this game. Tone-mapped inputs bypass it.");
            const auto nativeExposure=NativeControl::Read();
            const bool exposureReady=vulkan ? ExposureReadyVk() : dx11 ? nativeExposure.exposure>1e-6f : true;
            ImGui::BeginDisabled(!exposureReady);
            bool fromExposure = native ? config->DlssNrWhitePointFromExposure.value_or(false) : config->DlssNrWhitePointFromExposure.value_or_default();
            if (D18NrUi::Checkbox("Use game exposure", &fromExposure))
                config->DlssNrWhitePointFromExposure = fromExposure;

            ImGui::EndDisabled();
            if (native && !exposureReady) D18Ui::TextDisabled("Game exposure requires a supported texture and a completed readback; paper white is manual until then.");
            const auto ex = DlssNr::GameExposureStatus();
            if (vulkan)
                D18Ui::TextDisabled(exposureReady ? "Vulkan exposure readback ready" : DlssNr::ExposureOfferedVk() ? "Exposure offered; waiting for a known readable layout and GPU completion"
                                                                : "No game exposure offered");
            else if (dx11)
                D18Ui::Text("DX11 exposure %.5f | pre-exposure %.3f",nativeExposure.exposure,nativeExposure.preExposure);
            else if (ex.seenFrames == 0)
                D18Ui::TextDisabled("Exposure: waiting for a frame");
            else if (!ex.everOffered)
                D18Ui::TextColored(ImVec4(0.95f, 0.72f, 0.25f, 1.0f), "Exposure: not supplied by game");
            else if (ex.exposure > 1e-6f)
                D18Ui::Text("Exposure %.5f | pre-exposure %.3f%s", ex.exposure, ex.preExposure,
                            ex.offeredNow ? "" : " (held)");
            else
                D18Ui::TextDisabled("Exposure offered; readback pending");

            float paperWhite = config->DlssNrWhitePointScale.value_or_default();
            if (D18NrUi::SliderFloat(fromExposure && exposureReady ? "Paper white (x exposure)" : "Paper white", &paperWhite,
                                   0.25f, 240.0f, "%.2fx", ImGuiSliderFlags_Logarithmic))
                config->DlssNrWhitePointScale = paperWhite;

            float guard = config->DlssNrMaxRatio.value_or_default();
            if (D18NrUi::SliderFloat("Highlight guard", &guard, 1.0f, 8.0f, "%.1fx"))
                config->DlssNrMaxRatio = guard;

            ImGui::TreePop();
        }
            ImGui::TreePop();
        }

        if (D18Ui::TreeNodeEx("Characters and style", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth))
        {
            D18Ui::TextWrapped("Adjust skin, local structure and the overall model style.");
            D18Ui::TextDisabled("Committed on release to avoid rebuilding once per drag frame.");

            static const char* presetNames[] = { "Default", "Preset 1", "Preset 2", "Preset 3" };
            int preset = std::min((int) (native ? config->DlssNrPreset.value_or(1u) : config->DlssNrPreset.value_or_default()), 3);
            if (D18NrUi::Combo("Model preset##d18", &preset, presetNames, IM_ARRAYSIZE(presetNames)))
                config->DlssNrPreset = (uint32_t) preset;

            static const char* styleNames[] = { "Standard", "Natural", "Cinematic" };
            int style = std::min((int) config->DlssNrStyle.value_or_default(), 2);
            if (D18NrUi::Combo("NR style##d18", &style, styleNames, IM_ARRAYSIZE(styleNames)))
                config->DlssNrStyle = (uint32_t) style;

            DeferredSlider("Intensity##d18", &config->DlssNrIntensity, 0.0f, 2.0f);
            DeferredSlider("Local structure##d18", &config->DlssNrLocalStructure, 0.0f, 2.0f);
            DeferredSlider("Local tone##d18", &config->DlssNrLocalTone, 0.0f, 2.0f);
            DeferredSlider("Skin structure##d18", &config->DlssNrSkinStructure, -1.0f, 2.0f);

            bool autoMask = config->DlssNrAutoMask.value_or_default();
            if (D18NrUi::Checkbox("Auto skin mask", &autoMask))
                config->DlssNrAutoMask = autoMask;
            ImGui::Spacing();
            bool jitterCorrection = NativeControl::JitterEnabled();
            ImGui::BeginDisabled(!dx11 || !NativeControl::HasJitterControl());
            if (D18NrUi::Checkbox("Reduce detail flicker (NR jitter correction)", &jitterCorrection))
                config->DlssNrJitterCorrection = jitterCorrection;
            ImGui::EndDisabled();
            HelpMarker("Try this if skin, hair or fine details flicker with NR enabled. Turn it off if ghosting or instability increases. Changes apply on the next NR frame; use Save Settings to keep your choice for this game. Preserves skin/detail strength and the original SR/FG motion vectors. Enabled by default for Nioh 2, off for other games. Requires valid jittered motion-vector inputs.");
            if (dx11) D18Ui::TextWrapped("Try for skin/hair flicker; turn off if ghosting or instability increases.");
            if (!dx11) D18Ui::TextDisabled("Available with the native DX11 NR backend.");
            else if (!jitterCorrection) D18Ui::TextDisabled("Off");
            else D18Ui::TextWrapped("%s", NativeControl::JitterStatusText());
            ImGui::TreePop();
        }

        if (D18Ui::TreeNodeEx("Compare the result", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth))
        {
            D18Ui::TextWrapped("Compare NR against the original image in the same scene.");
            static const char* compareNames[] = { "Off", "Side by side", "Wipe" };
            int compare = (int) config->DlssNrCompare.value_or_default();
            if (D18NrUi::Combo("Compare##d18", &compare, compareNames, IM_ARRAYSIZE(compareNames)))
                config->DlssNrCompare = (uint32_t) compare;

            if (compare != 0)
            {
                bool swap = config->DlssNrCompareSwap.value_or_default();
                if (D18NrUi::Checkbox("Swap sides##d18", &swap))
                    config->DlssNrCompareSwap = swap;

                ImGui::BeginDisabled(dx11);
                bool tags = config->DlssNrCompareTags.value_or_default();
                if (D18NrUi::Checkbox("Label the sides##d18", &tags))
                    config->DlssNrCompareTags = tags;

                if (tags)
                {
                    float tagScale = config->DlssNrTagScale.value_or_default();
                    if (D18NrUi::SliderFloat("Label size##d18", &tagScale, 0.5f, 5.0f, "%.1fx"))
                        config->DlssNrTagScale = std::clamp(tagScale, 0.5f, 5.0f);
                }
                ImGui::EndDisabled();
            }

            if (compare == 1)
            {
                float zoom = config->DlssNrCompareZoom.value_or_default();
                if (D18NrUi::SliderFloat("Zoom##d18", &zoom, 1.0f, 2.0f, "%.2f"))
                    config->DlssNrCompareZoom = std::clamp(zoom, 1.0f, 2.0f);
            }

            if (compare == 2)
            {
                float split = config->DlssNrCompareSplit.value_or_default();
                if (D18NrUi::SliderFloat("Wipe split", &split, 0.0f, 1.0f, "%.2f"))
                    config->DlssNrCompareSplit = std::clamp(split, 0.0f, 1.0f);
            }

            ImGui::TreePop();
        }
        if (D18Ui::TreeNode("Advanced and diagnostics"))
        {
        D18Ui::SeparatorText("Diagnostics");
        static const char* diagnosticModes[] = { "Off", "Summary", "Trace" };
        int diagnosticMode = (int)std::min(config->DlssNrDiagnostics.value_or_default(), 2u);
        if (D18NrUi::Combo("Diagnostic mode##d18", &diagnosticMode, diagnosticModes,
                         IM_ARRAYSIZE(diagnosticModes)))
            config->DlssNrDiagnostics = (uint32_t)diagnosticMode;
        HelpMarker("Summary records lifecycle and failures. Trace also records per-frame contracts."
                   " DX12 uses the bounded ring; native backends use bounded metadata logs. Pixel capture is separate.");
        if (diagnosticMode != 0 && !native)
        {
            const auto diagnostic = Diagnostics::Latest();
            D18Ui::Text("Latest: %s | code 0x%08X | frame %llu", diagnostic.type,
                        diagnostic.result, diagnostic.frame);
            if (diagnostic.reason[0] != 0)
                D18Ui::TextWrapped("Reason: %s", diagnostic.reason);
            D18Ui::TextDisabled("Records %llu | overwritten %llu", diagnostic.recorded, diagnostic.dropped);
        }

            ImGui::BeginDisabled(false);
            static const char* debugNames[] = { "Off", "Proxy", "Raw model output", "Difference x20" };
            int debugView = (int) config->DlssNrDebugView.value_or_default();
            if (D18NrUi::Combo("Debug view", &debugView, debugNames, IM_ARRAYSIZE(debugNames)))
                config->DlssNrDebugView = (uint32_t) debugView;

            ImGui::EndDisabled();
            ImGui::BeginDisabled(native);
            if (DlssNr::CaptureInProgress())
                D18Ui::TextDisabled("Capturing / awaiting observed GPU completion...");
            else if (D18NrUi::Button("Capture 8 frames##d18"))
                DlssNr::RequestCapture(8);
            ImGui::EndDisabled();
            HelpMarker("DX12 backend only. No observed submission means no readback: do not treat a pending capture as completed.");

            if (native)
            {
                D18Ui::SeparatorText("Native diagnostics");
                if(vulkan){
                    if(D18NrUi::Button("Capture 3 full frames (colour)"))ColourCapture::Request();
                    D18Ui::TextWrapped("%s",ColourCapture::Status().c_str());
                    HelpMarker("Default off. Four stages from the same frame, up to 3 frames per click."
                               " Close menu and keep NR on. Up to 512 MiB readback memory plus one image;"
                               " files go to D18ColourCaptures beside the game proxy. Capture affects timing."
                               " Exposure texture pixels are not read without a known layout.");
                }
                D18NrUi::Checkbox("Conversion only (skip model)", &NativeControl::conversion);
                if (dx11)
                {
                    D18NrUi::Checkbox("Capture four-stage regions", &NativeControl::capture);
                    D18NrUi::SliderFloat("Capture X", &NativeControl::captureX, 0.0f, 1.0f);
                    D18NrUi::SliderFloat("Capture Y", &NativeControl::captureY, 0.0f, 1.0f);
                    D18NrUi::SliderInt("Capture size", &NativeControl::captureSize, 64, 512);
                    D18Ui::TextWrapped("Up to 32 sampled frames per run. Toggle off/on to rearm. Readback affects timing.");
                }
            }
            ImGui::TreePop();
        }
        ImGui::PopItemWidth();
    }
}

} // namespace DlssNr
