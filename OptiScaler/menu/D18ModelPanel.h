#pragma once
#include "D18Localization.h"
#include "D18Layout.h"
#include <imgui/imgui_internal.h>
#include <algorithm>
#include <dlssnr/MultipassPolicy.h>

// Same accepted layout; all active pass cards now select their own stored model settings.
class D18ModelPanel
{
    bool table=false;
    unsigned& selected;
    unsigned count;
    void Card(int index,float width,float scale,float ratio,float intensity,bool compact)
    {
        ImGui::PushID(index);
        const auto pos=ImGui::GetCursorScreenPos();
        const ImVec2 size(width,(compact?64.0f:76.0f)*scale);
        ImGui::BeginDisabled(unsigned(index)>=count);
        if(ImGui::InvisibleButton("pass-card",size))selected=unsigned(index);
        const auto helpKey=std::string("Pass ")+std::to_string(index+1);D18Ui::Describe(helpKey.c_str());
        auto* draw=ImGui::GetWindowDrawList();
        const auto fill=ImGui::GetColorU32(unsigned(index)==selected?ImGuiCol_Header:ImGuiCol_FrameBg);
        const auto edge=ImGui::GetColorU32(unsigned(index)==selected?ImGuiCol_CheckMark:ImGuiCol_Border);
        const auto ink=ImGui::GetColorU32(unsigned(index)==selected?ImGuiCol_CheckMark:ImGuiCol_TextDisabled);
        draw->AddRectFilled(pos,ImVec2(pos.x+size.x,pos.y+size.y),fill,4*scale);
        draw->AddRect(pos,ImVec2(pos.x+size.x,pos.y+size.y),edge,4*scale);
        const ImVec4 clip(pos.x+8*scale,pos.y,pos.x+size.x-8*scale,pos.y+size.y);
        const auto title=D18Ui::Format("Pass %d",index+1);
        draw->AddText(ImGui::GetFont(),ImGui::GetFontSize()*(compact?.85f:1.0f),ImVec2(clip.x,pos.y+10*scale),ink,title.c_str(),nullptr,0,&clip);
        if(!compact){const auto ratioText=D18Ui::Format("%.0f%%",ratio*100);draw->AddText(ImVec2(clip.z-ImGui::CalcTextSize(ratioText.c_str()).x,pos.y+10*scale),ink,ratioText.c_str());}
        const auto info=unsigned(index)<count?(compact?D18Ui::Format("%.0f%%",ratio*100):D18Ui::Format("Intensity %.2f",intensity)):std::string(D18Ui::Tr("Inactive - settings retained"));
        draw->AddText(ImGui::GetFont(),ImGui::GetFontSize()*0.85f,ImVec2(clip.x,pos.y+38*scale),ImGui::GetColorU32(ImGuiCol_TextDisabled),info.c_str(),nullptr,0,&clip);
        ImGui::EndDisabled();ImGui::PopID();
    }
  public:
    bool copyRequested=false;
    D18ModelPanel(bool high,float scale,const std::array<DlssNr::Multipass::Tuning,4>& tuning,unsigned active,unsigned& selection)
        :selected(selection),count(active)
    {
        const float width=ImGui::GetContentRegionAvail().x;
        if(!high && width>=510*scale){
            table=ImGui::BeginTable("D18ModelEditor",2,ImGuiTableFlags_SizingFixedFit,ImVec2(width,0));
            if(table){ImGui::TableSetupColumn("passes",ImGuiTableColumnFlags_WidthFixed,146*scale);ImGui::TableSetupColumn("parameters",ImGuiTableColumnFlags_WidthFixed,width-146*scale-ImGui::GetStyle().CellPadding.x*4);
                ImGui::TableNextColumn();for(int i=0;i<4;++i)Card(i,ImGui::GetContentRegionAvail().x,scale,tuning[i].scaling?tuning[i].ratio:1,tuning[i].intensity,false);
                ImGui::TableNextColumn();}
        }else if(!high){
            const float cardWidth=(width-ImGui::GetStyle().ItemSpacing.x*3)/4;
            for(int i=0;i<4;++i){if(i)ImGui::SameLine();Card(i,cardWidth,scale,tuning[i].scaling?tuning[i].ratio:1,tuning[i].intensity,true);}
        }
        ImGui::PushStyleColor(ImGuiCol_ChildBg,ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
        const float parentFontScale=ImGui::GetCurrentWindow()->FontWindowScale;
        ImGui::BeginChild("D18ParameterSurface",ImVec2(0,0),ImGuiChildFlags_Borders|ImGuiChildFlags_AutoResizeY,ImGuiWindowFlags_NoScrollbar);
        ImGui::SetWindowFontScale(parentFontScale);
        const auto title=D18Ui::Format("Pass %d parameters",selected+1);
        D18Layout::RowLabel(high?"Single-pass model parameters":title.c_str(),scale,150);
        ImGui::BeginDisabled(selected==0);copyRequested=D18Layout::Choice("Copy previous pass",false,scale);ImGui::EndDisabled();
        D18Ui::TextDisabled("Edits affect the current pass");
        ImGui::Spacing();
    }
    ~D18ModelPanel(){ImGui::EndChild();ImGui::PopStyleColor();if(table)ImGui::EndTable();}
};
