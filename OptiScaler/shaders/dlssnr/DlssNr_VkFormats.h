#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include <cstring>

// Specialize our own compiled HLSL storage-image declarations to the two bound UAV formats.
// Sampled images remain untyped. No runtime DLL or game shader is modified.
inline std::vector<char> DlssNrStorageFormats(const void* data,size_t bytes,VkFormat target,VkFormat keep)
{
    auto format=[](VkFormat f)->uint32_t {
        if(f==VK_FORMAT_R32G32B32A32_SFLOAT)return 1;
        if(f==VK_FORMAT_R16G16B16A16_SFLOAT)return 2;
        if(f==VK_FORMAT_B10G11R11_UFLOAT_PACK32)return 8;
        return UINT32_MAX;
    };
    const uint32_t formats[]={format(target),format(keep)};
    if(formats[0]==UINT32_MAX || formats[1]==UINT32_MAX || bytes%4 || bytes<20)return {};
    std::vector<uint32_t> words(bytes/4);std::memcpy(words.data(),data,bytes);
    uint32_t variables[2]{},pointers[2]{},types[2]{};
    for(size_t i=5;i<words.size();i+=words[i]>>16) {
        uint32_t n=words[i]>>16,op=words[i]&65535;
        if(!n || i+n>words.size())return {};
        if(op==71 && n==4 && words[i+2]==33 && (words[i+3]==5 || words[i+3]==6)) variables[words[i+3]-5]=words[i+1];
    }
    for(size_t i=5;i<words.size();i+=words[i]>>16)
        if((words[i]&65535)==59)for(unsigned k=0;k<2;k++)if(words[i+2]==variables[k])pointers[k]=words[i+1];
    for(size_t i=5;i<words.size();i+=words[i]>>16)
        if((words[i]&65535)==32)for(unsigned k=0;k<2;k++)if(words[i+1]==pointers[k])types[k]=words[i+3];
    if(!variables[0] || !variables[1] || !types[0] || !types[1])return {};
    uint32_t newTypes[]={words[3],words[3]+1},newPointers[]={words[3]+2,words[3]+3};
    if(types[0]!=types[1] || pointers[0]!=pointers[1])return {};
    for(unsigned k=0;k<2;k++)if(formats[k]==1){newTypes[k]=types[k];newPointers[k]=pointers[k];}
    if(formats[0]==formats[1]){newTypes[1]=newTypes[0];newPointers[1]=newPointers[0];}
    std::vector<uint32_t> out(words.begin(),words.begin()+5);out[3]+=4;
    if(formats[0]==8 || formats[1]==8){out.push_back((2u<<16)|17u);out.push_back(49);}
    for(size_t i=5;i<words.size();i+=words[i]>>16) {
        uint32_t n=words[i]>>16,op=words[i]&65535;
        std::vector<uint32_t> instruction(words.begin()+i,words.begin()+i+n);
        if(op==59)for(unsigned k=0;k<2;k++)if(instruction[2]==variables[k])instruction[1]=newPointers[k];
        if(op==61)for(unsigned k=0;k<2;k++)if(instruction[3]==variables[k])instruction[1]=newTypes[k];
        out.insert(out.end(),instruction.begin(),instruction.end());
        if(op==25)for(unsigned k=0;k<2;k++)if(words[i+1]==types[k] && newTypes[k]!=types[k] && (k==0 || newTypes[k]!=newTypes[0])) {
            auto clone=instruction;clone[1]=newTypes[k];clone[8]=formats[k];out.insert(out.end(),clone.begin(),clone.end());
        }
        if(op==32)for(unsigned k=0;k<2;k++)if(words[i+1]==pointers[k] && newPointers[k]!=pointers[k] && (k==0 || newPointers[k]!=newPointers[0])) {
            auto clone=instruction;clone[1]=newPointers[k];clone[3]=newTypes[k];out.insert(out.end(),clone.begin(),clone.end());
        }
    }
    std::vector<char> result(out.size()*4);std::memcpy(result.data(),out.data(),result.size());return result;
}
