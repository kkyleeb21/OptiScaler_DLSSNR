#pragma once
#include <d3d11_1.h>
#include <wrl/client.h>
#include <hooks/D18Dx11ManualState.h>
#include "Dx11DrawReplay.h"

namespace DlssNr::Dx11ColorReplay {
using Microsoft::WRL::ComPtr;
enum class Result : unsigned { Submitted, InvalidArgument, UnsupportedPipeline, DepthCoverage, ScissorCoverage, TargetAlias, SourceAlias, StateCapture };
struct Replacement {UINT slot; ID3D11ShaderResourceView* view;};

// Caller validates shader semantics, owns resource lifetimes/GPU fences and suppresses
// its own observers. Only private colour targets are permitted by this primitive.
inline Result Replay(ID3D11DeviceContext1* c,const Dx11DrawReplay::Command& command,
                     ID3D11RenderTargetView* destination,UINT originalWidth,UINT originalHeight,
                     UINT targetWidth,UINT targetHeight,const Replacement* replacements,UINT count,bool allowAlwaysDepth=false){
 if(!c||!destination||!command.Supported()||!originalWidth||!originalHeight||!targetWidth||!targetHeight||count>16)return Result::InvalidArgument;
 ComPtr<ID3D11Resource> output;destination->GetResource(&output);
 ID3D11RenderTargetView* raw[8]{};ComPtr<ID3D11DepthStencilView> ds;c->OMGetRenderTargets(8,raw,&ds);
 bool alias=false,extra=false;for(UINT i=0;i<8;++i){if(raw[i]){ComPtr<ID3D11Resource> r;raw[i]->GetResource(&r);alias|=r==output;extra|=i!=0;raw[i]->Release();}}
 if(alias)return Result::TargetAlias;
 if(extra)return Result::UnsupportedPipeline;
 ComPtr<ID3D11GeometryShader> gs;ComPtr<ID3D11HullShader> hs;ComPtr<ID3D11DomainShader> domain;ComPtr<ID3D11Predicate> predicate;
 c->GSGetShader(&gs,nullptr,nullptr);c->HSGetShader(&hs,nullptr,nullptr);c->DSGetShader(&domain,nullptr,nullptr);c->GetPredication(&predicate,nullptr);
 ID3D11Buffer* so[4]{};c->SOGetTargets(4,so);bool hasSO=false;for(auto p:so)if(p){hasSO=true;p->Release();}
 if(gs||hs||domain||predicate||hasSO)return Result::UnsupportedPipeline;
 if(ds){ComPtr<ID3D11DepthStencilState> state;c->OMGetDepthStencilState(&state,nullptr);D3D11_DEPTH_STENCIL_DESC desc{};
  if(!state)return Result::DepthCoverage;state->GetDesc(&desc);if(desc.StencilEnable || (desc.DepthEnable && !(allowAlwaysDepth && desc.DepthFunc==D3D11_COMPARISON_ALWAYS)))return Result::DepthCoverage;}
 ComPtr<ID3D11BlendState> blend;c->OMGetBlendState(&blend,nullptr,nullptr);
 if(blend){D3D11_BLEND_DESC desc{};blend->GetDesc(&desc);if(desc.AlphaToCoverageEnable)return Result::UnsupportedPipeline;}
 D3D11_VIEWPORT vp{};UINT n=1;c->RSGetViewports(&n,&vp);
 if(n!=1||vp.TopLeftX!=0||vp.TopLeftY!=0||vp.Width!=float(originalWidth)||vp.Height!=float(originalHeight))return Result::UnsupportedPipeline;
 ComPtr<ID3D11RasterizerState> raster;c->RSGetState(&raster);
 if(raster){D3D11_RASTERIZER_DESC desc{};raster->GetDesc(&desc);if(desc.ScissorEnable){D3D11_RECT rect{};n=1;c->RSGetScissorRects(&n,&rect);
  if(n!=1||rect.left>0||rect.top>0||rect.right<LONG(originalWidth)||rect.bottom<LONG(originalHeight))return Result::ScissorCoverage;}}
 for(UINT i=0;i<count;++i){if(replacements[i].slot>=128||!replacements[i].view)return Result::InvalidArgument;
  ComPtr<ID3D11Resource> input;replacements[i].view->GetResource(&input);if(input==output)return Result::SourceAlias;}
 D18Dx11ManualState::Snapshot saved;if(FAILED(saved.Capture(c)))return Result::StateCapture;
 // Preserve the game's PS/VS, constants, sampler, blend and geometry; redirect only
 // the verified colour resources and viewport. No game depth or UAV writes.
 ID3D11UnorderedAccessView* empty[64]{};ComPtr<ID3D11Device> device;c->GetDevice(&device);
 UINT slots=device->GetFeatureLevel()>=D3D_FEATURE_LEVEL_11_1?64:8;
 c->OMSetRenderTargetsAndUnorderedAccessViews(1,&destination,nullptr,1,slots-1,empty+1,nullptr);
 vp.Width=float(targetWidth);vp.Height=float(targetHeight);c->RSSetViewports(1,&vp);
 D3D11_RECT full{0,0,LONG(targetWidth),LONG(targetHeight)};c->RSSetScissorRects(1,&full);
 for(UINT i=0;i<count;++i)c->PSSetShaderResources(replacements[i].slot,1,&replacements[i].view);
 command.Run(c);
 return Result::Submitted;
}
}
