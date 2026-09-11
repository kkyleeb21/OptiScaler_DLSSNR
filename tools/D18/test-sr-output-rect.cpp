#include "../../OptiScaler/dlssnr/SrOutputRect.h"
#include <cassert>
int main() {
    using DlssNr::ResolveSrOutputRect;
    auto contract = [](DlssNrAbi::Rect r, unsigned aw, unsigned ah, unsigned fw, unsigned fh, bool rr = false) {
        return ResolveSrOutputRect(false, rr, r, 3840, 2160, aw, ah, fw, fh);
    };
    assert(contract({0,0,1920,1080},3840,2160,3840,2160).width == 3840);
    assert(contract({0,0,1920,1080},3840,2160,0,0).width == 1920);
    assert(contract({0,0,1920,1080},4096,2160,3840,2160).width == 1920);
    assert(contract({8,0,1920,1080},3840,2160,3840,2160).width == 1920);
    assert(contract({0,8,1920,1080},3840,2160,3840,2160).width == 1920);
    assert(contract({0,0,1920,1080},3840,2160,3840,2160,true).width == 1920);
    assert(contract({0,0,1920,1200},3840,2160,3840,2160).width == 1920);
    assert(contract({0,0,0,0},3840,2160,3840,2160).width == 0);
    assert(contract({0,0,3840,2160},3840,2160,3840,2160).width == 3840);
    assert(contract({0,0,3840,2160},1920,1080,1920,1080).width == 3840);
    auto check = [](bool game, bool rr, DlssNrAbi::Rect r, unsigned rw, unsigned rh,
                    unsigned aw, unsigned ah, unsigned ew, unsigned eh) {
        auto out = ResolveSrOutputRect(game, rr, r, rw, rh, aw, ah);
        assert(out.width == ew && out.height == eh && out.x == r.x && out.y == r.y);
    };
    check(true,false,{0,0,1920,1080},1920,1080,3840,2160,3840,2160);
    check(true,false,{0,0,2560,1440},2560,1440,3840,2160,3840,2160);
    check(false,false,{0,0,1920,1080},1920,1080,3840,2160,1920,1080);
    check(true,true,{0,0,1920,1080},1920,1080,3840,2160,1920,1080);
    check(true,false,{4,0,1920,1080},1920,1080,3840,2160,1920,1080);
    check(true,false,{0,0,3000,1800},1920,1080,3840,2160,3000,1800);
    check(true,false,{0,0,3840,2160},1920,1080,3840,2160,3840,2160);
    check(true,false,{0,0,1920,1080},1920,1080,4000,2160,1920,1080);
    check(true,false,{0,0,0,0},0,0,3840,2160,0,0);
    check(true,false,{0,0,3840,2160},3840,2160,3840,2160,3840,2160);
}
