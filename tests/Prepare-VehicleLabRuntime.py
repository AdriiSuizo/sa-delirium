"""Prepare an isolated native adapter with the first-car diagnostic fixture."""
import argparse
from pathlib import Path
import shutil
import subprocess
import sys
from road_prop_fixture import inject as inject_road_prop_fixture


def replace_once(source, before, after):
    if source.count(before) != 1:
        raise ValueError(f"Native adapter anchor changed: {before}")
    return source.replace(before, after)


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--out", type=Path, required=True)
parser.add_argument("--shipping", action="store_true", help="Omit the game bridge and scripted fixture exports")
args = parser.parse_args()
here = Path(__file__).resolve().parent
builder = here.parent / "installer/portable/Prepare-DevelopmentRuntime.py"
subprocess.run([sys.executable, str(builder), "--out", str(args.out),
                "--include-test-diagnostics"], check=True)
shutil.copyfile(here / "VehicleLab.hpp", args.out / "VehicleLab.hpp")
shutil.copyfile(here / "VehicleMenu.hpp", args.out / "VehicleMenu.hpp")
shutil.copyfile(here / "ClassicBodyMotion.hpp", args.out / "ClassicBodyMotion.hpp")
shutil.copyfile(here / "VehicleDeformation.hpp", args.out / "VehicleDeformation.hpp")
shutil.copyfile(here / "VehicleEntryLab.hpp", args.out / "VehicleEntryLab.hpp")
shutil.copyfile(here / "VehicleNitro.hpp", args.out / "VehicleNitro.hpp")
shutil.copyfile(here / "GravityControl.hpp", args.out / "GravityControl.hpp")
shutil.copyfile(here / "DriveByControls.hpp", args.out / "DriveByControls.hpp")
shutil.copyfile(here / "DriveByAim.hpp", args.out / "DriveByAim.hpp")
shutil.copyfile(here / "TrafficDriving.hpp", args.out / "TrafficDriving.hpp")
if not args.shipping:
    shutil.copyfile(here / "VehicleSpeedProbe.hpp", args.out / "VehicleSpeedProbe.hpp")
combat = args.out / "CombatRuntime.hpp"
combat_source = replace_once(combat.read_text(encoding="utf-8"),
    'static bool combatAllowed(){return combat&&ready&&playing()&&player()&&!cutscene()&&!(protectMissions&&scriptMissionActive());}',
    'static bool ivCombatEnabled=false,ivLocomotionEnabled=false;\n'
    'static bool authoredGameplayAllowed(){return combat&&ready&&playing()&&player()&&!cutscene()&&!(protectMissions&&scriptMissionActive());}\n'
    'static bool combatAllowed(){return ivCombatEnabled&&authoredGameplayAllowed();}')
combat_source = replace_once(combat_source,
    '    if(!GetPrivateProfileIntA("Combat","Enabled",0,ini.c_str()))return;',
    '    ivCombatEnabled=GetPrivateProfileIntA("Combat","Enabled",0,ini.c_str())!=0;\n'
    '    ivLocomotionEnabled=GetPrivateProfileIntA("Movement","NikoOnFoot",0,ini.c_str())!=0;\n'
    '    event("gameplay_modes",-1,-1,std::string(ivLocomotionEnabled?"movement=iv":"movement=sa")+(ivCombatEnabled?" combat=iv":" combat=sa"));\n'
    '    if(!ivCombatEnabled&&!ivLocomotionEnabled)return;\n'
    '    loadAuthoredGameplay();\n'
    '}\n'
    'static void loadAuthoredGameplay(){\n'
    '    if(combat)return;')
combat_source = replace_once(combat_source, 'static void initializeCombat(){',
    'static void loadAuthoredGameplay();\nstatic void initializeCombat(){')
if not args.shipping:
    combat_source = replace_once(combat_source, 'static bool combatProbeFatalCounter=false;',
        'static bool combatProbeFatalCounter=false;\nstatic int combatProbeFatalKick=-1;')
    combat_source = replace_once(combat_source, '        const bool motorStart=hit[15]==2;',
        '        const bool motorStart=hit[15]==2;\n'
        '        if(hit[15]==0&&by==player()&&target==combatProbeFatalKick&&combatStrikeBone(int(hit[12]))>=41){\n'
        '            at<float>(p,0x540)=1.f;combatProbeFatalKick=-1;\n'
        '            event("probe_fatal_kick_contact",target,at<short>(p,0x22));\n'
        '        }')
    combat_source = replace_once(combat_source, '    if(finishStrike) targets[cj]=strikeTarget;',
        '    if(finishStrike){targets[cj]=strikeTarget;\n'
        '        event("probe_finish_strike",cj,0,"result="+std::to_string(cjActor->second.state[4])+\n'
        '            " phase="+std::to_string(cjActor->second.state[5]));}')
combat.write_text(combat_source, encoding="utf-8", newline="\n")
niko = args.out / "NikoMovement.hpp"
niko_source = replace_once(niko.read_text(encoding="utf-8"), 'p!=player()||!combatAllowed()',
    'p!=player()||!ivLocomotionEnabled||!authoredGameplayAllowed()')
niko_source = replace_once(niko_source,
    '    const auto ini=(folder.parent_path()/"DeliriumSA.ini").string();\n'
    '    static int enabled=GetPrivateProfileIntA("Movement","NikoOnFoot",0,ini.c_str());if(!enabled)return;\n', '')
niko_source += """
static void vehicle_lab::setNikoMovementEnabled(bool value) {
    if (ivLocomotionEnabled == value) return;
    ivLocomotionEnabled = value;
    if (value) loadAuthoredGameplay();
    else nikoClear();
    event("niko_movement_mode", -1, -1, value ? "iv" : "sa");
}
"""
niko.write_text(niko_source, encoding="utf-8", newline="\n")
aiming = args.out / "Aiming.hpp"
aiming.write_text(replace_once(aiming.read_text(encoding="utf-8"),
    "static void __fastcall cameraProcess(void*camera,void*){\n",
    "static void __fastcall cameraProcess(void*camera,void*){\n"
    "    if(camera==reinterpret_cast<void*>(drive_by_controls::mainCamera))drive_by_controls::restoreNativeCamPose();\n"),
    encoding="utf-8", newline="\n")
