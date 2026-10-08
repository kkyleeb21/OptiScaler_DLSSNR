#pragma once
#include "NrGradeTable.h"
#include <charconv>
#include <bit>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
namespace DlssNr::Grade {
struct SavedPreset { std::string name; unsigned style=0; Values values=Defaults; };
inline std::string_view Trim(std::string_view s) {
    const auto first=s.find_first_not_of(" \t\r\n");
    if(first==s.npos)return {};
    return s.substr(first,s.find_last_not_of(" \t\r\n")-first+1);
}
inline std::optional<SavedPreset> ParsePreset(std::string name,unsigned style,std::string_view text) {
    if(style>2)return {};
    SavedPreset result{std::move(name),style};
    for(size_t i=0;i<result.values.size();++i) {
        const auto comma=text.find(',');
        if((i==13)!=(comma==text.npos))return {};
        const auto token=Trim(text.substr(0,comma));
        if(token.empty())return {};
        float value=0;
        const auto parsed=std::from_chars(token.data(),token.data()+token.size(),value);
        if(parsed.ec!=std::errc{} || parsed.ptr!=token.data()+token.size() ||
           ((std::bit_cast<uint32_t>(value)&0x7f800000u)==0x7f800000u) || value<Minimum[i] || value>Maximum[i])return {};
        result.values[i]=value;
        if(comma!=text.npos)text.remove_prefix(comma+1);
    }
    return result;
}
inline std::string SerializePreset(const Values& values) {
    std::string result;char buffer[64];
    for(float value:values) {
        if(!result.empty())result+=',';
        const auto converted=std::to_chars(buffer,buffer+sizeof(buffer),value,
            std::chars_format::general,std::numeric_limits<float>::max_digits10);
        result.append(buffer,converted.ptr);
    }
    return result;
}
template<class C> inline auto ReadPreset(const C& c,unsigned slot) {
    const auto& p=c.DlssNrGradePresets.at(slot);
    return ParsePreset(p.Name.value_or_default(),p.Style.value_or_default(),p.Values.value_or_default());
}
template<class C> inline bool MatchesPreset(const C& c,const SavedPreset& p) {
    return c.DlssNrStyle.value_or_default()==p.style && ReadValues(c)==p.values;
}
template<class C> inline void SavePreset(C& c,unsigned slot) {
    auto& p=c.DlssNrGradePresets.at(slot);
    p.Style=c.DlssNrStyle.value_or_default();p.Values=SerializePreset(ReadValues(c));
}
template<class C> inline bool LoadPreset(C& c,unsigned slot) {
    const auto p=ReadPreset(c,slot);if(!p)return false;
    SetValues(c,p->values);c.DlssNrGradeEnabled=true;c.DlssNrStyle=p->style;return true;
}
}
