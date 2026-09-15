from pathlib import Path
r=Path('E:/DLSSNR/builds/D18_Wildlands_DrawReplayV7_20260913/core-source/OptiScaler')
p=r/'hooks/D18WildlandsSr.inl';s=p.read_text(encoding='utf-8-sig')
s=s.replace('namespace Wildlands {','#include "../dlssnr/Dx11DrawReplay.h"\nnamespace Wildlands {',1)
s=s.replace('if(topology!=D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST)reason|=4;','if(topology!=D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST&&topology!=D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP)reason|=4;\n ID3D11Buffer* so[4]{};c->SOGetTargets(4,so);for(auto buffer:so)if(buffer){reason|=4096;buffer->Release();}')
s=s.replace('bool fullScreenDraw=false,UINT drawCount=0,UINT drawFirst=0','const Dx11DrawReplay::Command& command={}')
s=s.replace('(!fullScreenDraw?2u:0u)','(!command.Supported()?2u:0u)')
s=s.replace('coverage,drawCount,drawFirst,w,h,coverageInfo','coverage,command.count,command.first,w,h,coverageInfo')
s=s.replace('Status::coverageBlocked=false;','Dx11DrawReplay::Geometry geometry;\n if(!geometry.Capture(c)){Status::coverageReason=8192;Status::coverageBlocked=true;++waits;reset=true;return;}\n Status::coverageBlocked=false;',1)
old='c->ClearState();auto rt=outputRT[1].Get();c->OMSetRenderTargets(1,&rt,nullptr);c->RSSetViewports(1,&vp);c->RSSetState(raster.Get());c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);c->VSSetShader(fullscreen.Get(),nullptr,0);c->PSSetShader(compose.Get(),nullptr,0);'
new='c->ClearState();geometry.Apply(context.Get());auto rt=outputRT[1].Get();c->OMSetRenderTargets(1,&rt,nullptr);c->PSSetShader(compose.Get(),nullptr,0);'
assert old in s;s=s.replace(old,new)
s=s.replace('c->Draw(3,0);c->End(pending->stage[2].Get());','command.Run(c);c->End(pending->stage[2].Get());')
# Successful submissions record command identity once, independently of UI sampling.
s=s.replace('reset=false;++Status::frames;', 'if(!Status::frames.load()){D18ExecutionTrace::Point("sr_replay_command",c,0,UINT(command.kind),(UINT64(command.count)<<32)|command.first);D18ExecutionTrace::Point("sr_replay_instances",c,0,command.instances,command.firstInstance);D18ExecutionTrace::Point("sr_replay_base_vertex",c,0,UINT(command.baseVertex),0);}\n reset=false;++Status::frames;')
p.write_text(s,encoding='utf-8')
p=r/'hooks/D18InputProbe.h';s=p.read_text(encoding='utf-8-sig')
s=s.replace('Wildlands::AfterDraw(c,a==3&&b==0,a,b)','Wildlands::AfterDraw(c,{Dx11DrawReplay::Kind::Draw,a,b})')
s=s.replace('indexed(c,a,b,d);if(a)Wildlands::AfterDraw(c);','indexed(c,a,b,d);if(a)Wildlands::AfterDraw(c,{Dx11DrawReplay::Kind::Indexed,a,b,1,0,d});')
s=s.replace('instanced(c,a,b,d,e);Wildlands::AfterDraw(c);','instanced(c,a,b,d,e);Wildlands::AfterDraw(c,{Dx11DrawReplay::Kind::Instanced,a,d,b,e});')
s=s.replace('indexedInstanced(c,a,b,d,e,f);Wildlands::AfterDraw(c);','indexedInstanced(c,a,b,d,e,f);Wildlands::AfterDraw(c,{Dx11DrawReplay::Kind::IndexedInstanced,a,d,b,f,e});')
p.write_text(s,encoding='utf-8')
