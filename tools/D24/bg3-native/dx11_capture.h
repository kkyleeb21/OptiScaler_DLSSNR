// Bounded opt-in crops. Map synchronizes only diagnostic frames.
#include "capture_schedule.h"
static CaptureSchedule captureSchedule;
static std::mutex captureStatusMutex;
static DlssNrNative::CaptureStatus captureLive;
static uint64_t captureRequest=0;
static uint64_t captureRun=0,captureStart=0;
static unsigned captureColourMask=0,captureStages=0,captureComplete=0;
static bool captureActive=false;
static bool captureFull8=false,captureError=false;
static unsigned captureWidth=0,captureHeight=0;
static uint64_t captureBytes=0;
static unsigned captureTarget(){return captureFull8?DlssNrNative::Full8::Target:32;}
static const char* captureProfile(){return captureFull8?"full8":"crop32";}
static const char* captureScope(){return captureFull8?"full_frame":"region";}
static unsigned captureFrameId=0;
static uint64_t captureCreation(){
 static const uint64_t value=[](){FILETIME c{},e{},k{},u{};
  return GetProcessTimes(GetCurrentProcess(),&c,&e,&k,&u)?
   (uint64_t(c.dwHighDateTime)<<32)|c.dwLowDateTime:uint64_t(0);}();
 return value;
}
static void capturePublish(capture::control::Phase phase){
 std::lock_guard lock(captureStatusMutex);captureLive.request=captureRequest;captureLive.phase=phase;
 captureLive.tick=GetTickCount64();captureLive.run=captureRun;captureLive.selected=captureSchedule.selected;captureLive.saved=captureComplete;
 captureLive.target=captureTarget();
}
static void captureComposeConstants(const DlssNrConstants& c){
 if(!captureActive||!logFile)return;
 logPrint(logFile,"{\"event\":\"dx11_capture_constants\",\"run\":%llu,\"frame\":%u,\"stage_mode\":%u,\"resolve_constants_hex\":\"",captureRun,captureFrameId,c.Mode);
 const auto* bytes=reinterpret_cast<const unsigned char*>(&c);
 for(size_t i=0;i<capture::kNamedConstantBytes;++i)logPrint(logFile,"%02x",bytes[i]);
 logPrint(logFile,"\"");capture::writeNamedConstants(logFile,c);
 logPrint(logFile,",\"sh0_schema\":1,\"sh0_requested\":%u,\"sh0_mode\":%u,\"sh0_half\":%u,\"sh0_mid\":%.9g,\"sh0_fine\":%.9g",sharpSettings.enabled,sharpSettings.mode,sharpSettings.half,sharpSettings.mid,sharpSettings.fine);
 logPrint(logFile,"}\n");fflush(logFile);
}
static void captureProgress(const char* state){
 using P=capture::control::Phase;
 capturePublish(!strcmp(state,"complete")?P::Complete:!strcmp(state,"failed")?P::Failed:!strcmp(state,"stopped")?P::Cancelled:!strcmp(state,"deadline_or_limit")?P::TimedOut:P::Recording);
 if(logFile){logPrint(logFile,"{\"event\":\"dx11_capture_progress\",\"run\":%llu,\"request\":%llu,\"pid\":%lu,\"creation\":\"%llu\",\"state\":\"%s\",\"selected\":%u,\"complete_colour_frames\":%u,\"limit\":%u,\"elapsed_ms\":%llu,\"capture_profile_schema\":\"d18-capture-profile-v1\",\"capture_profile\":\"%s\",\"requested_frames\":%u,\"capture_scope\":\"%s\",\"raw_bytes_written\":%llu}\n",captureRun,captureRequest,GetCurrentProcessId(),captureCreation(),state,captureSchedule.selected,captureComplete,captureTarget(),GetTickCount64()-captureSchedule.start,captureProfile(),captureTarget(),captureScope(),captureBytes);fflush(logFile);}
 FILE* status=nullptr;_wfopen_s(&status,(directory()/L"D24CaptureStatus.txt").c_str(),L"w");
 if(status){fprintf(status,"DX11 capture %s (%s)\nSelected %u / %u; complete colour frames %u\nElapsed %llu ms. %s\nUse Request capture to start a new capture. Readback can slow sampled frames.\n",state,captureProfile(),captureSchedule.selected,captureTarget(),captureComplete,GetTickCount64()-captureSchedule.start,captureFull8?"Eight full colour frames; deadline 30 seconds.":"Four bursts of 8; target spans 4.5 seconds, deadline 15 seconds.");fclose(status);}
}
static void captureDisarm(){if(captureSchedule.armed&&!captureSchedule.finished)captureProgress("stopped");captureSchedule.reset();}
// Called with the session entry lock held. Freeze the profile for this request.
static int captureCommand(uint64_t request,unsigned armed,bool full8){
 if(captureRequest==request&&captureFull8!=full8)return 0;
 if(captureRequest!=request){captureDisarm();captureRequest=request;captureFull8=full8;
  captureComplete=0;captureBytes=0;captureWidth=captureHeight=0;captureError=false;capturePublish(capture::control::Phase::Idle);}
 if(armed&&!captureSchedule.armed)capturePublish(capture::control::Phase::WaitingNr);
 if(!armed){captureDisarm();std::lock_guard statusLock(captureStatusMutex);
   if(captureLive.phase!=capture::control::Phase::Complete&&captureLive.phase!=capture::control::Phase::Failed&&captureLive.phase!=capture::control::Phase::TimedOut)captureLive.phase=capture::control::Phase::Cancelled;
   captureLive.tick=GetTickCount64();}
 return 1;
}
static float captureCenterX=0.5f,captureCenterY=0.5f;
static unsigned captureExtent=512;
static bool captureFrame(Session& s){
 static const bool enabled=GetFileAttributesW((directory()/L"D24Dx11Diagnostics.enabled").c_str())!=INVALID_FILE_ATTRIBUTES;
 const bool armed=managed?control.capture!=0:enabled;
 if(!armed){captureDisarm();return false;}
 const bool starting=!captureSchedule.armed,wasFinished=captureSchedule.finished;
 if(starting)captureSchedule.configure(captureTarget());
 const bool selected=captureSchedule.select(true,GetTickCount64());
 if(starting){FILETIME ft{};GetSystemTimeAsFileTime(&ft);captureRun=(uint64_t(ft.dwHighDateTime)<<32)|ft.dwLowDateTime;captureComplete=0;captureBytes=0;captureWidth=s.desc.Width;captureHeight=s.desc.Height;captureProgress("recording");}
 if(!selected){if(!wasFinished&&captureSchedule.finished)captureProgress("deadline_or_limit");return false;}
 // Configure follows the command in the legacy bridge. Check its actual value
 // here, after Configure, rather than rejecting against stale previous settings.
 if(captureFull8&&(!managed||!DlssNrNative::Full8::DiagnosticsEnabled(control.diagnostics))){
  captureSchedule.finished=true;event("capture_full8_diagnostics_required",1);captureProgress("failed");return false;}
 if(captureFull8&&!DlssNrNative::Full8::FullColourBounds(s.desc.Width,s.desc.Height,captureWidth,captureHeight,16)){
  captureSchedule.finished=true;event("capture_full8_dimensions_rejected",1);captureProgress("failed");return false;}
 captureStart=GetTickCount64();captureColourMask=captureStages=0;captureActive=true;
 captureError=false;
 // Snapshot once for all four stages of this frame. Normalized coordinates
 // keep a chosen region at the same screen location across resolutions.
 float x=0.5f,y=0.5f;std::ifstream region(directory()/L"D24Dx11CaptureROI.txt");
 if(!(region>>x>>y)||!std::isfinite(x)||!std::isfinite(y)||x<0||x>1||y<0||y>1){x=y=0.5f;}
 unsigned extent=512;if(!(region>>extent)||extent<64||extent>512)extent=512;
 captureExtent=extent;
 if(managed){x=control.captureX;y=control.captureY;captureExtent=std::clamp(control.captureSize,64u,512u);}
 captureCenterX=x;captureCenterY=y;
 return true;
}
static void captureCrop(Session& s,ID3D11Texture2D* texture,unsigned frame,const char* stage,unsigned validWidth=0,unsigned validHeight=0){
 D3D11_TEXTURE2D_DESC d{};texture->GetDesc(&d);
 const bool fullColour=captureFull8&&(!strcmp(stage,"sr")||!strcmp(stage,"input")||!strcmp(stage,"model")||!strcmp(stage,"composed"));
 const unsigned bpp=d.Format==DXGI_FORMAT_R16G16B16A16_FLOAT?8u:d.Format==DXGI_FORMAT_R32G32B32A32_FLOAT?16u:
                    (d.Format==DXGI_FORMAT_R32G32_FLOAT||d.Format==DXGI_FORMAT_R32G32_TYPELESS||d.Format==DXGI_FORMAT_R32G8X24_TYPELESS)?8u:
                    (d.Format==DXGI_FORMAT_R16_UNORM||d.Format==DXGI_FORMAT_R16_TYPELESS)?2u:
                    (d.Format==DXGI_FORMAT_R11G11B10_FLOAT||d.Format==DXGI_FORMAT_R32_FLOAT||d.Format==DXGI_FORMAT_R32_TYPELESS||d.Format==DXGI_FORMAT_R24G8_TYPELESS||d.Format==DXGI_FORMAT_R16G16_TYPELESS||d.Format==DXGI_FORMAT_R16G16_FLOAT)?4u:0u;
 if(!bpp||d.SampleDesc.Count!=1){event("capture_unsupported_format",d.Format);return;}
 if(fullColour&&(d.MipLevels!=1||d.ArraySize!=1||!DlssNrNative::Full8::FullColourBounds(d.Width,d.Height,captureWidth,captureHeight,bpp))){event("capture_full8_colour_layout_rejected",1);return;}
 const unsigned sourceWidth=d.Width,sourceHeight=d.Height;
 // D3D11 depth-stencil copies require an entire subresource. Crop on CPU
 // after full readback, preserving raw depth/stencil bits independently of convert().
 const bool wholeDepth=(d.BindFlags&D3D11_BIND_DEPTH_STENCIL)||d.Format==DXGI_FORMAT_R32G8X24_TYPELESS||d.Format==DXGI_FORMAT_R24G8_TYPELESS;
 if(wholeDepth&&(d.MipLevels!=1||d.ArraySize!=1||uint64_t(d.Width)*d.Height*bpp>128ull*1024*1024)){event("capture_depth_layout_limit",1);return;}
 unsigned width=(std::min)(d.Width,captureExtent),height=(std::min)(d.Height,captureExtent);
 unsigned x=static_cast<unsigned>(std::clamp(double(captureCenterX)*d.Width-width/2.0,0.0,double(d.Width-width)));
 unsigned y=static_cast<unsigned>(std::clamp(double(captureCenterY)*d.Height-height/2.0,0.0,double(d.Height-height)));
 if(validWidth&&validHeight){
  validWidth=(std::min)(validWidth,d.Width);validHeight=(std::min)(validHeight,d.Height);
  const unsigned cw=(std::min)(s.desc.Width,captureExtent),ch=(std::min)(s.desc.Height,captureExtent);
  const double cx=std::floor(std::clamp(double(captureCenterX)*s.desc.Width-cw/2.0,0.0,double(s.desc.Width-cw)));
  const double cy=std::floor(std::clamp(double(captureCenterY)*s.desc.Height-ch/2.0,0.0,double(s.desc.Height-ch)));
  x=unsigned(cx*validWidth/s.desc.Width);y=unsigned(cy*validHeight/s.desc.Height);
  width=unsigned(std::ceil((cx+cw)*validWidth/s.desc.Width))-x;height=unsigned(std::ceil((cy+ch)*validHeight/s.desc.Height))-y;
 }
 if(fullColour){x=y=0;width=d.Width;height=d.Height;}
 else if(width>512||height>512){event("capture_region_limit",1);return;}
 if(captureFull8&&!DlssNrNative::Full8::FitsBudget(captureBytes,uint64_t(width)*height*bpp)){
  captureError=true;event("capture_full8_byte_limit",1);return;}
 d.Width=wholeDepth?sourceWidth:width;d.Height=wholeDepth?sourceHeight:height;d.MipLevels=1;d.ArraySize=1;d.BindFlags=0;d.MiscFlags=0;d.Usage=D3D11_USAGE_STAGING;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
 ComPtr<ID3D11Texture2D> staging;
 HRESULT hr=s.device->CreateTexture2D(&d,nullptr,&staging);
 if(FAILED(hr)){event("capture_create_failed",hr);return;}
 D3D11_BOX box{x,y,0,x+width,y+height,1};if(wholeDepth)s.context->CopyResource(staging.Get(),texture);else s.context->CopySubresourceRegion(staging.Get(),0,0,0,0,texture,0,&box);
 capturePublish(capture::control::Phase::WaitingGpu);
 D3D11_MAPPED_SUBRESOURCE map{};hr=s.context->Map(staging.Get(),0,D3D11_MAP_READ,0,&map);
 if(FAILED(hr)){event("capture_map_failed",hr);return;}
 if(captureFull8&&(!map.pData||map.RowPitch<uint64_t(wholeDepth?x+width:width)*bpp)){
  s.context->Unmap(staging.Get(),0);captureError=true;event("capture_full8_rowpitch_rejected",1);return;}
 const char* extension=d.Format==DXGI_FORMAT_R16G16B16A16_FLOAT?".rgba16f":d.Format==DXGI_FORMAT_R32G32B32A32_FLOAT?".rgba32f":d.Format==DXGI_FORMAT_R11G11B10_FLOAT?".r11g11b10":(d.Format==DXGI_FORMAT_R32_FLOAT||d.Format==DXGI_FORMAT_R32_TYPELESS)?".r32raw":".raw";
 const auto name="D24Capture_"+std::to_string(captureRun)+"_"+std::to_string(frame)+"_"+stage+extension;
 capturePublish(capture::control::Phase::Writing);
 FILE* file=nullptr;_wfopen_s(&file,(directory()/name).c_str(),L"wb");bool okay=file!=nullptr;
 if(file){for(unsigned row=0;row<height;++row){const auto written=fwrite(static_cast<const unsigned char*>(map.pData)+size_t(row+(wholeDepth?y:0))*map.RowPitch+(wholeDepth?size_t(x)*bpp:0),bpp,width,file);captureBytes+=written*bpp;if(written!=width)okay=false;} if(fclose(file))okay=false;}
 s.context->Unmap(staging.Get(),0);
 if(okay){++captureStages;if(!strcmp(stage,"sr"))captureColourMask|=1;if(!strcmp(stage,"input"))captureColourMask|=2;if(!strcmp(stage,"model"))captureColourMask|=4;if(!strcmp(stage,"composed"))captureColourMask|=8;}
 if(logFile){logPrint(logFile,"{\"event\":\"dx11_crop\",\"frame\":%u,\"run\":%llu,\"stage\":\"%s\",\"file\":\"%s\",\"x\":%u,\"y\":%u,\"width\":%u,\"height\":%u,\"source_width\":%u,\"source_height\":%u,\"valid_width\":%u,\"valid_height\":%u,\"resource\":\"%p\",\"format\":%u,\"row_bytes\":%u,\"ok\":%u,\"tick\":%llu}\n",frame,captureRun,stage,name.c_str(),x,y,width,height,sourceWidth,sourceHeight,validWidth,validHeight,texture,unsigned(d.Format),width*bpp,unsigned(okay),GetTickCount64());fflush(logFile);}
}

