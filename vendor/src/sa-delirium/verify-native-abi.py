"""Check critical native entry points against the independently maintained SDK sources."""
from pathlib import Path
import re
import json
import struct
root=Path(__file__).resolve().parents[2]
sdk=root/'src/plugin-sdk-sa/plugin_sa/game_sa'
bone_tags=(root/'research/native-abi-reference/eBoneTag.h').read_text()
for name,tag in [('BONE_PELVIS',1),('BONE_SPINE',2),('BONE_SPINE1',3),('BONE_NECK',4),('BONE_HEAD',5),('BONE_JAW',8)]:
    assert re.search(rf'{name}\s*=\s*{tag}\b',bone_tags), 'Do not use the mislabeled legacy ePedBones enum'
assert 'eMoveState m_moveState' in (root/'research/native-abi-reference/TaskSimpleGoTo.h').read_text()
assert 'VALIDATE_SIZE(CTaskSimple, 0x8)' in (sdk/'CTaskSimple.h').read_text()
for member,offset in [('m_pTargetVehicle','0xC'),('m_nTargetDoor','0x14')]:
    assert f'VALIDATE_OFFSET(CTaskComplexEnterCar, {member}, {offset})' in (sdk/'CTaskComplexEnterCar.h').read_text()
assert 'CallMethod<0x6402F0' in (sdk/'CTaskComplexEnterCarAsDriver.cpp').read_text()
assert 'VALIDATE_SIZE(CTaskComplexEnterCar, 0x50)' in (sdk/'CTaskComplexEnterCar.h').read_text()
assert 'plugin::GetVMT(this, 6)' in (sdk/'CTask.cpp').read_text()
assert 'm_anim = {};              // 0xC' in (root/'research/native-abi-reference/TaskSimpleCarOpenDoorFromOutside.h').read_text()
assert 'VALIDATE_OFFSET(CAutomobile, m_doors, 0x5B8)' in (sdk/'CAutomobile.h').read_text()
for field,offset in [('m_fAngle','0xC'),('m_fPrevAngle','0x10'),('m_nAxis','0xA')]:
    assert f'VALIDATE_OFFSET(CDoor, {field}, {offset})' in (sdk/'CDoor.h').read_text()
for address in ['0x7F1200','0x7F0990','0x7F0910']:
    assert address in (sdk/'RenderWare.cpp').read_text()
for member,offset in [('m_nMoveState','0x534'),('m_vecAnimMovingShift','0x550'),('m_nFightingStyle','0x72D')]:
    assert f'VALIDATE_OFFSET(CPed, {member}, {offset})' in (sdk/'CPed.h').read_text()
assert 'VALIDATE_OFFSET(CPlayerData, m_fMoveBlendRatio, 0x14)' in (sdk/'CPlayerData.h').read_text()
assert 'unsigned int& CTimer::m_FrameCounter = *(unsigned int*)0xB7CB4C;' in (sdk/'CTimer.cpp').read_text()
assert 'CallAndReturn<bool, 0x417950, CColLine const&, CMatrix const&, CColModel&, CColPoint&, float&, bool, bool>' in (sdk/'CCollision.cpp').read_text()
for expected in ['VALIDATE_SIZE(CColLine, 0x20)','VALIDATE_OFFSET(CColLine, m_vecEnd, 0x10)']:
    assert expected in (sdk/'CColLine.h').read_text(), 'Vehicle-only brace ray ABI missing'
assert '0x681720' in (sdk/'CTaskManager.cpp').read_text()
assert 'plugin::GetVMT(this, 4)' in (sdk/'CTask.cpp').read_text()
assert 'plugin::GetVMT(this, 2)' in (sdk/'CTask.cpp').read_text()
assert 'VALIDATE_OFFSET(CPed, m_pStats, 0x59C)' in (sdk/'CPed.h').read_text()
ped_stats=(root/'research/native-abi-reference/PedStats.h').read_text()
assert 'float  m_fAttackStrength;' in ped_stats and 'float  m_fDefendWeakness;' in ped_stats
assert 'VALIDATE_SIZE(CPedStat, 0x34)' in ped_stats
assert 'RH_ScopedInstall(Constructor, 0x65C430)' in (root/'research/native-abi-reference/TaskComplexSmartFleeEntity.cpp').read_text()
assert 'VALIDATE_SIZE(CTaskComplexSmartFleeEntity, 0x40)' in (root/'research/native-abi-reference/TaskComplexSmartFleeEntity.h').read_text()
assert 'CTheScripts::bAlreadyRunningAMissionScript = *reinterpret_cast<bool *>(GLOBAL_ADDRESS_BY_VERSION(0xA444B1' in (sdk/'CTheScripts.cpp').read_text()
mission_loader=(root/'research/native-abi-reference/MissionCommands.cpp').read_text()
assert 'CTheScripts::StartNewScript(&CTheScripts::MissionBlock[0])' in mission_loader
assert 'CTheScripts::bAlreadyRunningAMissionScript = true' in mission_loader
for address in ['0x53FB70','0x540670','0x540340','0x540750','0x540770','0x5407A0','0x540700','0x53FC90','0x53FD30']:
    assert address in (sdk/'CPad.cpp').read_text(), 'Combat input ABI missing: '+address
