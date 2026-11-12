#pragma once

#include "ExpandedSkinRenderHost.hpp"
#include <array>
#include <map>
#include <string_view>
#include <utility>

namespace vehicle_deformation {
using namespace expandedSkinHost;
constexpr uintptr_t damageAddress = 0x6A7650, renderAddress = 0x6A2B10, fixAddress = 0x6A3440;
constexpr unsigned maxCars = 32, maxParts = 48;
static const Api rw = pinnedApi();
static bool enabled = false, installed = false;
static void (__thiscall* nativeDamage)(void*, float, int, void*, V*, V*, int) = nullptr;
static void (__thiscall* nativeRender)(void*) = nullptr;
static void (__thiscall* nativeFix)(void*) = nullptr;

static void* (__cdecl* createDentMap)(float, unsigned) = nullptr;
static void (__cdecl* destroyDentMap)(void*) = nullptr;
static void (__cdecl* queueImpact)(void*, const V*, const V*, float, float) = nullptr;
static void (__cdecl* queueCornerImpact)(void*, const V*, const V*, float, float) = nullptr;
static int (__cdecl* stampImpacts)(void*) = nullptr;
static void (__cdecl* sampleDentMap)(const void*, const V*, V*) = nullptr;

struct Surface {
    Atomic* atomic;
    Geometry* source;
    Geometry* shape;
    unsigned revision;
    bool visible;
};

struct Car {
    void* clump = nullptr;
    short model = -1;
    unsigned lastHit = 0, revision = 0;
    void* dentMap = nullptr;
    std::array<Surface, maxParts> surfaces{};
    unsigned parts = 0;

