#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <map>
#pragma comment(lib,"user32.lib")
#pragma comment(lib,"shell32.lib")
static std::wstring quote(const std::wstring& text) {
    std::wstring out=L"\"";size_t slashes=0;
    for(wchar_t ch:text){if(ch==L'\\'){++slashes;continue;}out.append(slashes*(ch==L'"'?2:1),L'\\');slashes=0;if(ch==L'"')out+=L'\\';out+=ch;}
    out.append(slashes*2,L'\\');return out+L'"';
}
struct EnvironmentKeyLess {bool operator()(const std::wstring& a,const std::wstring& b)const{return CompareStringOrdinal(a.c_str(),-1,b.c_str(),-1,TRUE)==CSTR_LESS_THAN;}};
static std::vector<wchar_t> environment(){
    std::map<std::wstring,std::wstring,EnvironmentKeyLess> values;
    auto block=GetEnvironmentStringsW();
    if(block){for(auto p=block;*p;p+=wcslen(p)+1){std::wstring entry=p;auto split=entry.find(L'=',entry[0]==L'='?1:0);if(split!=std::wstring::npos)values[entry.substr(0,split)]=entry.substr(split+1);}FreeEnvironmentStringsW(block);}
    std::vector<wchar_t> result;for(const auto& v:values){auto entry=v.first+L"="+v.second;result.insert(result.end(),entry.begin(),entry.end());result.push_back(0);}result.push_back(0);return result;
}
int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int){
    wchar_t module[32768]{},system[32768]{};
    if(!GetModuleFileNameW(nullptr,module,32768)||!GetSystemDirectoryW(system,32768))return 2;
    std::wstring root=module;root.resize(root.find_last_of(L"\\/"));
    const auto data=root+L"\\D18",script=data+L"\\D18-Setup.ps1";
    if(GetFileAttributesW(script.c_str())==INVALID_FILE_ATTRIBUTES){MessageBoxW(nullptr,L"Keep the D18 folder beside this EXE. Extract the complete ZIP first.\n\n请先完整解压，并将 D18 文件夹与此 EXE 放在一起。",L"D18 Setup",MB_OK|MB_ICONERROR);return 3;}
    const std::wstring exe=std::wstring(system)+L"\\WindowsPowerShell\\v1.0\\powershell.exe";
    std::wstring command=quote(exe)+L" -NoProfile -STA -ExecutionPolicy Bypass -File "+quote(script);
#ifdef D18_UNINSTALL
    command+=L" -UninstallMode";
#endif
    int argc=0;auto argv=CommandLineToArgvW(GetCommandLineW(),&argc);bool wait=false;std::wstring testDirectory;
    for(int i=1;i<argc;++i){
        std::wstring key=argv[i],parameter;
        if(key==L"--preview")parameter=L"PreviewDirectory";
        else if(key==L"--smoke")parameter=L"UiSmokeDirectory";
        else if(key==L"--runtime")parameter=L"SmokeRuntime";
        else if(key==L"--language")parameter=L"Language";
        else {LocalFree(argv);return 4;}
        if(++i>=argc){LocalFree(argv);return 4;}
        command+=L" -"+parameter+L" "+quote(argv[i]);
        if(key==L"--preview"||key==L"--smoke"){wait=true;testDirectory=argv[i];}
    }
    LocalFree(argv);
    // CREATE_NO_WINDOW suppresses the console without hiding the first WinForms window.
    STARTUPINFOW si{};si.cb=sizeof(si);PROCESS_INFORMATION pi{};
    HANDLE log=INVALID_HANDLE_VALUE,input=INVALID_HANDLE_VALUE;
    if(!wait){wchar_t temp[32768]{};if(GetTempPathW(32768,temp))testDirectory=std::wstring(temp)+L"D18-Launcher-"+std::to_wstring(GetCurrentProcessId());}
    if(!testDirectory.empty()){
        CreateDirectoryW(testDirectory.c_str(),nullptr);SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE};
        log=CreateFileW((testDirectory+L"\\launcher.log").c_str(),GENERIC_WRITE,FILE_SHARE_READ,&sa,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        input=CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&sa,OPEN_EXISTING,0,nullptr);
        if(log!=INVALID_HANDLE_VALUE&&input!=INVALID_HANDLE_VALUE){si.dwFlags|=STARTF_USESTDHANDLES;si.hStdOutput=log;si.hStdError=log;si.hStdInput=input;}
    }
    std::vector<wchar_t> writable(command.begin(),command.end());writable.push_back(0);
    auto childEnvironment=environment();
    if(!CreateProcessW(exe.c_str(),writable.data(),nullptr,nullptr,(si.dwFlags&STARTF_USESTDHANDLES)?TRUE:FALSE,CREATE_NO_WINDOW|CREATE_UNICODE_ENVIRONMENT,childEnvironment.data(),data.c_str(),&si,&pi)){
        MessageBoxW(nullptr,L"Unable to start Windows PowerShell.\n无法启动 Windows PowerShell。",L"D18 Setup",MB_OK|MB_ICONERROR);return 5;
    }
    if(log!=INVALID_HANDLE_VALUE)CloseHandle(log);if(input!=INVALID_HANDLE_VALUE)CloseHandle(input);
    CloseHandle(pi.hThread);DWORD code=0;WaitForSingleObject(pi.hProcess,INFINITE);GetExitCodeProcess(pi.hProcess,&code);CloseHandle(pi.hProcess);
    if(code&&!wait){auto message=L"D18 setup exited with an error. Log / 启动失败，日志：\n"+testDirectory+L"\\launcher.log";MessageBoxW(nullptr,message.c_str(),L"D18 Setup",MB_OK|MB_ICONERROR);}
    return static_cast<int>(code);
}
