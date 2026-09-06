#pragma once
#include <cstring>
namespace DlssNr::ReProfile {
inline bool Known(const char* exe){return _stricmp(exe,"OnimushaWotS.exe")==0 || _stricmp(exe,"re9.exe")==0 || _stricmp(exe,"PRAGMATA.exe")==0 || _stricmp(exe,"MonsterHunterWilds.exe")==0;}
inline bool NativeRr(const char* exe){return _stricmp(exe,"re9.exe")==0 || _stricmp(exe,"PRAGMATA.exe")==0 || _stricmp(exe,"MonsterHunterWilds.exe")==0;}
}
