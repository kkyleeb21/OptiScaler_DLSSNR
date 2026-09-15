// Included at the single final-composition site. The original SR target has not
// been edited yet. Every failure below leaves that full SR base untouched.
        bool mpInputReadable=false,mpAnswerReadable=false,mpZeroReadable=false;
        bool mpDeltaReadable[2]{};
        NrScopeExit restoreMultipass{[&]{
            if(mpInputReadable)Barrier(cmdList,g_multi.input,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            if(mpAnswerReadable)Barrier(cmdList,g_multi.answer,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            if(mpZeroReadable)Barrier(cmdList,g_multi.zeroMotion,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            for(unsigned i=0;i<2;++i)if(mpDeltaReadable[i])Barrier(cmdList,g_multi.delta[i],D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }};
        if(g_multi.ready>1){
            g_multi.scratchUse=submission;
            DlssNrConstants residual=encodeParams;
            residual.RelativeColour=2;residual.Mode=7;
            residual.Width=width;residual.Height=height;
            residual.ValidX=frame.Rects.output.x;residual.ValidY=frame.Rects.output.y;
            residual.ValidWidth=frame.Rects.output.width;residual.ValidHeight=frame.Rects.output.height;
            residualReady=DispatchPass(cmdList,residual,modelInput,g_nr.output,nullptr,nullptr,nullptr,g_multi.delta[0],nullptr);
            if(residualReady){Barrier(cmdList,g_multi.delta[0],D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);mpDeltaReadable[0]=true;}
            if(residualReady&&g_multi.shared){
                if(!g_multi.zeroInitialized.Ready()){
                    auto clear=residual;clear.Mode=9;
                    clear.Width=unsigned(g_multi.zeroMotion->GetDesc().Width);clear.Height=g_multi.zeroMotion->GetDesc().Height;
                    residualReady=DispatchPass(cmdList,clear,g_nr.colorCopy,nullptr,nullptr,nullptr,nullptr,g_multi.zeroMotion,nullptr);
                    if(residualReady)g_multi.zeroInitialized.Recorded(submission);
                }
                if(residualReady){Barrier(cmdList,g_multi.zeroMotion,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);mpZeroReadable=true;}
            }
            ID3D12Resource* previousAnswer=g_nr.output;
            unsigned previousDelta=0;
            for(unsigned passIndex=1;residualReady&&passIndex<g_multi.ready;++passIndex){
                auto& pass=g_multi.passes[passIndex-1];
                const auto tuning=g_multi.shared?DlssNr::Multipass::Read(cfg,0):pass.built;
                const float ratio=tuning.scaling?tuning.ratio:1;
                // Conservative footprint across independently sized passes; never
                // compound ratios against the previous pass's allocation.
                resolveParams.NetworkRatioX=std::min(resolveParams.NetworkRatioX,ratio);
                resolveParams.NetworkRatioY=std::min(resolveParams.NetworkRatioY,ratio);
                auto* passInput=previousAnswer;
                if(cfg.DlssNrCustomColorFilter.value_or_default()&&tuning.scaling&&ratio<1){
                    if(mpInputReadable){Barrier(cmdList,g_multi.input,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);mpInputReadable=false;}
                    DlssNrConstants filter{};filter.Mode=DlssNrMode_ColorPrefilter;
                    filter.Width=DlssNr::Multipass::Network(width,ratio,16);filter.Height=DlssNr::Multipass::Network(height,ratio,8);
                    filter.SourceWidth=width;filter.SourceHeight=height;
                    filter.NetworkRatioX=float(filter.Width)/width;filter.NetworkRatioY=float(filter.Height)/height;
                    filter.CatmullRomInput=cfg.DlssNrCatmullRomInput.value_or_default()?1:0;
                    residualReady=DispatchPass(cmdList,filter,previousAnswer,nullptr,nullptr,nullptr,nullptr,g_multi.input,nullptr);
                    if(!residualReady)break;
                    Barrier(cmdList,g_multi.input,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                    mpInputReadable=true;passInput=g_multi.input;
                }
                auto* passOutput=previousAnswer==g_nr.output?g_multi.answer:g_nr.output;
                if(passOutput==g_nr.output || mpAnswerReadable)
                    Barrier(cmdList,passOutput,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                if(passOutput==g_multi.answer)mpAnswerReadable=false;
                if(!g_multi.shared)pass.lastUse=submission;
                const bool resetPass=!g_multi.shared&&pass.reset;
                const int passResult=g_nr.evaluate(cmdList,g_multi.shared?g_nr.feature:pass.feature,
                    g_multi.shared?g_nr.capabilityParams:pass.params,passInput,depthIn,g_multi.shared?g_multi.zeroMotion:motionIn,
                    passOutput,width,height,guideWidth,guideHeight,g_nr.guideDepthInverted?1:0,resetPass?1:0,
                    tuning.intensity,int(tuning.style),tuning.structure,tuning.tone,tuning.skin,tuning.autoMask?1:0,
                    g_nr.guideMvScaleX,g_nr.guideMvScaleY,ratio,&modelRects);
                Barrier(cmdList,passOutput,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                if(passOutput==g_multi.answer)mpAnswerReadable=true;
                if(passResult==NVSDK_NGX_Result_Success)++g_multi.recorded;
                MultipassEvent(cfg,"mp_evaluate",passIndex,uint32_t(passResult),g_multi.shared?"shared_zero_motion":"independent_history",true,resetPass);
                if(passResult!=NVSDK_NGX_Result_Success){
                    residualReady=false;g_nr.failed=true;g_nr.reset=true;ResetAdditional();
                    g_multi.reason=g_nr.reason="Additional NR evaluation failed; complete SR retained";
                    if(g_multi.shared)g_nr.builtIntensity=-999; // fenced rebuild on explicit retry
                    else pass.width=0;
                    if(FAILED(device->GetDeviceRemovedReason()))g_deviceLost=true;
                    MultipassEvent(cfg,"mp_failure",passIndex,uint32_t(passResult),"evaluate_failed_original_sr_retained");break;
                }
                if(passIndex==1 && g_layerCapture.inFrame())
                    g_layerCapture.copy(cmdList,2,passOutput,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                pass.reset=false;
                const unsigned nextDelta=1-previousDelta;
                if(mpDeltaReadable[nextDelta]){Barrier(cmdList,g_multi.delta[nextDelta],D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);mpDeltaReadable[nextDelta]=false;}
                residual.Mode=8;
                residualReady=DispatchPass(cmdList,residual,passInput,passOutput,nullptr,nullptr,g_multi.delta[previousDelta],g_multi.delta[nextDelta],nullptr);
                if(residualReady){Barrier(cmdList,g_multi.delta[nextDelta],D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);mpDeltaReadable[nextDelta]=true;}
                previousDelta=nextDelta;previousAnswer=passOutput;
            }
            multipassDelta=g_multi.delta[previousDelta];
            resolveParams.RelativeColour=2;resolveParams.Transfer=0;resolveParams.ExperimentalCompose=0;
            resolveParams.GuidedReconstruction=0; // Preserve the user high-frequency switch and per-chain footprint.
            if(!residualReady){g_nr.reset=true;ResetAdditional();}
        }
