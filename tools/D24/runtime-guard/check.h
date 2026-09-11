#pragma once
#include <windows.h>
#include <bcrypt.h>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>
#include <string>
#pragma comment(lib,"bcrypt.lib")
namespace D18RuntimeGuard {
struct Variant {uint32_t size;const char* hash;};
struct Site {uint32_t offset;const Variant* variants;size_t count;};
struct Result {bool accepted=false;const char* reason="invalid_pe";const char* region="headers";uint32_t offset=0;const char* profile="unknown";};
}
#include "patch-sites.generated.h"
namespace D18RuntimeGuard {
inline bool range(size_t size,uint32_t off,uint32_t n){return off<=size&&n<=size-off;}
template<class T> inline T read(const std::vector<unsigned char>& b,size_t off){T v{};if(off<=b.size()&&sizeof(T)<=b.size()-off)memcpy(&v,b.data()+off,sizeof(T));return v;}
inline bool digest(const unsigned char* bytes,uint32_t n,std::string& text){
    BCRYPT_ALG_HANDLE alg=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;DWORD size=0,got=0;unsigned char value[32]{};
    if(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)return false;
    bool ok=BCryptGetProperty(alg,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&size),sizeof(size),&got,0)>=0;
    std::vector<unsigned char> object(size);
    if(ok)ok=BCryptCreateHash(alg,&hash,object.data(),size,nullptr,0,0)>=0;
    if(ok)ok=BCryptHashData(hash,const_cast<PUCHAR>(bytes),n,0)>=0;
    if(ok)ok=BCryptFinishHash(hash,value,sizeof(value),0)>=0;
    if(hash)BCryptDestroyHash(hash);BCryptCloseAlgorithmProvider(alg,0);
    if(ok){constexpr char hex[]="0123456789abcdef";text.clear();for(auto v:value){text+=hex[v>>4];text+=hex[v&15];}}
    return ok;
}
inline Result CheckBytes(const std::vector<unsigned char>& b){
    Result result;
    // File-format validity only; no comparison with a reference host layout.
    if(b.size()<0x100||read<uint16_t>(b,0)!=0x5a4d)return result;
    const auto pe=read<uint32_t>(b,60);
    if(!range(b.size(),pe,24)||read<uint32_t>(b,pe)!=0x4550||read<uint16_t>(b,pe+4)!=0x8664)return result;
    const auto optionalSize=read<uint16_t>(b,pe+20);const auto count=read<uint16_t>(b,pe+6);const auto opt=pe+24;
    if(optionalSize<112||!range(b.size(),opt,optionalSize)||read<uint16_t>(b,opt)!=0x20b)return result;
    const auto table=opt+optionalSize;
    if(!count||count>96||!range(b.size(),table,uint32_t(count)*40))return result;
    result.profile="patch-sites";
    for(const auto& site:Sites){
        bool match=false;result.region="patch_site";result.offset=site.offset;
        for(size_t i=0;i<site.count;++i){
            const auto& v=site.variants[i];
            if(!v.size){if(site.offset==b.size())match=true;continue;}
            if(!range(b.size(),site.offset,v.size))continue;
            std::string hash;if(!digest(b.data()+site.offset,v.size,hash)){result.reason="hash_failed";return result;}
            if(hash==v.hash){match=true;break;}
        }
        if(!match){result.reason="patch_site_conflict";return result;}
    }
    // Native DX11 writes these seven bytes after load. Protect exactly that write,
    // mapped through the supplied PE sections rather than a full section fingerprint.
    constexpr uint32_t nativeRva=0x20f2c;
    constexpr unsigned char expected[]={0x4c,0x8d,0x0d,0x8d,0xf2,0x08,0x00};
    bool found=false;result.reason="native_patch_site_conflict";result.region="native_patch_site";
    for(uint32_t i=0;i<count;++i){
        const auto off=table+i*40;const auto rva=read<uint32_t>(b,off+12);
        const auto size=read<uint32_t>(b,off+16),raw=read<uint32_t>(b,off+20);
        if(nativeRva<rva||nativeRva-rva>size||sizeof(expected)>size-(nativeRva-rva))continue;
        const uint64_t fileOffset=uint64_t(raw)+nativeRva-rva;
        if(fileOffset>UINT32_MAX)return result;
        result.offset=static_cast<uint32_t>(fileOffset);
        if(!range(b.size(),result.offset,sizeof(expected))||memcmp(b.data()+result.offset,expected,sizeof(expected)))return result;
        found=true;break;
    }
    if(!found)return result;
    result.accepted=true;result.reason="patch_sites_compatible";result.region="none";result.offset=0;return result;
}
inline Result CheckFile(const std::filesystem::path& path) noexcept {
    try {
        std::ifstream file(path,std::ios::binary|std::ios::ate);if(!file)return {false,"read_failed"};
        const auto size=file.tellg();if(size<0||size>512ll*1024*1024)return {false,"invalid_size"};
        std::vector<unsigned char> bytes(static_cast<size_t>(size));file.seekg(0);
        if(!file.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size())))return {false,"read_failed"};
        return CheckBytes(bytes);
    }catch(...){return {false,"read_failed"};}
}
}