assert 'RH_ScopedInstall(ComputeDamageAnim, 0x4B3FC0)' in (root/'research/native-abi-reference/EventDamage.cpp').read_text()
assert 'RH_ScopedInstall(ComputeDeathAnim, 0x4B3A60)' in (root/'research/native-abi-reference/EventDamage.cpp').read_text()
assert 'void CEventDamage::ComputeDeathAnim(CPed* ped, bool bMakeActiveTaskAbortable)' in (root/'research/native-abi-reference/EventDamage.cpp').read_text()
for expected in ['VALIDATE_OFFSET(CPed, m_nSelectedWepSlot, 0x718)','VALIDATE_OFFSET(CPed, m_aWeapons, 0x5A0)','VALIDATE_OFFSET(CPed, m_pTargetedObject, 0x71C)']:
    assert expected in (sdk/'CPed.h').read_text(), 'Combat ped ABI mismatch: '+expected
assert 'VALIDATE_SIZE(CWeapon, 0x1C)' in (sdk/'CWeapon.h').read_text()
assert 'VALIDATE_OFFSET(CPed, m_nDeathTime, 0x750)' in (sdk/'CPed.h').read_text()
native_die=(root/'research/native-abi-reference/TaskSimpleDie.cpp').read_text()
assert 'm_animId == ANIM_ID_NO_ANIMATION_SET' in native_die
assert 'ped->m_nDeathTimeMS = CTimer::GetTimeInMS()' in native_die
population=(root/'research/native-abi-reference/Population.cpp').read_text()
assert 'CTimer::GetTimeInMS() - ped->m_nDeathTimeMS' in population
assert '0x5E1B10' in (sdk/'CPed.cpp').read_text()
assert 'VALIDATE_OFFSET(CPed, m_vecAnimMovingShift, 0x550)' in (sdk/'CPed.h').read_text()
adapter=(root/'src/sa-delirium/Adapter.cpp').read_text()
checks=[('process',sdk/'CGame.cpp',r'addrof\(CGame::Process\)\s*=\s*ADDRESS_BY_VERSION\(0x([0-9A-Fa-f]+)'),
        ('damage',root/'research/native-abi-reference/EventDamage.cpp',r'// 0x([0-9A-Fa-f]+)\s*\nvoid CEventDamage::ProcessDamage')]
results=[]
for hook,file,pattern in checks:
    expected=int(re.search(pattern,file.read_text()).group(1),16)
    actual=int(re.search(r'hook\(reinterpret_cast<void\*>\(0x([0-9A-Fa-f]+)\),&'+hook+r',',adapter).group(1),16)
    assert actual==expected, f'{hook}: hook {actual:x}, SDK {expected:x}'
    results.append({'hook':hook,'address':hex(actual),'source':str(file)})
assert '0x40F5E0' in (sdk/'CCollisionData.cpp').read_text(), 'Native triangle decode ABI missing'
assert '0x734A40' in (sdk/'common.cpp').read_text(), 'Skin hierarchy ABI missing'
for source,address in [('CStreaming.cpp','0x40ED80'),('CWorld.cpp','0x5696C0'),('CWorld.cpp','0x5674E0'),('CWeapon.cpp','0x73A530')]:
    assert address in (sdk/source).read_text(), f'Probe ABI missing: {source} {address}'
print(json.dumps({'status':'critical_native_entries_verified','checks':results}))
assert '0xBA86F0' in (sdk/'CRadar.cpp').read_text()
assert '((float(__cdecl *)(float, float))0x41BD90)(min, max)' in (sdk/'CGeneral.cpp').read_text(), 'Native RNG ABI missing'
for definition in ['VALIDATE_SIZE(tRadarTrace, 0x28)', 'VALIDATE_OFFSET(tRadarTrace, m_vecPos, 0x8)', 'VALIDATE_OFFSET(tRadarTrace, m_nRadarSprite, 0x24)', 'RADAR_SPRITE_RYDER,         // 34']:
    assert definition in (sdk/'CRadar.h').read_text(), 'Radar test ABI mismatch: '+definition
assert 'VALIDATE_SIZE(CColPoint, 0x2C)' in (sdk/'CColPoint.h').read_text()
for source,address in [('CTask.cpp','0x61A5A0'),('CTaskManager.cpp','0x681AF0')]:
    assert address in (sdk/source).read_text(), 'Native recovery ABI missing'
