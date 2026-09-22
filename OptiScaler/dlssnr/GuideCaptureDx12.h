#pragma once
#include "GuideCapture.h"
#include "CaptureWrite.h"
#include <d3d12.h>
#include <array>
#include <filesystem>

namespace capture {
struct GuideDx12 {
    ID3D12Resource* readback=nullptr;D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout{};uint64_t bytes=0;
    GuideEvidence evidence;bool whole=false;
    // Release only through the owner's completion-proven cleanup, including partial batches.
    ~GuideDx12()=default;
    GuideDx12()=default;GuideDx12(const GuideDx12&)=delete;GuideDx12& operator=(const GuideDx12&)=delete;
    void Release(){if(readback)readback->Release();readback=nullptr;bytes=0;evidence={};}
    bool Record(ID3D12Device* device,ID3D12GraphicsCommandList* cmd,ID3D12Resource* source,
                D3D12_RESOURCE_STATES state,GuideRegion valid,uint32_t outputW,uint32_t outputH,
                const char* role,unsigned frameIndex,float sx,float sy,uint64_t& budget){
        evidence.role=role;evidence.api="DXGI";evidence.scaleX=sx;evidence.scaleY=sy;
        evidence.units=evidence.role.find("motion")==0?"raw_mv_times_scale_to_pixels":"device_depth";
        if(readback){evidence.reason="slot_already_recorded";return false;}
        if(!source){evidence.reason="resource_missing";return false;}
        const auto d=source->GetDesc();evidence.sourceFormat=unsigned(d.Format);evidence.sourceWidth=uint32_t(d.Width);evidence.sourceHeight=d.Height;
        evidence.valid=valid;evidence.roi=GuideCrop(valid,outputW,outputH);
        if(d.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||d.DepthOrArraySize!=1||d.MipLevels!=1||d.SampleDesc.Count!=1){evidence.reason="unsupported_layout_or_msaa";return false;}
        if(!valid.width||!valid.height||uint64_t(valid.x)+valid.width>d.Width||uint64_t(valid.y)+valid.height>d.Height){evidence.reason="invalid_valid_rect";return false;}
        switch(d.Format){
        case DXGI_FORMAT_R32_TYPELESS:case DXGI_FORMAT_D32_FLOAT:case DXGI_FORMAT_R32_FLOAT:evidence.texelBytes=4;evidence.channels=1;break;
        case DXGI_FORMAT_R16_TYPELESS:case DXGI_FORMAT_D16_UNORM:case DXGI_FORMAT_R16_UNORM:case DXGI_FORMAT_R16_FLOAT:evidence.texelBytes=2;evidence.channels=1;break;
        case DXGI_FORMAT_R24G8_TYPELESS:case DXGI_FORMAT_D24_UNORM_S8_UINT:case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:evidence.texelBytes=4;evidence.channels=1;break;
        case DXGI_FORMAT_R32G8X24_TYPELESS:case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:evidence.texelBytes=4;evidence.channels=1;break;
        case DXGI_FORMAT_R16G16_TYPELESS:case DXGI_FORMAT_R16G16_FLOAT:evidence.texelBytes=4;evidence.channels=2;break;
        case DXGI_FORMAT_R32G32_TYPELESS:case DXGI_FORMAT_R32G32_FLOAT:evidence.texelBytes=8;evidence.channels=2;break;
        default:evidence.reason="unsupported_format";return false;
        }
        // Depth/stencil must copy the complete subresource; crop only on the CPU.
        whole=(d.Flags&D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)!=0 || d.Format==DXGI_FORMAT_R24G8_TYPELESS || d.Format==DXGI_FORMAT_R24_UNORM_X8_TYPELESS || d.Format==DXGI_FORMAT_R32G8X24_TYPELESS || d.Format==DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
        auto footprint=d;
        if(!whole){footprint.Width=evidence.roi.width;footprint.Height=evidence.roi.height;}
        device->GetCopyableFootprints(&footprint,0,1,0,&layout,nullptr,nullptr,&bytes);
        evidence.storageFormat=unsigned(layout.Footprint.Format);
        // Query the native footprint; never invent a depth-plane format or stride.
        if(!bytes||bytes>128ull*1024*1024||budget+bytes>512ull*1024*1024){evidence.reason="guide_readback_budget";bytes=0;return false;}
        if(layout.Footprint.RowPitch<uint64_t(layout.Footprint.Width)*evidence.texelBytes){evidence.reason="unexpected_plane_stride";bytes=0;return false;}
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_READBACK;D3D12_RESOURCE_DESC b{};b.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;b.Width=bytes;b.Height=1;b.DepthOrArraySize=1;b.MipLevels=1;b.SampleDesc.Count=1;b.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if(FAILED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&b,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback)))){evidence.reason="readback_allocation_failed";bytes=0;return false;}
        budget+=bytes;D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;barrier.Transition={source,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,state,D3D12_RESOURCE_STATE_COPY_SOURCE};
        if(state!=D3D12_RESOURCE_STATE_COPY_SOURCE)cmd->ResourceBarrier(1,&barrier);
        D3D12_TEXTURE_COPY_LOCATION from{};from.pResource=source;from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION to{};to.pResource=readback;to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=layout;
        auto r=evidence.roi;D3D12_BOX box{r.x,r.y,0,r.x+r.width,r.y+r.height,1};cmd->CopyTextureRegion(&to,0,0,0,&from,whole?nullptr:&box);
        if(state!=D3D12_RESOURCE_STATE_COPY_SOURCE){std::swap(barrier.Transition.StateBefore,barrier.Transition.StateAfter);cmd->ResourceBarrier(1,&barrier);}
        char name[96];std::snprintf(name,sizeof(name),"guide_%s_%02u.raw",role,frameIndex);evidence.file=name;evidence.recorded=true;evidence.reason="";return true;
    }
    bool Write(const std::filesystem::path& directory,const FileIo& io){
        if(!evidence.recorded||!evidence.aliasOf.empty())return true;
        void* mapped=nullptr;D3D12_RANGE range{0,size_t(bytes)};if(FAILED(readback->Map(0,&range,&mapped))||!mapped)return false;
        auto roi=evidence.roi;const auto* start=static_cast<const char*>(mapped)+layout.Offset+(whole?size_t(roi.y)*layout.Footprint.RowPitch+size_t(roi.x)*evidence.texelBytes:0);
        const size_t row=size_t(roi.width)*evidence.texelBytes;
        bool ok=WriteChecked(directory/evidence.file,[&](FILE* f){for(unsigned y=0;y<roi.height;++y)if(io.write(start+size_t(y)*layout.Footprint.RowPitch,1,row,f)!=row)return false;return true;},io);
        D3D12_RANGE empty{0,0};readback->Unmap(0,&empty);return ok;
    }
};
}
