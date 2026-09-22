#pragma once
#include <atomic>
namespace DlssNr::V8NativeStatus {
inline std::atomic<int> applied{0};
inline std::atomic<const char*> reason{"waiting_for_NR"};
}
