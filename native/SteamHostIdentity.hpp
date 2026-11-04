#pragma once
// Same owned Steam file fingerprint used by our existing bootstrap plugin.
#include <windows.h>
#include <bcrypt.h>
#include <array>
#include <fstream>
#include <filesystem>
#include <string>
#include <vector>
#include <algorithm>
#include <cstring>
#pragma comment(lib,"bcrypt.lib")
namespace sa_steam_host_identity {
inline std::vector<DWORD> relocationRVAs;
inline bool relocationMetadataReady=false;
inline bool readRelocationMetadata(const std::string&bytes){
    const auto*dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(bytes.data());
    if(dos->e_lfanew<0||std::size_t(dos->e_lfanew)+sizeof(IMAGE_NT_HEADERS32)>bytes.size())return false;
    const auto*nt=reinterpret_cast<const IMAGE_NT_HEADERS32*>(bytes.data()+dos->e_lfanew);
    const auto*sections=IMAGE_FIRST_SECTION(nt);
    if(nt->FileHeader.NumberOfSections>96||reinterpret_cast<const char*>(sections+nt->FileHeader.NumberOfSections)>bytes.data()+bytes.size())return false;
    const auto directory=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
    std::size_t raw=bytes.size();
    for(unsigned n=0;n<nt->FileHeader.NumberOfSections;n++){
        const auto&s=sections[n];
        if(directory.VirtualAddress>=s.VirtualAddress&&directory.VirtualAddress-s.VirtualAddress<s.SizeOfRawData){
            raw=std::size_t(s.PointerToRawData)+directory.VirtualAddress-s.VirtualAddress;break;
        }
    }
    if(!directory.Size||raw>bytes.size()||directory.Size>bytes.size()-raw)return false;
    std::vector<DWORD> found;
    for(std::size_t cursor=raw;cursor<raw+directory.Size;){
        if(raw+directory.Size-cursor<sizeof(IMAGE_BASE_RELOCATION))return false;
        IMAGE_BASE_RELOCATION block{};std::memcpy(&block,bytes.data()+cursor,sizeof(block));
        if(block.SizeOfBlock<sizeof(block)||block.SizeOfBlock>raw+directory.Size-cursor||(block.SizeOfBlock-sizeof(block))%2)return false;
        for(std::size_t at=cursor+sizeof(block);at<cursor+block.SizeOfBlock;at+=2){
            WORD entry{};std::memcpy(&entry,bytes.data()+at,2);
            if((entry>>12)==IMAGE_REL_BASED_ABSOLUTE)continue;
            if((entry>>12)!=IMAGE_REL_BASED_HIGHLOW)return false;
            const DWORD rva=block.VirtualAddress+(entry&0xfff);
            if(rva>nt->OptionalHeader.SizeOfImage-4)return false;
            found.push_back(rva);
        }
        cursor+=block.SizeOfBlock;
    }
    std::sort(found.begin(),found.end());relocationRVAs=std::move(found);relocationMetadataReady=true;return true;
}
inline bool prefixMatches(uintptr_t address,const std::array<unsigned char,32>&expected){
    if(!relocationMetadataReady)return false;
    std::array<unsigned char,10> input{};SIZE_T copied=0;
    if(!ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(address),input.data(),input.size(),&copied)||copied!=input.size())return false;
    const auto base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    if(address<base)return false;
    const auto rva=static_cast<DWORD>(address-base),delta=static_cast<DWORD>(base-0x400000);
    // Windows relocates embedded absolute operands. Compare their canonical
    // disk representation without changing any live instruction or PE flags.
    auto row=std::lower_bound(relocationRVAs.begin(),relocationRVAs.end(),rva);
    for(;row!=relocationRVAs.end()&&*row<rva+7;++row){
        const auto offset=*row-rva;DWORD value{};std::memcpy(&value,input.data()+offset,4);
        value-=delta;std::memcpy(input.data()+offset,&value,4);
    }
    std::array<unsigned char,32> actual{};
    return BCryptHash(BCRYPT_SHA256_ALG_HANDLE,nullptr,0,input.data(),7,
        actual.data(),static_cast<ULONG>(actual.size()))>=0&&actual==expected;
}
inline bool supported(){
    wchar_t path[32768]{};
    if(!GetModuleFileNameW(nullptr,path,32768))return false;
    const std::filesystem::path file(path);
    if(_wcsicmp(file.filename().c_str(),L"gta-sa.exe"))return false;
    std::ifstream in(file,std::ios::binary|std::ios::ate);
    if(!in||in.tellg()!=5971456)return false;
    std::string bytes(5971456,'\0');in.seekg(0);
    if(!in.read(bytes.data(),bytes.size()))return false;
    std::array<unsigned char,32> actual{};
    constexpr std::array<unsigned char,32> expected{
        0x15,0xe3,0xcf,0xed,0xba,0x9a,0x84,0x1d,0xf6,0x7d,0x81,0x94,0xe7,0x24,0x9a,0xfb,
        0x49,0x3b,0x0e,0x10,0xd6,0x13,0x8f,0xb8,0xeb,0xab,0x2c,0x13,0x6e,0x54,0x3e,0xfb};
    return BCryptHash(BCRYPT_SHA256_ALG_HANDLE,nullptr,0,reinterpret_cast<PUCHAR>(bytes.data()),
        static_cast<ULONG>(bytes.size()),actual.data(),static_cast<ULONG>(actual.size()))>=0&&actual==expected&&readRelocationMetadata(bytes);
}
}
