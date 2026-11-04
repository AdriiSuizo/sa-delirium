// SPDX-License-Identifier: GPL-3.0-only
// Read-only tests of our actual native-address/prefix implementation. The
// owner's executable is mapped as data, never run, altered on disk or shipped.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <fstream>
#include <iterator>
#include <iostream>
#include <cassert>
static HMODULE simulatedImage{};
static HMODULE WINAPI testImageBase(LPCWSTR){return simulatedImage;}
#define GetModuleHandleW testImageBase
#include "SteamNativeBindings.hpp"
#undef GetModuleHandleW
#include "NativeReviewedQueries.inc"
int wmain(int argc,wchar_t**argv){
    assert(argc==3);
    std::ifstream input(argv[1],std::ios::binary);
    const std::string bytes{std::istreambuf_iterator<char>(input),{}};
    assert(sa_steam_host_identity::readRelocationMetadata(bytes));
    const auto*dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(bytes.data());
    const auto*nt=reinterpret_cast<const IMAGE_NT_HEADERS32*>(bytes.data()+dos->e_lfanew);
    const auto requested=static_cast<uintptr_t>(std::wcstoul(argv[2],nullptr,16));
    auto*image=static_cast<unsigned char*>(VirtualAlloc(reinterpret_cast<void*>(requested),nt->OptionalHeader.SizeOfImage,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    assert(image&&reinterpret_cast<uintptr_t>(image)==requested);simulatedImage=reinterpret_cast<HMODULE>(image);
    std::memcpy(image,bytes.data(),nt->OptionalHeader.SizeOfHeaders);
    for(unsigned n=0;n<nt->FileHeader.NumberOfSections;++n){
        const auto&s=IMAGE_FIRST_SECTION(nt)[n];
        assert(std::size_t(s.PointerToRawData)+s.SizeOfRawData<=bytes.size());
        if(s.SizeOfRawData)std::memcpy(image+s.VirtualAddress,bytes.data()+s.PointerToRawData,s.SizeOfRawData);
    }
    const auto delta=static_cast<DWORD>(requested-nt->OptionalHeader.ImageBase);
    for(auto rva:sa_steam_host_identity::relocationRVAs){DWORD value{};std::memcpy(&value,image+rva,4);value+=delta;std::memcpy(image+rva,&value,4);}
    for(const auto&q:queries){assert(q.address()==requested+q.preferred-0x400000);assert(q.matches());}
    assert(sa_steam_native::address<0x123456>()==0);
    auto*entry=reinterpret_cast<unsigned char*>(sa_steam_native::address<0x53bee0>());
    *entry^=1;assert(!sa_steam_native::prefixMatches<0x53bee0>());*entry^=1;
    constexpr auto call=sa_steam_native::directCallBytes<0x535f94,0x4d34f0>();
    assert(std::memcmp(image+sa_steam_native::preferred<0x535f94>()-0x400000,call.data(),call.size())==0);
    std::cout<<"base="<<std::hex<<requested<<std::dec<<" prefixes="<<std::size(queries)<<" negative_control=1 call_relative=1\n";
    VirtualFree(image,0,MEM_RELEASE);
}
