#pragma once
#include <d3d11.h>

namespace D18InputProbe {
inline thread_local ID3D11DeviceContext* boundContext=nullptr;
inline thread_local ID3D11PixelShader* boundShader=nullptr;
inline thread_local ID3D11DeviceContext* computeContext=nullptr;
inline thread_local ID3D11ComputeShader* boundCompute=nullptr;
inline thread_local bool internalWork=false;
inline thread_local unsigned executeDepth=0;
struct ScopedExecuteDepth {ScopedExecuteDepth(){++executeDepth;}~ScopedExecuteDepth(){--executeDepth;}};

// Context-state swaps bypass SetShader hooks. Restore the observer's identities
// alongside the actual context, including nested internal draws.
struct ScopedInternalContext {
 ID3D11DeviceContext* pixelContext=boundContext;
 ID3D11PixelShader* pixelShader=boundShader;
 ID3D11DeviceContext* csContext=computeContext;
 ID3D11ComputeShader* csShader=boundCompute;
 bool previous=internalWork;
 ScopedInternalContext(){internalWork=true;}
 ~ScopedInternalContext(){boundContext=pixelContext;boundShader=pixelShader;computeContext=csContext;boundCompute=csShader;internalWork=previous;}
 ScopedInternalContext(const ScopedInternalContext&)=delete;
 ScopedInternalContext& operator=(const ScopedInternalContext&)=delete;
};
}
