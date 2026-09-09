// Generated scroll-section.inc is the production ScopedCollapsingHeader class.
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <menu/D18Localization.h>
#include <d3d11.h>
#include <imgui/imgui_impl_dx11.h>
#include <cstdio>
#include <cstdlib>
#include "scroll-section.inc"
#define CHECK(x) do {if(!(x)){std::printf("FAIL line %d: %s\n",__LINE__,#x);std::exit(1);}} while(0)
static ImVec2 cardPoint, panelPoint;
static ImRect sliderRect;
static float value=0.25f;
static void section(const char* id,bool first=false) {
 ScopedCollapsingHeader header(id,ImGuiTreeNodeFlags_DefaultOpen);
 if(header.IsHeaderOpen()) {
  if(first) {
   ImGui::SetNextItemWidth(160);
   D18Ui::SliderFloat("Detail strength",&value,0,1);
   sliderRect={ImGui::GetItemRectMin(),ImGui::GetItemRectMax()};
   panelPoint=ImGui::GetCursorScreenPos();panelPoint.x+=20;panelPoint.y+=10;
  }
  for(int i=0;i<25;i++) ImGui::Text("Content %d",i);
 }
}
static void frame() {
 ImGui_ImplDX11_NewFrame();ImGui::NewFrame();
 ImGui::SetNextWindowPos({20,20});ImGui::SetNextWindowSize({600,500});
 ImGui::Begin("D18 scroll regression",nullptr,ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoCollapse);
 ImGui::BeginChild("status",{0,62},true,ImGuiWindowFlags_NoScrollWithMouse);
 ImGui::Text("Status");ImGui::EndChild();
 cardPoint=ImGui::GetItemRectMin();cardPoint.x+=40;cardPoint.y+=30;
 if(ImGui::BeginTable("main",2,ImGuiTableFlags_SizingStretchSame|ImGuiTableFlags_Resizable)) {
  ImGui::TableNextColumn();section("DLSS SR",true);section("DLSS FG");
  ImGui::TableNextColumn();section("DLSS NR");section("Hotkeys");ImGui::EndTable();
 }
 ImGui::End();ImGui::EndFrame();
}
int main(int argc,char** argv) {
 const float scale=argc>1?float(std::atof(argv[1])):1.f;
 const unsigned language=argc>2?unsigned(std::atoi(argv[2])):0;
 ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.LogFilename=nullptr;io.DisplaySize={1280,720};io.DeltaTime=1.f/60;
 ImGui::GetStyle().ScaleAllSizes(scale);D18Ui::chineseFontAvailable=true;D18Ui::SetLanguage(language);
 ID3D11Device* device=nullptr;ID3D11DeviceContext* context=nullptr;
 CHECK(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context)));
 CHECK(ImGui_ImplDX11_Init(device,context));
 for(int i=0;i<6;i++)frame();auto* window=ImGui::FindWindowByName("D18 scroll regression");CHECK(window->ScrollMax.y>0);
 io.AddMousePosEvent(cardPoint.x,cardPoint.y);frame();frame();io.AddMouseWheelEvent(0,-2);frame();frame();CHECK(window->Scroll.y>0);
 ImGui::SetScrollY(window,0);frame();frame();
 io.AddMousePosEvent(panelPoint.x,panelPoint.y);frame();frame();io.AddMouseWheelEvent(0,-2);frame();frame();CHECK(window->Scroll.y>0);
 ImGui::SetScrollY(window,0);frame();frame();
 const ImRect bar=ImGui::GetWindowScrollbarRect(window,ImGuiAxis_Y);const float x=(bar.Min.x+bar.Max.x)/2;
 io.AddMousePosEvent(x,bar.Min.y+15);frame();frame();io.AddMouseButtonEvent(0,true);frame();
 io.AddMousePosEvent(x,bar.Min.y+160);frame();frame();CHECK(window->Scroll.y>0);
 const float dragged=window->Scroll.y;frame();CHECK(window->Scroll.y==dragged);
 io.AddMouseButtonEvent(0,false);frame();frame();CHECK(window->Scroll.y==dragged);
 ImGui::SetScrollY(window,0);frame();frame();
 const float sy=(sliderRect.Min.y+sliderRect.Max.y)/2;
 io.AddMousePosEvent(sliderRect.Min.x+30,sy);frame();frame();io.AddMouseButtonEvent(0,true);frame();
 io.AddMousePosEvent(sliderRect.Min.x+140,sy);frame();frame();CHECK(value>0.7f);
 io.AddMouseButtonEvent(0,false);frame();frame();
 const auto id=window->ID;D18Ui::SetLanguage(1-language);frame();CHECK(window->ID==id);
 std::printf("PASS shared ImGui UI scale=%.1f language=%u: card/panel wheel, persistent scrollbar drag/release, slider drag, language switch\n",scale,language);
 ImGui_ImplDX11_Shutdown();ImGui::DestroyContext();context->Release();device->Release();
}
