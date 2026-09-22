#pragma once
#include "DlssNr_Common.h"
namespace DlssNr::Sh0Native {
// Native SH0 owns these otherwise unused compose fields. Keep the frozen
// safety values as float uniforms, matching the DX12 reference arithmetic.
inline DlssNrConstants Constants(const DlssNrConstants& original) {
 auto c=original;
 c.MaxRatio=0.00019650281637217395f;
 c.MvScaleX=0.020636500883055946f;c.MvScaleY=0.04127300176611189f;
 c.CompareSplit=0.5390625f;c.CompareZoom=5.6875f;
 return c;
}
}
