#include <dlssnr/NativeFgPause.h>
#include <dlssnr/FgChain.h>
#include <cmath>
#include <TlHelp32.h>
#include <vector>
// Native DX12 diagnostic adapter. Included after the existing Streamline implementation.
// Returned function targets are immutable and retained; exhaustion returns the original pointer.
namespace {
namespace Fgp = DlssNr::Diagnostics;
Fgp::Mode FgProbeMode() {
    if (State::Instance().api != API::DX12 && State::Instance().swapchainApi != API::DX12)
        return Fgp::Mode::Off;
    return static_cast<Fgp::Mode>(std::min(Config::Instance()->DlssNrDiagnostics.value_or_default(), 2u));
}
#include "FgChainStreamline.inl"

decltype(&slFreeResources) fgFreeResources = nullptr;
decltype(&slAllocateResources) fgAllocateResources = nullptr;
decltype(&slShutdown) fgShutdown = nullptr;
std::atomic<bool> fgPauseLifecycleReady{false}; // Hook publication only; mode uses the single signal word.
thread_local bool fgPauseOwnOptions=false;
bool FgPauseObserve() {
    if(fgPauseOwnOptions) {DlssNr::FgPause::ResetIfEnabled();return false;}
    if(!DlssNr::FgPause::ObserveEnabled())return false;
    // Uncertain lifecycle hook coverage must never authorize suspension.
    if(fgPauseLifecycleReady.load(std::memory_order_acquire))return true;
    DlssNr::FgPause::Reset();return false;
}
struct FgCallbackSlot {
    std::atomic<PFun_slDLSSGSetOptions*> set {nullptr};
    std::atomic<PFun_slDLSSGGetState*> get {nullptr};
};
std::array<FgCallbackSlot, 4> fgCallbacks;
std::mutex fgBindMutex;
bool FgReadOptions(const sl::ViewportHandle& viewport, const sl::DLSSGOptions* options, Fgp::FgSample& s) {
    __try {
        s.context = static_cast<uint32_t>(viewport);
        if (!options || options->structVersion < 1 || options->structVersion > 5) return false;
        s.mode = static_cast<uint32_t>(options->mode); s.count = options->numFramesToGenerate;
        s.flags = static_cast<uint32_t>(options->flags);
        s.known |= Fgp::FgMode | Fgp::FgCount | Fgp::FgFlags;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool FgReadState(const sl::ViewportHandle& viewport, const sl::DLSSGState& state, Fgp::FgSample& s) {
    __try {
        s.context = static_cast<uint32_t>(viewport);
        if (state.structVersion < 1 || state.structVersion > 4) return false;
        s.aux0 = static_cast<uint32_t>(state.status); s.aux1 = state.numFramesActuallyPresented;
        s.known |= Fgp::FgAux0 | Fgp::FgAux1;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
void FgFinishOptions(Fgp::Mode mode, Fgp::FgSample& s, bool read, sl::Result result) {
    if (mode == Fgp::Mode::Off) return;
    s.result = static_cast<uint32_t>(result); s.known |= Fgp::FgReturn | Fgp::FgObserved;
    s.state = result != sl::Result::eOk ? Fgp::FgState::Failed : !read ? Fgp::FgState::Unobserved :
        s.mode == 0 ? Fgp::FgState::Off : s.mode == 1 ? Fgp::FgState::On : Fgp::FgState::Auxiliary;
    Fgp::ObserveFg(mode, s);
}
// Nested native -> plugin calls publish only the actual innermost submission once.
struct FgOptionsCall;
thread_local FgOptionsCall* fgOptionsCall=nullptr;
struct FgOptionsCall {
    FgOptionsCall* parent=nullptr;
    Fgp::Mode diagnostic=Fgp::Mode::Off;
    Fgp::FgSample requested{},submitted{};
    ChainOptions requestedDetails{},submittedDetails{};
    uint64_t pauseBefore=0;
    bool requestedRead=false,submittedRead=false,pauseObserve=false,nested=false;
    const char* path;
    FgOptionsCall(const sl::ViewportHandle& viewport,const sl::DLSSGOptions& options,const char* source):path(source) {
        parent=fgOptionsCall;fgOptionsCall=this;
        if(parent){parent->nested=true;return;}
        diagnostic=FgProbeMode();pauseObserve=FgPauseObserve();
        if(!pauseObserve && diagnostic==Fgp::Mode::Off)return;
        pauseBefore=DlssNr::FgPause::signal.load();
        requestedRead=FgReadOptions(viewport,&options,requested);
        if(diagnostic!=Fgp::Mode::Off)requestedDetails=ChainReadOptions(options);
    }
    ~FgOptionsCall(){fgOptionsCall=parent;}
    void Submit(const sl::ViewportHandle& viewport,const sl::DLSSGOptions& options) {
        auto* root=this;while(root->parent)root=root->parent;
        if(root->pauseObserve || root->diagnostic!=Fgp::Mode::Off)
            root->submittedRead=FgReadOptions(viewport,&options,root->submitted);
        if(root->diagnostic!=Fgp::Mode::Off)root->submittedDetails=ChainReadOptions(options);
    }
    void Finish(sl::Result result) {
        if(parent)return;
        const bool success=result==sl::Result::eOk;
        if(pauseObserve && FgPauseObserve()) {
            // Coverage is UI information only; failed reads/returns cannot authorize a pause.
            if((pauseBefore & ~DlssNr::FgPause::payloadMask)==
               (DlssNr::FgPause::signal.load() & ~DlssNr::FgPause::payloadMask))
                DlssNr::FgPause::observedPaths.fetch_or((std::strcmp(path,"plugin")==0?2u:1u)|(nested?2u:0u),std::memory_order_relaxed);
            DlssNr::FgPause::Accept(pauseBefore,uint32_t(submitted.context),submitted.mode,submittedRead,success);
        }
        if(diagnostic==Fgp::Mode::Off)return;
        submitted.source=Fgp::FgSource::SlOptions;
        FgFinishOptions(diagnostic,submitted,submittedRead,result);
        const auto chainMode=Fgp::ChainMode();
        Fgp::ChainPair(chainMode,requestedRead?requested.mode:15,submittedRead?submitted.mode:15);
        Fgp::ChainSample(chainMode,"options",{requestedRead?requested.mode:15,submittedRead?submitted.mode:15,requested.count,submitted.count},uint32_t(result),!success);
        ChainOptionsEmit(chainMode,requestedDetails,false,uint32_t(result));
        ChainOptionsEmit(chainMode,submittedDetails,true,uint32_t(result));
        // Content changes only; call/failure totals remain in the per-second options windows.
        static std::mutex mutex;static std::array<uint64_t,5> previous{};static bool seen=false;
        const std::array<uint64_t,5> signature{requestedDetails.fingerprint,submittedDetails.fingerprint,requested.context,
            uint32_t(result),uint64_t(requestedRead)|(uint64_t(submittedRead)<<1)};
        bool emit=false;{std::lock_guard lock(mutex);emit=!seen || previous!=signature;previous=signature;seen=true;}
        if(!emit)return;
        Fgp::Event e{};e.type="fg_options";e.commandList=requested.context;
        e.height=requested.mode;e.networkWidth=submitted.mode;
        e.guideWidth=requested.count;e.guideHeight=submitted.count;e.result=uint32_t(result);
        e.flags=(requestedRead?1u:0u)|(submittedRead?2u:0u);
        e.reason=path;Fgp::Record(diagnostic,e);
    }
};
template<size_t I> sl::Result FgSet(const sl::ViewportHandle& viewport, const sl::DLSSGOptions& options) {
    FgOptionsCall call(viewport,options,"native");
    call.Submit(viewport,options);
    const auto result = fgCallbacks[I].set.load(std::memory_order_acquire)(viewport, options);
    call.Finish(result);
    return result;
}
template<size_t I> sl::Result FgGet(const sl::ViewportHandle& viewport, sl::DLSSGState& state,
                                  const sl::DLSSGOptions* options) {
    const auto mode = FgProbeMode();
    Fgp::FgSample s {}; s.source = Fgp::FgSource::SlState;
    if (mode != Fgp::Mode::Off) FgReadOptions(viewport, options, s);
    const auto result = fgCallbacks[I].get.load(std::memory_order_acquire)(viewport, state, options);
    if (mode != Fgp::Mode::Off) {
        ChainState(viewport,state,result);
        const bool read = result == sl::Result::eOk && FgReadState(viewport, state, s);
        s.result = static_cast<uint32_t>(result); s.known |= Fgp::FgReturn | Fgp::FgObserved;
        s.state = result != sl::Result::eOk ? Fgp::FgState::Failed :
            read ? Fgp::FgState::Auxiliary : Fgp::FgState::Unobserved;
        Fgp::ObserveFg(mode, s);
    }
    return result;
}
constexpr std::array<PFun_slDLSSGSetOptions*, 4> fgSetThunks { FgSet<0>, FgSet<1>, FgSet<2>, FgSet<3> };
constexpr std::array<PFun_slDLSSGGetState*, 4> fgGetThunks { FgGet<0>, FgGet<1>, FgGet<2>, FgGet<3> };
bool FgRetain(void* address) {
    HMODULE module = nullptr;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
        reinterpret_cast<LPCWSTR>(address), &module) != FALSE;
}
void FgBind(const char* name, void*& function) {
    if (!name || !function) return;
    const bool set = std::strcmp(name, "slDLSSGSetOptions") == 0;
    const bool get = std::strcmp(name, "slDLSSGGetState") == 0;
    if (!set && !get) return;
    // A lookup may run under the loader lock. Never wait behind a thread retaining a module.
    std::unique_lock lock(fgBindMutex, std::try_to_lock);
    if (!lock.owns_lock()) { Fgp::FgCoverage(FgProbeMode(), "callback/busy_original_preserved"); return; }
    for (size_t i = 0; i < fgCallbacks.size(); ++i) {
        void* target = set ? reinterpret_cast<void*>(fgCallbacks[i].set.load()) :
                            reinterpret_cast<void*>(fgCallbacks[i].get.load());
        void* thunk = set ? reinterpret_cast<void*>(fgSetThunks[i]) : reinterpret_cast<void*>(fgGetThunks[i]);
        if (function == thunk) return;
        if (function == target) { function = thunk; return; }
    }
    for (size_t i = 0; i < fgCallbacks.size(); ++i) {
        if ((set && !fgCallbacks[i].set.load()) || (get && !fgCallbacks[i].get.load())) {
            if (!FgRetain(function)) { Fgp::FgCoverage(FgProbeMode(), "callback/module_lease_failed", GetLastError()); return; }
            if (set) { fgCallbacks[i].set.store(reinterpret_cast<PFun_slDLSSGSetOptions*>(function), std::memory_order_release);
                function = reinterpret_cast<void*>(fgSetThunks[i]); }
            else { fgCallbacks[i].get.store(reinterpret_cast<PFun_slDLSSGGetState*>(function), std::memory_order_release);
                function = reinterpret_cast<void*>(fgGetThunks[i]); }
            Fgp::FgCoverage(FgProbeMode(), set ? "lookup/slDLSSGSetOptions" : "lookup/slDLSSGGetState");
            return;
        }
    }
    Fgp::FgCoverage(FgProbeMode(), "callback/target_capacity_original_preserved");
}
decltype(&slGetFeatureFunction) fgFeatureFunction = nullptr;
decltype(&slSetFeatureLoaded) fgFeatureLoaded = nullptr;
sl::Result FgFeatureFunction(sl::Feature feature, const char* name, void*& function) {
    const auto result = fgFeatureFunction(feature, name, function);
    if(result==sl::Result::eOk && (feature==sl::kFeatureReflex || feature==sl::kFeaturePCL))ChainBind(name,function);
    const auto steam = KernelBaseProxy::GetModuleHandleA_()("gameoverlayrenderer64.dll");
    if (steam && Util::GetCallerModule(_ReturnAddress()) == steam) return result;
    // kFeatureDLSS_G is public Streamline's FG feature, no extra query is performed.
    if (feature == sl::kFeatureDLSS_G && (Config::Instance()->DlssNrDiagnostics.value_or_default() != 0 ||
        DlssNr::FgPause::Enabled())) {
        if (result == sl::Result::eOk) FgBind(name, function);
        else Fgp::FgCoverage(static_cast<Fgp::Mode>(std::min(Config::Instance()->DlssNrDiagnostics.value_or_default(), 2u)),
            "lookup/failed_original_preserved", static_cast<uint32_t>(result));
    }
    return result;
}
sl::Result FgFeatureLoaded(sl::Feature feature, bool loaded) {
    if(feature==sl::kFeatureDLSS_G)DlssNr::FgPause::ResetIfEnabled();
    const auto result = fgFeatureLoaded(feature, loaded);
    if(feature==sl::kFeatureDLSS_G)DlssNr::FgPause::ResetIfEnabled();
    if (feature == sl::kFeatureDLSS_G) {
        Fgp::FgSample s {}; s.source = Fgp::FgSource::SlLoaded;
        s.known = Fgp::FgReturn | Fgp::FgObserved | Fgp::FgAux0;
        s.aux0 = loaded; s.result = static_cast<uint32_t>(result);
        s.state = result == sl::Result::eOk ? Fgp::FgState::Auxiliary : Fgp::FgState::Failed;
        Fgp::ObserveFg(FgProbeMode(), s);
    }
    Fgp::ChainSample(Fgp::ChainMode(),"slSetFeatureLoaded",{feature,loaded?1u:0u,0,0},uint32_t(result),result!=sl::Result::eOk);
    return result;
}
sl::Result FgFreeResources(sl::Feature feature,const sl::ViewportHandle& viewport) {
    if(feature==sl::kFeatureDLSS_G)DlssNr::FgPause::ResetIfEnabled();
    const auto result=fgFreeResources(feature,viewport);
    Fgp::ChainSample(Fgp::ChainMode(),"slFreeResources",{feature,uint32_t(viewport),0,0},uint32_t(result),result!=sl::Result::eOk);
    if(feature==sl::kFeatureDLSS_G)DlssNr::FgPause::ResetIfEnabled();
    return result;
}
sl::Result FgAllocateResources(sl::CommandBuffer* cmd,sl::Feature feature,const sl::ViewportHandle& viewport) {
    if(feature==sl::kFeatureDLSS_G)DlssNr::FgPause::ResetIfEnabled();
    const auto result=fgAllocateResources(cmd,feature,viewport);
    Fgp::ChainSample(Fgp::ChainMode(),"slAllocateResources",{feature,uint32_t(viewport),0,0},uint32_t(result),result!=sl::Result::eOk);
    if(feature==sl::kFeatureDLSS_G)DlssNr::FgPause::ResetIfEnabled();
    return result;
}
sl::Result FgShutdown() {
    // Streamline must shut down before DXGI/D3D12 destruction. Latch before forwarding.
    DlssNr::FgPause::ResetIfEnabled();
    Fgp::ChainFlush(Fgp::ChainMode());
    return fgShutdown();
}
}
namespace {
std::atomic<HMODULE> fgNativeModule{nullptr};
void FgInstallStatus(DlssNr::FgPause::Installation state,long error=0) {
    DlssNr::FgPause::installError.store(error,std::memory_order_relaxed);
    DlssNr::FgPause::installation.store(state,std::memory_order_release);
}
// Prepare handles before suspending any other thread. Keep them alive through commit/abort.
// Hot installation is outside loader discovery; startup keeps its existing current-thread update.
struct FgHookThreads {
    std::vector<HANDLE> handles;
    ~FgHookThreads(){for(auto handle:handles)CloseHandle(handle);}
    long Prepare(bool hot) {
        if(!hot)return NO_ERROR;
        const auto snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);
        if(snapshot==INVALID_HANDLE_VALUE)return GetLastError();
        THREADENTRY32 entry{};entry.dwSize=sizeof(entry);
        long error=NO_ERROR;
        if(!Thread32First(snapshot,&entry))error=GetLastError();
        else do {
            if(entry.th32OwnerProcessID!=GetCurrentProcessId() || entry.th32ThreadID==GetCurrentThreadId())continue;
            const auto handle=OpenThread(THREAD_SUSPEND_RESUME|THREAD_GET_CONTEXT|THREAD_SET_CONTEXT|THREAD_QUERY_INFORMATION,FALSE,entry.th32ThreadID);
            if(!handle){const auto code=GetLastError();if(code==ERROR_INVALID_PARAMETER)continue;error=code;break;}
            handles.push_back(handle);
        } while(Thread32Next(snapshot,&entry));
        if(!error && GetLastError()!=ERROR_NO_MORE_FILES)error=GetLastError();
        CloseHandle(snapshot);return error;
    }
    long Update() {
        auto error=DetourUpdateThread(GetCurrentThread());
        if(error)return error;
        for(auto handle:handles){error=DetourUpdateThread(handle);if(error)return error;}
        return NO_ERROR;
    }
};
}
void StreamlineHooks::enableNativeFgPause() {
    if(!DlssNr::FgPause::Eligible())return; // StatusText names the unsupported condition.
    auto module=fgNativeModule.load(std::memory_order_acquire);
    if(!module)module=KernelBaseProxy::GetModuleHandleA_()("sl.interposer.dll");
    if(!module){FgInstallStatus(DlssNr::FgPause::Installation::NoModule);return;}
    probeNativeFg(module,true);
}
void StreamlineHooks::probeNativeFg(HMODULE module,bool hotInstall) {
    using Installation=DlssNr::FgPause::Installation;
    if(module)fgNativeModule.store(module,std::memory_order_release);
    const auto mode = static_cast<Fgp::Mode>(std::min(Config::Instance()->DlssNrDiagnostics.value_or_default(), 2u));
    if ((mode == Fgp::Mode::Off && !DlssNr::FgPause::Enabled()) || !module) return;
    // Native-only observation, preserving replacement and explicit override behavior.
    if (State::Instance().activeFgInput != FGInput::NoFG || State::Instance().activeFgOutput != FGOutput::NoFG) return;
    static std::recursive_mutex installMutex;
    std::unique_lock lock(installMutex, std::try_to_lock);
    if (!lock.owns_lock()) {if(!fgPauseLifecycleReady.load())FgInstallStatus(Installation::Busy);return;}
    static bool installing=false;
    if(installing)return; // Version discovery may reenter on this same thread.
    if(fgPauseLifecycleReady.load(std::memory_order_acquire)){FgInstallStatus(Installation::Ready);return;}
    if (Config::Instance()->SkipStreamlineHooks.value_or_default()) {
        Fgp::FgCoverage(mode, "interposer/SkipStreamlineHooks_preserved"); return;
    }
    struct Scope{bool& flag;Scope(bool& f):flag(f){flag=true;}~Scope(){flag=false;}} scope(installing);
    FgInstallStatus(Installation::Installing);
    wchar_t path[MAX_PATH] {}; version_t version {};
    if (!GetModuleFileNameW(module, path, MAX_PATH)) {FgInstallStatus(Installation::PathUnavailable,GetLastError());Fgp::FgCoverage(mode, "interposer/path_unavailable", GetLastError()); return;}
    if (!Util::GetFileVersion(path, &version)) {FgInstallStatus(Installation::VersionUnavailable);Fgp::FgCoverage(mode, "interposer/version_unavailable"); return;}
    if (version.major != 2) {FgInstallStatus(Installation::UnsupportedVersion);Fgp::FgCoverage(mode, "interposer/only_SL2_supported"); return;}
    auto getter = KernelBaseProxy::GetProcAddress_()(module, "slGetFeatureFunction");
    auto loaded = KernelBaseProxy::GetProcAddress_()(module, "slSetFeatureLoaded");
    auto freeResources=KernelBaseProxy::GetProcAddress_()(module,"slFreeResources");
    auto allocateResources=KernelBaseProxy::GetProcAddress_()(module,"slAllocateResources");
    auto shutdown=KernelBaseProxy::GetProcAddress_()(module,"slShutdown");
    // Complete lifecycle coverage is required; a partial observer never authorizes suspension.
    if (!getter || !loaded || !freeResources || !allocateResources || !shutdown) {
        FgInstallStatus(Installation::ExportsUnavailable);Fgp::FgCoverage(mode,"interposer/lifecycle_export_missing");return;
    }
    if(!FgRetain(reinterpret_cast<void*>(getter))) {
        FgInstallStatus(Installation::LeaseFailed,GetLastError());Fgp::FgCoverage(mode,"interposer/module_lease_failed",GetLastError());return;
    }
    FgHookThreads threads;
    if(const auto error=threads.Prepare(hotInstall)) {FgInstallStatus(Installation::ThreadFailed,error);return;}
    fgFeatureFunction=reinterpret_cast<decltype(fgFeatureFunction)>(getter);
    fgFeatureLoaded=reinterpret_cast<decltype(fgFeatureLoaded)>(loaded);
    fgFreeResources=reinterpret_cast<decltype(fgFreeResources)>(freeResources);
    fgAllocateResources=reinterpret_cast<decltype(fgAllocateResources)>(allocateResources);
    fgShutdown=reinterpret_cast<decltype(fgShutdown)>(shutdown);
    const auto result = HookLifecycle::Transact(
        [] { return DetourTransactionBegin(); }, [&threads] { return threads.Update(); },
        [] { auto code=DetourAttach(&(PVOID&)fgFeatureFunction,FgFeatureFunction);if(code)return code;
            code=DetourAttach(&(PVOID&)fgFeatureLoaded,FgFeatureLoaded);if(code)return code;
            code=DetourAttach(&(PVOID&)fgFreeResources,FgFreeResources);if(code)return code;
            code=DetourAttach(&(PVOID&)fgAllocateResources,FgAllocateResources);if(code)return code;
            return DetourAttach(&(PVOID&)fgShutdown,FgShutdown); },
        [] { return DetourTransactionCommit(); }, [] { return DetourTransactionAbort(); });
    Fgp::FgCoverage(mode,result?"interposer/attached_getter_and_loaded":result.stage,uint32_t(result.code));
    if(result) {
        DlssNr::FgPause::ResetIfEnabled();
        fgPauseLifecycleReady.store(true,std::memory_order_release);
        FgInstallStatus(Installation::Ready);
        // The P3 observers remain default-off. Hot P1 enable adds no P3 hooks or ring writes.
        if(!hotInstall)ChainInstall(module);
    } else {
        fgFeatureFunction=nullptr;fgFeatureLoaded=nullptr;fgFreeResources=nullptr;fgAllocateResources=nullptr;fgShutdown=nullptr;
        const auto state=std::strcmp(result.stage,"begin")==0?Installation::BeginFailed:
            std::strcmp(result.stage,"update_thread")==0?Installation::UpdateFailed:
            std::strcmp(result.stage,"modify")==0?Installation::AttachFailed:Installation::CommitFailed;
        FgInstallStatus(state,result.code);
    }
}
