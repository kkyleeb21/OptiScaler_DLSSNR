// Bounded opt-in crops. Map synchronizes only diagnostic frames.
#include "capture_schedule.h"
static CaptureSchedule captureSchedule;
static uint64_t captureRun=0,captureStart=0;
static unsigned captureColourMask=0,captureStages=0,captureComplete=0;
static bool captureActive=false;
static void captureProgress(const char* state){
 if(logFile){logPrint(logFile,"{\"event\":\"dx11_capture_progress\",\"run\":%llu,\"state\":\"%s\",\"selected\":%u,\"complete_colour_frames\":%u,\"limit\":32,\"elapsed_ms\":%llu}\n",captureRun,state,captureSchedule.selected,captureComplete,GetTickCount64()-captureSchedule.start);fflush(logFile);}
 FILE* status=nullptr;_wfopen_s(&status,(directory()/L"D24CaptureStatus.txt").c_str(),L"w");
 if(status){fprintf(status,"DX11 capture %s\nSelected %u / 32; complete colour frames %u\nElapsed %llu ms. Four bursts of 8; target spans 4.5 seconds, deadline 15 seconds.\nUncheck then check to start a new capture. Readback can slow sampled frames.\n",state,captureSchedule.selected,captureComplete,GetTickCount64()-captureSchedule.start);fclose(status);}
}
static void captureDisarm(){if(captureSchedule.armed&&!captureSchedule.finished)captureProgress("stopped");captureSchedule.reset();}
static float captureCenterX=0.5f,captureCenterY=0.5f;
static unsigned captureExtent=512;
static bool captureFrame(){
 static const bool enabled=GetFileAttributesW((directory()/L"D24Dx11Diagnostics.enabled").c_str())!=INVALID_FILE_ATTRIBUTES;
 const bool armed=managed?control.capture!=0:enabled;
 if(!armed){captureDisarm();return false;}
 const bool starting=!captureSchedule.armed,wasFinished=captureSchedule.finished;
 const bool selected=captureSchedule.select(true,GetTickCount64());
 if(starting){FILETIME ft{};GetSystemTimeAsFileTime(&ft);captureRun=(uint64_t(ft.dwHighDateTime)<<32)|ft.dwLowDateTime;captureComplete=0;captureProgress("recording");}
 if(!selected){if(!wasFinished&&captureSchedule.finished)captureProgress("deadline_or_limit");return false;}
 captureStart=GetTickCount64();captureColourMask=captureStages=0;captureActive=true;
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
 const unsigned bpp=d.Format==DXGI_FORMAT_R16G16B16A16_FLOAT?8u:d.Format==DXGI_FORMAT_R32G32B32A32_FLOAT?16u:
                    (d.Format==DXGI_FORMAT_R32G32_FLOAT||d.Format==DXGI_FORMAT_R32G32_TYPELESS||d.Format==DXGI_FORMAT_R32G8X24_TYPELESS)?8u:
                    (d.Format==DXGI_FORMAT_R16_UNORM||d.Format==DXGI_FORMAT_R16_TYPELESS)?2u:
                    (d.Format==DXGI_FORMAT_R11G11B10_FLOAT||d.Format==DXGI_FORMAT_R32_FLOAT||d.Format==DXGI_FORMAT_R32_TYPELESS||d.Format==DXGI_FORMAT_R24G8_TYPELESS||d.Format==DXGI_FORMAT_R16G16_TYPELESS||d.Format==DXGI_FORMAT_R16G16_FLOAT)?4u:0u;
 if(!bpp||d.SampleDesc.Count!=1){event("capture_unsupported_format",d.Format);return;}
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
 if(width>512||height>512){event("capture_region_limit",1);return;}
 d.Width=wholeDepth?sourceWidth:width;d.Height=wholeDepth?sourceHeight:height;d.MipLevels=1;d.ArraySize=1;d.BindFlags=0;d.MiscFlags=0;d.Usage=D3D11_USAGE_STAGING;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
 ComPtr<ID3D11Texture2D> staging;
 HRESULT hr=s.device->CreateTexture2D(&d,nullptr,&staging);
 if(FAILED(hr)){event("capture_create_failed",hr);return;}
 D3D11_BOX box{x,y,0,x+width,y+height,1};if(wholeDepth)s.context->CopyResource(staging.Get(),texture);else s.context->CopySubresourceRegion(staging.Get(),0,0,0,0,texture,0,&box);
 D3D11_MAPPED_SUBRESOURCE map{};hr=s.context->Map(staging.Get(),0,D3D11_MAP_READ,0,&map);
 if(FAILED(hr)){event("capture_map_failed",hr);return;}
 const char* extension=d.Format==DXGI_FORMAT_R16G16B16A16_FLOAT?".rgba16f":d.Format==DXGI_FORMAT_R32G32B32A32_FLOAT?".rgba32f":d.Format==DXGI_FORMAT_R11G11B10_FLOAT?".r11g11b10":(d.Format==DXGI_FORMAT_R32_FLOAT||d.Format==DXGI_FORMAT_R32_TYPELESS)?".r32raw":".raw";
 const auto name="D24Capture_"+std::to_string(captureRun)+"_"+std::to_string(frame)+"_"+stage+extension;
 FILE* file=nullptr;_wfopen_s(&file,(directory()/name).c_str(),L"wb");bool okay=file!=nullptr;
 if(file){for(unsigned row=0;row<height;++row)if(fwrite(static_cast<const unsigned char*>(map.pData)+size_t(row+(wholeDepth?y:0))*map.RowPitch+(wholeDepth?size_t(x)*bpp:0),bpp,width,file)!=width)okay=false; if(fclose(file))okay=false;}
 s.context->Unmap(staging.Get(),0);
 if(okay){++captureStages;if(!strcmp(stage,"sr"))captureColourMask|=1;if(!strcmp(stage,"input"))captureColourMask|=2;if(!strcmp(stage,"model"))captureColourMask|=4;if(!strcmp(stage,"composed"))captureColourMask|=8;}
 if(logFile){logPrint(logFile,"{\"event\":\"dx11_crop\",\"frame\":%u,\"run\":%llu,\"stage\":\"%s\",\"file\":\"%s\",\"x\":%u,\"y\":%u,\"width\":%u,\"height\":%u,\"source_width\":%u,\"source_height\":%u,\"valid_width\":%u,\"valid_height\":%u,\"resource\":\"%p\",\"format\":%u,\"row_bytes\":%u,\"ok\":%u,\"tick\":%llu}\n",frame,captureRun,stage,name.c_str(),x,y,width,height,sourceWidth,sourceHeight,validWidth,validHeight,texture,unsigned(d.Format),width*bpp,unsigned(okay),GetTickCount64());fflush(logFile);}
}