assert 'RH_ScopedInstall(Constructor, 0x677F50)' in (root/'research/native-abi-reference/TaskSimpleGetUp.cpp').read_text()
assert 'VALIDATE_SIZE(CTaskSimpleGetUp, 0x10)' in (root/'research/native-abi-reference/TaskSimpleGetUp.h').read_text()
assert '0x67FA80' in (root/'research/native-abi-reference/TaskSimpleGetUp.cpp').read_text()
fall = root/'research/stutter-continuity-20261103/sa-native'
assert 'RH_ScopedInstall(Constructor2, 0x678700)' in (fall/'TaskComplexFallAndGetUp.cpp').read_text()
assert 'RH_ScopedInstall(IsFalling, 0x6787D0)' in (fall/'TaskComplexFallAndGetUp.cpp').read_text()
assert 'VALIDATE_SIZE(CTaskComplexFallAndGetUp, 0x18)' in (fall/'TaskComplexFallAndGetUp.h').read_text()
assert 'ADDRESS_BY_VERSION(0x616A40' in (sdk/'CPedPlacement.cpp').read_text()
for source,expected in [('CPed.h','VALIDATE_OFFSET(CPed, m_standingOnEntity, 0x568)'),('CPhysical.h','VALIDATE_OFFSET(CPhysical, m_pAttachedTo, 0xFC)'),('CVehicle.h','VALIDATE_OFFSET(CVehicle, m_nVehicleClass, 0x590)')]:
    assert expected in (sdk/source).read_text(), 'Recovery clearance ABI missing: '+expected
assert 'RH_ScopedInstall(SetPedOutCar, 0x4AFF60)' in (root/'research/native-abi-reference/EventKnockOffBike.cpp').read_text()
for name,address in [('SetPedSafePosition','4B4AC0'),('CalcForcesAndAnims','4B0020')]:
    assert f'RH_ScopedInstall({name}, 0x{address})' in (root/'research/native-abi-reference/EventKnockOffBike.cpp').read_text()
    assert f'hook(reinterpret_cast<void*>(0x{address})' in adapter
# Verify actual shipped handler order, not only source declarations. SafePosition
# relocates the native capsule; CalcForcesAndAnims must not launch after handoff.
bike_handler=(root/'research/bike-transition-20261101/native-handler-disassembly.txt').read_text().lower()
assert bike_handler.index('4b4ac0') < bike_handler.index('4aff60') < bike_handler.index('4b0020')
for source,expected in [('CPed.h','VALIDATE_OFFSET(CPed, m_pVehicle, 0x58C)'),('CVehicle.h','VALIDATE_OFFSET(CVehicle, m_nVehicleSubClass, 0x594)'),('CAutomobile.h','VALIDATE_OFFSET(CAutomobile, m_aCarNodes, 0x648)'),('CHeli.h','VALIDATE_SIZE(CHeli, 0xA18)'),('CCarEnterExit.cpp','0x64E740')]:
    assert expected in (sdk/source).read_text(), 'Vehicle ABI missing: '+expected
for source,expected in [('CRunningScript.h','VALIDATE_SIZE(CRunningScript, 0xE0)'),('CRunningScript.h','VALIDATE_OFFSET(CRunningScript, m_pCurrentIP, 0x14)'),('CRunningScript.h','VALIDATE_OFFSET(CRunningScript, m_aLocalVars, 0x3C)'),('CPedIntelligence.h','VALIDATE_OFFSET(CPedIntelligence, m_eventGroup, 0x68)'),('CRunningScript.cpp','0x469EB0'),('CStreaming.cpp','0x4087E0'),('CStreaming.cpp','0x40EA10'),('CPlaceable.cpp','0x43E0C0')]:
    assert expected in (sdk/source).read_text(), 'Fixture ABI missing: '+expected
assert '0x4AB420' in (root/'research/native-abi-reference/EventGroup.cpp').read_text()
for address in ['0x4AD830','0x4B35A0']:
    assert address in (root/'research/native-abi-reference/EventDamage.cpp').read_text(), 'Held-drag damage event ABI missing: '+address
assert 'VALIDATE_OFFSET(CPed, m_fHeadingCurrent, 0x558)' in (sdk/'CPed.h').read_text()
assert 'VALIDATE_OFFSET(CPed, m_fHeadingGoal, 0x55C)' in (sdk/'CPed.h').read_text()
assert '0x612710' in (sdk/'CPopulation.cpp').read_text()
assert 'bool makeWander' in (sdk/'CPopulation.cpp').read_text()
assert '0x4AFCF0' in (root/'research/native-abi-reference/EventKnockOffBike.cpp').read_text()
assert 'VALIDATE_SIZE(CEventKnockOffBike, 0x3C)' in (root/'research/native-abi-reference/EventKnockOffBike.h').read_text()
assert '0x5E4280' in (sdk/'CPed.cpp').read_text()
for source, address in [('CTaskManager.cpp','0x681C10'),('CPed.cpp','0x5DEC00'),('CPlaceable.cpp','0x43E0C0'),('CAnimManager.cpp','0x4D4610'),('CAnimBlendAssociation.cpp','0x4CEB70'),('CAnimBlendAssociation.cpp','0x4CEBE0')]:
    assert address in (sdk/source).read_text(), 'Recovery ABI missing: '+source+' '+address
