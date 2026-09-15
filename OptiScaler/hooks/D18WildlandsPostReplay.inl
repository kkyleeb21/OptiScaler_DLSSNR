// Included inside Wildlands. Private replay may explicitly hand colour to a native draw.
namespace PostReplay {
using Replay=DlssNr::Dx11ColorReplay::Result;
struct Profile {UINT64 hash;UINT bytes;UINT slots[2];UINT count;bool boundary;};
inline constexpr Profile profiles[]={
 {0x6277c995b757aee6ULL,1748,{0,1},2,false},
 {0x66582a5d69dd420bULL,656,{0,1},2,false},
 {0x911ad5384f414e9fULL,1100,{1,0},1,false},
 {0x3f4d34c5e5c3d1cdULL,1604,{1,0},1,false},
 {0xaf3add4dd72ccd8dULL,2352,{1,0},1,false},
 {0xc8780f432eb9201dULL,3180,{1,0},1,false},
 {0xa2fe8e72d47a955bULL,688,{0,0},1,false},
 {0xccf5406356466b11ULL,440,{0,1},2,true},
 {0x322b6332230da2d7ULL,304,{0,0},1,false}};
inline const GUID shaderTag={0xf540782c,0xc683,0x40dd,{0x9e,0xd1,0x17,0x28,0x65,0x41,0x11,0x03}};
inline bool enabled=false;
inline std::atomic<bool> active{false},invalid{false};
using Node=DlssNr::Dx11ReplaySurface;
inline ComPtr<ID3D11UnorderedAccessView> privateUavs[8];
inline DlssNr::Dx11PrivateCompute computeWorkspace;
inline DlssNr::Dx11NativeLayer nativeLayer;inline Node* pendingNativeLayer=nullptr;
inline Node nodes[8];inline UINT nodeCount=0;inline UINT64 allocated=0;
inline ComPtr<ID3D11PixelShader> blit;inline ComPtr<ID3D11SamplerState> linear;
inline ComPtr<ID3D11Buffer> blitConstants;
struct Fence {ComPtr<ID3D11Query> query;bool pending=false,sealed=false,handed=false;ULONGLONG tick=0,scaleGeneration=0;UINT inputWidth=0,inputHeight=0;};
inline Fence fences[4];inline Fence* current=nullptr;
inline UINT64 replayed=0,ready=0,finished=0,lastBoundary=~0ULL,accepted=0;
inline void Event(const char* stage,UINT64 hash=0,UINT detail=0){
 static UINT records=0;if(!log||records>=96)return;++records;
 fprintf(log,"{\"event\":\"sr_post_replay\",\"stage\":\"%s\",\"shader\":\"%016llx\",\"detail\":%u,\"frame\":%llu,\"replayed\":%llu,\"boundary\":%llu,\"gpu_finished\":%llu,\"accepted_frames\":%llu,\"rejected_frames\":%llu,\"tick\":%llu,\"handoffs\":%llu,\"handoff_gpu_completed\":%llu,\"native_layers\":%llu}\n",stage,hash,detail,frameNumber,replayed,ready,finished,accepted,Status::postRejected.load(),GetTickCount64(),Status::handoffs.load(),Status::handoffGpuCompleted.load(),Status::nativeLayers.load());fflush(log);
}
inline void Reject(const char* reason,UINT64 hash=0,UINT detail=0){if(!invalid.exchange(true)){++Status::postRejected;static const char* last=nullptr;static UINT64 lastHash=0;if(last!=reason||lastHash!=hash){last=reason;lastHash=hash;Event(reason,hash,detail);}}}
inline void Register(ID3D11DeviceChild* shader,UINT64 hash){shader->SetPrivateData(shaderTag,sizeof(hash),&hash);}
inline Node* Find(ID3D11Resource* resource){for(UINT i=0;i<nodeCount;++i)if(nodes[i].original.Get()==resource)return &nodes[i];return nullptr;}
inline DlssNr::Dx11WriterJournal::Journal writerJournal;
inline DlssNr::Dx11ComputeIdentity::Archive computeArchive;
inline void TraceWrite(ID3D11DeviceContext* c,const char* kind,ID3D11Resource* destination,ID3D11Resource* source=nullptr,ID3D11View* view=nullptr,const UINT* values=nullptr,const void* caller=nullptr,const UINT* range=nullptr){
 if(!enabled||!active.load()||internalWork||c!=context.Get()||!writerJournal.Selected(frameNumber))return;
 std::unique_lock lock(guard,std::try_to_lock);if(!lock.owns_lock())return;
 writerJournal.Record(frameNumber,invalid.load(),kind,destination,source,view,values,caller,0,0,range);
}
inline void TraceDraw(ID3D11DeviceContext* c){
 if(!enabled||!active.load()||internalWork||c!=context.Get()||!writerJournal.Selected(frameNumber))return;
 std::unique_lock lock(guard,std::try_to_lock);if(!lock.owns_lock())return;
 ComPtr<ID3D11PixelShader> ps;c->PSGetShader(&ps,nullptr,nullptr);UINT64 hash=0;UINT size=sizeof(hash);if(ps)ps->GetPrivateData(shaderTag,&size,&hash);
 const Profile* profile=nullptr;for(auto& p:profiles)if(p.hash==hash){profile=&p;break;}
 ID3D11RenderTargetView* targets[8]{};c->OMGetRenderTargets(8,targets,nullptr);
 bool related=profile!=nullptr;
 for(auto v:targets)if(v){ComPtr<ID3D11Resource> resource;v->GetResource(&resource);related|=Find(resource.Get())!=nullptr;}
 for(UINT slot=0;slot<8;++slot)if(targets[slot]){ComPtr<ID3D11Resource> resource;targets[slot]->GetResource(&resource);if(related)writerJournal.Record(frameNumber,invalid.load(),"draw_target",resource.Get(),nullptr,targets[slot],nullptr,nullptr,hash,slot);targets[slot]->Release();}
 if(!related)return;
 // Known profiles use declared slots; unknown writers get a bounded t0/t1 observation only.
 for(UINT i=0;i<(profile?profile->count:2);++i){UINT slot=profile?profile->slots[i]:i;ComPtr<ID3D11ShaderResourceView> v;c->PSGetShaderResources(slot,1,&v);if(v){ComPtr<ID3D11Resource> resource;v->GetResource(&resource);writerJournal.Record(frameNumber,invalid.load(),"draw_source",nullptr,resource.Get(),v.Get(),nullptr,nullptr,hash,slot);}}
}
// These observers only invalidate private work; original API calls are never suppressed.
inline void Mutation(ID3D11DeviceContext* c,ID3D11Resource* destination,const char* reason){
 if(!enabled||!active.load()||invalid.load()||internalWork||c!=context.Get())return;
 D18ContextTransaction::Scope transaction(c,2);if(!transaction){invalid=true;return;}
 std::unique_lock lock(guard,std::try_to_lock);if(!lock.owns_lock()){invalid=true;return;}
 if(!destination){Reject(reason);return;}
 auto node=Find(destination);if(node&&node->epoch==frameNumber)Reject(reason);
}
inline void CommandList(ID3D11DeviceContext* c,ID3D11CommandList* list,BOOL restore,const void* caller){
 if(!DlssNr::WildlandsSr::postReplay||internalWork||c!=selectedContext)return;
 const bool protects=enabled&&active.load()&&c==context.Get();
 if(!protects){namespace M=DlssNr::Dx11CommandListWrites;auto metadata=M::Read<M::List>(list,M::listTag);DlssNr::Dx11CommandListTrace::journal.Execute(metadata.Get(),3,restore,caller);return;}
 D18ContextTransaction::Scope transaction(c,2);if(!transaction){invalid=true;return;}
 std::unique_lock lock(guard,std::try_to_lock);if(!lock.owns_lock()){invalid=true;return;}
 namespace M=DlssNr::Dx11CommandListWrites;auto metadata=M::Read<M::List>(list,M::listTag);
 UINT decision=0;if(!metadata||metadata->writes.reasons)decision=1;
 if(!decision)for(UINT i=0;i<metadata->writes.size;++i){
  auto resource=metadata->writes.resources[i].Get();ComPtr<IUnknown> identity;resource->QueryInterface(IID_PPV_ARGS(&identity));
  for(UINT j=0;j<nodeCount;++j){auto& n=nodes[j];if(n.epoch!=frameNumber)continue;ComPtr<IUnknown> original;n.original.As(&original);
   if(n.original.Get()==resource||(identity&&identity==original)){decision=2;break;}
   ComPtr<ID3D11Texture2D> texture;if(SUCCEEDED(n.original.As(&texture))){D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);if(desc.MiscFlags&(D3D11_RESOURCE_MISC_TILED|D3D11_RESOURCE_MISC_SHARED|D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX|D3D11_RESOURCE_MISC_SHARED_NTHANDLE)){decision=1;break;}}
  }if(decision)break;
 }
 DlssNr::Dx11CommandListTrace::journal.Execute(metadata.Get(),decision,restore,caller);
 if(Status::nativeHandoff)DlssNr::Dx11CommandListTrace::conflicts.Capture(ProbeRoot(),frameNumber,c,metadata.Get(),decision,restore,caller);
 const UINT details[]={1,metadata?UINT(metadata->id):0,metadata?UINT(metadata->id>>32):0,metadata?metadata->writes.reasons:32u,metadata?metadata->writes.size:0,metadata?metadata->writes.draws:0,metadata?metadata->writes.dispatches:0,metadata?metadata->writes.operations:0,UINT(restore),decision,0,0};
 writerJournal.Record(frameNumber,invalid.load(),"command_list",nullptr,nullptr,nullptr,nullptr,caller,0,0,details);
 if(metadata)for(UINT i=0;i<metadata->writes.size;++i)writerJournal.Record(frameNumber,invalid.load(),"command_list_write",metadata->writes.resources[i].Get(),nullptr,nullptr,nullptr,caller,0,i);
 if(decision==2&&Status::nativeHandoff&&Status::enabled&&!invalid.load()&&current&&metadata&&metadata->writes.reasons==0&&metadata->writes.operations<=1024&&metadata->writes.draws<=8&&metadata->writes.dispatches<=4){
  Node* colour=nullptr;bool multiple=false;
  for(UINT i=0;i<metadata->writes.size;++i)if(auto n=Find(metadata->writes.resources[i].Get());n&&n->epoch==frameNumber){if(colour&&colour!=n)multiple=true;colour=n;}
  if(colour&&!multiple&&colour->originalView){
   ComPtr<ID3D11Texture2D> original;ComPtr<ID3D11Device> d;c->GetDevice(&d);
   auto hr=colour->original.As(&original);if(SUCCEEDED(hr))hr=nativeLayer.Prepare(d.Get(),original.Get(),colour->texture.Get());
   if(SUCCEEDED(hr)){StateScope saved;if(saved){nativeLayer.Snapshot(c,original.Get());pendingNativeLayer=colour;return;}}
  }
 }
 if(decision){Reject(decision==1?"command_list_unknown":"command_list_affects_chain",0,details[3]);return;}
 static bool once=false;if(!once){once=true;Event("command_list_disjoint");}
}
inline void CommandListFinished(ID3D11DeviceContext* c){
 if(!enabled||internalWork||c!=context.Get())return;
 std::unique_lock lock(guard,std::try_to_lock);if(!lock.owns_lock()){invalid=true;return;}
 auto node=pendingNativeLayer;pendingNativeLayer=nullptr;if(!node)return;
 if(!active.load()||invalid.load()||node->epoch!=frameNumber||!current)return;
 StateScope saved;if(!saved){Reject("native_layer_state");return;}
 nativeLayer.Merge(c,node->originalView.Get(),node->view.Get(),node->texture.Get());
 ++Status::nativeLayers;static bool once=false;if(!once){once=true;Event("native_layer_merged");}
}
inline void MutationView(ID3D11DeviceContext* c,ID3D11View* view,const char* reason){
 if(!enabled||!active.load()||internalWork||!view)return;
 ComPtr<ID3D11Resource> resource;view->GetResource(&resource);TraceWrite(c,reason,resource.Get(),nullptr,view);Mutation(c,resource.Get(),reason);
}
inline bool ReplayCompute(ID3D11DeviceContext*,ID3D11ComputeShader*,const DlssNr::Dx11ComputeIdentity::Info&,UINT,UINT,UINT,bool);
inline void ComputeMutation(ID3D11DeviceContext* c,UINT x=0,UINT y=0,UINT z=0,bool indirect=false){
 if(!enabled||!active.load()||internalWork||c!=context.Get())return;
 ComPtr<ID3D11ComputeShader> cs;c->CSGetShader(&cs,nullptr,nullptr);if(!cs)return;
 auto info=DlssNr::Dx11ComputeIdentity::Read(cs.Get());
 if(writerJournal.Selected(frameNumber)){
  std::unique_lock lock(guard,std::try_to_lock);
  if(lock.owns_lock()){
   // Log only dispatches related to a private-chain resource; stale bindings are distinguished.
   bool related=false;
   ComPtr<ID3D11UnorderedAccessView> uavs[64];ComPtr<ID3D11ShaderResourceView> inputs[128];
   for(UINT slot=0;slot<uavSlotCount;++slot){c->CSGetUnorderedAccessViews(slot,1,&uavs[slot]);if(uavs[slot]){ComPtr<ID3D11Resource> resource;uavs[slot]->GetResource(&resource);related|=Find(resource.Get())!=nullptr;}}
   for(UINT slot=0;slot<128;++slot)if(info.known&&(info.srv[slot/64]&(1ULL<<(slot%64)))){c->CSGetShaderResources(slot,1,&inputs[slot]);if(inputs[slot]){ComPtr<ID3D11Resource> resource;inputs[slot]->GetResource(&resource);related|=Find(resource.Get())!=nullptr;}}
   if(related){
    if(DlssNr::BuildProfile::ResearchCaptureRequested(ProbeRoot())&&!computeArchive.Capture(ProbeRoot(),cs.Get(),info))writerJournal.Record(frameNumber,invalid.load(),"compute_archive_missing",nullptr,nullptr,nullptr,nullptr,nullptr,info.hash);
    const UINT details[]={x,y,z,info.group[0],info.group[1],info.group[2],UINT(indirect),info.known,UINT(info.uav),UINT(info.uav>>32),info.cbMask,2};
    writerJournal.Record(frameNumber,invalid.load(),"dispatch",nullptr,nullptr,nullptr,nullptr,nullptr,info.hash,0,details);
    for(UINT slot=0;slot<uavSlotCount;++slot)if(uavs[slot]){ComPtr<ID3D11Resource> resource;uavs[slot]->GetResource(&resource);const bool used=info.known&&(info.uav&(1ULL<<slot));if(used||Find(resource.Get()))writerJournal.Record(frameNumber,invalid.load(),used?"compute_declared_target":info.known?"compute_bound_unused":"compute_unknown_target",resource.Get(),nullptr,uavs[slot].Get(),nullptr,nullptr,info.hash,slot);}
    for(UINT slot=0;slot<14;++slot)if(info.known&&(info.cbMask&(1u<<slot))){
     ComPtr<ID3D11Buffer> buffer;UINT first=0,count=0;context->CSGetConstantBuffers1(slot,1,&buffer,&first,&count);D3D11_BUFFER_DESC bd{};if(buffer)buffer->GetDesc(&bd);
     const UINT detail[]={first,count,info.cbVectors[slot],bd.ByteWidth,0,0,0,0,0,0,0,0};writerJournal.Record(frameNumber,invalid.load(),"compute_constant_buffer",nullptr,buffer.Get(),nullptr,nullptr,nullptr,info.hash,slot,detail);
    }
    for(UINT slot=0;slot<128;++slot)if(inputs[slot]){ComPtr<ID3D11Resource> resource;inputs[slot]->GetResource(&resource);writerJournal.Record(frameNumber,invalid.load(),"compute_source",nullptr,resource.Get(),inputs[slot].Get(),nullptr,nullptr,info.hash,slot);}
   }
  }
 }
 if(ReplayCompute(c,cs.Get(),info,x,y,z,indirect))return;
 for(UINT slot=0;slot<uavSlotCount;++slot){
  if(info.known&&!(info.uav&(1ULL<<slot)))continue;
  ComPtr<ID3D11UnorderedAccessView> v;c->CSGetUnorderedAccessViews(slot,1,&v);if(v){ComPtr<ID3D11Resource> resource;v->GetResource(&resource);Mutation(c,resource.Get(),info.known?"compute_write":"compute_unknown");}
 }
}
inline Node* Ensure(ID3D11Device* d,ID3D11Resource* original,bool boundary=false){
 if(auto n=Find(original))return n;
 if(nodeCount>=8)return nullptr;
 ComPtr<ID3D11Texture2D> source;if(FAILED(original->QueryInterface(IID_PPV_ARGS(&source))))return nullptr;
 D3D11_TEXTURE2D_DESC td{};source->GetDesc(&td);
 if(td.MipLevels!=1||td.ArraySize!=1||td.SampleDesc.Count!=1)return nullptr;
 if(td.Format!=DXGI_FORMAT_R10G10B10A2_UNORM&&td.Format!=DXGI_FORMAT_R8G8B8A8_UNORM&&td.Format!=DXGI_FORMAT_R16G16B16A16_FLOAT)return nullptr;
 if(!td.Width||!td.Height||td.Width>resolution.outputWidth||td.Height>resolution.outputHeight)return nullptr;
 UINT ow=boundary?td.Width:UINT((UINT64(td.Width)*resolution.outputWidth+width/2)/width);
 UINT oh=boundary?td.Height:UINT((UINT64(td.Height)*resolution.outputHeight+height/2)/height);
 if(!(td.BindFlags&D3D11_BIND_SHADER_RESOURCE)&&(ow!=td.Width||oh!=td.Height))return nullptr;
 UINT64 bytes=UINT64(ow)*oh*(td.Format==DXGI_FORMAT_R16G16B16A16_FLOAT?8:4);
 if(!ow||!oh||ow>8192||oh>8192||allocated+bytes>192ULL*1024*1024)return nullptr;
 auto& n=nodes[nodeCount];n.original=original;n.w=td.Width;n.h=td.Height;n.ow=ow;n.oh=oh;
 HRESULT hr=S_OK;if(td.BindFlags&D3D11_BIND_SHADER_RESOURCE)hr=d->CreateShaderResourceView(original,nullptr,&n.originalView);
 td.Width=ow;td.Height=oh;td.Usage=D3D11_USAGE_DEFAULT;td.CPUAccessFlags=td.MiscFlags=0;td.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;
 UINT support=0;if(SUCCEEDED(d->CheckFormatSupport(td.Format,&support))&&(support&D3D11_FORMAT_SUPPORT_TYPED_UNORDERED_ACCESS_VIEW))td.BindFlags|=D3D11_BIND_UNORDERED_ACCESS;
 if(SUCCEEDED(hr))hr=d->CreateTexture2D(&td,nullptr,&n.texture);
 if(SUCCEEDED(hr))hr=d->CreateShaderResourceView(n.texture.Get(),nullptr,&n.view);
 if(SUCCEEDED(hr))hr=d->CreateRenderTargetView(n.texture.Get(),nullptr,&n.target);
 if(FAILED(hr)){n=Node{};return nullptr;}
 ++nodeCount;allocated+=bytes;Event("private_target_created",0,nodeCount);return &n;
}