static void captureFinish(int result){
 if(!captureActive)return;captureActive=false;
 if(result==1&&captureColourMask==15)++captureComplete;
 if(logFile){logPrint(logFile,"{\"event\":\"dx11_capture_frame_end\",\"run\":%llu,\"call\":%llu,\"result\":%d,\"colour_mask\":%u,\"stages_written\":%u,\"diagnostic_frame_ms\":%llu}\n",captureRun,session().calls,result,captureColourMask,captureStages,GetTickCount64()-captureStart);fflush(logFile);}
 if(captureSchedule.selected>=32||result<0)captureSchedule.finished=true;
 captureProgress(result<0?"failed":captureSchedule.finished?"complete":"recording");
}

static void captureContext(Session& s,NVSDK_NGX_Parameter* game,unsigned flags,unsigned frame){
 if(!logFile)return;
 logPrint(logFile,"{\"event\":\"dx11_frame_context\",\"frame\":%u,\"run\":%llu,\"call\":%llu,\"tick\":%llu,\"epoch\":%u,\"history_frames\":%u,\"owner\":\"%p\",\"context\":\"%p\",\"flags\":%u,\"mode\":%u,\"auto_mask\":%u,\"linear_resolve\":%u,\"linear_input\":%u,\"use_exposure\":%u,\"game_params\":{",frame,captureRun,s.calls,captureStart,s.epoch,s.frames,s.owner,s.context.Get(),flags,s.mode,control.autoMask,control.linearResolve,control.linearColorInput,control.useExposure);
 bool first=true;
 for(const char* key:{"MV.Scale.X","MV.Scale.Y","Jitter.Offset.X","Jitter.Offset.Y","DLSS.Pre.Exposure"}){
  float value=0;auto result=game->Get(key,&value);logPrint(logFile,"%s\"%s\":{\"result\":%u,\"value\":",first?"":",",key,unsigned(result));if(std::isfinite(value))logPrint(logFile,"%.9g",value);else logPrint(logFile,"null");logPrint(logFile,"}");first=false;
 }
 for(const char* key:{"Reset","DLSS.Feature.Create.Flags","DLSS.Render.Subrect.Dimensions.Width","DLSS.Render.Subrect.Dimensions.Height"}){unsigned value=0;auto result=game->Get(key,&value);logPrint(logFile,",\"%s\":{\"result\":%u,\"value\":%u}",key,unsigned(result),value);}
 logPrint(logFile,"},\"model_params\":{");first=true;
 for(const char* key:{"DLSSNR.MVecScaleX","DLSSNR.MVecScaleY","DLSSNR.ScalingRatio","DLSSNR.Intensity","DLSSNR.LocalStructureStrength","DLSSNR.LocalToneStrength","DLSSNR.SkinStructureStrength"}){float value=0;auto result=s.parameters.Get(key,&value);logPrint(logFile,"%s\"%s\":{\"result\":%u,\"value\":",first?"":",",key,unsigned(result));if(std::isfinite(value))logPrint(logFile,"%.9g",value);else logPrint(logFile,"null");logPrint(logFile,"}");first=false;}
 for(const char* key:{"DLSSNR.Reset","DLSSNR.DepthInverted","DLSSNR.DepthSubrectWidth","DLSSNR.DepthSubrectHeight","DLSSNR.MVecSubrectWidth","DLSSNR.MVecSubrectHeight","DLSSNR.UseAutoMask","DLSSNR.UICorrection"}){unsigned value=0;auto result=s.parameters.Get(key,&value);logPrint(logFile,",\"%s\":{\"result\":%u,\"value\":%u}",key,unsigned(result),value);}
 logPrint(logFile,"},\"jitter_correction\":{\"active\":%u,\"applied\":%u,\"reset\":%u,\"raw_offset_x\":%.9g,\"raw_offset_y\":%.9g},\"flags_scope\":\"D24Process_argument\",\"capture_schema\":3}\n",unsigned(s.jitterPlan.active),unsigned(s.jitterPlan.apply),unsigned(s.jitterPlan.reset),s.jitterPlan.dx,s.jitterPlan.dy);fflush(logFile);
}

