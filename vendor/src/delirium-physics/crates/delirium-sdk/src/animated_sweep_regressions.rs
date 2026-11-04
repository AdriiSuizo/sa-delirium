//! Isolated regression survey of production sweep. Reports examples, without
//! changing implementation or accepting a live collision case from this alone.
use delirium::{prelude::*,collision::Collision,ragdoll::{Build,Ragdoll}};
fn source()->(Build,Vec<Transform>){
    let root=std::path::PathBuf::from(std::env::var("NM_TEST_ASSETS").unwrap());
    let asset=delirium_data::nm_asset::read(&std::fs::read(root.join("fred.xrt")).unwrap());delirium_data::kv::unlock(&asset);
    Build::undump(&std::fs::read_to_string(root.join("fred-build.txt")).unwrap())
}
#[test]fn survey_flat_floor_full_pose_horizontal_translation(){
    let(build,base)=source();let mut trapped=vec![];
    for centre in [Vec3::ZERO,Vec3::new(2428.,-1666.,13.)]{
      for orientation in [Quat::IDENTITY,Quat::from_rotation_x(1.2),Quat::from_rotation_y(-1.57)]{
       for overlap in [-0.02f32,-0.005,0.,0.005,0.02]{
        let mut pose:Vec<_>=base.iter().map(|p|Transform{translation:orientation*p.translation,rotation:orientation*p.rotation,..*p}).collect();
        // Conservative floor placement; the survey includes overlap but does
        // not assume an empty ray is sufficient to prove whole-pose clearance.
        let fitted=Ragdoll::new(&build,&pose,Vec3::ZERO);
        let floor=fitted.capsules().zip(&build.parts).map(|(f,p)|{
            let m=Mat3::from_quat(f.rotation);
            let e=p.boxed.unwrap_or(Vec3::new(p.radius,p.half+p.radius,p.radius));
            f.translation.z-(m.x_axis.abs()*e.x+m.y_axis.abs()*e.y+m.z_axis.abs()*e.z).z
        }).fold(f32::INFINITY,f32::min);
        for p in &mut pose{p.translation+=centre-Vec3::Z*(floor+overlap);}
        let doll=Ragdoll::new(&build,&pose,Vec3::ZERO);let mut world=Collision::default();world.add_floor(centre.x,centre.y,centre.z,20.,0);
        for wish in [Vec3::new(0.05,0.,0.),Vec3::new(0.,0.05,0.),Vec3::new(-0.035,-0.035,0.)]{
            let(by,hits)=doll.sweep_pose_translation(&build,&pose,&world,&[],wish);
            if by.distance(wish)>0.001{trapped.push((centre,orientation,overlap,wish,by,hits));}
        }
       }
      }
    }
    println!("whole_pose_flat_floor: {} trapped cases / 90: {trapped:?}",trapped.len());
    assert!(trapped.is_empty(),"flat ground must not trap horizontal travel");
}