inline ID3D11UnorderedAccessView* PrivateUav(Node& n){
 auto& v=privateUavs[&n-nodes];if(!v){ComPtr<ID3D11Device> d;context->GetDevice(&d);if(FAILED(d->CreateUnorderedAccessView(n.texture.Get(),nullptr,&v)))return nullptr;}return v.Get();
}
// Full-view clears are reproducible without assumptions about later draw coverage.
inline bool ClearPrivate(ID3D11DeviceContext* c,ID3D11View* view,const UINT* bits,bool uintClear,bool renderTarget){
 if(!enabled||!active.load()||invalid.load()||internalWork||c!=context.Get()||!view||!bits)return false;
 D18ContextTransaction::Scope transaction(c,2);if(!transaction){invalid=true;return false;}
 std::unique_lock lock(guard,std::try_to_lock);if(!lock.owns_lock()){invalid=true;return false;}
 ComPtr<ID3D11Resource> resource;view->GetResource(&resource);auto n=Find(resource.Get());if(!n||n->epoch!=frameNumber)return false;
 D3D11_TEXTURE2D_DESC td{};n->texture->GetDesc(&td);
 if(renderTarget){ComPtr<ID3D11RenderTargetView> v;if(FAILED(view->QueryInterface(IID_PPV_ARGS(&v))))return false;D3D11_RENDER_TARGET_VIEW_DESC vd{};v->GetDesc(&vd);if(vd.ViewDimension!=D3D11_RTV_DIMENSION_TEXTURE2D||vd.Texture2D.MipSlice||vd.Format!=td.Format)return false;}
 else {ComPtr<ID3D11UnorderedAccessView> v;if(FAILED(view->QueryInterface(IID_PPV_ARGS(&v))))return false;D3D11_UNORDERED_ACCESS_VIEW_DESC vd{};v->GetDesc(&vd);if(vd.ViewDimension!=D3D11_UAV_DIMENSION_TEXTURE2D||vd.Texture2D.MipSlice||vd.Format!=td.Format)return false;}
 ScopedInternalContext internal;auto u=renderTarget?nullptr:PrivateUav(*n);if(!renderTarget&&!u)return false;
 FLOAT values[4];memcpy(values,bits,sizeof(values));
 if(renderTarget)c->ClearRenderTargetView(n->target.Get(),values);else if(uintClear)c->ClearUnorderedAccessViewUint(u,bits);else c->ClearUnorderedAccessViewFloat(u,values);
 ++Status::postCleared;static bool once=false;if(!once){once=true;Event("private_clear");}return true;
}
inline bool InitializeTarget(Node& node);
inline bool ReplayCompute(ID3D11DeviceContext* c,ID3D11ComputeShader* cs,const DlssNr::Dx11ComputeIdentity::Info& info,UINT x,UINT y,UINT z,bool indirect){
 if(invalid.load()||info.hash!=0xb57679d3bd5acecdULL)return false;
 D18ContextTransaction::Scope transaction(c,2);if(!transaction){invalid=true;return false;}
 std::unique_lock lock(guard,std::try_to_lock);if(!lock.owns_lock()){invalid=true;return false;}
 static bool traced=false;const bool trace=!traced;traced=true;if(trace)Event("compute_enter",info.hash);
 auto bad=[&](const char* reason){Reject(reason,info.hash);return false;};
 if(indirect||!info.known||info.uav!=1||info.srv[0]!=3||info.srv[1]||info.cbMask!=32||info.cbVectors[5]!=3||info.group[0]!=18||info.group[1]!=18||info.group[2]!=1)return bad("compute_contract");
 ComPtr<ID3D11Predicate> predicate;c->GetPredication(&predicate,nullptr);UINT classes=0;ComPtr<ID3D11ComputeShader> checkedCs;c->CSGetShader(&checkedCs,nullptr,&classes);if(predicate||classes)return bad("compute_pipeline");
 ComPtr<ID3D11ShaderResourceView> sources[2];c->CSGetShaderResources(0,1,&sources[0]);c->CSGetShaderResources(1,1,&sources[1]);
 ComPtr<ID3D11UnorderedAccessView> target;c->CSGetUnorderedAccessViews(0,1,&target);
 if(!sources[0]||!sources[1]||!target)return bad("compute_bindings");
 ComPtr<ID3D11Resource> colour,guide,destination;sources[0]->GetResource(&colour);sources[1]->GetResource(&guide);target->GetResource(&destination);
 auto input=Find(colour.Get());auto output=Find(destination.Get());
 if(!input||input->epoch!=frameNumber||colour==destination||guide==destination||guide==colour)return bad("compute_lineage");
 // A compute-first branch has no preceding draw to initialize its destination.
 // Require current input lineage; initialize the output from its native contents
 // (including the game's preceding clear), then retain the existing dispatch contract.
 if(!output){ComPtr<ID3D11Device> d;c->GetDevice(&d);output=Ensure(d.Get(),destination.Get());}
 if(!output)return bad("compute_target");
 D3D11_SHADER_RESOURCE_VIEW_DESC vd{};sources[0]->GetDesc(&vd);D3D11_UNORDERED_ACCESS_VIEW_DESC ud{};target->GetDesc(&ud);
 ComPtr<ID3D11Texture2D> gt;if(FAILED(guide.As(&gt)))return bad("compute_guide");D3D11_TEXTURE2D_DESC gd{};gt->GetDesc(&gd);
 if(vd.Format!=DXGI_FORMAT_R10G10B10A2_UNORM||vd.ViewDimension!=D3D11_SRV_DIMENSION_TEXTURE2D||vd.Texture2D.MostDetailedMip||vd.Texture2D.MipLevels!=1||ud.Format!=vd.Format||ud.ViewDimension!=D3D11_UAV_DIMENSION_TEXTURE2D||ud.Texture2D.MipSlice)return bad("compute_view");
 if(input->w!=output->w||input->h!=output->h||input->ow!=output->ow||input->oh!=output->oh||gd.Width!=input->w||gd.Height!=input->h||x!=(input->w+15)/16||y!=(input->h+15)/16||z!=1)return bad("compute_dimensions");
 ComPtr<ID3D11Buffer> cb;UINT first=0,count=0;context->CSGetConstantBuffers1(5,1,&cb,&first,&count);if(!cb||count<3)return bad("compute_constants");
 ComPtr<ID3D11SamplerState> sampler;c->CSGetSamplers(0,1,&sampler);if(!sampler)return bad("compute_sampler");
 ScopedInternalContext internal;auto u=PrivateUav(*output);if(!u)return bad("compute_target");
 if(trace)Event("compute_capture",info.hash);
 StateScope scope;if(!scope)return bad("compute_state");
 if(output->epoch!=frameNumber&&!InitializeTarget(*output))return bad("compute_target_initialization");
 if(trace)Event("compute_prepare_begin",info.hash);
 auto hr=computeWorkspace.Prepare(c,cb.Get(),first,output->ow,output->oh,sources[1].Get());if(FAILED(hr))return bad("compute_prepare");
 if(trace)Event("compute_prepare_end",info.hash);
 ResetBindings();c->CSSetShader(cs,nullptr,0);auto sc=sampler.Get();c->CSSetSamplers(0,1,&sc);
 auto b=computeWorkspace.Constants();context->CSSetConstantBuffers1(5,1,&b,&first,&count);
 ID3D11ShaderResourceView* inputs[]={input->view.Get(),computeWorkspace.Scalar()};c->CSSetShaderResources(0,2,inputs);c->CSSetUnorderedAccessViews(0,1,&u,nullptr);
 c->Dispatch((output->ow+15)/16,(output->oh+15)/16,1);
 output->epoch=frameNumber;
 ++Status::postComputed;++replayed;Status::postReplayed=replayed;static bool once=false;if(!once){once=true;Event("private_compute",info.hash);}return true;
}

