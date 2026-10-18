#pragma once
#include "RuntimeProfile.h"
#include <optional>
#include <string>
namespace DlssNr::Model {
// Default HT (user decision 2026-10-18 after in-game comparison in four games: stronger
// structure and the model's own colour); the small FP8 model is the performance option.
inline constexpr const char* Default = "HT";
inline std::optional<std::string> Parse(const std::string& value) {
    if(value=="HT" || value=="Small")return value;
    return std::nullopt;
}
inline uint32_t Preset(const Runtime::Profile* profile,uint32_t legacy,const std::string& model) {
    return profile && profile->ModelSupported() ? (model=="Small"?2u:1u) : legacy;
}
inline const char* Name(uint32_t preset) {return preset==2?"Small":"HT";}
}
