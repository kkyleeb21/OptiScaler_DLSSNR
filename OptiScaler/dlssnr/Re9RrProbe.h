#pragma once
// CPU metadata observation only. No parameter writes, barriers, replay, readback or NR dispatch.
namespace DlssNr::Re9RrProbe {
inline void Inputs(NVSDK_NGX_Parameter* params, const char* phase, unsigned id, uint64_t frame) {
    if (!params) return;
    auto resource = [&](const char* primary, const char* alias) {
        ID3D12Resource* value = nullptr;
        const char* key = primary;
        const char* route = "typed";
        bool found = params->Get(primary, &value) == NVSDK_NGX_Result_Success && value;
        if (!found) { value=nullptr; key=alias; found=params->Get(alias,&value)==NVSDK_NGX_Result_Success && value; }
        if (!found) {
            void* raw=nullptr; route="untyped"; key=primary;
            found=params->Get(primary,&raw)==NVSDK_NGX_Result_Success && raw;
            if (!found) { raw=nullptr; key=alias; found=params->Get(alias,&raw)==NVSDK_NGX_Result_Success && raw; }
            value=found?static_cast<ID3D12Resource*>(raw):nullptr;
        }
        if (!found) {
            LOG_INFO("RE9 RR input: phase={} id={} frame={} resource={} present=false",phase,id,frame,primary);
            return;
        }
        const auto desc=value->GetDesc();
        LOG_INFO("RE9 RR input: phase={} id={} frame={} resource={} present=true key={} route={} ptr={} dimension={} size={}x{} array={} mips={} format={} samples={} flags=0x{:X} state=UNOBSERVED",
            phase,id,frame,primary,key,route,(void*)value,(unsigned)desc.Dimension,desc.Width,desc.Height,
            desc.DepthOrArraySize,desc.MipLevels,(unsigned)desc.Format,desc.SampleDesc.Count,(unsigned)desc.Flags);
    };
    resource("Output","DLSSD.Output");
    resource("Color","DLSSD.Color");
    resource("Depth","DLSSD.Depth");
    resource("MotionVectors","DLSSD.MotionVectors");
    auto integer = [&](const char* key) {
        unsigned value=0; const bool known=params->Get(key,&value)==NVSDK_NGX_Result_Success;
        LOG_INFO("RE9 RR field: phase={} id={} frame={} key={} known={} value={}",phase,id,frame,key,known,value);
    };
    for (const char* key : {"Width","Height","OutWidth","OutHeight","DLSS.Feature.Create.Flags",
         "DLSS.Render.Subrect.Dimensions.Width","DLSS.Render.Subrect.Dimensions.Height",
         "DLSS.Output.Subrect.Base.X","DLSS.Output.Subrect.Base.Y",
         "DLSS.Input.Color.Subrect.Base.X","DLSS.Input.Color.Subrect.Base.Y",
         "DLSS.Input.Depth.Subrect.Base.X","DLSS.Input.Depth.Subrect.Base.Y",
         "DLSS.Input.MV.Subrect.Base.X","DLSS.Input.MV.Subrect.Base.Y"}) integer(key);
    auto scalar = [&](const char* key) {
        float value=0; const bool known=params->Get(key,&value)==NVSDK_NGX_Result_Success;
        LOG_INFO("RE9 RR field: phase={} id={} frame={} key={} known={} value={}",phase,id,frame,key,known,value);
    };
    for (const char* key : {"MV.Scale.X","MV.Scale.Y","Jitter.Offset.X","Jitter.Offset.Y","DLSS.Pre.Exposure"}) scalar(key);
    int reset=0; const bool resetKnown=params->Get("Reset",&reset)==NVSDK_NGX_Result_Success;
    LOG_INFO("RE9 RR field: phase={} id={} frame={} key=Reset known={} value={}",phase,id,frame,resetKnown,reset);
}
}
