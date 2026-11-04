#pragma once
// Preserve the completed predecessor before native traffic/death changes it.
// Pose AND velocity share the final publication receipt; a 100ms cache without
// provenance must never masquerade as an end-of-current-frame pose.
static void captureRoadPoseSeeds(){
    for(const auto&[ref,s]:finalPedPoses){
        void*p=pedFromRef(ref);if(p!=s.ped||agents.contains(ref)||!validPed(p,false,false,p==player())||inVehicle(p))continue;
        const auto*completed=previousFinalPedPose(p,s.ids);if(!completed)continue;
        auto*h=hierarchy(p);if(!h||!h->nodes||h->num!=int(s.ids.size()))continue;
        bool same=true;for(int i=0;i<h->num;i++)same&=h->nodes[i].id==s.ids[i];if(!same)continue;
        auto seed=std::make_shared<PoseSeed>();seed->bones=s.bones;seed->deriveParts=true;seed->velocity=s.velocity;
        seed->sampledRoot=s.root;seed->centre=s.root.p;
        seed->poseEpoch={s.frame,s.serial,SeedPhase::EvaluatedEnd,0,0};seed->motionEpoch=seed->poseEpoch;
        beforePedRoot[ref]=s.root.p;beforePedPose[ref]=std::move(seed);
    }
}
