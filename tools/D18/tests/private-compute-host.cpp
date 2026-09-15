#include <dlssnr/Dx11PrivateCompute.h>
#include <hooks/D18Dx11ManualState.h>
#include <d3d11sdklayers.h>
#include <vector>
#include <cstdio>
#include <cmath>
#pragma comment(lib,"d3d11.lib")
#pragma comment(lib,"d3dcompiler.lib")
using Microsoft::WRL::ComPtr;
int main(){
 for(auto driver:{D3D_DRIVER_TYPE_WARP,D3D_DRIVER_TYPE_HARDWARE})for(auto level:{D3D_FEATURE_LEVEL_11_0,D3D_FEATURE_LEVEL_11_1}){
 ComPtr<ID3D11Device> d;ComPtr<ID3D11DeviceContext> c;D3D_FEATURE_LEVEL actual{};
 if(FAILED(D3D11CreateDevice(nullptr,driver,nullptr,D3D11_CREATE_DEVICE_DEBUG,&level,1,D3D11_SDK_VERSION,&d,&actual,&c)))return 1;
 ComPtr<ID3D11InfoQueue> q;d.As(&q);
 for(UINT first:{0u,16u}){
 DlssNr::Dx11PrivateCompute workspace;UINT n=first?1024:48;std::vector<UINT> values(n/4);for(UINT i=0;i<n/4;++i)values[i]=0x3f000000+i*17;
 D3D11_BUFFER_DESC bd{};bd.ByteWidth=n;bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;D3D11_SUBRESOURCE_DATA data{values.data(),0,0};ComPtr<ID3D11Buffer> cb;if(FAILED(d->CreateBuffer(&bd,&data,&cb)))return 2;
 std::vector<float> pixels(17*19);for(UINT i=0;i<pixels.size();++i)pixels[i]=float(i)/512;
 D3D11_TEXTURE2D_DESC td{};td.Width=17;td.Height=19;td.MipLevels=td.ArraySize=td.SampleDesc.Count=1;td.Format=DXGI_FORMAT_R32_FLOAT;td.BindFlags=D3D11_BIND_SHADER_RESOURCE;
 D3D11_SUBRESOURCE_DATA texData{pixels.data(),17*4,0};ComPtr<ID3D11Texture2D> tex;ComPtr<ID3D11ShaderResourceView> view;
 if(FAILED(d->CreateTexture2D(&td,&texData,&tex))||FAILED(d->CreateShaderResourceView(tex.Get(),nullptr,&view)))return 3;
 for(unsigned repeat=0;repeat<3;++repeat){D18Dx11ManualState::Snapshot scope;if(FAILED(scope.Capture(c.Get())))return 4;scope.Reset();if(FAILED(workspace.Prepare(c.Get(),cb.Get(),first,31,33,view.Get())))return 5;}
 bd.BindFlags=0;bd.Usage=D3D11_USAGE_STAGING;bd.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ComPtr<ID3D11Buffer> read;if(FAILED(d->CreateBuffer(&bd,nullptr,&read)))return 6;
 c->CopyResource(read.Get(),workspace.Constants());D3D11_MAPPED_SUBRESOURCE m{};if(FAILED(c->Map(read.Get(),0,D3D11_MAP_READ,0,&m)))return 7;
 auto out=static_cast<UINT*>(m.pData);float inv[]={1.0f/31,1.0f/33};UINT bits[2];memcpy(bits,inv,sizeof(bits));
 for(UINT i=0;i<n/4;++i)if(out[i]!=(i==first*4+1?bits[0]:i==first*4+2?bits[1]:values[i]))return 8;c->Unmap(read.Get(),0);
 c->CopyResource(read.Get(),cb.Get());if(FAILED(c->Map(read.Get(),0,D3D11_MAP_READ,0,&m)))return 9;if(memcmp(m.pData,values.data(),n))return 10;c->Unmap(read.Get(),0);
 ComPtr<ID3D11Resource> scalar;workspace.Scalar()->GetResource(&scalar);td.Width=31;td.Height=33;td.BindFlags=0;td.Usage=D3D11_USAGE_STAGING;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ComPtr<ID3D11Texture2D> result;if(FAILED(d->CreateTexture2D(&td,nullptr,&result)))return 11;c->CopyResource(result.Get(),scalar.Get());if(FAILED(c->Map(result.Get(),0,D3D11_MAP_READ,0,&m)))return 12;
 for(UINT y=0;y<33;++y)for(UINT x=0;x<31;++x){auto row=reinterpret_cast<float*>(static_cast<char*>(m.pData)+y*m.RowPitch);UINT sx=UINT((float(x)+.5f)*17/31),sy=UINT((float(y)+.5f)*19/33);if(row[x]!=pixels[sy*17+sx])return 13;}c->Unmap(result.Get(),0);
 if(SUCCEEDED(workspace.Prepare(c.Get(),cb.Get(),first,32,33,view.Get())))return 14;
 if(SUCCEEDED(workspace.Prepare(c.Get(),cb.Get(),65535,31,33,view.Get())))return 15;
 }
 for(UINT64 i=0;i<q->GetNumStoredMessages();++i){SIZE_T n=0;q->GetMessage(i,nullptr,&n);std::vector<char> bytes(n);auto msg=reinterpret_cast<D3D11_MESSAGE*>(bytes.data());q->GetMessage(i,msg,&n);if(msg->Severity<=D3D11_MESSAGE_SEVERITY_WARNING){printf("debug %u %s\n",unsigned(msg->Severity),msg->pDescription);return 16;}}
 if(FAILED(d->GetDeviceRemovedReason()))return 17;
 printf("driver=%u feature=%x full_and_ranged_constants_preserved=1 scalar_pixels=1023 repeated=3 debug_clean=1\n",unsigned(driver),unsigned(actual));
 }
 return 0;
}
