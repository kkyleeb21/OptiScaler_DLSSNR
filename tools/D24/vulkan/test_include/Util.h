#pragma once
#include <windows.h>
#include <filesystem>
namespace Util {
inline std::filesystem::path DllPath() {
    wchar_t path[32768]{};
    GetModuleFileNameW(nullptr, path, 32768);
    return path;
}
}
