#pragma once
#include <cstdio>
#include <filesystem>
#include <string>

namespace capture {
// Injected only by offline failure tests. Production uses the CRT directly.
struct FileIo {
    std::FILE* (*open)(const wchar_t*, const wchar_t*) = &_wfopen;
    size_t (*write)(const void*, size_t, size_t, std::FILE*) = &std::fwrite;
    int (*error)(std::FILE*) = &std::ferror;
    int (*close)(std::FILE*) = &std::fclose;
};
template<class Fill> bool WriteChecked(const std::filesystem::path& path, Fill fill, const FileIo& io)
{
    auto* file = io.open(path.c_str(), L"wb");
    if (!file) return false;
    const bool filled = fill(file);
    const bool clean = io.error(file) == 0;
    const bool closed = io.close(file) == 0; // A buffered write can fail only at close.
    return filled && clean && closed;
}
struct WriteResult {
    enum class State { Idle, Rearmed, Success, Failed } state = State::Idle;
    std::string directory;
    const char* reason = "";
    // Retain source compatibility with existing offline capture callers.
    bool empty() const { return state != State::Success; }
};
}
