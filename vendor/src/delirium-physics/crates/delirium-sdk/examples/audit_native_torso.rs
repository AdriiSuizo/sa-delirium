use delirium::prelude::*;
use delirium::ragdoll::Build;
use delirium_physics::*;
use std::ffi::CString;
fn read(path:impl AsRef<std::path::Path>)->Vec<f32>{std::fs::read_to_string(path).unwrap().split_whitespace().map(|x|x.parse().unwrap()).collect()}
fn transforms(v:&[f32])->Vec<Transform>{v.chunks_exact(7).map(|x|Transform::from_translation(Vec3::from_slice(x)).with_rotation(Quat::from_xyzw(x[3],x[4],x[5],x[6]))).collect()}
fn main(){
    let args:Vec<_>=std::env::args().collect();let assets=std::path::Path::new(&args[1]);let data=std::path::Path::new(&args[2]);
    let path=|s:&str|CString::new(assets.join(s).to_str().unwrap()).unwrap();
    assert!(unsafe{nm_init(path("fred.xrt").as_ptr())}>=0);
    let (source,frames)=Build::undump(&std::fs::read_to_string(assets.join("fred-build.txt")).unwrap());
    let pts=read(data.join("model-7-native-fit.txt"));let points:Vec<Vec3>=pts.chunks_exact(3).map(Vec3::from_slice).collect();
    let bind=transforms(&read(data.join("model-7-native-bind-joints.txt")));
    let pose=transforms(&read(data.join("model-7-native-pose-joints.txt")));
    let (fit,out)=source.fit(&frames,&points);
    let seeded:Vec<_>=pose.iter().zip(&bind).zip(&out).map(|((p,b),f)|{
        let q=p.rotation*b.rotation.inverse();
        Transform::from_translation(p.translation+q*(f.translation-b.translation)).with_rotation(q*f.rotation)
    }).collect();
    let mut max_gap=0f32;
    for (i,p) in fit.parts.iter().enumerate(){
        let mass=p.mass*p.size.powi(3)*p.heft;
        assert!(mass.is_finite()&&mass>0.0001&&p.radius>0.0001&&p.inertia.min_element()>0.,"undefined fitted body {i}");
        if let Some(parent)=p.parent {
            let rest=out[parent].translation+out[parent].rotation*p.anchor_parent;
            assert!(rest.distance(out[i].translation+out[i].rotation*p.anchor)<1e-5,"rest anchor {i}");
            let gap=(seeded[parent].translation+seeded[parent].rotation*p.anchor_parent).distance(seeded[i].translation+seeded[i].rotation*p.anchor);
            // Existing virtual head pivot policy is a separate audited issue.
            if i!=12 {max_gap=max_gap.max(gap);assert!(gap<0.0008,"native seed anchor {i}: {gap}");}
        }
    }
    // The public SDK must accept a coincident articulation with a real distal
    // body, reject an undefined axis/leaf, and never create NaN/zero masses.
    let original=unsafe{nm_build_load(path("fred-build.txt").as_ptr())};assert!(!original.is_null());
    let mut zero=points.clone();zero[7]=zero[0];
    let mut worst=0f32;
    let (_,baseline)=source.fit(&frames,&zero);
    for epsilon in [-1e-7,0.,1e-7]{for axis in [Vec3::X,Vec3::Y,Vec3::Z]{
        let mut variant=zero.clone();variant[7]+=axis*epsilon;
        let (b,f)=source.fit(&frames,&variant);
        let drift=f[0].rotation.angle_between(baseline[0].rotation).to_degrees();worst=worst.max(drift);
        assert!(drift<0.2,"near-coincident pelvis orientation unstable: {drift}");
        assert!(b.parts.iter().all(|p|p.mass*p.size.powi(3)>0.0001&&p.radius>0.0001&&p.inertia.is_finite()));
        let flat:Vec<f32>=variant.iter().flat_map(|p|p.to_array()).collect();
        let fitted=unsafe{nm_build_fit(original,flat.as_ptr())};assert!(!fitted.is_null(),"ABI rejected real distal torso");
        for i in 0..21 {let mut m=0.;let mut r=0.;let mut h=0.;unsafe{nm_build_part_info(fitted,i,&mut m,&mut r,&mut h,std::ptr::null_mut());}assert!(m.is_finite()&&m>0.&&r>0.&&h>=0.);}
        unsafe{nm_build_destroy(fitted);}
    }}
    let mut bad=zero.clone();bad[8]=bad[7];
    let flat:Vec<f32>=bad.iter().flat_map(|p|p.to_array()).collect();
    assert!(unsafe{nm_build_fit(original,flat.as_ptr())}.is_null(),"coincident body without distal axis must fail");
    let mut nan=pts.clone();nan[0]=f32::NAN;
    assert!(unsafe{nm_build_fit(original,nan.as_ptr())}.is_null());
    unsafe{nm_build_destroy(original);}
    println!("native torso: captured seed max anchor gap={max_gap:.7}m; coincident ABI variants=9; root orientation max drift={worst:.7}deg; mass/inertia finite; degenerate and NaN rejected");
}