static void captureContract(Session& s,NVSDK_NGX_Parameter* game,unsigned flags,unsigned frame){
 if(logFile)logPrint(logFile,"{\"event\":\"dx11_capture_contract\",\"frame\":%u,\"passthrough\":%u,\"ratio\":%.9g,\"white_point_scale\":%.9g,\"exposure_used\":%.9g,\"pre_exposure_used\":%.9g,\"transfer_strength\":%.9g,\"colour_strength\":%.9g,\"intensity\":%.9g,\"local_structure\":%.9g,\"local_tone\":%.9g,\"skin_structure\":%.9g,\"style\":%u,\"preset\":%u,\"custom_filter\":%u,\"transfer\":%u,\"debug_view\":%u,\"compare\":%u}\n",frame,unsigned(!(flags&NVSDK_NGX_DLSS_Feature_Flags_IsHDR)),control.networkRatio,control.whitePoint,s.exposure,s.exposurePre,control.transferStrength,control.colourStrength,control.intensity,control.localStructure,control.localTone,control.skinStructure,control.style,control.preset,control.customFilter,control.transfer,control.debugView,control.compare);
 ID3D11Resource* raw=nullptr;game->Get(NVSDK_NGX_Parameter_ExposureTexture,&raw);ComPtr<ID3D11Texture2D> texture;
 if(raw&&SUCCEEDED(raw->QueryInterface(IID_PPV_ARGS(&texture)))){
  D3D11_TEXTURE2D_DESC d{};texture->GetDesc(&d);
  if(d.Width<=16&&d.Height<=16)captureCrop(s,texture.Get(),frame,"exposure");
 }
}


