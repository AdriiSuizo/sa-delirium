use delirium::prelude::*;
use delirium::ragdoll::Build;
use std::ffi::CString;
fn main(){
    let args:Vec<String>=std::env::args().collect();
    let root=std::path::Path::new(&args[1]);
    let xrt=CString::new(root.join("fred.xrt").to_str().unwrap()).unwrap();
    assert!(unsafe{delirium_physics::nm_init(xrt.as_ptr())}>=0);
    let (build,frames)=Build::undump(&std::fs::read_to_string(root.join("fred-build.txt")).unwrap());
    for model in [0,7]{
        let values:Vec<f32>=std::fs::read_to_string(root.join(format!("model-{model}-fit.txt"))).unwrap().split_whitespace().map(|s|s.parse().unwrap()).collect();
        let points:Vec<Vec3>=values.chunks_exact(3).map(|v|Vec3::new(v[0],v[1],v[2])).collect();
        let (fit,out)=build.fit_skin(&frames,&points);
        let mut worst_pivot=0.0f32;
        for (k,part) in fit.parts.iter().enumerate(){
            let pivot=if part.parent.is_some(){out[k].translation+out[k].rotation*part.anchor}else{out[k].translation};
            let error=pivot.distance(points[k]);worst_pivot=worst_pivot.max(error);
            println!("PIVOT model={model} part={k} error_m={error:.6}");
            assert!(error<1e-4,"physical pivot differs from skin bind joint model={model} part={k}");
            if let Some(parent)=part.parent{
                let parent_pivot=out[parent].translation+out[parent].rotation*part.anchor_parent;
                assert!(parent_pivot.distance(pivot)<1e-4,"joint is open in the fitted rest pose");
            }
        }
        println!("PIVOT_SUMMARY model={model} max_error_m={worst_pivot:.6}");
        let source=delirium::nmrs::joints(&build,delirium::nmrs::ASSET[build.nm].get().unwrap()).unwrap();
        let target=delirium::nmrs::joints(&fit,delirium::nmrs::ASSET[fit.nm].get().unwrap()).unwrap();
        println!("MODEL {model}");
        for (a,b) in source.iter().zip(&target){
            let before=(frames[a.parent].rotation*a.in_parent).inverse()*(frames[a.child].rotation*a.in_child);
            let after=(out[b.parent].rotation*b.in_parent).inverse()*(out[b.child].rotation*b.in_child);
            let delta=before.angle_between(after).to_degrees();
            println!("{} {} joint_retarget_drift_deg={delta:.3} source_angle={:.3} fitted_angle={:.3}",a.child,build.parts[a.child].name,before.angle_between(Quat::IDENTITY).to_degrees(),after.angle_between(Quat::IDENTITY).to_degrees());
            if (7..=13).contains(&a.child) || a.child==17 {
                assert!(delta<30.0,"anatomical torso/neck inversion at {}",a.child);
            }
        }
        // Coincident clavicles must not change the result with round-off noise,
        // body heading or uniform body size. This reproduces the observed CJ bug.
        let mut worst=0.0f32;
        for sign in [-1.0,0.0,1.0] {
            for axis in [Vec3::X,Vec3::Y,Vec3::Z] {
                let mut p=points.clone();
                p[13]+=axis*sign*1e-6;p[17]-=axis*sign*1e-6;
                for yaw in [0.0,1.1,3.14] {
                    let q=Quat::from_rotation_z(yaw);
                    for scale in [0.7,1.0,1.3] {
                        let moved:Vec<Vec3>=p.iter().map(|v| q*(*v*scale)+Vec3::new(2.0,-3.0,0.4)).collect();
                        let (variant,f)=build.fit_skin(&frames,&moved);
                        for (k,part) in variant.parts.iter().enumerate(){
                            let pivot=if part.parent.is_some(){f[k].translation+f[k].rotation*part.anchor}else{f[k].translation};
                            assert!(pivot.distance(moved[k])<1e-4,"fit pivot instability model={model} part={k}");
                        }
                        for k in [10,11,12,13,14,15,17,18,19] {
                            let error=f[k].rotation.angle_between(q*out[k].rotation).to_degrees();
                            worst=worst.max(error);
                            assert!(error<0.2,"unstable fit model={model} part={k} error={error}");
                        }
                    }
                }
            }
        }
        println!("REGRESSION model={model} variations=81 maximum_orientation_error_deg={worst:.6} passed");
    }
}
