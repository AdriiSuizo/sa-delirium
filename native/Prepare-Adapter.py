"""Prepare an isolated Steam source overlay while retaining the original adapter.

No development files or executable are changed. Unresolved bindings are listed
before any hooks can be installed. Never pass automated candidate maps here.
"""
from pathlib import Path
import argparse,hashlib,json,re,shutil,subprocess,sys
ROOT=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output',type=Path,required=True)
parser.add_argument('--include-test-diagnostics',action='store_true',help='Also compile opt-in experimental GPU capture and native-store test instrumentation')
args=parser.parse_args()
assert not args.output.exists(),'Use a fresh output directory'
subprocess.run([sys.executable,str(ROOT/'tools/Verify-Vendor.py')],check=True,stdout=subprocess.DEVNULL)
registry=json.loads((ROOT/'native/steam-bindings.json').read_text())
assert registry['status']=='reviewed_subset_not_a_complete_runtime_map'
bindings={int(r['legacy_va'],16):int(r['steam_va'],16)for r in registry['bindings']}
implementations=json.loads((ROOT/'native/host-implementations.json').read_text())['implementations']
helpers={int(key,16):row for key,row in implementations.items()}
assert all(int(dep,16)in bindings for row in helpers.values()for dep in row['dependencies']),'Host helper dependency is not reviewed'
SOURCE=ROOT/'vendor/src/sa-delirium'
out=args.output.resolve();out.mkdir(parents=True)
originals={};unresolved={};references={}
texts={file.name:file.read_text(encoding='utf-8-sig')for file in SOURCE.iterdir()if file.suffix in ['.hpp','.cpp']}
omitted=[]
if not args.include_test_diagnostics:
    # These modules explicitly identify themselves as TEST_ONLY and require
    # passive-test environment gates in the original. They are inactive during
    # normal gameplay. Keep the original sources; omit their hooks from shipping.
    a=texts['Adapter.cpp']
    # Spawning, teleporting, camera readback and scripted fixture exports are
    # authoring tools, not normal gameplay. Their original source is retained
    # in vendor and available with --include-test-diagnostics.
    begin=a.index('extern "C" __declspec(dllexport) int __cdecl DeliriumSA_ProbeHit(')
    end=a.index('static DWORD WINAPI setup(void*){',begin)
    a=a[:begin]+'// Distribution build omits scripted fixture exports.\n'+a[end:]
    omitted.append('DeliriumSA_Probe* exports and their fixture-only includes')
    a=re.sub(r'^.*\btickWheelSmokeProbe\([^\n]*\n','',a,flags=re.M)
    for name in ['tickPhysicalPeerProbe','tickSolidBodyProbe','tickStaticMeshProbe']:
        a=a.replace('static void '+name+'();','')
    a,count=re.subn(r'^\s*if\(recoveryAllowed\(\)\)\{tickPhysicalPeerProbe\(\);tickSolidBodyProbe\(\);tickStaticMeshProbe\(\);\}\n','\n',a,flags=re.M);assert count==1
    for header in ['ExpandedSkinCaptureHost.hpp','ExpandedSkinAdapterTest.hpp','NativeGroundAuditHost.hpp','SkinGroundAudit.hpp']:
        include=f'#include "{header}"';assert a.count(include)==1
        a=a.replace(include,'// Distribution build omits test-only '+header)
        omitted.append(header)
    for declaration in ['static void clearExpandedSkinAdapterTest();','static void recordNativeGroundOwnerCommit(void*,V,V,V);','static void traceSolidBodyContact(int,int,const V&,const V&,const V&,int,float,float,float);']:
        assert a.count(declaration)==1;a=a.replace(declaration,'')
    for name in ['captureExpandedSkinAuthored','captureExpandedSkinRuntime','maintainExpandedSkinAdapterTest','noteSkinGroundAudit']:
        a=re.sub(r'^.*\b'+name+r'\([^\n]*\n','',a,flags=re.M)
    a=a.replace('clearExpandedSkinAdapterTest();','')
    for name in ['installExpandedSkinAdapterTest','installNativeGroundAudit']:
        pattern=r'^\s*if\(!'+name+r'\(\)\)\{[^\n]*\}\n'
        a,count=re.subn(pattern,'\n',a,flags=re.M);assert count==1,(name,count)
    a,count=re.subn(r'^\s*const auto\*expandedFrame=stageExpandedSkinAdapterTest\([^\n]*\n','\n',a,flags=re.M);assert count==1
    # Remove only the three opt-in expanded-palette blocks. The ordinary
    # retargeter, output palette, physical joints and recovery are untouched.
    lexical=re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[{}]',re.S)
    for _ in range(3):
        found=re.search(r'\bif\(expandedFrame\)\{',a);assert found
        opening=a.index('{',found.start());depth=0;end=None
        for token in lexical.finditer(a,opening):
            if token.group()=='{':depth+=1
            elif token.group()=='}':
                depth-=1
                if not depth:end=token.end();break
        assert end is not None;a=a[:found.start()]+a[end:]
    a=a.replace('!expandedFrame&&','')
    assert 'expandedFrame'not in a
    texts['Adapter.cpp']=a
    ground=texts['OriginalAnimatedOwner.hpp']
    ground,count=re.subn(r'\brecordNativeGroundOwnerCommit\([^;]*\);','',ground)
    assert count==1
    ground,count=re.subn(r'if\(length\(contactImpulse\)>1\.e-6f\)traceSolidBodyContact\([^;]*\);','',ground)
    assert count==1
    texts['OriginalAnimatedOwner.hpp']=ground
    # GPU readback is a fixture service, never part of contact/solver state.
    # Omit its callers and definitions together, rather than returning a fake
    # successful capture or substituting an unverified device pointer.
    def remove_block(source, marker):
        start=source.index(marker);opening=source.index('{',start)
        depth=0
        for token in lexical.finditer(source,opening):
            if token.group()=='{':depth+=1
            elif token.group()=='}':
                depth-=1
                if not depth:return source[:start]+source[token.end():]
        raise AssertionError(marker)
    for file,name in [('FootSupport.hpp','captureFootSupportFrame'),('MeleeContactProbe.hpp','captureMeleeFrames'),('RoadTelemetry.hpp','captureRoadFrames')]:
        texts[file]=remove_block(texts[file],'static void '+name+'()')
        texts['Adapter.cpp']=texts['Adapter.cpp'].replace(name+'();','')
    for file in ['MeleeContactProbe.hpp','RoadTelemetry.hpp']:
        texts[file]=texts[file].replace('extern "C" __declspec(dllexport) int __cdecl DeliriumSA_ProbeFrame();','')
    texts['PlayerShove.hpp']=remove_block(texts['PlayerShove.hpp'],'if(!shoveFrames.empty()&&gameTime()>=shoveFrames.front())')
    texts['PlayerShove.hpp'],count=re.subn(r'    if\(GetEnvironmentVariableA\("GTASA_PASSIVE_TEST_WINDOW"[^\n]*\n        for\(unsigned delay:[^\n]*\n','',texts['PlayerShove.hpp']);assert count==1
    omitted.append('GPU readback fixture services (melee/foot/road/shove)')
