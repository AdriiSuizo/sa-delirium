#pragma once

namespace classic_body_motion {
struct State {
    void* entity{};
    unsigned time{};
    V velocity{}, spin{};
    V offset{}, rate{}; // pitch, roll, heave
};
static bool enabled = false;
static std::unordered_map<int, State> motion;

static void setEnabled(bool value) {
    enabled = value;
    if (!value) motion.clear();
}

static bool bodyVehicle(void* entity) {
    if ((at<unsigned char>(entity, 0x36) & 7) != 2) return false;
    const int type = at<int>(entity, 0x594);
    // CAutomobile, CMonsterTruck, CQuadBike and CTrailer share the body nodes.
    return (type >= 0 && type <= 2) || type == 11;
}

static State* state(void* car) {
    if (!enabled || !bodyVehicle(car) || !playing() || cutscene() ||
        (protectMissions && scriptMissionActive()) || !matrix(car) || matrix(car)->at.z < .52f)
        return nullptr;
    const int ref = vehicleRef(car);
    if (ref < 0) return nullptr;
    auto& s = motion[ref];
    const auto now = gameTime();
    if (s.entity != car || now - s.time > 240)
        s = {car, now, at<V>(car, 0x44) * 50.f, at<V>(car, 0x50) * 50.f};
    return &s;
}

static void impulse(void* car, V velocity, V torque) {
    if (velocity.z >= -.0001f || !finite(velocity) || !finite(torque)) return;
    if (auto* s = state(car)) {
        const auto& pose = *matrix(car);
        const float moment = std::max(at<float>(car, 0x90), 400.f);
        s->rate.x += std::clamp(dot(torque, pose.right) * 33.f / moment, -.6f, .6f);
        s->rate.y += std::clamp(dot(torque, pose.up) * 33.f / moment, -.6f, .6f);
        s->rate.z += std::clamp(velocity.z * 30.f, -.32f, 0.f);
        // The body impulse is accounted for here. It is already present in
        // SA's velocity, so exclude it from the next visual impact sample.
        s->velocity = at<V>(car, 0x44) * 50.f;
        s->spin = at<V>(car, 0x50) * 50.f;
    }
}

static void maintain() {
    if (!enabled) return;
    const unsigned now = gameTime();
    std::erase_if(motion, [now](const auto& entry) {
        return now - entry.second.time > 800 || vehicleFromRef(entry.first) != entry.second.entity;
    });
}

static Q axis(V direction, float angle) {
    const V v = direction * std::sin(angle * .5f);
    return {v.x, v.y, v.z, std::cos(angle * .5f)};
}

static T poseFor(void* car) {
    auto* s = state(car);
    if (!s) return {};
    const auto now = gameTime();
    const auto& pose = *matrix(car);
    const float dt = std::min((now - s->time) * .001f, .06f);
    if (dt > 0.f) {
        const V velocity = at<V>(car, 0x44) * 50.f;
        const V spin = at<V>(car, 0x50) * 50.f;
        if (!finite(velocity) || !finite(spin)) return {};
        const V dv = velocity - s->velocity;
        const V dw = spin - s->spin;
        const auto* model = reinterpret_cast<ColModel*(__thiscall*)(void*)>(0x535300)(car);
        const float wheelbase = model ? std::max(model->box.max.y - model->box.min.y, 1.4f) : 4.f;
        const float width = model ? std::max(model->box.max.x - model->box.min.x, .8f) : 1.7f;
        const float mass = std::max(at<float>(car, 0x8C), 500.f);
        const float compliance = std::clamp(std::sqrt(1450.f / mass), .55f, 1.35f);
        const float forward = dot(dv, pose.up);
        const float lateral = dot(dv, pose.right);
        const float vertical = dot(dv, pose.at);
        const V target{
            std::clamp(forward / dt * .028f / wheelbase * compliance, -.064f, .064f),
            std::clamp(-lateral / dt * .019f / width * compliance, -.075f, .075f),
            0.f
        };
        if (std::abs(forward) > 1.6f || std::abs(lateral) > 1.4f || std::abs(vertical) > 1.2f) {
            s->rate = s->rate + V{
                std::clamp(.18f * forward / wheelbase + .16f * dot(dw, pose.right), -.55f, .55f),
                std::clamp(.14f * lateral / width + .16f * dot(dw, pose.up), -.55f, .55f),
                -std::clamp(std::abs(vertical) * .12f, 0.f, .38f)
            } * compliance;
        }
        const float stiffness = 78.f / compliance;
        const unsigned steps = std::max(1u, unsigned(std::ceil(dt / .012f)));
        const float h = dt / steps;
        for (unsigned i = 0; i < steps; ++i) {
            s->rate = (s->rate + (target - s->offset) * (stiffness * h)) * std::exp(-10.f * h);
            s->offset = s->offset + s->rate * h;
            s->offset = {std::clamp(s->offset.x, -.09f, .09f), std::clamp(s->offset.y, -.10f, .10f),
                         std::clamp(s->offset.z, -.065f, .025f)};
        }
        s->velocity = velocity;
        s->spin = spin;
        s->time = now;
    }
    return {mul(axis({1,0,0}, s->offset.x), axis({0,1,0}, s->offset.y)), {0,0,s->offset.z}};
}

static bool displaced(const T& pose) {
    return pose.q.x != 0.f || pose.q.y != 0.f || pose.q.z != 0.f || std::abs(pose.p.z) >= .0001f;
}

static M withOffset(const M& original, const T& offset) {
    M local{};
    write(local, offset);
    auto result = affineProduct(original, local);
    result.flags &= ~0x20000u; // RenderWare's matrix is no longer identity.
    return result;
}

struct RenderPose {
    static inline thread_local RenderPose* current = nullptr;
    RenderPose* previous = nullptr;
    void* entity;
    void* root = nullptr;
    M original{};
    struct WheelFrame { void* frame; M local; };
    std::array<WheelFrame, 6> wheels{};
    unsigned wheelCount = 0;
    static void update(void* frame) {
        reinterpret_cast<void*(__cdecl*)(void*)>(0x7F0910)(frame);
    }
    explicit RenderPose(void* car) : entity(car) {
        if (rootFor(car)) return;
        const auto offset = poseFor(car);
        if (!displaced(offset)) return;
        auto* clump = at<void*>(car, 0x18);
        if (!clump) return;
        root = at<void*>(clump, 4);
        if (!root) return;
        auto& local = at<M>(root, 0x10);
        original = local;
        local = withOffset(original, offset);
        // Wheels directly attached to the clump keep their native ground
        // position. The visual suspension travels between them and the shell.
        M undo{};
        write(undo, inverse(offset));
        for (int node = 2; node <= 7; ++node) {
            auto* wheel = at<void*>(car, 0x648 + node * 4);
            if (!wheel || at<void*>(wheel, 4) != root) continue;
            auto& model = at<M>(wheel, 0x10);
            wheels[wheelCount++] = {wheel, model};
            model = affineProduct(undo, model);
            model.flags &= ~0x20000u;
        }
        previous = current;
        current = this;
        update(root);
    }
    ~RenderPose() {
        if (!root) return;
        current = previous;
        at<M>(root, 0x10) = original;
        for (unsigned i = 0; i < wheelCount; ++i)
            at<M>(wheels[i].frame, 0x10) = wheels[i].local;
        update(root);
    }
    static void* rootFor(void* car) {
        for (auto* scope = current; scope; scope = scope->previous)
            if (scope->entity == car) return scope->root;
        return nullptr;
    }
};

static void (__cdecl* nativeRenderEntity)(void*) = nullptr;
static void (__thiscall* nativeLights)(void*, M&, unsigned) = nullptr;
static void (__thiscall* nativeBeam)(void*, unsigned, M&, bool) = nullptr;
using Corona = void(__cdecl*)(unsigned, void*, unsigned char, unsigned char, unsigned char,
    unsigned char, const V&, float, float, unsigned char, unsigned char, unsigned char,
    unsigned char, unsigned char, float, bool, float, bool, float, bool, bool);
static Corona nativeCorona = nullptr;

static void __cdecl renderEntity(void* entity) {
    // RenderOneNonRoad drains the vehicle's deferred alpha list after the
    // automobile's own Render returns. Keep the draw matrices until then.
    RenderPose pose(entity);
    nativeRenderEntity(entity);
}

struct LightMatrix {
    M value;
    void* attachment = nullptr;
    bool ownsAttachment = false;
};
static_assert(sizeof(LightMatrix) == 0x48);

static void __fastcall lights(void* car, void*, M& basis, unsigned flags) {
    const auto offset = poseFor(car);
    if (!displaced(offset)) { nativeLights(car, basis, flags); return; }
    LightMatrix draw{withOffset(basis, offset)};
    nativeLights(car, draw.value, flags);
}

static void __fastcall beam(void* car, void*, unsigned light, M& basis, bool right) {
    const auto offset = poseFor(car);
    if (!displaced(offset)) { nativeBeam(car, light, basis, right); return; }
    LightMatrix draw{withOffset(basis, offset)};
    nativeBeam(car, light, draw.value, right);
}

static void __cdecl corona(unsigned id, void* entity, unsigned char r, unsigned char g, unsigned char b,
    unsigned char alpha, const V& position, float size, float range, unsigned char type,
    unsigned char flare, unsigned char reflection, unsigned char los, unsigned char trail,
    float angle, bool neon, float pull, bool fullBright, float fade, bool below, bool white) {
    // Attached coronas are drawn later against the unchanged physical matrix.
    // Supply the body-local point in that matrix's local space at registration.
    const T offset = entity ? poseFor(entity) : T{};
    const V local = displaced(offset) ? offset.p + rotate(offset.q, position) : position;
    nativeCorona(id, entity, r, g, b, alpha, local, size, range, type, flare, reflection,
                 los, trail, angle, neon, pull, fullBright, fade, below, white);
}

static bool install() {
    struct Hook { uintptr_t address; const char* prologue; void* detour; void** original; };
    const Hook hooks[] = {
        {0x553260, "\x56\x8B\x74\x24\x08\x8A", reinterpret_cast<void*>(&renderEntity), reinterpret_cast<void**>(&nativeRenderEntity)},
        {0x6E1A60, "\xA0\x18\xCC\xC1\x00\x83", reinterpret_cast<void*>(&lights), reinterpret_cast<void**>(&nativeLights)},
        {0x6E0E20, "\x81\xEC\x94\x00\x00\x00", reinterpret_cast<void*>(&beam), reinterpret_cast<void**>(&nativeBeam)},
        {0x6FC580, "\x0F\xB6\x54\x24\x28\x8B", reinterpret_cast<void*>(&corona), reinterpret_cast<void**>(&nativeCorona)}
    };
    for (const auto& hook : hooks) {
        if (std::memcmp(reinterpret_cast<void*>(hook.address), hook.prologue, 6) != 0) {
            event("classic_visual_hook_refused", -1, -1, std::to_string(hook.address));
            return false;
        }
    }
    unsigned created = 0;
    for (const auto& hook : hooks) {
        auto* entry = reinterpret_cast<void*>(hook.address);
        if (MH_CreateHook(entry, hook.detour, hook.original) != MH_OK) break;
        ++created;
        if (MH_QueueEnableHook(entry) != MH_OK) break;
        if (created == std::size(hooks)) return true;
    }
    while (created) MH_RemoveHook(reinterpret_cast<void*>(hooks[--created].address));
    return false;
}
}
