// Offscreen UI-only test: no injection, no game/runtime, no rendering feature changes.
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <imgui/imgui_impl_dx11.h>
#include <menu/D18ChineseFont.h>
#include <menu/D18NrHints.h>
#include <cassert>
#include <fstream>
#include <vector>
using Microsoft::WRL::ComPtr;
static void draw(unsigned language){
 D18Ui::SetLanguage(language);
 ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({1400,1000});
 ImGui::Begin("D18 locale preview",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings);
 int selected=int(language);const char* langs[]={"English","\xe7\xae\x80\xe4\xbd\x93\xe4\xb8\xad\xe6\x96\x87"};
 ImGui::SetNextItemWidth(180);D18Ui::Combo("Language / \xe8\xaf\xad\xe8\xa8\x80",&selected,langs,2);
 D18Ui::SeparatorText("D18 Runtime Status");
 if(ImGui::BeginTable("status",3)){
  for(const char* label:{"DLSS SR","DLSS FG","DLSS NR"}){ImGui::TableNextColumn();D18Ui::Text("%s  %s",label,"ACTIVE");D18Ui::TextWrapped("NR completed after DLSS SR");}
  ImGui::EndTable();
 }
 if(ImGui::BeginTable("main",2,ImGuiTableFlags_Resizable)){
  ImGui::TableNextColumn();ImGui::PushItemWidth(240);
  D18Ui::SeparatorText("DLSS SR");bool yes=true,no=false;
  D18Ui::Checkbox("Use native NVIDIA DLSS SR",&yes);D18Ui::Text("%ux%u -> %ux%u | frame %ld",1920u,1080u,3840u,2160u,420L);
  D18Ui::Checkbox("Override render preset",&no);D18Ui::Button("Apply SR");
  D18Ui::SeparatorText("DLSS FG");D18Ui::TextWrapped("Game-native FG: enable/disable and multiplier are controlled in game settings. This switch only controls an OptiScaler FG route.");
  ImGui::BeginDisabled();D18Ui::Checkbox("Enable OptiScaler FG route",&no);ImGui::EndDisabled();
  D18Ui::SeparatorText("Sharpness");D18Ui::Checkbox("Override game sharpness",&no);float sharp=.4f;D18Ui::SliderFloat("Sharpness",&sharp,0,1);
  D18Ui::Checkbox("Enable OptiScaler sharpening (RCAS/DA)",&no);
  D18Ui::SeparatorText("Hotkeys");D18Ui::TextWrapped("Click an action, then press a key. Escape cancels; Backspace unbinds; R restores the default.");D18Ui::Button("UI hotkey");ImGui::SameLine();D18Ui::TextUnformatted("Insert");D18Ui::Button("NR hotkey");ImGui::SameLine();D18Ui::TextUnformatted("Page Up");
  ImGui::PopItemWidth();ImGui::TableNextColumn();ImGui::PushItemWidth(225);
  D18Ui::SeparatorText("DLSS NR");D18NrUi::Checkbox("Enable Neural Rendering",&yes);D18Ui::TextUnformatted("Running");
  D18NrUi::Checkbox("Internal network scaling",&yes);float ratio=.5f;D18NrUi::SliderFloat("Network ratio",&ratio,.5f,1);
  D18Ui::SeparatorText("Clarity and detail");float detail=1;D18NrUi::SliderFloat("Detail strength",&detail,0,2);D18NrUi::Checkbox("Preserve original high frequencies",&yes);
  D18Ui::SeparatorText("Colour and brightness");float colour=1;D18NrUi::SliderFloat("Colour strength",&colour,0,1);D18NrUi::Checkbox("Transfer only model colour changes (experimental)",&no);
  D18Ui::SeparatorText("Characters and style");const char* presets[]={"Default","Preset 1","Preset 2","Preset 3"};int p=1;D18NrUi::Combo("Model preset##d18",&p,presets,4);
  for(auto label:{"Intensity##d18","Local structure##d18","Local tone##d18","Skin structure##d18"})D18NrUi::SliderFloat(label,&detail,0,2);
  D18NrUi::Checkbox("Auto skin mask",&yes);D18NrUi::Checkbox("Reduce detail flicker (NR jitter correction)",&yes);
  D18Ui::TextWrapped("Try for skin/hair flicker; turn off if ghosting or instability increases.");
  D18Ui::TextWrapped("Not applied: game does not declare jittered motion vectors.");
  D18Ui::SeparatorText("Advanced and diagnostics");int mode=0;const char* modes[]={"Off","Summary","Trace"};D18NrUi::Combo("Diagnostic mode##d18",&mode,modes,3);D18NrUi::Checkbox("Capture four-stage regions",&no);
  ImGui::PopItemWidth();ImGui::EndTable();
 }
 D18Ui::SeparatorText("Compare the result");D18Ui::Button("Save Settings");ImGui::SameLine();D18Ui::Button("Close");
 ImGui::End();
}
int main(){
 ComPtr<ID3D11Device> d;ComPtr<ID3D11DeviceContext> c;D3D_FEATURE_LEVEL level;
 if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&d,&level,&c)))return 1;
 ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.LogFilename=nullptr;io.DisplaySize={1400,1000};io.DeltaTime=1.f/60;ImGui::StyleColorsDark();
 ImFontConfig cfg;cfg.SizePixels=20;io.FontDefault=io.Fonts->AddFontDefault(&cfg);
 if(!D18Ui::AddChineseFont(io.Fonts,20))return 2;
 if(!ImGui_ImplDX11_Init(d.Get(),c.Get()))return 6;
 ImGui_ImplDX11_NewFrame();ImGui::NewFrame();ImGui::EndFrame();
 unsigned glyphs=0;
 for(auto& t:D18Ui::translations){
  for(const char* p=t.zh;*p;){unsigned cp=0;p+=ImTextCharFromUtf8(&cp,p,nullptr);if(cp>127){if(!io.FontDefault->IsGlyphInFont(ImWchar(cp)))return 3;++glyphs;}}
  D18Ui::SetLanguage(0);auto en=ImHashStr(D18Ui::Label(t.en));D18Ui::SetLanguage(1);if(en!=ImHashStr(D18Ui::Label(t.en)))return 4;
 }
 for(auto l:{"Model preset##d18","Sharpness##d18_sharpness_panel","##hidden"}){
  D18Ui::SetLanguage(0);auto en=ImHashStr(D18Ui::Label(l));D18Ui::SetLanguage(1);if(en!=ImHashStr(D18Ui::Label(l)))return 5;
 }
 D3D11_TEXTURE2D_DESC td{1400,1000,1,1,DXGI_FORMAT_B8G8R8A8_UNORM,{1,0},D3D11_USAGE_DEFAULT,D3D11_BIND_RENDER_TARGET,0,0};
 ComPtr<ID3D11Texture2D> color;ComPtr<ID3D11RenderTargetView> view;
 if(FAILED(d->CreateTexture2D(&td,nullptr,&color))||FAILED(d->CreateRenderTargetView(color.Get(),nullptr,&view)))return 7;
 for(unsigned language=0;language<2;++language){
  for(unsigned frame=0;frame<2;++frame){ImGui_ImplDX11_NewFrame();ImGui::NewFrame();draw(language);ImGui::Render();auto v=view.Get();c->OMSetRenderTargets(1,&v,nullptr);float bg[4]={.03f,.03f,.03f,1};c->ClearRenderTargetView(v,bg);ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());}
  td.Usage=D3D11_USAGE_STAGING;td.BindFlags=0;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ComPtr<ID3D11Texture2D> stage;if(FAILED(d->CreateTexture2D(&td,nullptr,&stage)))return 8;c->CopyResource(stage.Get(),color.Get());D3D11_MAPPED_SUBRESOURCE m{};if(FAILED(c->Map(stage.Get(),0,D3D11_MAP_READ,0,&m)))return 9;
  BITMAPFILEHEADER fh{};BITMAPINFOHEADER ih{};fh.bfType=0x4d42;fh.bfOffBits=sizeof(fh)+sizeof(ih);fh.bfSize=fh.bfOffBits+1400*1000*4;ih.biSize=sizeof(ih);ih.biWidth=1400;ih.biHeight=-1000;ih.biPlanes=1;ih.biBitCount=32;
  std::ofstream f(language?"E:\\DLSSNR\\reports\\D18_NR_HINTS_ZH_PREVIEW.bmp":"E:\\DLSSNR\\reports\\D18_NR_HINTS_EN_PREVIEW.bmp",std::ios::binary);f.write((char*)&fh,sizeof(fh));f.write((char*)&ih,sizeof(ih));for(unsigned y=0;y<1000;++y)f.write((const char*)m.pData+size_t(y)*m.RowPitch,1400*4);c->Unmap(stage.Get(),0);
 }
 D18Ui::chineseFontAvailable=false;D18Ui::SetLanguage(1);if(D18Ui::chinese)return 10;
 printf("PASS: %zu translations; %u non-ASCII glyph uses covered; stable widget IDs; English/Chinese render; missing-font fallback.\n",std::size(D18Ui::translations),glyphs);
 ImGui_ImplDX11_Shutdown();ImGui::DestroyContext();return 0;
}