static void captureFinish(int result){
 if(!captureActive)return;captureActive=false;
 if(result==1&&captureColourMask==15&&!captureError)++captureComplete;
 if(logFile){logPrint(logFile,"{\"event\":\"dx11_capture_frame_end\",\"run\":%llu,\"frame\":%u,\"request\":%llu,\"call\":%llu,\"result\":%d,\"colour_mask\":%u,\"stages_written\":%u,\"diagnostic_frame_ms\":%llu,\"capture_profile\":\"%s\",\"requested_frames\":%u,\"capture_error\":%u}\n",captureRun,captureFrameId,captureRequest,session().calls,result,captureColourMask,captureStages,GetTickCount64()-captureStart,captureProfile(),captureTarget(),unsigned(captureError));fflush(logFile);}
 const bool bad=result!=1||captureColourMask!=15||captureError;
 const bool expired=captureFull8&&GetTickCount64()-captureSchedule.start>=30000;
 if(captureSchedule.selected>=captureTarget()||bad||expired)captureSchedule.finished=true;
 captureProgress(bad?"failed":expired?"deadline_or_limit":captureSchedule.finished?(captureComplete==captureTarget()?"complete":"failed"):"recording");
}

static void captureContext(Session& s,NVSDK_NGX_Parameter* game,unsigned flags,unsigned frame){
 if(!logFile)return;
 logPrint(logFile,"{\"event\":\"dx11_frame_context\",\"frame\":%u,\"run\":%llu,\"request\":%llu,\"pid\":%lu,\"creation\":\"%llu\",\"call\":%llu,\"tick\":%llu,\"epoch\":%u,\"history_frames\":%u,\"owner\":\"%p\",\"context\":\"%p\",\"flags\":%u,\"mode\":%u,\"auto_mask\":%u,\"linear_resolve\":%u,\"linear_input\":%u,\"use_exposure\":%u,\"game_params\":{",frame,captureRun,captureRequest,GetCurrentProcessId(),captureCreation(),s.calls,captureStart,s.epoch,s.frames,s.owner,s.context.Get(),flags,s.mode,control.autoMask,control.linearResolve,control.linearColorInput,control.useExposure);
 bool first=true;
 for(const char* key:{"MV.Scale.X","MV.Scale.Y","Jitter.Offset.X","Jitter.Offset.Y","DLSS.Pre.Exposure"}){
  float value=0;auto result=game->Get(key,&value);logPrint(logFile,"%s\"%s\":{\"result\":%u,\"value\":",first?"":",",key,unsigned(result));if(std::isfinite(value))logPrint(logFile,"%.9g",value);else logPrint(logFile,"null");logPrint(logFile,"}");first=false;
 }
 for(const char* key:{"Reset","DLSS.Feature.Create.Flags","DLSS.Render.Subrect.Dimensions.Width","DLSS.Render.Subrect.Dimensions.Height"}){unsigned value=0;auto result=game->Get(key,&value);logPrint(logFile,",\"%s\":{\"result\":%u,\"value\":%u}",key,unsigned(result),value);}
 logPrint(logFile,"},\"model_params\":{");first=true;
 for(const char* key:{"DLSSNR.MVecScaleX","DLSSNR.MVecScaleY","DLSSNR.ScalingRatio","DLSSNR.Intensity","DLSSNR.LocalStructureStrength","DLSSNR.LocalToneStrength","DLSSNR.SkinStructureStrength"}){float value=0;auto result=s.parameters.Get(key,&value);logPrint(logFile,"%s\"%s\":{\"result\":%u,\"value\":",first?"":",",key,unsigned(result));if(std::isfinite(value))logPrint(logFile,"%.9g",value);else logPrint(logFile,"null");logPrint(logFile,"}");first=false;}
 for(const char* key:{"DLSSNR.Reset","DLSSNR.DepthInverted","DLSSNR.DepthSubrectWidth","DLSSNR.DepthSubrectHeight","DLSSNR.MVecSubrectWidth","DLSSNR.MVecSubrectHeight","DLSSNR.UseAutoMask","DLSSNR.UICorrection"}){unsigned value=0;auto result=s.parameters.Get(key,&value);logPrint(logFile,",\"%s\":{\"result\":%u,\"value\":%u}",key,unsigned(result),value);}
 logPrint(logFile,"},\"jitter_correction\":{\"active\":%u,\"applied\":%u,\"reset\":%u,\"raw_offset_x\":%.9g,\"raw_offset_y\":%.9g},\"flags_scope\":\"D24Process_argument\",\"capture_schema\":4,\"capture_profile_schema\":\"d18-capture-profile-v1\",\"capture_profile\":\"%s\",\"requested_frames\":%u,\"capture_scope\":\"%s\",\"capture_target_width\":%u,\"capture_target_height\":%u}\n",unsigned(s.jitterPlan.active),unsigned(s.jitterPlan.apply),unsigned(s.jitterPlan.reset),s.jitterPlan.dx,s.jitterPlan.dy,captureProfile(),captureTarget(),captureScope(),s.desc.Width,s.desc.Height);fflush(logFile);
}

