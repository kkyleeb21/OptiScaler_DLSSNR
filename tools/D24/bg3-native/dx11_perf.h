#pragma once
// CPU-side timing, not GPU execution time. A sampled window never includes
// pixel capture; it is reset when diagnostics, mode or capture changes.
struct NativePerf {
 bool enabled=false;unsigned frames=0,mode=0;
 unsigned long long polls=0,srv=0,uav=0,waits[3]{};
 double waitUs[3]{},totalUs=0,maxFrameUs=0;
};
static long long perfTick(){LARGE_INTEGER t;QueryPerformanceCounter(&t);return t.QuadPart;}
static double perfUs(long long start){
 static const double frequency=[](){LARGE_INTEGER f;QueryPerformanceFrequency(&f);return double(f.QuadPart);}();
 return double(perfTick()-start)*1000000.0/frequency;
}
static void reportPerf(NativePerf& p,int result,unsigned allocations,size_t resident,size_t inflight){
 if(!logFile)return;
 logPrint(logFile,"{\"event\":\"dx11_performance\",\"scope\":\"cpu_process_window\",\"mode\":%u,\"frames\":%u,\"last_result\":%d,\"total_us\":%.3f,\"max_frame_us\":%.3f,\"model_wait_us\":%.3f,\"compose_wait_us\":%.3f,\"other_wait_us\":%.3f,\"model_waits\":%llu,\"compose_waits\":%llu,\"other_waits\":%llu,\"polls\":%llu,\"srv_attempts\":%llu,\"uav_attempts\":%llu,\"exposure_allocation_attempts_session\":%u,\"resident_resources\":%zu,\"inflight_resources\":%zu,\"log_bytes\":%llu,\"log_dropped_writes\":%llu,\"pixel_capture\":false}\n",
 p.mode,p.frames,result,p.totalUs,p.maxFrameUs,p.waitUs[0],p.waitUs[1],p.waitUs[2],p.waits[0],p.waits[1],p.waits[2],p.polls,p.srv,p.uav,allocations,resident,inflight,nativeLogBytes,nativeLogDropped);
 fflush(logFile);
}
