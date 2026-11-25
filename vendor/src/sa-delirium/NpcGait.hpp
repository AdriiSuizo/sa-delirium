#pragma once
#include "ABI.hpp"
#include "NpcGaitData.hpp"
#include "NpcGaitSole.hpp"
#include "NpcStride.hpp"

namespace npc_gait {
static Bank bank;
static npc_stride::Bank movers;
struct Actor {
    void* ped = nullptr;
    void* clump = nullptr;
    const Node* nodes = nullptr;
    const Set* set = nullptr;
    int model = -1, count = 0;
    Binding binding;
    SoleCalibration sole;
    V previous{};
    float movementPhase = 0, idlePhase = 0, blend = 0;
    float heading = 0, turnRate = 0, forwardSpeed = 0, legScale = 0;
    unsigned lastSeen = 0;
    bool moving = false;
    const npc_stride::Profile* mover = nullptr;
    npc_stride::State drive;
    unsigned driveFrame = UINT32_MAX;
    bool jogger = false;
    int modelGroup = -1;
};
static std::map<int, Actor> actors;
static void clear() { actors.clear(); }
static void release(int ref) { actors.erase(ref); }
static void prune(unsigned now) {
    for (auto it = actors.begin(); it != actors.end(); )
        if (now - it->second.lastSeen > 1200) it = actors.erase(it);
        else ++it;
}
static bool load(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    clear();
    bank = {};
    return in && readBank(in, bank);
}

struct Layer {
    float weight = 0, phase = 0;
};
struct NativeMotion {
    std::array<Layer, CycleCount> layers{};
    float baseWeight = 0;
    bool partial = false;
};
// The IDs overlap unrelated groups. Check the association group before its
// ID. Road-crossing, script IFPs and hit reactions retain their native weight.
static int nativeSlot(int group, int id,int modelGroup=-1) {
    if (group != 0 && (group < 118 || (group > 138 && group!=modelGroup))) return -1;
    if (id >= Walk && id <= Idle) return id;
    if (id == 5) return WalkStart;
    if (group == 0 && id == 6) return RunStopL;
    if (group == 0 && id == 7) return RunStopR;
    return -1;
}
static bool identify(void* clump, NativeMotion& native,int modelGroup=-1) {
    void* a = reinterpret_cast<void*(__cdecl*)(void*)>(0x4D15E0)(clump);
    unsigned traversed = 0;
    while (a && traversed++ < 32) {
        const float weight = at<float>(a, 0x18);
        if (!std::isfinite(weight)) return false;
        if (weight > .001f) {
            const int id = at<short>(a, 0x2C), group = at<short>(a, 0x0E);
            // In both installed ped.ifp dictionaries FACSURP..FACGUM key
            // only jaw/eyebrows (6, 7, 8), all preserved as native helpers.
            const bool facial = group == 0 && id >= 154 && id <= 161;
            if (!facial && (at<unsigned short>(a, 0x2E) & 0x10)) native.partial = true;
            else if (!facial) {
                native.baseWeight += weight;
                const int slot = nativeSlot(group, id,modelGroup);
                if (slot >= 0) {
                    auto* h = at<void*>(a, 0x14);
                    if (!h) return false;
                    const float seconds = at<float>(h, 0x10), t = at<float>(a, 0x20);
                    if (!(seconds > .001f && seconds < 120) || !std::isfinite(t)) return false;
                    Layer& layer = native.layers[slot];
                    if (weight >= layer.weight) layer.phase = slot < WalkStart ? wrap(t / seconds) :
                        std::clamp(t / seconds, 0.f, 1.f);
                    layer.weight += weight;
                }
            }
        }
        a = reinterpret_cast<void*(__cdecl*)(void*)>(0x4D6AB0)(a);
    }
    return !a && native.baseWeight > .001f;
}

struct Frame {
    void* ped;
    void* clump;
    Hierarchy* h;
    int ref, model;
    unsigned now;
    V pos;
    bool jogger, longGun;
    float heading = 0;
    const M* entity = nullptr;
};
// The mover distance in IV is calibrated to its own legs. Read the length
// of the actual streamed ped, including mesh and entity scale, only on entry.
static float actorLegScale(const Binding& binding, const M* bones, float donorLength) {
    if (donorLength <= 0) return 1.f; // NG01/02 did not carry this measurement
    float sum = 0;
    int segments = 0;
    for (int i = 0; i < binding.count; ++i) {
        const int id = binding.ids[i];
        if (id != 42 && id != 43 && id != 52 && id != 53) continue;
        const int parent = binding.parents[i];
        if (parent < 0 || binding.ids[parent] != id-1) return 0;
        const float segment = length(bones[i].pos - bones[parent].pos);
        if (!std::isfinite(segment) || segment <= 1.e-5f) return 0;
        sum += segment;
        ++segments;
    }
    return segments == 4 ? .5f * sum / donorLength : 0;
}

static bool drawPose(const Frame& frame, const Set& set, const NativeMotion& native) {
    const auto& [ped, clump, h, ref, model, now, pos, jogger, longGun, heading, entity] = frame;
    if (!finite(pos) || !std::isfinite(heading)) { release(ref); return false; }
    Actor& actor = actors[ref];
    const bool fresh = actor.ped != ped || actor.model != model || actor.clump != clump ||
        actor.nodes != h->nodes || actor.count != h->num || actor.set != &set ||
        now - actor.lastSeen > 350;
    if (fresh) {
        actor = {};
        actor.ped = ped; actor.model = model; actor.clump = clump;
        actor.nodes = h->nodes; actor.count = h->num; actor.set = &set;
        actor.previous = pos; actor.heading = heading; actor.lastSeen = now;
        actor.binding.init(h->nodes, h->num, bank.tags);
        if(actor.binding.count && bank.legLength>0 && fitFootwear)
            fitFootwear(ped,actor.binding,actor.sole.shape);
        actor.idlePhase = native.layers[Idle].phase;
    }
    const float dt = std::min((now - actor.lastSeen) * .001f, .20f);
    actor.lastSeen = now;
    if (!actor.binding.count) return false;
    if (actor.legScale <= 0) {
        actor.legScale = actorLegScale(actor.binding, h->matrices, bank.legLength);
        if (actor.legScale <= 0) return false;
    }
    const V step = pos - actor.previous;
    const float displacement = std::hypot(step.x, step.y);
    const float yaw = std::remainder(heading - actor.heading, 6.2831853f);
    const float midHeading = heading - .5f * yaw;
    // Project onto the tangent of this stride. A lateral shove or moving
    // backwards while the navigation task survives is not a forward footstep.
    const float forward = std::max(0.f, -step.x*std::sin(midHeading) + step.y*std::cos(midHeading));
    actor.previous = pos;
    const bool resetStride = displacement > std::max(.8f, 12.f * dt);
    if (dt > 0) {
        const float smoothing = dt / (.09f + dt);
        const float rate = !resetStride && forward > .0001f && std::abs(yaw) < 1.4f ? yaw/dt : 0;
        actor.turnRate += (rate - actor.turnRate) * smoothing;
        actor.forwardSpeed += ((resetStride ? 0.f : forward/dt) - actor.forwardSpeed) * smoothing;
    }
    actor.heading = heading;
    std::array<float, CycleCount> weights{};
    float sum = 0, movingSum = 0, strides = 0, physicalWeight = 0;
    int dominant = Walk, dominantClip = Walk;
    for (int i = Walk; i < CycleCount; ++i) {
        const float w = native.layers[i].weight;
        if (w <= 0) continue;
        int source = jogger && i == Walk ? Run : i;
        if (source == Sprint && !set[Sprint].frames) source = Run;
        if (!set[source].frames) continue;
        sum += w;
        weights[source] += w;
        if (i > Sprint) continue;
        if (!movingSum || w > native.layers[dominant].weight) {
            dominant = i;
            dominantClip = source;
        }
        movingSum += w;
        // Positive CPed heading turns from +Y towards -X (left). IV's
        // mover rotation supplies the native angular rate of each curved clip.
        const int curve = WalkTurnL + 2*source + (actor.turnRate < 0 ? 1 : 0);
        const auto& arcClip = set[curve];
        const float angle = arcClip.turnRate * arcClip.duration;
        // Compare path curvature (rad/m). A slower actor following the
        // same bend still needs the same curved step, at a slower cadence.
        const float arc = arcClip.frames && angle > .001f && actor.forwardSpeed > .001f ?
            std::min(1.f, std::abs(actor.turnRate) * arcClip.stride * actor.legScale /
                          (actor.forwardSpeed * angle)) : 0.f;
        weights[source] -= w * arc;
        weights[curve] += w * arc;
        const float stride = set[source].stride * (1-arc) + set[curve].stride * arc;
        if (stride > .05f) { strides += stride * w; physicalWeight += w; }
    }
    if (sum < .01f) { release(ref); return false; }
    if (movingSum > .001f) {
        if (!actor.moving || physicalWeight <= .001f || resetStride)
            actor.movementPhase = wrap(native.layers[dominant].phase - set[dominantClip].phaseOrigin);
        else {
            // An idle/walk native crossfade shortens the visible stride as
            // well as slowing the capsule. Account for both in the foot clock.
            const float stride = actor.legScale * strides / (physicalWeight + weights[Idle]);
            actor.movementPhase = wrap(actor.movementPhase + forward / stride);
        }
    }
    actor.moving = movingSum > .001f;
    if (weights[Idle] > 0) actor.idlePhase = wrap(actor.idlePhase + dt / set[Idle].duration);
    // GTA has already positioned the capsule and evaluated root translation.
    // Only visible joint rotation is replaced, inclusive of native crossfades.
    actor.blend = std::min(1.f, actor.blend + dt / .14f);
    const float weight = actor.blend * std::min(1.f, sum / std::max(native.baseWeight, 1.f));
    if (weight <= 0) return false;
    std::array<Q, maxTags> target{}, sampled{};
    T root{};
    float blended = 0;
    for (int i = Walk; i < CycleCount; ++i) {
        const float w = weights[i];
        if (w <= 0) continue;
        const float phase = i >= WalkStart ? native.layers[i].phase :
            (i == Idle ? actor.idlePhase : actor.movementPhase + set[i].phaseOrigin);
        T sampledRoot{};
        sample(set[i], bank.tags.size(), phase, sampled.data(), &sampledRoot);
        if (blended == 0) {
            target = sampled;
            root = sampledRoot;
        } else {
            const float alpha = w / (blended + w);
            for (size_t k = 0; k < bank.tags.size(); ++k)
                target[k] = mix(target[k], sampled[k], alpha);
            root = mix(root, sampledRoot, alpha);
        }
        blended += w;
    }
    M upright{};
    if (!entity)
        write(upright, {{0, 0, std::sin(.5f*heading), std::cos(.5f*heading)}, pos});
    M grounded=entity?*entity:upright;
    auto& sole=actor.sole;
    sole.advance(dt);
    grounded.pos.z+=sole.bias;
    const bool measure=sole.due(now,weight);
    const float nativeBottom=measure?sole.shape.height(h->matrices,pos.z):0.f;
    const bool drawn=apply(actor.binding,target.data(),weight,native.partial || longGun,h->matrices,
                           &grounded,bank.legLength>0?&root:nullptr,actor.legScale);
    if(drawn&&measure)
        sole.observe(nativeBottom,sole.shape.height(h->matrices,pos.z)-sole.bias*weight,actor.legScale,now);
    return drawn;
}
static bool draw(void* ped, int ref, const char* profile, bool jogger = false,int group=-1) {
    if (!profile || ref < 0 || !ped || ped == player()) return false;
    const auto match = bank.sets.find(profile);
    if (match == bank.sets.end()) { release(ref); return false; }
    void* clump = at<void*>(ped, 0x18);
    auto* h = clump ? hierarchy(ped) : nullptr;
    if (!h || !h->nodes || !h->matrices) { release(ref); return false; }
    NativeMotion native;
    if (!identify(clump, native,group)) { release(ref); return false; }
    // Long-gun carry and partial overlay tasks preserve the evaluated hand,
    // weapon, head and torso relationship in world space.
    const auto slot = at<unsigned char>(ped, 0x718);
    const int weapon = slot < 13 ? at<int>(ped, 0x5A0 + slot * 0x1C) : -1;
    const bool longGun = (weapon >= 25 && weapon <= 27) || (weapon >= 30 && weapon <= 38);
    const bool drawn=drawPose({ped, clump, h, ref, at<short>(ped, 0x22), gameTime(), position(ped), jogger, longGun,
                    at<float>(ped, 0x558), matrix(ped)},
                    match->second, native);
    if(drawn){
        auto& a=actors.at(ref);
        if(!a.mover){auto m=movers.find(profile);if(m!=movers.end())a.mover=&m->second;}
        a.jogger=jogger;
        a.modelGroup=group;
    }
    return drawn;
}
}