expansion = args.out / "ExpansionProbe.hpp"
fixture = expansion.read_text(encoding="utf-8")
fixture = replace_once(fixture, "        for(int model:{461,7})",
    """        char testModel[8]{};
        GetEnvironmentVariableA("GTASA_TEST_BIKE_MODEL",testModel,sizeof(testModel));
        const int requestedModel=std::atoi(testModel);
        const int bikeModel=requestedModel==481||requestedModel==463?requestedModel:461;
        for(int model:{bikeModel,7})""")
fixture = replace_once(fixture, "create.integer(461);create.number(origin.x+8)",
    "create.integer(bikeModel);create.number(origin.x+8)")
expansion.write_text(fixture, encoding="utf-8", newline="\n")
if not args.shipping:
    vehicle_probe = args.out / "VehicleProbe.hpp"
    vehicle_probe.write_text(replace_once(vehicle_probe.read_text(encoding="utf-8"),
        "    if(model!=400&&model!=445&&model!=415)return -1;",
        "    if(model!=400&&model!=445&&model!=415&&model!=410&&model!=470)return -1;"),
        encoding="utf-8", newline="\n")
adapter = args.out / "Adapter.cpp"
source = adapter.read_text(encoding="utf-8")
speed_probe_define = '' if args.shipping else '#define DELIRIUM_VEHICLE_SPEED_PROBE\n'
source = replace_once(source, '#include "BodyContacts.hpp"',
                      'static bool vehicleLabInverseTensor(void*, V*);\n'
                      'static void vehicleLabObserveHullLoad(void*, V, V);\n'
                      'static void* activeCamera();\n'
                      '#include "BodyContacts.hpp"\n' + speed_probe_define +
                      '#include "VehicleLab.hpp"\nnamespace vehicle_entry_probe {static void tick();}')
source = replace_once(source, 'reinterpret_cast<void*>(&processSuspension),',
                      'reinterpret_cast<void*>(&vehicleLabSuspension),')
source = replace_once(source, '    if(MH_ApplyQueued()!=MH_OK)',
                      '    if(!vehicle_lab::install(sharedCore)){status="vehicle_lab_hook_failed";event(status);return 0;}\n'
                      '    if(MH_ApplyQueued()!=MH_OK)')
source = replace_once(source, '    crash_guard::timedGameProcess(originalProcess);',
                      '    gravity_control::tick();\n'
                      '    crash_guard::timedGameProcess(originalProcess);vehicle_lab::maintain();vehicle_entry_probe::tick();')
source = replace_once(source,
    '        a->agent=nm_agent_create(a->rig->build,&feet.x,0,behaviourSeed);if(!a->agent){ok(-1);return false;}',
    '        a->agent=nm_agent_create(a->rig->build,&feet.x,0,behaviourSeed);if(!a->agent){ok(-1);return false;}\n'
    '        if(gravity_control::agentGravity)gravity_control::agentGravity(a->agent,p==player());')

wheel_path = args.out / "WheelBodyContacts.hpp"
wheel = wheel_path.read_text(encoding="utf-8")
wheel = replace_once(wheel, 'static void* suspensionCar=nullptr;',
                     'static void* suspensionCar=nullptr;\n'
                     'static V wheelBodyDeltaVelocity{},wheelBodyTorque{};')
wheel = replace_once(wheel, '    if(!finite(impulse)||length(impulse)<=1.e-5f)return;',
                     '    if(!finite(impulse)||length(impulse)<=1.e-5f)return;\n'
                     '    const V dv=at<V>(car,0x44)-before;\n'
                     '    wheelBodyDeltaVelocity=wheelBodyDeltaVelocity+dv;\n'
                     '    wheelBodyTorque=wheelBodyTorque+cross(c.world-point(*matrix(car),at<V>(car,0xA4)),dv*(50.f*at<float>(car,0x8C)));')
native_inverse = 'const float carInverse=std::max(0.f,dot(carPointVelocity(car,point)-v,n)/nativeJ);'
if wheel.count(native_inverse) != 2:
    raise ValueError("Expected the spring and damping point-mobility sites")
wheel = wheel.replace(
    native_inverse,
    '''float carInverse=std::max(0.f,dot(carPointVelocity(car,point)-v,n)/nativeJ);
        V inverse[3];if(vehicleLabInverseTensor(car,inverse)){
            const V lever=position(car)+point-::point(*matrix(car),at<V>(car,0xA4));
            const V torque=cross(lever,n);
            const V angular{dot(inverse[0],torque),dot(inverse[1],torque),dot(inverse[2],torque)};
            carInverse=1.f/at<float>(car,0x8C)+dot(cross(angular,lever),n);
        }''')
wheel = replace_once(wheel, 'static void __fastcall processSuspension(void*car,void*){',
                     'static bool prepareWheelBodyContacts(void*car){')
wheel = wheel.replace('{originalSuspension(car);return;}', '{return false;}')
wheel = replace_once(wheel, '    originalSuspension(car);suspensionCar=nullptr;\n}',
                     '''    return true;
}
static void __fastcall processSuspension(void*car,void*){
    wheelBodyDeltaVelocity={};wheelBodyTorque={};
    prepareWheelBodyContacts(car);
    originalSuspension(car);suspensionCar=nullptr;
}''')
if not args.shipping:
    wheel = replace_once(wheel, '                observeSolvedAnimatedContact(a,ids[i],j,p,dt);',
        '''                observeSolvedAnimatedContact(a,ids[i],j,p,dt);
                if(a.ref==roadProbePed&&ids[i]>=0&&ids[i]<int(collision->vehicles.size())&&length(j)>2.f){
                    const auto& vehicle=collision->vehicles[ids[i]];
                    if(vehicle.ref==roadProbeVehicle&&vehicleFromRef(vehicle.ref)==vehicle.entity&&matrix(vehicle.entity)){
                        const V local=mul(inverse(transform(*matrix(vehicle.entity))),T{{},p}).p;
                        event("probe_vehicle_body_contact",a.ref,at<short>(vehicle.entity,0x22),
                            "vehicle_ref="+std::to_string(vehicle.ref)+" local="+vec(local)+" impulse="+vec(j));
                    }
                }''')
wheel_path.write_text(wheel, encoding="utf-8", newline="\n")

owner_path = args.out / "ExternalMobility.hpp"
owner = owner_path.read_text(encoding="utf-8")
owner = replace_once(owner,
    '    out.rows[18]=out.rows[22]=noMove?0.f:invTurn;out.rows[26]=invTurn;',
    '''    out.rows[18]=out.rows[22]=noMove?0.f:invTurn;out.rows[26]=invTurn;
    V inverse[3];
    if(!noTurn&&!noMove&&!infinite&&!attached&&vehicleLabInverseTensor(v,inverse)){
        for(int row=0;row<3;row++){
            out.rows[18+row*3]=inverse[row].x;
            out.rows[19+row*3]=inverse[row].y;
            out.rows[20+row*3]=inverse[row].z;
        }
    }''')
