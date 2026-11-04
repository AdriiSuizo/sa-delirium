//! Read-only handoff audit against the exact registered main vehicle bounds.
//! Uses the same GJK shapes, margins and bound frames as the physical solver;
//! it neither excludes a pair nor changes a part's position or velocity.
use super::*;
use delirium::gjk::{self, Pen, Shape};

/// Per-part output rows (8 floats): deepest primitive index, signed gap,
/// point on the body xyz, contact normal xyz. Negative gap means overlap.
/// `frame` is source vehicle shape xyz+quaternion xyzw. `parts` must exactly
/// match the fitted build. Primitive -1 means no GJK answer (not a clear body).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_agent_vehicle_overlap(a:*const NmAgent,shape:i32,frame:*const f32,out:*mut f32,parts:i32)->i32 {
    let a=agent_ref!(a);
    if shape<0||frame.is_null()||out.is_null()||parts as usize!=a.build.parts.len(){return fail(NM_E_ARG,"invalid vehicle overlap arguments")}
    let row=unsafe{std::slice::from_raw_parts(frame,7)};
    if row.iter().any(|x|!x.is_finite()){return fail(NM_E_ARG,"nonfinite vehicle overlap frame")}
    let position=Vec3::from_slice(row);let q=Quat::from_xyzw(row[3],row[4],row[5],row[6]);
    if (q.length_squared()-1.).abs()>0.01{return fail(NM_E_ARG,"invalid vehicle overlap rotation")}
    let rot=Mat3::from_quat(q);
    let cache=VEHICLE_SHAPES.lock().unwrap_or_else(|e|e.into_inner());
    let Some((_,primitives))=cache.get(shape as usize) else{return fail(NM_E_ARG,"unknown overlap vehicle shape")};
    let margin=delirium_data::kv!(48);
    let mut overlaps=0;
    for (k,(p,t)) in a.build.parts.iter().zip(a.agent.doll.capsules()).enumerate(){
        let body=match p.boxed{Some(e)=>Shape::Box(e-Vec3::splat(margin),margin),None=>Shape::Capsule(p.half,p.radius)};
        let mut best:Option<(usize,gjk::Closest)>=None;
        for (c,v) in primitives.iter().enumerate(){
            // Registered vehicle primitives use the convex-pair penetration
            // directions, exactly as ragdoll's car/child contact query.
            // Translate into a nearby common origin, as the solver does, to
            // avoid losing millimetres at San Andreas world coordinates.
            if let Some(mut hit)=gjk::closest(&body,Mat3::from_quat(t.rotation),t.translation-position,&v.shape,rot*v.m,rot*v.at,Vec3::X,f32::MAX.sqrt(),Some(Pen::Sphere)){
                hit.a+=position;hit.b+=position;
                if best.as_ref().is_none_or(|(_,old)|hit.dist<old.dist){best=Some((c,hit));}
            }
        }
        let mut result=[-1.,10000.,0.,0.,0.,0.,0.,0.];
        if let Some((c,h))=best{result=[c as f32,h.dist,h.a.x,h.a.y,h.a.z,h.n.x,h.n.y,h.n.z];if h.dist<0.{overlaps+=1;}}
        unsafe{std::ptr::copy_nonoverlapping(result.as_ptr(),out.add(k*8),8)};
    }
    overlaps
}
