#pragma once
#include "D18Localization.h"
#include <imgui/imgui_internal.h>
#include <algorithm>
#include <cmath>

// Only the NR panel calls these wrappers. Annotation items must not replace
// LastItemData: deferred sliders commit on IsItemDeactivatedAfterEdit().
namespace D18NrUi {
inline void Annotate(const char* label) { D18Ui::Describe(label); }
#define D18_NR_HINT_WIDGET(name) template<class... A> inline bool name(const char* label,A&&... args) { \
    const bool changed=D18Ui::name(label,std::forward<A>(args)...); Annotate(label); return changed; }
D18_NR_HINT_WIDGET(Checkbox)
D18_NR_HINT_WIDGET(SliderInt)
D18_NR_HINT_WIDGET(Button)
#undef D18_NR_HINT_WIDGET
inline bool SliderFloat(const char* label,float* value,float min,float max,const char* format="%.3f",ImGuiSliderFlags flags=0){
    const std::string base=std::string(label).substr(0,std::string(label).find("##"));
    D18Ui::TextUnformatted(base.c_str());D18Ui::Describe(label);
    char formatted[96];std::snprintf(formatted,sizeof(formatted),format,*value);
    if(base=="Network ratio")std::snprintf(formatted,sizeof(formatted),"%g%%",std::round(*value*1000.0f)/10.0f);
    const float width=ImGui::GetContentRegionAvail().x;
    const float labelEnd=ImGui::GetItemRectMax().x;
    const float right=ImGui::GetCurrentWindow()->WorkRect.Max.x;
    if(labelEnd+ImGui::CalcTextSize(formatted).x+16<right){ImGui::SameLine();ImGui::SetCursorScreenPos(ImVec2(right-ImGui::CalcTextSize(formatted).x,ImGui::GetCursorScreenPos().y));}
    ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_CheckMark),"%s",formatted);
    ImGui::PushID(label);
    ImGui::PushStyleColor(ImGuiCol_FrameBg,ImVec4(0,0,0,0));ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,ImVec4(0,0,0,0));ImGui::PushStyleColor(ImGuiCol_FrameBgActive,ImVec4(0,0,0,0));
    ImGui::PushStyleColor(ImGuiCol_SliderGrab,ImVec4(0,0,0,0));ImGui::PushStyleColor(ImGuiCol_SliderGrabActive,ImVec4(0,0,0,0));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize,0);
    ImGui::SetNextItemWidth(width);
    const bool editing=ImGui::TempInputIsActive(ImGui::GetID("##value"));
    if(!editing)ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(0,0,0,0));
    const bool changed=ImGui::SliderFloat("##value",value,min,max,format,flags);
    if(!editing)ImGui::PopStyleColor();
    const bool editingNow=ImGui::TempInputIsActive(ImGui::GetID("##value"));
    ImGui::PopStyleVar();ImGui::PopStyleColor(5);ImGui::PopID();
    if(editingNow){Annotate(label);return changed;}
    const auto rect=ImGui::GetCurrentContext()->LastItemData.Rect;
    auto* draw=ImGui::GetWindowDrawList();const float radius=ImGui::GetFontSize()*.35f;
    const float left=rect.Min.x+radius,rightEdge=rect.Max.x-radius,y=(rect.Min.y+rect.Max.y)*.5f;
    float fraction=(*value-min)/(max-min);
    if((flags&ImGuiSliderFlags_Logarithmic)&&min>0&&*value>0)fraction=std::log(*value/min)/std::log(max/min);
    fraction=std::clamp(fraction,0.0f,1.0f);const float x=left+(rightEdge-left)*fraction;
    draw->AddRectFilled(ImVec2(left,y-3),ImVec2(rightEdge,y+3),ImGui::GetColorU32(ImGuiCol_Border),3);
    draw->AddRectFilled(ImVec2(left,y-3),ImVec2(x,y+3),ImGui::GetColorU32(ImGuiCol_CheckMark),3);
    draw->AddCircleFilled(ImVec2(x,y),radius,ImGui::GetColorU32(ImGuiCol_CheckMark));
    Annotate(label);return changed;
}
inline bool Combo(const char* label,int* selected,const char* const options[],int count,int height=-1){
    const std::string base=std::string(label).substr(0,std::string(label).find("##"));
    ImGui::AlignTextToFramePadding();
    D18Ui::TextUnformatted(base.c_str());D18Ui::Describe(label);ImGui::PushID(label);ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    const bool changed=D18Ui::Combo("##selection",selected,options,count,height);ImGui::PopID();Annotate(label);return changed;
}
}
