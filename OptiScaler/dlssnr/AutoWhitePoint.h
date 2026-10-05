#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace DlssNr::AutoWhitePoint {
inline constexpr size_t Cells = 64 * 64;
inline constexpr double TauSeconds = 0.75;
inline constexpr double MaxDtSeconds = 0.25;
inline constexpr double HoldSeconds = 2.0;
inline constexpr double SampleSeconds = 0.1;
inline constexpr float LogFloor = -19.9315685693f; // log2(1e-6)
inline float Pre(float p) { return std::isfinite(p) && p > 1e-6f ? p : 1.0f; }
inline float Key(float k) { return std::isfinite(k) ? std::clamp(k, .005f, .5f) : .05f; }
inline float Trim(float m) { return std::isfinite(m) ? std::clamp(m, .1f, 10.f) : 1.f; }
struct Measurement { bool valid=false; float raw=0; size_t count=0; };
inline Measurement Measure(const float* values, size_t size) {
    std::array<float, Cells> sorted{};
    size_t n=0, floor=0;
    for(size_t i=0; i<std::min(size,Cells); ++i) if(std::isfinite(values[i])) {
        sorted[n++]=values[i];
        if(values[i]<=LogFloor+1e-5f) ++floor;
    }
    if(!n) return {};
    std::sort(sorted.begin(),sorted.begin()+n);
    const size_t tail=n*2/100; // 81 cells per end for 4096 finite cells
    double sum=0;
    for(size_t i=tail; i<n-tail; ++i) sum+=sorted[i];
    const float raw=float(sum/double(n-2*tail));
    return {std::isfinite(raw) && raw>=std::log2(1e-5f) && floor*100<=n*95,raw,n};
}
struct Controller {
    bool ready=false, snapNext=true;
    double b=0, target=0, lastValid=0, lastFrame=0;
    void ResetSample() { snapNext=true; }
    bool Accept(Measurement m, float samplePre, float key, double sampleTime, double now) {
        if(!m.valid || !std::isfinite(sampleTime) || now-sampleTime>HoldSeconds || now<sampleTime) return false;
        // log2(2^Lraw / Ps / K), avoiding underflow/overflow in the intermediate.
        const double next=double(m.raw)-std::log2(double(Pre(samplePre)))-std::log2(double(Key(key)));
        if(!std::isfinite(next)) return false;
        target=next;
        if(!ready || snapNext) b=target;
        ready=true; snapNext=false; lastValid=sampleTime;
        return true;
    }
    bool Holding(double now) const { return ready && now-lastValid>HoldSeconds; }
    void Frame(double now) {
        const double dt=lastFrame>0?std::clamp(now-lastFrame,0.0,MaxDtSeconds):0.0;
        lastFrame=now;
        if(ready && !Holding(now)) b+=(1.0-std::exp(-dt/TauSeconds))*(target-b);
    }
    float White(float currentPre, float trim, float fixed) const {
        if(!ready) return std::isfinite(fixed) && fixed>0 ? fixed : 1.f;
        // Clamp in the log domain so even extreme finite input never yields NaN/Inf.
        const double logW=std::log2(double(Pre(currentPre)))+b+std::log2(double(Trim(trim)));
        return float(std::exp2(std::clamp(logW,std::log2(1e-4),12.0)));
    }
};
}
