// Inside the existing Vulkan state/retirement namespace. Runtime lifecycle is untouched.
bool ApplyV8Vk(VkCommandBuffer cmd,NVSDK_NGX_Resource_VK* colour,const DlssNrConstants& c){
 if(g_vk.v8Failed)return false;
 auto fail=[](){g_vk.v8Failed=true;return false;};
 if(!g_vk.v8Pass)g_vk.v8Pass=std::make_unique<DlssNr_Vk>("D18 V8",g_vk.device,g_vk.physicalDevice,false,false,true);
 if(!g_vk.v8Pass->CanRender())return fail();
 for(int i=0;i<V8::Count;++i){auto& im=g_vk.v8Scratch[i];auto w=V8::ScratchWidth(i,c.Width),h=V8::ScratchHeight(i,c.Height);
  if(im.Valid()){if(im.width!=w||im.height!=h||im.format!=VK_FORMAT_R32G32B32A32_SFLOAT)return fail();}
  else if(!CreateImage(im,w,h,VK_FORMAT_R32G32B32A32_SFLOAT,true,"V8 scratch",false))return fail();
 }
 OwnedImage output;output.view=colour->Resource.ImageViewInfo.ImageView;output.format=colour->Resource.ImageViewInfo.Format;
 auto resource=[&](int i)->OwnedImage*{
  if(i>=0)return &g_vk.v8Scratch[i];
  if(i==V8::SourceOriginal)return &g_vk.keep;if(i==V8::ProxyInput)return &g_vk.proxy;
  if(i==V8::ModelOutput)return &g_vk.output;if(i==V8::Destination)return &output;return nullptr;
 };
 for(const auto& p:V8::Passes(c.Width,c.Height)){
  auto *a=resource(p.source),*b=resource(p.model),*d=resource(p.original),*coef=resource(p.coeff),*out=resource(p.target),*keep=resource(p.keep);
  for(auto* im:{a,b,d,coef})if(im)Transition(cmd,*im,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  if(out!=&output)Transition(cmd,*out,VK_IMAGE_LAYOUT_GENERAL);
  if(keep)Transition(cmd,*keep,VK_IMAGE_LAYOUT_GENERAL);
  auto cc=c;cc.Mode=p.mode;
  if(!g_vk.v8Pass->Dispatch(cmd,cc,p.width,p.height,a?a->view:VK_NULL_HANDLE,b?b->view:VK_NULL_HANDLE,d?d->view:VK_NULL_HANDLE,coef?coef->view:VK_NULL_HANDLE,out->view,keep?keep->view:VK_NULL_HANDLE,out->format,keep?keep->format:VK_FORMAT_R16G16B16A16_SFLOAT))return fail();
 }
 return true;
}
