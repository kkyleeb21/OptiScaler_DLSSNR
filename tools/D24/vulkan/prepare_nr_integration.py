from pathlib import Path
import difflib
path=Path('E:/DLSSNR/workspace/dlss5/worktrees/d18-012-re-integration/OptiScaler/dlssnr/DlssNrFeature_Vk.cpp')
old=path.read_text();s=old
def change(a,b,count=1):
    global s
    assert s.count(a)==count,(a,s.count(a))
    s=s.replace(a,b)
change('if (!VkAudit::NativeExecutionValidated)','if (!VkAudit::NativeArmed())',2)
change('    if (!cfg.DlssNrEnabled.value_or_default())\n        return;', '''    static bool keyWasDown=false;
    static unsigned mode=0;
    const bool keyDown=(GetAsyncKeyState(VK_PRIOR)&0x8000)!=0;
    if(keyDown && !keyWasDown) {
        mode=(mode+1)%3; g_vk.reset=true;
        VkAudit::Write("event=nr_mode mode=%u meaning=0_off_1_conversion_2_nr",mode);
    }
    keyWasDown=keyDown;
    if(mode==0) return;''')
change('    std::lock_guard<std::mutex> lock(g_vkMutex);','    std::lock_guard<std::mutex> lock(g_vkMutex);\n    CollectRetired();')
change('    const uint32_t width = colour->Resource.ImageViewInfo.Width;', '''    if(colour->Type!=NVSDK_NGX_RESOURCE_VK_TYPE_VK_IMAGEVIEW || depth->Type!=NVSDK_NGX_RESOURCE_VK_TYPE_VK_IMAGEVIEW || motion->Type!=NVSDK_NGX_RESOURCE_VK_TYPE_VK_IMAGEVIEW) return;
    const uint32_t width = colour->Resource.ImageViewInfo.Width;''')
change('    g_vk.instance = instance;\n    g_vk.physicalDevice = physicalDevice;', '''    if(g_vk.device && (g_vk.device!=device || g_vk.width!=width || g_vk.height!=height)) {
        if(retiredStates.size()>=4){Fail("too many pending resource generations");return;}
        retiredStates.push_back(std::make_unique<VkState>(std::move(g_vk)));
        g_vk=VkState{};
        VkAudit::Write("event=nr_resize width=%u height=%u retired=%zu",width,height,retiredStates.size());
    }
    const auto lease=VkAudit::Acquire(cmdBuffer,device);
    if(!lease.Valid()){Fail("untracked Vulkan command buffer");return;}
    g_vk.leases.erase(std::remove_if(g_vk.leases.begin(),g_vk.leases.end(),[](auto oldLease){return VkAudit::Ready(oldLease);}),g_vk.leases.end());
    if(g_vk.leases.size()>=64){Fail("too many pending Vulkan recordings");return;}
    g_vk.leases.push_back(lease);
    g_vk.instance = instance;
    g_vk.physicalDevice = physicalDevice;''')
change('        ShutdownVk();\n        g_vk.device = device;','        g_vk.device = device;')
a=s.index('    if (g_vk.queryPool == VK_NULL_HANDLE)\n');b=s.index('    if (g_vk.pass == nullptr)',a)
s=s[:a]+'    // GPU timing stays disabled until it shares the completion lease.\n'+s[b:]
change('!CreateImage(g_vk.keep, width, height, working, true)','!CreateImage(g_vk.keep, width, height, VK_FORMAT_R32G32B32A32_SFLOAT, true)')
change('    if (g_vk.feature == nullptr)\n    {\n        g_vk.feature =', '    if (g_vk.feature == nullptr && mode==2)\n    {\n        g_vk.feature =')
change('const unsigned int createFlags = GameCreateFlags(params);','const unsigned int createFlags = featureFlags>=0 ? unsigned(featureFlags) : GameCreateFlags(params);')
change('    Transition(cmdBuffer, g_vk.keep, VK_IMAGE_LAYOUT_GENERAL);','''    Transition(cmdBuffer, g_vk.keep, VK_IMAGE_LAYOUT_GENERAL);
    TransitionForeign(cmdBuffer,colour->Resource.ImageViewInfo.Image,colourRange,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);''')
change('    const int evaluated = g_vk.evaluate(', '    const int evaluated = mode==1 ? 1 : g_vk.evaluate(')
change('    Transition(cmdBuffer, g_vk.keep, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);','''    Transition(cmdBuffer, g_vk.keep, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    TransitionForeign(cmdBuffer,colour->Resource.ImageViewInfo.Image,colourRange,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_IMAGE_LAYOUT_GENERAL);''')
change('resolve, width, height, g_vk.proxy.view, g_vk.output.view, g_vk.keep.view,','resolve, width, height, g_vk.proxy.view, mode==1?g_vk.proxy.view:g_vk.output.view, g_vk.keep.view,')
change('    g_vk.frames++;','''    g_vk.frames++;
    if(g_vk.frames<=120 || g_vk.frames%600==0)
        VkAudit::Write("event=nr_frame frame=%llu mode=%u width=%u height=%u result=%d",g_vk.frames,mode,width,height,evaluated);''')
a=s.index('void ShutdownVk()\n');s=s[:a]+'''void ShutdownVk()
{
    std::lock_guard<std::mutex> lock(g_vkMutex);
    retiredStates.push_back(std::make_unique<VkState>(std::move(g_vk)));
    g_vk=VkState{};
    CollectRetired();
}

void ShutdownDeviceVk(VkDevice device)
{
    std::lock_guard<std::mutex> lock(g_vkMutex);
    bool owned=g_vk.device==device;
    for(const auto& state:retiredStates) owned|=state->device==device;
    if(!owned) return;
    const auto idle=vkDeviceWaitIdle(device);
    if(idle!=VK_SUCCESS && idle!=VK_ERROR_DEVICE_LOST) return;
    if(g_vk.device==device){DestroyState(g_vk);g_vk=VkState{};}
    for(auto it=retiredStates.begin();it!=retiredStates.end();)
        if((*it)->device==device){DestroyState(**it);it=retiredStates.erase(it);}else ++it;
    VkAudit::Write("event=nr_device_shutdown result=%d",int(idle));
}
} // namespace DlssNr
'''
diff=list(difflib.unified_diff(old.splitlines(),s.splitlines(),lineterm=''))[2:]
print('*** Begin Patch\n*** Update File: '+path.as_posix()+'\n'+'\n'.join(diff)+'\n*** End Patch')
