"""Derive the multi-submit/resize host from the audited baseline, with exact edit guards."""
from pathlib import Path

root = Path(__file__).resolve().parents[3]
s = Path(__file__).with_name('probe.cpp').read_text()
def replace(old, new):
    global s
    assert s.count(old) == 1, (old, s.count(old))
    s = s.replace(old, new)

replace(' ProbeParameters p;', '''
 VkCommandPool pools[4]{pool}; VkCommandBuffer cmds[4]{cmd}; VkFence fences[4]{};
 VkFenceCreateInfo extraFenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
 for(unsigned slot=0;slot<4;slot++){
   OK(vkCreateFence(device,&extraFenceInfo,nullptr,&fences[slot]));
   if(slot){OK(vkCreateCommandPool(device,&pc,nullptr,&pools[slot]));ac.commandPool=pools[slot];OK(vkAllocateCommandBuffers(device,&ac,&cmds[slot]));}
 }
 // No recorded references survive a resize: complete submissions, then reset every pool.
 for(unsigned cycle=0;cycle<3;cycle++){
 unsigned width=cycle==1?512:256, height=cycle==2?512:256;
 printf("cycle=%u width=%u height=%u inflight=4\\n",cycle,width,height);
 if(cycle){cmd=cmds[0];OK(vkBeginCommandBuffer(cmd,&bc));}
 ProbeParameters p;''')
replace('p.Set("Width",256u);p.Set("Height",256u);p.Set("DLSSNR.Width",256u);p.Set("DLSSNR.Height",256u);',
        'p.Set("Width",width);p.Set("Height",height);p.Set("DLSSNR.Width",width);p.Set("DLSSNR.Height",height);')
replace('info.extent={256,256,1}', 'info.extent={width,height,1}')
replace('range,formats[i],256,256}', 'range,formats[i],width,height}')
replace('bi.size=256ull*256*16', 'bi.size=uint64_t(width)*height*16*4')
replace('p.Set((prefix+"SubrectWidth").c_str(),256u);p.Set((prefix+"SubrectHeight").c_str(),256u);',
        'p.Set((prefix+"SubrectWidth").c_str(),width);p.Set((prefix+"SubrectHeight").c_str(),height);')
replace('OK(vkResetCommandPool(device,pool,0));OK(vkResetFences(device,1,&fence));OK(vkBeginCommandBuffer(cmd,&bc));',
        'cmd=cmds[frame%4];OK(vkResetCommandPool(device,pools[frame%4],0));OK(vkResetFences(device,1,&fences[frame%4]));OK(vkBeginCommandBuffer(cmd,&bc));')
replace('copy.imageExtent={256,256,1}', 'copy.imageExtent={width,height,1};copy.bufferOffset=uint64_t(frame%4)*width*height*16')
replace('OK(vkQueueSubmit(queue,1,&si,fence));OK(vkWaitForFences(device,1,&fence,VK_TRUE,10000000000ull));void* data=nullptr;',
        'OK(vkQueueSubmit(queue,1,&si,fences[frame%4]));if(frame%4==3){OK(vkWaitForFences(device,4,fences,VK_TRUE,10000000000ull));for(unsigned readFrame=frame-3;readFrame<=frame;readFrame++){void* data=nullptr;')
replace('auto pixels=static_cast<float*>(data);', 'auto pixels=static_cast<float*>(data)+uint64_t(readFrame%4)*width*height*4;')
s = s.replace('256*256', 'width*height')
start = s.index('auto pixels=')
end = s.index(' printf("pixel_bad=')
chunk = s[start:end].replace('frame==', 'readFrame==').replace('pixels,bi.size', 'pixels,uint64_t(width)*height*16')
chunk = chunk.replace('vkUnmapMemory(device,bufferMemory);}', 'vkUnmapMemory(device,bufferMemory);}}}')
s = s[:start]+chunk+s[end:]
replace('code=release(handle);', 'for(auto resetPool:pools){OK(vkResetCommandPool(device,resetPool,0));}\ncode=release(handle);')
start = s.index(' auto shutdown=')
end = s.index(' for(unsigned i=0;i<4;i++){vkDestroyImageView', start)
shutdown = s[start:end]
s = s[:start]+s[end:]
replace('vkDestroyFence(device,fence,nullptr);vkDestroyCommandPool(device,pool,nullptr);vkDestroyDevice(device,nullptr);vkDestroyInstance(instance,nullptr);',
        'vkDestroyFence(device,fence,nullptr);if(code!=1)return 11; }\n'+shutdown+'for(unsigned slot=0;slot<4;slot++){vkDestroyFence(device,fences[slot],nullptr);vkDestroyCommandPool(device,pools[slot],nullptr);}vkDestroyDevice(device,nullptr);vkDestroyInstance(instance,nullptr);')
Path(__file__).with_name('stress.cpp').write_text(s)
build = root/'builds/D24_Vulkan'
cmd = (build/'build.cmd').read_text().replace('probe.cpp','stress.cpp').replace('vkprobe.exe','vkstress.exe').replace('probe.obj','stress.obj')
(build/'build-stress.cmd').write_text(cmd)
