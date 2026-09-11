from pathlib import Path
root=Path(__file__).resolve().parents[3];src=Path(__file__).with_name('stress_tracking.cpp').read_text()
def change(a,b):
 global src
 assert src.count(a)==1,(a,src.count(a))
 src=src.replace(a,b)
change('#include <windows.h>','#include <windows.h>\n#include <memory>\n#include "shaders/dlssnr/DlssNr_Vk.h"')
change('int main(){','int main(int argc,char**){bool pure=argc>1;')
change('base.shaderInt64=VK_FALSE;', 'base.shaderInt64=VK_FALSE;base.shaderStorageImageExtendedFormats=VK_TRUE;')
change(' unsigned width=cycle==1?512:256, height=cycle==2?512:256;',' unsigned width=cycle==1?512:256, height=cycle==2?512:256;\n auto pass=std::make_unique<DlssNr_Vk>("host",device,pd);if(!pass->IsInit())return 32;')
for name in ['images','memories','views','resources']:
 src=src.replace(name+'[4]{}',name+'[6]{}')
change('VK_FORMAT_R32G32_SFLOAT,VK_FORMAT_R32G32B32A32_SFLOAT}', 'VK_FORMAT_R16G16_SFLOAT,VK_FORMAT_R16G16B16A16_SFLOAT,VK_FORMAT_R16G16B16A16_SFLOAT,VK_FORMAT_R32G32B32A32_SFLOAT}')
change('for(unsigned i=0;i<4;i++){VkImageCreateInfo','for(unsigned i=0;i<6;i++){VkImageCreateInfo')
change('for(unsigned i=0;i<4;i++){VkImageMemoryBarrier','for(unsigned i=0;i<6;i++){VkImageMemoryBarrier')
change('barrier.oldLayout=frame?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_UNDEFINED;', 'barrier.oldLayout=frame?(i>=3?VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:VK_IMAGE_LAYOUT_GENERAL):VK_IMAGE_LAYOUT_UNDEFINED;')
change('A::Handoff(cmd,nullptr,nullptr,0);p.Set("DLSSNR.Reset",frame%10==0?1u:0u);', '''
 auto transition=[&](unsigned i,VkImageLayout from,VkImageLayout to){VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};b.srcAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;b.dstAccessMask=b.srcAccessMask;b.oldLayout=from;b.newLayout=to;b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.image=images[i];b.subresourceRange=range;vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&b);};
 DlssNrConstants constants{};constants.Mode=DlssNrMode_Encode;constants.Width=width;constants.Height=height;constants.GuideWidth=width;constants.GuideHeight=height;constants.WhitePoint=1;constants.TransferStrength=1;constants.ColourStrength=1;constants.MaxRatio=4;
 transition(0,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
 if(!pass->Dispatch(cmd,constants,width,height,views[0],{}, {}, {},views[4],views[5],formats[4],formats[5]))return 33;
 A::Handoff(cmd,nullptr,nullptr,0);p.Set("DLSSNR.Color",static_cast<void*>(&resources[4]));p.Set("DLSSNR.Reset",frame%10==0?1u:0u);''')
change('code=evaluate(cmd,handle,&p,nullptr);', 'code=pure?1:evaluate(cmd,handle,&p,nullptr);')
change('vkCmdCopyImageToBuffer(cmd,images[3],VK_IMAGE_LAYOUT_GENERAL,buffer,1,&copy);', '''
 transition(3,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
 transition(4,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
 transition(5,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
 transition(0,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_IMAGE_LAYOUT_GENERAL);
 constants.Mode=DlssNrMode_Resolve;if(pure)constants.TransferStrength=0;
 if(!pass->Dispatch(cmd,constants,width,height,views[4],pure?views[4]:views[3],views[5],{},views[0],{},formats[0],VK_FORMAT_R16G16B16A16_SFLOAT))return 34;
 vkCmdCopyImageToBuffer(cmd,images[0],VK_IMAGE_LAYOUT_GENERAL,buffer,1,&copy);''')
change('for(unsigned i=0;i<4;i++){vkDestroyImageView', 'pass.reset();for(unsigned i=0;i<6;i++){vkDestroyImageView')
change('if(retired!=32)return 31;', 'if(retired<32)return 31;')
Path(__file__).with_name('compose_host.cpp').write_text(src)
build=root/'builds/D24_Vulkan'
cmd=(build/'build-tracking-host.cmd').read_text().replace('stress_tracking.cpp','compose_host.cpp').replace('vkstress_tracking.exe','vkcompose.exe').replace('stress_tracking.obj','compose_host.obj')
project=root/'workspace/dlss5/worktrees/d18-012-re-integration/OptiScaler'
cmd=cmd.replace('/std:c++17','/std:c++17 /I"'+str(project)+'"')
cmd=cmd.replace('/W4','/O2 /W4 /wd4324 /wd4245')
cmd=cmd.replace(' /Fo:', ' "'+str(project/'shaders/Shader_Vk.cpp')+'" "'+str(project/'shaders/dlssnr/DlssNr_Vk.cpp')+'" "'+str(project/'library/vulkan/vulkan-1.lib')+'" /Fo:')
cmd=cmd.replace('/Fo:"'+str(build/'compose_host.obj')+'"','/Fo'+str(build)+'\\')
(build/'build-compose-host.cmd').write_text(cmd)