closure=set();queue=['Adapter.cpp']
while queue:
    name=queue.pop()
    if name in closure:continue
    file=SOURCE/name
    if not file.is_file():continue
    closure.add(name)
    for include in re.findall(r'^\s*#include\s*"([^"]+)"',texts[name],re.M):
        if (SOURCE/include).is_file():queue.append(include)

# A token recognizer keeps strings and comments untouched. Addresses mentioned
# in documentation are not code; bit masks and image sizes are not bindings.
tokens=re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|\b0x[0-9a-fA-F]+[uUlL]*\b',re.S)
constants={0x400000,0x800000,0x80000000,0xffffff,0xffffffff,0x1177000}
for file in sorted(SOURCE.iterdir()):
    if file.suffix not in ('.hpp','.cpp'):continue
    raw=file.read_bytes();originals[file.name]=hashlib.sha256(raw).hexdigest()
    text=texts[file.name]
    text=re.sub(r'#include "\.\./plugin-sdk-sa/plugin_sa/game_sa/rw/(?:rwcore|rpworld)\.h"',
                '#include "NativeGeometryViews.hpp"',text)
    text=text.replace('#include "../plugin-sdk-sa/plugin_sa/game_sa/enums/eTaskType.h"','#include "NativeTaskIds.hpp"')
    def token(match):
        s=match.group()
        if not s.startswith('0x'):return s
        address=int(re.match(r'0x[0-9a-fA-F]+',s).group(),16)
        if not(0x400000<=address<0x1577000)or address in constants:return s
        if file.name in closure:references.setdefault(address,set()).add(file.name)
        if address in helpers:return '(reinterpret_cast<uintptr_t>(&'+helpers[address]['function']+'))'
        if address not in bindings and file.name in closure:unresolved.setdefault(address,set()).add(file.name)
        return f'sa_steam_native::address<{hex(address)}>()'
    text=tokens.sub(token,text)
    text='#include "SteamNativeBindings.hpp"\n'+text
    # Steam is ASLR-enabled. Addresses in the review registry use the preferred
    # image base only as metadata; every live pointer follows the loaded image.
    text=re.sub(r'\bconstexpr([^;\n]*sa_steam_native::address<[^;\n]*;)',r'const\1',text)
    if file.name=='NativePairAudit.hpp':
        enum=re.search(r'enum class Path[^;]+;',text,re.S);assert enum
        text=text[:enum.start()]+enum.group().replace('sa_steam_native::address<','sa_steam_native::preferred<')+text[enum.end():]
    if file.name=='BikeRenderPhase.hpp':
        for old,name in [(0x6bd090,'bikeStart'),(0x6c0810,'bmxStart')]:
            prior=f'std::memcmp(reinterpret_cast<void*>(sa_steam_native::address<{hex(old)}>()),{name},sizeof({name}))'
            assert text.count(prior)==1
            text=text.replace(prior,f'!sa_steam_native::prefixMatches<{hex(old)}>()')
        text=re.sub(r'    const unsigned char (?:bikeStart|bmxStart)\[\]=\{[^}]+\};\n','',text)
    if file.name=='BikeImpactCapture.hpp':
        text=text.replace('static constexpr Site sites[]','static const Site sites[]')
        for site in [0x54be1c,0x54c098,0x54c35a,0x54c54d]:
            pattern=r'\{sa_steam_native::address<'+hex(site)+r'>\(\),\{0xE8,[^}]+\}\}'
            text,count=re.subn(pattern,'{sa_steam_native::address<'+hex(site)+'>(),sa_steam_native::directCallBytes<'+hex(site)+',0x54a2c0>()}',text)
            assert count==1,(file.name,hex(site),count)
    if file.name=='BikePoseCapture.hpp':
        text,count=re.subn(r'const unsigned char callBytes\[\]=\{[^}]+\};','constexpr auto callBytes=sa_steam_native::directCallBytes<0x535f94,0x4d34f0>();',text);assert count==1
        for name,old in [('ownerBytes',0x535f00),('seatBytes',0x5df910)]:
            pattern=r'std::memcmp\(reinterpret_cast<void\*>\(sa_steam_native::address<'+hex(old)+r'>\(\)\),'+name+r',sizeof\('+name+r'\)\)'
            text,count=re.subn(pattern,'!sa_steam_native::prefixMatches<'+hex(old)+'>()',text);assert count==1
        text=re.sub(r'^    const unsigned char (?:ownerBytes|seatBytes)\[\]=\{[^}]+\};\n','',text,flags=re.M)
        text=text.replace(',callBytes,5',',callBytes.data(),5')
    if file.name=='NativeGroundAuthority.hpp':
        text,count=re.subn(r'inline constexpr unsigned char gravityCallBytes\[5\]=\{[^}]+\};','inline constexpr auto gravityCallBytes=sa_steam_native::directCallBytes<0x548661,0x542fe0>();',text);assert count==1
    if file.name=='OriginalGroundHost.hpp':
        text=text.replace(',gravityCallBytes,5',',gravityCallBytes.data(),5')
    if file.name=='Adapter.cpp':
        old='static DWORD WINAPI setup(void*){'
        assert text.count(old)==1
        text=text.replace(old,old+'''
    wchar_t startupPath[32768]{};GetModuleFileNameW(nullptr,startupPath,32768);
    std::ofstream startup(fs::path(startupPath).parent_path()/L"DeliriumSA-native-startup.log",std::ios::app);
    startup<<"pid="<<GetCurrentProcessId()<<" setup_entered base="<<std::hex<<reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr))<<std::dec<<std::endl;
    // Complete preflight is mandatory before native access or installation.
    if(!sa_steam_native::preflight()){startup<<"preflight_rejected"<<std::endl;return 0;}
    using Ready=int(__cdecl*)();
    using WaitReady=int(__cdecl*)(unsigned);
    auto core=GetModuleHandleW(L"sa_delirium_bootstrap.dll");
    auto bootstrapReady=core?reinterpret_cast<Ready>(GetProcAddress(core,"sa_delirium_runtime_ready")):nullptr;
    auto waitReady=core?reinterpret_cast<WaitReady>(GetProcAddress(core,"sa_delirium_wait_ready")):nullptr;
    if(!bootstrapReady||!waitReady){startup<<"shared_runtime_exports_missing"<<std::endl;return 0;}
    startup<<"waiting_for_owned_resources ready="<<bootstrapReady()<<std::endl;
    while(!bootstrapReady())if(waitReady(30000)<0)return 0;
    startup<<"shared_runtime_ready"<<std::endl;
''')
        old='folder=fs::path(exe).parent_path()/L"DeliriumSA";'
        assert text.count(old)==1;text=text.replace(old,'folder=fs::path(exe).parent_path()/L"DeliriumSA-local-v3";')
        old='nt->OptionalHeader.SizeOfImage!=0x1177000'
        assert text.count(old)==1;text=text.replace(old,'nt->OptionalHeader.SizeOfImage!=0x985000')
        old='if(nm_version()!=100||!ok(nm_init((folder/"fred.xrt").string().c_str()))||!ok(nm_load_task_params((folder/"taskparams.txt").string().c_str())))'
        assert text.count(old)==1;text=text.replace(old,'if(nm_version()!=100||!bootstrapReady())')
        # Export names of the existing test API are preserved in the local
        # overlay. The packaging allowlist never includes this work-in-progress.
    (out/file.name).write_text(text,encoding='utf-8',newline='\n')

