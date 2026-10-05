"""WARP fixture of production Basics, compare, theme and layout helpers.

Config option definitions and presentation functions are extracted unchanged from
the candidate. Backend observations are fixtures; no DLL, game or runtime loads.
"""
from pathlib import Path
import argparse,subprocess,json,re

ROOT=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--imgui-objects',type=Path,required=True);p.add_argument('--dependency-root',type=Path,required=True);a=p.parse_args()
out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
c=(ROOT/'OptiScaler/Config.h').read_text(encoding='utf-8')
optional=c[c.index('enum HasDefaultValue'):c.index('constexpr inline int UnboundKey')]
passes=c[c.index('struct DlssNrPassConfig'):c.index('class Config')]
fields='\n'.join(line for line in c.splitlines() if re.search(r'(CustomOptional<.*> DlssNr|std::array<DlssNrPassConfig)',line))
(out/'Config.h').write_text('#pragma once\n#include <optional>\n#include <string>\n#include <array>\n#include <concepts>\n#include <windows.h>\n'+optional+'\nconstexpr int UnboundKey=-1;\n'+passes+'\nstruct Config{\n'+fields+'\n};\n',encoding='utf-8')
n=(ROOT/'OptiScaler/dlssnr/DlssNr_Menu.cpp').read_text(encoding='utf-8')
deferred=n[n.index('static bool DeferredSlider'):n.index('static void CaptureEvent')]
basics=n[n.index('static void RatioChoices'):n.index('// Sections share original controls')]
start=n.index('    if(section==1)');end=n.index('    } else if(section==2)',start)
compare=n[start+len('    if(section==1) {'):end]
(out/'basics.inc').write_text(deferred+basics+'\nvoid RenderD18Menu(Config* config,float menuResScale,int section){const bool dx11=false;'+compare+'}\n',encoding='utf-8')
src=r'''
#define NOMINMAX
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <unordered_map>
#include <string>
#include <optional>
#include <cstdio>
#include <d3d11.h>
#include <wrl/client.h>
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <imgui/imgui_impl_dx11.h>
#include <menu/D18NrHints.h>
#include <menu/D18Layout.h>
#include <menu/D18PreviewTheme.h>
#include <menu/D18WindowLayout.h>
#include <menu/D18MenuPersistence.h>
#include <SimpleIni.h>
#include <dlssnr/MultipassConfig.h>
#include <dlssnr/V8NativeStatus.h>
#define CHECK(x) do{if(!(x)){printf("FAIL line %d: %s\n",__LINE__,#x);return 1;}}while(0)
using Microsoft::WRL::ComPtr;
struct FakeFeature{bool IsInited(){return true;}unsigned TargetWidth(){return 3840;}unsigned TargetHeight(){return 2160;}};
struct State{FakeFeature* currentFeature=nullptr;static State& Instance(){static State s;return s;}};
namespace DlssNrNative{struct AdvancedStatus{unsigned width=3840,height=2160;};}
namespace DlssNr{
struct Runtime{unsigned outputWidth=3840,outputHeight=2160,workWidth=3840,workHeight=2160;};
struct Snapshot{Runtime runtime;std::optional<double> gpuTime;};
inline Snapshot ReadUiSnapshot(){return {};}
inline std::optional<double> LastGpuTimeVk(){return {};}
inline DlssNrNative::AdvancedStatus ReadAdvancedStatusVk(){return {};}
namespace NativeControl{inline bool conversion=false;inline DlssNrNative::AdvancedStatus ReadAdvanced(){return {};}}
void RenderD18Menu(Config*,float,int);
#include "basics.inc"
}
static void saveBmp(ID3D11Device* d,ID3D11DeviceContext* c,ID3D11Texture2D* color,const char* name,unsigned w,unsigned h){
 D3D11_TEXTURE2D_DESC td{};color->GetDesc(&td);td.Usage=D3D11_USAGE_STAGING;td.BindFlags=0;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
 ComPtr<ID3D11Texture2D> stage;if(FAILED(d->CreateTexture2D(&td,nullptr,&stage)))exit(20);c->CopyResource(stage.Get(),color);D3D11_MAPPED_SUBRESOURCE m{};if(FAILED(c->Map(stage.Get(),0,D3D11_MAP_READ,0,&m)))exit(21);
 BITMAPFILEHEADER fh{};BITMAPINFOHEADER ih{};fh.bfType=0x4d42;fh.bfOffBits=sizeof(fh)+sizeof(ih);fh.bfSize=fh.bfOffBits+w*h*4;ih.biSize=sizeof(ih);ih.biWidth=w;ih.biHeight=-int(h);ih.biPlanes=1;ih.biBitCount=32;
 std::ofstream f(name,std::ios::binary);f.write((char*)&fh,sizeof(fh));f.write((char*)&ih,sizeof(ih));for(unsigned y=0;y<h;++y)f.write((const char*)m.pData+size_t(y)*m.RowPitch,w*4);c->Unmap(stage.Get(),0);
}
int main(){
 CHECK(D18Ui::AutoMenuScale(1440)==1);CHECK(D18Ui::AutoMenuScale(2160)==1.5f);CHECK(D18Ui::AutoMenuScale(2880)==2);CHECK(D18Ui::AutoMenuScale(720)<1);
 for(int base=0;base<4;++base){auto p=D18Ui::MakePalette(4,base,{0,0,0,1},true);CHECK((D18Ui::Luminance(p.accent)+.05f)/(D18Ui::Luminance(p.bg)+.05f)>=4.5f);CHECK(p.text.x>.9f&&p.text.y>.9f&&p.text.z>.85f);}
 CHECK(D18Layout::DiagnosticPosition({100,100},{500,500},{300,400},{1600,1000},12).x==612);
 CHECK(D18Layout::DiagnosticPosition({800,100},{500,500},{300,400},{1400,1000},12).x==488);
 CHECK(D18Layout::DiagnosticPosition({100,20},{650,400},{600,300},{1000,1000},12).y==432);
 CSimpleIniA ini;ini.SetUnicode(true);ini.SetValue("Menu","key","value");std::string reason;
 CHECK(D18Ui::SaveMenuIni(ini,std::filesystem::path("success.ini"),reason));CHECK(reason.empty());
 {std::ifstream saved("success.ini",std::ios::binary);unsigned char bom[3]{};saved.read(reinterpret_cast<char*>(bom),3);CHECK(bom[0]==0xef&&bom[1]==0xbb&&bom[2]==0xbf);}
 HANDLE locked=CreateFileW(L"locked.ini",GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);CHECK(locked!=INVALID_HANDLE_VALUE);
 CHECK(!D18Ui::SaveMenuIni(ini,std::filesystem::path("locked.ini"),reason));CHECK(reason.find("open:")!=std::string::npos);CloseHandle(locked);
 ComPtr<ID3D11Device> d;ComPtr<ID3D11DeviceContext> dc;
 CHECK(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&d,nullptr,&dc)));
 ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.LogFilename=nullptr;io.DisplaySize={3840,2160};io.DeltaTime=1.f/60;
 ImFontConfig fc;fc.SizePixels=14;io.FontDefault=io.Fonts->AddFontDefault(&fc);CHECK(D18Ui::AddChineseFont(io.Fonts,14));D18PreviewTheme::LoadFont(io.Fonts,14,false);CHECK(ImGui_ImplDX11_Init(d.Get(),dc.Get()));
 D3D11_TEXTURE2D_DESC td{3840,2160,1,1,DXGI_FORMAT_B8G8R8A8_UNORM,{1,0},D3D11_USAGE_DEFAULT,D3D11_BIND_RENDER_TARGET,0,0};ComPtr<ID3D11Texture2D> color;ComPtr<ID3D11RenderTargetView> view;CHECK(SUCCEEDED(d->CreateTexture2D(&td,nullptr,&color)));CHECK(SUCCEEDED(d->CreateRenderTargetView(color.Get(),nullptr,&view)));
 // Verify the scoped HDR path numerically, including its restoration and accents.
 for(float brightness:{.5f,.9f,1.f}) {
  ImGui_ImplDX11_NewFrame();ImGui::NewFrame();const auto original=ImGui::GetStyleColorVec4(ImGuiCol_Text);auto p=D18Ui::MakePalette(0,0,{0,0,0,1},true);
  {D18PreviewTheme theme(1,[](ImVec4 c){const auto peak=std::max({c.x,c.y,c.z});c.x/=1+peak;c.y/=1+peak;c.z/=1+peak;return c;},p,brightness);
   CHECK(std::abs(ImGui::GetStyleColorVec4(ImGuiCol_Text).x-p.text.x*brightness)<.00001f);
   CHECK(std::abs(ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled).x-p.muted.x*brightness)<.00001f);
   CHECK(std::abs(ImGui::GetStyleColorVec4(ImGuiCol_SeparatorActive).x-p.accent.x*brightness)<.00001f);
   CHECK(std::abs(ImGui::GetStyleColorVec4(ImGuiCol_WindowBg).x-p.bg.x/(1+p.bg.z))<.00001f);
   CHECK(std::abs(D18Ui::Foreground({1,.26f,.22f,1}).x-brightness)<.00001f);
  }
  CHECK(D18Ui::foregroundScale==1);CHECK(ImGui::GetStyleColorVec4(ImGuiCol_Text).x==original.x);ImGui::EndFrame();
 }
 for(unsigned language=0;language<2;++language)for(int scale=1;scale<=3;++scale)for(int mode=0;mode<4;++mode){
  Config cfg;cfg.DlssNrInternalScaling=true;cfg.DlssNrInternalScalingRatio=mode==0?1.f:.5f;cfg.DlssNrHighResolution=mode==3;cfg.DlssNrPassCount=mode==2?4:1;cfg.DlssNrCompare=mode==1?2:0;
  for(int frame=0;frame<5;++frame){
   ImGui_ImplDX11_NewFrame();ImGui::NewFrame();D18Ui::SetLanguage(language);
   auto p=D18Ui::MakePalette(0,0,{0,0,0,1},true);
   {D18PreviewTheme theme(float(scale),[](ImVec4 c){return c;},p);
    ImGui::SetNextWindowPos({20,20});ImGui::SetNextWindowSize({720.f*scale,std::min(1000.f*scale,2136.f)});
    ImGui::Begin("UI1 production Basics (fixture)",nullptr,ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoScrollbar);ImGui::SetWindowFontScale(float(scale));ImGui::PushTextWrapPos(0);
    bool enabled=true;D18Ui::Checkbox("Neural rendering",&enabled);D18Layout::NextChoice("Status and diagnostics",float(scale));D18Layout::Choice("Status and diagnostics",false,float(scale));
    D18Ui::TextWrapped("%s",D18Ui::Tr("Unobserved"));
    int i=0;for(const char* tab:{"Basics","Advanced","SR·FG","Sharpening","Settings"}){if(i++)D18Layout::NextChoice(tab,float(scale));D18Layout::Choice(tab,i==1,float(scale));}
    const float parentScale=ImGui::GetCurrentWindow()->FontWindowScale;
    ImGui::BeginChild("D18PageContent",{0,ImGui::GetContentRegionAvail().y-100.f*scale});ImGui::SetWindowFontScale(parentScale);ImGui::PushTextWrapPos(0);ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,ImVec2(10,6)*float(scale));
    DlssNr::RenderBasics(&cfg,float(scale),false,false,false,true);
    if(frame==4&&scale==1){printf("Basics language=%u mode=%d: body height %.1f, scroll %.1f\n",language,mode,ImGui::GetWindowHeight(),ImGui::GetCurrentWindow()->ScrollMax.y);CHECK(ImGui::GetCurrentWindow()->ScrollMax.y==0);}
    ImGui::PopStyleVar();ImGui::PopTextWrapPos();ImGui::EndChild();D18Ui::TextUnformatted("Unsaved changes");D18Ui::Button("Save");D18Layout::NextChoice("Close",float(scale));D18Ui::Button("Close");ImGui::PopTextWrapPos();ImGui::End();
   }
   ImGui::Render();auto v=view.Get();dc->OMSetRenderTargets(1,&v,nullptr);float bg[]={.025f,.025f,.03f,1};dc->ClearRenderTargetView(v,bg);ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
  }
  char name[80];sprintf_s(name,"basics-%s-%dx-mode%d.bmp",language?"zh":"en",scale,mode);saveBmp(d.Get(),dc.Get(),color.Get(),name,3840,2160);
 }
 // A real drag on the exact production deferred slider must commit only on release.
 Config cfg;ImRect rect;
 auto dragFrame=[&](){ImGui_ImplDX11_NewFrame();ImGui::NewFrame();ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({600,220});ImGui::Begin("deferred");DlssNr::DeferredSlider("Model intensity",&cfg.DlssNrIntensity,0,2);rect=GImGui->LastItemData.Rect;ImGui::End();ImGui::EndFrame();};
 for(int i=0;i<4;++i)dragFrame();io.AddMousePosEvent(rect.Min.x+20,(rect.Min.y+rect.Max.y)/2);dragFrame();dragFrame();io.AddMouseButtonEvent(0,true);dragFrame();io.AddMousePosEvent(rect.Max.x-20,(rect.Min.y+rect.Max.y)/2);dragFrame();dragFrame();CHECK(cfg.DlssNrIntensity.value_or_default()==1);io.AddMouseButtonEvent(0,false);dragFrame();dragFrame();CHECK(cfg.DlssNrIntensity.value_or_default()>1.5f);
 printf("PASS UI1: production Basics 24 WARP images; no 1x scrolling in four modes; release-only intensity; custom contrast; scale mapping; docking; scoped HDR foreground/background/accents; real save success and sharing denial.\n");
 ImGui_ImplDX11_Shutdown();ImGui::DestroyContext();return 0;
}
'''
(out/'ui1-layout.cpp').write_text(src,encoding='utf-8')
objects=[str(a.imgui_objects/(n+'.obj')) for n in ('imgui','imgui_draw','imgui_widgets','imgui_tables','imgui_freetype','imgui_impl_dx11')]
cmd=['cl','/nologo','/EHsc','/MD','/std:c++20','/utf-8','/DWIN32','/I'+str(out),'/I'+str(ROOT/'OptiScaler'),'/I'+str(ROOT/'OptiScaler/include'),'/I'+str(a.dependency_root/'external/simpleini'),'ui1-layout.cpp','/Fe:ui1-layout.exe','/link','/LTCG',*objects,str(a.dependency_root/'external/freetype/freetype.lib'),'d3d11.lib','d3dcompiler.lib','dxgi.lib','user32.lib','gdi32.lib']
(out/'build.cmd').write_text('@echo off\ncall C:\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat >nul\n'+subprocess.list2cmdline(cmd)+'\n',encoding='utf-8')
subprocess.run(['cmd','/d','/c',str(out/'build.cmd')],cwd=out,check=True)
subprocess.run([str(out/'ui1-layout.exe')],cwd=out,check=True)
from PIL import Image
for p in out.glob('basics-*.bmp'):
    with Image.open(p) as im:im.convert('RGB').save(out/(p.stem+'.png'))
(out/'SCREEN_SCOPE.json').write_text(json.dumps({'source':'unchanged extracted production Basics/compare; real theme/layout/wrappers','backend':'fixture observations; no game or runtime','modes':['standard100','standard50 with wipe','four passes','high resolution'],'not_covered':['Advanced page','SR/FG adjustable routes','Sharpening page','Settings page','diagnostic window']},indent=2))
