#pragma once
#include "VehicleNitro.hpp"
#include "GravityControl.hpp"
#include "DriveByControls.hpp"
#include "ClassicBodyMotion.hpp"
#include "VehicleDeformation.hpp"

namespace vehicle_lab {
using Quad = std::array<float, 4>;
using Basis = std::array<Quad, 4>;
struct Ground {
    Quad point, normal;
    float travel, compression, radius, loadShare;
    uint32_t supported;
};
struct Frame {
    Basis basis;
    Quad velocity, spin, nativeSpinLoss, inertia;
    std::array<Ground, 4> contacts;
    float mass, dt, gas, brake, steer;
    uint32_t now, reseedContacts, handbrake, nitro;
    float gravityScale;
};
struct Result {
    Quad velocity, spin, spring;
    float drive, revs, lateralForce;
    int32_t gear;
    Basis basis;
    Quad inertia;
};
constexpr uint32_t reseedWheels = 1;
constexpr uint32_t resetDriver = 2;
static_assert(sizeof(Ground) == 52 && sizeof(Frame) == 376 && sizeof(Result) == 144);

static void* (__cdecl* create)(float, uint32_t, uint32_t, float, float, float, uint32_t, float) = nullptr;
static int (__cdecl* tuningSource)(uint32_t) = nullptr;
static int (__cdecl* driverLimits)(void*, float*) = nullptr;
static int (__cdecl* step)(void*, const Frame*, Result*) = nullptr;
static int (__cdecl* turn)(const Basis*, const Quad*, float, Basis*) = nullptr;
static int (__cdecl* inertia)(uint32_t, const Quad*, Quad*) = nullptr;
static void (__cdecl* destroy)(void*) = nullptr;
static void (__thiscall* nativePair)(void*, int, int, float, V*, V*, float, float, float, bool) = nullptr;
static void (__thiscall* nativeControl)(void*) = nullptr;
static void (__thiscall* nativeTurn)(void*) = nullptr;
static void (__thiscall* nativeAirResistance)(void*) = nullptr;
static void (__thiscall* nativeMonsterSuspension)(void*) = nullptr;
static void suspension(void*);
static void __fastcall monsterSuspension(void*car,void*) { suspension(car); }
static void setNikoMovementEnabled(bool value);
static int carRef = -1;
static bool enabled = false, useIv = false, driving = false, replaced = false;
static bool reversingSteer = false, rollLaunched = false, admiralProfile = false;
static bool traffic = false, auditTraffic = false;
static int menuProbeCar = -1, menuProbePhysics = -1;
static void observeMenuPhysics(void* car, int mode) {
    if (menuProbeCar < 0 || vehicleFromRef(menuProbeCar) != car || menuProbePhysics == mode) return;
    menuProbePhysics = mode;
    event("vehicle_menu_test_physics", menuProbeCar, at<short>(car, 0x22), mode ? "iv" : "classic");
}

static bool adaptNpcDriving = true;
static bool quickerDriving = true;
static float rampCrestX = 0;
static void* rampObject = nullptr;
static unsigned rampIvSteps = 0;
static void* solver = nullptr;
static unsigned started = 0, sampleAt = 0, applied = 0, fallback = 0, pairs = 0;
static unsigned wheelFrame = 0;
static bool reseedContacts = true;
static void* wheelOwner = nullptr;
static std::ofstream trace;
static unsigned turnApplied = 0, integratedApplied = 0;
static unsigned admiralSteps = 0, nativeDampingReplaced = 0;
static unsigned bodyWheelSteps = 0;
static unsigned fixtureDuration = 12000;
static bool gripComparison = false, hardSwerve = false, rightAngleTurn = false;
static float curveEntrySpeed = 0.f;
static int collisionTarget = -1;
static struct {
    void* car;
    int ref;
    unsigned frame;
    V loss;
} angularDrag{};

struct TurnPlan {
    Basis before{}, after{};
    Quad nativeSpin{};
    float seconds = 0;
    unsigned frame = 0;
    bool valid = false;
};
static TurnPlan fixtureTurn;
struct TrafficDriving {
    unsigned sightSample = 0, lastAudit = 0;
    bool targetVisible = false;
};
static TrafficDriving fixtureDriver;
struct TrafficCar {
    void* entity;
    void* solver;
    float mass;
    unsigned lastSeen;
    unsigned lastSample = 0;
    bool reseed = true;
    bool resetControls = true;
    void* driverOwner = nullptr;
    bool boarding = false, parkedEntry = false;
    unsigned wheelStepFrame = 0;
    TurnPlan pose;
    TrafficDriving driver;
};
static std::unordered_map<int, TrafficCar> trafficCars;
// Allow a body to compress the springs even if the car was already parked.
// This is a short wake window, refreshed by the existing finite-owner solve.
static std::unordered_map<int, unsigned> hullLoadTimes;
struct LoadedSuspension {
    void* car;
    float verticalSpeed = 0.f;
};
static thread_local LoadedSuspension* loadedSuspension = nullptr;

static Quad quad(V v, float w = 0) { return {v.x, v.y, v.z, w}; }
static V vec(Quad q) { return {q[0], q[1], q[2]}; }
static Basis basisOf(const M& matrix, V localCom) {
    return {quad(matrix.right), quad(matrix.up), quad(matrix.at),
            quad(point(matrix, localCom), 1.f)};
}
static unsigned frameCounter() { return *reinterpret_cast<unsigned*>(0xB7CB4C); }
static unsigned carStatus(void* car) { return at<unsigned char>(car, 0x36) >> 3; }
#include "TrafficDriving.hpp"
static bool ordinary(void* car) {
    const int model = at<short>(car, 0x22);
    const int type=at<int>(car,0x594);
    // Automobiles, monster trucks and quads share CAutomobile wheel data.
    return type>=0 && type<=2 && model>0 && model!=432 && model!=486 && model!=564;
}
static bool trafficAllowed(void* car) {
    auto* cj = player();
    return traffic && recoveryAllowed() && cj && ordinary(car) && matrix(car) &&
        !at<void*>(car, 0xFC) && length(position(car) - position(cj)) <= 95.f &&
        carStatus(car) != 2 && !GetModuleHandleW(L"LivelyVehiclePhysics.asi");
}
static bool entering(void* car) {
    // CVehicle::m_nNumGettingIn and m_nGettingInFlags.
    return at<unsigned char>(car, 0x485) || at<unsigned char>(car, 0x486);
}
static bool eligible(void* car) {
    return enabled && (trafficAllowed(car) ||
        (carRef >= 0 && vehicleFromRef(carRef) == car && matrix(car) && recoveryAllowed()));
}

static bool canCarryHullLoad(void* car) {
    // CPhysical: collision/move force disabled, infinite mass, Z locked,
    // submerged, or speed integration disabled.
    constexpr unsigned blocked = 4u | 32u | 64u | 128u | 256u | 8192u;
    return useIv && eligible(car) && !entering(car) && !at<void*>(car, 0xFC) &&
        (at<unsigned>(car, 0x40) & blocked) == 0 && matrix(car)->at.z > .5f;
}

#ifdef DELIRIUM_VEHICLE_SPEED_PROBE
namespace speed_probe {
struct PadInput {
    void* pad = nullptr;
    std::array<short, 24> previous{};
    unsigned short previousMode = 0;
    explicit PadInput(void* car);
    ~PadInput();
};
}
#endif

static void observeHullLoad(void* car, V velocityDelta, V torque) {
    classic_body_motion::impulse(car, velocityDelta, torque);
    if (velocityDelta.z < -.0001f && canCarryHullLoad(car)) {
        const int ref = vehicleRef(car);
        if (ref >= 0) hullLoadTimes[ref] = gameTime();
    }
}

static bool inverseTensor(void* car, V* rows) {
    if (!inertia || !eligible(car)) return false;
    const auto* col = reinterpret_cast<ColModel*(__thiscall*)(void*)>(0x535300)(car);
    if (!col) return false;
    const V dimensions = col->box.max - col->box.min;
    const float k = at<float>(car, 0x8C) / 12.f;
    const Quad cuboid{k * (dimensions.y*dimensions.y + dimensions.z*dimensions.z),
                      k * (dimensions.x*dimensions.x + dimensions.z*dimensions.z),
                      k * (dimensions.x*dimensions.x + dimensions.y*dimensions.y), 1.f};
    Quad moment{};
    if (!inertia(at<short>(car, 0x22), &cuboid, &moment)) return false;
    const auto& m = *matrix(car);
    const V axes[] = {m.right, m.up, m.at};
    for (int row=0; row<3; ++row) {
        rows[row] = {};
        for (int axis=0; axis<3; ++axis) {
            const auto direction = quad(axes[axis]);
            rows[row] = rows[row] + axes[axis] * (direction[row] / moment[axis]);
        }
    }
    return true;
}

static void __fastcall airResistance(void* entity, void*) {
    if ((at<unsigned char>(entity, 0x36) & 7) != 2 ||
        at<short>(entity, 0x22) != 445 || !eligible(entity)) {
        nativeAirResistance(entity);
        return;
    }
    const V before = at<V>(entity, 0x50);
    nativeAirResistance(entity);
    const auto frame = frameCounter();
    const auto ref = vehicleRef(entity);
    const V loss = before - at<V>(entity, 0x50);
    if (angularDrag.car == entity && angularDrag.ref == ref && angularDrag.frame == frame) {
        angularDrag.loss = angularDrag.loss + loss;
    } else {
        angularDrag = {entity, ref, frame, loss};
    }
}

static void __fastcall control(void* car, void*) {
#ifdef DELIRIUM_VEHICLE_SPEED_PROBE
    speed_probe::PadInput padInput(car);
#endif
    // The native traffic controller owns the transition from path-node
    // motion to physical cars. Promoting ghost traffic here bypasses that
    // lifecycle and can strand the existing population during a mode change.
    if (hullLoadTimes.empty()) {
        nativeControl(car);
        return;
    }
    const auto load = hullLoadTimes.find(vehicleRef(car));
    if (load == hullLoadTimes.end() || gameTime() - load->second > 1200 || !canCarryHullLoad(car)) {
        nativeControl(car);
        return;
    }
    // CAutomobile's parked-car sleep and its final near-zero speed snap are
    // separate. Keep the step awake and preserve its Z if the snap erased it.
    at<unsigned char>(car, 0xB8) = 0;
    LoadedSuspension receipt{car};
    auto* prior = loadedSuspension;
    loadedSuspension = &receipt;
    nativeControl(car);
    loadedSuspension = prior;
    auto& velocity = at<V>(car, 0x44);
    if (velocity.x == 0.f && velocity.y == 0.f && velocity.z == 0.f &&
        std::abs(receipt.verticalSpeed) < .0045f && canCarryHullLoad(car)) {
        velocity.z = receipt.verticalSpeed;
    }
}

static void __fastcall applyTurn(void* entity, void*) {
    // ApplyTurnSpeed is shared by peds, objects and all vehicle subclasses.
    // Restrict replacement to the automobile whose IV step was accepted.
    if ((at<unsigned char>(entity, 0x36) & 7) != 2 || !eligible(entity) ||
        (at<unsigned short>(entity, 0x40) & 0x2060) != 0) {
        nativeTurn(entity);
        return;
    }
    const bool fixture = carRef >= 0 && vehicleFromRef(carRef) == entity;
    auto* plan = fixture ? &fixtureTurn : nullptr;
    if (!fixture) {
        const auto it = trafficCars.find(vehicleRef(entity));
        if (it != trafficCars.end() && it->second.entity == entity) plan = &it->second.pose;
    }
    const auto frame = frameCounter();
    if (!plan || !plan->valid || frame - plan->frame > 1) {
        nativeTurn(entity);
        return;
    }
    auto& pose = *matrix(entity);
    const V com = at<V>(entity, 0xA4);
    const Basis before = basisOf(pose, com);
    const Quad spin = quad(at<V>(entity, 0x50) * 50.f);
    const float seconds = *reinterpret_cast<float*>(0xB7CB5C) * .02f;
    Basis after{};
    // ApplyMoveSpeed may already have translated the chassis. Compare the
    // three axes, spin and timestep of the coupled wheel step. Collision
    // retries with another orientation or timestep use their own inputs.
    const bool integrated = plan->frame == frame &&
        std::equal(plan->before.begin(), plan->before.begin() + 3, before.begin()) &&
        plan->nativeSpin == spin && plan->seconds == seconds;
    if (integrated) {
        after = plan->after;
    } else if (!turn(&before, &spin, seconds, &after)) {
        nativeTurn(entity);
        return;
    }
    pose.right = vec(after[0]);
    pose.up = vec(after[1]);
    pose.at = vec(after[2]);
    pose.pos = vec(before[3]) - pose.right * com.x - pose.up * com.y - pose.at * com.z;
    if (fixture) {
        ++turnApplied;
        integratedApplied += integrated;
    }
}

static void __fastcall pair(void* car, void*, int left, int right, float steer,
                            V* speed, V* points, float grip, float drive, float brake, bool front) {
    const bool owned = replaced && car == wheelOwner && frameCounter() == wheelFrame;
    const V velocity = at<V>(car, 0x44), spin = at<V>(car, 0x50);
    nativePair(car, left, right, steer, speed, points, grip, drive, brake, front);
    if (owned) {
        // Retain wheel state and effects. The IV solver supplied these forces.
        at<V>(car, 0x44) = velocity;
        at<V>(car, 0x50) = spin;
        if (carRef >= 0 && vehicleFromRef(carRef) == car) ++pairs;
    }
}

static void setTrafficDriving(bool value) {
    if (traffic == value) return;
    traffic = value;
    classic_body_motion::setEnabled(!value);
    replaced = false;
    wheelOwner = nullptr;
    angularDrag = {};
    for (auto& [ref, car] : trafficCars)
        if (car.solver) destroy(car.solver);
    trafficCars.clear();
    hullLoadTimes.clear();
    event("vehicle_driving_mode", -1, -1, value ? "iv" : "classic");
}

#include "VehicleMenu.hpp"

static bool install(HMODULE engine) {
    char mode[16]{}, passive[2]{};
    GetEnvironmentVariableA("GTASA_VEHICLE_LAB", mode, sizeof(mode));
    const bool test = GetEnvironmentVariableA("GTASA_PASSIVE_TEST_WINDOW", passive, sizeof(passive)) == 1 &&
        passive[0] == '1';
    const bool trafficProbe = test && strcmp(mode, "traffic") == 0;
    const bool menuProbe = trafficProbe && GetEnvironmentVariableA("GTASA_VEHICLE_MENU_TEST", nullptr, 0);
    const bool fixture = test && (strcmp(mode, "classic") == 0 || strcmp(mode, "iv") == 0);
    const auto ini = folder.parent_path() / "DeliriumSA.ini";
    const bool canToggle = !test || menuProbe;
    auditTraffic = trafficProbe;
    const bool previouslyEnabled = GetPrivateProfileIntW(L"VehicleLab", L"Enabled", 0, ini.c_str()) != 0;
    if (!gravity_control::install(engine)) event("gravity_control_unavailable");
    if (!classic_body_motion::install()) event("classic_visual_hooks_unavailable");
    if (!vehicle_deformation::install(engine)) event("vehicle_deformation_hook_unavailable");
    vehicle_deformation::setEnabled(GetPrivateProfileIntW(L"VehicleDeformation", L"Enabled", 0, ini.c_str()) != 0);
    if (canToggle) vehicle_menu::readMode(ini, previouslyEnabled);
    traffic = canToggle ? vehicle_menu::active == vehicle_menu::Mode::Iv : trafficProbe;
    classic_body_motion::setEnabled(canToggle && vehicle_menu::active == vehicle_menu::Mode::Classic);
    adaptNpcDriving = GetPrivateProfileIntW(L"VehicleLab", L"AdaptNpcDriving", 1, ini.c_str()) != 0;
    if(test&&GetEnvironmentVariableA("GTASA_VEHICLE_NATIVE_AI",nullptr,0))adaptNpcDriving=false;
    if (!vehicle_nitro::install(ini))event("vehicle_nitro_binding_refused");
    if (!drive_by_controls::install(ini))event("vehicle_drive_by_binding_refused");
    else if(drive_by_controls::installed)event("vehicle_drive_by_controls_enabled");
    if (!fixture && !traffic && !canToggle) return true;
    if (canToggle && vehicle_menu::active == vehicle_menu::Mode::Izzy) {
        if (!vehicle_menu::install(ini)) event("vehicle_menu_unavailable");
        vehicle_menu::startIzzy();
        return true;
    }
    if (GetModuleHandleW(L"LivelyVehiclePhysics.asi")) return true;
    useIv = canToggle || traffic || strcmp(mode, "iv") == 0;
    if (useIv) {
        const auto abi = reinterpret_cast<uint32_t(__cdecl*)()>(GetProcAddress(engine, "sa_vehicle_lab_abi"));
        if (!abi || abi() != 10) return false;
        create = reinterpret_cast<decltype(create)>(GetProcAddress(engine, "sa_vehicle_lab_create"));
        step = reinterpret_cast<decltype(step)>(GetProcAddress(engine, "sa_vehicle_lab_step"));
        turn = reinterpret_cast<decltype(turn)>(GetProcAddress(engine, "sa_vehicle_lab_turn"));
        inertia = reinterpret_cast<decltype(inertia)>(GetProcAddress(engine, "sa_vehicle_lab_inertia"));
        destroy = reinterpret_cast<decltype(destroy)>(GetProcAddress(engine, "sa_vehicle_lab_destroy"));
        tuningSource = reinterpret_cast<decltype(tuningSource)>(
            GetProcAddress(engine, "sa_vehicle_lab_tuning_source"));
        driverLimits = reinterpret_cast<decltype(driverLimits)>(
            GetProcAddress(engine, "sa_vehicle_lab_driver_limits"));
        const auto loadHandling = reinterpret_cast<size_t(__cdecl*)(const wchar_t*, size_t)>(
            GetProcAddress(engine, "sa_vehicle_lab_load_handling"));
        if (!tuningSource || !loadHandling || !driverLimits) return false;
        const auto handlingPath = (folder / "iv-vehicle-handling.dat").wstring();
        const auto profiles = loadHandling(handlingPath.c_str(), handlingPath.size());
        event("vehicle_iv_handling_loaded", -1, -1, "profiles=" + std::to_string(profiles));
        constexpr unsigned char expected[] = {0x55,0x8B,0xEC,0x90,0xE9,0xA8,0xE9,0xD5,0xFF};
        auto* entry = reinterpret_cast<void*>(0x6A4EC0);
        if (!create || !step || !turn || !inertia || !destroy || memcmp(entry, expected, sizeof(expected)) != 0 ||
            MH_CreateHook(entry, reinterpret_cast<void*>(&pair), reinterpret_cast<void**>(&nativePair)) != MH_OK ||
            MH_QueueEnableHook(entry) != MH_OK) return false;
        constexpr unsigned char rotation[] = {0x83,0xEC,0x3C,0x56,0x8B,0xF1,0x8B,0x46,0x40};
        auto* physicalTurn = reinterpret_cast<void*>(0x542E20);
        if (memcmp(physicalTurn, rotation, sizeof(rotation)) != 0 ||
            MH_CreateHook(physicalTurn, reinterpret_cast<void*>(&applyTurn), reinterpret_cast<void**>(&nativeTurn)) != MH_OK ||
            MH_QueueEnableHook(physicalTurn) != MH_OK) return false;
        constexpr unsigned char monster[]={0x81,0xEC,0xE8,0,0,0,0x53,0x55,0x56,0x57};
        auto*monsterEntry=reinterpret_cast<void*>(0x6C83A0);
        if(std::memcmp(monsterEntry,monster,sizeof(monster))!=0||
           MH_CreateHook(monsterEntry,reinterpret_cast<void*>(&monsterSuspension),
                         reinterpret_cast<void**>(&nativeMonsterSuspension))!=MH_OK||
           MH_QueueEnableHook(monsterEntry)!=MH_OK)return false;
        char tuning[2]{};
        quickerDriving = !(test &&
            GetEnvironmentVariableA("GTASA_VEHICLE_LAB_SLOW", tuning, sizeof(tuning)) == 1 && tuning[0] == '1');
        const auto tune = reinterpret_cast<void(__cdecl*)(int)>(
            GetProcAddress(engine, "sa_vehicle_lab_set_quicker_driving"));
        if (!tune) return false;
        tune(quickerDriving);
        char baseline[2]{};
        const bool skipProfile = test &&
            GetEnvironmentVariableA("GTASA_VEHICLE_LAB_BASELINE", baseline, sizeof(baseline)) == 1 &&
            baseline[0] == '1';
        if (!skipProfile) {
            const auto load = reinterpret_cast<int(__cdecl*)(const wchar_t*, size_t)>(
                GetProcAddress(engine, "sa_vehicle_lab_load_admiral"));
            const auto path = (folder / "iv-admiral-suspension.json").wstring();
            constexpr unsigned char air[] = {0x51,0x56,0x8B,0xF1,0xD9,0x86,0x98,0,0,0};
            auto* physicalAir = reinterpret_cast<void*>(0x544C40);
            if (load && memcmp(physicalAir, air, sizeof(air)) == 0 && load(path.c_str(), path.size())) {
                if (MH_CreateHook(physicalAir, reinterpret_cast<void*>(&airResistance),
                                  reinterpret_cast<void**>(&nativeAirResistance)) != MH_OK ||
                    MH_QueueEnableHook(physicalAir) != MH_OK) return false;
                admiralProfile = true;
                event("vehicle_iv_admiral_suspension_enabled", -1, 445);
            }
        }
    }
    if (useIv) {
        constexpr unsigned char prologue[] = {0x81,0xEC,0xD8,0,0,0,0x53,0x55,0x56};
        auto* entry = reinterpret_cast<void*>(0x6B1880);
        if (memcmp(entry, prologue, sizeof(prologue)) != 0 ||
            MH_CreateHook(entry, reinterpret_cast<void*>(&control), reinterpret_cast<void**>(&nativeControl)) != MH_OK ||
            MH_QueueEnableHook(entry) != MH_OK) return false;
    }
    enabled = true;
    if (traffic) event("vehicle_iv_traffic_enabled", -1, -1, "activation_radius_m=95");
    if (canToggle) {
        if (!vehicle_menu::install(ini)) event("vehicle_menu_unavailable");
        event("vehicle_driving_mode", -1, -1, traffic ? "iv" : "classic");
    }
    return true;
}

static void* makeSolver(void* car, V dimensions) {
    const auto* handling = at<unsigned char*>(car, 0x384);
    if (!handling) return nullptr;
    return create(at<float>(car, 0x8C), at<short>(car, 0x22), handling[0x2C + 0x48],
                  dimensions.y, dimensions.z,
                  *reinterpret_cast<const float*>(handling+0x2C+0x5C)*50.f,
                  handling[0x2C+0x4A], *reinterpret_cast<const float*>(handling + 0xA0));
}

static void stockSuspension(void*car){
    if(at<int>(car,0x594)==1&&nativeMonsterSuspension)nativeMonsterSuspension(car);
    else originalSuspension(car);
}

static void stockSuspensionWithBodies(void*car){
    wheelBodyDeltaVelocity={};wheelBodyTorque={};
    prepareWheelBodyContacts(car);
    stockSuspension(car);
    suspensionCar=nullptr;
}

#ifdef DELIRIUM_VEHICLE_SPEED_PROBE
#include "VehicleSpeedProbe.hpp"
#endif

static void finishFixture(){
    trace.close();
    if(solver)destroy(solver);
    solver=nullptr;
    carRef=-1;
    fixtureTurn.valid=false;
    fixtureDriver={};
#ifdef DELIRIUM_VEHICLE_SPEED_PROBE
    speed_probe::mode = 0;
#endif
}

static bool begin(int ref, bool driverInputs, void* ramp = nullptr, float crestX = 0,
                  bool steeringReversal = false, bool compareGrip = false, float entrySpeed = 0.f) {
    auto* car = vehicleFromRef(ref);
    if (!enabled || carRef >= 0 || !car || !ordinary(car) || !matrix(car) || !recoveryAllowed()) return false;
    if (useIv) {
        const auto* col = reinterpret_cast<ColModel*(__thiscall*)(void*)>(0x535300)(car);
        if (!col) return false;
        solver = makeSolver(car, col->box.max - col->box.min);
        if (!solver) return false;
    }
    trace.open(folder / "vehicle-lab.csv", std::ios::trunc);
    if (!trace) {
        if (solver) destroy(solver);
        solver = nullptr;
        return false;
    }
    trace << "ms,dt,x,y,z,heading,pitch,roll,forward_mps,lateral_mps,gas,brake,steer,gear,revs,drive,lateral_force,"
             "compression_fl,compression_fr,compression_rl,compression_rr,"
             "load_fl,load_fr,load_rl,load_rr,iv_steps,fallback_steps,native_pairs_replaced,"
             "ramp_wheels,iv_ramp_steps,grounded,mass,inertia_x,inertia_y,inertia_z,front_load_share,"
             "iv_turn_calls,iv_rk4_pose_calls,admiral_suspension_steps,native_damping_replaced,body_wheel_steps,"
             "peer_speed_mps,peer_pitch,peer_roll,raw_steer,handbrake,from_native_pad\n";
    carRef = ref;
    driving = driverInputs;
    gripComparison = compareGrip;
    hardSwerve = rightAngleTurn = false;
    curveEntrySpeed = entrySpeed;
    reversingSteer = steeringReversal;
    rollLaunched = false;
    sampleAt = applied = fallback = pairs = rampIvSteps = 0;
    turnApplied = integratedApplied = admiralSteps = nativeDampingReplaced = bodyWheelSteps = 0;
    reseedContacts = true;
    rampObject = ramp;
    rampCrestX = crestX;
    started = gameTime();
    event("vehicle_lab_fixture", ref, at<short>(car, 0x22), std::string("mode=") + (useIv ? "iv" : "classic") +
          " driving=" + std::to_string(driving) +
          " source=" + std::to_string(useIv && tuningSource ? tuningSource(at<short>(car, 0x22)) : 0));
    return true;
}

static bool fixedWheelSupport(void* support) {
    return !support || ((at<unsigned char>(support, 0x36) & 7) == 4 &&
        (at<unsigned char>(support, 0x1C) & 4) != 0 &&
        !at<void*>(support, 0xFC) &&
        length(at<V>(support, 0x44)) < .0001f && length(at<V>(support, 0x50)) < .0001f);
}

static void suspension(void* car) {
    if (car == wheelOwner) replaced = false;
    if (!eligible(car)) {
        stockSuspensionWithBodies(car);
        if (!traffic) observeMenuPhysics(car, 0);
        return;
    }
    wheelOwner = car;
    replaced = false;
    wheelFrame = frameCounter();
    const auto now = gameTime();
    const bool fixture = carRef >= 0 && vehicleFromRef(carRef) == car;
    const unsigned elapsed = fixture ? now - started : 0;
    if (fixture && elapsed > fixtureDuration) {
        finishFixture();
        stockSuspensionWithBodies(car);
        return;
    }
    bool fixtureHandbrake = false;
#ifdef DELIRIUM_VEHICLE_SPEED_PROBE
    if (fixture && speed_probe::mode) {
        fixtureHandbrake = speed_probe::controls(car, elapsed);
    } else
#endif
    if (fixture && gripComparison) {
        if (!rollLaunched && elapsed >= 2300) {
            // Same entry speed for every model, independent of launch torque.
            const V forward = matrix(car)->up;
            auto& speed = at<V>(car, 0x44);
            const float entry = curveEntrySpeed > 0.f ? curveEntrySpeed / 50.f : .24f;
            speed.x = forward.x * entry;
            speed.y = forward.y * entry;
            rollLaunched = true;
        }
        at<float>(car, 0x49C) = (elapsed >= 500 && elapsed < 2250) ||
                               (elapsed >= 3050 && elapsed < 4300) ? 1.f : 0.f;
        at<float>(car, 0x4A0) = elapsed < 500 || elapsed >= 4700 ? .85f : 0.f;
        at<float>(car, 0x494) = elapsed >= 2350 && elapsed < 3050 ? .18f :
                               elapsed >= 3050 && elapsed < 3850 ? -.18f : 0.f;
        if (curveEntrySpeed > 0.f) {
            if (rightAngleTurn) {
                const bool turning = elapsed >= 2350 && elapsed < 4600 && matrix(car)->up.x > 0.f;
                at<float>(car, 0x49C) = turning ? .2f : 0.f;
                at<float>(car, 0x4A0) = elapsed < 500 || elapsed >= 4750 ? .85f : 0.f;
            }
            const float stick = rightAngleTurn ?
                (elapsed >= 2350 && elapsed < 4600 && matrix(car)->up.x > 0.f ? .90f : 0.f) :
                hardSwerve && elapsed >= 2350 && elapsed < 3500 ?
                (((elapsed-2350)/285)%2==0 ? .82f : -.82f) :
                (!hardSwerve && elapsed >= 2350 && elapsed < 3050 ? .55f : 0.f);
            const auto* handling = at<unsigned char*>(car, 0x384);
            at<float>(car, 0x58C) = stick;
            at<float>(car, 0x494) = handling ?
                *reinterpret_cast<const float*>(handling + 0xA0) * .01745329252f * stick * std::abs(stick) : 0.f;
        }
    } else if (fixture && reversingSteer) {
        at<float>(car, 0x49C) = 0.f;
        at<float>(car, 0x4A0) = elapsed < 2250 || elapsed >= 6000 ? .85f : 0.f;
        if (!rollLaunched && elapsed >= 2500) {
            const V forward = matrix(car)->up;
            auto& speed = at<V>(car, 0x44);
            speed.x = forward.x * .12f;
            speed.y = forward.y * .12f;
            rollLaunched = true;
        }
        at<float>(car, 0x494) = elapsed >= 2500 && elapsed < 3150 ? .24f :
                               elapsed >= 3150 && elapsed < 3850 ? -.24f : 0.f;
    } else if (fixture && driving) {
        const bool accelerating = elapsed >= 600 &&
            (rampObject ? elapsed < 7500 && position(car).x < rampCrestX + 10.f : elapsed < 2900);
        at<float>(car, 0x49C) = accelerating ? 1.f : 0.f;
        at<float>(car, 0x4A0) = accelerating ? 0.f : .85f;
        at<float>(car, 0x494) = !rampObject && elapsed >= 2180 && elapsed < 2540 ? .08f : 0.f;
    }
    const auto& transform = *matrix(car);
    const V velocity = at<V>(car, 0x44), spin = at<V>(car, 0x50);
    Frame frame{};
    frame.basis = basisOf(transform, at<V>(car, 0xA4));
    frame.velocity = quad(velocity * 50.f);
    frame.spin = quad(spin * 50.f);
    const bool tuned = admiralProfile && at<short>(car, 0x22) == 445;
    const bool replaceDrag = tuned && angularDrag.car == car &&
        angularDrag.frame == wheelFrame && angularDrag.ref == vehicleRef(car);
    if (replaceDrag) {
        frame.nativeSpinLoss = quad(angularDrag.loss * 50.f);
        angularDrag.car = nullptr;
    }
    frame.mass = at<float>(car, 0x8C);
    frame.dt = *reinterpret_cast<float*>(0xB7CB5C) * .02f;
    frame.gas = std::clamp(at<float>(car, 0x49C), -1.f, 1.f);
    frame.brake = std::clamp(at<float>(car, 0x4A0), 0.f, 1.f);
    frame.steer = at<float>(car, 0x494);
    const bool npcDriver = traffic && adaptNpcDriving && at<void*>(car,0x460) && at<void*>(car,0x460) != player();
    if (quickerDriving && !npcDriver && at<void*>(car, 0x460) == player()) {
        const auto* handling = at<unsigned char*>(car, 0x384);
        const float lock = handling ? *reinterpret_cast<const float*>(handling + 0xA0) : 0.f;
        const float raw = at<float>(car, 0x58C);
        if (lock >= 5.f && lock <= 65.f && std::isfinite(raw)) {
            const float stick = std::min(std::abs(raw), 1.f);
            const float speed = std::abs(dot(velocity, transform.up)) * 50.f;
            const float turn = std::clamp((stick - .18f) / .55f, 0.f, 1.f);
            const float deliberate = turn * turn * (3.f - 2.f * turn);
            const float street = std::clamp((speed - 4.f) / 10.f, 0.f, 1.f) *
                                 std::clamp((46.f - speed) / 22.f, 0.f, 1.f);
            const float highway = std::clamp((speed - 20.f) / 28.f, 0.f, 1.f);
            // Keep SA's squared input near centre, with more authority
            // at sustained lock. Leave damage-induced steer untouched.
            const float delta = deliberate * (.34f * stick * (1.f - stick) +
                                 (.13f + .22f * street) * stick * stick) -
                                .30f * highway * (1.f - deliberate) * stick * stick;
            frame.steer += lock * .01745329252f * delta * std::copysign(1.f, raw);
        }
    }
    frame.now = now;
    frame.gravityScale = gravity_control::scale(car);
    frame.handbrake = fixture ? fixtureHandbrake : (at<unsigned char>(car, 0x428) & 0x20) != 0;
    frame.nitro = (at<unsigned>(car,0x38C)&0x80000) && at<float>(car,0x8A4)<0.f;
    const auto* col = reinterpret_cast<ColModel*(__thiscall*)(void*)>(0x535300)(car);
    if (!col || !col->data || !col->data->line || col->data->lines != 4) {
        stockSuspensionWithBodies(car);
        return;
    }
    const bool diskWheels = (col->data->flags & 1u) != 0;
    if (diskWheels && at<int>(car, 0x594) != 1) {
        stockSuspensionWithBodies(car);
        return;
    }
    const V dimensions = col->box.max - col->box.min;
    TrafficCar* live = nullptr;
    if (!fixture && traffic) {
        const int ref = vehicleRef(car);
        if (ref < 0) { stockSuspensionWithBodies(car); return; }
        auto it = trafficCars.find(ref);
        if (it != trafficCars.end() && (it->second.entity != car || it->second.mass != frame.mass)) {
            if (it->second.solver) destroy(it->second.solver);
            it = trafficCars.erase(it);
        }
        if (it == trafficCars.end()) {
            it = trafficCars.emplace(ref, TrafficCar{car, makeSolver(car, dimensions), frame.mass, now}).first;
        }
        live = &it->second;
        live->lastSeen = now;
    }
    if (live) {
        void* owner = at<void*>(car, 0x460);
        const bool boarding = entering(car);
        if (!boarding || owner == player()) live->parkedEntry = false;
        else if (!live->boarding)
            live->parkedEntry = velocity.x * velocity.x + velocity.y * velocity.y < .0025f;
        if (live->driverOwner != owner) live->resetControls = true;
        if (live->driverOwner != owner || live->boarding != boarding ||
            wheelFrame - live->wheelStepFrame > 1) {
            live->reseed = true;
            live->pose.valid = false;
            live->driver = {};
        }
        live->driverOwner = owner;
        live->boarding = boarding;
        live->wheelStepFrame = wheelFrame;
        // Native alignment and its hold brake own a parked car during entry.
        // A moving car, or one CJ already drives, keeps the live suspension.
        if (live->parkedEntry) {
            live->reseed = true;
            live->pose.valid = false;
            stockSuspensionWithBodies(car);
            return;
        }
        if (!(at<unsigned char>(car, 0x428) & 0x10)) frame.gas = 0.f; // bEngineOn
    }
    frame.reseedContacts = (live ? live->reseed : reseedContacts) ? reseedWheels : 0u;
    if (live && live->resetControls) frame.reseedContacts |= resetDriver;
    const float inertiaScale = frame.mass / 12.f;
    // Approximate a box around the native centre of mass. Each axis uses
    // the two dimensions perpendicular to it, unlike SA's scalar turn mass.
    frame.inertia = {inertiaScale * (dimensions.y * dimensions.y + dimensions.z * dimensions.z),
                     inertiaScale * (dimensions.x * dimensions.x + dimensions.z * dimensions.z),
                     inertiaScale * (dimensions.x * dimensions.x + dimensions.y * dimensions.y), 1.f};
    const auto wheelCenter = [&](int index) {
        // CMonsterTruck::SetupSuspensionLines (0x6C7FB0) allocates four
        // 0x24-byte CColDisks. Each starts with a centre and a radius.
        if (diskWheels) return at<V>(col->data->line, index * 0x24);
        return static_cast<const SuspensionLine*>(col->data->line)[index].start;
    };
    const V fl = wheelCenter(0), rl = wheelCenter(1), fr = wheelCenter(2), rr = wheelCenter(3);
    const V com = at<V>(car, 0xA4);
    const float front = (fl.y + fr.y) * .5f;
    const float rear = (rl.y + rr.y) * .5f;
    const float left = (fl.x + rl.x) * .5f;
    const float right = (fr.x + rr.x) * .5f;
    const float frontShare = std::clamp((com.y - rear) / (front - rear), .1f, .9f);
    const float leftShare = std::clamp((right - com.x) / (right - left), .1f, .9f);
    bool supported = transform.at.z > .5f && finite(dimensions) &&
        dimensions.x > .2f && dimensions.y > .35f && dimensions.z > .15f &&
        front - rear > dimensions.y*.2f && right - left > dimensions.x*.25f &&
        finite(com) && finite(velocity) && finite(spin);
    constexpr int nativeWheel[] = {0, 2, 1, 3}; // FL, FR, RL, RR
    unsigned rampWheels = 0, groundedWheels = 0;
    const bool hasBodies = prepareWheelBodyContacts(car);
    unsigned bodyWheels = 0;
    for (int i = 0; i < 4; ++i) {
        const int index = nativeWheel[i];
        const bool bodySupport = hasBodies && suspensionContacts[index].body >= 0;
        bodyWheels += bodySupport;
        const float fraction = at<float>(car, 0x7D4 + index * 4);
        const float spring = at<float>(car, 0x878 + index * 4);
        const float line = at<float>(car, 0x888 + index * 4);
        // Monster control stores upper/lower wheel heights in these fields
        // and has already divided compression by their difference.
        const float travel = diskWheels ? spring - line : spring;
        const float radius = diskWheels ? at<float>(col->data->line, index * 0x24 + 0x0C) : line - spring;
        const auto& contact = at<ColPoint>(car, 0x724 + sizeof(ColPoint) * index);
        const bool grounded = fraction < 1.f;
        void* support = grounded ? at<void*>(car, 0x8E8 + index * sizeof(void*)) : nullptr;
        groundedWheels += grounded;
        rampWheels += support && support == rampObject;
        const float share = (i < 2 ? frontShare : 1.f - frontShare) *
                            (i % 2 == 0 ? leftShare : 1.f - leftShare);
        frame.contacts[i] = {quad(grounded ? contact.point : transform.pos),
                             quad(grounded ? contact.normal : V{0, 0, 1}),
                             travel, std::clamp(1.f - fraction, 0.f, 1.f), radius, share,
                             grounded && !bodySupport};
        supported = supported && std::isfinite(fraction) && travel > 0.f && radius > .05f &&
            (!grounded || (finite(contact.point) && finite(contact.normal) && contact.normal.z > .45f &&
                           fixedWheelSupport(support)));
    }
    Result output{};
    void* current = live ? live->solver : solver;
    const int ref = vehicleRef(car);
    TrafficDriving* driver = live ? &live->driver : fixture && traffic ? &fixtureDriver : nullptr;
    float limits[2]{};
    if (driver && npcDriver && (!live || !live->boarding) && supported && groundedWheels >= 3 && current &&
        driverLimits(current, limits) && limits[0] > .1f)
        adaptTrafficDriving(car, *driver, frame, front-rear, limits[0], limits[1]);
    replaced = current && supported && step(current, &frame, &output) != 0;
    auto& turnPlan = live ? live->pose : fixtureTurn;
    turnPlan.valid = replaced;
    if (replaced) {
        observeMenuPhysics(car, 1);
        if (driver && npcDriver) {
            at<float>(car, 0x49C) = frame.gas;
            at<float>(car, 0x4A0) = frame.brake;
        }
        wheelBodyDeltaVelocity = {};
        wheelBodyTorque = {};
        stockSuspension(car);
        suspensionCar = nullptr;
        V inverse[3], bodySpin{};
        if (inverseTensor(car, inverse)) {
            bodySpin = {dot(inverse[0], wheelBodyTorque), dot(inverse[1], wheelBodyTorque),
                        dot(inverse[2], wheelBodyTorque)};
        }
        at<V>(car, 0x44) = vec(output.velocity) * .02f + wheelBodyDeltaVelocity;
        at<V>(car, 0x50) = (vec(output.spin) + bodySpin) * .02f;
        if (loadedSuspension && loadedSuspension->car == car && groundedWheels >= 3) {
            loadedSuspension->verticalSpeed = at<V>(car, 0x44).z;
        }
        turnPlan = {frame.basis, output.basis, quad(vec(output.spin) * .02f * 50.f),
                    frame.dt, wheelFrame, true};
        if (fixture) {
            ++applied;
            admiralSteps += tuned;
            nativeDampingReplaced += replaceDrag;
            bodyWheelSteps += bodyWheels > 0;
            rampIvSteps += rampWheels != 0;
            reseedContacts = false;
        } else if (live) {
            live->reseed = false;
            live->resetControls = false;
            const auto* driver = at<void*>(car, 0x460);
            if (auditTraffic && driver && now - live->lastSample >= 900) {
                live->lastSample = now;
                const bool police = (at<unsigned char>(car, 0x428) & 1) != 0;
                const unsigned mission = at<unsigned char>(car, 0x390 + 0x2A);
                event("vehicle_iv_ai_step", vehicleRef(car), at<short>(car, 0x22),
                    std::string("role=") + (driver == player() ? "player" : police ? "police" : "civilian") +
                    " mission=" + std::to_string(mission) + " gas=" + std::to_string(frame.gas) +
                    " brake=" + std::to_string(frame.brake) + " steer=" + std::to_string(frame.steer) +
                    " mps=" + std::to_string(length(velocity)*50.f) +
                    " x=" + std::to_string(transform.pos.x) + " y=" + std::to_string(transform.pos.y));
            }
        }
    } else {
        stockSuspension(car);
        suspensionCar = nullptr;
        if (fixture && solver) {
            ++fallback;
            reseedContacts = true;
        }
        if (live) live->reseed = true;
    }

    if (fixture && elapsed >= sampleAt) {
        sampleAt = elapsed + 20;
        constexpr float deg = 57.295779513f;
        const auto& inertia = replaced ? output.inertia : frame.inertia;
        trace << elapsed << ',' << frame.dt << ',' << transform.pos.x << ',' << transform.pos.y << ',' << transform.pos.z
              << ',' << std::atan2(-transform.up.x, transform.up.y) * deg
              << ',' << std::asin(std::clamp(transform.up.z, -1.f, 1.f)) * deg
              << ',' << std::asin(std::clamp(transform.right.z, -1.f, 1.f)) * deg
              << ',' << dot(velocity, transform.up) * 50.f << ',' << dot(velocity, transform.right) * 50.f
              << ',' << frame.gas << ',' << frame.brake << ',' << frame.steer
              << ',' << (replaced ? output.gear : int(at<unsigned char>(car, 0x4B4)))
              << ',' << output.revs << ',' << output.drive << ',' << output.lateralForce;
        for (const auto& contact : frame.contacts) trace << ',' << contact.compression * contact.travel;
        for (float load : output.spring) trace << ',' << load;
        auto* peer = vehicleFromRef(collisionTarget);
        const float peerSpeed = peer ? length(at<V>(peer, 0x44))*50.f : 0.f;
        const float peerPitch = peer && matrix(peer) ? std::asin(std::clamp(matrix(peer)->up.z,-1.f,1.f))*deg : 0.f;
        const float peerRoll = peer && matrix(peer) ? std::asin(std::clamp(matrix(peer)->right.z,-1.f,1.f))*deg : 0.f;
        trace << ',' << applied << ',' << fallback << ',' << pairs
              << ',' << rampWheels << ',' << rampIvSteps << ',' << groundedWheels << ',' << frame.mass
              << ',' << inertia[0] << ',' << inertia[1] << ',' << inertia[2]
              << ',' << frontShare << ',' << turnApplied << ',' << integratedApplied
              << ',' << admiralSteps << ',' << nativeDampingReplaced << ',' << bodyWheelSteps
              << ',' << peerSpeed << ',' << peerPitch << ',' << peerRoll
              << ',' << at<float>(car,0x58C) << ',' << frame.handbrake;
#ifdef DELIRIUM_VEHICLE_SPEED_PROBE
        trace << ',' << speed_probe::usingPad;
#else
        trace << ",0";
#endif
        trace << std::endl;
    }
}

static void maintain() {
    vehicle_deformation::maintain();
    classic_body_motion::maintain();
    vehicle_menu::startIzzy();
    vehicle_nitro::samplePad();
    if(carRef>=0&&(!vehicleFromRef(carRef)||gameTime()-started>fixtureDuration+250))finishFixture();
    const unsigned now = gameTime();
    std::erase_if(hullLoadTimes, [now](const auto& entry) {
        return now - entry.second > 1200 || !vehicleFromRef(entry.first);
    });
    if (!traffic) return;
    for (auto it = trafficCars.begin(); it != trafficCars.end();) {
        if (vehicleFromRef(it->first) != it->second.entity || now - it->second.lastSeen > 2500) {
            if (it->second.solver) destroy(it->second.solver);
            it = trafficCars.erase(it);
        } else ++it;
    }
}
}

static void __fastcall vehicleLabSuspension(void* car, void*) {
    vehicle_lab::suspension(car);
}
static bool vehicleLabInverseTensor(void* car, V* rows) {
    return vehicle_lab::inverseTensor(car, rows);
}
static void vehicleLabObserveHullLoad(void* car, V velocityDelta, V torque) {
    vehicle_lab::observeHullLoad(car, velocityDelta, torque);
}
