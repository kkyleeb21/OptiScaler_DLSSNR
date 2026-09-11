#pragma once
namespace D18Reflex
{
// The game-side DX11 device still needs its native telemetry when FG uses DX12.
// Never repeat native submission on a failed forwarding attempt.
template<class Native, class Forward, class Status>
Status RouteMarker(bool preserveNative, Native native, Forward forward, Status ok)
{
    if (preserveNative)
    {
        const auto nativeResult = native();
        forward();
        return nativeResult;
    }
    return forward() ? ok : native();
}
}
