#pragma once
#include <Config.h>
#include "MultipassPolicy.h"
namespace DlssNr::Multipass {
inline Tuning Read(const Config& c,unsigned index) {
    if(index==0)return Sanitize({c.DlssNrInternalScaling.value_or_default(),c.DlssNrAutoMask.value_or_default(),
        c.DlssNrInternalScalingRatio.value_or_default(),c.DlssNrIntensity.value_or_default(),c.DlssNrLocalStructure.value_or_default(),
        c.DlssNrLocalTone.value_or_default(),c.DlssNrSkinStructure.value_or_default(),c.DlssNrPreset.value_or_default(),c.DlssNrStyle.value_or_default()});
    const auto& p=c.DlssNrPasses[std::clamp(index,1u,3u)-1];
    return Sanitize({p.Scaling.value_or_default(),p.AutoMask.value_or_default(),p.Ratio.value_or_default(),p.Intensity.value_or_default(),
        p.Structure.value_or_default(),p.Tone.value_or_default(),p.Skin.value_or_default(),p.Preset.value_or_default(),p.Style.value_or_default()});
}
}
