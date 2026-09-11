from pathlib import Path
root=Path(__file__).resolve().parents[3]
source=Path(__file__).with_name('stress_ext.cpp').read_text()
source=source.replace('#include <windows.h>', '#include <windows.h>\n#include "D24VkTracking.h"\nnamespace A=DlssNr::VkAudit;')
source=source.replace('FUN(vkCreateInstance);', 'FUN(vkGetDeviceProcAddr);FUN(vkCreateInstance);')
source=source.replace(' // No recorded references survive a resize:',
    ' A::RegisterDevice(device,vkGetDeviceProcAddr);for(unsigned slot=0;slot<4;slot++)A::Allocate(device,pools[slot],family,1,&cmds[slot]);\n // No recorded references survive a resize:')
source=source.replace('OK(vkBeginCommandBuffer(cmd,&bc));\n for(unsigned i=0;i<4;i++)',
                      'OK(vkBeginCommandBuffer(cmd,&bc));A::Invalidate(cmd);\n for(unsigned i=0;i<4;i++)')
source=source.replace('p.Set("DLSSNR.Reset",frame%10==0?1u:0u);',
                      'A::Handoff(cmd,nullptr,nullptr,0);p.Set("DLSSNR.Reset",frame%10==0?1u:0u);')
source=source.replace('OK(vkQueueSubmit(queue,1,&si,fences[frame%4]));',
                      'auto batch=A::Prepare(A::Commands(1,&si));OK(vkQueueSubmit(queue,1,&si,fences[frame%4]));A::Submitted(queue,batch,VK_SUCCESS,[&](VkFence f){return vkQueueSubmit(queue,0,nullptr,f);});')
source=source.replace('OK(vkResetCommandPool(device,resetPool,0));}',
                      'OK(vkResetCommandPool(device,resetPool,0));A::InvalidatePool(resetPool,false);}')
source=source.replace('vkDestroyDevice(device,nullptr);',
                      'unsigned retired=0;for(auto& s:A::completionSamples)if(s.reported)++retired;printf("tracking_samples=%zu retired=%u\\n",A::completionSamples.size(),retired);if(retired!=32)return 31;A::DestroyDevice(device);vkDestroyDevice(device,nullptr);')
assert source.count('A::Invalidate(cmd)')==1
assert source.count('A::Submitted(')==1
Path(__file__).with_name('stress_tracking.cpp').write_text(source)
build=root/'builds/D24_Vulkan'
cmd=(build/'build-stress-ext.cmd').read_text().replace('stress_ext.cpp','stress_tracking.cpp').replace('vkstress_ext.exe','vkstress_tracking.exe').replace('stress_ext.obj','stress_tracking.obj')
cmd=cmd.replace('cl /nologo', 'cl /nologo /I"E:\\DLSSNR\\tools\\D24\\vulkan\\test_include" /I"E:\\DLSSNR\\workspace\\dlss5\\worktrees\\d18-012-re-integration\\OptiScaler\\dlssnr"')
(build/'build-tracking-host.cmd').write_text(cmd)
