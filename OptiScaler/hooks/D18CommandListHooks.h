#pragma once
#include <dlssnr/Dx11CommandListWrites.h>
#include <tuple>
#include <type_traits>
namespace D18InputProbe::CommandLists {
namespace Meta=DlssNr::Dx11CommandListWrites;
inline std::atomic<bool> armed{false};inline void* covered[149][8]{};
inline bool Known(UINT slot,void* entry){for(auto p:covered[slot])if(p==entry)return true;return false;}
inline bool Register(UINT slot,void* entry){if(Known(slot,entry))return true;for(auto& p:covered[slot])if(!p){p=entry;return true;}return false;}
inline bool Covered(ID3D11DeviceContext*);
// All shadow writes are conservative: automatic unbinding may leave a superset.
// Finish(FALSE) / ClearState establish a fully known empty binding state.
template<UINT Slot,class... A> inline void Record(ID3D11DeviceContext* c,A... args){
 if(!armed.load()||internalWork||!c||c->GetType()!=D3D11_DEVICE_CONTEXT_DEFERRED)return;
 auto state=Meta::Get(c);if(!state)return;std::lock_guard lock(state->mutex);auto& w=state->writes;w.CountOperation();auto a=std::tuple<A...>(args...);(void)a;
 auto op=w.Trace(Slot);
 if(op){
  if constexpr(sizeof...(A)>0){using First=std::tuple_element_t<0,decltype(a)>;
   if constexpr(std::is_convertible_v<First,ID3D11Resource*>||std::is_convertible_v<First,ID3D11View*>)op->destination=Meta::Describe(std::get<0>(a));
  }
  if constexpr(Slot==47)op->source=Meta::Describe(std::get<1>(a));
  if constexpr(Slot==46||Slot==115){op->source=Meta::Describe(std::get<5>(a));op->args[0]=std::get<1>(a);op->args[1]=std::get<2>(a);op->args[2]=std::get<3>(a);op->args[3]=std::get<4>(a);op->args[4]=std::get<6>(a);auto box=std::get<7>(a);if(box){op->args[5]=1;memcpy(op->args+6,box,sizeof(*box));}}
  if constexpr(Slot==48||Slot==116){op->args[0]=std::get<1>(a);op->args[1]=std::get<4>(a);op->args[2]=std::get<5>(a);auto box=std::get<2>(a);if(box){op->args[3]=1;memcpy(op->args+4,box,sizeof(*box));}}
  if constexpr(Slot==49||Slot==57)op->source=Meta::Describe(std::get<2>(a));
  if constexpr(Slot==50||Slot==51||Slot==52){if(std::get<1>(a))memcpy(op->args,std::get<1>(a),16);}
  if constexpr(Slot==7||Slot==8||Slot==16||Slot==22||Slot==25||Slot==31||Slot==59||Slot==62||Slot==63||Slot==66||Slot==67||Slot==71||(Slot>=119&&Slot<=124)){
  UINT first=std::get<0>(a),count=std::get<1>(a);auto bindings=std::get<2>(a);if(count>128){++w.traceDropped;}else for(UINT i=0;i<count;++i)if(auto t=w.Trace(Slot,first+i)){if(bindings)t->source=Meta::Describe(bindings[i]);if constexpr(Slot>=119&&Slot<=124){t->args[0]=std::get<3>(a)?std::get<3>(a)[i]:0;t->args[1]=std::get<4>(a)?std::get<4>(a)[i]:4096;}}
 }
 if constexpr(Slot==9||Slot==11||Slot==23||Slot==60||Slot==64||Slot==69){
  if(op){auto shader=std::get<0>(a);const GUID pixel={0xf540782c,0xc683,0x40dd,{0x9e,0xd1,0x17,0x28,0x65,0x41,0x11,0x03}};UINT bytes=sizeof(op->shader);if(shader)shader->GetPrivateData(pixel,&bytes,&op->shader);if constexpr(Slot==69){if(shader)op->shader=DlssNr::Dx11ComputeIdentity::Read(shader).hash;}op->args[0]=std::get<2>(a);}
 }
 if constexpr(Slot==12||Slot==13||Slot==20||Slot==21||Slot==41){UINT i=0;std::apply([&](auto... v){((op->args[i++]=UINT(v)),...);},a);}
 }
 if constexpr(Slot==12||Slot==13||Slot==20||Slot==21||Slot==38||Slot==39||Slot==40){
  state->Draw();
  if(Meta::ExtendedTrace()){
   auto target=[&](ID3D11View* view,UINT slot,UINT role){if(view)if(auto t=w.Trace(Slot,slot)){t->destination=Meta::Describe(view);t->args[11]=role;}};
   for(UINT i=0;i<8;++i)target(state->rt[i].Get(),i,1);
   target(state->ds.Get(),0,2);
   for(UINT i=0;i<state->uavSlots;++i)target(state->om[i].Get(),i,3);
  }
 }
 else if constexpr(Slot==41||Slot==42){state->Dispatch();if(Meta::ExtendedTrace())for(UINT i=0;i<state->uavSlots;++i)if(state->cs[i])if(auto t=w.Trace(Slot,i)){t->destination=Meta::Describe(state->cs[i].Get());t->args[11]=4;}}
 else if constexpr(Slot==33||Slot==34){
  UINT n=std::get<0>(a);auto views=std::get<1>(a);
  if(n!=D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL){if(n>8||(n&&!views)){w.reasons|=Meta::Unsupported;state->bindingsKnown=false;return;}for(UINT i=0;i<8;++i)state->rt[i]=i<n?views[i]:nullptr;state->ds=std::get<2>(a);}
  if constexpr(Slot==34){UINT first=std::get<3>(a),count=std::get<4>(a);auto v=std::get<5>(a);auto counts=std::get<6>(a);
   if(count!=D3D11_KEEP_UNORDERED_ACCESS_VIEWS){if(first>state->uavSlots||count>state->uavSlots-first||(count&&!v)){w.reasons|=Meta::Unsupported;state->bindingsKnown=false;return;}for(UINT i=0;i<count;++i){state->om[first+i]=v[i];if(counts&&counts[i]!=UINT(-1))w.Add(v[i]);}}
  }
 }
 else if constexpr(Slot==68){UINT first=std::get<0>(a),count=std::get<1>(a);auto v=std::get<2>(a);auto counts=std::get<3>(a);
  if(first>state->uavSlots||count>state->uavSlots-first||(count&&!v)){w.reasons|=Meta::Unsupported;state->bindingsKnown=false;return;}for(UINT i=0;i<count;++i){state->cs[first+i]=v[i];if(counts&&counts[i]!=UINT(-1))w.Add(v[i]);}}
 else if constexpr(Slot==37){UINT n=std::get<0>(a);auto v=std::get<1>(a);if(n>4||(n&&!v)){w.reasons|=Meta::Unsupported;state->bindingsKnown=false;return;}for(UINT i=0;i<4;++i)state->so[i]=i<n?v[i]:nullptr;}
 else if constexpr(Slot==110)state->ClearBindings();
 else if constexpr(Slot==131){state->bindingsKnown=false;w.reasons|=Meta::Unsupported;}
 else if constexpr(Slot==58||Slot>=134)w.reasons|=Meta::Unsupported;
 else if constexpr(Slot==14){if(std::get<2>(a)!=D3D11_MAP_READ)w.Add(std::get<0>(a));}
 else if constexpr(Slot==7||Slot==8||Slot==9||Slot==11||Slot==16||Slot==22||Slot==23||Slot==25||Slot==31||Slot==59||Slot==60||Slot==62||Slot==63||Slot==64||Slot==66||Slot==67||Slot==69||Slot==71||(Slot>=119&&Slot<=124)){}
 else w.Add(std::get<0>(a));
}
template<UINT Slot,class Method>struct Hook;
template<UINT Slot,class R,class C,class... A>struct Hook<Slot,R(STDMETHODCALLTYPE C::*)(A...)>{
 using Fn=R(WINAPI*)(ID3D11DeviceContext*,A...);inline static Fn originals[4]{};inline static void* entries[4]{};inline static UINT count=0;
 template<UINT N>static R WINAPI Call(ID3D11DeviceContext* c,A... args){
  if constexpr(Slot==114){auto result=originals[N](c,args...);auto a=std::tuple<A...>(args...);if(armed.load()&&!internalWork&&SUCCEEDED(result)&&std::get<1>(a)&&*std::get<1>(a)&&c->GetType()==D3D11_DEVICE_CONTEXT_DEFERRED)Meta::Finish(c,*std::get<1>(a),std::get<0>(a),Covered(c));return result;}
  else {Record<Slot>(c,args...);if constexpr(std::is_void_v<R>)originals[N](c,args...);else return originals[N](c,args...);}
 }
 static LONG Attach(void* entry){if(Known(Slot,entry))return NO_ERROR;if(count==4)return ERROR_NOT_SUPPORTED;Fn callbacks[]={Call<0>,Call<1>,Call<2>,Call<3>};const UINT n=count;originals[n]=reinterpret_cast<Fn>(entry);auto result=DetourAttach(reinterpret_cast<PVOID*>(&originals[n]),callbacks[n]);if(result==NO_ERROR){entries[n]=entry;++count;if(!Register(Slot,entry))return ERROR_NOT_SUPPORTED;}return result;}
 static LONG Detach(){Fn callbacks[]={Call<0>,Call<1>,Call<2>,Call<3>};for(UINT i=0;i<count;++i){auto hr=DetourDetach(reinterpret_cast<PVOID*>(&originals[i]),callbacks[i]);if(hr!=NO_ERROR)return hr;}return NO_ERROR;}
};
using H12=Hook<12,decltype(&ID3D11DeviceContext::DrawIndexed)>;
using H13=Hook<13,decltype(&ID3D11DeviceContext::Draw)>;
using H14=Hook<14,decltype(&ID3D11DeviceContext::Map)>;
using H15=Hook<15,decltype(&ID3D11DeviceContext::Unmap)>;
using H20=Hook<20,decltype(&ID3D11DeviceContext::DrawIndexedInstanced)>;
using H21=Hook<21,decltype(&ID3D11DeviceContext::DrawInstanced)>;
using H33=Hook<33,decltype(&ID3D11DeviceContext::OMSetRenderTargets)>;
using H34=Hook<34,decltype(&ID3D11DeviceContext::OMSetRenderTargetsAndUnorderedAccessViews)>;
using H37=Hook<37,decltype(&ID3D11DeviceContext::SOSetTargets)>;
using H38=Hook<38,decltype(&ID3D11DeviceContext::DrawAuto)>;
using H39=Hook<39,decltype(&ID3D11DeviceContext::DrawIndexedInstancedIndirect)>;
using H40=Hook<40,decltype(&ID3D11DeviceContext::DrawInstancedIndirect)>;
using H41=Hook<41,decltype(&ID3D11DeviceContext::Dispatch)>;
using H42=Hook<42,decltype(&ID3D11DeviceContext::DispatchIndirect)>;
using H46=Hook<46,decltype(&ID3D11DeviceContext::CopySubresourceRegion)>;
using H47=Hook<47,decltype(&ID3D11DeviceContext::CopyResource)>;
using H48=Hook<48,decltype(&ID3D11DeviceContext::UpdateSubresource)>;
using H49=Hook<49,decltype(&ID3D11DeviceContext::CopyStructureCount)>;
using H50=Hook<50,decltype(&ID3D11DeviceContext::ClearRenderTargetView)>;
using H51=Hook<51,decltype(&ID3D11DeviceContext::ClearUnorderedAccessViewUint)>;
using H52=Hook<52,decltype(&ID3D11DeviceContext::ClearUnorderedAccessViewFloat)>;
using H53=Hook<53,decltype(&ID3D11DeviceContext::ClearDepthStencilView)>;
using H54=Hook<54,decltype(&ID3D11DeviceContext::GenerateMips)>;
using H55=Hook<55,decltype(&ID3D11DeviceContext::SetResourceMinLOD)>;
using H57=Hook<57,decltype(&ID3D11DeviceContext::ResolveSubresource)>;
using H58=Hook<58,decltype(&ID3D11DeviceContext::ExecuteCommandList)>;
using H68=Hook<68,decltype(&ID3D11DeviceContext::CSSetUnorderedAccessViews)>;
using H110=Hook<110,decltype(&ID3D11DeviceContext::ClearState)>;
using H114=Hook<114,decltype(&ID3D11DeviceContext::FinishCommandList)>;
using H115=Hook<115,decltype(&ID3D11DeviceContext1::CopySubresourceRegion1)>;
using H116=Hook<116,decltype(&ID3D11DeviceContext1::UpdateSubresource1)>;
using H117=Hook<117,decltype(&ID3D11DeviceContext1::DiscardResource)>;
using H118=Hook<118,decltype(&ID3D11DeviceContext1::DiscardView)>;
using H131=Hook<131,decltype(&ID3D11DeviceContext1::SwapDeviceContextState)>;
using H132=Hook<132,decltype(&ID3D11DeviceContext1::ClearView)>;
using H133=Hook<133,decltype(&ID3D11DeviceContext1::DiscardView1)>;
using H134=Hook<134,decltype(&ID3D11DeviceContext2::UpdateTileMappings)>;
using H135=Hook<135,decltype(&ID3D11DeviceContext2::CopyTileMappings)>;
using H136=Hook<136,decltype(&ID3D11DeviceContext2::CopyTiles)>;
using H137=Hook<137,decltype(&ID3D11DeviceContext2::UpdateTiles)>;
using H138=Hook<138,decltype(&ID3D11DeviceContext2::ResizeTilePool)>;
using H139=Hook<139,decltype(&ID3D11DeviceContext2::TiledResourceBarrier)>;
using H145=Hook<145,decltype(&ID3D11DeviceContext3::SetHardwareProtectionState)>;
using H147=Hook<147,decltype(&ID3D11DeviceContext4::Signal)>;
using H148=Hook<148,decltype(&ID3D11DeviceContext4::Wait)>;
using H7=Hook<7,decltype(&ID3D11DeviceContext::VSSetConstantBuffers)>;
using H8=Hook<8,decltype(&ID3D11DeviceContext::PSSetShaderResources)>;
using H9=Hook<9,decltype(&ID3D11DeviceContext::PSSetShader)>;
using H11=Hook<11,decltype(&ID3D11DeviceContext::VSSetShader)>;
using H16=Hook<16,decltype(&ID3D11DeviceContext::PSSetConstantBuffers)>;
using H22=Hook<22,decltype(&ID3D11DeviceContext::GSSetConstantBuffers)>;
using H23=Hook<23,decltype(&ID3D11DeviceContext::GSSetShader)>;
using H25=Hook<25,decltype(&ID3D11DeviceContext::VSSetShaderResources)>;
using H31=Hook<31,decltype(&ID3D11DeviceContext::GSSetShaderResources)>;
using H59=Hook<59,decltype(&ID3D11DeviceContext::HSSetShaderResources)>;
using H60=Hook<60,decltype(&ID3D11DeviceContext::HSSetShader)>;
using H62=Hook<62,decltype(&ID3D11DeviceContext::HSSetConstantBuffers)>;
using H63=Hook<63,decltype(&ID3D11DeviceContext::DSSetShaderResources)>;
using H64=Hook<64,decltype(&ID3D11DeviceContext::DSSetShader)>;
using H66=Hook<66,decltype(&ID3D11DeviceContext::DSSetConstantBuffers)>;
using H67=Hook<67,decltype(&ID3D11DeviceContext::CSSetShaderResources)>;
using H69=Hook<69,decltype(&ID3D11DeviceContext::CSSetShader)>;
using H71=Hook<71,decltype(&ID3D11DeviceContext::CSSetConstantBuffers)>;
using H119=Hook<119,decltype(&ID3D11DeviceContext1::VSSetConstantBuffers1)>;
using H120=Hook<120,decltype(&ID3D11DeviceContext1::HSSetConstantBuffers1)>;
using H121=Hook<121,decltype(&ID3D11DeviceContext1::DSSetConstantBuffers1)>;
using H122=Hook<122,decltype(&ID3D11DeviceContext1::GSSetConstantBuffers1)>;
using H123=Hook<123,decltype(&ID3D11DeviceContext1::PSSetConstantBuffers1)>;
using H124=Hook<124,decltype(&ID3D11DeviceContext1::CSSetConstantBuffers1)>;
inline bool Covered(ID3D11DeviceContext* c){
 auto base=*reinterpret_cast<void***>(c);
 if(!Known(7,base[7]))return false;
 if(!Known(8,base[8]))return false;
 if(!Known(9,base[9]))return false;
 if(!Known(11,base[11]))return false;
 if(!Known(16,base[16]))return false;
 if(!Known(22,base[22]))return false;
 if(!Known(23,base[23]))return false;
 if(!Known(25,base[25]))return false;
 if(!Known(31,base[31]))return false;
 if(!Known(59,base[59]))return false;
 if(!Known(60,base[60]))return false;
 if(!Known(62,base[62]))return false;
 if(!Known(63,base[63]))return false;
 if(!Known(64,base[64]))return false;
 if(!Known(66,base[66]))return false;
 if(!Known(67,base[67]))return false;
 if(!Known(69,base[69]))return false;
 if(!Known(71,base[71]))return false;
 if(!Known(12,base[12]))return false;
 if(!Known(13,base[13]))return false;
 if(!Known(14,base[14]))return false;
 if(!Known(15,base[15]))return false;
 if(!Known(20,base[20]))return false;
 if(!Known(21,base[21]))return false;
 if(!Known(33,base[33]))return false;
 if(!Known(34,base[34]))return false;
 if(!Known(37,base[37]))return false;
 if(!Known(38,base[38]))return false;
 if(!Known(39,base[39]))return false;
 if(!Known(40,base[40]))return false;
 if(!Known(41,base[41]))return false;
 if(!Known(42,base[42]))return false;
 if(!Known(46,base[46]))return false;
 if(!Known(47,base[47]))return false;
 if(!Known(48,base[48]))return false;
 if(!Known(49,base[49]))return false;
 if(!Known(50,base[50]))return false;
 if(!Known(51,base[51]))return false;
 if(!Known(52,base[52]))return false;
 if(!Known(53,base[53]))return false;
 if(!Known(54,base[54]))return false;
 if(!Known(55,base[55]))return false;
 if(!Known(57,base[57]))return false;
 if(!Known(58,base[58]))return false;
 if(!Known(68,base[68]))return false;
 if(!Known(110,base[110]))return false;
 if(!Known(114,base[114]))return false;
 Meta::ComPtr<ID3D11DeviceContext1> c1;if(SUCCEEDED(c->QueryInterface(IID_PPV_ARGS(&c1)))){auto vt=*reinterpret_cast<void***>(c1.Get());
 if(!Known(119,vt[119]))return false;
 if(!Known(120,vt[120]))return false;
 if(!Known(121,vt[121]))return false;
 if(!Known(122,vt[122]))return false;
 if(!Known(123,vt[123]))return false;
 if(!Known(124,vt[124]))return false;
  if(!Known(115,vt[115]))return false;
  if(!Known(116,vt[116]))return false;
  if(!Known(117,vt[117]))return false;
  if(!Known(118,vt[118]))return false;
  if(!Known(131,vt[131]))return false;
  if(!Known(132,vt[132]))return false;
  if(!Known(133,vt[133]))return false;
 }
 Meta::ComPtr<ID3D11DeviceContext2> c2;if(SUCCEEDED(c->QueryInterface(IID_PPV_ARGS(&c2)))){auto vt=*reinterpret_cast<void***>(c2.Get());
  if(!Known(134,vt[134]))return false;
  if(!Known(135,vt[135]))return false;
  if(!Known(136,vt[136]))return false;
  if(!Known(137,vt[137]))return false;
  if(!Known(138,vt[138]))return false;
  if(!Known(139,vt[139]))return false;
 }
 Meta::ComPtr<ID3D11DeviceContext3> c3;if(SUCCEEDED(c->QueryInterface(IID_PPV_ARGS(&c3)))){auto vt=*reinterpret_cast<void***>(c3.Get());
  if(!Known(145,vt[145]))return false;
 }
 Meta::ComPtr<ID3D11DeviceContext4> c4;if(SUCCEEDED(c->QueryInterface(IID_PPV_ARGS(&c4)))){auto vt=*reinterpret_cast<void***>(c4.Get());
  if(!Known(147,vt[147]))return false;
  if(!Known(148,vt[148]))return false;
 }
 return true;
}
inline void* creationEntries[4]{};
template<class Device,class Context>struct Creator {
 using Fn=HRESULT(WINAPI*)(Device*,UINT,Context**);inline static Fn original=nullptr;
 static HRESULT WINAPI Call(Device* d,UINT flags,Context** out){auto hr=original(d,flags,out);
  if(armed.load()&&!internalWork&&SUCCEEDED(hr)&&out&&*out){auto state=Meta::Get(*out);if(state){std::lock_guard lock(state->mutex);state->ClearBindings();state->writes.reasons=Covered(*out)?0u:UINT(Meta::CoverageMissing);}}return hr;
 }
 static LONG Attach(void* entry){for(auto p:creationEntries)if(p==entry)return NO_ERROR;original=reinterpret_cast<Fn>(entry);auto hr=DetourAttach(reinterpret_cast<PVOID*>(&original),Call);if(hr==NO_ERROR)for(auto& p:creationEntries)if(!p){p=entry;break;}return hr;}
 static LONG Detach(){return original?DetourDetach(reinterpret_cast<PVOID*>(&original),Call):NO_ERROR;}
};
using Create0=Creator<ID3D11Device,ID3D11DeviceContext>;using Create1=Creator<ID3D11Device1,ID3D11DeviceContext1>;using Create2=Creator<ID3D11Device2,ID3D11DeviceContext2>;using Create3=Creator<ID3D11Device3,ID3D11DeviceContext3>;
inline LONG Install(ID3D11Device* d){
 Meta::ComPtr<ID3D11DeviceContext> c;auto hr=d->CreateDeferredContext(0,&c);if(FAILED(hr))return ERROR_NOT_SUPPORTED;
 LONG result=DetourTransactionBegin();if(result!=NO_ERROR)return result;result=DetourUpdateThread(GetCurrentThread());auto base=*reinterpret_cast<void***>(c.Get());
 if(result==NO_ERROR)result=H7::Attach(base[7]);
 if(result==NO_ERROR)result=H8::Attach(base[8]);
 if(result==NO_ERROR)result=H9::Attach(base[9]);
 if(result==NO_ERROR)result=H11::Attach(base[11]);
 if(result==NO_ERROR)result=H16::Attach(base[16]);
 if(result==NO_ERROR)result=H22::Attach(base[22]);
 if(result==NO_ERROR)result=H23::Attach(base[23]);
 if(result==NO_ERROR)result=H25::Attach(base[25]);
 if(result==NO_ERROR)result=H31::Attach(base[31]);
 if(result==NO_ERROR)result=H59::Attach(base[59]);
 if(result==NO_ERROR)result=H60::Attach(base[60]);
 if(result==NO_ERROR)result=H62::Attach(base[62]);
 if(result==NO_ERROR)result=H63::Attach(base[63]);
 if(result==NO_ERROR)result=H64::Attach(base[64]);
 if(result==NO_ERROR)result=H66::Attach(base[66]);
 if(result==NO_ERROR)result=H67::Attach(base[67]);
 if(result==NO_ERROR)result=H69::Attach(base[69]);
 if(result==NO_ERROR)result=H71::Attach(base[71]);
 if(result==NO_ERROR)result=H12::Attach(base[12]);
 if(result==NO_ERROR)result=H13::Attach(base[13]);
 if(result==NO_ERROR)result=H14::Attach(base[14]);
 if(result==NO_ERROR)result=H15::Attach(base[15]);
 if(result==NO_ERROR)result=H20::Attach(base[20]);
 if(result==NO_ERROR)result=H21::Attach(base[21]);
 if(result==NO_ERROR)result=H33::Attach(base[33]);
 if(result==NO_ERROR)result=H34::Attach(base[34]);
 if(result==NO_ERROR)result=H37::Attach(base[37]);
 if(result==NO_ERROR)result=H38::Attach(base[38]);
 if(result==NO_ERROR)result=H39::Attach(base[39]);
 if(result==NO_ERROR)result=H40::Attach(base[40]);
 if(result==NO_ERROR)result=H41::Attach(base[41]);
 if(result==NO_ERROR)result=H42::Attach(base[42]);
 if(result==NO_ERROR)result=H46::Attach(base[46]);
 if(result==NO_ERROR)result=H47::Attach(base[47]);
 if(result==NO_ERROR)result=H48::Attach(base[48]);
 if(result==NO_ERROR)result=H49::Attach(base[49]);
 if(result==NO_ERROR)result=H50::Attach(base[50]);
 if(result==NO_ERROR)result=H51::Attach(base[51]);
 if(result==NO_ERROR)result=H52::Attach(base[52]);
 if(result==NO_ERROR)result=H53::Attach(base[53]);
 if(result==NO_ERROR)result=H54::Attach(base[54]);
 if(result==NO_ERROR)result=H55::Attach(base[55]);
 if(result==NO_ERROR)result=H57::Attach(base[57]);
 if(result==NO_ERROR)result=H58::Attach(base[58]);
 if(result==NO_ERROR)result=H68::Attach(base[68]);
 if(result==NO_ERROR)result=H110::Attach(base[110]);
 if(result==NO_ERROR)result=H114::Attach(base[114]);
 Meta::ComPtr<ID3D11DeviceContext1> c1;if(SUCCEEDED(c.As(&c1))){auto vt=*reinterpret_cast<void***>(c1.Get());
 if(result==NO_ERROR)result=H119::Attach(vt[119]);
 if(result==NO_ERROR)result=H120::Attach(vt[120]);
 if(result==NO_ERROR)result=H121::Attach(vt[121]);
 if(result==NO_ERROR)result=H122::Attach(vt[122]);
 if(result==NO_ERROR)result=H123::Attach(vt[123]);
 if(result==NO_ERROR)result=H124::Attach(vt[124]);
  if(result==NO_ERROR)result=H115::Attach(vt[115]);
  if(result==NO_ERROR)result=H116::Attach(vt[116]);
  if(result==NO_ERROR)result=H117::Attach(vt[117]);
  if(result==NO_ERROR)result=H118::Attach(vt[118]);
  if(result==NO_ERROR)result=H131::Attach(vt[131]);
  if(result==NO_ERROR)result=H132::Attach(vt[132]);
  if(result==NO_ERROR)result=H133::Attach(vt[133]);
 }
 Meta::ComPtr<ID3D11DeviceContext2> c2;if(SUCCEEDED(c.As(&c2))){auto vt=*reinterpret_cast<void***>(c2.Get());
  if(result==NO_ERROR)result=H134::Attach(vt[134]);
  if(result==NO_ERROR)result=H135::Attach(vt[135]);
  if(result==NO_ERROR)result=H136::Attach(vt[136]);
  if(result==NO_ERROR)result=H137::Attach(vt[137]);
  if(result==NO_ERROR)result=H138::Attach(vt[138]);
  if(result==NO_ERROR)result=H139::Attach(vt[139]);
 }
 Meta::ComPtr<ID3D11DeviceContext3> c3;if(SUCCEEDED(c.As(&c3))){auto vt=*reinterpret_cast<void***>(c3.Get());
  if(result==NO_ERROR)result=H145::Attach(vt[145]);
 }
 Meta::ComPtr<ID3D11DeviceContext4> c4;if(SUCCEEDED(c.As(&c4))){auto vt=*reinterpret_cast<void***>(c4.Get());
  if(result==NO_ERROR)result=H147::Attach(vt[147]);
  if(result==NO_ERROR)result=H148::Attach(vt[148]);
 }
 auto dv=*reinterpret_cast<void***>(d);if(result==NO_ERROR)result=Create0::Attach(dv[27]);
 Meta::ComPtr<ID3D11Device1> d1;if(SUCCEEDED(d->QueryInterface(IID_PPV_ARGS(&d1)))&&result==NO_ERROR)result=Create1::Attach((*reinterpret_cast<void***>(d1.Get()))[44]);
 Meta::ComPtr<ID3D11Device2> d2;if(SUCCEEDED(d->QueryInterface(IID_PPV_ARGS(&d2)))&&result==NO_ERROR)result=Create2::Attach((*reinterpret_cast<void***>(d2.Get()))[51]);
 Meta::ComPtr<ID3D11Device3> d3;if(SUCCEEDED(d->QueryInterface(IID_PPV_ARGS(&d3)))&&result==NO_ERROR)result=Create3::Attach((*reinterpret_cast<void***>(d3.Get()))[62]);
 if(result==NO_ERROR)result=DetourTransactionCommit();else DetourTransactionAbort();armed=result==NO_ERROR;return result;
}
inline LONG Detach(){armed=false;LONG result=DetourTransactionBegin();if(result!=NO_ERROR)return result;result=DetourUpdateThread(GetCurrentThread());
 if(result==NO_ERROR)result=Create0::Detach();
 if(result==NO_ERROR)result=Create1::Detach();
 if(result==NO_ERROR)result=Create2::Detach();
 if(result==NO_ERROR)result=Create3::Detach();
 if(result==NO_ERROR)result=H7::Detach();
 if(result==NO_ERROR)result=H8::Detach();
 if(result==NO_ERROR)result=H9::Detach();
 if(result==NO_ERROR)result=H11::Detach();
 if(result==NO_ERROR)result=H16::Detach();
 if(result==NO_ERROR)result=H22::Detach();
 if(result==NO_ERROR)result=H23::Detach();
 if(result==NO_ERROR)result=H25::Detach();
 if(result==NO_ERROR)result=H31::Detach();
 if(result==NO_ERROR)result=H59::Detach();
 if(result==NO_ERROR)result=H60::Detach();
 if(result==NO_ERROR)result=H62::Detach();
 if(result==NO_ERROR)result=H63::Detach();
 if(result==NO_ERROR)result=H64::Detach();
 if(result==NO_ERROR)result=H66::Detach();
 if(result==NO_ERROR)result=H67::Detach();
 if(result==NO_ERROR)result=H69::Detach();
 if(result==NO_ERROR)result=H71::Detach();
 if(result==NO_ERROR)result=H119::Detach();
 if(result==NO_ERROR)result=H120::Detach();
 if(result==NO_ERROR)result=H121::Detach();
 if(result==NO_ERROR)result=H122::Detach();
 if(result==NO_ERROR)result=H123::Detach();
 if(result==NO_ERROR)result=H124::Detach();
 if(result==NO_ERROR)result=H12::Detach();
 if(result==NO_ERROR)result=H13::Detach();
 if(result==NO_ERROR)result=H14::Detach();
 if(result==NO_ERROR)result=H15::Detach();
 if(result==NO_ERROR)result=H20::Detach();
 if(result==NO_ERROR)result=H21::Detach();
 if(result==NO_ERROR)result=H33::Detach();
 if(result==NO_ERROR)result=H34::Detach();
 if(result==NO_ERROR)result=H37::Detach();
 if(result==NO_ERROR)result=H38::Detach();
 if(result==NO_ERROR)result=H39::Detach();
 if(result==NO_ERROR)result=H40::Detach();
 if(result==NO_ERROR)result=H41::Detach();
 if(result==NO_ERROR)result=H42::Detach();
 if(result==NO_ERROR)result=H46::Detach();
 if(result==NO_ERROR)result=H47::Detach();
 if(result==NO_ERROR)result=H48::Detach();
 if(result==NO_ERROR)result=H49::Detach();
 if(result==NO_ERROR)result=H50::Detach();
 if(result==NO_ERROR)result=H51::Detach();
 if(result==NO_ERROR)result=H52::Detach();
 if(result==NO_ERROR)result=H53::Detach();
 if(result==NO_ERROR)result=H54::Detach();
 if(result==NO_ERROR)result=H55::Detach();
 if(result==NO_ERROR)result=H57::Detach();
 if(result==NO_ERROR)result=H58::Detach();
 if(result==NO_ERROR)result=H68::Detach();
 if(result==NO_ERROR)result=H110::Detach();
 if(result==NO_ERROR)result=H114::Detach();
 if(result==NO_ERROR)result=H115::Detach();
 if(result==NO_ERROR)result=H116::Detach();
 if(result==NO_ERROR)result=H117::Detach();
 if(result==NO_ERROR)result=H118::Detach();
 if(result==NO_ERROR)result=H131::Detach();
 if(result==NO_ERROR)result=H132::Detach();
 if(result==NO_ERROR)result=H133::Detach();
 if(result==NO_ERROR)result=H134::Detach();
 if(result==NO_ERROR)result=H135::Detach();
 if(result==NO_ERROR)result=H136::Detach();
 if(result==NO_ERROR)result=H137::Detach();
 if(result==NO_ERROR)result=H138::Detach();
 if(result==NO_ERROR)result=H139::Detach();
 if(result==NO_ERROR)result=H145::Detach();
 if(result==NO_ERROR)result=H147::Detach();
 if(result==NO_ERROR)result=H148::Detach();
 if(result==NO_ERROR)result=DetourTransactionCommit();else DetourTransactionAbort();return result;
}
}
