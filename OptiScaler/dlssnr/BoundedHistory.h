#pragma once
#include <array>
#include <cstddef>

namespace DlssNr {
template<class T, size_t Capacity> struct BoundedHistory {
    static_assert(Capacity>0);
    std::array<T,Capacity> values{};
    size_t next=0, count=0;
    void clear(){next=0;count=0;}
    size_t size() const{return count;}
    void push_back(const T& value){values[next]=value;next=(next+1)%Capacity;if(count<Capacity)++count;}
    const T& newest(size_t index) const{return values[(next+Capacity-1-index)%Capacity];}
};
}