for address in ['0x678110','0x677FE0']:
    assert address in (root/'research/native-abi-reference/TaskSimpleGetUp.cpp').read_text()
assert '0x543490' in (sdk/'CPhysical.cpp').read_text()
assert 'void CPhysical::ApplyForce(CVector vecForce, CVector point, bool bUpdateTurnSpeed)' in (root/'research/native-abi-reference/Physical.cpp').read_text()
assert '005f12ca: call       0x542b50' in (root/'research/native-abi-reference/KillPedWithCar-5f0360-disassembly.txt').read_text()
# The return-address filter must refer to an actual CALL, not a guessed range.
exe=(root/'game/gta_sa.exe').read_bytes();pe=struct.unpack_from('<I',exe,0x3c)[0]
imagebase=struct.unpack_from('<I',exe,pe+24+28)[0];sections=struct.unpack_from('<H',exe,pe+6)[0]
# GenerateDamageEvent's no-animation sentinel is 191, NOT -1. Validate the
# executable comparison as well as the ABI entry; this guards the crash in 031900.
import pefile
native_pe=pefile.PE(data=exe)
import capstone
native_decoder=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32)
# Fatal accepted Impact: native damage consumer and ComplexDie first subtask.
# Both entries begin with one complete six-byte FS load; no interior operand
# is overwritten. Bookkeeping calls are the exact tail of SimpleDie::StartAnim.
event_handler_source=(root/'research/native-abi-reference/EventHandler.cpp').read_text()
complex_die_source=(root/'research/native-abi-reference/TaskComplexDie.cpp').read_text()
assert 'void CEventHandler::ComputeDamageResponse(CEventDamage* e, CTask* tactive, CTask* tsimplest, CTask* abortedTaskEventResponse)' in event_handler_source
assert '0x6302D0' in complex_die_source and 'CTaskComplexDie::CreateFirstSubTask' in complex_die_source
hook_entries={int(x,16) for x in re.findall(r'(?:\{0x|hook\(reinterpret_cast<void\*>\(0x)([0-9a-fA-F]+)',adapter)}
for address,name,original in [(0x4C0170,'physicalDamageResponse','originalPhysicalDamageResponse'),(0x6302D0,'physicalDieFirst','originalPhysicalDieFirst')]:
    assert f'{{0x{address:X},reinterpret_cast<void*>(&{name})' in adapter, 'Missing fatal Impact hook: '+name
    assert native_pe.get_data(address-imagebase,8)==bytes.fromhex('64 a1 00 00 00 00 6a ff'), 'Fatal Impact entry changed: '+name
    instruction=next(native_decoder.disasm(native_pe.get_data(address-imagebase,6),address))
    assert instruction.address==address and instruction.size==6 and instruction.mnemonic=='mov'
    assert not any(other!=address and address-5<other<address+6 for other in hook_entries), 'Overlapping fatal Impact hook: '+name
assert native_pe.get_data(0x4C0BC3-imagebase,3)==bytes.fromhex('c2 10 00'), 'Damage consumer thiscall argument count changed'
assert native_pe.get_data(0x630553-imagebase,3)==bytes.fromhex('c2 04 00'), 'ComplexDie first-subtask argument count changed'
for site,target in [(0x637589,0x5E5320),(0x63759A,0x5DED10)]:
    op=native_pe.get_data(site-imagebase,5)
    assert op[0]==0xE8 and site+5+struct.unpack_from('<i',op,1)[0]==target, 'Death bookkeeping CALL changed'
assert native_pe.get_data(0x63759F-imagebase,12)==bytes.fromhex('8b 0d 84 cb b7 00 89 8f 50 07 00 00'), 'Native death-time bookkeeping changed'
assert '0x5E5320' in (sdk/'CPed.cpp').read_text() and '0x5DED10' in (sdk/'CPed.cpp').read_text()
physical_source=(root/'research/native-abi-reference/Physical.cpp').read_text()
bike_source=(root/'research/native-abi-reference/Bike.cpp').read_text()
for address,callback,signature in [
    (0x5435C0,'bikeCollisionOne','bool(CPhysical::*)(CEntity*, const CColPoint&, float&)'),
    (0x543890,'bikeSoftCollisionOne','bool(CPhysical::*)(CEntity*, const CColPoint&, float&)'),
    (0x548680,'bikeCollisionTwo','bool(CPhysical::*)(CEntity*, CColPoint&, float&, float&)'),
    (0x54A2C0,'bikeSoftCollisionTwo','bool(CPhysical::*)(CPhysical*, CColPoint&, float&, float&)'),
    (0x544D50,'bikeCollisionAlt','bool CPhysical::ApplyCollisionAlt(CPhysical* entity, CColPoint& colPoint, float& damageIntensity, CVector& outVecMoveSpeed, CVector& outVecTurnSpeed)'),
    (0x6B5A10,'bikeDamageKnockOff','bool CBike::DamageKnockOffRider(CVehicle* vehicle, float damageIntensity, uint16 pieceType, CEntity* damager, const CVector& collisionPos, const CVector& collisionImpactVelocity)')]:
    source=bike_source if address==0x6B5A10 else physical_source
    assert f'0x{address:X}' in source and signature in source, 'Contact-time ABI mismatch: '+callback
    if address!=0x54A2C0:
        assert f'{{0x{address:X},reinterpret_cast<void*>(&{callback})' in adapter, 'Missing contact-time hook: '+callback
    else:
        assert '{0x54A2C0,reinterpret_cast<void*>' not in adapter, 'The executable reads54A2C1 as data: never hook this entry'
        assert native_pe.get_data(0x406B2B-imagebase,5)==bytes.fromhex('a1 c1 a2 54 00'), 'Expected native code/data alias changed'
        for site in [0x54BE1C,0x54C098,0x54C35A,0x54C54D]:
            op=native_pe.get_data(site-imagebase,5)
            assert op[0]==0xe8 and site+5+struct.unpack_from('<i',op,1)[0]==address, 'Native collision CALL mismatch'
    decoded=list(native_decoder.disasm(native_pe.get_data(address-imagebase,32),address))
    assert decoded and decoded[0].address==address and sum(i.size for i in decoded)>=5, 'Invalid native hook boundary: '+callback
    assert decoded[0].mnemonic not in ('ret','int3'), 'Not an executable function entry: '+callback
