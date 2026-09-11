from pathlib import Path
root=Path(__file__).resolve().parents[3]
s=Path(__file__).with_name('compose_host.cpp').read_text()
s=s.replace('VK_FORMAT_R32G32B32A32_SFLOAT,VK_FORMAT_R32_SFLOAT,','VK_FORMAT_B10G11R11_UFLOAT_PACK32,VK_FORMAT_D24_UNORM_S8_UINT,')
s=s.replace('OK(vkCreateImage(device,&info,nullptr,&images[i]));','if(i==1)info.usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;OK(vkCreateImage(device,&info,nullptr,&images[i]));')
s=s.replace('vi.subresourceRange=range;','vi.subresourceRange=range;if(i==1)vi.subresourceRange.aspectMask=VK_IMAGE_ASPECT_DEPTH_BIT;')
s=s.replace('resources[i].ReadWrite=true;','resources[i].ReadWrite=i!=1 && i!=2;')
s=s.replace('range,formats[i],width,height};','range,formats[i],width,height};if(i==1)resources[i].Resource.ImageViewInfo.SubresourceRange.aspectMask=VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT;')
s=s.replace('FUN(vkCmdClearColorImage);','FUN(vkCmdClearColorImage);FUN(vkCmdClearDepthStencilImage);')
s=s.replace('barrier.subresourceRange=range;','barrier.subresourceRange=range;if(i==1)barrier.subresourceRange.aspectMask=VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT;')
s=s.replace('vkCmdClearColorImage(cmd,images[i],VK_IMAGE_LAYOUT_GENERAL,&clear,1,&range);','if(i==1){VkClearDepthStencilValue value{0.5f,0};vkCmdClearDepthStencilImage(cmd,images[i],VK_IMAGE_LAYOUT_GENERAL,&value,1,&barrier.subresourceRange);}else vkCmdClearColorImage(cmd,images[i],VK_IMAGE_LAYOUT_GENERAL,&clear,1,&range);')
s=s.replace('auto pixels=static_cast<float*>(data)+uint64_t(readFrame%4)*width*height*4;', '''
 auto packed=static_cast<uint32_t*>(data)+uint64_t(readFrame%4)*width*height*4;
 auto decode=[](unsigned v,unsigned bits){unsigned mantissa=v&((1u<<bits)-1),exponent=v>>bits;return exponent==31?NAN:std::ldexp(float(mantissa+(exponent?(1u<<bits):0)),int(exponent?exponent:1)-15-int(bits));};
 std::vector<float> decoded(uint64_t(width)*height*4);
 for(unsigned i=0;i<width*height;i++){decoded[i*4]=decode(packed[i]&2047,6);decoded[i*4+1]=decode((packed[i]>>11)&2047,6);decoded[i*4+2]=decode(packed[i]>>22,5);decoded[i*4+3]=1;}
 auto pixels=decoded.data();''')
s=s.replace('return code==1?0:11;','return code==1?0:11;')
Path(__file__).with_name('bg3_format_host.cpp').write_text(s)
build=root/'builds/D24_Vulkan'
cmd=(build/'build-compose-host.cmd').read_text().replace('compose_host.cpp','bg3_format_host.cpp').replace('vkcompose.exe','vkbg3format.exe')
(build/'build-bg3-format-host.cmd').write_text(cmd)
