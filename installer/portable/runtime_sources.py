"""Remove authoring-only hooks from the unremapped SA1.0 distribution source.

Derived from the reviewed native/Prepare-Adapter.py fixture removal only;
no address remapping or native-R2 runtime changes are applied here.
"""
import re


def strip_fixtures(texts):
    omitted = []
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
    # The animation trace (GTASA_ANIM_TRACE) also follows the probe rider,
    # defined with the omitted fixtures: there is none in a distribution build.
    declaration='static int traceProbeRider();'
    if declaration in a and 'static int traceProbeRider(){' not in a:
        a=a.replace(declaration,'static int traceProbeRider(){return -1;}')
    a=re.sub(r'^.*\btickWheelSmokeProbe\([^\n]*\n','',a,flags=re.M)
    a=re.sub(r'^.*\btickCopProbe\([^\n]*\n','',a,flags=re.M)
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
    texts['Aiming.hpp']=remove_block(texts['Aiming.hpp'],'extern "C" __declspec(dllexport) int __cdecl DeliriumSA_ProbeAimWheel(')
    for file in ['MeleeContactProbe.hpp','RoadTelemetry.hpp']:
        texts[file]=texts[file].replace('extern "C" __declspec(dllexport) int __cdecl DeliriumSA_ProbeFrame();','')
    texts['PlayerShove.hpp']=remove_block(texts['PlayerShove.hpp'],'if(!shoveFrames.empty()&&gameTime()>=shoveFrames.front())')
    texts['PlayerShove.hpp'],count=re.subn(r'    if\(GetEnvironmentVariableA\("GTASA_PASSIVE_TEST_WINDOW"[^\n]*\n        for\(unsigned delay:[^\n]*\n','',texts['PlayerShove.hpp']);assert count==1
    omitted.append('GPU readback fixture services (melee/foot/road/shove)')
    return omitted