assert native_pe.get_data(0x73A7B0-imagebase,5)==bytes.fromhex('3d bf 00 00 00')
# Ordinary CPed::UpdatePosition XY branch, preserving the original function
# entry hook and all native support/slope/Z routes. Full x86 branch replay is
# in research/structural-physics-20261101/test-original-ped-motor-branch.cmd.
assert native_pe.get_data(0x5E1E4C-imagebase,6)==bytes.fromhex('d9 86 50 05 00 00'), 'Ordinary motor overwrite instruction changed'
motor_native=list(native_decoder.disasm(native_pe.get_data(0x5E1E4C-imagebase,0x180),0x5E1E4C))
assert motor_native[0].mnemonic=='fld' and motor_native[0].size==6
assert any(i.address==0x5E1E52 for i in motor_native) and any(i.address==0x5E1F99 for i in motor_native), 'Motor fallback/exit is not an instruction boundary'
# Renderer barrier and no-render Idle fallback. In particular, never replace
# SpecialHeliPreRender globally: only this verified call follows all ped/bike lists.
for address,signature in [(0x6BD090,'6a ff 68 21 83 84 00'),(0x6C0810,'6a ff 68 01 84 84 00')]:
    assert native_pe.get_data(address-imagebase,len(bytes.fromhex(signature)))==bytes.fromhex(signature),f'Frame phase entry changed: {address:x}'
assert native_pe.get_data(0x5539FC-imagebase,5)==bytes.fromhex('e8 4f 0d 17 00'), 'End-of-Prerender CALL changed'
assert native_pe.get_data(0x53ECBD-imagebase,5)==bytes.fromhex('e8 5e fc ff ff'), 'Native Idle caller changed'
assert native_pe.get_data(0x53ECB8-imagebase,5)==bytes.fromhex('8b 54 24 08 52'), 'Idle render argument ABI changed'
assert native_pe.get_data(0x53ECC2-imagebase,3)==bytes.fromhex('83 c4 04'), 'Idle cdecl caller cleanup changed'
frame_phase=(root/'src/sa-delirium/BikeRenderPhase.hpp').read_text()
assert '{0x53E920,reinterpret_cast<void*>' not in frame_phase, 'SilentPatch owns Idle entry: interpose verified caller only'
# modloader redirects the 0x53ECBD CALL at runtime before our setup thread runs.
# Chain the CURRENT rel32 destination (native or another mod's hook); never fix
# the callee address or require pristine runtime bytes (refused all of Delirium,
# manual session 20261101-151742).
assert '{0x5539FC,0x6C4750,{}' in frame_phase and '{0x53ECBD,0x53E920' not in frame_phase, 'Idle caller is chained on the main thread only'
assert 'static void maintainIdleCaller()' in frame_phase and 'maintainIdleCaller();' in adapter
assert '*s.chain=reinterpret_cast<void*>(destination)' in frame_phase
assert 'reinterpret_cast<void(__cdecl*)()>(0x6C4750)' not in frame_phase, 'Call the chained callee, not the native one directly'
assert 'installBikeRenderPhase(&finishPhysicsFrame,&physicsFrameNeedsCompletion)' in adapter
# Current rider pose is captured from the one native animation update, never
# from a merely finite old skin palette or a replay of animation callbacks.
assert native_pe.get_data(0x535F94-imagebase,5)==bytes.fromhex('e8 57 d5 f9 ff'), 'Rider animation evaluation CALL changed'
assert native_pe.get_data(0x535F00-imagebase,3)==bytes.fromhex('56 8b f1'), 'Native animation entity owner in ESI changed'
assert native_pe.get_data(0x535F99-imagebase,3)==bytes.fromhex('83 c4 0c'), 'Rider animation cdecl cleanup changed'
assert native_pe.get_data(0x5DF910-imagebase,8)==bytes.fromhex('64 a1 00 00 00 00 6a ff'), 'Native seated-root entry changed'
assert '0x5DF910' in (sdk/'CPed.cpp').read_text()
assert '0x4D34F0' in (sdk/'common.cpp').read_text()
assert 'damage->anim=191' in (root/'src/sa-delirium/CombatRuntime.hpp').read_text()
table=pe+24+struct.unpack_from('<H',exe,pe+20)[0];rva=0x5f12ca-imagebase
for n in range(sections):
    off=table+40*n;size,va,rawsize,raw=struct.unpack_from('<4I',exe,off+8)
    if va<=rva<va+max(size,rawsize):
        at=raw+rva-va;assert exe[at]==0xe8 and 0x5f12cf+struct.unpack_from('<i',exe,at+1)[0]==0x542b50
        break