owner = replace_once(owner, '        recordRoadHull(o.ref,impulse,torque);',
    '        recordRoadHull(o.ref,impulse,torque);\n'
    '        vehicleLabObserveHullLoad(v,dv,torque);')
owner_path.write_text(owner, encoding="utf-8", newline="\n")
source = replace_once(source, '#include "ExpansionProbe.hpp"',
                      '#include "ExpansionProbe.hpp"\n#include "VehicleEntryLab.hpp"')
if not args.shipping:
    shutil.copyfile(here / "CoverFrameProbe.hpp", args.out / "CoverFrameProbe.hpp")
    shutil.copyfile(here / "VehicleDeformationProbe.hpp", args.out / "VehicleDeformationProbe.hpp")
    shutil.copyfile(here / "GameplayRegressionProbe.hpp", args.out / "GameplayRegressionProbe.hpp")
    source = replace_once(source, 'namespace player_cover {',
                          'namespace player_cover {\n    static void traceFrame(void*);')
    source = replace_once(source, '#include "PoliceCover.hpp"',
                          '#include "PoliceCover.hpp"\n#include "CoverFrameProbe.hpp"')
    source = replace_once(source, '    applyWalkingSupport(p);',
                          '    applyWalkingSupport(p);\n    player_cover::traceFrame(p);')
    source = replace_once(source, '#include "VehicleEntryLab.hpp"',
                          '#include "VehicleEntryLab.hpp"\n#include "VehicleDeformationProbe.hpp"\n#include "GameplayRegressionProbe.hpp"')
    source = replace_once(source, '    if(mode==45&&ready&&playing())return recoveryAuditFixture(mode);',
                          '    if(mode>=380&&mode<=389)return deformationFixture(mode);\n'
                          '    if((mode>=390&&mode<=398)||(mode>=400&&mode<=444)||(mode>=1900&&mode<=1915))return gameplayRegressionFixture(mode);\n'
                          '    if(mode==45&&ready&&playing())return recoveryAuditFixture(mode);')
source = replace_once(source,
    '    if(mode==45&&ready&&playing())return recoveryAuditFixture(mode);',
    '''    if(((mode>=346&&mode<=349)||(mode>=351&&mode<=358))&&GetEnvironmentVariableA("GTASA_PASSIVE_TEST_WINDOW",nullptr,0)&&
       GetEnvironmentVariableA("GTASA_VEHICLE_MENU_TEST",nullptr,0)){
        using namespace vehicle_lab::vehicle_menu;
        auto*manager=reinterpret_cast<void*>(frontend);
        if(mode==349)return vehicle_lab::traffic;
        if(mode==353)return int(selected);
        if(mode==354)return int(active);
        if(mode==357){
            auto* pool=vehiclePool();
            if(!pool||!player())return -357;
            unsigned count=0;
            for(int i=0;i<pool->size;++i)if(!(pool->flags[i]&0x80)){
                auto* car=pool->objects+i*0xA18;
                if(!vehicle_lab::ordinary(car)||at<unsigned char>(car,0x4A4)!=1||
                   !at<void*>(car,0x460)||at<void*>(car,0x460)==player())continue;
                if(length(position(car)-position(player()))<140.f)++count;
            }
            return count;
        }
        if(mode==358)return !ivLocomotionEnabled?0:combat?1:-358;
        const auto menuFrame=reinterpret_cast<void(__thiscall*)(void*)>(0x576B70);
        if(mode==346){
            if(!at<bool>(manager,0x5C)){
                at<bool>(manager,0x33)=true;
                menuFrame(manager);
            }
            if(!at<bool>(manager,0x5C))return -346;
            reinterpret_cast<void(__thiscall*)(void*,char)>(0x573680)(manager,optionsPage);
            return optionsPage;
        }
        if(!at<bool>(manager,0x5C))return -347;
        if(mode==348){
            at<bool>(manager,0x32)=true;
            menuFrame(manager);
            return at<bool>(manager,0x5C)?-348:0;
        }
        const auto*page=activeOptionsPage();
        const int item=page?itemIndex(*page,mode==351?movementKey:mode==352?combatKey:mode==355?deformationKey:key):-1;
        if(item<0||!nativeProcess)return -349;
        at<int>(manager,0x54)=item;
        bool back=false;
        reinterpret_cast<void(__thiscall*)(void*,signed char,bool*,bool)>(processEntry)(manager,mode==356?-1:0,&back,true);
        if(mode==351)return movementSelected;
        if(mode==352)return combatSelected;
        if(mode==355)return vehicle_deformation::enabled;
        return vehicle_lab::traffic;
    }
    if(mode==45&&ready&&playing())return recoveryAuditFixture(mode);''')