static void captureContract(Session& s,NVSDK_NGX_Parameter* game,unsigned flags,unsigned frame){
 captureFrameId=frame;
 if(logFile)logPrint(logFile,"{\"event\":\"dx11_capture_contract\",\"run\":%llu,\"frame\":%u,\"passthrough\":%u,\"ratio\":%.9g,\"white_point_scale\":%.9g,\"exposure_used\":%.9g,\"pre_exposure_used\":%.9g,\"transfer_strength\":%.9g,\"colour_strength\":%.9g,\"intensity\":%.9g,\"local_structure\":%.9g,\"local_tone\":%.9g,\"skin_structure\":%.9g,\"style\":%u,\"preset\":%u,\"custom_filter\":%u,\"transfer\":%u,\"debug_view\":%u,\"compare\":%u}\n",captureRun,frame,unsigned(!(flags&NVSDK_NGX_DLSS_Feature_Flags_IsHDR)),control.networkRatio,control.whitePoint,s.exposure,s.exposurePre,control.transferStrength,control.colourStrength,control.intensity,control.localStructure,control.localTone,control.skinStructure,control.style,control.preset,control.customFilter,control.transfer,control.debugView,control.compare);
 ID3D11Resource* raw=nullptr;game->Get(NVSDK_NGX_Parameter_ExposureTexture,&raw);ComPtr<ID3D11Texture2D> texture;
 if(raw&&SUCCEEDED(raw->QueryInterface(IID_PPV_ARGS(&texture)))){
  D3D11_TEXTURE2D_DESC d{};texture->GetDesc(&d);
  if(d.Width<=16&&d.Height<=16)captureCrop(s,texture.Get(),frame,"exposure");
 }
}


