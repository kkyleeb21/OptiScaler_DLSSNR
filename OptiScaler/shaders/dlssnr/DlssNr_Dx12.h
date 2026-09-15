#pragma once

// The composition pass for Neural Rendering.
//
// Neural Rendering is two things, and only one of them is a shader. The model is an NGX feature --
// created and evaluated, not dispatched -- and that stays where it is. This is the other half: the
// pass that builds the tone-mapped proxy the model is shown, and then transfers the model's answer
// back onto the real frame.
//
// It is an ordinary compute shader with a constant struct, so it belongs here alongside RCAS and
// Output Scaling rather than owning a bespoke root signature and descriptor ring of its own.
//
// One shader, three modes, because all three read and write the same set of resources and differ
// only in what they compute:
//
//   Encode   the frame -> a tone-mapped proxy, plus an untouched copy to transfer against later
//   Down     the proxy -> a smaller proxy, when the model is asked to work below full resolution
//   Resolve  proxy + model answer + untouched copy -> the frame, edited

#include "DlssNr_Common.h"
#include <dlssnr/DlssNrFeature_Dx12.h>

#include <d3d12.h>
#include <d3dx/d3dx12.h>
#include <shaders/Shader_Dx12.h>
#include <shaders/Shader_Dx12Utils.h>

// Four-pass shared history needs at most 12 dispatch slots per frame. A bounded
// 64-slot ring accommodates five such recordings, while actual submission
// fences still gate every reuse and full-frame admission. No frame-age reuse.
#include <dlssnr/MultipassPolicy.h>
#include <dlssnr/MultipassFormats.h>
#define DLSSNR_NUM_OF_HEAPS DlssNr::Multipass::DescriptorSlots
#include <dlssnr/Submission.h>

class DlssNr_Dx12 : public Shader_Dx12, public DlssNr_Common
{
  private:
    FrameDescriptorHeap _frameHeaps[DLSSNR_NUM_OF_HEAPS];
    ID3D12PipelineState* _hybridPipelineState = nullptr;
    ID3D12PipelineState* _multipassPipelineState = nullptr;
    ID3D12PipelineState* _highResolutionPipelineState = nullptr;
    bool _hybridPipelineAttempted = false;
    bool _submissionObserverReady = false;
    DXGI_FORMAT _multipassCheckedFormat=DXGI_FORMAT_UNKNOWN;
    bool _multipassFormatSupported=false;

    // One constant buffer per heap, not one for the class.
    //
    // The shared buffer in the base class suits a shader that dispatches once a frame. Three
    // dispatches recorded onto one command list all map and overwrite the same upload buffer before
    // any of them executes, so every pass ends up reading whichever constants were written last --
    // encode and downsample would run with the resolve's parameters.
    ID3D12Resource* _constantBuffers[DLSSNR_NUM_OF_HEAPS] = {};

    uint32_t _heapIndex = 0;
    DlssNr::Submission::Token _heapCompletion[DLSSNR_NUM_OF_HEAPS];

    // The shader reads five inputs and writes two, and not every mode uses all of them. Unused slots
    // still need a view bound -- an unbound descriptor is not an empty read, it is a read from
    // nothing -- so a stand-in is written into whichever are spare.
    static constexpr uint32_t kSrvCount = 5;
    static constexpr uint32_t kUavCount = 2;

    uint32_t _numThreadsX = 8;
    uint32_t _numThreadsY = 8;

  public:
    DlssNr_Dx12(std::string InName, ID3D12Device* InDevice);
    ~DlssNr_Dx12();

    // The pass. Resources in, and nothing read from anywhere the caller cannot see.
    //
    // This is the whole filter: it brings the model up if it is not already, builds the feature and
    // rebuilds it when the tuning or the resolution changes, evaluates it, and runs the compute passes
    // that show it the frame and bring its answer back. One call, like any other shader here.
    //
    // Sizes come from the resources. Everything the pass cannot work out for itself is in
    // DlssNrFrameInfo; everything the user chose stays in Config. colour and output may be the same
    // resource. timingQueue is the queue this list will be executed on, when the caller knows it.
    void Dispatch(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* colour, ID3D12Resource* depth,
                  ID3D12Resource* motion, ID3D12Resource* output, const DlssNrFrameInfo& frame,
                  ID3D12CommandQueue* timingQueue = nullptr, int observedRayReconstruction = -1);

  private:
    friend void DlssNr::EvaluateAfterUpscale(ID3D12GraphicsCommandList*, NVSDK_NGX_Parameter*,
        ID3D12CommandQueue*, bool, uint32_t, uint32_t, int, uint64_t);
    // The handoff owns g_nrMutex from input bookkeeping through dispatch and publication.
    void DispatchLocked(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* colour, ID3D12Resource* depth,
        ID3D12Resource* motion, ID3D12Resource* output, const DlssNrFrameInfo& frame,
        ID3D12CommandQueue* timingQueue, int observedRayReconstruction);
  public:
    // Records one pass. Resources that a given mode does not read may be null; a stand-in is bound in
    // their place so every descriptor in the table is valid.
    // One compute pass. The public entry below drives three of these plus the model.
    bool DispatchPass(ID3D12GraphicsCommandList* InCmdList, const DlssNrConstants& InConstants,
                  ID3D12Resource* InSource, ID3D12Resource* InModel, ID3D12Resource* InOriginal,
                  ID3D12Resource* InMotion,
                  // Vestigial. Fed to the slot the removed edit accumulator read its history from;
                  // nothing reads it now and every caller passes nullptr. Kept only so the binding
                  // table keeps its shape -- not evidence that temporal accumulation exists.
                  ID3D12Resource* InPrevEdit, ID3D12Resource* OutTarget,
                  ID3D12Resource* OutKeep);
};
