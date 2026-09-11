// CPU-only routing and foreign-resource handoff regressions; no game/runtime load.
#include "../../OptiScaler/inputs/NgxFeatureRegistry.h"
#include "../../OptiScaler/upscalers/NgxOptionalDx12Inputs.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <thread>

struct Parameters
{
    void* exposure = nullptr;
    void* reactive = nullptr;
    void Set(const char* key, void* value)
    {
        if (std::strcmp(key, NVSDK_NGX_Parameter_ExposureTexture) == 0) exposure = value;
        else if (std::strcmp(key, NVSDK_NGX_Parameter_DLSS_Input_Bias_Current_Color_Mask) == 0) reactive = value;
        else assert(false);
    }
};

static void bridge(Parameters& parameters, void* originalExposure, void* originalReactive,
                   ID3D12Resource* exposure, ID3D12Resource* reactive,
                   bool automatic, bool disabled, int exitMode)
{
    ScopedOptionalDx12Inputs inputs(&parameters, originalExposure, originalReactive,
                                    exposure, reactive, automatic, disabled);
    // The DX12 consumer must see either its own resource or null, never a foreign pointer.
    assert(parameters.exposure == (automatic ? nullptr : exposure));
    assert(parameters.reactive == (disabled ? nullptr : reactive));
    if (exitMode == 1) return;
    if (exitMode == 2) throw std::runtime_error("downstream failure");
}

int main()
{
    NgxFeatureRegistry registry;
    constexpr auto ok = NVSDK_NGX_Result_Success;
    constexpr auto fail = NVSDK_NGX_Result_Fail;
    constexpr auto sr = NVSDK_NGX_Feature_SuperSampling;
    constexpr auto rr = NVSDK_NGX_Feature_RayReconstruction;
    constexpr auto fg = NVSDK_NGX_Feature_FrameGeneration;
    NVSDK_NGX_Handle handle { 42 };
    for (unsigned i = 0; i < 1000; ++i) assert(!registry.Read(i).IsUpscaler());
    registry.RecordCreated(fail, &handle, sr);
    registry.RecordCreated(ok, nullptr, sr);
    assert(!registry.Read(42).feature);
    registry.RecordCreated(ok, &handle, sr);
    assert(registry.Read(42).IsUpscaler());
    registry.ForgetReleased(fail, 42);
    assert(registry.Read(42).feature == sr);
    registry.ForgetReleased(ok, 42);
    assert(!registry.Read(42).IsUpscaler());
    registry.RecordCreated(ok, &handle, rr);
    assert(registry.Read(42).IsUpscaler());
    registry.Record(200, fg);
    registry.Record(201, fg);
    registry.Record(200, fg); // Re-registering must not leak the FG count.
    registry.ForgetReleased(ok, 200);
    assert(registry.Read(42).frameGenerationCreated);
    registry.ForgetReleased(fail, 201);
    assert(registry.Read(42).frameGenerationCreated);
    registry.Record(201, sr); // Reused id changes feature kind.
    assert(!registry.Read(42).frameGenerationCreated);
    registry.ForgetReleased(ok, 201);
    registry.ForgetReleased(ok, 201);
    assert(!registry.Read(42).frameGenerationCreated);

    std::thread writer([&] {
        for (unsigned i = 0; i < 10000; ++i)
        {
            registry.Record(200, fg);
            registry.ForgetReleased(ok, 200);
        }
    });
    for (unsigned i = 0; i < 10000; ++i)
    {
        assert(registry.Read(42).feature == rr);
        const auto snapshot = registry.Read(200);
        assert(snapshot.frameGenerationCreated == (snapshot.feature == fg));
        assert(!snapshot.IsUpscaler());
    }
    writer.join();
    registry.Clear();
    assert(!registry.Read(42).feature && !registry.Read(42).frameGenerationCreated);

    int foreignExposure = 0, foreignReactive = 0, dx12Exposure = 0, dx12Reactive = 0;
    unsigned cases = 0;
    for (unsigned mask = 0; mask < 64; ++mask)
    {
        void* oldExposure = (mask & 1) ? &foreignExposure : nullptr;
        void* oldReactive = (mask & 2) ? &foreignReactive : nullptr;
        auto* exposure = (mask & 4) ? reinterpret_cast<ID3D12Resource*>(&dx12Exposure) : nullptr;
        auto* reactive = (mask & 8) ? reinterpret_cast<ID3D12Resource*>(&dx12Reactive) : nullptr;
        const bool automatic = (mask & 16) != 0, disabled = (mask & 32) != 0;
        for (int exitMode = 0; exitMode < 3; ++exitMode)
        {
            Parameters parameters { oldExposure, oldReactive };
            try { bridge(parameters, oldExposure, oldReactive, exposure, reactive, automatic, disabled, exitMode); }
            catch (const std::runtime_error&) { assert(exitMode == 2); }
            assert(parameters.exposure == oldExposure && parameters.reactive == oldReactive);
            ++cases;
        }
    }
    std::cout << "PASS: NGX lifecycle, routing, FG-count reuse, concurrent snapshots; "
              << cases << " optional-input/restore cases\n";
}
