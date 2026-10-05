#pragma once
#include <filesystem>
#include <string>
#include <cstdio>
#include <cerrno>
#include <cstring>

namespace D18Ui {
inline std::string MenuIniPathText(const std::filesystem::path& path) {
    const auto utf8=path.u8string();return {reinterpret_cast<const char*>(utf8.data()),utf8.size()};
}
// SimpleIni's FILE writer does not propagate every fwrite/flush/close error.
// Preserve its format and destination, but report the actual write result.
template<class Ini> bool SaveMenuIni(Ini& ini,const std::filesystem::path& path,std::string& reason) {
    FILE* file=nullptr;errno=0;
    const auto opened=_wfopen_s(&file,path.c_str(),L"wb");
    if(opened||!file) {reason="open: errno="+std::to_string(opened)+" ("+std::strerror(opened)+"); "+MenuIniPathText(path);return false;}
    const auto result=ini.SaveFile(file,true); // Match the original path overload's UTF-8 BOM policy.
    bool ok=result>=0&&!std::ferror(file);int error=errno;
    if(std::fflush(file)!=0){ok=false;error=errno;}
    if(std::fclose(file)!=0){ok=false;error=errno;}
    reason.clear();
    if(!ok){if(!error)error=EIO;reason="write: SimpleIni="+std::to_string(result)+"; errno="+std::to_string(error)+" ("+std::strerror(error)+"); "+MenuIniPathText(path);}
    return ok;
}
}
