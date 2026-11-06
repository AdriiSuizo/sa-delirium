#pragma once
// One production policy for firearm/melee hits on motorcycles and bicycles.
// Uses the stock seat-release routine, never the stock launch-force routine.
static bool bikeHitPolicy(void* p){
    if(!ridingBike(p))return false;
    const int type=at<int>(at<void*>(p,0x58C),0x594);
    return (type==9&&hitRagdollMotorcycles)||(type==10&&hitRagdollBicycles);
}
static bool queueBikeHitFall(void* p,int weapon,V impulse,V direction,int part){
    if(!bikeHitPolicy(p)||!((weapon>=0&&weapon<=15)||(weapon>=22&&weapon<=34)||weapon==38))return false;
    auto* vehicle=at<void*>(p,0x58C);
    // Preserve the evaluated riding pose before SA replaces the sit animation.
    // Otherwise even zero launch velocity can start from an elevated fall pose.
    const auto pose=captureBikeExitPose(p,vehicle);
    if(!pose){event("bike_hit_pose_unavailable",reference(p),at<short>(p,0x22),"seated_pose_rejected");return false;}
    const V velocity=pose->velocity;
    bikeHitFalls[reference(p)]={vehicleRef(vehicle),velocity,gameTime(),weapon,pose,impulse,direction,part};
    event("bike_hit_fall_queued",reference(p),at<short>(p,0x22),"weapon="+std::to_string(weapon)+" vehicle_model="+std::to_string(at<short>(vehicle,0x22))+
        " part="+std::to_string(part)+" impulse="+std::to_string(length(impulse))+" dir_x="+std::to_string(direction.x)+" dir_y="+std::to_string(direction.y)+" dir_z="+std::to_string(direction.z));
    return true;
}
static void processBikeHitFalls(){
    auto requests=std::move(bikeHitFalls);bikeHitFalls.clear();
    for(const auto&[ref,request]:requests){
        auto* p=pedFromRef(ref);auto* vehicle=vehicleFromRef(request.vehicle);
        if(!p||!vehicle||gameTime()-request.time>500||!validPed(p,true,true))continue;
        if(ridingBike(p)&&at<void*>(p,0x58C)!=vehicle)continue;
        const V before=position(p);
        if(ridingBike(p)){
            // Verified EventKnockOffBike.{h,cpp}: type NONE=0, size 0x3C.
            // SetPedOutCar updates driver/passenger ownership and removes sit
            // animations. CalcForcesAndAnims (which adds upward kicks) is not called.
            alignas(4) unsigned char ev[0x3C]{};V zero{};V velocity=request.velocity;
            reinterpret_cast<void*(__thiscall*)(void*,void*,V*,V*,float,float,unsigned char,unsigned char,int,void*,bool,bool)>(0x4AFCF0)
                (ev,vehicle,&velocity,&zero,0.f,0.f,0,0,int(gameTime()),nullptr,true,true);
            originalBikeOut(ev,p);
            reinterpret_cast<void(__thiscall*)(void*,unsigned)>((*reinterpret_cast<void***>(ev))[0])(ev,0);
        }
        if(inVehicle(p)){event("bike_hit_fall_detach_failed",ref,at<short>(p,0x22));continue;}
        // The native routine can relocate or accelerate a ped. Preserve the hit
        // location and full coherent bike twist, including upward inertia on a
        // ramp. There is no fixed launch or vertical velocity deletion.
        movePhysicalRoot(p,before);at<V>(p,0x44)=request.velocity;
        clearBikeNativeIgnore(p,vehicle);
        // CEventHandler::ComputeKnockOffBikeResponse (0x4BA53E..0x4BA54B), which
        // this path skips: knocked off with no jacker, a bike (CVehicle 0x590 == 9,
        // motorbikes and BMX) loses bOnSideStand (CBike 0x614 bit 0x10) and falls.
        // Only a jack (0x4BA74B) keeps it standing. Script-made bikes start on it.
        const unsigned char stand=at<unsigned char>(vehicle,0x614);
        if(at<int>(vehicle,0x590)==9)at<unsigned char>(vehicle,0x614)&=~0x10;
        pending[ref]={request.impulse,request.direction,gameTime(),request.part,false,Reaction::BikeFall,-1,request.pose};
        bikeEjections++;
        event("bike_hit_full_ragdoll",ref,at<short>(p,0x22),"weapon="+std::to_string(request.weapon)+" initial_z="+std::to_string(before.z)+" velocity_z="+std::to_string(request.velocity.z)+
            " hit_impulse="+std::to_string(length(request.impulse))+" part="+std::to_string(request.part)+" side_stand_before="+std::to_string((stand>>4)&1)+" side_stand_after="+std::to_string((at<unsigned char>(vehicle,0x614)>>4)&1));
    }
}
