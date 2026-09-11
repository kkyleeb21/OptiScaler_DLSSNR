#include "../../OptiScaler/framegen/VulkanFgInputPolicy.h"
#include <cassert>
#include <limits>
#include <cstdio>
int main(){
    using namespace VulkanFg;
    CameraInput bg3;bg3.outputWidth=3840;bg3.outputHeight=2160;bg3.depthInverted=true;
    auto camera=ResolveCamera(bg3);
    assert(camera.valid&&camera.Approximate()&&!camera.planesFromGame&&!camera.fovFromGame);
    assert(camera.nearPlane==100000.0f&&camera.farPlane==0.1f);
    auto scale=NormalizeMotion({-1,true},{-1,true},1920,1080);
    assert(scale.valid&&std::abs(scale.x+1.0f/1920)<1e-8f&&std::abs(scale.y+1.0f/1080)<1e-8f);
    assert(!NormalizeMotion({-1,false},{-1,true},1920,1080).valid);
    bg3.nearPlane={1,true};bg3.farPlane={100,true};bg3.verticalFovRadians={1.2f,true};
    camera=ResolveCamera(bg3);assert(camera.valid&&!camera.Approximate()&&camera.nearPlane==1);
    bg3.farPlane.supplied=false;camera=ResolveCamera(bg3);assert(camera.valid&&camera.Approximate()&&!camera.planesFromGame&&camera.fovFromGame);
    bg3.useGameValues=false;bg3.verticalOverride=true;bg3.configuredVerticalDegrees=180;
    assert(!ResolveCamera(bg3).valid);
    bg3.verticalOverride=false;bg3.configuredHorizontalDegrees=90;
    camera=ResolveCamera(bg3);assert(camera.valid&&std::abs(camera.verticalFovRadians-2*std::atan(9.0f/16))<1e-5f);
    bg3.outputHeight=0;assert(!ResolveCamera(bg3).valid);
    assert(!NormalizeMotion({std::numeric_limits<float>::quiet_NaN(),true},{1,true},1920,1080).valid);
    puts("PASS: BG3 missing parameters, reversed configured planes, game provenance, invalid dimensions/FOV/NaN, normalized motion");
}
