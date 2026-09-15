#pragma once
#include "BuildProfile.h"
#include <filesystem>
namespace DlssNr::BuildProfile {
// Explicit opt-in at process start. Routine Summary never arms binary archives.
inline bool ResearchCaptureRequested(const std::filesystem::path& root){
 if constexpr(!Diagnostic)return false;
 static const bool requested=[&]{std::error_code error;return std::filesystem::is_regular_file(root/L"D18ResearchCapture.enabled",error);}();
 return requested;
}
}
