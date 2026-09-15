#pragma once
namespace DlssNr::InputDiscovery {
struct FocusedShaders {
    unsigned char keys[16][16]{};
    unsigned count=0;
    static constexpr int Hex(char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;}
    constexpr bool Add(const char* text,unsigned length){
        if(length!=32||count==16)return false;
        unsigned char key[16]{};
        for(unsigned i=0;i<16;++i){int a=Hex(text[i*2]),b=Hex(text[i*2+1]);if(a<0||b<0)return false;key[i]=static_cast<unsigned char>(a*16+b);}
        for(unsigned n=0;n<count;++n){bool same=true;for(unsigned i=0;i<16;++i)same&=keys[n][i]==key[i];if(same)return true;}
        for(unsigned i=0;i<16;++i)keys[count][i]=key[i];++count;return true;
    }
    constexpr bool Matches(const unsigned char* data,unsigned long long size)const{
        if(size<32||data[0]!='D'||data[1]!='X'||data[2]!='B'||data[3]!='C')return false;
        for(unsigned n=0;n<count;++n){bool same=true;for(unsigned i=0;i<16;++i)same&=keys[n][i]==data[i+4];if(same)return true;}
        return false;
    }
};
}
