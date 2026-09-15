#pragma once
#include "D18Localization.h"
#include <imgui/imgui_internal.h>
#include <algorithm>

namespace D18Layout {
inline float ChoiceWidth(const char* label,float scale,float minWidth=0) {
    return std::max(minWidth*scale,D18Ui::CalcTextSize(label,nullptr,true).x+24*scale);
}
inline bool Fold(const char* label,ImGuiTreeNodeFlags flags,float scale) {
    ImGui::Dummy(ImVec2(0,8*scale));ImGui::Separator();ImGui::Dummy(ImVec2(0,6*scale));
    const bool open=D18Ui::TreeNodeEx(label,flags);
    if(open)ImGui::Dummy(ImVec2(0,8*scale));return open;
}
inline void RowLabel(const char* label,float scale,float controlsWidth) {
    ImGui::AlignTextToFramePadding();D18Ui::TextUnformatted(label);
    const float right=ImGui::GetCurrentWindow()->WorkRect.Max.x-ImGui::GetWindowPos().x;
    if(ImGui::GetContentRegionAvail().x>controlsWidth*scale+D18Ui::CalcTextSize(label).x+24*scale) {
        ImGui::SameLine(std::max(ImGui::GetCursorPosX(),right-controlsWidth*scale));
    }
}
inline bool Choice(const char* label,bool selected,float scale,float minWidth=0) {
    if(selected){ImGui::PushStyleColor(ImGuiCol_Button,ImGui::GetStyleColorVec4(ImGuiCol_Header));
        ImGui::PushStyleColor(ImGuiCol_Border,ImGui::GetStyleColorVec4(ImGuiCol_CheckMark));
        ImGui::PushStyleColor(ImGuiCol_Text,ImGui::GetStyleColorVec4(ImGuiCol_CheckMark));}
    const bool clicked=D18Ui::Button(label,ImVec2(ChoiceWidth(label,scale,minWidth),ImGui::GetFrameHeight()+6*scale));
    if(selected)ImGui::PopStyleColor(3);return clicked;
}
// No child window: the group grows to its text, so no inner scrolling or stale
// first-frame auto-height can clip a status line. Background is drawn behind it.
inline void StatusCard(const char* id,const char* label,const char* status,ImVec4 color,const char* detail,float scale,bool pipeline) {
    ImGui::PushID(id);const auto pos=ImGui::GetCursorScreenPos();
    const float width=ImGui::GetContentRegionAvail().x,pad=14*scale;
    auto* draw=ImGui::GetWindowDrawList();ImDrawListSplitter layers;layers.Split(draw,2);layers.SetCurrentChannel(draw,1);
    ImGui::BeginGroup();ImGui::SetCursorScreenPos(ImVec2(pos.x+pad,pos.y+pad));
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX()+std::max(1.0f,width-2*pad));
    const float dot=ImGui::GetFontSize()*.25f;
    draw->AddCircleFilled(ImVec2(pos.x+pad+dot,ImGui::GetCursorScreenPos().y+ImGui::GetTextLineHeight()*.5f),dot,ImGui::GetColorU32(color));
    ImGui::Dummy(ImVec2(dot*2,ImGui::GetTextLineHeight()));ImGui::SameLine();D18Ui::TextUnformatted(label);
    if(!pipeline && ImGui::GetItemRectMax().x+D18Ui::CalcTextSize(status).x+ImGui::GetStyle().ItemSpacing.x<pos.x+width-pad) ImGui::SameLine();
    else ImGui::SetCursorScreenPos(ImVec2(pos.x+pad,ImGui::GetCursorScreenPos().y));
    D18Ui::TextColored(color,"%s",status);
    if(detail[0]) {
        if(pipeline && ImGui::GetItemRectMax().x+D18Ui::CalcTextSize(detail).x+ImGui::GetStyle().ItemSpacing.x<pos.x+width-pad) ImGui::SameLine();
        else ImGui::SetCursorScreenPos(ImVec2(pos.x+pad,ImGui::GetCursorScreenPos().y));
        D18Ui::TextDisabled("%s",detail);
    }
    const float bottom=ImGui::GetItemRectMax().y+pad;
    ImGui::PopTextWrapPos();ImGui::SetCursorScreenPos(ImVec2(pos.x,pos.y));ImGui::Dummy(ImVec2(width,bottom-pos.y));ImGui::EndGroup();
    layers.SetCurrentChannel(draw,0);draw->AddRectFilled(pos,ImVec2(pos.x+width,bottom),ImGui::GetColorU32(ImGuiCol_ChildBg),4*scale);
    draw->AddRect(pos,ImVec2(pos.x+width,bottom),ImGui::GetColorU32(ImGuiCol_Border),4*scale);layers.Merge(draw);ImGui::PopID();
}
}