else:raise AssertionError('KillPedWithCar ApplyForce call not found')
assert '// 0x548680\nbool CPhysical::ApplyCollision(CEntity* theEntity, CColPoint& colPoint, float& thisDamageIntensity, float& entityDamageIntensity)' in (root/'research/native-abi-reference/Physical.cpp').read_text()
assert 'bool (__thiscall *)(CPhysical*, CPhysical*, CColPoint&, float&, float&))0x548680' in (sdk/'CPhysical.cpp').read_text()
assert 'RH_ScopedInstall(Constructor, 0x667CD0)' in (root/'research/native-abi-reference/TaskSimpleGoToPoint.cpp').read_text()
assert 'VALIDATE_SIZE(CTaskSimpleGoToPoint, 0x30)' in (root/'research/native-abi-reference/TaskSimpleGoToPoint.h').read_text()
assert '// 0x543490\nvoid CPhysical::AddCollisionRecord' in (root/'research/native-abi-reference/Physical.cpp').read_text()
for member,offset in [('m_pHierarchy','0x14'),('m_fBlendAmount','0x18'),('m_fBlendDelta','0x1C'),('m_fCurrentTime','0x20'),('m_fSpeed','0x24'),('m_nFlags','0x2E')]:
    assert f'VALIDATE_OFFSET(CAnimBlendAssociation, {member}, {offset})' in (sdk/'CAnimBlendAssociation.h').read_text()
for source,address in [('CWorld.cpp','0x563220'),('CWorld.cpp','0x563280'),('CEntity.cpp','0x571A00'),('common.cpp','0x4D68B0')]:
    assert address in (sdk/source).read_text(), 'Lifecycle ABI missing: '+source+' '+address
assert 'VALIDATE_OFFSET(CPhysical, m_pDamageEntity, 0xDC)' in (sdk/'CPhysical.h').read_text()
assert 'GetTaskManager().HasAnyOf<TASK_COMPLEX_LEAVE_CAR>()' in (root/'research/native-abi-reference/Ped.cpp').read_text()
assert 'VALIDATE_SIZE(CTaskSimpleStandStill, 0x20)' in (sdk/'CTaskSimpleStandStill.h').read_text()
assert '0x62F310' in (sdk/'CTaskSimpleStandStill.cpp').read_text()
assert '0x4D34F0' in (sdk/'common.cpp').read_text()
for source,address in [('CPhysical.cpp','0x5429F0'),('CPhysical.cpp','0x542560'),('CPhysical.cpp','0x543D60'),('CPhysical.cpp','0x543E90')]:
    assert address in (sdk/source).read_text(), 'Body contact ABI missing: '+address
assert 'RH_ScopedVMTInstall(ProcessSuspension, 0x6AFB10)' in (root/'research/native-abi-reference/Automobile-20261029.cpp').read_text()
assert 'SURFACE_PED = 62' in (sdk/'enums/eSurfaceType.h').read_text()
for member,offset in [('m_wheelColPoint','0x724'),('m_fWheelsSuspensionCompression','0x7D4'),('m_aSuspensionSpringLength','0x878'),('m_aSuspensionLineLength','0x888'),('m_pWheelCollisionEntity','0x8E8')]:
    assert f'VALIDATE_OFFSET(CAutomobile, {member}, {offset})' in (sdk/'CAutomobile.h').read_text()
assert 'VALIDATE_SIZE(CColLine, 0x20)' in (sdk/'CColLine.h').read_text()
assert 'VALIDATE_OFFSET(CPhysical, m_nFakePhysics, 0xB8)' in (sdk/'CPhysical.h').read_text()
for source,address in [('CPhysical.cpp','0x542CE0'),('CPhysical.cpp','0x542B50'),('CTimer.cpp','0xB7CB5C')]:
    assert address in (sdk/source).read_text(), 'Coupled body contact ABI missing: '+address
