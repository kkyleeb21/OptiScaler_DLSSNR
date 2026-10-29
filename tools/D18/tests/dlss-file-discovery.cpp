// Offline MSVC host test; --scan only lists files and never loads/copies DLLs.
#include <misc/DlssFileDiscovery.h>
#include <dlssnr/RuntimeDiscoveryPolicy.h>
#include <chrono>
#include <fstream>
#include <iostream>
#include <queue>
#include <stdexcept>

namespace D = DlssFileDiscovery;
namespace fs = std::filesystem;

// Read-only mirror of Util::FindFilePath for before/after scans. Production keeps
// calling Util itself. OptiFG output is false for the audited auto/NoFG configs.
static std::optional<fs::path> LegacyFind(const fs::path& start, const fs::path& filename,
                                          const fs::path& opti, bool hideCopies = false, bool dlssgOutput = false)
{
    const auto visible = [&](const fs::path& path)
    {
        return !(hideCopies && path.parent_path() == opti &&
                 (filename == L"nvngx_dlss.dll" || filename == L"nvngx_dlssd.dll"));
    };
    if (auto direct = start / filename; D::IsFile(direct) && visible(direct))
        return direct;
    const auto bfs = [&](const fs::path& root) -> std::optional<fs::path>
    {
        std::queue<fs::path> pending;
        pending.push(root);
        while (!pending.empty())
        {
            auto current = pending.front();
            pending.pop();
            std::error_code ec;
            fs::directory_iterator it(current, fs::directory_options::skip_permission_denied, ec), end;
            for (; !ec && it != end; it.increment(ec))
            {
                if (D::IsDirectory(it->path()))
                    pending.push(it->path());
                else if (_wcsicmp(it->path().filename().c_str(), filename.c_str()) == 0 && visible(it->path()))
                {
                    auto rel = fs::relative(it->path().lexically_normal(), (opti / L"streamline").lexically_normal(), ec);
                    bool inStreamline = !ec && !rel.empty() && *rel.begin() != L"." && *rel.begin() != L"..";
                    if (DlssNr::AllowRuntimeCandidate(filename.wstring(), inStreamline, dlssgOutput))
                        return it->path();
                }
            }
        }
        return std::nullopt;
    };
    if (auto file = bfs(start))
        return file;
    const auto parent = start.parent_path().parent_path();
    unsigned index = 0;
    for (const auto* folder : { L"Win64", L"WinGDK", L"Win64MasterMasterSteamPGO" })
    {
        if (D::IsDirectory(parent / folder))
        {
            auto root = index < 2 ? parent.parent_path().parent_path() : parent.parent_path();
            return bfs(root);
        }
        ++index;
    }
    return std::nullopt;
}

static void Touch(const fs::path& path)
{
    fs::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    file << "offline fixture, not a DLL";
    if (!file)
        throw std::runtime_error("fixture write failed");
}

static unsigned checks = 0;
static void Check(bool result, const char* name)
{
    if (!result)
        throw std::runtime_error(name);
    ++checks;
}