cases='\n'.join(f'case {hex(old)}:return {hex(new)};'for old,new in bindings.items())
prefixes='\n'.join('case '+r['legacy_va']+':return sa_steam_host_identity::prefixMatches(address<'+r['legacy_va']+'>(),{'+
    ','.join('0x'+r['entry_prefix_sha256'][i:i+2]for i in range(0,64,2))+'});'for r in registry['bindings']if r['kind']=='function')
missing='\n'.join(f'    // {hex(old)}: {", ".join(sorted(files))}'for old,files in sorted(unresolved.items()))
header='''#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstdint>
#include <array>
#include "SteamHostIdentity.hpp"
namespace sa_steam_glue {void __fastcall updateRwMatrix(void*);}
namespace sa_steam_native {
template<uintptr_t Legacy> constexpr uintptr_t preferred(){
    switch(Legacy){
'''+cases+'''
    default:return 0; // Preflight blocks the entire module before native access.
    }
}
inline uintptr_t imageBase(){static const auto image=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));return image;}
template<uintptr_t Legacy> inline uintptr_t address(){
    constexpr auto selected=preferred<Legacy>();
    return selected?imageBase()+selected-0x400000:0;
}
template<uintptr_t Site,uintptr_t Target> constexpr std::array<unsigned char,5> directCallBytes(){
    static_assert(preferred<Site>()&&preferred<Target>(),"A reviewed call-site and target are required");
    constexpr auto relative=static_cast<uint32_t>(preferred<Target>()-preferred<Site>()-5);
    return {0xe8,static_cast<unsigned char>(relative),static_cast<unsigned char>(relative>>8),static_cast<unsigned char>(relative>>16),static_cast<unsigned char>(relative>>24)};
}
template<uintptr_t Legacy> inline bool prefixMatches(){
    switch(Legacy){
'''+prefixes+'''
    default:return false;
    }
}
inline bool preflight(){
    const auto*dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(imageBase());
    if(dos->e_magic!=IMAGE_DOS_SIGNATURE)return false;
    const auto*nt=reinterpret_cast<const IMAGE_NT_HEADERS32*>(imageBase()+dos->e_lfanew);
    if(nt->Signature!=IMAGE_NT_SIGNATURE||nt->FileHeader.Machine!=IMAGE_FILE_MACHINE_I386||nt->OptionalHeader.SizeOfImage!=0x985000)return false;
    if(!sa_steam_host_identity::supported())return false;
'''+missing+f'\n    return {"true"if not unresolved else"false"};\n'+'''
}
}
'''
(out/'SteamNativeBindings.hpp').write_text(header,encoding='utf-8')
shutil.copyfile(ROOT/'native/SteamHostIdentity.hpp',out/'SteamHostIdentity.hpp')
for name in ['NativeGeometryViews.hpp','NativeTaskIds.hpp']:
    shutil.copyfile(ROOT/'native'/name,out/name)
for row in implementations.values():shutil.copyfile(ROOT/'native'/row['source'],out/row['source'])
report={'status':'blocked_unresolved_bindings'if unresolved else'prepared_requires_gameplay_validation',
    'source_include_closure':sorted(closure),
    'omitted_test_only_modules':omitted,
    'original_source_sha256':originals,'registry_sha256':hashlib.sha256((ROOT/'native/steam-bindings.json').read_bytes()).hexdigest(),
    'referenced_bindings':len(references),'resolved_bindings':len(references.keys()&(bindings.keys()|helpers.keys())),
    'host_implementations':implementations,
    'unresolved':{hex(a):sorted(files)for a,files in sorted(unresolved.items())},
    'executable_modified':False,'hooks_enabled':0,'gameplay_accepted':False}
(out/'preparation.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
print(json.dumps({k:report[k]for k in ['status','referenced_bindings','resolved_bindings','hooks_enabled']}))