assert '0x532B20' in (sdk/'CEntity.cpp').read_text()
assert '0x532B00' in (sdk/'CEntity.cpp').read_text()
assert '0x446F90' in (sdk/'CEntity.cpp').read_text()
assert 'EVENT_DAMAGE = 9' in (root/'research/native-abi-reference/eEventType.h').read_text()
assert 'COMMAND_WARP_CHAR_INTO_CAR = 0x036A' in (sdk/'enums/eScriptCommands.h').read_text()
assert 'COMMAND_SET_CAR_HEADING = 0x0175' in (sdk/'enums/eScriptCommands.h').read_text()
assert 'auto WarpCharIntoCar(CPed& ped, CVehicle& veh)' in (root/'research/native-abi-reference/CharacterCommands.cpp').read_text()
assert 'VALIDATE_OFFSET(CPed, m_pPlayerData, 0x480)' in (sdk/'CPed.h').read_text()
assert 'RH_ScopedVMTInstall(Teleport, 0x5E4110)' in (root/'research/native-abi-reference/Ped.cpp').read_text()
for file,address in [('PedDamageResponseCalculator.cpp','0x4B5AC0'),('World.cpp','0x56BA00'),('World.cpp','0x56B790'),('TaskSimpleFight.cpp','0x629920'),('TaskSimpleCarJumpOut.cpp','0x64DD60'),('Camera.cpp','0x52B730')]:
    assert address in (root/'research/native-abi-reference'/file).read_text(), 'Extension ABI missing: '+file
for file,address in [('CPad.cpp','0x53F3C0'),('CDraw.cpp','0x6FF410')]:
    assert address in (sdk/file).read_text()
assert 'VALIDATE_SIZE(CCam, 0x238)' in (sdk/'CCam.h').read_text()
assert 'VALIDATE_OFFSET(CMouseControllerState, wheelUp, 0x3)' in (sdk/'CPad.h').read_text()
assert 'VALIDATE_OFFSET(CPedDamageResponseCalculator, m_weaponType, 0xC)' in (sdk/'CPedDamageResponseCalculator.h').read_text()
assert 'VALIDATE_OFFSET(CTaskSimpleFight, m_pAnim, 0x1C)' in (sdk/'CTaskSimpleFight.h').read_text()
assert 'VALIDATE_OFFSET(CAnimBlendHierarchy, m_fTotalTime, 0x10)' in (sdk/'CAnimBlendHierarchy.h').read_text()
assert 'RH_ScopedInstall(Constructor, 0x64BE40)' in (root/'research/native-abi-reference/TaskSimpleCarJumpOut.cpp').read_text()
assert '0x620E30' in (sdk/'CTaskComplexKillPedOnFoot.cpp').read_text()
assert 'VALIDATE_SIZE(CTaskComplexKillPedOnFoot, 0x38)' in (sdk/'CTaskComplexKillPedOnFoot.h').read_text()
assert 'COMMAND_WARP_CHAR_INTO_CAR_AS_PASSENGER = 0x0430' in (sdk/'enums/eScriptCommands.h').read_text()
# Packed one-bit CPed flags begin at 0x46C, as shown by the adjacent 0x46D
# bInVehicle field used above; bFallenDown is flag 84 (byte +10, bit 4).
ped_flags=re.sub(r'//[^\n]*','',(sdk/'CPed.h').read_text()).split('bool bIsStanding : 1;')[1].split('bool bFallenDown : 1;')[0]
assert len(re.findall(r'bool\s+\w+\s*:\s*1\s*;',ped_flags))+1==84
assert 'VALIDATE_OFFSET(CPhysical, m_pEntityIgnoredCollision, 0x128)' in (sdk/'CPhysical.h').read_text()
assert 'VALIDATE_OFFSET(CPed, m_nFightingStyle, 0x72D)' in (sdk/'CPed.h').read_text()
assert 'STYLE_STANDARD = 4' in (sdk/'CPed.h').read_text()
assert '0x681B60' in (sdk/'CTaskManager.cpp').read_text()
assert 'TASK_SECONDARY_ATTACK = 0' in (sdk/'CTaskManager.h').read_text()
assert 'ANIM_DEFAULT_IDLE_STANCE = 3' in (sdk/'enums/eAnimations.h').read_text()
assert '0x5F0360' in (sdk/'CPed.cpp').read_text()
for address in ['0x4D15E0','0x4D6AB0']:
    assert address in (sdk/'common.cpp').read_text(), 'Session animation query ABI missing: '+address