static int Fixtures(const fs::path& parent)
{
    const auto root = fs::absolute(parent) /
        ("dlss-path-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Check(!fs::exists(root), "fresh fixture root");
    fs::create_directories(root);
    const auto ue = root / L"PBZ";
    const auto exe = ue / L"platform/steam";
    const auto opti = exe / L"OptiScaler";
    const auto srDir = ue / L"Engine/Plugins/Runtime/Nvidia/DLSS/Binaries/ThirdParty/Win64";
    const auto fgDir = ue / L"Engine/Plugins/Runtime/Nvidia/StreamlineCore/Binaries/ThirdParty/Win64";
    fs::create_directories(opti);
    Touch(srDir / L"nvngx_dlss.dll");
    Touch(srDir / L"nvngx_dlssd.dll");
    Touch(fgDir / L"nvngx_dlssg.dll");
    auto legacy = [&](const fs::path& dir, const fs::path& name) { return LegacyFind(dir, name, opti); };
    auto find = [&](const fs::path& name, const std::optional<fs::path>& overrideFile = std::nullopt)
    {
        return D::Find(name, overrideFile, opti, exe, legacy);
    };
    for (const auto* name : { L"nvngx_dlss.dll", L"nvngx_dlssd.dll", L"nvngx_dlssg.dll" })
    {
        auto result = find(name);
        Check(result && std::string(result->source) == "UE candidate", "PBZ UE discovery");
        Check(result->file == (name == std::wstring(L"nvngx_dlssg.dll") ? fgDir : srDir) / name,
              "PBZ exact file");
        Check(!legacy(exe, name), "PBZ baseline miss without temporary copies");
        Touch(opti / name);
        result = find(name);
        Check(result && result->file == opti / name && std::string(result->source) == "OptiScaler directory",
              "OptiScaler copies precede UE");
    }
    const auto overrideDir = root / L"override";
    for (const auto* name : { L"nvngx_dlss.dll", L"nvngx_dlssd.dll", L"nvngx_dlssg.dll" })
        Touch(overrideDir / name);
    const auto expected = overrideDir / L"nvngx_dlss.dll";
    for (const auto& value : { overrideDir, fs::path(overrideDir.wstring() + L"\\"), expected })
    {
        auto normalized = D::NormalizeOverride(value);
        Check(normalized && *normalized == expected && D::IsFile(*normalized), "override normalizes to full file");
        for (const auto* name : { L"nvngx_dlss.dll", L"nvngx_dlssd.dll", L"nvngx_dlssg.dll" })
        {
            auto result = find(name, normalized);
            const bool sr = std::wstring(name) == L"nvngx_dlss.dll";
            Check(result && result->file == (sr ? overrideDir : opti) / name &&
                      std::string(result->source) == (sr ? "NvngxDlssPath" : "OptiScaler directory"),
                  "override affects SR only; RR and FG keep legacy precedence");
        }
    }
    for (const auto& invalid : { root / L"absent", root / L"absent.dll", root / L"empty-override" })
    {
        fs::create_directories(root / L"empty-override");
        Touch(root / L"empty-override/nvngx_dlssd.dll");
        const auto normalized = D::NormalizeOverride(invalid);
        Check(!normalized, "invalid override ignored, including directory without SR");
        auto result = find(L"nvngx_dlss.dll", normalized);
        Check(result && result->file == opti / L"nvngx_dlss.dll", "invalid override resumes normal search");
    }
    Touch(root / L"sr-only/nvngx_dlss.dll");
    auto siblingFallback = find(L"nvngx_dlssd.dll", D::NormalizeOverride(root / L"sr-only"));
    Check(siblingFallback && siblingFallback->file == opti / L"nvngx_dlssd.dll", "missing override sibling resumes search");

    const auto filterRoot = root / L"override-filter";
    const auto filterOpti = filterRoot / L"OptiScaler";
    const auto filterExe = filterRoot / L"exe";
    Touch(filterOpti / L"streamline/nvngx_dlss.dll");
    Touch(filterOpti / L"streamline/nvngx_dlssg.dll");
    fs::create_directories(filterExe);
    for (bool dlssgOutput : { false, true })
    {
        auto filtered = [&](const fs::path& dir, const fs::path& name) {
            return LegacyFind(dir, name, filterOpti, false, dlssgOutput);
        };
        auto match = D::Find(L"nvngx_dlssg.dll", D::NormalizeOverride(filterOpti / L"streamline"),
                             filterOpti, filterExe, filtered);
        Check(bool(match) == dlssgOutput, "SR override cannot bypass Streamline FG output filter");
        if (match)
            Check(match->file == filterOpti / L"streamline/nvngx_dlssg.dll" &&
                      std::string(match->source) == "OptiScaler directory", "DLSSG uses legacy filtered result");
    }
    auto overrideOnly = [&](const fs::path& dir, const fs::path& name) { return LegacyFind(dir, name, opti); };
    Check(!D::Find(L"nvngx_dlssg.dll", D::NormalizeOverride(overrideDir), root / L"absent-opti",
                   root / L"absent-exe", overrideOnly), "external SR override does not supply FG");
    Check(!D::Find(L"nvngx_dlssd.dll", D::NormalizeOverride(overrideDir), root / L"absent-opti",
                   root / L"absent-exe", overrideOnly), "external SR override does not supply RR");

    auto directOnly = [](const fs::path& dir, const fs::path& name) -> std::optional<fs::path>
    {
        return D::IsFile(dir / name) ? std::optional<fs::path>(dir / name) : std::nullopt;
    };
    auto noEngine = D::Find(L"nvngx_dlss.dll", std::nullopt, root / L"missing", root / L"no-engine/bin", directOnly);
    Check(!noEngine && D::UeCandidates(root / L"no-engine/bin").empty(), "no Engine is a harmless miss");
    Check(D::UeCandidates(ue / L"a/b/c/d").size() >= 2, "four parent steps accepted");
    Check(D::UeCandidates(fs::path((ue / L"a/b/c/d").wstring() + L"\\")).size() >= 2,
          "trailing exe separator does not consume a parent step");
    Check(D::UeCandidates(ue / L"a/b/c/d/e").empty(), "five parent steps rejected");
    Check(!D::Find(L"nvngx_dlss.dll", std::nullopt, root / L"missing", ue / L"a/b/c/d/e", directOnly),
          "over-limit lookup does not escape bound");
    Touch(root / L"exe-only/nvngx_dlss.dll");
    auto exeMatch = D::Find(L"nvngx_dlss.dll", std::nullopt, root / L"missing", root / L"exe-only", directOnly);
    Check(exeMatch && std::string(exeMatch->source) == "exe directory", "exe precedence");
    Touch(exe / L"nvngx_dlss.dll");
    Check(find(L"nvngx_dlss.dll")->file == opti / L"nvngx_dlss.dll", "OptiScaler precedes exe");

    const auto projectRoot = root / L"projects";
    fs::create_directories(projectRoot / L"Engine");
    Touch(projectRoot / L"Alpha/Alpha.uproject");
    const auto plugin = projectRoot / L"Alpha/Plugins/Nvidia/DLSS/Binaries/ThirdParty/Win64/nvngx_dlss.dll";
    Touch(plugin);
    auto projectMatch = D::Find(L"nvngx_dlss.dll", std::nullopt, root / L"missing", projectRoot / L"platform/steam", directOnly);
    Check(projectMatch && projectMatch->file == plugin, "uproject project plugin candidate");
    const auto ngx = projectRoot / L"Beta/Binaries/ThirdParty/NVIDIA/NGX/Win64/nvngx_dlssd.dll";
    Touch(ngx);
    projectMatch = D::Find(L"nvngx_dlssd.dll", std::nullopt, root / L"missing", projectRoot / L"platform/steam", directOnly);
    Check(!projectMatch, "outside two projects does not select project NGX");
    Check(!D::Find(L"nvngx_dlss.dll", std::nullopt, root / L"missing", projectRoot / L"platform/steam", directOnly),
          "outside two projects does not select first project plugin");
    auto alpha = D::Find(L"nvngx_dlss.dll", std::nullopt, root / L"missing", projectRoot / L"Alpha/Binaries/Win64", directOnly);
    Check(alpha && alpha->file == plugin, "EXE in Alpha uses Alpha plugin");
    Check(!D::Find(L"nvngx_dlssd.dll", std::nullopt, root / L"missing", projectRoot / L"Alpha/Binaries/Win64", directOnly),
          "EXE in Alpha cannot use Beta NGX");
    auto beta = D::Find(L"nvngx_dlssd.dll", std::nullopt, root / L"missing", projectRoot / L"Beta/Binaries/Win64", directOnly);
    Check(beta && beta->file == ngx, "EXE in Beta uses Beta NGX");
    Check(!D::Find(L"nvngx_dlss.dll", std::nullopt, root / L"missing", projectRoot / L"Beta/Binaries/Win64", directOnly),
          "EXE in Beta cannot use Alpha plugin");

    const auto collection = root / L"collection";
    const auto nonUe = collection / L"NonUE/bin/tools/x64";
    const auto other = collection / L"AnotherTitle/Plugins/Nvidia/DLSS/Binaries/ThirdParty/Win64/nvngx_dlss.dll";
    fs::create_directories(collection / L"Engine");
    fs::create_directories(collection / L"AnotherTitle/Binaries");
    fs::create_directories(collection / L"SecondTitle/Binaries");
    fs::create_directories(nonUe / L"OptiScaler");
    Touch(other);
    auto nonUeLegacy = [&](const fs::path& dir, const fs::path& name) { return LegacyFind(dir, name, nonUe / L"OptiScaler"); };
    Check(!D::Find(L"nvngx_dlss.dll", std::nullopt, nonUe / L"OptiScaler", nonUe, nonUeLegacy),
          "non-UE EXE near Engine cannot select an unrelated project among multiple projects");
    // The requested single-project fallback deliberately permits the original
    // audit's ambiguous layout. Keep it explicit instead of claiming UE identity.
    const auto single = root / L"single-project";
    fs::create_directories(single / L"Engine");
    fs::create_directories(single / L"OnlyTitle/Binaries");
    Touch(single / L"OnlyTitle/Plugins/DLSS/Binaries/ThirdParty/Win64/nvngx_dlss.dll");
    Check(bool(D::Find(L"nvngx_dlss.dll", std::nullopt, root / L"missing", single / L"NonUE/bin/tools/x64", directOnly)),
          "explicit single-project fallback applies even without proven EXE identity");
    fs::create_directories(projectRoot / L"Engine/Plugins/Runtime/Nvidia/DLSS/Binaries/ThirdParty/Win64/nvngx_dlssg.dll");
    Check(!D::Find(L"nvngx_dlssg.dll", std::nullopt, root / L"missing", projectRoot / L"platform/steam", directOnly),
          "directory named DLL is rejected");
    std::cout << "PASS: " << checks << " discovery checks; fixtures retained, no DLLs loaded\n";
    return 0;
}

int wmain(int argc, wchar_t** argv)
{
    try
    {
        if (argc == 3 && std::wstring(argv[1]) == L"--fixtures")
            return Fixtures(argv[2]);
        if ((argc == 4 || argc == 5) && std::wstring(argv[1]) == L"--scan")
        {
            const fs::path exe = fs::path(argv[2]) / L"", opti = argv[3]; // Match startup remove_filename().
            const bool hideCopies = argc == 5 && std::wstring(argv[4]) == L"without-copies";
            auto legacy = [&](const fs::path& dir, const fs::path& name) { return LegacyFind(dir, name, opti, hideCopies); };
            for (const auto* name : { L"nvngx_dlss.dll", L"nvngx_dlssd.dll", L"nvngx_dlssg.dll" })
            {
                auto before = legacy(opti, name);
                if (!before)
                    before = legacy(exe, name);
                auto after = D::Find(name, std::nullopt, opti, exe, legacy);
                std::wcout << name << L'\t' << (before ? before->wstring() : L"NOT_FOUND") << L'\t'
                           << (after ? after->file.wstring() : L"NOT_FOUND") << L'\t';
                if (after)
                {
                    const std::string source = after->source;
                    std::wcout << std::wstring(source.begin(), source.end());
                }
                std::wcout << L'\n';
            }
            return 0;
        }
        std::cerr << "Usage: --fixtures <authorized-root> | --scan <exe-dir> <opti-dir> [without-copies]\n";
        return 2;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
