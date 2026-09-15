#pragma once
#include "CaptureSubmission.h"
#include "CaptureWrite.h"
#include "CaptureEvidence.h"
#include <algorithm>
#include <array>

namespace capture {
// Explicit two-pass diagnostic, never changes the renderer's requested/applied mode.
// All methods except submitted() belong to the NR render lock. A missing submission
// retains the bounded allocation; elapsed time is never completion evidence.
class LayerCapture {
public:
    static constexpr unsigned Frames=8, Stages=4, Regions=5, Side=128;
    struct Metadata {
        uint64_t frame=0, source=0, attempt=0;
        unsigned resetMask=0;
        bool shared=false;
        bool exposureTexture=false,depthInverted=false;
        unsigned depthWidth=0,depthHeight=0,depthFormat=0,motionWidth=0,motionHeight=0,motionFormat=0;
        FrameEvidence evidence{};
        float ratio[2]{1,1}, intensity[2]{1,1}, mvScale[2]{};
        float structure[2]{1,1}, tone[2]{1,1}, skin[2]{-1,-1};
        unsigned style[2]{}, preset[2]{}, autoMask[2]{};
    };
private:
    struct Tile { D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{}; unsigned x=0,y=0; };
    std::array<std::array<std::array<Tile,Regions>,Stages>,Frames> tiles_{};
    std::array<D3D12_RESOURCE_DESC,Stages> desc_{};
    std::array<Metadata,Frames> metadata_{};
    std::array<unsigned,Frames> masks_{};
    SubmissionBatch submissions_;
    ID3D12Resource* readback_=nullptr;
    UINT64 bytes_=0, started_=0;
    unsigned wanted_=0,count_=0,runs_=0;
    bool active_=false,inFrame_=false;
    const char* reason_="";
    static unsigned pixelBytes(DXGI_FORMAT f) {
        return f==DXGI_FORMAT_R16G16B16A16_FLOAT?8:f==DXGI_FORMAT_R32G32B32A32_FLOAT?16:0;
    }
    static bool same(const D3D12_RESOURCE_DESC& a,const D3D12_RESOURCE_DESC& b) {
        return a.Dimension==b.Dimension && a.Width==b.Width && a.Height==b.Height &&
            a.Format==b.Format && a.DepthOrArraySize==b.DepthOrArraySize &&
            a.MipLevels==b.MipLevels && a.SampleDesc.Count==b.SampleDesc.Count;
    }
public:
    bool pending() const {return readback_!=nullptr;}
    bool active() const {return active_;}
    bool inFrame() const {return inFrame_;}
    const char* reason() const {return reason_;}
    UINT64 bytes() const {return bytes_;}
    void stop(const char* why) { if(active_){active_=false;reason_=why;} }
    void expire() {if(active_ && GetTickCount64()-started_>10000)stop("recording_timeout");}
    bool prepare(ID3D12Device* device,const std::array<ID3D12Resource*,Stages>& sources,unsigned frames) {
        if(pending())return false;
        if(runs_>=3){reason_="session_capture_limit";return false;}
        ++runs_; wanted_=std::clamp(frames,1u,Frames);count_=0;bytes_=0;masks_={};inFrame_=false;
        for(unsigned s=0;s<Stages;++s){
            if(!sources[s]){reason_="missing_resource";return false;}
            desc_[s]=sources[s]->GetDesc();const auto& d=desc_[s];
            if(d.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D || !pixelBytes(d.Format) ||
               d.Width<Side || d.Height<Side || d.Width>32768 || d.Height>32768 ||
               d.MipLevels!=1 || d.DepthOrArraySize!=1 || d.SampleDesc.Count!=1 ||
               d.Width!=desc_[0].Width || d.Height!=desc_[0].Height){reason_="unsupported_contract";return false;}
        }
        for(unsigned f=0;f<wanted_;++f)for(unsigned s=0;s<Stages;++s)for(unsigned r=0;r<Regions;++r){
            auto& t=tiles_[f][s][r];const auto& d=desc_[s];
            const unsigned w=unsigned(d.Width),h=d.Height;
            const unsigned cx=r==0?w/2:(r%2?w/4:3*w/4);
            const unsigned cy=r==0?h/2:(r<=2?h/4:3*h/4);
            t.x=std::min(w-Side,cx>Side/2?cx-Side/2:0u);
            t.y=std::min(h-Side,cy>Side/2?cy-Side/2:0u);
            auto& p=t.footprint;p.Offset=(bytes_+511)&~UINT64(511);
            p.Footprint={d.Format,Side,Side,1,(Side*pixelBytes(d.Format)+255)&~255u};
            bytes_=p.Offset+UINT64(p.Footprint.RowPitch)*Side;
        }
        if(bytes_>128ull*1024*1024){reason_="memory_limit";return false;}
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=bytes_;
        d.Height=1;d.DepthOrArraySize=1;d.MipLevels=1;d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if(FAILED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,
            D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback_)))){reason_="allocation_failed";return false;}
        reason_="recording";active_=true;started_=GetTickCount64();return true;
    }
    bool begin(ID3D12Device* device,ID3D12GraphicsCommandList* list,const Metadata& m) {
        if(!active_ || inFrame_ || count_>=wanted_)return false;
        if(count_ && (m.source!=metadata_[0].source || m.shared!=metadata_[0].shared ||
            m.attempt!=metadata_[count_-1].attempt+1)) {stop("source_mode_or_recording_gap");return false;}
        if(!submissions_.arm(device,list)){stop("submission_arm_failed");return false;}
        metadata_[count_]=m;inFrame_=true;return true;
    }
    void copy(ID3D12GraphicsCommandList* list,unsigned stage,ID3D12Resource* source,D3D12_RESOURCE_STATES state) {
        if(!inFrame_ || !active_ || stage>=Stages)return;
        if(!source || !same(source->GetDesc(),desc_[stage])){stop("resource_contract_changed");return;}
        D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition={source,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,state,D3D12_RESOURCE_STATE_COPY_SOURCE};
        if(state!=D3D12_RESOURCE_STATE_COPY_SOURCE)list->ResourceBarrier(1,&b);
        D3D12_TEXTURE_COPY_LOCATION src{};src.pResource=source;src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        for(const auto& t:tiles_[count_][stage]){
            D3D12_TEXTURE_COPY_LOCATION dst{};dst.pResource=readback_;dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            dst.PlacedFootprint=t.footprint;D3D12_BOX box{t.x,t.y,0,t.x+Side,t.y+Side,1};
            list->CopyTextureRegion(&dst,0,0,0,&src,&box);
        }
        std::swap(b.Transition.StateBefore,b.Transition.StateAfter);
        if(state!=D3D12_RESOURCE_STATE_COPY_SOURCE)list->ResourceBarrier(1,&b);
        masks_[count_]|=1u<<stage;
    }
    void resolve(const DlssNrConstants& c) {if(inFrame_)metadata_[count_].evidence.resolve=c;}
    void end() {
        if(!inFrame_)return;
        if(masks_[count_]!=15)stop("missing_stage");
        ++count_;inFrame_=false;
        if(active_ && count_>=wanted_){active_=false;reason_="complete";}
    }
    void submitted(ID3D12CommandQueue* q,UINT n,ID3D12CommandList* const* lists) {submissions_.submitted(q,n,lists);}
    bool completed() {return pending() && !active_ && !inFrame_ && (count_==0 || submissions_.complete());}
    // Never use this to abandon a submitted resource. Even incomplete captures wait for completion.
    bool releaseCompleted() {
        if(!completed())return false;
        readback_->Release();readback_=nullptr;submissions_.clearCompleted();return true;
    }
    WriteResult write(const std::filesystem::path& root,const FileIo& io={}) {
        if(!completed())return {};
        const auto directory=root/(std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(started_)+L"-"+std::to_wstring(runs_));
        const auto fail=[&](const char* why){releaseCompleted();return WriteResult{WriteResult::State::Failed,directory.string(),why};};
        std::error_code ec;
        if(!std::filesystem::create_directories(directory,ec) || ec)return fail("directory_create");
        void* mapped=nullptr;const D3D12_RANGE range{0,SIZE_T(bytes_)};
        if(FAILED(readback_->Map(0,&range,&mapped)))return fail("map_failed");
        bool ok=true;
        for(unsigned f=0;f<count_&&ok;++f)for(unsigned s=0;s<Stages&&ok;++s)if(masks_[f]&(1u<<s))
            for(unsigned r=0;r<Regions&&ok;++r){
                char name[64];std::snprintf(name,sizeof(name),"f%02u_s%u_r%u.raw",f,s,r);
                const auto& p=tiles_[f][s][r].footprint;const size_t row=Side*pixelBytes(p.Footprint.Format);
                ok=WriteChecked(directory/name,[&](std::FILE* file){
                    for(unsigned y=0;y<Side;++y)if(io.write(static_cast<const char*>(mapped)+p.Offset+size_t(y)*p.Footprint.RowPitch,1,row,file)!=row)return false;
                    return true;
                },io);
            }
        D3D12_RANGE none{0,0};readback_->Unmap(0,&none);
        if(!ok)return fail("pixel_write_failed");
        ok=WriteChecked(directory/"manifest.pending",[&](std::FILE* file){
            std::fprintf(file,"{\"schema\":\"d18-layer-capture-v1\",\"api\":\"d3d12\",\"candidate\":\"C7\",\"gpu_complete\":true,\"complete\":%s,\"reason\":\"%s\",\"requested_frames\":%u,\"frames\":%u,\"readback_bytes\":%llu,\"guides_captured\":false,\"stages\":[\"sr_base\",\"pass1\",\"pass2\",\"final\"],\"domains\":[\"game\",\"encoded_proxy\",\"encoded_proxy\",\"game\"],\"regions\":[",
                count_==wanted_ && std::string(reason_)=="complete"?"true":"false",reason_,wanted_,count_,bytes_);
            for(unsigned r=0;r<Regions;++r){const auto& t=tiles_[0][0][r];std::fprintf(file,"%s[%u,%u,%u,%u]",r?",":"",t.x,t.y,Side,Side);}
            std::fprintf(file,"],\"resources\":[");
            for(unsigned s=0;s<Stages;++s)std::fprintf(file,"%s{\"format\":%u,\"width\":%llu,\"height\":%u}",s?",":"",unsigned(desc_[s].Format),desc_[s].Width,desc_[s].Height);
            std::fprintf(file,"],\"samples\":[");
            for(unsigned f=0;f<count_;++f){const auto& m=metadata_[f];const auto& e=m.evidence;
                std::fprintf(file,"%s{\"frame\":%llu,\"source\":%llu,\"attempt\":%llu,\"stage_mask\":%u,\"shared\":%s,\"reset_mask\":%u,\"pre_exposure\":%.9g,\"rr\":%s,\"network\":[%u,%u],\"ratio\":[%.9g,%.9g],\"intensity\":[%.9g,%.9g],\"structure\":[%.9g,%.9g],\"tone\":[%.9g,%.9g],\"skin\":[%.9g,%.9g],\"style\":[%u,%u],\"preset\":[%u,%u],\"auto_mask\":[%u,%u],\"mv_scale\":[%.9g,%.9g],\"later_pass_zero_mv\":%s,",
                    f?",":"",m.frame,m.source,m.attempt,masks_[f],m.shared?"true":"false",m.resetMask,e.preExposure,e.rr?"true":"false",e.networkWidth,e.networkHeight,m.ratio[0],m.ratio[1],m.intensity[0],m.intensity[1],m.structure[0],m.structure[1],m.tone[0],m.tone[1],m.skin[0],m.skin[1],m.style[0],m.style[1],m.preset[0],m.preset[1],m.autoMask[0],m.autoMask[1],m.mvScale[0],m.mvScale[1],m.shared?"true":"false");
                const auto rect=[&](const char* key,const DlssNrAbi::Rect& r){std::fprintf(file,"\"%s\":[%u,%u,%u,%u],",key,r.x,r.y,r.width,r.height);};
                std::fprintf(file,"\"exposure_texture_present\":%s,\"depth_inverted\":%s,\"depth_resource\":[%u,%u,%u],\"motion_resource\":[%u,%u,%u],",m.exposureTexture?"true":"false",m.depthInverted?"true":"false",m.depthWidth,m.depthHeight,m.depthFormat,m.motionWidth,m.motionHeight,m.motionFormat);
                rect("output_rect",e.rects.output);rect("depth_rect",e.rects.depth);rect("motion_rect",e.rects.motion);
                std::fprintf(file,"\"resolve_constants_hex\":\"");
                const auto* p=reinterpret_cast<const unsigned char*>(&e.resolve);
                for(size_t i=0;i<offsetof(DlssNrConstants,RelativeColour)+sizeof(e.resolve.RelativeColour);++i)std::fprintf(file,"%02x",p[i]);
                std::fprintf(file,"\"}");
            }
            std::fprintf(file,"]}\n");return true;
        },io);
        if(!ok)return fail("manifest_write_failed");
        std::filesystem::rename(directory/"manifest.pending",directory/"manifest.json",ec);
        if(ec)return fail("manifest_commit_failed");
        releaseCompleted();return {WriteResult::State::Success,directory.string(),reason_};
    }
};
}
