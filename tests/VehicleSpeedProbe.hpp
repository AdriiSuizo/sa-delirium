#pragma once

// Included inside vehicle_lab only in the passive test build.
namespace speed_probe {
constexpr V runway{1450.f, -2601.f, 35.f};
constexpr unsigned topRunMs = 52000;
constexpr unsigned turnRunMs = 5350;
constexpr unsigned nativeTurnEndMs = 3750;
constexpr unsigned launchRunMs = 6800;
static std::unordered_map<int, float> measuredTop;
static int mode = 0;
static float startHeight = 0.f;
static bool usingPad = false;

static void place(void* car, V location, float height, float speed) {
    location.z = height;
    teleport(car, location);
    reinterpret_cast<void(__thiscall*)(void*,float)>(0x43E0C0)(car, -1.5707963268f);
    at<V>(car, 0x44) = matrix(car)->up * (speed / 50.f);
    at<V>(car, 0x50) = {};
    reseedContacts = true;
}

struct Inputs { float gas, brake, stick; bool handbrake; };
static Inputs inputs(void* car, unsigned elapsed, bool nativePad) {
    const int model = at<short>(car, 0x22);
    const float forward = dot(at<V>(car, 0x44), matrix(car)->up) * 50.f;
    float gas = 0.f, brake = elapsed < 500 ? .85f : 0.f, stick = 0.f;
    bool handbrake = false;
    if (mode == 1 || mode == 4) {
        if (mode == 1 && position(car).x > 1880.f) {
            // Repeat the same asphalt section, keeping the speed reached under power.
            place(car, runway, startHeight, forward);
        }
        gas = elapsed >= 500 ? 1.f : 0.f;
        if (mode == 1 && elapsed >= topRunMs - 10000) measuredTop[model] = std::max(measuredTop[model], forward);
    } else {
        const float entry = measuredTop.at(model) * .75f;
        if (!rollLaunched && elapsed >= 2300) {
            place(car, runway, startHeight, entry);
            rollLaunched = true;
        }
        const bool turning = elapsed >= 2350 &&
            elapsed < (nativePad ? nativeTurnEndMs : 4850) && matrix(car)->up.x > 0.f;
        stick = turning ? (nativePad ? .55f : .9f) : 0.f;
        handbrake = mode == 3 && turning && elapsed < 2650;
        if (elapsed >= (nativePad ? nativeTurnEndMs : 5050)) brake = .85f;
    }
    return {gas, brake, stick, handbrake};
}

PadInput::PadInput(void* car) {
    static const bool enabled = GetEnvironmentVariableA("GTASA_VEHICLE_NATIVE_PAD", nullptr, 0) != 0;
    if (!enabled || !mode || carRef < 0 || car != vehicleFromRef(carRef) ||
        !matrix(car) || !player() || at<void*>(car,0x460) != player()) return;
    const unsigned elapsed = gameTime() - started;
    if (elapsed > fixtureDuration) return;
    const Inputs command = inputs(car, elapsed, true);
    pad = reinterpret_cast<void*>(0xB73458);
    std::memcpy(previous.data(), pad, sizeof(previous));
    std::memset(pad, 0, sizeof(previous));
    previousMode = at<unsigned short>(pad, 0x10A);
    at<unsigned short>(pad, 0x10A) = 0;
    at<short>(pad, 0x00) = short(-command.stick * 128.f);
    at<short>(pad, 0x20) = short(command.gas * 255.f);
    at<short>(pad, 0x1C) = short(command.brake * 255.f);
    at<short>(pad, 0x0C) = command.handbrake ? 255 : 0;
    usingPad = true;
}

PadInput::~PadInput() {
    if (!pad) return;
    usingPad = false;
    std::memcpy(pad, previous.data(), sizeof(previous));
    at<unsigned short>(pad, 0x10A) = previousMode;
}

static bool controls(void* car, unsigned elapsed) {
    if (usingPad) return (at<unsigned char>(car, 0x428) & 0x20u) != 0;
    const Inputs command = inputs(car, elapsed, false);
    const auto* handling = at<unsigned char*>(car, 0x384);
    at<float>(car, 0x49C) = command.gas;
    at<float>(car, 0x4A0) = command.brake;
    at<float>(car, 0x58C) = command.stick;
    at<float>(car, 0x494) = handling ?
        *reinterpret_cast<const float*>(handling + 0xA0) * .01745329252f * command.stick * std::abs(command.stick) : 0.f;
    return command.handbrake;
}
}
