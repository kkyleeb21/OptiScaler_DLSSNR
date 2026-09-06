#pragma once
#include <filesystem>
#include <cwchar>
namespace DlssNr {
inline std::filesystem::path ConfigLocation(const std::filesystem::path& dll, const std::filesystem::path& name) {
    auto directory = dll.parent_path();
    if (_wcsicmp(directory.filename().c_str(), L"_storage_") == 0)
        directory = directory.parent_path();
    return directory / name;
}
}
