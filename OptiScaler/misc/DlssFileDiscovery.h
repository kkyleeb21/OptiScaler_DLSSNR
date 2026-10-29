#pragma once

#include <algorithm>
#include <filesystem>
#include <optional>
#include <vector>

// Startup-only discovery. No Config/State dependencies and no DLL loading.
namespace DlssFileDiscovery
{
namespace fs = std::filesystem;
inline constexpr unsigned MaxParentSteps = 4;

inline bool IsFile(const fs::path& path)
{
    std::error_code ec;
    return fs::is_regular_file(path, ec);
}

inline bool IsDirectory(const fs::path& path)
{
    std::error_code ec;
    return fs::is_directory(path, ec);
}

inline std::optional<fs::path> NormalizeOverride(const fs::path& value)
{
    if (value.empty())
        return std::nullopt;
    auto file = IsDirectory(value) ? value / L"nvngx_dlss.dll" : value;
    if (!IsFile(file))
        return std::nullopt;
    std::error_code ec;
    auto absolute = fs::absolute(file, ec);
    return ec ? std::nullopt : std::optional<fs::path>(absolute.lexically_normal());
}

inline bool IsProject(const fs::path& directory)
{
    if (IsDirectory(directory / L"Binaries"))
        return true;
    std::error_code ec;
    fs::directory_iterator it(directory, fs::directory_options::skip_permission_denied, ec), end;
    for (; !ec && it != end; it.increment(ec))
        if (it->path().extension() == L".uproject" && IsFile(it->path()))
            return true;
    return false;
}

inline std::vector<fs::path> UeCandidates(const fs::path& exeDirectory)
{
    auto root = exeDirectory.lexically_normal();
    // remove_filename() at startup leaves a separator; do not count it as a parent step.
    if (root.filename().empty() && root != root.root_path())
        root = root.parent_path();
    for (unsigned step = 0; step <= MaxParentSteps; ++step)
    {
        if (IsDirectory(root / L"Engine"))
        {
            const fs::path binaries = L"Binaries/ThirdParty/Win64";
            std::vector<fs::path> result {
                root / L"Engine/Plugins/Runtime/Nvidia/DLSS" / binaries,
                root / L"Engine/Plugins/Runtime/Nvidia/StreamlineCore" / binaries
            };
            // Only enumerate immediate children; plugin locations themselves are fixed.
            std::vector<fs::path> projects;
            std::error_code ec;
            fs::directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
            for (; !ec && it != end; it.increment(ec))
                if (it->path().filename() != L"Engine" && IsDirectory(it->path()) && IsProject(it->path()))
                    projects.push_back(it->path());
            std::sort(projects.begin(), projects.end());
            // Prefer only the project containing the EXE. An EXE outside every
            // project may use project plugins only when the root has one project.
            std::optional<fs::path> owner;
            const auto exe = exeDirectory.lexically_normal();
            for (const auto& project : projects)
            {
                std::error_code relativeError;
                const auto relative = fs::relative(exe, project, relativeError);
                if (!relativeError && !relative.empty() && *relative.begin() != L".." && !relative.is_absolute())
                {
                    owner = project;
                    break;
                }
            }
            if (!owner && projects.size() == 1)
                owner = projects.front();
            if (owner)
            {
                const auto& project = *owner;
                for (const auto* plugins : { L"Plugins/Runtime/Nvidia", L"Plugins/Nvidia", L"Plugins" })
                    for (const auto* plugin : { L"DLSS", L"StreamlineCore" })
                        result.push_back(project / plugins / plugin / binaries);
                result.push_back(project / L"Binaries/ThirdParty/NVIDIA/NGX/Win64");
            }
            for (auto& directory : result)
                directory.make_preferred(); // Native separators for NGX/Windows DLL loaders.
            return result; // Use the nearest UE root only.
        }
        const auto parent = root.parent_path();
        if (parent.empty() || parent == root)
            break;
        root = parent;
    }
    return {};
}

struct Match
{
    fs::path file;
    const char* source;
};

// legacyFind retains the existing OptiScaler/exe search, including its filtering.
template<class LegacyFind>
std::optional<Match> Find(const fs::path& filename, const std::optional<fs::path>& srOverride,
                          const fs::path& optiDirectory, const fs::path& exeDirectory, LegacyFind legacyFind)
{
    if (srOverride && filename == L"nvngx_dlss.dll")
    {
        auto file = *srOverride;
        if (IsFile(file))
            return Match { file, "NvngxDlssPath" };
    }
    if (auto file = legacyFind(optiDirectory, filename))
        return Match { *file, "OptiScaler directory" };
    if (auto file = legacyFind(exeDirectory, filename))
        return Match { *file, "exe directory" };
    for (const auto& directory : UeCandidates(exeDirectory))
        if (auto file = directory / filename; IsFile(file))
            return Match { file, "UE candidate" };
    return std::nullopt;
}
}
