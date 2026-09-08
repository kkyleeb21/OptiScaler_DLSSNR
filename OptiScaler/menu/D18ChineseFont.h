#pragma once
#include "D18Localization.h"
#include <windows.h>
#include <filesystem>
#include <fstream>
namespace D18Ui {
inline bool AddChineseFont(ImFontAtlas* atlas,float size){
 chineseFontAvailable=false;
 if(atlas->Fonts.empty())atlas->AddFontDefault();
 static ImVector<ImWchar> ranges;
 if(ranges.empty()){
  ImFontGlyphRangesBuilder builder;
  for(auto& t:translations)builder.AddText(t.zh);
  builder.AddText("\xe7\xae\x80\xe4\xbd\x93\xe4\xb8\xad\xe6\x96\x87");
  builder.BuildRanges(&ranges);
 }
 wchar_t windows[MAX_PATH]{};if(!GetWindowsDirectoryW(windows,MAX_PATH))return false;
 for(const wchar_t* name:{L"msyh.ttc",L"msjh.ttc",L"simhei.ttf"}){
  std::ifstream file(std::filesystem::path(windows)/L"Fonts"/name,std::ios::binary|std::ios::ate);
  if(!file)continue;auto length=file.tellg();if(length<=0||length>64*1024*1024)continue;
  auto data=IM_ALLOC(size_t(length));if(!data)return false;
  file.seekg(0);if(!file.read(static_cast<char*>(data),length)){IM_FREE(data);continue;}
  ImFontConfig config;config.MergeMode=true;config.OversampleH=1;config.OversampleV=1;
  config.FontDataOwnedByAtlas=true;
  if(atlas->AddFontFromMemoryTTF(data,int(length),size,&config,ranges.Data)){chineseFontAvailable=true;return true;}
 }
 return false;
}
}
