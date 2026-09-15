// Read-only runtime probe: each invocation tests a fresh process/device.
#include <windows.h>
#include <d3d11_4.h>
#include <cstdio>
#include <cwchar>
#pragma comment(lib,"user32.lib")
int wmain(int argc, wchar_t** argv) {
    if(argc != 3) return 2;
    if(wcscmp(argv[1],L"system") && !LoadLibraryW(argv[1])) return 3;
    auto module=LoadLibraryExW(L"d3d11.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    if(!module) return 4;
    auto create=reinterpret_cast<PFN_D3D11_CREATE_DEVICE>(GetProcAddress(module,"D3D11CreateDevice"));
    if(!create) return 5;
    D3D_FEATURE_LEVEL requested=wcscmp(argv[2],L"11_1")==0?D3D_FEATURE_LEVEL_11_1:D3D_FEATURE_LEVEL_11_0;
    const bool defaults=wcscmp(argv[2],L"default")==0;
    D3D_FEATURE_LEVEL actual{}; ID3D11Device* device=nullptr; ID3D11DeviceContext* context=nullptr;
    HRESULT hr;
    if(wcscmp(argv[2],L"swap11_0")==0) {
        WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"D18ContractProbe";
        RegisterClassW(&wc);
        HWND window=CreateWindowW(wc.lpszClassName,L"D18 probe",WS_OVERLAPPEDWINDOW,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);
        if(!window)return 8;
        DXGI_SWAP_CHAIN_DESC desc{};desc.BufferDesc.Width=64;desc.BufferDesc.Height=64;desc.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count=1;desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;desc.BufferCount=1;desc.OutputWindow=window;desc.Windowed=TRUE;
        auto createSwap=reinterpret_cast<PFN_D3D11_CREATE_DEVICE_AND_SWAP_CHAIN>(GetProcAddress(module,"D3D11CreateDeviceAndSwapChain"));
        IDXGISwapChain* swap=nullptr;
        hr=createSwap(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,&requested,1,D3D11_SDK_VERSION,&desc,&swap,&device,&actual,&context);
    } else {
        hr=create(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,defaults?nullptr:&requested,defaults?0:1,D3D11_SDK_VERSION,&device,&actual,&context);
    }
    std::printf("{\"event\":\"device_contract\",\"hr\":%ld,\"requested\":%u,\"actual\":%u,\"default_list\":%s",hr,unsigned(requested),unsigned(actual),defaults?"true":"false");
    if(FAILED(hr)||!device) { std::puts("}"); return 6; }
    ID3D11Device5* newer=nullptr; const auto interfaceHr=device->QueryInterface(IID_PPV_ARGS(&newer));
    std::printf(",\"device_level\":%u,\"device5_hr\":%ld",unsigned(device->GetFeatureLevel()),interfaceHr);
    if(newer) { ID3D11Fence* fence=nullptr; auto fenceHr=newer->CreateFence(0,D3D11_FENCE_FLAG_SHARED,IID_PPV_ARGS(&fence)); std::printf(",\"shared_fence_hr\":%ld",fenceHr); if(fence)fence->Release(); newer->Release(); }
    const bool matches=actual==requested && device->GetFeatureLevel()==actual;
    std::printf(",\"matches\":%s}\n",matches?"true":"false");
    std::fflush(stdout);
    // Process exit owns teardown: D18 may retain the captured device pointer.
    return matches?0:7;
}
