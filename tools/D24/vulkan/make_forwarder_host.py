from pathlib import Path
root=Path(__file__).resolve().parents[3];s=Path(__file__).with_name('bg3_format_host.cpp').read_text()
core=(root/'workspace/dlss5/worktrees/d18-012-re-integration/OptiScaler/dlssnr/DlssNrFeature_Vk.cpp').read_text()
types=core[core.index('using PFN_VkProbe'):core.index('// One image this pass owns')]
s=s.replace('int main(int argc,char**){',types+'\nint main(int argc,char**){')
s=s.replace('volatile int code=init(0,L"E:\\\\DLSSNR\\\\builds\\\\D24_Vulkan",instance,pd,device,0x15,nullptr);', '''
 auto shim=LoadLibraryW(L"E:\\\\DLSSNR\\\\builds\\\\D24_Vulkan\\\\nvngx.dll_dlssnr.dll");if(!shim)return 40;
 auto shimInit=(PFN_VkInit)GetProcAddress(shim,"dlssnr_vk_init");auto shimCreate=(PFN_VkCreate)GetProcAddress(shim,"dlssnr_vk_create");auto shimEval=(PFN_VkEvaluate)GetProcAddress(shim,"dlssnr_vk_evaluate");
 if(!shimInit || !shimCreate || !shimEval)return 41;
 volatile int code=shimInit(L"E:\\\\DLSSNR\\\\builds\\\\D24_Vulkan\\\\nvngx_dlssnr.dll",L"E:\\\\DLSSNR\\\\builds\\\\D24_Vulkan",instance,pd,device,0x15);''')
assert 'auto shim=' in s
s=s.replace('code=create(cmd,18,&p,&handle);','handle=shimCreate(cmd,&p,width,height,1,1.0f,0,1.0f,1.0f,-1.0f,0,1);code=handle?1:0;')
s=s.replace('code=pure?1:evaluate(cmd,handle,&p,nullptr);','code=pure?1:shimEval(cmd,handle,&p,&resources[4],&resources[1],&resources[2],&resources[3],width,height,width,height,1,frame%10==0,1.0f,0,1.0f,1.0f,-1.0f,0,-1.0f,-1.0f);')
s=s.replace('GetProcAddress(nr,"NVSDK_NGX_VULKAN_Shutdown1")','GetProcAddress(shim,"dlssnr_vk_shutdown")')
Path(__file__).with_name('forwarder_host.cpp').write_text(s)
build=root/'builds/D24_Vulkan';cmd=(build/'build-bg3-format-host.cmd').read_text().replace('bg3_format_host.cpp','forwarder_host.cpp').replace('vkbg3format.exe','vkforwarder.exe')
(build/'build-forwarder-host.cmd').write_text(cmd)
