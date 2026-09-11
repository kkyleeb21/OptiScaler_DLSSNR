// Validate the indexed DX11 source assumption without launching a game.
// No normal Present: render changing colors, read pixels, resize, repeat.
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <iostream>
#include <stdexcept>
#include <string>
#include <cmath>
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "user32.lib")
using Microsoft::WRL::ComPtr;
void require(HRESULT hr) { if (FAILED(hr)) throw std::runtime_error("HRESULT failure " + std::to_string(hr)); }
LRESULT CALLBACK proc(HWND w, UINT m, WPARAM p, LPARAM l) { return DefWindowProc(w, m, p, l); }
int main()
{
    HWND window = nullptr;
    try {
        WNDCLASSW wc{};
        wc.lpfnWndProc = proc;
        wc.hInstance = GetModuleHandle(nullptr);
        wc.lpszClassName = L"D18UnpresentedSourceTest";
        RegisterClassW(&wc);
        window = CreateWindowW(wc.lpszClassName, L"", WS_POPUP, 0, 0, 96, 96, nullptr, nullptr, wc.hInstance, nullptr);
        if (!window) throw std::runtime_error("CreateWindow failed");
        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        D3D_FEATURE_LEVEL level;
        require(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
                                 D3D11_SDK_VERSION, &device, &level, &context));
        ComPtr<IDXGIDevice> dxgi;
        require(device.As(&dxgi));
        ComPtr<IDXGIAdapter> adapter;
        require(dxgi->GetAdapter(&adapter));
        ComPtr<IDXGIFactory2> factory;
        require(adapter->GetParent(IID_PPV_ARGS(&factory)));
        DXGI_SWAP_CHAIN_DESC1 desc{};
        desc.Width = 64; desc.Height = 64;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 3;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        ComPtr<IDXGISwapChain1> sc1;
        require(factory->CreateSwapChainForHwnd(device.Get(), window, &desc, nullptr, nullptr, &sc1));
        ComPtr<IDXGISwapChain3> sc;
        require(sc1.As(&sc));
        int checked = 0;
        for (int phase = 0; phase < 2; ++phase) {
            if (phase) {
                context->ClearState();
                context->Flush();
                require(sc->ResizeBuffers(2, 96, 96, desc.Format, 0));
            }
            auto first = sc->GetCurrentBackBufferIndex();
            ComPtr<ID3D11Texture2D> source;
            require(sc->GetBuffer(first, IID_PPV_ARGS(&source)));
            D3D11_TEXTURE2D_DESC td{};
            source->GetDesc(&td);
            td.Usage = D3D11_USAGE_STAGING; td.BindFlags = 0;
            td.CPUAccessFlags = D3D11_CPU_ACCESS_READ; td.MiscFlags = 0;
            ComPtr<ID3D11Texture2D> readback;
            require(device->CreateTexture2D(&td, nullptr, &readback));
            ComPtr<ID3D11RenderTargetView> rtv;
            require(device->CreateRenderTargetView(source.Get(), nullptr, &rtv));
            for (int i = 0; i < 120; ++i) {
                if (sc->GetCurrentBackBufferIndex() != first) throw std::runtime_error("source index rotated");
                float color[4] = {float(i % 3 == 0), float(i % 3 == 1), float(i % 3 == 2), 1};
                context->OMSetRenderTargets(1, rtv.GetAddressOf(), nullptr);
                context->ClearRenderTargetView(rtv.Get(), color);
                context->CopyResource(readback.Get(), source.Get());
                D3D11_MAPPED_SUBRESOURCE mapped{};
                require(context->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &mapped));
                auto pixel = static_cast<unsigned char*>(mapped.pData);
                bool match = true;
                for (int c = 0; c < 4; ++c) match &= std::abs(int(pixel[c]) - int(color[c] * 255)) <= 1;
                context->Unmap(readback.Get(), 0);
                if (!match) throw std::runtime_error("pixel failed to update");
                ++checked;
            }
            context->OMSetRenderTargets(0, nullptr, nullptr);
        }
        DestroyWindow(window);
        std::cout << "PASS: " << checked << " hardware DX11 pixel checks, resize 3/64x64 -> 2/96x96, no normal Present\n";
        return 0;
    } catch (const std::exception& e) {
        if (window) DestroyWindow(window);
        std::cerr << e.what() << '\n';
        return 1;
    }
}
