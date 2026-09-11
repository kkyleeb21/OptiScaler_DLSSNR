#pragma once
#include <cmath>
#include <cstdint>
#include <utility>

namespace VulkanFg
{
// No graphics calls or allocation. Keep queried values separate from defaults;
// missing NGX parameters must never turn their zero-initialized output into data.
struct QueriedFloat { float value=0; bool supplied=false; };
struct CameraInput
{
    QueriedFloat nearPlane,farPlane,verticalFovRadians;
    float configuredNear=0.1f,configuredFar=100000.0f;
    float configuredVerticalDegrees=60.0f,configuredHorizontalDegrees=0.0f;
    bool verticalOverride=false,useGameValues=true,depthInverted=false;
    uint32_t outputWidth=0,outputHeight=0;
};
struct Camera
{
    float nearPlane=0,farPlane=0,verticalFovRadians=0,aspect=0;
    bool planesFromGame=false,fovFromGame=false,valid=false;
    bool Approximate() const { return valid && (!planesFromGame || !fovFromGame); }
};
inline bool PositiveFinite(float v){return std::isfinite(v)&&v>0;}
inline Camera ResolveCamera(const CameraInput& input)
{
    Camera result;
    if(!input.outputWidth||!input.outputHeight)return result;
    result.aspect=float(input.outputWidth)/float(input.outputHeight);
    result.planesFromGame=input.useGameValues&&input.nearPlane.supplied&&input.farPlane.supplied&&
        PositiveFinite(input.nearPlane.value)&&PositiveFinite(input.farPlane.value)&&input.nearPlane.value!=input.farPlane.value;
    if(result.planesFromGame){result.nearPlane=input.nearPlane.value;result.farPlane=input.farPlane.value;}
    else {
        result.nearPlane=input.configuredNear;result.farPlane=input.configuredFar;
        if(input.depthInverted)std::swap(result.nearPlane,result.farPlane);
    }
    constexpr float pi=3.14159265358979323846f;
    result.fovFromGame=input.useGameValues&&input.verticalFovRadians.supplied&&
        PositiveFinite(input.verticalFovRadians.value)&&input.verticalFovRadians.value<pi;
    if(result.fovFromGame)result.verticalFovRadians=input.verticalFovRadians.value;
    else if(input.verticalOverride)result.verticalFovRadians=input.configuredVerticalDegrees*(pi/180.0f);
    else if(PositiveFinite(input.configuredHorizontalDegrees)&&input.configuredHorizontalDegrees<180.0f)
        result.verticalFovRadians=2.0f*std::atan(std::tan(input.configuredHorizontalDegrees*(pi/360.0f))/result.aspect);
    else result.verticalFovRadians=pi/3.0f;
    result.valid=PositiveFinite(result.nearPlane)&&PositiveFinite(result.farPlane)&&result.nearPlane!=result.farPlane&&
        PositiveFinite(result.verticalFovRadians)&&result.verticalFovRadians<pi&&PositiveFinite(result.aspect);
    return result;
}
struct MotionScale {float x=0,y=0;bool valid=false;};
inline MotionScale NormalizeMotion(QueriedFloat x,QueriedFloat y,uint32_t width,uint32_t height)
{
    if(!x.supplied||!y.supplied||!width||!height||!std::isfinite(x.value)||!std::isfinite(y.value))return {};
    // Same NGX-pixel to SL-normalized conversion as the existing DX12 FG input.
    return {x.value/float(width),y.value/float(height),true};
}
}
