#include <dlssnr/NrOutcome.h>
#include <dlssnr/MultipassPolicy.h>
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>
#include <memory>

struct Captured { DlssNr::Diagnostics::Event e; std::string reason; };
std::vector<Captured> captured;
namespace DlssNr::Diagnostics {
void Record(Mode mode,const Event& e,bool) {
    assert(mode!=Mode::Off);captured.push_back({e,e.reason});
}
}
uint64_t now=1000;
uint64_t GetTickCount64(){return now;}
template<class T> struct Option {T value;T value_or_default() const{return value;}};
struct Config {
    Option<unsigned> DlssNrPassCount{2},DlssNrDiagnostics{1};
    Option<bool> DlssNrHighResolution{false},DlssNrSharedHistory{false};
};
struct {unsigned ready=1,recorded=0;bool shared=false;const char* reason="";} g_multi;
struct {bool failed=false;} g_nr;
namespace DlssNr::Submission {using Token=std::shared_ptr<int>;}
DlssNr::Submission::Token g_submission;
#include "../../OptiScaler/shaders/dlssnr/NrOutcome_Dx12.inl"

int main() {
    using namespace DlssNr;
    NrContinuity h;
    assert(h.ResetFor(10)==HistoryReset::FirstUse);
    h.Finish(10,true);assert(h.ResetFor(10)==HistoryReset::None);
    h.Finish(20,false);assert(h.ResetFor(10)==HistoryReset::None); // unrelated rejected view
    h.Finish(10,false);assert(h.ResetFor(10)==HistoryReset::RecordingGap);
    h.Finish(10,false);assert(h.ResetFor(10)==HistoryReset::RecordingGap); // repeated pending frames
    h.Finish(10,true);assert(h.ResetFor(10)==HistoryReset::None);
    assert(h.ResetFor(20)==HistoryReset::SourceChanged);
    h.Finish(20,true);assert(h.ResetFor(10)==HistoryReset::SourceChanged);
    h.Disable();assert(h.ResetFor(20)==HistoryReset::Disabled);
    h.Finish(20,true);h.Release(10);assert(h.ResetFor(20)==HistoryReset::None);
    h.Release(20);assert(h.ResetFor(20)==HistoryReset::Released);
    h.Finish(20,true);assert(h.ResetFor(20)==HistoryReset::None);
    h.Finish(0,true);h.Finish(0,false);assert(h.ResetFor(0)==HistoryReset::RecordingGap);

    Config cfg;
    // Actual production scope: early return after an accepted source invalidates its history,
    // other-source early returns do not; completed prefixes are distinguished from full SR.
    {NrOutcomeGuard guard(cfg,10);guard.prepared=true;g_multi.ready=2;g_multi.recorded=2;guard.value.composed=2;}
    assert(g_continuity.ResetFor(10)==HistoryReset::None);
    {NrOutcomeGuard guard(cfg,10);guard.sameUnsubmittedRecording=true;guard.value.Reason("duplicate_recording_pending");}
    assert(g_continuity.ResetFor(10)==HistoryReset::None);
    {NrOutcomeGuard guard(cfg,20);guard.value.Reason("submission_pending");}
    assert(g_continuity.ResetFor(10)==HistoryReset::None);
    {NrOutcomeGuard guard(cfg,10);guard.value.Reason("submission_pending");}
    assert(g_continuity.ResetFor(10)==HistoryReset::RecordingGap);
    {NrOutcomeGuard guard(cfg,10);guard.prepared=true;g_multi.ready=1;g_multi.recorded=1;
        guard.value.resetMask=1;guard.value.history=g_continuity.ResetFor(10);guard.value.composed=1;}
    assert(g_continuity.ResetFor(10)==HistoryReset::None);
    assert(g_activeOutcome==nullptr);
    g_outcomeSummary.Flush(EmitOutcome);
    assert(captured.size()==5);captured.clear();

    NrOutcomeSummary summary;
    NrOutcome o{};o.source=10;o.width=3840;o.height=2160;o.requested=2;o.ready=2;
    // High-frequency alternation is aggregated, not discarded by a transition rate limiter.
    for(unsigned i=0;i<600;++i){o.attempt=i+1;o.evaluated=i%2?0:2;o.composed=o.evaluated;
        o.Reason(i%2?"descriptor_busy":"composed");summary.Add(o,Diagnostics::Mode::Summary,now,EmitOutcome);}
    assert(captured.empty());
    summary.Flush(EmitOutcome);assert(captured.size()==2);
    for(const auto& c:captured)assert(c.reason.find(";n=300;")!=std::string::npos);
    // Reduced prefix and history reset have separate counts and all four pass bits survive.
    o.attempt=601;o.requested=4;o.ready=2;o.evaluated=2;o.composed=2;o.resetMask=3;
    o.history=HistoryReset::RecordingGap;o.Reason("ready_prefix");
    summary.Add(o,Diagnostics::Mode::Summary,now,EmitOutcome);
    summary.Flush(EmitOutcome);
    for(const auto& c:captured)std::printf("EVENT|%llu|%u|%u|%u|%u|%s\n",
        static_cast<unsigned long long>(c.e.frame),c.e.flags,c.e.width,c.e.height,c.e.result,c.reason.c_str());
    captured.clear();
    summary.Mode(Diagnostics::Mode::Off,now,EmitOutcome);
    for(unsigned i=0;i<10000;++i)summary.Add(o,Diagnostics::Mode::Off,now,EmitOutcome);
    summary.Flush(EmitOutcome);assert(captured.empty());
    // Fixed capacity reports overflow explicitly rather than claiming a successful output.
    for(unsigned i=0;i<18;++i){o.source=i;summary.Add(o,Diagnostics::Mode::Summary,now,EmitOutcome);}
    summary.Flush(EmitOutcome);assert(captured.size()==17);
    assert(captured.back().reason.find("summary_overflow;n=2;")==0);captured.clear();
    summary.Add(o,Diagnostics::Mode::Trace,now,EmitOutcome);assert(captured.size()==1);
    summary.Add(o,Diagnostics::Mode::Trace,now,EmitOutcome);assert(captured.size()==2);
    puts("PASS source continuity, actual outcome scope, partial output, Summary alternation counts, overflow, Off and Trace; CPU only");
}