assert '0x54EF40' in (sdk/'CSimpleTransform.cpp').read_text()
assert 'VALIDATE_OFFSET(CPlaceable, m_placement, 0x4)' in (sdk/'CPlaceable.h').read_text()
assert 'COMMAND_GIVE_WEAPON_TO_CHAR = 0x01B2' in (sdk/'enums/eScriptCommands.h').read_text()
assert 'COMMAND_SET_CURRENT_CHAR_WEAPON = 0x01B9' in (sdk/'enums/eScriptCommands.h').read_text()
assert '0x743C60' in (sdk/'CWeaponInfo.cpp').read_text()
assert 'CallMethodAndReturn<int32, 0x5E2530, CPed*, CEntity*, CColPoint*>' in (root/'research/native-abi-reference/Ped.cpp').read_text()
assert 'bool CWeapon::GenerateDamageEvent(CPed* victim, CEntity* creator, eWeaponType weaponType, int32 damageFactor, ePedPieceTypes pedPiece, uint8 direction)' in (root/'research/native-abi-reference/Weapon.cpp').read_text()
assert 'VALIDATE_OFFSET(CWeaponInfo, m_nAnimToPlay, 0x1C)' in (sdk/'CWeaponInfo.h').read_text()
for member,offset in [('m_nAnimGroup','0xE'),('m_fBlendAmount','0x18'),('m_fBlendDelta','0x1C'),('m_fCurrentTime','0x20'),('m_fSpeed','0x24'),('m_nAnimId','0x2C')]:
    assert f'VALIDATE_OFFSET(CAnimBlendAssociation, {member}, {offset})' in (sdk/'CAnimBlendAssociation.h').read_text()
assert '// 0x54A2C0\nbool CPhysical::ApplySoftCollision(CPhysical* physical, CColPoint& colPoint, float& thisDamageIntensity, float& entityDamageIntensity)' in (root/'research/native-abi-reference/Physical.cpp').read_text()
assert 'VALIDATE_OFFSET(CPhysical, m_vecTurnSpeed, 0x50)' in (sdk/'CPhysical.h').read_text()
assert 'void(__thiscall *)(CPed*, CVehicle*, float, bool))0x5F0360' in (root/'research/native-abi-reference/Ped.cpp').read_text()
for name, address in [('GetExitVehicle','0x5400D0'),('ExitVehicleJustDown','0x540120')]:
    assert f'bool CPad::{name}()' in (sdk/'CPad.cpp').read_text()
    assert f'CallMethodAndReturn<bool, {address}, CPad *>' in (sdk/'CPad.cpp').read_text()
assert 'GLOBAL_ADDRESS_BY_VERSION(0xB6F028, 0, 0, 0, 0, 0)' in (sdk/'CCamera.cpp').read_text()
for member,offset in [('m_nActiveCam','0x59'),('m_aCams','0x174')]:
    assert f'VALIDATE_OFFSET(CCamera, {member}, {offset})' in (sdk/'CCamera.h').read_text()
assert 'VALIDATE_OFFSET(CCam, m_vecFront, 0x190)' in (sdk/'CCam.h').read_text()

# Experimental skin capture is a read-only test observer; validate every leaf
# API and the actual engine-global default-pipeline addressing expression.
renderware=(sdk/'RenderWare.cpp').read_text()
for address,expected in [
    (0x7C7550,'8b 44 24 04 8b 0d a8 78 c9 00 8b 04 01 c3'),
    (0x7C77E0,'8b 44 24 04 8b 00 c3'),
    (0x7C77F0,'8b 44 24 04 8b 40 18 c3'),
    (0x7C7800,'8b 44 24 04 8b 40 14 c3'),
    (0x7C7810,'8b 44 24 04 8b 40 0c c3'),
    (0x7C7540,'8b 44 24 04 8b 0d a4 78 c9 00 8b 04 01 c3'),
    (0x812140,'8b 44 24 04 8b 0d 74 ab c9 00 8b 04 01 85 c0 75 01 c3 8b 40 30 c3'),
    (0x749B70,'8b 44 24 04 53 55 56 57 8d 78 08'),
]:
    assert f'0x{address:X}' in renderware, f'Skin capture SDK entry missing: {address:x}'
    actual=native_pe.get_data(address-imagebase,len(bytes.fromhex(expected)))
    assert actual==bytes.fromhex(expected), f'Skin capture leaf ABI differs: {address:x}'
assert native_pe.get_data(0x7491CC-imagebase,15)==bytes.fromhex('a1 24 7b c9 00 8b 0d 60 bc c9 00 8b 44 01 3c'), 'Default pipeline must use engine+offset+0x3c'
assert '#define rwFRAMEPRIVATEHIERARCHYSYNCLTM  0x01' in (sdk/'rw/rwcore.h').read_text()
assert '#define rwFRAMEPRIVATESUBTREESYNCLTM    0x04' in (sdk/'rw/rwcore.h').read_text()

# Passive ground-authority observer preserves these complete displaced stores.
for address, expected in [(0x5E2C71, '8b 46 14 d9 58 38'),
                          (0x5E2C7F, '8b 46 14 d9 58 38'),
                          (0x5E31B8, 'd8 64 24 14 d9 59 38'),
                          (0x5E30B4, 'c7 46 4c 00 00 00 00')]:
    assert native_pe.get_data(address-imagebase,len(bytes.fromhex(expected)))==bytes.fromhex(expected), f'Ground observer store differs: {address:x}'
