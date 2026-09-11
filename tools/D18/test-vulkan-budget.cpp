#include "../../OptiScaler/dlssnr/BoundedDiagnosticFile.h"
#include "../../OptiScaler/dlssnr/BoundedHistory.h"
#include <cassert>
#include <string>
int main(){
    DlssNr::BoundedHistory<int,512> history;
    for(int i=0;i<10000;++i)history.push_back(i);
    assert(history.size()==512);
    for(size_t i=0;i<512;++i)assert(history.newest(i)==9999-int(i));
    history.clear();assert(history.size()==0);history.push_back(42);assert(history.newest(0)==42);
    FILE* file=nullptr;assert(fopen_s(&file,"vulkan-budget-fixture.log","w+b")==0);
    DlssNr::BoundedDiagnosticFile budget;
    const std::string line(73,'x');
    for(int i=0;i<1000;++i)budget.Write(file,line.data(),line.size(),uint64_t(i),1024);
    assert(budget.closed && budget.bytes<=1024);
    fflush(file);assert(_ftelli64(file)==static_cast<long long>(budget.bytes));
    rewind(file);char contents[1025]{};const auto size=fread(contents,1,1024,file);
    const std::string output(contents,size);
    const auto marker=output.find(DlssNr::BoundedDiagnosticFile::marker);
    assert(marker!=std::string::npos && output.find(DlssNr::BoundedDiagnosticFile::marker,marker+1)==std::string::npos);
    fclose(file);
    puts("PASS: 10000 history inserts retain newest 512 in order; binary log bounded with one terminal marker");
}
