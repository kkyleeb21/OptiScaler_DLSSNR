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
struct Section {const char* name;uint32_t rva,virtualSize,offset,size,flags;};
struct Directory {uint32_t rva,size;};
struct Region {const char* name;uint32_t offset,size;const char* hashes[2];};
struct Profile {const char* name;uint32_t entry;uint64_t imageBase;uint32_t imageSize,headersSize;const Section* sections;size_t count;const Directory* directories;const Region* regions;size_t regionCount;};
struct Result {bool accepted=false;const char* reason="invalid_pe";const char* region="headers";uint32_t offset=0;const char* profile="unknown";};
}
#include "rules.generated.h"
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
    if(b.size()<0x100||read<uint16_t>(b,0)!=0x5a4d)return result;
    const auto pe=read<uint32_t>(b,60);
    if(!range(b.size(),pe,264)||read<uint32_t>(b,pe)!=0x4550||read<uint16_t>(b,pe+4)!=0x8664)return result;
    const auto optionalSize=read<uint16_t>(b,pe+20);const auto count=read<uint16_t>(b,pe+6);const auto opt=pe+24;
    if(optionalSize!=240||read<uint16_t>(b,opt)!=0x20b||read<uint32_t>(b,opt+108)!=16)return result;
    const auto table=opt+optionalSize;
    if(count>8||!range(b.size(),table,uint32_t(count)*40))return result;
    for(const auto& p:Profiles){
        if(count!=p.count)continue;
        result.profile=p.name;result.reason="layout_mismatch";
        if(read<uint32_t>(b,opt+16)!=p.entry||read<uint64_t>(b,opt+24)!=p.imageBase||read<uint32_t>(b,opt+56)!=p.imageSize||read<uint32_t>(b,opt+60)!=p.headersSize)return result;
        for(size_t i=0;i<p.count;++i){
            const auto off=table+static_cast<uint32_t>(i)*40;const auto& s=p.sections[i];char name[8]{};memcpy(name,s.name,strlen(s.name));
            result.region=s.name;result.offset=off;
            if(memcmp(b.data()+off,name,8)||read<uint32_t>(b,off+8)!=s.virtualSize||read<uint32_t>(b,off+12)!=s.rva||read<uint32_t>(b,off+16)!=s.size||read<uint32_t>(b,off+20)!=s.offset||read<uint32_t>(b,off+36)!=s.flags||!range(b.size(),s.offset,s.size))return result;
        }
        for(uint32_t i=0;i<16;++i){
            if(i==4)continue; // Certificate location does not affect the loaded host ABI.
            const auto off=opt+112+i*8;result.region="directories";result.offset=off;
            if(read<uint32_t>(b,off)!=p.directories[i].rva||read<uint32_t>(b,off+4)!=p.directories[i].size)return result;
        }
        for(size_t i=0;i<p.regionCount;++i){
            const auto& r=p.regions[i];result.region=r.name;result.offset=r.offset;
            if(!range(b.size(),r.offset,r.size))return result;
            std::string hash;result.reason="hash_failed";
            if(!digest(b.data()+r.offset,r.size,hash))return result;
            result.reason="dependency_changed";
            if(hash!=r.hashes[0]&&(!r.hashes[1]||hash!=r.hashes[1]))return result;
        }
        result.accepted=true;result.reason="host_layout_compatible";result.region="none";result.offset=0;return result;
    }
    return result;
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
