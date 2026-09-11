#include <cstdio>
#include <cstdlib>
#include <cassert>
#include <string>
static int errors = 0;
void invalid(const wchar_t*, const wchar_t*, const wchar_t*, unsigned, uintptr_t) { ++errors; }
int main()
{
    _set_invalid_parameter_handler(invalid);
    char text[256];
    std::string hud = "180 FPS (60 FPS)   1% Low 48 FPS   GPU 92%   280 W   PC Latency N/A";
    snprintf(text, sizeof(text), hud.c_str());
    assert(errors > 0);
    errors = 0;
    snprintf(text, sizeof(text), "%s", hud.c_str());
    assert(errors == 0 && hud == text);
    puts("HUD percent text: old format triggers invalid parameter; literal text PASS");
}
