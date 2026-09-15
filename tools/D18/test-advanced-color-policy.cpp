#include <dlssnr/MultipassFormats.h>
#include <dlssnr/HighResolutionRetry.h>
#include <cassert>
#include <limits>
#include <initializer_list>
int main(){using namespace DlssNr;
 for(auto f:{DXGI_FORMAT_R16G16B16A16_FLOAT,DXGI_FORMAT_R32G32B32A32_FLOAT,DXGI_FORMAT_R11G11B10_FLOAT,DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_R10G10B10A2_UNORM,DXGI_FORMAT_R16G16B16A16_UNORM,DXGI_FORMAT_B8G8R8A8_UNORM}){assert(Multipass::ColorFormat(f));assert(Multipass::ModelFormat(f,true)==DXGI_FORMAT_R16G16B16A16_FLOAT);assert(Multipass::ModelFormat(f,false)==f);}
 for(auto f:{DXGI_FORMAT_UNKNOWN,DXGI_FORMAT_R8G8B8A8_TYPELESS,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,DXGI_FORMAT_R16G16B16A16_TYPELESS,DXGI_FORMAT_R10G10B10A2_UINT,DXGI_FORMAT_D32_FLOAT})assert(!Multipass::ColorFormat(f));
 HighResolution::RejectedRequest p;assert(!p.Changed(false,1.25f,false));p.Block(true,1.25f);
 for(int i=0;i<10000;++i)assert(!p.Changed(true,1.25f,false));assert(p.Changed(false,1.25f,false));assert(p.Changed(true,1.5f,false));assert(!p.Changed(false,1.25f,true));p.Clear();assert(!p.Changed(false,1.25f,false));
 auto nan=std::numeric_limits<float>::quiet_NaN();p.Block(true,nan);assert(!p.Changed(true,nan,false));assert(p.Changed(true,1.25f,false));
}
