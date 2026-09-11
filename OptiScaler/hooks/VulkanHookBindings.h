#pragma once
#include <Windows.h>
#include <array>
#include "../include/detours/detours.h"
#include "HookTransaction.h"

struct VulkanHookBinding
{
    PVOID* original{};
    PVOID replacement{};
    bool installed = false;
    PVOID entry{}; // The address before Detours replaces the original slot with its trampoline.
};

// Use when filling an owned runtime's native dispatch table, not on every frame.
// The caller must stop that runtime before detaching these hooks: a cached
// trampoline is only valid while its Detours attachment remains alive.
template<size_t N> inline PVOID ResolveVulkanOriginal(const std::array<VulkanHookBinding,N>& bindings, PVOID resolved)
{
    if (!resolved) return nullptr;
    for (const auto& binding : bindings)
        if (binding.installed && binding.entry == resolved && binding.original && *binding.original)
            return *binding.original;
    return resolved;
}

template<size_t N> inline HookLifecycle::Result ChangeVulkanBindings(std::array<VulkanHookBinding,N>& bindings,bool attach)
{
    if (attach)
        for (auto& binding : bindings)
            if (binding.installed && binding.original) binding.entry = *binding.original;
    return HookLifecycle::Transact(
        []{return DetourTransactionBegin();},
        []{return DetourUpdateThread(GetCurrentThread());},
        [&]() -> LONG {
            for(auto& binding:bindings){
                if(!binding.installed)continue;
                const LONG result=attach?DetourAttach(binding.original,binding.replacement):DetourDetach(binding.original,binding.replacement);
                if(result!=NO_ERROR)return result;
            }
            return NO_ERROR;
        },
        []{return DetourTransactionCommit();},[]{DetourTransactionAbort();});
}
