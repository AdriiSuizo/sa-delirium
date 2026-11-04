#pragma once
// A completed render palette, after all native/authored/physical publication.
// A pre-frame consumer may use only the consecutive completed predecessor.
struct FinalPedPoseSnapshot {
    int ref=-1,model=-1;void*ped=nullptr;void*clump=nullptr;unsigned frame=0;
    uint64_t serial=0;T root{};V velocity{};std::vector<int> ids;std::vector<T> bones;
};
static std::map<int,FinalPedPoseSnapshot> finalPedPoses;
static uint64_t finalPedPoseSerial=0;
static void recordFinalPedPose(void*p){
    const int ref=reference(p);if(ref<0)return;
    auto*h=hierarchy(p);if(!p||!matrix(p)||!h||!h->nodes||!h->matrices||h->num<20||h->num>128){finalPedPoses.erase(ref);return;}
    FinalPedPoseSnapshot s{ref,at<short>(p,0x22),p,at<void*>(p,0x18),*reinterpret_cast<unsigned*>(0xB7CB4C),++finalPedPoseSerial,transform(*matrix(p))};
    s.velocity=at<V>(p,0x44);if(!finite(s.velocity)){finalPedPoses.erase(ref);return;}
    for(int i=0;i<h->num;i++){const auto t=transform(h->matrices[i]);if(!finite(t.p)){finalPedPoses.erase(ref);return;}
        s.ids.push_back(h->nodes[i].id);s.bones.push_back(t);}
    finalPedPoses[ref]=std::move(s);
}
static const FinalPedPoseSnapshot*previousFinalPedPose(void*p,const std::vector<int>&ids){
    if(!p)return nullptr;const int ref=reference(p);const auto i=finalPedPoses.find(ref);if(i==finalPedPoses.end())return nullptr;
    const auto&s=i->second;const unsigned frame=*reinterpret_cast<unsigned*>(0xB7CB4C);
    return s.ped==p&&pedFromRef(ref)==p&&s.model==at<short>(p,0x22)&&s.clump==at<void*>(p,0x18)&&
        unsigned(frame-s.frame)==1&&s.ids==ids?&s:nullptr;
}
static void clearFinalPedPoses(){finalPedPoses.clear();}
