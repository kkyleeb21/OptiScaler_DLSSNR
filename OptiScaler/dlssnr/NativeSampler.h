#pragma once
#include <windows.h>
#include <cstdint>
#include <cstring>
namespace DlssNrNative::Sampler {
inline constexpr uintptr_t kLastSamplerPatchEnd=0x21DDC+6;
inline bool isSupportedRuntimeImage(HMODULE module) {
    if (module == nullptr) {
        return false;
    }

    const auto *base = reinterpret_cast<const unsigned char *>(module);
    MEMORY_BASIC_INFORMATION mapping {};
    if (VirtualQuery(base, &mapping, sizeof(mapping)) != sizeof(mapping) ||
        mapping.AllocationBase != module || mapping.State != MEM_COMMIT) {
        return false;
    }

    const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < sizeof(IMAGE_DOS_HEADER) ||
        dos->e_lfanew > 0x1000) {
        return false;
    }

    const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS64 *>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        nt->FileHeader.SizeOfOptionalHeader < sizeof(IMAGE_OPTIONAL_HEADER64) ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        nt->OptionalHeader.SizeOfHeaders <
            static_cast<DWORD>(dos->e_lfanew + sizeof(IMAGE_NT_HEADERS64)) ||
        nt->OptionalHeader.SizeOfImage < kLastSamplerPatchEnd) {
        return false;
    }

    return true;
}

// Runtime 310.8 normally selects POINT for both the network answer and the model Color input. These
// exact signatures make the two sampling experiments reversible without modifying the file on disk.
inline bool Apply(HMODULE module, bool linearResolve, bool linearColorInput) {
    if (!module) {
        return true;
    }

    struct SamplerPatch {
        uintptr_t rva;
        unsigned char point[6];
        unsigned char linear[6];
        size_t length;
        bool useLinear;
    };

    const SamplerPatch patches[] = {
        { 0x1CAF2, { 0x45, 0x8B, 0xCE, 0, 0, 0 }, { 0x45, 0x33, 0xC9, 0, 0, 0 }, 3,
          linearResolve },
        { 0x21DB4, { 0x41, 0xB9, 0x02, 0, 0, 0 }, { 0x41, 0xB9, 0, 0, 0, 0 }, 6,
          linearColorInput },
        { 0x21DDC, { 0x41, 0xB9, 0x02, 0, 0, 0 }, { 0x41, 0xB9, 0, 0, 0, 0 }, 6,
          linearColorInput },
    };

    auto *base = reinterpret_cast<unsigned char *>(module);
    if (!isSupportedRuntimeImage(module)) {
        return false;
    }

    struct PatchTransaction {
        unsigned char original[6] {};
        DWORD oldProtect = 0;
        bool changed = false;
        bool writable = false;
    };
    PatchTransaction transaction[_countof(patches)] {};

    // Validate every byte range and retain its exact starting state before making any page writable.
    for (const auto &patch : patches) {
        const auto *address = base + patch.rva;
        if (std::memcmp(address, patch.point, patch.length) != 0 &&
            std::memcmp(address, patch.linear, patch.length) != 0) {
            return false;
        }
    }

    for (size_t i = 0; i < _countof(patches); ++i) {
        const auto &patch = patches[i];
        auto *address = base + patch.rva;
        const auto *wanted = patch.useLinear ? patch.linear : patch.point;
        std::memcpy(transaction[i].original, address, patch.length);
        transaction[i].changed = std::memcmp(address, wanted, patch.length) != 0;
        if (!transaction[i].changed) {
            continue;
        }

        if (!VirtualProtect(address, patch.length, PAGE_EXECUTE_READWRITE,
                            &transaction[i].oldProtect)) {
            // No bytes have been written yet. Restore earlier pages in reverse order, including the
            // two Color sites that normally share a page and therefore nest protection changes.
            for (size_t j = i; j-- > 0;) {
                if (transaction[j].writable) {
                    DWORD ignored = 0;
                    VirtualProtect(base + patches[j].rva, patches[j].length,
                                   transaction[j].oldProtect, &ignored);
                }
            }
            return false;
        }
        transaction[i].writable = true;
    }

    bool committed = true;
    for (size_t i = 0; i < _countof(patches); ++i) {
        const auto &patch = patches[i];
        if (!transaction[i].changed) {
            continue;
        }
        auto *address = base + patch.rva;
        const auto *wanted = patch.useLinear ? patch.linear : patch.point;
        std::memcpy(address, wanted, patch.length);
        if (!FlushInstructionCache(GetCurrentProcess(), address, patch.length) ||
            std::memcmp(address, wanted, patch.length) != 0) {
            committed = false;
        }
    }

    if (!committed) {
        for (size_t i = 0; i < _countof(patches); ++i) {
            if (transaction[i].changed) {
                std::memcpy(base + patches[i].rva, transaction[i].original, patches[i].length);
                FlushInstructionCache(GetCurrentProcess(), base + patches[i].rva, patches[i].length);
            }
        }
    }

    bool protectionsRestored = true;
    for (size_t i = _countof(patches); i-- > 0;) {
        if (transaction[i].writable) {
            DWORD ignored = 0;
            if (!VirtualProtect(base + patches[i].rva, patches[i].length,
                                transaction[i].oldProtect, &ignored)) {
                protectionsRestored = false;
            }
        }
    }
    return committed && protectionsRestored;
}


}
