#pragma once
#include <cstdio>
#define LOG_ERROR(...) std::fprintf(stderr,"Vulkan shader error at %s:%d\n",__FILE__,__LINE__)
#define LOG_WARN(...) ((void)0)
#define LOG_INFO(...) ((void)0)
#define LOG_DEBUG(...) ((void)0)
