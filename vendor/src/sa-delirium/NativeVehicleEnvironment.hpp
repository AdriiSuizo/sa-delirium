#pragma once
// The rider's detailed contact mesh and the native vehicle's environment
// bounds have different owners. DFF wheel triangles are not suspension tyres.
// Keep SA's accepted engine/suspension trajectory; constrain only additional
// shared-solver motion against the actual current native rigid COL.
struct NativeEnvironmentGeometry {
    void*entity=nullptr;void*clump=nullptr;const ColModel*col=nullptr;
    unsigned frame=~0u;V centre{},half{};VehicleShape shape;
};
static std::map<int,NativeEnvironmentGeometry> nativeEnvironmentGeometry;
static bool bindNativeVehicleEnvironment(){
    std::vector<uint64_t> ids;std::vector<int> components,shapes;std::vector<float> rows;
    const unsigned frame=*reinterpret_cast<unsigned*>(0xB7CB4C);
    std::erase_if(nativeEnvironmentGeometry,[frame](const auto&v){
        if(vehicleFromRef(v.first)==v.second.entity&&frame-v.second.frame<=120)return false;
        if(v.second.shape.id>=0)nm_vehicle_shape_release(v.second.shape.id);
        return true;
    });
    for(const auto&o:externalHostOwners){
        if(o.animated)continue;
        // Cars also append articulated DFF doors to the peer scene. Their
        // triangles cannot be used to judge a baseline accepted by SA's actual
        // instance COL. Use that same native envelope for EVERY finite car or
        // bike; retain the complete articulated peer scene for body contacts.
        const auto peer=std::find_if(collision->vehicles.begin(),collision->vehicles.end(),[&](const auto&v){return v.ref==o.ref&&v.component==0;});
        if(peer==collision->vehicles.end()){event("native_environment_geometry_rejected",o.ref,o.model,"missing_chassis_identity=1");return false;}
        if(!hostExternalCurrent(o)||!hostNativeEnds.contains(o.id))return false;
        const auto&native=hostNativeEnds.at(o.id);
        auto&geometry=nativeEnvironmentGeometry[o.ref];
        if(geometry.frame!=frame||geometry.entity!=o.entity||geometry.clump!=o.clump){
            auto*col=reinterpret_cast<ColModel*(__thiscall*)(void*)>(0x535300)(o.entity);
            if(!col||!col->data){event("native_environment_geometry_rejected",o.ref,o.model,"missing_actual_col=1");return false;}
            const V centre=(col->box.min+col->box.max)*.5f,half=(col->box.max-col->box.min)*.5f;
            if(!finite(centre)||!finite(half)||std::min({half.x,half.y,half.z})<=0)return false;
            auto primitives=readVehiclePrimitives(col);
            if(primitives.empty()){event("native_environment_geometry_rejected",o.ref,o.model,"empty_actual_col=1");return false;}
            // GetColModel may select an instance COL. SetupSuspensionLines can
            // also change its spheres/bounds in place. Compare content, not
            // model ID or pointer. SDK registration deduplicates exact bytes.
            if(geometry.shape.primitives!=primitives){
                if(geometry.shape.id>=0)nm_vehicle_shape_release(geometry.shape.id);
                const int shape=nm_vehicle_shape_acquire(primitives[0].data(),int(primitives.size()));
                geometry.shape={shape,std::move(primitives)};
                event(shape>=0?"native_environment_geometry":"native_environment_shape_fallback",o.ref,o.model,
                    "shape="+std::to_string(shape)+" primitives="+std::to_string(geometry.shape.primitives.size())+
                    " actual_instance_col=1 suspension_lines_excluded=1 peer_geometry_unchanged=1");
            }
            geometry.entity=o.entity;geometry.clump=o.clump;geometry.col=col;geometry.frame=frame;geometry.centre=centre;geometry.half=half;
        }
        // With an unrepresentable COL, the SDK uses its native bounds for
        // this owner. The remaining scene and peer shapes stay independent.
        const V offset=geometry.centre-(native.infinite?V{}:native.localCom);
        const Q q=o.staged.centre.q;
        const V centre=o.staged.centre.p+rotate(q,offset),com=o.staged.centre.p;
        const V velocity=o.staged.linear+cross(o.staged.angular,centre-com),spin=o.staged.angular,half=geometry.half;
        if(!finite(centre)||!finite(velocity)||!finite(spin))return false;
        ids.push_back(o.id);components.push_back(peer->index);shapes.push_back(std::max(-1,geometry.shape.id));
        rows.insert(rows.end(),{centre.x,centre.y,centre.z,q.x,q.y,q.z,q.w,half.x,half.y,half.z,velocity.x,velocity.y,velocity.z,spin.x,spin.y,spin.z,com.x,com.y,com.z});
    }
    return ok(nm_group_set_external_environment(bodyGroup,externalHostEpoch,ids.data(),components.data(),shapes.data(),rows.data(),int(ids.size())));
}
