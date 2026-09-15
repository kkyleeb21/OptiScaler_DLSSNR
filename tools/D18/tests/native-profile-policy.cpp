#include <dlssnr/NativeSrProfile.h>
#include <dlssnr/ResearchCapture.h>
#include <cassert>
#include <filesystem>
#include <iostream>
int main(int argc,char** argv){
 assert(argc==3);
 for(int enabled=0;enabled<2;++enabled)for(unsigned flags=0;flags<32;++flags){
  const bool panel=flags&1,post=flags&2,upscale=flags&4,evaluate=flags&8,handoff=flags&16;
  const auto m=DlssNr::NativeSrProfile::Resolve(enabled!=0,panel,post,upscale,evaluate,handoff);
  if constexpr(DlssNr::BuildProfile::Diagnostic){
   assert(m.stagePanel==(panel&&!post&&!upscale));
   assert(m.postReplay==post&&m.upscale==(post||upscale));
   assert(m.evaluateOnly==(post||upscale||evaluate)&&m.nativeHandoff==(post&&handoff));
  }else{assert(!m.stagePanel&&m.postReplay==(enabled!=0)&&m.upscale==(enabled!=0)&&m.evaluateOnly==(enabled!=0)&&m.nativeHandoff==(enabled!=0));}
 }
 const bool expected=argv[2][0]=='1';
 assert(DlssNr::BuildProfile::ResearchCaptureRequested(std::filesystem::path(argv[1]))==expected);
 std::cout<<"PASS 64 native routing combinations; explicit capture gate "<<expected<<"\n";
}