    Car() = default;
    Car(const Car&) = delete;
    Car& operator=(const Car&) = delete;
    ~Car() {
        if (dentMap) destroyDentMap(dentMap);
        for (unsigned i = 0; i < parts; ++i) {
            const auto& part = surfaces[i];
            if (part.shape) rw.destroy(part.shape);
            rw.destroy(part.source);
        }
    }
};

// The OS reclaims this pool at exit. Native geometry destructors are only
// called during gameplay, while RenderWare and its plugins are still loaded.
static auto& cars = *new std::map<int, Car>;

static void setEnabled(bool value) {
    enabled = value;
    if (!value) cars.clear();
}

static bool allowed() {
    return enabled && playing() && !cutscene() && !(protectMissions && scriptMissionActive());
}

static bool supported(void* car) {
    return at<int>(car, 0x594) == 0 && at<void*>(car, 0x18) && matrix(car) &&
        !(protectMissions && at<unsigned char>(car, 0x4A4) == 2 && onMission());
}

static void maintain() {
    std::erase_if(cars, [](const auto& entry) {
        auto* car = vehicleFromRef(entry.first);
        return !car || at<void*>(car, 0x18) != entry.second.clump || at<short>(car, 0x22) != entry.second.model;
    });
    for (auto& [ref, car] : cars) if (car.dentMap && stampImpacts(car.dentMap)) ++car.revision;
}

static bool collisionCorner(void* source, V hit, V direction, V& corner) {
    if (!source || !finite(hit) || !finite(direction)) return false;
    const unsigned type = at<unsigned char>(source, 0x36) & 7u;
    if (type != 1 && (type != 4 || !(at<unsigned char>(source, 0x1C) & 4u))) return false;
    if (std::abs(direction.z) > .45f) return false;
    direction = normalized(V{direction.x, direction.y, 0.f});
    if (length(direction) < .8f) return false;
    auto* col = reinterpret_cast<ColModel*(__thiscall*)(void*)>(0x535300)(source);
    if (!col || !col->data) return false;
    M simple{};
    const M* m = matrix(source);
    if (!m) {
        reinterpret_cast<void(__thiscall*)(void*, M*)>(0x54EF40)(static_cast<unsigned char*>(source) + 4, &simple);
        m = &simple;
    }
    struct Line { V from; float fromSize; V to; float toSize; };
    static_assert(sizeof(Line) == 0x20);
    const auto cast = reinterpret_cast<bool(__cdecl*)(const Line&,const M&,ColModel&,ColPoint&,float&,bool,bool)>(0x417950);
    const V tangent{-direction.y, direction.x, 0.f};
    ColPoint faces[2]{};
    for (int i = 0; i < 2; ++i) {
        const float side = i ? 1.f : -1.f;
        const V ray = normalized(direction + tangent * (side * 1.10f));
        const V centre = hit + tangent * (side * .08f);
        const Line line{centre + ray * .36f, 0.f, centre - ray * .28f, 0.f};
        float fraction = 1.f;
        auto& face = faces[i];
        if (!cast(line, *m, *col, face, fraction, false, false) ||
            !finite(face.point) || !finite(face.normal) || std::abs(face.normal.z) > .45f ||
            dot(face.normal, ray) <= .15f || length(face.point - hit) > .32f) return false;
    }
    const auto& a = faces[0];
    const auto& b = faces[1];
    const float cosine = dot(a.normal, b.normal);
    if (cosine > .80f || cosine < -.75f ||
        dot(a.normal, b.point - a.point) > .025f ||
        dot(b.normal, a.point - b.point) > .025f) return false;
    // Closest point on the intersection of the two outward planes.
    const float da = dot(a.normal, a.point - hit), db = dot(b.normal, b.point - hit);
    const V shift = (a.normal * (da - cosine * db) + b.normal * (db - cosine * da)) *
        (1.f / (1.f - cosine * cosine));
    if (!finite(shift) || length(shift) > .24f) return false;
    corner = hit + shift;
    return true;
}

static void record(void* car, int ref, V worldPoint, V worldDirection, float impulse, bool corner) {
    M worldToCar{};
    if (!finite(worldPoint) || !finite(worldDirection) || !affineInverse(*matrix(car), worldToCar)) return;
    const V local = point(worldToCar, worldPoint);
    V direction = normalized(vector(worldToCar, worldDirection));
    if (!finite(local) || length(local) > 13.f || length(direction) < .8f) return;
    const float mass = at<float>(car, 0x8C);
    if (!std::isfinite(mass) || mass <= 0.f || !std::isfinite(impulse) || impulse <= 0.f) return;
    const auto* col = reinterpret_cast<ColModel*(__thiscall*)(void*)>(0x535300)(car);
    if (!col || !std::isfinite(col->radius) || col->radius <= 0.f) return;
    if (dot(direction, local) > 0.f) direction = direction * -1.f;
    if (cars.size() >= maxCars && !cars.contains(ref)) {
        auto oldest = std::min_element(cars.begin(), cars.end(), [](const auto& a, const auto& b) {
            return a.second.lastHit < b.second.lastHit;
        });
        cars.erase(oldest);
    }
    const auto clump = at<void*>(car, 0x18);
    const auto model = at<short>(car, 0x22);
    const auto old = cars.find(ref);
    if (old != cars.end() && (old->second.clump != clump || old->second.model != model)) cars.erase(old);
    auto& state = cars.try_emplace(ref).first->second;
    state.clump = clump;
    state.model = model;
    state.lastHit = gameTime();
    if (!state.dentMap) {
        state.dentMap = createDentMap(col->radius, unsigned(ref));
        if (!state.dentMap) { cars.erase(ref); return; }
        event("vehicle_deformation_impact", ref, model, "map=iv contact_ns=" + std::to_string(impulse * 50.f));
    }
    // The host converts SA's mass-times-speed impulse to Newton seconds for
    // CVehicleDeformation::ApplyCollisionImpact (TU8 0x822833A8).
    const V j = direction * (impulse * 50.f);
    if (corner) {
        queueCornerImpact(state.dentMap, &j, &local, mass, 1.f);
        event("vehicle_deformation_corner", ref, model, "source_faces=2 contact_ns=" + std::to_string(impulse * 50.f) +
            " local_x=" + std::to_string(local.x) + " local_y=" + std::to_string(local.y));
    } else queueImpact(state.dentMap, &j, &local, mass, 1.f);
}

static void __fastcall damage(void* car, void*, float intensity, int component, void* source, V* contact, V* direction, int weapon) {
    const int ref = intensity == 0.f && allowed() && supported(car) ? vehicleRef(car) : -1;
    const float before = ref >= 0 ? at<float>(car, 0x4C0) : 0.f;
    const float impulse = ref >= 0 ? at<float>(car, 0xD8) : 0.f;
    const V hit = ref >= 0 ? at<V>(car, 0xEC) : V{};
    const V normal = ref >= 0 ? at<V>(car, 0xE0) : V{};
    const float mass = ref >= 0 ? at<float>(car, 0x8C) : 0.f;
    V edge{};
    const bool corner = ref >= 0 && queueCornerImpact && before > 0.f &&
        (at<unsigned char>(car, 0x42A) & 0x20u) && !(at<unsigned>(car, 0x40) & (1u << 20)) &&
        std::isfinite(mass) && mass > 0.f && std::isfinite(impulse) && impulse * 50.f / mass >= 2.f &&
        collisionCorner(at<void*>(car, 0xDC), hit, normal, edge);
    nativeDamage(car, intensity, component, source, contact, direction, weapon);
    if (ref < 0 || vehicleFromRef(ref) != car || !supported(car)) return;
    const float lost = before - at<float>(car, 0x4C0);
    if (corner || (std::isfinite(lost) && lost > 1.f))
        record(car, ref, corner ? edge : hit, normal, impulse, corner);
}

static V displacement(V pointInCar, const Car& car) {
    V offset{};
    sampleDentMap(car.dentMap, &pointInCar, &offset);
    return offset;
}

static bool supportedGeometry(const Geometry* g) {
    return g && !(g->flags & 0x0F000000) && g->morphsCount == 1 && g->morph &&
        g->morph->vertices && g->verticesCount >= 3 && g->verticesCount <= 4800 &&
        g->trianglesCount > 0 && g->trianglesCount <= 9600 && g->triangles &&
        g->uvCount >= 0 && g->uvCount <= 8 && g->materials.items && g->materials.count > 0 &&
        (!(g->flags & 0x10) || g->morph->normals);
}

static Geometry* makeGeometry(const Geometry& source) {
    auto* g = rw.create(source.verticesCount, source.trianglesCount,
        (source.flags & 0xFFFF) | (unsigned(source.uvCount) << 16));
    if (!g) return nullptr;
    const auto fail = [&]() -> Geometry* { rw.destroy(g); return nullptr; };
    if (!g->triangles || !g->morph || !g->morph->vertices) return fail();
    for (int i = 0; i < source.trianglesCount; ++i) {
        const auto& tri = source.triangles[i];
        if (tri.material >= source.materials.count || !source.materials.items[tri.material] ||
            tri.vertex[0] >= source.verticesCount || tri.vertex[1] >= source.verticesCount ||
            tri.vertex[2] >= source.verticesCount) return fail();
        g->triangles[i] = tri;
        if (!rw.material(g, &g->triangles[i], source.materials.items[tri.material])) return fail();
    }
    const size_t count = size_t(source.verticesCount);
    if (source.flags & 8) {
        if (!source.prelight || !g->prelight) return fail();
        std::memcpy(g->prelight, source.prelight, count * sizeof(uint32_t));
    }
    for (int i = 0; i < source.uvCount; ++i) {
        if (!source.uv[i] || !g->uv[i]) return fail();
        std::memcpy(g->uv[i], source.uv[i], count * sizeof(float) * 2);
    }
    // The deformation goes inwards. Keep the original sphere as a
    // conservative bound and share the stock atomic's visibility pipeline.
    g->morph->bounds = source.morph->bounds;
    g->morph->bounds.radius += .5f * std::sqrt(3.f);
    return g;
}

struct DrawContext {
    Car& car;
    M worldToCar;
    std::array<Surface*, maxParts> swaps{};
    unsigned count = 0;
};

static bool eligiblePanel(std::string_view panel) {
    return panel.starts_with("chassis") || panel.starts_with("bonnet") || panel.starts_with("boot") ||
        panel.starts_with("bump_") || panel.starts_with("door_") || panel.starts_with("wing_");
}

static Atomic* __cdecl selectPanel(Atomic* atomic, void* context) {
    auto& draw = *static_cast<DrawContext*>(context);
    auto& car = draw.car;
    if (draw.count == maxParts || !(atomic->frameObject[2] & 4) || atomic->instance) return atomic;
    auto* source = atomic->geometry;
    auto* frame = at<void*>(atomic, 4);
    if (!supportedGeometry(source) || !frame) return atomic;
    const auto name = reinterpret_cast<const char*(__cdecl*)(void*)>(0x72FB30)(frame);
    if (!name) return atomic;
    const std::string_view panel(name, strnlen(name, 24));
    if (!eligiblePanel(panel)) return atomic;
    auto end = car.surfaces.begin() + car.parts;
    auto part = std::find_if(car.surfaces.begin(), end, [=](const Surface& s) {
        return s.atomic == atomic && s.source == source;
    });
    if (part == end) {
        if (car.parts == maxParts || !rw.addRef(source)) return atomic;
        ++car.parts;
        *part = {atomic, source, nullptr, 0, false};
    }
    if (part->revision != car.revision) {
        const auto* frameToWorld = reinterpret_cast<const M*(__cdecl*)(void*)>(0x7F0990)(frame);
        if (!frameToWorld) return atomic;
        const M localToCar = affineProduct(draw.worldToCar, *frameToWorld);
        M carToLocal{};
        if (!affineInverse(localToCar, carToLocal)) return atomic;
        const auto n = source->verticesCount;
        float moved = 0.f;
        std::array<V, 4800> positions;
        const auto offsetAt = [&](V v) {
            return vector(carToLocal, displacement(point(localToCar, v), car));
        };
        for (int i = 0; i < n; ++i) {
            const V v = source->morph->vertices[i];
            if (!finite(v)) return atomic;
            const V offset = offsetAt(v);
            moved = std::max(moved, dot(offset, offset));
            positions[i] = v + offset;
        }
        part->revision = car.revision;
        part->visible = false;
        if (moved < .000025f) return atomic;
        const bool newShape = !part->shape;
        auto* shape = newShape ? makeGeometry(*source) : part->shape;
        if (!shape || !rw.lock(shape, 6)) {
            if (shape && newShape) rw.destroy(shape);
            return atomic;
        }
        std::memcpy(shape->morph->vertices, positions.data(), size_t(n) * sizeof(V));
        if (shape->morph->normals && source->morph->normals) {
            for (int i = 0; i < n; ++i) {
                const V v = source->morph->vertices[i];
                const V normal = source->morph->normals[i];
                const V change = positions[i] - v;
                shape->morph->normals[i] = normal;
                if (dot(change, change) <= 1.e-6f || !finite(normal) || length(normal) < .5f) continue;
                const V tangent = normalized(cross(std::abs(normal.z) > .9f ? V{0,1,0} : V{0,0,1}, normal)) * .018f;
                const V bitangent = cross(normalized(normal), tangent);
                const V warped = normalized(cross(tangent + offsetAt(v + tangent) - change,
                    bitangent + offsetAt(v + bitangent) - change));
                if (dot(warped, normal) > .25f) shape->morph->normals[i] = warped;
            }
        }
        if (!rw.unlock(shape)) {
            if (newShape) rw.destroy(shape);
            return atomic;
        }
        part->shape = shape;
        part->visible = true;
        if (newShape) event("vehicle_deformation_mesh", -1, car.model,
            "panel=" + std::string(panel) + " max_displacement=" + std::to_string(std::sqrt(moved)));
    }
    if (!part->visible) return atomic;
    draw.swaps[draw.count++] = &*part;
    rw.setGeometry(atomic, part->shape, 1);
    return atomic;
}

static void drawCar(void* car, DrawContext* draw) {
    __try {
        reinterpret_cast<void*(__cdecl*)(void*, decltype(&selectPanel), void*)>(0x749B70)(
            draw->car.clump, selectPanel, draw);
        nativeRender(car);
    } __finally {
        while (draw->count) {
            auto& part = *draw->swaps[--draw->count];
            if (part.atomic->instance) rw.freeInstance(part.atomic->instance);
            part.atomic->instance = nullptr;
            rw.setGeometry(part.atomic, part.source, 1);
        }
    }
}

static void __fastcall render(void* car, void*) {
    classic_body_motion::RenderPose bodyPose(car);
    if (cars.empty() || !allowed()) { nativeRender(car); return; }
    const auto found = cars.find(vehicleRef(car));
    if (found == cars.end() || !supported(car) || at<void*>(car, 0x18) != found->second.clump ||
        at<short>(car, 0x22) != found->second.model) { nativeRender(car); return; }
    M worldToCar{};
    const auto* root = classic_body_motion::RenderPose::rootFor(car);
    const auto* pose = root ? reinterpret_cast<const M*(__cdecl*)(const void*)>(0x7F0990)(root) : matrix(car);
    if (!pose || !affineInverse(*pose, worldToCar)) { nativeRender(car); return; }
    auto& state = found->second;
    if (!state.dentMap || !state.revision) { nativeRender(car); return; }
    DrawContext draw{state, worldToCar};
    drawCar(car, &draw);
}

static void __fastcall fix(void* car, void*) {
    if (!cars.empty()) cars.erase(vehicleRef(car));
    nativeFix(car);
}

static bool install(HMODULE engine) {
    createDentMap = reinterpret_cast<decltype(createDentMap)>(GetProcAddress(engine, "sa_deformation_create"));
    destroyDentMap = reinterpret_cast<decltype(destroyDentMap)>(GetProcAddress(engine, "sa_deformation_destroy"));
    queueImpact = reinterpret_cast<decltype(queueImpact)>(GetProcAddress(engine, "sa_deformation_impact"));
    queueCornerImpact = reinterpret_cast<decltype(queueCornerImpact)>(GetProcAddress(engine, "sa_deformation_corner_impact"));
    stampImpacts = reinterpret_cast<decltype(stampImpacts)>(GetProcAddress(engine, "sa_deformation_update"));
    sampleDentMap = reinterpret_cast<decltype(sampleDentMap)>(GetProcAddress(engine, "sa_deformation_at"));
    if (!createDentMap || !destroyDentMap || !queueImpact || !stampImpacts || !sampleDentMap) return false;
    struct Hook { uintptr_t address; const char* entry; void* detour; void** next; };
    const Hook hooks[] = {
        {damageAddress, "\x6A\xFF\x68\xD8\x7F\x84", reinterpret_cast<void*>(&damage), reinterpret_cast<void**>(&nativeDamage)},
        {renderAddress, "\x83\xEC\x74\xA1\x84\xCB", reinterpret_cast<void*>(&render), reinterpret_cast<void**>(&nativeRender)},
        {fixAddress, "\x6A\xFF\x64\xA1\x00\x00", reinterpret_cast<void*>(&fix), reinterpret_cast<void**>(&nativeFix)}
    };
    for (const auto& hook : hooks)
        if (std::memcmp(reinterpret_cast<void*>(hook.address), hook.entry, 6) != 0) return false;
    unsigned created = 0;
    for (const auto& hook : hooks) {
        auto* target = reinterpret_cast<void*>(hook.address);
        if (MH_CreateHook(target, hook.detour, hook.next) != MH_OK) break;
        ++created;
        if (MH_QueueEnableHook(target) != MH_OK) break;
        if (created == std::size(hooks)) return installed = true;
    }
    while (created) MH_RemoveHook(reinterpret_cast<void*>(hooks[--created].address));
    return false;
}
}