inline bool Blit(Node& node,ID3D11ShaderResourceView* colour,ID3D11ShaderResourceView* alpha){
 StateScope scope;if(!scope)return false;
 auto rt=node.target.Get();context->OMSetRenderTargets(1,&rt,nullptr);
 D3D11_VIEWPORT vp{0,0,float(node.ow),float(node.oh),0,1};context->RSSetViewports(1,&vp);context->RSSetState(raster.Get());
 context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);context->VSSetShader(fullscreen.Get(),nullptr,0);context->PSSetShader(blit.Get(),nullptr,0);
 auto cb=blitConstants.Get();context->PSSetConstantBuffers(0,1,&cb);
 ID3D11ShaderResourceView* views[]={colour,alpha};context->PSSetShaderResources(0,2,views);auto sampler=linear.Get();context->PSSetSamplers(0,1,&sampler);
 context->Draw(3,0);return true;
}
inline bool Init(ID3D11Device* d){
 if(blit&&linear)return true;
 // SV_Position belongs to the private viewport. A small explicit CB avoids assuming
 // that either source texture has the target dimensions.
 const char source[]="Texture2D<float4> c:register(t0);Texture2D<float4> a:register(t1);SamplerState s:register(s0);cbuffer B:register(b0){float2 inverseSize;float2 pad;}float4 main(float4 p:SV_Position):SV_Target{float2 uv=p.xy*inverseSize;return float4(c.SampleLevel(s,uv,0).rgb,a.SampleLevel(s,uv,0).a);}";
 ComPtr<ID3DBlob> code,error;auto hr=D3DCompile(source,sizeof(source)-1,nullptr,nullptr,nullptr,"main","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&error);
 if(SUCCEEDED(hr))hr=d->CreatePixelShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&blit);
 D3D11_SAMPLER_DESC sd{};sd.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sd.MaxLOD=D3D11_FLOAT32_MAX;
 if(SUCCEEDED(hr))hr=d->CreateSamplerState(&sd,&linear);
 return SUCCEEDED(hr);
}
inline bool Copy(Node& n,ID3D11ShaderResourceView* colour,ID3D11ShaderResourceView* alpha){
 ComPtr<ID3D11Device> d;context->GetDevice(&d);if(!Init(d.Get()))return false;
 if(!blitConstants){D3D11_BUFFER_DESC bd{};bd.ByteWidth=16;bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;if(FAILED(d->CreateBuffer(&bd,nullptr,&blitConstants)))return false;}
 // Blit's state scope clears bindings, so the constant binding is installed there.
 float values[]={1.0f/n.ow,1.0f/n.oh,0,0};context->UpdateSubresource(blitConstants.Get(),0,nullptr,values,0,0);
 return Blit(n,colour,alpha);
}
inline bool InitializeTarget(Node& node){
 if(node.originalView)return Copy(node,node.originalView.Get(),node.originalView.Get());
 // CopyResource needs no source SRV. Preserve all existing target pixels before replay.
 if(node.w!=node.ow||node.h!=node.oh)return false;
 StateScope scope;if(!scope)return false;
 context->CopyResource(node.texture.Get(),node.original.Get());return true;
}
inline void Begin(ID3D11Device* d,ID3D11Resource* original){
 if(!enabled)return;active=false;invalid=false;current=nullptr;pendingNativeLayer=nullptr;
 for(auto& f:fences)if(!f.pending){if(!f.query){D3D11_QUERY_DESC qd{D3D11_QUERY_EVENT,0};if(FAILED(d->CreateQuery(&qd,&f.query))){Reject("query_allocation");return;}}current=&f;break;}
 if(!current){Reject("query_slots_busy");return;}
 current->pending=true;current->sealed=false;current->handed=false;current->tick=GetTickCount64();current->scaleGeneration=DlssNr::WildlandsScale::request.generation;current->inputWidth=width;current->inputHeight=height;active=true;if(Config::Instance()->DlssNrDiagnostics.value_or_default()!=0)writerJournal.Start(ProbeRoot(),frameNumber);
 DlssNr::Dx11CommandListTrace::journal.Mark(context.Get(),"sr_private_seed");
 auto root=Ensure(d,original);if(!root||!root->originalView||!Copy(*root,srv[3].Get(),root->originalView.Get())){Reject("seed_failed");return;}
 root->epoch=frameNumber;static bool reported=false;if(!reported){reported=true;Event("seeded");}
}
// Hold only this draw's input substitution; original colour/depth draw executes once.
class NativeHandoff {
 std::unique_lock<std::recursive_mutex> transaction{D18ContextTransaction::mutex,std::defer_lock};
 std::unique_lock<std::mutex> lock{guard,std::defer_lock};
 DlssNr::Dx11ScopedPsInput binding;ID3D11DeviceContext* ctx=nullptr;
 public:
 NativeHandoff(ID3D11DeviceContext* c,const Dx11DrawReplay::Command& command){
  if(!Status::nativeHandoff||!Status::enabled||!enabled||!active.load()||invalid.load()||internalWork||c!=context.Get()||!command.Supported())return;
  if(!transaction.try_lock()||!lock.try_lock()){invalid=true;return;}
  ComPtr<ID3D11PixelShader> ps;c->PSGetShader(&ps,nullptr,nullptr);UINT64 hash=0;UINT size=sizeof(hash);if(ps)ps->GetPrivateData(shaderTag,&size,&hash);
  if(hash!=0xccf5406356466b11ULL)return;
  ComPtr<ID3D11ShaderResourceView> input;c->PSGetShaderResources(0,1,&input);ComPtr<ID3D11Resource> original;if(input)input->GetResource(&original);
  auto node=Find(original.Get());if(!node||node->epoch!=frameNumber||!current){Reject("handoff_source_missing",hash);return;}
  auto result=binding.Bind(c,0,12,node->view.Get(),resolution.outputWidth,resolution.outputHeight);
  if(result!=DlssNr::Dx11ScopedPsInput::Ready){Reject("handoff_contract",hash,UINT(result));return;}
  ctx=c;
 }
 ~NativeHandoff(){
  if(!ctx)return;binding.Restore();
  // Queue closure follows the native draw's read, never its submission preparation.
  ctx->End(current->query.Get());current->sealed=true;current->handed=true;current=nullptr;active=false;
  lastBoundary=frameNumber;++ready;++accepted;++Status::handoffs;Status::postReady=ready;Status::postAccepted=accepted;
  static bool once=false;if(!once){once=true;Event("native_handoff",0xccf5406356466b11ULL);}
 }
 NativeHandoff(const NativeHandoff&)=delete;NativeHandoff& operator=(const NativeHandoff&)=delete;
};
inline void Observe(ID3D11DeviceContext* c,const Dx11DrawReplay::Command& command){
 TraceDraw(c);
 if(!enabled||!active.load()||invalid.load()||internalWork||c!=context.Get()||c->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE)return;
 D18ContextTransaction::Scope transaction(c,2);if(!transaction){invalid=true;return;}
 std::unique_lock lock(guard,std::try_to_lock);if(!lock.owns_lock()){invalid=true;return;}
 ComPtr<ID3D11PixelShader> ps;c->PSGetShader(&ps,nullptr,nullptr);UINT64 hash=0;UINT bytes=sizeof(hash);if(ps)ps->GetPrivateData(shaderTag,&bytes,&hash);
 const Profile* profile=nullptr;for(const auto& p:profiles)if(p.hash==hash){profile=&p;break;}
 ComPtr<ID3D11RenderTargetView> rt;c->OMGetRenderTargets(1,&rt,nullptr);if(!rt)return;
 ComPtr<ID3D11Resource> original;rt->GetResource(&original);auto existing=Find(original.Get());
 if(!profile){if(existing&&existing->epoch==frameNumber)Reject("unknown_writer",hash);return;}
 DlssNr::Dx11ColorReplay::Replacement replacements[2]{};UINT count=0;
 for(UINT i=0;i<profile->count;++i){ComPtr<ID3D11ShaderResourceView> view;c->PSGetShaderResources(profile->slots[i],1,&view);if(!view)continue;ComPtr<ID3D11Resource> resource;view->GetResource(&resource);
  if(auto node=Find(resource.Get());node&&node->epoch==frameNumber)replacements[count++]={profile->slots[i],node->view.Get()};}
 if(!count&&(!existing||existing->epoch!=frameNumber))return;
 ComPtr<ID3D11Device> device;c->GetDevice(&device);ScopedInternalContext internal;
 bool nativeTarget=profile->boundary;
 if(hash==0x322b6332230da2d7ULL){
  // A copy without a current private source cannot propagate an SR lineage.
  if(!count){Reject("copy_source_untracked",hash);return;}
  ComPtr<ID3D11Texture2D> t;if(SUCCEEDED(original.As(&t))){D3D11_TEXTURE2D_DESC td{};t->GetDesc(&td);nativeTarget=td.Width==resolution.outputWidth&&td.Height==resolution.outputHeight;}
 }
 auto output=Ensure(device.Get(),original.Get(),nativeTarget);if(!output){Reject("target_contract",hash);return;}
 if(output->epoch!=frameNumber){ComPtr<ID3D11BlendState> blend;c->OMGetBlendState(&blend,nullptr,nullptr);if(blend){D3D11_BLEND_DESC desc{};blend->GetDesc(&desc);if(desc.RenderTarget[0].BlendEnable){Reject("uninitialized_blend",hash);return;}}
  if(!InitializeTarget(*output)){Reject("target_initialization",hash);return;}}
 auto result=DlssNr::Dx11ColorReplay::Replay(context.Get(),command,output->target.Get(),output->w,output->h,output->ow,output->oh,replacements,count,profile->boundary);
 if(result!=Replay::Submitted){Reject("pipeline_contract",hash,UINT(result));return;}
 output->epoch=frameNumber;++replayed;Status::postReplayed=replayed;
 // At 1:1 the engine omits the upscale handoff. The captured final blend
 // completes this private post colour before subsequent native layer lists.
 // Copy the completed same-size colour once; do not replay those native lists.
 if(hash==0xa2fe8e72d47a955bULL&&Status::nativeHandoff&&Status::enabled&&
    width==resolution.outputWidth&&height==resolution.outputHeight&&current&&
    output->w==width&&output->h==height&&output->ow==width&&output->oh==height){
  ComPtr<ID3D11Texture2D> native;
  if(FAILED(output->original.As(&native))||!DlssNr::SameSizeColourCopyValid(c,output->texture.Get(),native.Get(),width,height)){Reject("same_size_handoff_contract",hash);return;}
  {StateScope saved;if(!saved){Reject("same_size_handoff_state",hash);return;}c->CopyResource(native.Get(),output->texture.Get());}
  c->End(current->query.Get());current->sealed=true;current->handed=true;current=nullptr;active=false;
  lastBoundary=frameNumber;++ready;++accepted;++Status::handoffs;Status::postReady=ready;Status::postAccepted=accepted;
  static bool once=false;if(!once){once=true;Event("native_same_size_handoff",hash);}return;
 }
 if(hash==0x3f4d34c5e5c3d1cdULL){static bool once=false;if(!once){once=true;Event("alternate_blur",hash);}}
 if(hash==0xc8780f432eb9201dULL){static bool once=false;if(!once){once=true;Event("alternate_composite",hash);}}
 static UINT trace=0;if(trace++<24)Event("replayed",hash);
 if(profile->boundary){lastBoundary=frameNumber;++ready;Status::postReady=ready;}
}
// Both SR and the private post graph must retire before releasing dimension-bound resources.
inline bool Drained(){
 if(active.load()||current||pendingNativeLayer)return false;
 for(const auto& f:fences)if(f.pending)return false;
 return true;
}
inline bool ResetResolution(){
 if(!Drained())return false;
 for(auto& u:privateUavs)u.Reset();
 for(auto& n:nodes)n=Node{};
 computeWorkspace=DlssNr::Dx11PrivateCompute{};
 nativeLayer=DlssNr::Dx11NativeLayer{};
 nodeCount=0;allocated=0;lastBoundary=~0ULL;invalid=false;
 return true;
}
inline void Frame(){
 DlssNr::Dx11CommandListTrace::conflicts.Tick(frameNumber);
 if(!enabled||!context)return;
 if(active.exchange(false)&&current){if(!invalid.load()&&lastBoundary==frameNumber){++accepted;Status::postAccepted=accepted;}context->End(current->query.Get());current->sealed=true;current=nullptr;}
 for(auto& f:fences)if(f.pending&&f.sealed){BOOL done=FALSE;auto hr=context->GetData(f.query.Get(),&done,sizeof(done),D3D11_ASYNC_GETDATA_DONOTFLUSH);
  if(FAILED(hr)){Fail("post_query_failed",hr);return;}if(hr==S_OK&&done){f.pending=false;++finished;if(f.handed){++Status::handoffGpuCompleted;Status::handoffGpuTick=GetTickCount64();DlssNr::WildlandsScale::SrCompleted(f.scaleGeneration,f.inputWidth,f.inputHeight);}Status::postGpuFinished=finished;}
  else if(GetTickCount64()-f.tick>1500){Fail("post_query_timeout",WAIT_TIMEOUT);return;}}
 static ULONGLONG last=0;if(finished&&GetTickCount64()-last>1000){last=GetTickCount64();Event("progress");}
}
}
