#pragma once
#include <imgui/imgui.h>
#include <algorithm>

namespace D18Layout {
inline ImVec2 DiagnosticPosition(ImVec2 mainPos,ImVec2 mainSize,ImVec2 diagnostic,ImVec2 display,float gap) {
    ImVec2 p{mainPos.x+mainSize.x+gap,mainPos.y};
    if(p.x+diagnostic.x>display.x) {
        p.x=mainPos.x-diagnostic.x-gap;
        if(p.x<0)p={mainPos.x,mainPos.y+mainSize.y+gap};
    }
    // A display with no free adjacent region may overlap the lower edge, but the
    // main window never shrinks and the diagnostic title/close button stays on screen.
    p.x=std::clamp(p.x,0.f,std::max(0.f,display.x-diagnostic.x));
    p.y=std::clamp(p.y,0.f,std::max(0.f,display.y-diagnostic.y));
    return p;
}
}
