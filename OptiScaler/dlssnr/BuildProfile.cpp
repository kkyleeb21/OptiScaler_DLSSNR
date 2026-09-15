#include "pch.h"
#include "BuildProfile.h"
// Pure queries: used by the package verifier without initializing the proxy.
extern "C" __declspec(dllexport) unsigned D18GetBuildCapabilities() {
    return DlssNr::BuildProfile::Capabilities;
}
extern "C" __declspec(dllexport) const char* D18GetBuildProfileName() {
    return DlssNr::BuildProfile::Name;
}
