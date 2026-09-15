#pragma once
// Exact engine revision adapter. These executable fragments contain no absolute
// relocated pointers; this is not a claim of support for another GRW build.
namespace DlssNr::WildlandsScaleProfile {
struct Fragment {unsigned rva,size;unsigned long long hash;};
inline constexpr unsigned TimeStamp=0x6a7c5143,ImageSize=0x18b09000;
inline constexpr Fragment Fragments[]={
 {0x1372170,64,0x115b25ea41f27defULL},
 {0x1389e00,5,0xe4d7ad742a331336ULL},
 {0xd801020,32,0x735ffa0ecd29d6ffULL},
 {0x154b20,64,0xda897c4cf9a6d473ULL},
 {0x153b70,5,0xc65c6d4a1f3fd2d0ULL},
 {0x60af710,112,0xd6ba19229f7e2a9aULL},
 {0x60b2191,28,0x66509339d45ca95dULL},
 {0x1385530,5,0x3b176449d02c9b10ULL},
 {0xd7dde50,32,0x9325acf112bac0a6ULL},
 {0x60aff90,104,0x443daac0e9ce4291ULL},
};
}
