#pragma once
#include "Math.hpp"
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace npc_gait {
inline constexpr int maxTags = 32, maxNodes = 128;
enum Cycle { Walk, Run, Sprint, Idle, WalkTurnL, WalkTurnR, RunTurnL, RunTurnR,
             SprintTurnL, SprintTurnR, WalkStart, RunStopL, RunStopR, CycleCount };
inline constexpr std::array<const char*, CycleCount> cycleNames{
    "walk", "run", "sprint", "idle", "walk_turn_l", "walk_turn_r", "run_turn_l", "run_turn_r",
    "sprint_turn_l", "sprint_turn_r", "wstart", "rstop_l", "rstop_r"
};

struct Clip {
    unsigned frames = 0;
    float duration = 0, stride = 0, turnRate = 0, phaseOrigin = 0;
    bool loop = true;
    std::vector<Q> keys;
    std::vector<T> roots;
};
using Set = std::array<Clip, CycleCount>;
struct Bank {
    std::vector<int16_t> tags;
    std::map<std::string, Set, std::less<>> sets;
    float legLength = 0;
};

// The on-disk format is packed little endian.
inline bool readBank(std::istream& in, Bank& result) {
    auto read = [&](void* dst, size_t size) {
        return bool(in.read(static_cast<char*>(dst), std::streamsize(size)));
    };
    char signature[4];
    uint16_t count = 0, tags = 0;
    if (!read(signature, 4)) return false;
    const auto version = std::string(signature, 4);
    const bool hasRoot = version == "NG03";
    if ((!hasRoot && version != "NG01" && version != "NG02") ||
        !read(&count, 2) || !read(&tags, 2) || !count || count > 512 ||
        !tags || tags > maxTags) return false;
    Bank next;
    next.tags.resize(tags);
    if (!read(next.tags.data(), tags * sizeof(int16_t))) return false;
    if (hasRoot && (!read(&next.legLength,4) || !(next.legLength>.3f && next.legLength<2.f))) return false;
    for (size_t i = 0; i < tags; ++i)
        if (next.tags[i] < 1 || std::find(next.tags.begin(), next.tags.begin() + i, next.tags[i]) != next.tags.begin() + i)
            return false;
    size_t keyCount = 0;
    for (unsigned i = 0; i < count; ++i) {
        std::array<char, 48> name{};
        uint16_t frames = 0;
        Clip clip;
        if (!read(name.data(), name.size()) || !read(&frames, 2) ||
            !read(&clip.duration, 4) || !read(&clip.stride, 4) ||
            (version != "NG01" && !read(&clip.turnRate, 4)) ||
            (hasRoot && !read(&clip.phaseOrigin,4))) return false;
        const auto end = std::find(name.begin(), name.end(), '\0');
        const auto slash = std::find(name.begin(), end, '/');
        if (end == name.end() || slash == end || slash == name.begin() ||
            frames < 2 || frames > 1024 || !(clip.duration > 0 && clip.duration < 120) ||
            !(clip.stride >= 0 && clip.stride < 100) ||
            !(clip.turnRate >= 0 && clip.turnRate < 50) ||
            !(clip.phaseOrigin >= 0 && clip.phaseOrigin <= 1)) return false;
        const std::string suffix(slash + 1, end);
        const auto key = std::find(cycleNames.begin(), cycleNames.end(), suffix);
        if (key == cycleNames.end()) return false;
        const auto index = key - cycleNames.begin();
        clip.loop = index < WalkStart;
        clip.frames = frames;
        keyCount += size_t(frames) * tags;
        if (keyCount > 4000000) return false;
        clip.keys.resize(size_t(frames) * tags);
        static_assert(sizeof(Q) == 16);
        if (!read(clip.keys.data(), clip.keys.size() * sizeof(Q))) return false;
        for (Q& q : clip.keys) {
            const float norm = q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w;
            if (!(norm > .98f && norm < 1.02f)) return false;
            q = unit(q);
        }
        if (hasRoot) {
            clip.roots.resize(frames);
            static_assert(sizeof(T)==28 && sizeof(V)==12);
            if (!read(clip.roots.data(),frames*sizeof(T))) return false;
            for (T& root:clip.roots) {
                const Q q=root.q;
                const float n=q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w;
                if (!(n>.98f && n<1.02f) || !finite(root.p) || length(root.p)>2.f) return false;
                root.q=unit(q);
            }
        }
        auto& slot = next.sets[std::string(name.begin(), slash)][index];
        if (slot.frames) return false;
        slot = std::move(clip);
    }
    if (in.peek() != std::char_traits<char>::eof()) return false;
    result = std::move(next);
    return true;
}

inline float wrap(float phase) { return phase - std::floor(phase); }
inline void sample(const Clip& clip, size_t tags, float phase, Q* out, T* root=nullptr) {
    const float frame = (clip.loop ? wrap(phase) : std::clamp(phase, 0.f, 1.f)) * (clip.frames - 1);
    const size_t a = std::min(size_t(frame), size_t(clip.frames - 2));
    const Q* start = clip.keys.data() + a * tags;
    for (size_t k = 0; k < tags; ++k) out[k] = mix(start[k], start[tags + k], frame - float(a));
    if (root) *root=clip.roots.empty()?T{}:mix(clip.roots[a],clip.roots[a+1],frame-float(a));
}

struct Binding {
    std::array<int, maxNodes> parents{}, tracks{}, ids{};
    int pelvis=-1;
    std::array<std::array<int,3>,2> legs;
    int count = 0;
    template<class NodeType>
    bool init(const NodeType* nodes, int n, const std::vector<int16_t>& tags) {
        count = 0;
        pelvis=-1;
        for(auto& leg:legs) leg.fill(-1);
        if (!nodes || n < 16 || n > maxNodes) return false;
        int parent = -1, depth = 1;
        std::array<int, maxNodes + 2> stack{};
        stack[0] = -1;
        uint32_t found = 0;
        for (int i = 0; i < n; ++i) {
            ids[i] = nodes[i].id;
            if(ids[i]==1) pelvis=i;
            for(int side=0;side<2;side++){
                int segment=ids[i]-(41+side*10);
                if(segment>=0 && segment<3)legs[side][segment]=i;
            }
            parents[i] = parent;
            const auto tag = std::find(tags.begin(), tags.end(), ids[i]);
            tracks[i] = tag == tags.end() ? -1 : int(tag - tags.begin());
            if (tracks[i] >= 0) {
                const uint32_t bit = uint32_t(1) << tracks[i];
                if (found & bit || parent < 0) return false;
                found |= bit;
            }
            if (nodes[i].flags & 2) stack[depth++] = parent;
            parent = i;
            if (nodes[i].flags & 1) {
                if (!depth) return false;
                parent = stack[--depth];
            }
        }
        // Extra facial/clothing nodes and missing toes are permitted. Do not
        // animate an unrelated streamed hierarchy that happens to share a tag.
        for (int required : {1, 2, 3, 5, 41, 42, 43, 51, 52, 53, 22, 23, 32, 33})
            if (std::find(ids.begin(), ids.begin() + n, required) == ids.begin() + n) return false;
        count = n;
        return true;
    }
};

inline Q turnSegment(V from,V to){
    const V a=normalized(from),b=normalized(to);
    const float d=std::clamp(dot(a,b),-1.f,1.f);
    if(d<-.99999f){
        const V axis=normalized(cross(a,std::abs(a.x)<.8f?V{1,0,0}:V{0,1,0}));
        return {axis.x,axis.y,axis.z,0};
    }
    const V axis=cross(a,b);
    return unit({axis.x,axis.y,axis.z,1+d});
}

// With both hands holding a gun, SA's upper body and hip sockets must stay
// where the native task put them. Reach for IV's ankle from those sockets;
// the IV knee supplies the bend plane. A target outside reach never extends
// a segment or pulls a shoulder away from its prop.
inline void fitMaskedFeet(const Binding& binding,const Q* targets,float weight,const M* native,
                          const M* inverses,M* output,M authoredRoot){
    const int pelvis=binding.pelvis;
    if(pelvis<0 || binding.parents[pelvis]<0 || binding.tracks[pelvis]<0)return;
    auto animatedChild=[&](const M& parent,int node){
        const int p=binding.parents[node];
        M local=affineProduct(inverses[p],native[node]);
        local.pos=vector(inverses[p],native[node].pos-native[p].pos);
        const Q delta=mul(targets[binding.tracks[node]],conjugate(rotation(local)));
        local.right=rotate(delta,local.right);local.up=rotate(delta,local.up);local.at=rotate(delta,local.at);
        return affineProduct(parent,local);
    };
    const M authoredPelvis=animatedChild(authoredRoot,pelvis);
    for(const auto& leg:binding.legs){
        const int hip=leg[0],knee=leg[1],ankle=leg[2];
        if(hip<0||knee<0||ankle<0||binding.parents[hip]!=pelvis||binding.parents[knee]!=hip||
           binding.parents[ankle]!=knee||binding.tracks[hip]<0||binding.tracks[knee]<0||binding.tracks[ankle]<0)continue;
        const M kneeGoal=animatedChild(animatedChild(authoredPelvis,hip),knee);
        const V ankleGoal=animatedChild(kneeGoal,ankle).pos;
        const V h=output[hip].pos,k=output[knee].pos,f=output[ankle].pos;
        const float upper=length(k-h),lower=length(f-k);
        const V toGoal=native[ankle].pos+(ankleGoal-native[ankle].pos)*weight-h;
        const float reach=length(toGoal);
        if(upper<.005f||lower<.005f||reach<.005f)continue;
        const V direction=toGoal*(1.f/reach);
        const float d=std::clamp(reach,std::abs(upper-lower)+.0001f,upper+lower-.0001f);
        const float along=(upper*upper-lower*lower+d*d)/(2.f*d);
        const V pole=native[knee].pos+(kneeGoal.pos-native[knee].pos)*weight-h;
        V across=pole-direction*dot(pole,direction);
        if(length(across)<.0001f)across=(k-h)-direction*dot(k-h,direction);
        if(length(across)<.0001f)continue;
        const V nextKnee=h+direction*along+normalized(across)*std::sqrt(std::max(0.f,upper*upper-along*along));
        const V nextFoot=h+direction*d;
        const Q turn[3]={turnSegment(k-h,nextKnee-h),turnSegment(f-k,nextFoot-nextKnee),Q{}};
        const V before[3]={h,k,f},after[3]={h,nextKnee,nextFoot};
        // Keep toe orientation and any helpers that are children of a leg.
        std::array<unsigned char,maxNodes> section{};
        for(int i=hip;i<binding.count;i++){
            const int parent=binding.parents[i];
            const unsigned char role=i==hip?1:i==knee?2:i==ankle?3:parent>=0?section[parent]:0;
            section[i]=role;
            if(!role)continue;
            M& bone=output[i];
            bone.pos=after[role-1]+rotate(turn[role-1],bone.pos-before[role-1]);
            bone.right=rotate(turn[role-1],bone.right);bone.up=rotate(turn[role-1],bone.up);
            bone.at=rotate(turn[role-1],bone.at);
        }
    }
}

// Replace the rotation in each mapped LOCAL frame, retaining the game's
// current offsets, scale, and residual shear. Helpers retain their entire
// local frame. No palette is committed unless every transform is valid.
inline bool apply(const Binding& binding, const Q* targets, float weight,
                  bool legsOnly, M* palette, const M* entity=nullptr,
                  const T* root=nullptr, float legScale=1.f) {
    std::array<M, maxNodes> out, inverses;
    std::array<bool, maxNodes> changed{};
    Q authoredRoot{}, authoredPelvis{};
    const bool actorRoot=entity&&root;
    if (actorRoot) authoredRoot=mul(rotation(*entity),root->q);
    for (int i = 0; i < binding.count; ++i) {
        const M& native = palette[i];
        if (!finite(native.pos) || !affineInverse(native, inverses[i])) return false;
        const int parent = binding.parents[i], id = binding.ids[i];
        if (parent < 0) {
            out[i] = native;
            if (actorRoot && !legsOnly) {
                const Q old=rotation(native);
                const Q correction=mul(mix(old,authoredRoot,weight),conjugate(old));
                out[i].right=rotate(correction,native.right);
                out[i].up=rotate(correction,native.up);
                out[i].at=rotate(correction,native.at);
                // legScale is measured from this live palette in world metres.
                const V origin=entity->pos+rotate(rotation(*entity),root->p*legScale);
                out[i].pos=native.pos+(origin-native.pos)*weight;
                if (!finite(out[i].pos)) return false;
                changed[i]=true;
            }
            continue;
        }
        const int track = binding.tracks[i];
        if (actorRoot && id==1 && track>=0)
            authoredPelvis=mul(authoredRoot,targets[track]);
        const bool leg = (id >= 41 && id <= 44) || (id >= 51 && id <= 54);
        const bool driven = track >= 0 && (!legsOnly || leg);
        changed[i] = changed[parent] || driven;
        if (!changed[i]) {
            out[i] = native;
            continue;
        }
        M local = affineProduct(inverses[parent], native);
        // Subtract world coordinates before rotating to avoid cancellation
        // when the ped is kilometres away from the map origin.
        local.pos = vector(inverses[parent], native.pos - palette[parent].pos);
        if (driven) {
            const Q old = rotation(local);
            Q wanted = targets[track];
            if (actorRoot && legsOnly && (id==41 || id==51) && binding.ids[parent]==1) {
                // Native gun/prop upper is locked. Express each IV thigh in
                // that pelvis's actual frame rather than replaying it under a
                // pelvis with a different orientation and animation clock.
                wanted=mul(conjugate(rotation(palette[parent])),mul(authoredPelvis,wanted));
            }
            const Q correction = mul(mix(old, wanted, weight), conjugate(old));
            local.right = rotate(correction, local.right);
            local.up = rotate(correction, local.up);
            local.at = rotate(correction, local.at);
        }
        out[i] = affineProduct(out[parent], local);
        if (!finite(out[i].pos) || !finite(out[i].right) || !finite(out[i].up) || !finite(out[i].at))
            return false;
    }
    if(actorRoot && legsOnly){
        M basis=palette[0];
        const Q correct=mul(authoredRoot,conjugate(rotation(basis)));
        basis.right=rotate(correct,basis.right);basis.up=rotate(correct,basis.up);basis.at=rotate(correct,basis.at);
        basis.pos=entity->pos+rotate(rotation(*entity),root->p*legScale);
        fitMaskedFeet(binding,targets,weight,palette,inverses.data(),out.data(),basis);
    }
    std::copy_n(out.begin(), binding.count, palette);
    return true;
}
}
