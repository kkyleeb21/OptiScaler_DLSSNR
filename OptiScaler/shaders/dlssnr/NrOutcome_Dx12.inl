// Included under g_nrMutex, beside the shared DX12 renderer state.
DlssNr::NrContinuity g_continuity;
DlssNr::NrOutcomeSummary g_outcomeSummary;
uint64_t g_outcomeAttempt=0;
DlssNr::NrOutcome* g_activeOutcome=nullptr;
struct {DlssNr::Submission::Token ticket;uint64_t source=0,list=0;} g_lastComposedRecording;
void EmitOutcome(DlssNr::Diagnostics::Mode mode,const DlssNr::Diagnostics::Event& e) {
    DlssNr::Diagnostics::Record(mode,e);
}
const char* OutcomeFallback(const char* reason) {
    if(std::strstr(reason,"identical parameters"))return "shared_parameters_mismatch";
    if(std::strstr(reason,"video-memory"))return "video_memory_headroom";
    if(std::strstr(reason,"scratch headroom"))return "scratch_headroom";
    if(std::strstr(reason,"scratch allocation"))return "scratch_allocation_failed";
    if(std::strstr(reason,"scratch awaits"))return "scratch_pending";
    if(std::strstr(reason,"motion resource awaits"))return "shared_motion_pending";
    if(std::strstr(reason,"Pass edit awaits"))return "pass_edit_pending";
    if(std::strstr(reason,"creation awaits"))return "pass_creation_pending";
    if(std::strstr(reason,"generation awaits"))return "retired_generation_pending";
    if(std::strstr(reason,"parameter allocation"))return "pass_parameters_failed";
    if(std::strstr(reason,"shader unavailable"))return "multipass_shader_failed";
    if(std::strstr(reason,"Additional pass unavailable"))return "pass_blocked";
    if(std::strstr(reason,"evaluation failed"))return "additional_evaluate_failed";
    return "ready_prefix";
}
struct NrOutcomeGuard {
    const Config& cfg;
    DlssNr::NrOutcome value{};
    bool prepared=false, sameUnsubmittedRecording=false;
    uint64_t list=0;
    NrOutcomeGuard(const Config& config,uint64_t source,uint64_t commandList=0):cfg(config),list(commandList) {
        value.source=source;value.attempt=++g_outcomeAttempt;
        value.requested=DlssNr::Multipass::Count(cfg.DlssNrPassCount.value_or_default(),cfg.DlssNrHighResolution.value_or_default());
        value.requestedShared=cfg.DlssNrSharedHistory.value_or_default();
        g_activeOutcome=&value;
        // Clear stale progress even if this attempt exits before PrepareAdditional.
        g_multi.recorded=0;
    }
    ~NrOutcomeGuard() {
        if(prepared){value.ready=g_multi.ready;value.evaluated=g_multi.recorded;value.shared=g_multi.shared;}
        // Only successful Evaluate calls can be counted as recorded reset applications.
        value.resetMask &= (1u<<value.evaluated)-1u;
        if(value.composed) value.Reason(value.composed<value.requested ?
            (g_multi.reason[0]?OutcomeFallback(g_multi.reason):"ready_prefix") : "composed");
        else if(std::strcmp(value.reason,"pre_model_return")==0) {
            if(g_multi.reason[0] && prepared)value.Reason(OutcomeFallback(g_multi.reason));
            else if(g_nr.failed)value.Reason("renderer_failed");
        }
        // Multiple callbacks on one still-unsubmitted recording do not prove a new temporal frame.
        if(!sameUnsubmittedRecording)g_continuity.Finish(value.source,value.composed!=0);
        if(value.composed)g_lastComposedRecording={g_submission,value.source,list};
        const auto mode=static_cast<DlssNr::Diagnostics::Mode>(std::min(cfg.DlssNrDiagnostics.value_or_default(),2u));
        g_outcomeSummary.Add(value,mode,GetTickCount64(),EmitOutcome);
        g_activeOutcome=nullptr;
    }
};
