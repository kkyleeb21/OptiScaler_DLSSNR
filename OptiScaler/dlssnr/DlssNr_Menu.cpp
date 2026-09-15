#include "pch.h"
#include <dlssnr/BuildProfile.h>
#include "DlssNrFeature_Vk.h"
#include "Diagnostics.h"
#include "NativeControl.h"
#include "WildlandsSrStatus.h"
#include <menu/D18NrHints.h>
#include <menu/D18ModelPanel.h>
#include <menu/D18Layout.h>
#include "VkColourCapture.h"

#include "DlssNr.h"
#include <Config.h>
#include "MultipassConfig.h"
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
    static std::unordered_map<ImGuiID, float> pending;

    const auto key=ImGui::GetID(label);
    auto it = pending.find(key);
    float value = it != pending.end() ? it->second : opt->value_or_default();

    if (D18NrUi::SliderFloat(label, &value, mn, mx, fmt))
        pending[key] = value;

    if (ImGui::IsItemDeactivatedAfterEdit())
    {
        auto committed = pending.find(key);

        if (committed != pending.end())
        {
            *opt = std::clamp(committed->second, mn, mx);
            pending.erase(committed);
            return true;
        }
    }

    return false;
}

static unsigned selectedPass=0;

// Sections share original controls, config keys and backend capability gates.
// 0: NR, 1: global comparison, 2: diagnostics, 3: live status.
void RenderD18Menu(Config* config, float menuResScale, int section)
{
    const auto dx12Snapshot = DlssNr::ReadUiSnapshot();
    const bool vulkan = State::Instance().api == API::Vulkan;
    const bool dx11 = State::Instance().api == API::DX11 || WildlandsSr::nativeHandoff;
    const bool native = vulkan || dx11;
    const bool nativeAdvanced=vulkan||(dx11&&NativeControl::HasAdvanced());
    const bool advancedSupported=!native||nativeAdvanced;
    bool highResolution=config->DlssNrHighResolution.value_or_default();
    bool internalScaling=native ? config->DlssNrInternalScaling.value_or(false) : config->DlssNrInternalScaling.value_or_default();
    bool effectiveInternalScaling=internalScaling;
    unsigned passCount=Multipass::Count(config->DlssNrPassCount.value_or_default(),highResolution,advancedSupported);
    bool shared=BuildProfile::SharedHistoryResearch && config->DlssNrSharedHistory.value_or_default();
    ImGui::PushItemWidth(std::min(320.0f*menuResScale, ImGui::GetContentRegionAvail().x*0.52f));
    if(section==1) {
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

    } else if(section==2) {
        if (D18Ui::TreeNode("Advanced and diagnostics"))
        {
        D18Ui::SeparatorText("Diagnostics");
        D18Ui::TextDisabled("%s", D18Ui::Tr(BuildProfile::Name));
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
            ImGui::BeginDisabled(!BuildProfile::PixelCapture || native || passCount>2);
            if(!BuildProfile::PixelCapture) D18Ui::TextDisabled("Pixel capture requires the diagnostic build");
            if(passCount>2) D18Ui::TextDisabled("Pixel capture supports one or two passes. Three/four-pass metadata remains available.");
            if(passCount==2) D18Ui::TextDisabled("Two-pass capture: five regions, up to 8 frames; SR, pass 1, pass 2, final. Limit: 3 captures per session.");
            if (DlssNr::CaptureInProgress())
                D18Ui::TextDisabled("Capturing / awaiting observed GPU completion...");
            else if (D18NrUi::Button("Capture 8 frames##d18"))
                DlssNr::RequestCapture(8);
            ImGui::EndDisabled();
            HelpMarker("DX12 backend only. No observed submission means no readback: do not treat a pending capture as completed.");
            if (!native && dx12Snapshot.captureFailure[0])
                D18Ui::TextWrapped("%s", dx12Snapshot.captureFailure.data());

            if (native)
            {
                D18Ui::SeparatorText("Native diagnostics");
                ImGui::BeginDisabled(!BuildProfile::Diagnostic);
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
                ImGui::EndDisabled();
            }
            ImGui::TreePop();
        }

    } else if(section==3) {
        const bool enabled=config->DlssNrEnabled.value_or_default();
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

        const bool running = dx12Snapshot.running || vulkan;
        if (running)
        {
            const auto ms = vulkan ? DlssNr::LastGpuTimeVk() : dx12Snapshot.gpuTime;
            if (ms.has_value())
                D18Ui::TextColored(ImVec4(0.35f, 0.92f, 0.55f, 1.0f), "Active - %.2f ms", ms.value());
            else
                D18Ui::TextColored(ImVec4(0.35f, 0.92f, 0.55f, 1.0f), "Active - %s", vulkan ? DlssNr::GpuTimingStatusVk() : "timing pending");
        }
        else if (const char* reason = vulkan ? DlssNr::FailureReasonVk() : dx12Snapshot.failure.data(); reason[0] != 0)
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

        if (!native && dx12Snapshot.resourceWarning.data()[0]) D18Ui::TextWrapped("%s", dx12Snapshot.resourceWarning.data());

    } else {
        D18Layout::RowLabel("Neural rendering",menuResScale,230.0f);
        bool enabled = config->DlssNrEnabled.value_or_default();
        if (D18NrUi::Checkbox("Enable Neural Rendering", &enabled))
            config->DlssNrEnabled = enabled;

        HelpMarker("Runs DLSS Neural Rendering immediately after DLSS SR and before frame generation.");

        D18Ui::TextDisabled("Final image strength - one composition");
        ImGui::Spacing();
        float detail = config->DlssNrTransferStrength.value_or_default();
        if (D18NrUi::SliderFloat("Detail strength", &detail, 0.0f, 2.0f, "%.2f"))
            config->DlssNrTransferStrength = detail;

        float colour = config->DlssNrColourStrength.value_or_default();
        if (D18NrUi::SliderFloat("Colour strength", &colour, 0.0f, 1.0f, "%.2f"))
            config->DlssNrColourStrength = colour;

            float guard = config->DlssNrMaxRatio.value_or_default();
            if (D18NrUi::SliderFloat("Highlight guard", &guard, 1.0f, 8.0f, "%.1fx"))
                config->DlssNrMaxRatio = guard;


if(D18Layout::Fold("Detail reconstruction", ImGuiTreeNodeFlags_SpanAvailWidth, menuResScale)) {
    const bool advancedCompose = advancedSupported && (highResolution || passCount>1);
    if(advancedCompose)D18Ui::TextWrapped("High-frequency protection applies once to the final image. Other single-pass reconstruction settings are retained.");
        const bool reducedPhysical = !native && effectiveInternalScaling &&
                                     config->DlssNrExperimentalCompose.value_or_default() &&
                                     config->DlssNrInternalScalingRatio.value_or_default() < 0.999f;
        static const char* enlargementNames[] = { "Classic", "Matched residual" };
        int enlargement = config->DlssNrTransfer.value_or_default() == 1 ? 1 : 0;
        ImGui::BeginDisabled(!reducedPhysical || advancedCompose);
        if (D18NrUi::Combo("Enlargement##d18", &enlargement, enlargementNames,
                         IM_ARRAYSIZE(enlargementNames)))
            config->DlssNrTransfer = (uint32_t) enlargement;
        ImGui::EndDisabled();

        if (native && !advancedCompose) D18Ui::TextDisabled("High-frequency protection is available in native high-resolution and multipass modes.");
        ImGui::BeginDisabled(native && !advancedCompose);
        bool preserve = config->DlssNrPreserveHighFrequency.value_or_default();
        if (D18NrUi::Checkbox("Preserve original high frequencies", &preserve))
            config->DlssNrPreserveHighFrequency = preserve;

        HelpMarker("Keeps original SR texture while transferring model lighting at lower spatial frequencies. Applies once after all passes, including 100% and high-resolution NR. It does not directly correct model colour or skin tone.");

        ImGui::EndDisabled();
        ImGui::BeginDisabled(native);
        bool experimentalCompose = config->DlssNrExperimentalCompose.value_or_default();
        ImGui::BeginDisabled(vulkan || advancedCompose);
        if (D18NrUi::Checkbox("Experimental low-ratio compose##d18", &experimentalCompose))
            config->DlssNrExperimentalCompose = experimentalCompose;
        ImGui::EndDisabled();
        HelpMarker("Default Off preserves original D18 composition. DX12 backend only; not a promise of 100% quality at 50%.");
        bool guided = config->DlssNrGuidedReconstruction.value_or_default();
        ImGui::BeginDisabled(vulkan || advancedCompose || !experimentalCompose || !effectiveInternalScaling || !preserve);
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

            ImGui::BeginDisabled(native || advancedCompose || !config->DlssNrExperimentalCompose.value_or_default());
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

if(D18Layout::Fold("Colour transfer", ImGuiTreeNodeFlags_SpanAvailWidth, menuResScale)) {
        ImGui::BeginDisabled(!vulkan || (advancedSupported && (highResolution || passCount>1)));
        bool relativeColour=config->DlssNrRelativeColour.value_or_default();
        if(D18NrUi::Checkbox("Transfer only model colour changes (experimental)",&relativeColour))
            config->DlssNrRelativeColour=relativeColour;
        ImGui::EndDisabled();
        HelpMarker("Vulkan ordinary single pass only; advanced modes use matched residual composition. Anchors colour to the SR original and transfers the model's change"
                   " relative to its encoded input. Keeps model detail and the existing brightness guard."
                   " Live switch; default Off. Colour strength still controls the amount."
                   " Input normalization remains the separate Paper white control.");


ImGui::TreePop();
}

if(D18Layout::Fold("Model settings", ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen, menuResScale)) {
        D18Layout::RowLabel("Render mode", menuResScale, (D18Layout::ChoiceWidth("Standard NR",menuResScale)+D18Layout::ChoiceWidth("High resolution - single pass",menuResScale)+ImGui::GetStyle().ItemSpacing.x)/menuResScale);
        if(D18Layout::Choice("Standard NR", !highResolution, menuResScale)) {highResolution=false;config->DlssNrHighResolution=false;}
        ImGui::SameLine();ImGui::BeginDisabled(!advancedSupported);
        if(D18Layout::Choice("High resolution - single pass", highResolution, menuResScale)) {highResolution=true;config->DlssNrHighResolution=true;}
        ImGui::EndDisabled();
        if(highResolution && advancedSupported) {
            D18Layout::RowLabel("Enlargement factor", menuResScale, 168.0f);
            for(float factor : {1.25f,1.5f}) {if(factor>1.25f)ImGui::SameLine();
                const auto label=D18Ui::Format("%.2fx",factor);
                if(D18Layout::Choice(label.c_str(),config->DlssNrHighResolutionScale.value_or_default()==factor,menuResScale)) config->DlssNrHighResolutionScale=factor;}
            D18Ui::TextDisabled("Standard NR settings are retained while high resolution is enabled.");

        } else {
            D18Layout::RowLabel("Model pass count", menuResScale, 240.0f);
            for(unsigned pass=1;pass<=4;++pass) {ImGui::PushID(int(pass));if(pass>1)ImGui::SameLine();ImGui::BeginDisabled(!advancedSupported&&pass>1);
                if(D18Layout::Choice(std::to_string(pass).c_str(),pass==passCount,menuResScale,48.0f)){config->DlssNrPassCount=pass;passCount=pass;}
                ImGui::EndDisabled();ImGui::PopID();}
            D18Ui::TextDisabled("Full SR -> %u NR passes -> one final composition",passCount);
            D18Layout::RowLabel("History mode",menuResScale,(D18Layout::ChoiceWidth("Independent history",menuResScale)+D18Layout::ChoiceWidth("Shared history - experimental",menuResScale)+ImGui::GetStyle().ItemSpacing.x)/menuResScale);
            ImGui::BeginDisabled(!advancedSupported||passCount==1);
            if(D18Layout::Choice("Independent history",!shared,menuResScale)){shared=false;config->DlssNrSharedHistory=false;}
            ImGui::SameLine();
            ImGui::BeginDisabled(!BuildProfile::SharedHistoryResearch);
            if(D18Layout::Choice("Shared history - experimental",shared,menuResScale)){shared=true;config->DlssNrSharedHistory=true;}
            ImGui::EndDisabled();
            if(shared) D18Ui::TextWrapped("Experimental shared history may cause flickering or unstable lighting. Independent history is recommended.");
            ImGui::EndDisabled();
            if(!native) D18Ui::TextDisabled("Requested %u | ready %u | last recording %u",Multipass::Count(config->DlssNrPassCount.value_or_default(),false),dx12Snapshot.runtime.readyPasses,dx12Snapshot.runtime.recordedPasses);
            else if(nativeAdvanced){const auto status=vulkan?ReadAdvancedStatusVk():NativeControl::ReadAdvanced();D18Ui::TextDisabled("Requested %u | ready %u | last recording %u",status.requested,status.ready,status.recorded);}
            else D18Ui::TextWrapped("This native NR backend currently supports one pass. Saved multipass settings are retained; DX12 pass counters do not apply.");
            bool compatible=true;const auto actualNative=NativeControl::AdvancedSettings();for(unsigned i=1;i<passCount;++i)compatible &= native?(actualNative.passes[i]==actualNative.passes[0]):(Multipass::Read(*config,i)==Multipass::Read(*config,0));
            if(shared&&passCount>1&&!compatible)D18Ui::TextWrapped(native?"Shared history needs identical settings in every active pass. Use Copy previous pass; until then the SR image is retained.":"Shared history needs identical settings in every active pass. Use Copy previous pass; otherwise only one pass runs.");
            if(!native && dx12Snapshot.runtime.multipassReason[0])D18Ui::TextWrapped("%s",D18Ui::Tr(dx12Snapshot.runtime.multipassReason.data()));
        }
        if(!advancedSupported) D18Ui::TextDisabled("Waiting for an NR backend with high-resolution and multipass support.");
if(D18Layout::Fold("Model parameters", ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen, menuResScale)) {
{
    passCount=Multipass::Count(config->DlssNrPassCount.value_or_default(),highResolution,advancedSupported);
    selectedPass=std::min(selectedPass,passCount-1);
    std::array<Multipass::Tuning,4> tuning;for(unsigned i=0;i<4;++i)tuning[i]=Multipass::Read(*config,i);
    if(native){tuning[0].scaling=internalScaling;tuning[0].preset=NativeControl::Settings().preset;}
    D18ModelPanel modelPanel(highResolution && advancedSupported,menuResScale,tuning,passCount,selectedPass);
    if(modelPanel.copyRequested&&selectedPass>0){auto t=Multipass::Read(*config,selectedPass-1);if(native&&selectedPass==1){t.scaling=internalScaling;t.preset=NativeControl::Settings().preset;}auto& p=config->DlssNrPasses[selectedPass-1];
        p.Scaling=t.scaling;p.AutoMask=t.autoMask;p.Ratio=t.ratio;p.Intensity=t.intensity;p.Structure=t.structure;p.Tone=t.tone;p.Skin=t.skin;p.Preset=t.preset;p.Style=t.style;}
    ImGui::PushID(int(selectedPass));
    auto& scalingOpt=selectedPass?config->DlssNrPasses[selectedPass-1].Scaling:config->DlssNrInternalScaling;
    auto& ratioOpt=selectedPass?config->DlssNrPasses[selectedPass-1].Ratio:config->DlssNrInternalScalingRatio;
    auto& intensityOpt=selectedPass?config->DlssNrPasses[selectedPass-1].Intensity:config->DlssNrIntensity;
    auto& structureOpt=selectedPass?config->DlssNrPasses[selectedPass-1].Structure:config->DlssNrLocalStructure;
    auto& toneOpt=selectedPass?config->DlssNrPasses[selectedPass-1].Tone:config->DlssNrLocalTone;
    auto& skinOpt=selectedPass?config->DlssNrPasses[selectedPass-1].Skin:config->DlssNrSkinStructure;
    auto& presetOpt=selectedPass?config->DlssNrPasses[selectedPass-1].Preset:config->DlssNrPreset;
    auto& styleOpt=selectedPass?config->DlssNrPasses[selectedPass-1].Style:config->DlssNrStyle;
    auto& maskOpt=selectedPass?config->DlssNrPasses[selectedPass-1].AutoMask:config->DlssNrAutoMask;
    bool internalScaling=native&&selectedPass==0?scalingOpt.value_or(false):scalingOpt.value_or_default();
    bool effectiveInternalScaling=internalScaling;

if(!highResolution || !advancedSupported) {

        ImGui::BeginDisabled(!native && highResolution);

        if (D18NrUi::Checkbox("Internal network scaling", &internalScaling))
            scalingOpt = internalScaling;
        effectiveInternalScaling = internalScaling;

        D18Ui::TextWrapped("Use the ratio below when enabled; otherwise run at the full SR size.");

        static float pendingRatios[4]={-1,-1,-1,-1};
        float& pendingRatio=pendingRatios[selectedPass];
        float ratio = pendingRatio >= 0.0f ? pendingRatio : ratioOpt.value_or_default();
        ImGui::BeginDisabled(!effectiveInternalScaling);
        if (D18NrUi::SliderFloat("Network ratio", &ratio, 0.5f, 1.0f, "%.3f"))
            pendingRatio = ratio;
        if (ImGui::IsItemDeactivatedAfterEdit() && pendingRatio >= 0.0f)
        {
            ratioOpt = std::clamp(pendingRatio, 0.5f, 1.0f);
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
            if (D18Layout::Choice(presets[i].label, std::abs(ratio-presets[i].ratio)<0.002f, menuResScale))
            {
                ratioOpt = presets[i].ratio;
                pendingRatio = -1.0f;
            }
        }
        ImGui::EndDisabled();

        ImGui::EndDisabled();
        D18Ui::TextWrapped("Ratio uses the full SR width and height, not the preceding pass size.");



} else { if(native){const auto s=vulkan?ReadAdvancedStatusVk():NativeControl::ReadAdvanced();D18Ui::Text("Applied NR size: %u x %u",s.width,s.height);}else D18Ui::Text("Applied NR size: %u x %u",dx12Snapshot.runtime.workWidth,dx12Snapshot.runtime.workHeight); }
            ImGui::Spacing();

            DeferredSlider("Intensity##d18", &intensityOpt, 0.0f, 2.0f);
            D18Ui::TextWrapped("Model processing strength is separate from final detail composition. Applied on release.");
            ImGui::Spacing();
            ImGui::PushStyleVar(ImGuiStyleVar_CellPadding,ImVec2(8*menuResScale,0));
            const bool presetColumns=ImGui::GetContentRegionAvail().x>330.0f*menuResScale &&
                ImGui::BeginTable("NRPresetStyle",2,ImGuiTableFlags_SizingStretchSame);
            if(presetColumns)ImGui::TableNextColumn();
            static const char* presetNames[] = { "Default", "Preset 1", "Preset 2", "Preset 3" };
            int preset = std::min((int) (native&&selectedPass==0 ? presetOpt.value_or(1u) : presetOpt.value_or_default()), 3);
            if (D18NrUi::Combo("Model preset##d18", &preset, presetNames, IM_ARRAYSIZE(presetNames)))
                presetOpt = (uint32_t) preset;

            if(presetColumns)ImGui::TableNextColumn();
            static const char* styleNames[] = { "Standard", "Natural", "Cinematic" };
            int style = std::min((int) styleOpt.value_or_default(), 2);
            if (D18NrUi::Combo("NR style##d18", &style, styleNames, IM_ARRAYSIZE(styleNames)))
                styleOpt = (uint32_t) style;

            if(presetColumns)ImGui::EndTable();
            ImGui::PopStyleVar();
            ImGui::Spacing();ImGui::Separator();ImGui::Spacing();
            if(D18Ui::TreeNode("Advanced model parameters")) {
            DeferredSlider("Local structure##d18", &structureOpt, 0.0f, 2.0f);
            DeferredSlider("Local tone##d18", &toneOpt, 0.0f, 2.0f);
            DeferredSlider("Skin structure##d18", &skinOpt, -1.0f, 2.0f);

            bool autoMask = maskOpt.value_or_default();
            if (D18NrUi::Checkbox("Auto skin mask", &autoMask))
                maskOpt = autoMask;
            ImGui::TreePop();
            }
            ImGui::Spacing();

    ImGui::PopID();
}
D18Ui::TextWrapped("Each ratio uses the complete SR output. Final composition runs once.");
ImGui::TreePop();
}

if(D18Layout::Fold("Input and sampling", ImGuiTreeNodeFlags_SpanAvailWidth, menuResScale)) {
        bool anyScaling=effectiveInternalScaling;
        if(advancedSupported&&!highResolution)for(unsigned i=1;i<passCount;++i)anyScaling |= Multipass::Read(*config,i).scaling;
        ImGui::BeginDisabled(false);

        bool linearResolve = config->DlssNrLinearResolve.value_or_default();
        if (D18NrUi::Checkbox("Linear network output sampling##d18", &linearResolve))
            config->DlssNrLinearResolve = linearResolve;

        bool linearColorInput = config->DlssNrLinearColorInput.value_or_default();
        if (D18NrUi::Checkbox("Linear model Color input##d18", &linearColorInput))
            config->DlssNrLinearColorInput = linearColorInput;

        bool customFilter = config->DlssNrCustomColorFilter.value_or_default();
        ImGui::BeginDisabled(!anyScaling);
        if (D18NrUi::Checkbox("Custom model Color prefilter##d18", &customFilter))
            config->DlssNrCustomColorFilter = customFilter;
        ImGui::EndDisabled();

        if (customFilter && anyScaling)
            D18Ui::TextDisabled("Effective Runtime Color sampler: POINT (custom prefilter active)");
        bool catmull = config->DlssNrCatmullRomInput.value_or_default();
        ImGui::BeginDisabled(!customFilter || !anyScaling);
        if (D18NrUi::Checkbox("Catmull-Rom input kernel (A/B)##d18", &catmull))
            config->DlssNrCatmullRomInput = catmull;
        ImGui::EndDisabled();
        HelpMarker("Custom prefilter OFF: Runtime baseline. ON: Mitchell; with this option: Catmull-Rom."
                   " Same grid and anti-ringing clamp. Change only one control per capture.");

        HelpMarker("Mitchell phase-aligns the full-resolution Color input to the reduced network grid."
                   "\nIt overrides Linear Color input so two low-pass filters never stack."
                   "\nThe two LINEAR switches remain available for direct Runtime A/B testing.");

        ImGui::EndDisabled();


ImGui::TreePop();
}

if(D18Layout::Fold("Exposure and HDR input", ImGuiTreeNodeFlags_SpanAvailWidth, menuResScale)) {
            ImGui::BeginDisabled(dx11);
            const char* encodingNames[] = { "Classic (default)", "Hybrid (experimental)", "Neutwo (experimental)" };
            const auto configuredEncoding = config->DlssNrHighlightEncoding.value_or_default();
            const int encodingCount = vulkan ? 3 : 2;
            int encoding = configuredEncoding < (uint32_t)encodingCount ? (int)configuredEncoding : 0;
            if (D18NrUi::Combo("Highlight encoding", &encoding, encodingNames, encodingCount))
                config->DlssNrHighlightEncoding = (uint32_t)encoding;
            ImGui::EndDisabled();
            HelpMarker("DX12 and Vulkan linear HDR inputs only. DX12 offers Classic and Hybrid. Changes apply on the next NR frame and reset model history."
                       " Hybrid preserves midtones; Neutwo compresses the full range."
                       " Both keep the existing composition and highlight guard; neither uses raw replacement."
                       " Save Settings stores the selection for this game. Tone-mapped inputs bypass it.");
            const auto nativeExposure=NativeControl::Read();
            const bool exposureReady=vulkan ? ExposureReadyVk() : dx11 ? nativeExposure.exposure>1e-6f : true;
            // DX11 starts readback only after this request. Do not require a
            // completed readback to enable the very control that starts it.
            ImGui::BeginDisabled(!exposureReady && !dx11);
            bool fromExposure = native ? config->DlssNrWhitePointFromExposure.value_or(false) : config->DlssNrWhitePointFromExposure.value_or_default();
            if (D18NrUi::Checkbox("Use game exposure", &fromExposure))
                config->DlssNrWhitePointFromExposure = fromExposure;

            ImGui::EndDisabled();
            if (native && !exposureReady) D18Ui::TextDisabled("Game exposure requires a supported texture and a completed readback; paper white is manual until then.");
            const auto ex = dx12Snapshot.exposure;
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


ImGui::TreePop();
}

if(D18Layout::Fold("Temporal stability", ImGuiTreeNodeFlags_SpanAvailWidth, menuResScale)) {
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



ImGui::TreePop();
}

    }
    ImGui::PopItemWidth();
}
} // namespace DlssNr