anchor = '    if((mode>=256&&mode<=260)||(mode>=265&&mode<=271))return armedCombatFixture(mode);'
source = replace_once(source, anchor, '''    constexpr int comparisonModels[]={480,411,415,451,429,477,506,541,405,409,421,507,533,555,580,560,562,445};
    if(mode==294){
        if(!vehicle_nitro::installed||inVehicle(player()))return -294;
        if(carFixture(415)<0)return -295;
        ProbeScript seat(0x036A);seat.integer(reference(player()));seat.integer(probeCar);seat.run();
        auto*car=vehicleFromRef(probeCar);
        auto*pad=reinterpret_cast<void*(__cdecl*)(int)>(0x53FB70)(0);
        if(!car||!pad||!vehicle_nitro::key)return -296;
        auto native=reinterpret_cast<void(__thiscall*)(void*,signed char)>(0x6A3EA0);
        const unsigned key=vehicle_nitro::key;
        auto&current=reinterpret_cast<short*>(0xB73190+0x18)[key];
        auto&previous=reinterpret_cast<short*>(0xB72F20+0x18)[key];
        const short savedCurrent=current,savedPrevious=previous;
        const float savedGas=at<float>(car,0x49C);
        const short savedFire=at<short>(pad,0x22);
        current=previous=0;
        at<short>(pad,0x22)=255;
        at<float>(car,0x49C)=1.f;
        native(car,3);
        native(car,0);
        const bool throttleIsolated=at<unsigned char>(car,0x48A)==3&&at<float>(car,0x8A4)==1.f;
        current=255;
        native(car,0);
        const bool pressed=at<unsigned char>(car,0x48A)==2&&at<float>(car,0x8A4)<0.f;
        current=savedCurrent;previous=savedPrevious;
        at<short>(pad,0x22)=savedFire;at<float>(car,0x49C)=savedGas;
        event("probe_vehicle_nitro",probeCar,415,"throttle_isolated="+std::to_string(throttleIsolated)+
              " dedicated_press="+std::to_string(pressed));
        return throttleIsolated&&pressed?0:-297;
    }
    if(mode==297){
        if(!GetEnvironmentVariableA("GTASA_PASSIVE_TEST_WINDOW",nullptr,0)||inVehicle(player()))return -297;
        // Give the native blast an airborne player with an initial upward
        // velocity, then let the live solver carry him through the landing.
        teleport(player(),position(player())+V{0,0,6.f});
        at<V>(player(),0x44)={0,0,.22f};
        return expansionFixture(85);
    }
    if(mode==298){
        const int rider=expansionFixture(250);
        if(rider<0)return rider;
        expansionTarget=rider;
        if(expansionFixture(140)<0)return -298;
        return rider;
    }
    if((mode>=318&&mode<=323)||(mode>=327&&mode<=335)){
        if(!drive_by_controls::installed||!GetEnvironmentVariableA("GTASA_PASSIVE_TEST_WINDOW",nullptr,0))return -318;
        auto*cj=player();
        if(mode==318||mode==331){
            if(!cj||inVehicle(cj)||vehicleFromRef(probeCar))return -318;
            const int model=mode==331?463:412;
            for(int requested:{model,1087,352})reinterpret_cast<void(__cdecl*)(int,int)>(0x4087E0)(requested,2);
            reinterpret_cast<void(__cdecl*)(bool)>(0x40EA10)(false);
            ProbeScript create(0x00A5);create.integer(model);create.number(2406.f);create.number(-1658.f);
            create.number(position(cj).z);create.result();probeCar=create.run();
            auto*car=vehicleFromRef(probeCar);if(!car)return -319;
            reinterpret_cast<void(__thiscall*)(void*,float)>(0x43E0C0)(car,-1.5707963268f);
            ProbeScript seat(0x036A);seat.integer(reference(cj));seat.integer(probeCar);seat.run();
            if(model==412)reinterpret_cast<void(__thiscall*)(void*,int)>(0x6E3290)(car,1087);
            reinterpret_cast<int(__thiscall*)(void*,int,unsigned,bool)>(0x5E6080)(cj,28,90,true);
            reinterpret_cast<void(__thiscall*)(void*,int)>(0x5E6280)(cj,28);
            if(drive_by_controls::armedDriverVehicle(reinterpret_cast<void*>(drive_by_controls::primaryPad))!=car)
                return -320;
            drive_by_controls::hydraulicSuppressions=0;
            drive_by_controls::testInput=true;
            drive_by_controls::testPad={};
        }else if(mode==328){
            auto*car=vehicleFromRef(probeCar);if(!car||!matrix(car))return -328;
            at<unsigned char>(car,0xB8)=0;
            at<V>(car,0x44)=matrix(car)->up*.16f;
        }else if(mode!=323){
            drive_by_controls::testPad={};
            if(mode==319||mode==320||mode==329||mode==330||mode==332||mode==333||mode==335){
                drive_by_controls::testPad.wButtons=XINPUT_GAMEPAD_LEFT_SHOULDER;
            }
            if(mode==319)drive_by_controls::testPad.sThumbRY=-24500;
            if(mode==332)drive_by_controls::testPad.sThumbRY=29000;
            if(mode==330)drive_by_controls::testPad.sThumbRX=32767;
            if(mode==320||mode==327||mode==330||mode==333||mode==335)
                drive_by_controls::testPad.wButtons|=XINPUT_GAMEPAD_RIGHT_SHOULDER;
            if(mode==321||mode==329||mode==332||mode==333)
                drive_by_controls::testPad.bRightTrigger=255;
            if(mode==335)reinterpret_cast<void(__thiscall*)(void*,int)>(0x5E61F0)(cj,0);
            drive_by_controls::sampledFrame=~0u;
        }
        const unsigned slot=at<unsigned char>(cj,0x718);
        const unsigned ammo=at<unsigned>(cj,0x5A0+slot*0x1C+0xC);
        auto*pad=reinterpret_cast<void*>(drive_by_controls::primaryPad);
        const auto padShort=[pad](uintptr_t address){return reinterpret_cast<short(__thiscall*)(void*)>(address)(pad);};
        const auto padBool=[pad](uintptr_t address){return reinterpret_cast<bool(__thiscall*)(void*)>(address)(pad);};
        const auto&view=drive_by_controls::view;
        auto*car=vehicleFromRef(probeCar);
        event("probe_driveby_controls",probeCar,car?at<short>(car,0x22):0,"mode="+std::to_string(mode)+
            " ammo="+std::to_string(ammo)+" hydraulic_blocks="+std::to_string(drive_by_controls::hydraulicSuppressions)+
            " fire="+std::to_string(padShort(0x53FF90))+" throttle="+std::to_string(padShort(0x5403F0))+
            " left="+std::to_string(padBool(0x53FDD0))+" right="+std::to_string(padBool(0x53FE10))+
            " vehicle_x="+std::to_string(padShort(0x53FC50))+" vehicle_y="+std::to_string(padShort(0x53FC10))+
            " camera="+std::to_string(view.ready)+" blend="+std::to_string(view.blend)+
            " yaw="+std::to_string(view.yaw)+" pitch="+std::to_string(view.pitch));
        if(mode>=332){
            // Passive tests have no real GInput input. Verify its active IV-mode
            // getters using the equivalent native pad fields, then restore them.
            auto&padMode=at<short>(pad,0x10A);
            auto&axisX=at<short>(pad,4);
            auto&axisY=at<short>(pad,6);
            auto&trigger=at<short>(pad,0xE);
            const short savedMode=padMode,savedX=axisX,savedY=axisY,savedTrigger=trigger;
            padMode=1;axisX=73;axisY=-63;trigger=drive_by_controls::testPad.bRightTrigger;
            const short gas=padShort(0x5403F0),shot=padShort(0x53FF90);
            const short x=padShort(0x53FC50),y=padShort(0x53FC10);
            padMode=savedMode;axisX=savedX;axisY=savedY;trigger=savedTrigger;
            event("probe_driveby_native_pad",probeCar,car?at<short>(car,0x22):0,
                "mode="+std::to_string(mode)+" weapon_slot="+std::to_string(slot)+
                " throttle="+std::to_string(gas)+" fire="+std::to_string(shot)+
                " vehicle_x="+std::to_string(x)+" vehicle_y="+std::to_string(y));
        }
        return ammo;
    }
    if(mode>=324&&mode<=326){
        if(!vehicle_lab::auditTraffic||inVehicle(player())||vehicleFromRef(probeCar))return -324;
        const bool law=mode!=326;
        for(int model:{445,596,405,280,7})reinterpret_cast<void(__cdecl*)(int,int)>(0x4087E0)(model,2);
        reinterpret_cast<void(__cdecl*)(bool)>(0x40EA10)(false);
        const auto spawn=[](int model,float x){
            ProbeScript create(0x00A5);create.integer(model);create.number(x);create.number(-1658.f);
            create.number(position(player()).z);create.result();
            const int ref=create.run();
            if(auto*car=vehicleFromRef(ref))
                reinterpret_cast<void(__thiscall*)(void*,float)>(0x43E0C0)(car,-1.5707963268f);
            return ref;
        };
        probeCar=spawn(445,2423.f);
        const int follower=spawn(law?596:405,2409.f);
        auto*target=vehicleFromRef(probeCar);auto*car=vehicleFromRef(follower);
        if(!target||!car)return -325;
        expansionTarget=reference(player());
        if(expansionFixture(140)<0)return -326;
        ProbeScript driver(0x0129);driver.integer(follower);driver.integer(law?6:4);
        driver.integer(law?280:7);driver.result();
        if(!pedFromRef(driver.run()))return -327;
        ProbeScript speed(0x00AD);speed.integer(follower);speed.number(32.f);speed.run();
        if(law){
            ProbeScript wanted(0x010D);wanted.integer(0);wanted.integer(mode==324?1:3);wanted.run();
            at<unsigned char>(car,0x428)|=1;
            at<unsigned char>(car,0x3BA)=mode==324?44:3;
        }else{
            reinterpret_cast<void(__cdecl*)(void*,void*,float)>(0x41C960)(car,target,8.f);
        }
        at<unsigned char>(target,0xB8)=at<unsigned char>(car,0xB8)=0;
        at<V>(target,0x44)=matrix(target)->up*.12f;
        at<V>(car,0x44)=matrix(car)->up*.18f;
        event("probe_traffic_pursuit_target",follower,at<short>(car,0x22),"case="+std::to_string(mode));
        return follower;
    }
    if(mode==338||mode==339)return vehicle_entry_probe::begin(mode);
    const auto startCjFallProbe=[&](V release){
        auto*p=player();
        if(!GetEnvironmentVariableA("GTASA_PASSIVE_TEST_WINDOW",nullptr,0)||!p||inVehicle(p))return -340;
        teleport(p,release);
        at<V>(p,0x44)={0,0,-.04f};
        at<unsigned char>(p,0x46C)&=~3u;
        void*task=reinterpret_cast<void*(__cdecl*)(unsigned)>(0x61A5A0)(0x10);
        if(!task)return -341;
        reinterpret_cast<void*(__thiscall*)(void*,bool,bool)>(0x678C80)(task,false,false);
        reinterpret_cast<void(__thiscall*)(void*,void*,int,bool)>(0x681AF0)(taskManager(p),task,0,true);
        return reference(p);
    };
    if(mode==340){
        if(!player())return -340;
        const int ref=startCjFallProbe(position(player())+V{0,0,7.f});
        if(ref>=0)event("probe_cj_high_fall",ref,at<short>(player(),0x22),"native_air_task=1 start_offset_m=7");
        return ref;
    }
    const bool fleetProbe=mode>=800&&mode<=1011;
    const bool modelCorner=mode>=2400&&mode<=2611;
    if(mode>=1400000&&mode<1612000){
        // Use a previously measured top speed for a paired run at the same entry speed.
        const int model=(mode-1000000)/1000;
        const float measured=static_cast<float>(mode%1000)*.1f;
        if(measured<18.f||measured>90.f||!GetEnvironmentVariableA("GTASA_PASSIVE_TEST_WINDOW",nullptr,0))return -307;
        vehicle_lab::speed_probe::measuredTop[model]=measured;
        return model;
    }
    const bool runwayProbe=mode>=3400&&mode<=6611&&mode%1000>=400&&mode%1000<=611;
    const bool rightAngle=mode==342||mode==343||modelCorner;
    const bool curveProbe=mode==336||mode==337||mode==341||rightAngle;
    if(runwayProbe||fleetProbe||curveProbe||(mode>=300&&mode<300+std::size(comparisonModels))){
        if(vehicle_lab::carRef>=0||!playing())return -300;
        if(inVehicle(player())){
            auto*previous=vehicleFromRef(probeCar);
            if(runwayProbe&&previous&&at<void*>(previous,0x460)==player()){
                at<V>(previous,0x44)={};
                at<V>(previous,0x50)={};
            }
            const V observer=position(player());
            ProbeScript leave(0x0362);leave.integer(reference(player()));
            leave.number(observer.x);leave.number(observer.y);leave.number(observer.z+1.f);leave.run();
            if(inVehicle(player()))return -303;
        }
        if(vehicleFromRef(probeCar)){
            ProbeScript remove(0x00A6);remove.integer(probeCar);remove.run();
        }
        const int model=runwayProbe?mode%1000:modelCorner?mode-2000:fleetProbe?mode-400:curveProbe?((mode==337||mode==343)?480:445):comparisonModels[mode-300];
        const int speedMode=runwayProbe?mode/1000-2:0;
        if((speedMode==2||speedMode==3)&&!vehicle_lab::speed_probe::measuredTop.contains(model))return -307;
        const bool remote=model==441||model==594;
        reinterpret_cast<void(__cdecl*)(int,int)>(0x4087E0)(model,2);
        reinterpret_cast<void(__cdecl*)(bool)>(0x40EA10)(false);
        const auto vertical=reinterpret_cast<bool(__cdecl*)(const V&,float,ColPoint&,void*&,bool,bool,bool,bool,bool,bool,void*)>(0x5674E0);
        V road=runwayProbe?vehicle_lab::speed_probe::runway:rightAngle?V{2450.f,-1668.f,38.f}:V{2406.f,-1658.f,38.f};
        if(runwayProbe){
            reinterpret_cast<void(__cdecl*)(const V*)>(0x40ED80)(&road);
        }
        ColPoint ground{};void*support=nullptr;
        if(!vertical(road,road.z-50,ground,support,true,false,false,true,true,false,nullptr))return -304;
        road.z=ground.point.z;
        if(runwayProbe){
            if(road.z<11.f||road.z>15.f)return -306;
            teleport(player(),road+V{0,-4.f,1.f});
            at<V>(player(),0x44)={};
        }
        ProbeScript spawn(0x00A5);spawn.integer(model);spawn.number(road.x);
        spawn.number(road.y);spawn.number(road.z);spawn.result();
        probeCar=spawn.run();
        auto*car=vehicleFromRef(probeCar);
        if(!car||!matrix(car))return -301;
        // Native CVehicle::GetHeightAboveRoad (vtable slot 53). The truck's
        // origin can be below the ground where CREATE_CAR initially puts it.
        const auto heightAboveRoad=reinterpret_cast<float(__thiscall*)(void*)>((*static_cast<void***>(car))[53]);
        const float height=heightAboveRoad(car);
        if(!std::isfinite(height)||height<0.f||height>6.f)return -304;
        road.z=ground.point.z+height+.01f;
        teleport(car,road);
        reinterpret_cast<void(__thiscall*)(void*,float)>(0x43E0C0)(car,-1.5707963268f);
        if(remote){
            at<unsigned char>(car,0x36)=(at<unsigned char>(car,0x36)&7)|(8<<3);
            at<unsigned char>(car,0xB8)=0;
        }else{
            ProbeScript seat(0x036A);seat.integer(reference(player()));seat.integer(probeCar);seat.run();
            if(!inVehicle(player())||at<void*>(car,0x460)!=player())return -305;
        }
        vehicle_lab::fixtureDuration=speedMode==1?vehicle_lab::speed_probe::topRunMs:
            speedMode==4?vehicle_lab::speed_probe::launchRunMs:
            runwayProbe?vehicle_lab::speed_probe::turnRunMs:fleetProbe?1200:5500;
        const float entrySpeed=curveProbe?(rightAngle?12.f:mode==337?24.f:mode==341?18.f:16.f):0.f;
        if(!vehicle_lab::begin(probeCar,true,nullptr,0.f,false,true,entrySpeed))return -302;
        if(runwayProbe){
            vehicle_lab::speed_probe::mode=speedMode;
            vehicle_lab::speed_probe::startHeight=road.z;
            if(speedMode==1)vehicle_lab::speed_probe::measuredTop[model]=0.f;
        }
        vehicle_lab::hardSwerve=mode==341;
        vehicle_lab::rightAngleTurn=rightAngle;
        return probeCar;
    }
    if(mode==286){
        if(!vehicle_lab::auditTraffic || vehicleFromRef(probeCar))return -286;
        if(carFixture(445)<0 || expansionFixture(133)<0)return -286;
        ProbeScript remove(0x009B);remove.integer(expansionTarget);remove.run();
        ProbeScript driver(0x0129);driver.integer(probeCar);driver.integer(4);driver.integer(7);driver.result();
        const int driverRef=driver.run();if(!pedFromRef(driverRef))return -286;
        ProbeScript speed(0x00AD);speed.integer(probeCar);speed.number(20.f);speed.run();
        ProbeScript style(0x00AE);style.integer(probeCar);style.integer(0);style.run();
        ProbeScript goal(0x00A7);goal.integer(probeCar);goal.number(2345.f);goal.number(-1575.f);goal.number(23.7f);goal.run();
        teleport(player(),V{2410.f,-1625.f,14.f});
        vehicle_lab::fixtureDuration=35000;
        if(!vehicle_lab::begin(probeCar,false))return -286;
        event("vehicle_lab_route_setup",probeCar,445,"target_x=2345 target_y=-1575 native_driver=1");
        return probeCar;
    }
    if(mode==287 || mode==288){
        if(carFixture(445)<0 || expansionFixture(133)<0)return -287;
        auto*car=vehicleFromRef(probeCar);auto*ped=pedFromRef(expansionTarget);
        if(!car||!ped)return -287;
        if(mode==288){
            auto*col=reinterpret_cast<ColModel*(__thiscall*)(void*)>(0x535300)(car);
            if(!col)return -288;
            const V drop=position(car)+matrix(car)->up*.9f+V{0,0,col->box.max.z+.9f};
            teleport(ped,drop);at<V>(ped,0x44)={};
        }
        if(DeliriumSA_ProbeGameDamage(expansionTarget,1)<0)return -287;
        if(mode==288&&!vehicle_lab::begin(probeCar,false))return -288;
        return expansionTarget;
    }
    if(mode==289){
        if(expansionFixture(128)<0||!vehicle_lab::begin(probeCar,false))return -289;
        return expansionFixture(126);
    }
    if(mode==356){
        if(carFixture(445)<0||expansionFixture(133)<0)return -356;
        auto*car=vehicleFromRef(probeCar);auto*ped=pedFromRef(expansionTarget);
        if(!car||!ped||!matrix(car))return -356;
        roadProbePed=expansionTarget;roadProbeVehicle=probeCar;
        roadProbeSide=matrix(car)->right*-1.f;roadProbeForward=matrix(car)->up;
        roadProbeCameraUntil=gameTime()+12000;
        vehicle_lab::fixtureDuration=10000;
        if(!vehicle_lab::begin(probeCar,false))return -356;
        return probeCar;
    }
    if(mode>=357&&mode<=359){
        auto*car=vehicleFromRef(probeCar);auto*ped=pedFromRef(expansionTarget);
        if(!car||!ped||!matrix(car)||vehicle_lab::carRef!=probeCar)return -357;
        if(mode==359){
            if(auto a=agents.find(expansionTarget);a!=agents.end()){
                restore(*a->second,"hood_probe_unload",false);agents.erase(a);
            }
            pending.erase(expansionTarget);
            ProbeScript remove(0x009B);remove.integer(expansionTarget);remove.run();
            event("vehicle_hood_unloaded",probeCar,445);
            return probeCar;
        }
        const auto*col=reinterpret_cast<ColModel*(__thiscall*)(void*)>(0x535300)(car);
        if(!col)return -357;
        const float height=mode==357?5.f:.7f;
        const V release=point(*matrix(car),{-.4f,1.35f,col->box.max.z})+V{0,0,height};
        teleport(ped,release);at<V>(ped,0x44)={};
        if(DeliriumSA_ProbeGameDamage(expansionTarget,1)<0)return -357;
        watchRoadVehicle(probeCar,expansionTarget);
        event("vehicle_hood_drop",probeCar,445,"height_m="+std::to_string(height));
        return probeCar;
    }
    if(mode==360||mode==361||mode==364){
        const int model=mode==360?410:mode==361?470:445;
        if(carFixture(model)<0||expansionFixture(133)<0)return -360;
        ProbeScript remove(0x009B);remove.integer(expansionTarget);remove.run();
        auto*car=vehicleFromRef(probeCar);
        if(!car||!matrix(car)||!player())return -360;
        roadProbePed=reference(player());roadProbeVehicle=probeCar;
        roadProbeSide=matrix(car)->right*-1.f;roadProbeForward=matrix(car)->up;
        roadProbeCameraUntil=gameTime()+12000;
        vehicle_lab::fixtureDuration=10000;
        if(!vehicle_lab::begin(probeCar,false))return -360;
        // Let the four springs settle to their unloaded equilibrium before the drop.
        vehicle_lab::observeHullLoad(car,{0,0,-.001f},{});
        return probeCar;
    }
    if(mode==367){
        auto*car=vehicleFromRef(probeCar);
        if(!car||!matrix(car)||vehicle_lab::carRef!=probeCar)return -367;
        at<unsigned char>(car,0xB8)=0;
        at<V>(car,0x44)=matrix(car)->up*.14f;
        return probeCar;
    }
    if(mode==362||mode==363||mode==365||mode==366){
        auto*car=vehicleFromRef(probeCar);auto*p=player();
        if(!car||!p||!matrix(car)||vehicle_lab::carRef!=probeCar)return -362;
        const auto*col=reinterpret_cast<ColModel*(__thiscall*)(void*)>(0x535300)(car);
        if(!col)return -362;
        const bool roof=mode==365,trunk=mode==366,moving=roof||trunk;
        const float across=trunk?col->box.max.x*.35f:col->box.min.x*.35f;
        const float along=roof?-.15f:trunk?col->box.min.y*.68f:col->box.max.y*.65f;
        const V above=point(*matrix(car),{across,along,col->box.max.z+1.f});
        ColPoint surface{};void*support=nullptr;
        const auto vertical=reinterpret_cast<bool(__cdecl*)(const V&,float,ColPoint&,void*&,bool,bool,bool,bool,bool,bool,void*)>(0x5674E0);
        if(!vertical(above,above.z-4.f,surface,support,false,true,false,false,false,false,nullptr)||support!=car)return -363;
        const float height=mode==363?1.7f:moving?4.f:5.f;
        const V motion=at<V>(car,0x44);
        const float speed=dot(motion,matrix(car)->up)*50.f;
        if(moving&&speed<3.f)return -365;
        const int ref=startCjFallProbe(surface.point+V{0,0,height});
        if(ref<0)return ref;
        // CJ starts with the same horizontal motion as the car and falls freely.
        if(moving){at<V>(p,0x44).x=motion.x;at<V>(p,0x44).y=motion.y;}
        watchRoadVehicle(probeCar,ref);
        event(moving?"vehicle_cj_moving_drop":"vehicle_cj_hood_drop",probeCar,at<short>(car,0x22),
            "height_m="+std::to_string(height)+" player_ref="+std::to_string(ref)+
            " surface="+(roof?"roof":trunk?"trunk":"hood")+" initial_mps="+std::to_string(speed));
        return probeCar;
    }
    if(mode==368){
        if(carFixture(445)<0||expansionFixture(133)<0||expansionFixture(141)<0||!vehicle_lab::begin(probeCar,false))return -368;
        return expansionFixture(135);
    }
    if(mode==350&&GetEnvironmentVariableA("GTASA_VEHICLE_MENU_TEST",nullptr,0)&&vehicle_lab::auditTraffic){
        if(carFixture(445)<0||expansionFixture(133)<0||expansionFixture(140)<0)return -350;
        ProbeScript remove(0x009B);remove.integer(expansionTarget);remove.run();
        vehicle_lab::menuProbeCar=probeCar;
        return probeCar;
    }
    if(mode==344){
        if(!vehicle_lab::auditTraffic||carFixture(445)<0||expansionFixture(133)<0||expansionFixture(140)<0)return -344;
        ProbeScript remove(0x009B);remove.integer(expansionTarget);remove.run();
        auto*car=vehicleFromRef(probeCar);if(!car)return -344;
        const V start=position(car)+matrix(car)->up*13.f;
        ProbeScript other(0x00A5);other.integer(445);other.number(start.x);other.number(start.y);
        other.number(start.z);other.result();vehicle_lab::collisionTarget=other.run();
        auto*peer=vehicleFromRef(vehicle_lab::collisionTarget);if(!peer)return -344;
        teleport(peer,start);
        reinterpret_cast<void(__thiscall*)(void*,float)>(0x43E0C0)(peer,1.5707963268f);
        return probeCar;
    }
    if(mode==345){
        auto*car=vehicleFromRef(probeCar);auto*peer=vehicleFromRef(vehicle_lab::collisionTarget);
        if(!car||!peer||!vehicle_lab::auditTraffic||at<void*>(car,0x460)!=player())return -345;
        for(auto*v:{car,peer}){
            at<unsigned char>(v,0xB8)=0;
            at<V>(v,0x44)=matrix(v)->up*.60f;
        }
        event("probe_windscreen_frontal",probeCar,445,"other="+std::to_string(vehicle_lab::collisionTarget)+" initial_each_mps=30 native_collision=1");
        return probeCar;
    }
    if(mode==290){
        if(carFixture(445)<0||expansionFixture(133)<0)return -290;
        ProbeScript remove(0x009B);remove.integer(expansionTarget);remove.run();
        auto*car=vehicleFromRef(probeCar);if(!car)return -290;
        reinterpret_cast<void(__cdecl*)(int,int)>(0x4087E0)(400,2);
        reinterpret_cast<void(__cdecl*)(bool)>(0x40EA10)(false);
        const V start=position(car)+matrix(car)->up*7.f+matrix(car)->right*.65f;
        ProbeScript other(0x00A5);other.integer(400);other.number(start.x);other.number(start.y);other.number(start.z);other.result();
        vehicle_lab::collisionTarget=other.run();
        auto*peer=vehicleFromRef(vehicle_lab::collisionTarget);if(!peer)return -290;
        reinterpret_cast<void(__thiscall*)(void*,float)>(0x43E0C0)(peer,0.f);
        if(!vehicle_lab::begin(probeCar,false))return -290;
        at<unsigned char>(car,0xB8)=0;at<V>(car,0x44)=matrix(car)->up*.16f;
        event("vehicle_lab_car_collision",probeCar,445,"peer="+std::to_string(vehicle_lab::collisionTarget));
        return probeCar;
    }
    if(mode==284){
        if(!vehicle_lab::auditTraffic || vehicleFromRef(probeCar))return -290;
        if(carFixture(445)<0 || expansionFixture(133)<0 || expansionFixture(140)<0)return -291;
        ProbeScript removePed(0x009B);removePed.integer(expansionTarget);removePed.run();
        ProbeScript wanted(0x010D);wanted.integer(0);wanted.integer(3);wanted.run();
        event("vehicle_lab_pursuit_setup",probeCar,445,"wanted=3 native_traffic=1 native_police=1");
        return probeCar;
    }
    if((mode>=280 && mode<=283) || mode==285){
        if(!vehicle_lab::enabled || vehicle_lab::carRef>=0 || vehicleFromRef(probeCar))return -280;
        if(carFixture(445)<0 || expansionFixture(133)<0)return -281;
        if(mode!=281){
            if(expansionFixture(140)<0)return -282;
            ProbeScript removePed(0x009B);
            removePed.integer(expansionTarget);
            removePed.run();
            void* ramp=nullptr;
            constexpr float rampX=2417.f, rampY=-1658.f, crestX=rampX+3.2f;
            if(mode==282 || mode==283){
                const int model=mode==282?1633:1634; // landjump, landjump2
                auto* car=vehicleFromRef(probeCar);
                const float top=position(car).z+6.f;
                const auto vertical=reinterpret_cast<bool(__cdecl*)(const V&,float,ColPoint&,void*&,bool,bool,bool,bool,bool,bool,void*)>(0x5674E0);
                ColPoint road{};void* entity=nullptr;
                if(!vertical(V{rampX,rampY,top},top-12.f,road,entity,true,false,false,true,true,false,nullptr))return -285;
                reinterpret_cast<void(__cdecl*)(int,int)>(0x4087E0)(model,2);
                reinterpret_cast<void(__cdecl*)(bool)>(0x40EA10)(false);
                ProbeScript createRamp(0x029B);
                createRamp.integer(model);createRamp.number(rampX);createRamp.number(rampY);
                createRamp.number(road.point.z+1.09f);createRamp.result();
                const int objectRef=createRamp.run();
                if(objectRef<0)return -286;
                ramp=reinterpret_cast<void*(__cdecl*)(int)>(0x550050)(objectRef);
                if(!ramp)return -286;
                ProbeScript heading(0x0177);
                heading.integer(objectRef);heading.number(270.f);heading.run();
                ProbeScript immobile(0x0392);
                immobile.integer(objectRef);immobile.integer(0);immobile.run();
                if(!vehicle_lab::fixedWheelSupport(ramp))return -287;
                ColPoint toe{},lip{};void *toeEntity=nullptr,*lipEntity=nullptr;
                if(!vertical(V{rampX-4.6f,rampY,top},top-12.f,toe,toeEntity,true,false,false,true,true,false,nullptr) ||
                   !vertical(V{crestX-.3f,rampY,top},top-12.f,lip,lipEntity,true,false,false,true,true,false,nullptr) ||
                   lipEntity!=ramp || lip.point.z-toe.point.z<1.5f)return -288;
                event("vehicle_lab_ramp",probeCar,445,"ramp="+std::to_string(model)+
                      " crest_x="+std::to_string(crestX)+" rise_m="+std::to_string(lip.point.z-toe.point.z));
            }
            if(!vehicle_lab::begin(probeCar,true,ramp,crestX,mode==285))return -283;
            return probeCar;
        }
        if(expansionFixture(141)<0 || !vehicle_lab::begin(probeCar,false))return -284;
        return expansionFixture(134);
    }
'''+anchor)
adapter.write_text(source, encoding="utf-8", newline="\n")
if args.shipping:
    import json
    sys.path.insert(0, str(builder.parent))
    from runtime_sources import strip_fixtures
    paths = [path for path in args.out.iterdir() if path.suffix in {".cpp", ".hpp"}]
    texts = {path.name: path.read_text(encoding="utf-8") for path in paths}
    texts["Adapter.cpp"] = texts["Adapter.cpp"].replace("namespace vehicle_entry_probe {static void tick();}", "")
    texts["Adapter.cpp"] = texts["Adapter.cpp"].replace("vehicle_entry_probe::tick();", "")
    texts["VehicleLab.hpp"] = replace_once(texts["VehicleLab.hpp"], "const bool test = GetEnvironmentVariableA",
        "const bool test = false && GetEnvironmentVariableA")
    omitted = strip_fixtures(texts)
    texts["Adapter.cpp"] = replace_once(texts["Adapter.cpp"],
        "static bool started=false;if(started)return;started=true;",
        "static volatile LONG started=0;if(InterlockedExchange(&started,1))return;")
    texts["Adapter.cpp"] = replace_once(texts["Adapter.cpp"],
        "BOOL WINAPI DllMain(HINSTANCE instance,DWORD why,LPVOID){if(why==DLL_PROCESS_ATTACH)DisableThreadLibraryCalls(instance);return TRUE;}",
        r"""
// Silent's loader maps the plugin through LoadLibrary without calling InitializeASI.
static DWORD WINAPI initializeFromSilentLoader(void*) {
    InitializeASI();
    return 0;
}
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        const auto proxy = GetModuleHandleW(L"vorbisFile.dll");
        if (proxy && !GetProcAddress(proxy, "IsUltimateASILoader") &&
            GetModuleHandleW(L"vorbisHooked.dll")) {
            // The worker starts after the DLL loader has released its lock.
            if (const auto thread = CreateThread(nullptr, 0, &initializeFromSilentLoader, nullptr, 0, nullptr))
                CloseHandle(thread);
        }
    }
    return TRUE;
}
""")
    for path in paths:
        path.write_text(texts[path.name], encoding="utf-8", newline="\n")
    receipt = args.out / "source-receipt.json"
    record = json.loads(receipt.read_text(encoding="utf-8"))
    record.update(fixtures="authoring hooks and exports omitted", omitted=omitted)
    receipt.write_text(json.dumps(record, indent=2), encoding="utf-8")
else:
    inject_road_prop_fixture(args.out)
