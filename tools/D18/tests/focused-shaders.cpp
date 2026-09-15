#include <dlssnr/Dx11FocusedShaders.h>
constexpr bool Cases(){
    DlssNr::InputDiscovery::FocusedShaders t;
    if(t.Add("invalid",7)||t.count)return false;
    if(t.Add("00112233445566778899aabbccddeegf",32)||t.count)return false;
    if(!t.Add("00112233445566778899aabbccddeeff",32))return false;
    if(!t.Add("00112233445566778899AABBCCDDEEFF",32)||t.count!=1)return false;
    unsigned char code[32]={'D','X','B','C',0,0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,0x99,0xaa,0xbb,0xcc,0xdd,0xee,0xff};
    if(t.Matches(code,19)||!t.Matches(code,32))return false;
    code[0]='X';if(t.Matches(code,32))return false;
    code[0]='D';code[19]=0;if(t.Matches(code,32))return false;
    return true;
}
static_assert(Cases(),"Focused checksum matching must be exact and bounded");
int main(){return 0;}
