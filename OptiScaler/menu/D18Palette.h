#pragma once
#include <imgui/imgui.h>
#include <algorithm>
#include <cmath>

namespace D18Ui {
inline ImVec4 PaletteRgb(int r,int g,int b,float a=1) {return {r/255.f,g/255.f,b/255.f,a};}
inline ImVec4 Mix(ImVec4 a,ImVec4 b,float t) {return {a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t,a.w};}
inline float Luminance(ImVec4 c) {
    const auto linear=[](float v){return v<=.04045f?v/12.92f:std::pow((v+.055f)/1.055f,2.4f);};
    return .2126f*linear(c.x)+.7152f*linear(c.y)+.0722f*linear(c.z);
}
struct Palette {ImVec4 bg,surface,inset,border,text,muted,accent,strong,selected,hover,warn;};
inline Palette MakePalette(int preset,int background,ImVec4 custom,bool highContrast) {
    // RGB values and custom derivation match d18-menu-prototype.html.
    const Palette presets[]={
        {PaletteRgb(28,32,37),PaletteRgb(36,41,48),PaletteRgb(25,29,34),PaletteRgb(61,69,79),PaletteRgb(229,233,238),PaletteRgb(166,177,191),PaletteRgb(121,203,187),PaletteRgb(159,228,214),PaletteRgb(41,63,62),PaletteRgb(49,72,72),PaletteRgb(255,179,46)},
        {PaletteRgb(27,27,29),PaletteRgb(38,38,41),PaletteRgb(21,21,23),PaletteRgb(66,66,71),PaletteRgb(236,235,231),PaletteRgb(170,168,162),PaletteRgb(240,176,86),PaletteRgb(255,205,130),PaletteRgb(62,48,28),PaletteRgb(52,46,38),PaletteRgb(255,122,92)},
        {PaletteRgb(20,26,36),PaletteRgb(29,37,50),PaletteRgb(15,20,29),PaletteRgb(52,64,84),PaletteRgb(228,235,245),PaletteRgb(152,168,192),PaletteRgb(122,184,255),PaletteRgb(170,212,255),PaletteRgb(30,52,84),PaletteRgb(38,58,88),PaletteRgb(255,179,46)},
        {PaletteRgb(26,24,26),PaletteRgb(37,34,37),PaletteRgb(20,18,20),PaletteRgb(68,62,68),PaletteRgb(240,234,236),PaletteRgb(178,166,172),PaletteRgb(255,138,122),PaletteRgb(255,176,164),PaletteRgb(70,38,36),PaletteRgb(58,42,42),PaletteRgb(255,196,84)},
        // Black and white (preset 5, default): warm neutrals from d18-installer-prototype.html, dark set.
        {PaletteRgb(31,30,29),PaletteRgb(42,41,39),PaletteRgb(24,23,22),PaletteRgb(64,62,58),PaletteRgb(228,226,220),PaletteRgb(176,172,163),PaletteRgb(232,230,224),PaletteRgb(255,255,255),PaletteRgb(72,70,64),PaletteRgb(58,56,52),PaletteRgb(255,179,46)}
    };
    Palette p=presets[preset==5?4:std::clamp(preset==4?background:preset,0,3)];
    if(preset==4) {
        custom.x=std::clamp(custom.x,0.f,1.f);custom.y=std::clamp(custom.y,0.f,1.f);custom.z=std::clamp(custom.z,0.f,1.f);custom.w=1;
        for(int i=0;i<12&&(Luminance(custom)+.05f)/(Luminance(p.bg)+.05f)<4.5f;++i) custom=Mix(custom,PaletteRgb(255,255,255),.15f);
        p.accent=custom;p.strong=Mix(custom,PaletteRgb(255,255,255),.35f);p.selected=Mix(p.bg,custom,.24f);p.hover=Mix(p.bg,custom,.16f);
    }
    // Slightly off-white: pure white read as harsh over HDR game images (user, 2026-10-05).
    if(highContrast){p.text=PaletteRgb(232,232,228);p.muted=PaletteRgb(192,196,202);}
    return p;
}
inline float AutoMenuScale(float height) {
    // Preserve the original sub-900p steps, then keep 1x through 1440p.
    if(height<900) return std::clamp(float(int(height/108))/10.f,.5f,1.f);
    return std::clamp(1.f+std::max(0.f,height-1440.f)/1440.f,1.f,2.f);
}
}
