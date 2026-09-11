#define SPDLOG_HEADER_ONLY
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/logger.h>
#include <filesystem>
#include <cassert>
#include <iostream>
int main(){
 auto dir=std::filesystem::path("rotation-fixture");std::filesystem::create_directories(dir);
 auto sink=std::make_shared<spdlog::sinks::rotating_file_sink_mt>((dir/"OptiScaler.log").string(),16u*1024u*1024u,2,true);
 spdlog::logger logger("test",sink);logger.set_pattern("%v");logger.flush_on(spdlog::level::warn);
 for(int i=0;i<16000;++i)logger.info("{}",std::string(4096,'x'));
 logger.warn("retained_warning");logger.flush();
 size_t count=0;uintmax_t total=0;
 for(auto& e:std::filesystem::directory_iterator(dir)){++count;total+=e.file_size();assert(e.file_size()<=16u*1024u*1024u);}
 assert(count==3&&total<=48u*1024u*1024u);
 std::cout<<"rotating log PASS: files="<<count<<" bytes="<<total<<"\n";
}
