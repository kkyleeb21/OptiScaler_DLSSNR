from pathlib import Path
r=Path('E:/DLSSNR/builds/D18_Wildlands_StagePanelV9_20260913/core-source/OptiScaler')
p=r/'dlssnr/WildlandsSrStatus.h';s=p.read_text();s=s.replace('#include <atomic>','#include <atomic>\n#include "SrDiagnosticStages.h"')
s=s.replace('inline std::atomic<bool> evaluateOnly', '''inline std::atomic<bool> stagePanel{false},stagePending{false};
inline std::atomic<unsigned> requestedStage{0},activeStage{0};
inline std::atomic<unsigned long long> stageCpu[DiagnosticStages::Count]{},stageGpu[DiagnosticStages::Count]{};
inline void SetDiagnosticStage(unsigned stage){if(stage>=DiagnosticStages::Count)return;requestedStage=stage;D18ExecutionTrace::Point("sr_diagnostic_mode_requested",nullptr,0,stage,0);}
inline std::atomic<bool> evaluateOnly''')
p.write_text(s)
p=r/'hooks/D18WildlandsSr.inl';s=p.read_text()
s=s.replace('namespace Status=DlssNr::WildlandsSr;', 'namespace Status=DlssNr::WildlandsSr;\nnamespace Stages=DlssNr::DiagnosticStages;')
s=s.replace('unsigned done=0,issued=0;', 'unsigned done=0,issued=0;unsigned diagnosticStage=Stages::Full;bool producesSr=false;')
s=s.replace('if(f.issued==3){++gpuComplete;', 'if(Status::stagePanel)++Status::stageGpu[f.diagnosticStage];if(f.producesSr&&f.issued==3){++gpuComplete;')
s=s.replace('f.done=0;f.issued=0;return &f;', 'f.done=0;f.issued=0;f.producesSr=false;return &f;')
s=s.replace('Status::evaluateOnly=GetFileAttributesW', 'Status::stagePanel=GetFileAttributesW((root/L"D18SrDiagnostics.panel.enabled").c_str())!=INVALID_FILE_ATTRIBUTES;\n Status::evaluateOnly=GetFileAttributesW')
s=s.replace('if(Status::evaluateOnly)Event("diagnostic_evaluate_only_armed");', 'if(Status::stagePanel){Status::requestedStage=Status::activeStage=Stages::Observe;Event("diagnostic_stage_panel_armed");}else if(Status::evaluateOnly)Event("diagnostic_evaluate_only_armed");')
# Separate context creation, pipeline allocation and NGX feature lifetime.
start=s.index('inline bool Initialize(ID3D11Device* d,ID3D11DeviceContext* c){')
body=s.index(' StateScope scope;',start)
context=s[start:body].replace('inline bool Initialize(', 'inline bool EnsureContext(')
context=context.replace(' ComPtr<ID3D11Device1> d1;', ' if(context&&privateState)return true;\n ComPtr<ID3D11Device1> d1;')+' return true;\n}\n'
s=s[:start]+context+'inline bool Initialize(ID3D11Device* d,ID3D11DeviceContext* c){\n if(!EnsureContext(d,c))return false;\n HRESULT hr=S_OK;\n D18ExecutionTrace::Point("sr_pipeline_create_begin",c);\n'+s[body:]
ngx=s.index(' if(!NVNGXProxy::InitDx11(d)');end=s.index('\ninline bool Texture(',ngx)
old=s[ngx:end]
old=old.replace(' if(!NVNGXProxy::InitDx11(d)', ' D18ExecutionTrace::Point("sr_ngx_init_begin",c);\n if(!NVNGXProxy::InitDx11(d)')
old=old.replace(' auto nr=NVNGXProxy::D3D11_AllocateParameters()', ' D18ExecutionTrace::Point("sr_ngx_allocate_begin",c);\n auto nr=NVNGXProxy::D3D11_AllocateParameters()')
old=old.replace(' nr=NVNGXProxy::D3D11_CreateFeature()', ' D18ExecutionTrace::Point("sr_ngx_create_begin",c);\n nr=NVNGXProxy::D3D11_CreateFeature()')
old=old.replace('initialized=true;Status::width=width;Status::height=height;Event("created");return true;', 'D18ExecutionTrace::Point("sr_ngx_create_end",c);Event("created");return true;')
s=s[:ngx]+''' initialized=true;Status::width=width;Status::height=height;D18ExecutionTrace::Point("sr_pipeline_create_end",c);return true;
}
inline bool CreateNgx(ID3D11Device* d,ID3D11DeviceContext* c){
 if(handle)return true;
 StateScope scope;
'''+old+s[end:]
# Requests are consumed at the Present boundary, only after all actual submissions drain.
pos=s.index('inline void AfterDraw(')
s=s[:pos]+'''inline void UpdateDiagnosticStage(){
 if(!Status::stagePanel)return;
 const unsigned wanted=Status::requestedStage;
 if(wanted==Status::activeStage){Status::stagePending=false;return;}
 Status::stagePending=true;
 if(fault||gpuDrained!=gpuSubmitted)return;
 Numeric::Internal internal;
 if(handle){auto fn=NVNGXProxy::D3D11_ReleaseFeature();if(!fn){Fail("diagnostic_release_unavailable",E_NOINTERFACE);return;}auto result=fn(handle);if(result!=NVSDK_NGX_Result_Success){Fail("diagnostic_release_failed",long(result));return;}handle=nullptr;}
 if(params){NVNGXProxy::D3D11_DestroyParameters()(params);params=nullptr;}
 Status::activeStage=wanted;Status::stagePending=false;Status::frames=0;Status::lastTick=0;Status::lastGpuTick=0;Status::evaluations=0;reset=true;
 D18ExecutionTrace::Point("sr_diagnostic_mode_applied",context.Get(),0,wanted,gpuDrained);
}
inline void StageCpu(unsigned mode){
 if(!Status::stagePanel)return;
 auto n=++Status::stageCpu[mode];static ULONGLONG last=0;
 if(n==1||GetTickCount64()-last>1000){last=GetTickCount64();D18ExecutionTrace::Point("sr_diagnostic_cpu_completed",context.Get(),0,mode,n);D18ExecutionTrace::Point("sr_diagnostic_gpu_completed",context.Get(),0,mode,Status::stageGpu[mode]);}
}
'''+s[pos:]
s=s.replace('ComPtr<ID3D11Device> d;c->GetDevice(&d);if(d.Get()!=observedDevice)return;', 'if(Status::stagePanel&&(Status::stagePending||Status::requestedStage!=Status::activeStage))return;\n const unsigned mode=Status::stagePanel?Status::activeStage.load():(Status::evaluateOnly?Stages::Evaluate:Stages::Full);\n ComPtr<ID3D11Device> d;c->GetDevice(&d);if(d.Get()!=observedDevice)return;',1)
needle=' Dx11DrawReplay::Geometry geometry;'
s=s.replace(needle,''' if(mode==Stages::Observe){Status::coverageBlocked=false;StageCpu(mode);return;}
 if(mode==Stages::StateOnly){if(!EnsureContext(d.Get(),c))return;{StateScope scope;}Status::coverageBlocked=false;StageCpu(mode);return;}
'''+needle,1)
needle=' if(Status::lastTick&&'
s=s.replace(needle, ' if(Stages::NeedsNgx(mode)&&!CreateNgx(d.Get(),c))return;\n if(Status::lastTick&&',1)
s=s.replace('pending->pending=true;pending->id=', 'pending->diagnosticStage=mode;pending->pending=true;pending->id=',1)
start=s.index(' c->CopyResource(tex[4].Get(),outputResource[1].Get());c->UpdateSubresource')
params=s.index(' params->Set(NVSDK_NGX_Parameter_Color',start)
prep=s[start:params]
prep=prep.replace(' c->CopyResource(tex[4].Get(),outputResource[1].Get());','')
s=s[:start]+''' if(mode==Stages::OriginalWriteback){
  c->CopyResource(tex[4].Get(),outputResource[1].Get());geometry.Apply(context.Get());auto rt=outputRT[1].Get();c->OMSetRenderTargets(1,&rt,nullptr);c->PSSetShader(compose.Get(),nullptr,0);
  ID3D11ShaderResourceView* originals[]={srv[4].Get(),srv[4].Get()};c->PSSetShaderResources(0,2,originals);command.Run(c);
  c->End(pending->stage[0].Get());pending->issued=1;StageCpu(mode);return;
 }
 if(mode==Stages::Full)c->CopyResource(tex[4].Get(),outputResource[1].Get());
'''+prep+''' if(mode==Stages::Prepare||mode==Stages::Create){StageCpu(mode);return;}
'''+s[params:]
s=s.replace('if(Status::evaluateOnly){','if(mode==Stages::Evaluate){',1)
s=s.replace('++Status::evaluations;reset=false;', '++Status::evaluations;reset=false;StageCpu(mode);',1)
s=s.replace('pending->issued=3;', 'pending->issued=3;pending->producesSr=true;StageCpu(mode);',1)
s=s.replace('PollGpu();static unsigned long long gpuLogTick', 'PollGpu();UpdateDiagnosticStage();static unsigned long long gpuLogTick',1)
# Query stage index 0 has a different meaning for OriginalWriteback. Do not
# increment prepare_done for this independent branch.
s=s.replace('++gpuStages[f.done];++f.done;', 'if(f.diagnosticStage!=Stages::OriginalWriteback)++gpuStages[f.done];++f.done;')
p.write_text(s)
p=r/'menu/menu_common.cpp';s=p.read_text(encoding='utf-8-sig')
s=s.replace('if(W::evaluateOnly && !W::failed)srDetail=', 'if(W::stagePanel && !W::failed)srDetail=D18Ui::Format("Diagnostic stage: %s | CPU %llu | GPU %llu",DlssNr::DiagnosticStages::Name(W::activeStage),W::stageCpu[W::activeStage].load(),W::stageGpu[W::activeStage].load());\n        else if(W::evaluateOnly && !W::failed)srDetail=')
needle='        bool on=W::enabled&&config->DLSSEnabled.value_or_default();'
insert='''        if(W::stagePanel){
            D18Ui::SeparatorText("SR diagnostic stages");
            auto requested=W::requestedStage.load();
            if(ImGui::BeginCombo("Run through stage",DlssNr::DiagnosticStages::Name(requested))){
                for(unsigned i=0;i<DlssNr::DiagnosticStages::Count;++i){if(ImGui::Selectable(DlssNr::DiagnosticStages::Name(i),i==requested))W::SetDiagnosticStage(i);}
                ImGui::EndCombo();
            }
            D18Ui::Text("Active: %s",DlssNr::DiagnosticStages::Name(W::activeStage));
            if(W::failed)D18Ui::TextWrapped("Stopped after an error. Restart the game before another test.");
            else if(W::stagePending||W::requestedStage!=W::activeStage)D18Ui::TextWrapped("Switch pending: waiting for submitted GPU work to finish.");
            for(unsigned i=0;i<DlssNr::DiagnosticStages::Count;++i)D18Ui::Text("%s | CPU %llu | GPU %llu",DlssNr::DiagnosticStages::Name(i),W::stageCpu[i].load(),W::stageGpu[i].load());
            D18Ui::TextWrapped("Stages 0-5 preserve the original image. Stage 5 replays only the original image; stage 6 writes SR. CPU/GPU counts are diagnostics, not image-quality acceptance.");
        }
'''
assert needle in s;s=s.replace(needle,insert+needle,1)
s=s.replace('if(W::evaluateOnly){D18Ui::TextWrapped(', 'if(W::evaluateOnly&&!W::stagePanel){D18Ui::TextWrapped(',1)
p.write_text(s,encoding='utf-8-sig')
