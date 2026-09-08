#pragma once
#include "D18Translations.h"
#include <imgui/imgui.h>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <utility>
#include <cstdio>
#include <cstring>
namespace D18Ui {
inline bool chinese=false, chineseFontAvailable=false;
inline void SetLanguage(unsigned language){chinese=language==1&&chineseFontAvailable;}
inline const char* Lookup(const char* text){
 if(!text)return text;
 static const auto table=[](){std::unordered_map<std::string_view,const char*> m;for(auto& p:translations)m.emplace(p.en,p.zh);return m;}();
 auto it=table.find(text);return it==table.end()?text:it->second;
}
inline const char* Tr(const char* text){return chinese?Lookup(text):text;}
// Original English IDs survive language changes, including labels with ## suffixes.
inline const char* Label(const char* text){
 if(!text||!text[0])return text;
 std::string base=text;auto suffix=base.find("##");if(suffix!=std::string::npos)base.resize(suffix);
 const char* translated=Lookup(base.c_str());if(base==translated)return text;
 struct Labels{std::string en,zh;};static std::unordered_map<std::string,Labels> labels;
 auto [it,inserted]=labels.try_emplace(text);if(inserted){it->second.en=base+"###"+text;it->second.zh=std::string(translated)+"###"+text;}
 return (chinese?it->second.zh:it->second.en).c_str();
}
inline const char* Arg(const char* value){return Tr(value);}
inline const char* Arg(char* value){return Tr(value);}
template<class T> inline T Arg(T value){return value;}
template<class... A> inline void Text(const char* f,A... a){ImGui::Text(Tr(f),Arg(a)...);}
template<class... A> inline void TextDisabled(const char* f,A... a){ImGui::TextDisabled(Tr(f),Arg(a)...);}
template<class... A> inline void TextWrapped(const char* f,A... a){ImGui::TextWrapped(Tr(f),Arg(a)...);}
template<class... A> inline void TextColored(const ImVec4& c,const char* f,A... a){ImGui::TextColored(c,Tr(f),Arg(a)...);}
inline void TextUnformatted(const char* t,const char* end=nullptr){ImGui::TextUnformatted(end?t:Tr(t),end);}
inline void SeparatorText(const char* t){ImGui::SeparatorText(Tr(t));}
inline ImVec2 CalcTextSize(const char* t,const char* end=nullptr,bool hide=false,float wrap=-1){return ImGui::CalcTextSize(end?t:Tr(t),end,hide,wrap);}
template<class... A> inline std::string Format(const char* f,A... a){f=Tr(f);int n=std::snprintf(nullptr,0,f,Arg(a)...);if(n<0)return {};std::string s(size_t(n)+1,'\0');std::snprintf(s.data(),s.size(),f,Arg(a)...);s.resize(n);return s;}
#define D18_LABEL_WIDGET(name) template<class... A> inline bool name(const char* l,A&&... a){return ImGui::name(Label(l),std::forward<A>(a)...);}
D18_LABEL_WIDGET(SmallButton)
D18_LABEL_WIDGET(Checkbox)
D18_LABEL_WIDGET(SliderFloat)
D18_LABEL_WIDGET(SliderInt)
D18_LABEL_WIDGET(InputFloat)
D18_LABEL_WIDGET(InputInt)
D18_LABEL_WIDGET(RadioButton)
D18_LABEL_WIDGET(CollapsingHeader)
#undef D18_LABEL_WIDGET
inline bool Button(const char* l,const ImVec2& size=ImVec2(0,0)){return ImGui::Button(Label(l),size);}
inline bool Selectable(const char* l,bool selected=false,ImGuiSelectableFlags f=0,const ImVec2& size=ImVec2(0,0)){return ImGui::Selectable(Label(l),selected,f,size);}
inline bool Selectable(const char* l,bool* selected,ImGuiSelectableFlags f=0,const ImVec2& size=ImVec2(0,0)){return ImGui::Selectable(Label(l),selected,f,size);}
inline bool TreeNode(const char* l){return ImGui::TreeNode(Label(l));}
inline bool TreeNodeEx(const char* l,ImGuiTreeNodeFlags flags=0){return ImGui::TreeNodeEx(Label(l),flags);}
inline bool BeginCombo(const char* l,const char* p,ImGuiComboFlags flags=0){return ImGui::BeginCombo(Label(l),Tr(p),flags);}
inline bool Combo(const char* l,int* current,const char* const items[],int count,int height=-1){
 std::vector<const char*> values;values.reserve(count);for(int i=0;i<count;++i)values.push_back(Tr(items[i]));
 return ImGui::Combo(Label(l),current,values.data(),count,height);
}
inline bool Combo(const char* l,int* current,const char* items,int height=-1){
 std::vector<const char*> values;for(auto p=items;*p;p+=std::strlen(p)+1)values.push_back(Tr(p));
 return ImGui::Combo(Label(l),current,values.data(),int(values.size()),height);
}
inline bool Combo(const char* l,int* current,const char*(*getter)(void*,int),void* data,int count,int height=-1){
 struct Adapter{const char*(*get)(void*,int);void* data;} adapter{getter,data};
 return ImGui::Combo(Label(l),current,[](void* p,int i){auto& a=*static_cast<Adapter*>(p);return Tr(a.get(a.data,i));},&adapter,count,height);
}
}
