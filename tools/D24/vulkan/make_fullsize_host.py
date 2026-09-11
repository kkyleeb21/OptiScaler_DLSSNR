from pathlib import Path
root=Path(__file__).resolve().parents[3];s=Path(__file__).with_name('bg3_format_host.cpp').read_text()
s=s.replace('cycle<3','cycle<2').replace('unsigned width=cycle==1?512:256, height=cycle==2?512:256;','unsigned width=cycle?3840:1920, height=cycle?2160:1080;')
s=s.replace('frame<60','frame<12').replace('unsigned bad=0;','unsigned bad=0,changed=0;')
s=s.replace('transition(0,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);', '''VkBufferImageCopy originalCopy{};originalCopy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};originalCopy.imageExtent={width,height,1};originalCopy.bufferOffset=uint64_t(frame%4)*width*height*16+uint64_t(width)*height*8;vkCmdCopyImageToBuffer(cmd,images[0],VK_IMAGE_LAYOUT_GENERAL,buffer,1,&originalCopy);
 transition(0,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);''')
s=s.replace('for(unsigned i=0;i<width*height;i++){decoded', 'for(unsigned i=0;i<width*height;i++){if(packed[i]!=packed[uint64_t(width)*height*2+i]){if(pure)++bad;else ++changed;}decoded')
s=s.replace('reset_replay_checked=3','reset_replay_checked=1')
s=s.replace(' printf("pixel_bad=', ' printf("pure=%d changed_pixels=%u\\n",pure,changed);if(!pure && !changed)return 35;\n printf("pixel_bad=')
s=s.replace('if(retired<32)return 31;','if(retired<24)return 31;')
Path(__file__).with_name('fullsize_host.cpp').write_text(s)
build=root/'builds/D24_Vulkan';cmd=(build/'build-bg3-format-host.cmd').read_text().replace('bg3_format_host.cpp','fullsize_host.cpp').replace('vkbg3format.exe','vkfullsize.exe')
(build/'build-fullsize-host.cmd').write_text(cmd)
