// Explicit owned-host observer. Hardware execution breakpoints; no code/data patches.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <stdexcept>
#include <cstdio>
#include <cstdint>
#include <sstream>
#include <algorithm>

static void need(bool ok,const char* what){if(!ok)throw std::runtime_error(what);}
static std::wstring quote(const std::wstring& s){
    std::wstring out=L"\"";unsigned slashes=0;
    for(wchar_t c:s){if(c==L'\\'){++slashes;continue;}if(c==L'\"')out.append(slashes*2+1,L'\\');else out.append(slashes,L'\\');slashes=0;out+=c;}
    out.append(slashes*2,L'\\');return out+L"\"";
}
template<class T> static T read(HANDLE p,ULONG64 at){T value{};SIZE_T done=0;need(ReadProcessMemory(p,reinterpret_cast<void*>(at),&value,sizeof(value),&done)&&done==sizeof(value),"remote read");return value;}
static std::string header(HANDLE p,ULONG64 at){
    unsigned char raw[256]{};SIZE_T done=0;
    if(!at||!ReadProcessMemory(p,reinterpret_cast<void*>(at),raw,sizeof(raw),&done)||done!=sizeof(raw))return "unavailable";
    const char hex[]="0123456789abcdef";std::string out;out.reserve(512);for(auto v:raw){out+=hex[v>>4];out+=hex[v&15];}return out;
}
int wmain(int argc,wchar_t** argv){
    PROCESS_INFORMATION pi{};bool exited=false;std::ofstream log;
    try{
        need(argc>=6,"state-output runtime phase owned-host args...");
        const std::wstring phase=argv[3];need(phase==L"controls"||phase==L"state"||phase==L"bindings"||phase==L"resources","observer phase");const bool statePhase=phase==L"state",bindingsPhase=phase==L"bindings",resourcesPhase=phase==L"resources";std::vector<ULONG64> states;
        const std::filesystem::path exe=std::filesystem::absolute(argv[4]);
        need(exe.filename()==L"probe-reset-boundary-runtime.exe","Only explicit boundary host supported");
        const auto runtime=std::filesystem::weakly_canonical(argv[2]);
        need(!std::filesystem::exists(argv[1]),"New state output required");log.open(std::filesystem::path(argv[1]));need(log.good(),"state output");
        std::wstring command=quote(exe.wstring());for(int i=5;i<argc;++i)command+=L" "+quote(argv[i]);
        STARTUPINFOW si{};si.cb=sizeof(si);si.dwFlags=STARTF_USESTDHANDLES;si.hStdOutput=GetStdHandle(STD_OUTPUT_HANDLE);si.hStdError=GetStdHandle(STD_ERROR_HANDLE);si.hStdInput=GetStdHandle(STD_INPUT_HANDLE);
        need(CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,TRUE,DEBUG_ONLY_THIS_PROCESS|CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi)!=FALSE,"owned host create");
        need(DebugSetProcessKillOnExit(TRUE)!=FALSE,"debug kill on exit");
        const auto start=GetTickCount64();ULONG64 base=0,imageSize=0;unsigned hits=0,controlCalls=0,gateCalls=0,virtualCalls=0,afterCalls=0;bool initialBreak=false;
        const ULONG64 rvas[]={0x179d0,resourcesPhase?0x21bb0ull:bindingsPhase?0x60cf0ull:0x18eacull,resourcesPhase?0x3f490ull:bindingsPhase?0x60e41ull:statePhase?0x616e0ull:0x22d10ull,resourcesPhase?0x2292dull:bindingsPhase?0x61026ull:0x18ebcull};
        log<<"{\"schema\":\"d18-nr-state-observer-v3\",\"event\":\"start\",\"pid\":"<<pi.dwProcessId<<",\"thread\":"<<pi.dwThreadId<<",\"resources_phase\":"<<int(resourcesPhase)<<",\"bindings_phase\":"<<int(bindingsPhase)<<",\"state_phase\":"<<int(statePhase)<<",\"code_modified\":false,\"coverage\":\"owned host main thread only\",\"max_hits\":768}\n";
        while(!exited){
            need(GetTickCount64()-start<60000,"debug host 60 second timeout");
            DEBUG_EVENT e{};if(!WaitForDebugEvent(&e,1000)){need(GetLastError()==ERROR_SEM_TIMEOUT,"wait debug event");continue;}
            DWORD status=DBG_CONTINUE;
            if(e.dwDebugEventCode==CREATE_PROCESS_DEBUG_EVENT){if(e.u.CreateProcessInfo.hFile)CloseHandle(e.u.CreateProcessInfo.hFile);}
            else if(e.dwDebugEventCode==LOAD_DLL_DEBUG_EVENT){
                auto& d=e.u.LoadDll;wchar_t path[32768]{};DWORD n=d.hFile?GetFinalPathNameByHandleW(d.hFile,path,32768,FILE_NAME_NORMALIZED):0;
                bool match=false;if(n&&n<32768){std::error_code ec;match=std::filesystem::equivalent(runtime,std::filesystem::path(path),ec)&&!ec;}
                if(d.hFile)CloseHandle(d.hFile);
                if(match){
                    need(!base,"runtime loaded once");base=reinterpret_cast<ULONG64>(d.lpBaseOfDll);
                    const auto dos=read<IMAGE_DOS_HEADER>(pi.hProcess,base);need(dos.e_magic==IMAGE_DOS_SIGNATURE&&dos.e_lfanew>0,"runtime DOS header");
                    const auto nt=read<IMAGE_NT_HEADERS64>(pi.hProcess,base+ULONG64(dos.e_lfanew));need(nt.Signature==IMAGE_NT_SIGNATURE&&nt.FileHeader.Machine==IMAGE_FILE_MACHINE_AMD64,"runtime NT header");imageSize=nt.OptionalHeader.SizeOfImage;
                    unsigned char expected[][6]={{0x40,0x53,0x55,0x56,0x57,0x41},{0x39,0xb5,0x20,0x01,0x00,0x00},{0x48,0xff,0x25,0x11,0x97,0x08},{0x40,0x88,0xb5,0x80,0x01,0x00}};
                    if(statePhase){const unsigned char op[]={0x48,0x8b,0x81,0x78,0x01,0x00};for(unsigned j=0;j<6;++j)expected[2][j]=op[j];}
                    if(bindingsPhase){const unsigned char op[3][6]={{0x4c,0x8b,0xdc,0x55,0x53,0x56},{0x49,0x8b,0x83,0xa8,0x00,0x00},{0xff,0x15,0xfc,0xb3,0x04,0x00}};for(unsigned k=1;k<4;++k)for(unsigned j=0;j<6;++j)expected[k][j]=op[k-1][j];}
                    if(resourcesPhase){const unsigned char op[3][6]={{0x48,0x8b,0xc4,0x53,0x56,0x57},{0x40,0x53,0x55,0x57,0x41,0x55},{0xe8,0x1e,0x9d,0xff,0xff,0x41}};for(unsigned k=1;k<4;++k)for(unsigned j=0;j<6;++j)expected[k][j]=op[k-1][j];}
                    for(unsigned k=0;k<4;++k)for(unsigned j=0;j<6;++j)need(read<unsigned char>(pi.hProcess,base+rvas[k]+j)==expected[k][j],"runtime breakpoint bytes mismatch");
                    CONTEXT c{};c.ContextFlags=CONTEXT_DEBUG_REGISTERS;need(GetThreadContext(pi.hThread,&c)!=FALSE,"get main debug context");need((c.Dr7&0xff)==0,"preexisting hardware breakpoints");
                    c.Dr0=base+rvas[0];c.Dr1=base+rvas[1];c.Dr2=base+rvas[2];c.Dr3=base+rvas[3];c.Dr6=0;c.Dr7=0x55;
                    need(SetThreadContext(pi.hThread,&c)!=FALSE,"set execution breakpoints");log<<"{\"event\":\"armed\",\"runtime_base\":"<<base<<"}\n";
                }
            }else if(e.dwDebugEventCode==EXCEPTION_DEBUG_EVENT){
                const auto code=e.u.Exception.ExceptionRecord.ExceptionCode;
                if(code==EXCEPTION_BREAKPOINT&&!initialBreak){initialBreak=true;}
                else if(code==EXCEPTION_SINGLE_STEP&&base&&e.dwThreadId==pi.dwThreadId){
                    CONTEXT c{};c.ContextFlags=CONTEXT_ALL;need(GetThreadContext(pi.hThread,&c)!=FALSE,"get hit context");int point=-1;
                    for(int k=0;k<4;++k)if(c.Rip==base+rvas[k]&&(c.Dr6&(1ull<<k)))point=k;
                    if(point<0)status=DBG_EXCEPTION_NOT_HANDLED;
                    else{
                        need(++hits<=768,"breakpoint hit budget");
                        std::ostringstream event;
                        event<<"{\"event\":\"hit\",\"point\":"<<point<<",\"ordinal\":"<<hits;
                        if(point==0){
                            ++controlCalls;need(controlCalls<=128,"control call budget");
                            event<<",\"call\":"<<controlCalls-1<<",\"feature\":"<<c.Rcx<<",\"context\":"<<c.Rdx<<",\"reset_before_control\":"<<read<unsigned>(pi.hProcess,c.R8+0x100)<<",\"control_gate\":"<<unsigned(read<unsigned char>(pi.hProcess,c.Rcx+0x68))<<",\"prior_control_valid\":"<<unsigned(read<unsigned char>(pi.hProcess,c.Rcx+0x1f8));
                            event<<",\"controls_bits\":[";const unsigned offsets[]={0xe4,0xe8,0xec,0xf0,0xf4,0xf8,0xfc};for(unsigned k=0;k<7;++k){if(k)event<<',';event<<read<unsigned>(pi.hProcess,c.R8+offsets[k]);}event<<']';
                        }else if(resourcesPhase){
                            event<<",\"call\":"<<controlCalls-1;
                            if(point==1){++gateCalls;need(gateCalls==controlCalls,"resource entry pairing");event<<",\"feature\":"<<c.R9<<",\"history_resource\":"<<read<ULONG64>(pi.hProcess,c.R9+0x60)<<",\"history_valid\":"<<unsigned(read<unsigned char>(pi.hProcess,c.R9+0x68))<<",\"reset\":"<<read<unsigned>(pi.hProcess,c.R8+0x100)<<",\"resources\":[";const unsigned offsets[]={0,0x18,0x30,0x48};for(unsigned k=0;k<4;++k){if(k)event<<',';event<<read<ULONG64>(pi.hProcess,c.R8+offsets[k]);}event<<']';
                            }else if(point==2){++virtualCalls;need(virtualCalls==gateCalls,"resource setter pairing");event<<",\"network\":"<<c.Rcx<<",\"history_handle\":"<<c.Rdx<<",\"mv_handle\":"<<c.R8<<",\"optional_handle\":"<<c.R9<<",\"input_gate\":"<<unsigned(read<unsigned char>(pi.hProcess,base+0xe06d0));
                            }else{++afterCalls;need(afterCalls==gateCalls,"history copy pairing");event<<",\"feature\":"<<c.R12<<",\"history_resource\":"<<read<ULONG64>(pi.hProcess,c.R12+0x60)<<",\"selected_output_resource\":"<<read<ULONG64>(pi.hProcess,c.Rsp+0x98)<<",\"selected_output_handle\":"<<read<ULONG64>(pi.hProcess,c.Rsp+0xd8)<<",\"source_resource\":"<<read<ULONG64>(pi.hProcess,c.R9)<<",\"destination_resource\":"<<read<ULONG64>(pi.hProcess,read<ULONG64>(pi.hProcess,c.Rsp+0x20))<<",\"copy_from_temporary\":"<<unsigned(read<unsigned char>(pi.hProcess,c.Rsp+0x90));}
                        }else if(bindingsPhase){
                            event<<",\"call\":"<<controlCalls-1;
                            if(point==1){++gateCalls;need(gateCalls==controlCalls,"consumer entry pairing");
                                const auto state=read<ULONG64>(pi.hProcess,c.Rcx+0x178);event<<",\"layer\":"<<c.Rcx<<",\"state\":"<<state<<",\"counter\":"<<read<ULONG64>(pi.hProcess,state+0xc8)<<",\"return_address\":"<<read<ULONG64>(pi.hProcess,c.Rsp);
                                event<<",\"block\":"<<c.R15<<",\"layer_index\":"<<c.R13<<",\"block_caller\":"<<read<ULONG64>(pi.hProcess,c.Rbp+0xb8);
                                const auto descriptor=read<ULONG64>(pi.hProcess,c.R15+0x28)+c.R13*0x170;
                                const auto blockInputs=read<ULONG64>(pi.hProcess,c.Rbp-0x78);
                                const ULONG64 vectors[]={blockInputs,descriptor+0x90,descriptor+0xa8,descriptor+0x108,descriptor+0x120,descriptor+0x138};
                                const char* names[]={"block_inputs","input_layers","input_blocks","completion_layers","completion_outputs","completion_blocks"};
                                for(unsigned k=0;k<6;++k){const auto begin=read<ULONG64>(pi.hProcess,vectors[k]),end=read<ULONG64>(pi.hProcess,vectors[k]+8);need(end>=begin&&(end-begin)%8==0&&(end-begin)/8<=16,"metadata vector limit");event<<",\""<<names[k]<<"\":[";for(auto at=begin;at<end;at+=8){if(at!=begin)event<<',';event<<read<ULONG64>(pi.hProcess,at);}event<<']';}
                                for(int k=0;k<2;++k){const auto vector=k?c.R8:c.Rdx;const auto begin=read<ULONG64>(pi.hProcess,vector),end=read<ULONG64>(pi.hProcess,vector+8);need(end>=begin&&(end-begin)%8==0&&(end-begin)/8<=16,"binding vector limit");event<<(k?",\"outputs\":[":",\"inputs\":[");for(ULONG64 at=begin;at<end;at+=8){if(at!=begin)event<<',';event<<read<ULONG64>(pi.hProcess,at);}event<<']';}
                            }else if(point==2){++virtualCalls;need(virtualCalls==gateCalls,"consumer gate pairing");event<<",\"state\":"<<c.R11<<",\"counter\":"<<read<ULONG64>(pi.hProcess,c.R11+0xc8)<<",\"gated_inputs\":["<<read<ULONG64>(pi.hProcess,c.Rsp+0x70)<<','<<read<ULONG64>(pi.hProcess,c.Rsp+0x78)<<','<<read<ULONG64>(pi.hProcess,c.Rbp-0x80)<<','<<read<ULONG64>(pi.hProcess,c.Rbp-0x78)<<']';
                            }else{++afterCalls;need(afterCalls==gateCalls,"consumer launch pairing");event<<",\"dispatch_target\":"<<c.Rax<<",\"dispatch_target_rva\":"<<(c.Rax>=base&&c.Rax<base+imageSize?c.Rax-base:0)<<",\"kernel_handle\":"<<c.Rdx<<",\"argument_counter\":"<<read<ULONG64>(pi.hProcess,c.Rbp+0x38)<<",\"argument_words\":[";for(unsigned k=0;k<33;++k){if(k)event<<',';event<<read<ULONG64>(pi.hProcess,c.Rsp+0x70+8*k);}event<<']';}
                        }else if(point==1){++gateCalls;need(gateCalls==controlCalls,"control/gate pairing");event<<",\"call\":"<<controlCalls-1<<",\"effective_reset_at_gate\":"<<read<unsigned>(pi.hProcess,c.Rbp+0x120);}
                        else if(point==2&&statePhase){++virtualCalls;const auto state=read<ULONG64>(pi.hProcess,c.Rcx+0x178);
                            if(std::find(states.begin(),states.end(),state)==states.end()){need(states.size()<64,"state pointer budget");states.push_back(state);}
                            event<<",\"call\":"<<(controlCalls?int(controlCalls)-1:-1)<<",\"holder\":"<<c.Rcx<<",\"state\":"<<state<<",\"value_before_clear\":"<<read<ULONG64>(pi.hProcess,state+0xc8)<<",\"return_address\":"<<read<ULONG64>(pi.hProcess,c.Rsp);
                        }
                        else if(point==2){++virtualCalls;const auto vtable=read<ULONG64>(pi.hProcess,c.Rcx);need(read<ULONG64>(pi.hProcess,vtable+0x48)==c.Rax,"virtual target mismatch");event<<",\"call\":"<<(controlCalls?int(controlCalls)-1:-1)<<",\"return_address\":"<<read<ULONG64>(pi.hProcess,c.Rsp)<<",\"model\":"<<c.Rcx<<",\"vtable\":"<<vtable<<",\"target\":"<<c.Rax<<",\"target_rva_if_in_runtime\":"<<(c.Rax>=base&&c.Rax<base+imageSize?c.Rax-base:0)<<",\"header_before_reset\":\""<<header(pi.hProcess,c.Rcx)<<'"';}
                        else{++afterCalls;need(afterCalls==gateCalls,"gate/after pairing");const auto model=read<ULONG64>(pi.hProcess,c.Rbx+0x48);event<<",\"call\":"<<controlCalls-1<<",\"model\":"<<model<<",\"header_after_gate\":\""<<header(pi.hProcess,model)<<'"';}
                        if(statePhase&&(point==0||point==3)){event<<",\"states\":[";for(size_t i=0;i<states.size();++i){if(i)event<<',';event<<"{\"address\":"<<states[i]<<",\"value\":"<<read<ULONG64>(pi.hProcess,states[i]+0xc8)<<'}';}event<<']';}
                        event<<"}\n";log<<event.str();log.flush();need(log.good(),"state log write");c.Dr6=0;c.EFlags|=0x10000;need(SetThreadContext(pi.hThread,&c)!=FALSE,"resume hardware breakpoint");
                    }
                }else{status=DBG_EXCEPTION_NOT_HANDLED;log<<"{\"event\":\"exception\",\"code\":"<<code<<",\"first_chance\":"<<e.u.Exception.dwFirstChance<<"}\n";}
            }else if(e.dwDebugEventCode==EXIT_PROCESS_DEBUG_EVENT){
                exited=true;log<<"{\"event\":\"exit\",\"code\":"<<e.u.ExitProcess.dwExitCode<<",\"control_calls\":"<<controlCalls<<",\"gate_calls\":"<<gateCalls<<(resourcesPhase?",\"resource_setter_calls\":":bindingsPhase?",\"consumer_gate_calls\":":statePhase?",\"state_clear_calls\":":",\"virtual_reset_calls\":")<<virtualCalls<<",\"after_calls\":"<<afterCalls<<",\"hits\":"<<hits<<"}\n";log.flush();
                need(e.u.ExitProcess.dwExitCode==0&&controlCalls==128&&gateCalls==128&&afterCalls==128,"host exit or coverage incomplete");
            }
            need(ContinueDebugEvent(e.dwProcessId,e.dwThreadId,status)!=FALSE,"continue debug event");
        }
        CloseHandle(pi.hThread);CloseHandle(pi.hProcess);puts("PASS owned-host state observation; debugger perturbs scheduling");return 0;
    }catch(const std::exception& e){
        if(pi.hProcess&&!exited){TerminateProcess(pi.hProcess,91);WaitForSingleObject(pi.hProcess,5000);}
        if(pi.hThread)CloseHandle(pi.hThread);if(pi.hProcess)CloseHandle(pi.hProcess);
        if(log.is_open())log<<"{\"event\":\"observer_failed\"}\n";
        printf("FAIL observer %s win32=%lu\n",e.what(),GetLastError());return 1;
    }
}
