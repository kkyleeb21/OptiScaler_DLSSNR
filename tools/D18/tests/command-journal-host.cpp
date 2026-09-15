#include <dlssnr/Dx11CommandListTrace.h>
#include <filesystem>
#include <fstream>
#pragma comment(lib,"d3d11.lib")
#pragma comment(lib,"dxgi.lib")
int wmain(int argc,wchar_t** argv){
 if(argc!=2)return 2;
 const std::filesystem::path root(argv[1]);std::filesystem::create_directories(root);
 namespace M=DlssNr::Dx11CommandListWrites;
 std::ofstream(root/L"D18ResearchCapture.enabled") << "CPU fixture explicit research opt-in";
 M::researchTrace=DlssNr::BuildProfile::ResearchCaptureRequested(root);
 auto& journal=DlssNr::Dx11CommandListTrace::journal;
 ++M::lists;M::List list;list.writes.reasons=0;
 M::extendedTrace=true;
 for(UINT i=0;i<4096;++i){list.writes.CountOperation();if(!list.writes.Trace(13))return 3;}
 for(UINT64 frame:{1ULL,2ULL,3ULL,1001ULL}){
  journal.Begin(root,frame,nullptr);
  for(UINT i=0;i<120;++i)journal.Execute(&list,0,FALSE,nullptr);
  journal.Mark(nullptr,"sr_private_seed");
  const UINT count=frame==3?4u:1u;
  for(UINT i=0;i<count;++i)journal.Execute(&list,2,FALSE,nullptr);
  journal.Mark(nullptr,"ui_begin");journal.Mark(nullptr,"ui_end");
  journal.Mark(nullptr,"present_before");journal.Mark(nullptr,"present_after");
  journal.End(frame,true);
 }
 auto& conflicts=DlssNr::Dx11CommandListTrace::conflicts;
 conflicts.Capture(root,20,nullptr,&list,2,FALSE,nullptr);
 if(conflicts.captures||M::ExtendedTrace())return 4;
 ++M::lists;M::List shortList;shortList.writes.reasons=0;shortList.writes.operations=504;shortList.writes.draws=2;shortList.writes.dispatches=1;
 for(UINT i=0;i<512;++i)if(!shortList.writes.Trace(13))return 5;
 if(shortList.writes.Trace(13))return 6;
 conflicts.Capture(root,30,nullptr,&shortList,2,FALSE,nullptr);
 if(conflicts.captures!=1||!M::ExtendedTrace())return 7;
 shortList.writes.traceDropped=0;
 for(UINT i=512;i<1500;++i)if(!shortList.writes.Trace(13))return 8;
 conflicts.Capture(root,31,nullptr,&shortList,2,FALSE,nullptr);
 conflicts.Capture(root,32,nullptr,&shortList,2,FALSE,nullptr);
 conflicts.Capture(root,33,nullptr,&shortList,2,FALSE,nullptr);
 if(conflicts.captures!=3||M::ExtendedTrace())return 9;
 M::conflictTraceUntil=GetTickCount64()-1;if(M::ExtendedTrace())return 10;
 return 0;
}
