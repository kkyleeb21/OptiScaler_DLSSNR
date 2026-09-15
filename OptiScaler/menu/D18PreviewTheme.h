#pragma once
#include <imgui/imgui.h>
#include "D18ChineseFont.h"

// Scoped to the D18 window. Never changes game/overlay theme configuration.
// The caller supplies the existing menu HDR colour conversion.
class D18PreviewTheme
{
    int colors = 0;
    int vars = 0;
    bool fontPushed=false;
    inline static ImFont* font=nullptr;
  public:
    static void LoadFont(ImFontAtlas* atlas,float size,bool customFont)
    {
        font=nullptr;if(customFont)return;
        wchar_t windows[MAX_PATH]{};if(!GetWindowsDirectoryW(windows,MAX_PATH))return;
        std::ifstream file(std::filesystem::path(windows)/L"Fonts"/L"segoeui.ttf",std::ios::binary|std::ios::ate);
        if(!file)return;const auto length=file.tellg();if(length<=0||length>32*1024*1024)return;
        auto data=IM_ALLOC(size_t(length));if(!data)return;
        file.seekg(0);if(!file.read(static_cast<char*>(data),length)){IM_FREE(data);return;}
        ImFontConfig config;config.FontDataOwnedByAtlas=true;
        font=atlas->AddFontFromMemoryTTF(data,int(length),size,&config,atlas->GetGlyphRangesDefault());
        if(font){const bool available=D18Ui::chineseFontAvailable;D18Ui::AddChineseFont(atlas,size);D18Ui::chineseFontAvailable=available;}
    }
    template<class Convert> explicit D18PreviewTheme(float scale, Convert convert)
    {
        if(font){ImGui::PushFont(font);fontPushed=true;}
        const auto rgb=[](int r,int g,int b,float a=1.0f){return ImVec4(r/255.0f,g/255.0f,b/255.0f,a);};
        const auto color=[&](ImGuiCol id,ImVec4 value){ImGui::PushStyleColor(id,convert(value));++colors;};
        const auto number=[&](ImGuiStyleVar id,float value){ImGui::PushStyleVar(id,value);++vars;};
        const auto vector=[&](ImGuiStyleVar id,ImVec2 value){ImGui::PushStyleVar(id,value);++vars;};
        const auto bg=rgb(28,32,37),surface=rgb(36,41,48),inset=rgb(25,29,34);
        const auto border=rgb(61,69,79),text=rgb(229,233,238),muted=rgb(166,177,191);
        const auto teal=rgb(121,203,187),selected=rgb(41,63,62),hover=rgb(49,72,72);
        color(ImGuiCol_WindowBg,bg);color(ImGuiCol_ChildBg,bg);color(ImGuiCol_PopupBg,surface);
        color(ImGuiCol_Text,text);color(ImGuiCol_TextDisabled,muted);color(ImGuiCol_TextLink,teal);
        color(ImGuiCol_Border,border);color(ImGuiCol_BorderShadow,rgb(0,0,0,0));
        color(ImGuiCol_FrameBg,surface);color(ImGuiCol_FrameBgHovered,hover);color(ImGuiCol_FrameBgActive,selected);
        color(ImGuiCol_TitleBg,inset);color(ImGuiCol_TitleBgActive,inset);color(ImGuiCol_TitleBgCollapsed,inset);
        color(ImGuiCol_CheckMark,teal);color(ImGuiCol_SliderGrab,teal);color(ImGuiCol_SliderGrabActive,rgb(159,228,214));
        color(ImGuiCol_Button,surface);color(ImGuiCol_ButtonHovered,hover);color(ImGuiCol_ButtonActive,selected);
        color(ImGuiCol_Header,selected);color(ImGuiCol_HeaderHovered,hover);color(ImGuiCol_HeaderActive,selected);
        color(ImGuiCol_Tab,surface);color(ImGuiCol_TabHovered,hover);color(ImGuiCol_TabSelected,selected);
        color(ImGuiCol_TabSelectedOverline,teal);color(ImGuiCol_TabDimmed,inset);color(ImGuiCol_TabDimmedSelected,selected);
        color(ImGuiCol_Separator,border);color(ImGuiCol_SeparatorHovered,teal);color(ImGuiCol_SeparatorActive,teal);
        color(ImGuiCol_ScrollbarBg,inset);color(ImGuiCol_ScrollbarGrab,border);color(ImGuiCol_ScrollbarGrabHovered,muted);color(ImGuiCol_ScrollbarGrabActive,teal);
        color(ImGuiCol_ResizeGrip,border);color(ImGuiCol_ResizeGripHovered,teal);color(ImGuiCol_ResizeGripActive,teal);
        color(ImGuiCol_TableHeaderBg,inset);color(ImGuiCol_TableBorderStrong,border);color(ImGuiCol_TableBorderLight,border);
        color(ImGuiCol_TableRowBg,rgb(0,0,0,0));color(ImGuiCol_TableRowBgAlt,rgb(229,233,238,.025f));
        color(ImGuiCol_TextSelectedBg,rgb(121,203,187,.25f));color(ImGuiCol_NavCursor,teal);color(ImGuiCol_TreeLines,border);
        number(ImGuiStyleVar_WindowRounding,7*scale);number(ImGuiStyleVar_FrameRounding,4*scale);
        number(ImGuiStyleVar_ChildRounding,4*scale);number(ImGuiStyleVar_PopupRounding,4*scale);
        number(ImGuiStyleVar_TabRounding,4*scale);number(ImGuiStyleVar_GrabRounding,4*scale);
        number(ImGuiStyleVar_FrameBorderSize,1);number(ImGuiStyleVar_WindowBorderSize,1);number(ImGuiStyleVar_TabBorderSize,1);
        number(ImGuiStyleVar_DisabledAlpha,.5f);
        vector(ImGuiStyleVar_WindowPadding,ImVec2(18*scale,14*scale));
        vector(ImGuiStyleVar_FramePadding,ImVec2(10*scale,5*scale));
        vector(ImGuiStyleVar_ItemSpacing,ImVec2(12*scale,10*scale));
        vector(ImGuiStyleVar_ItemInnerSpacing,ImVec2(10*scale,6*scale));
    }
    ~D18PreviewTheme(){ImGui::PopStyleVar(vars);ImGui::PopStyleColor(colors);if(fontPushed)ImGui::PopFont();}
    D18PreviewTheme(const D18PreviewTheme&)=delete;
    D18PreviewTheme& operator=(const D18PreviewTheme&)=delete;
};
