#pragma once
// Fail closed in builds that do not explicitly opt into the diagnostic profile.
#ifndef D18_DIAGNOSTIC_BUILD
#define D18_DIAGNOSTIC_BUILD 0
#endif
static_assert(D18_DIAGNOSTIC_BUILD==0 || D18_DIAGNOSTIC_BUILD==1);
namespace DlssNr::BuildProfile {
inline constexpr bool Diagnostic = D18_DIAGNOSTIC_BUILD != 0;
inline constexpr bool PixelCapture = Diagnostic;
inline constexpr bool ModelBypass = Diagnostic;
// User-selectable in both profiles. Experimental and off by default; capture remains diagnostic-only.
inline constexpr bool SharedHistoryResearch = true;
inline constexpr unsigned Capabilities = Diagnostic ? 0x0fu : 0x09u;
inline constexpr const char* Name = Diagnostic ? "D18 diagnostic - integrated candidate" : "D18 release - integrated candidate";
}
