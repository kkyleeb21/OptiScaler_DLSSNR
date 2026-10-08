// Reads public NGX parameter keys only for registry-confirmed DX12 FG handles.
#include <dlssnr/Diagnostics.h>
#include <dlssnr/FgChain.h>
namespace {
using FgDiagMode = DlssNr::Diagnostics::Mode;
struct NgxFgProbe {
    DlssNr::Diagnostics::FgSample sample {};
    FgDiagMode mode = FgDiagMode::Off;
    NVSDK_NGX_Handle** output = nullptr;
    NVSDK_NGX_Parameter* parameters=nullptr;
    bool creation=false;
    std::array<std::array<uint64_t,4>,16> scalar{};
    std::array<std::array<uint64_t,4>,8> resource{};
    NgxFgProbe(bool enabled, bool create, NVSDK_NGX_Parameter* params, uint64_t handle,
               NVSDK_NGX_Handle** out = nullptr) : output(out) {
        if (!enabled) return;
        parameters=params;creation=create;
        mode = static_cast<FgDiagMode>(std::min(Config::Instance()->DlssNrDiagnostics.value_or_default(), 2u));
        if (mode == FgDiagMode::Off) return;
        sample.source = create ? DlssNr::Diagnostics::FgSource::NgxCreate : DlssNr::Diagnostics::FgSource::NgxEvaluate;
        sample.context = handle; sample.known = DlssNr::Diagnostics::FgObserved;
        unsigned value = 0;
        if (Read(params, "DLSSG.MultiFrameCount", value)) {
            sample.count = value; sample.known |= DlssNr::Diagnostics::FgCount;
        }
        if (Read(params, "DLSSG.NotRenderingGameFrames", value)) {
            sample.aux0 = value; sample.known |= DlssNr::Diagnostics::FgAux0;
        }
        if (Read(params, "DLSSG.MenuDetectionEnabled", value)) {
            sample.aux1 = value; sample.known |= DlssNr::Diagnostics::FgAux1;
        }
        if(DlssNr::Diagnostics::ChainMode()!=FgDiagMode::Off)ChainRead();
    }
    static bool Pointer(NVSDK_NGX_Parameter* params,const char* name,void*& value) {
        __try {
            if(!params)return false;
            ID3D12Resource* resource=nullptr;
            if(params->Get(name,&resource)==NVSDK_NGX_Result_Success){value=resource;return true;}
            return params->Get(name,&value)==NVSDK_NGX_Result_Success;
        }
        __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    void ChainRead() {
        static const char* keys[]={"DLSSG.MultiFrameCount","DLSSG.NotRenderingGameFrames","DLSSG.MenuDetectionEnabled",
            "DLSSG.Reset","DLSSG.ColorBuffersHDR","DLSSG.DepthInverted","DLSSG.CameraMotionIncluded",
            "DLSSG.MvecDilated","DLSSG.MvecJittered","DLSSG.OrthoProjection","DLSSG.MultiFrameIndex",
            "DLSSG.ResourceAlwaysProvidedFlags","DLSSG.ResourceNeverProvidedFlags","DLSSG.UserInterfaceRecompositionEnabled","DLSSG.EvalFlags","DLSSG.DynamicResolution"};
        static const char* resources[]={"DLSSG.Backbuffer","DLSSG.MVecs","DLSSG.Depth","DLSSG.HUDLess","DLSSG.UI","DLSSG.UIAlpha",
            "DLSSG.OutputInterpolated","DLSSG.OutputDisableInterpolation"};
        for(size_t i=0;i<scalar.size();++i){unsigned v=0;const bool valid=Read(parameters,keys[i],v);scalar[i]={valid?1u:0u,v,0,0};}
        for(size_t i=0;i<resource.size();++i){void* p=nullptr;const bool valid=Pointer(parameters,resources[i],p);resource[i]={valid?1u:0u,p?1u:0u,0,0};}
    }
    static bool Read(NVSDK_NGX_Parameter* params, const char* name, unsigned& value) {
        __try { return params && params->Get(name, &value) == NVSDK_NGX_Result_Success; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    NVSDK_NGX_Result Return(NVSDK_NGX_Result result) {
        if (mode == FgDiagMode::Off) return result;
        sample.result = static_cast<uint32_t>(result); sample.known |= DlssNr::Diagnostics::FgReturn;
        if (result == NVSDK_NGX_Result_Success && output && *output) sample.context = (*output)->Id;
        sample.state = result != NVSDK_NGX_Result_Success ? DlssNr::Diagnostics::FgState::Failed :
            (sample.known & (DlssNr::Diagnostics::FgCount | DlssNr::Diagnostics::FgAux0 | DlssNr::Diagnostics::FgAux1)) ?
                DlssNr::Diagnostics::FgState::Auxiliary : DlssNr::Diagnostics::FgState::Unobserved;
        DlssNr::Diagnostics::ObserveFg(mode, sample);
        const auto chainMode=DlssNr::Diagnostics::ChainMode();
        DlssNr::Diagnostics::ChainSample(chainMode,creation?"ngx.create":"ngx.evaluate",{sample.context,0,0,0},uint32_t(result),result!=NVSDK_NGX_Result_Success);
        if(!creation && chainMode!=FgDiagMode::Off) {
            static const char* keys[]={"ngx.MultiFrameCount","ngx.NotRenderingGameFrames","ngx.MenuDetectionEnabled","ngx.Reset",
                "ngx.ColorBuffersHDR","ngx.DepthInverted","ngx.CameraMotionIncluded","ngx.MvecDilated","ngx.MvecJittered","ngx.OrthoProjection",
                "ngx.MultiFrameIndex","ngx.ResourceAlwaysProvidedFlags","ngx.ResourceNeverProvidedFlags","ngx.UserInterfaceRecompositionEnabled","ngx.EvalFlags","ngx.DynamicResolution"};
            static const char* resources[]={"ngx.resource.Backbuffer","ngx.resource.MVecs","ngx.resource.Depth","ngx.resource.HUDLess",
                "ngx.resource.UI","ngx.resource.UIAlpha","ngx.resource.OutputInterpolated","ngx.resource.OutputDisableInterpolation"};
            for(size_t i=0;i<scalar.size();++i)DlssNr::Diagnostics::ChainSample(chainMode,keys[i],scalar[i],uint32_t(result),result!=NVSDK_NGX_Result_Success);
            for(size_t i=0;i<resource.size();++i)DlssNr::Diagnostics::ChainSample(chainMode,resources[i],resource[i],uint32_t(result),result!=NVSDK_NGX_Result_Success);
        }
        return result;
    }
};
}
