#pragma once
#include "nvsdk_ngx_params.h"
#include <map>
#include <string>
class ProbeParameters final : public NVSDK_NGX_Parameter {
    std::map<std::string,double> numbers;
    std::map<std::string,void*> pointers;
    template<class T> NVSDK_NGX_Result number(const char* n,T* v) const {auto i=numbers.find(n);if(i==numbers.end())return NVSDK_NGX_Result_FAIL_InvalidParameter;*v=static_cast<T>(i->second);return NVSDK_NGX_Result_Success;}
    template<class T> NVSDK_NGX_Result pointer(const char* n,T** v) const {auto i=pointers.find(n);if(i==pointers.end())return NVSDK_NGX_Result_FAIL_InvalidParameter;*v=static_cast<T*>(i->second);return NVSDK_NGX_Result_Success;}
public:
    void Set(const char* n,unsigned long long v) override {numbers[n]=static_cast<double>(v);}
    void Set(const char* n,float v) override {numbers[n]=v;}
    void Set(const char* n,double v) override {numbers[n]=v;}
    void Set(const char* n,unsigned v) override {numbers[n]=v;}
    void Set(const char* n,int v) override {numbers[n]=v;}
    void Set(const char* n,ID3D11Resource* v) override {pointers[n]=v;}
    void Set(const char* n,ID3D12Resource* v) override {pointers[n]=v;}
    void Set(const char* n,void* v) override {pointers[n]=v;}
    NVSDK_NGX_Result Get(const char* n,unsigned long long* v) const override {return number(n,v);}
    NVSDK_NGX_Result Get(const char* n,float* v) const override {return number(n,v);}
    NVSDK_NGX_Result Get(const char* n,double* v) const override {return number(n,v);}
    NVSDK_NGX_Result Get(const char* n,unsigned* v) const override {return number(n,v);}
    NVSDK_NGX_Result Get(const char* n,int* v) const override {return number(n,v);}
    NVSDK_NGX_Result Get(const char* n,ID3D11Resource** v) const override {return pointer(n,v);}
    NVSDK_NGX_Result Get(const char* n,ID3D12Resource** v) const override {return pointer(n,v);}
    NVSDK_NGX_Result Get(const char* n,void** v) const override {return pointer(n,v);}
    void Reset() override {numbers.clear();pointers.clear();}
};
