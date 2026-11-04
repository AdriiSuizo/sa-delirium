use super::*;
use crate::external_mobility::{ExternalDrive,ExternalOwner,ExternalPredictor,ExternalError};
fn setup()->(Group,ExternalOwner){
    moving_obstacle_regression::constants();
    let owner=ExternalOwner{id:7,epoch:11,com:Vec3::Z*10.,velocity:Vec3::new(0.3,0.4,0.5),spin:Vec3::new(0.1,0.2,0.3),linear_inverse:Mat3::IDENTITY/25.,angular_inverse:Mat3::IDENTITY/100.};
    let mut group=Group::default();group.stage_external(11,&[owner]).unwrap();
    group.stage_external_geometry(&[Obstacle{id:7,owner:Some(7),at:owner.com,centre:owner.com,com:owner.com,vel:owner.velocity,spin:owner.spin,half:Vec3::splat(0.2),..Default::default()}]).unwrap();
    group.stage_external_predictors(11,&[ExternalPredictor{id:7,pending_position:Vec3::ZERO,pending_rotation:Quat::IDENTITY}]).unwrap();
    (group,owner)
}
#[test]fn staged_drive_is_physical_once_and_not_a_contact(){
    let(mut group,owner)=setup();let h=1./120.;
    let drive=ExternalDrive{id:7,impulse:Vec3::new(12.5,-5.,0.),torque:Vec3::Z*10.};
    group.stage_external_drives(11,&[drive,drive]).unwrap();group.stage_external_drives(11,&[drive]).unwrap();
    assert_eq!(group.external.owners[&7],owner,"staging must not rewrite native baseline velocity");
    Ragdoll::step_group(&mut[],&Collision::default(),h,&mut group);
    let r=group.external_results()[0];let p=group.external_placements()[0];
    assert_eq!(r.impulse,drive.impulse);assert_eq!(r.angular_impulse,drive.torque);
    assert!(r.delta_velocity.distance(Vec3::new(0.5,-0.2,0.))<1e-6);assert!(r.delta_spin.distance(Vec3::Z*0.1)<1e-6);
    // TU8 collider pre-step82972440 integrates velocity at82972650 BEFORE
    // collision/solve; post-step829726A0 applies only positional push. These
    // freshly solved momenta belong to the NEXT prediction, not elapsed h.
    assert!(p.valid);assert_eq!(p.admitted_position,Vec3::ZERO);
    assert_eq!(p.admitted_rotation,Quat::IDENTITY);
    assert!(group.external_contacts().is_empty(),"a motor alone never creates a bump receipt");
    Ragdoll::step_group(&mut[],&Collision::default(),h,&mut group);
    assert_eq!(group.external_results()[0].impulse,drive.impulse,"a second solve must not consume the impulse twice");
    assert_eq!(group.external_results()[0].angular_impulse,drive.torque);
    assert_eq!(group.external_results()[0].delta_velocity,r.delta_velocity);
    assert_eq!(group.external_results()[0].delta_spin,r.delta_spin);
    assert_eq!(group.external.owners[&7].velocity,owner.velocity+r.delta_velocity);
    assert_eq!(group.external.owners[&7].spin,owner.spin+r.delta_spin);
    assert_eq!(group.stage_external_drives(11,&[drive]),Err(ExternalError::ConsumedDriveEpoch(11)));
    group.stage_external(11,&[owner]).unwrap();
    assert_eq!(group.stage_external_drives(11,&[drive]),Err(ExternalError::ConsumedDriveEpoch(11)),"restaging the same snapshot epoch cannot replay the drive");
    group.stage_external(12,&[ExternalOwner{epoch:12,..owner}]).unwrap();assert!(group.stage_external_drives(12,&[drive]).is_ok());
}
#[test]fn drive_stage_rejects_invalid_partial_or_divergent_input_atomically(){
    let(mut group,_)=setup();let d=ExternalDrive{id:7,impulse:Vec3::X,torque:Vec3::ZERO};
    assert_eq!(group.stage_external_drives(12,&[d]),Err(ExternalError::WrongEpoch(12)));
    assert_eq!(group.stage_external_drives(11,&[d,ExternalDrive{id:99,..d}]),Err(ExternalError::MissingSnapshot(99)));
    assert!(!group.external_drives_staged);assert!(group.external_drives.is_empty());
    assert_eq!(group.stage_external_drives(11,&[ExternalDrive{impulse:Vec3::NAN,..d}]),Err(ExternalError::InvalidDrive(7)));
    group.stage_external_drives(11,&[d]).unwrap();
    assert_eq!(group.stage_external_drives(11,&[ExternalDrive{impulse:Vec3::Y,..d}]),Err(ExternalError::DivergentDrive(7)));
    assert_eq!(group.external_drive(7),d,"a rejected revision is not a partially replaced command");
}
#[test]fn zero_drive_equals_absence_and_creates_no_motor_receipt(){
    let(mut group,_)=setup();group.stage_external_drives(11,&[ExternalDrive{id:7,..Default::default()}]).unwrap();group.stage_external_drives(11,&[]).unwrap();
    Ragdoll::step_group(&mut[],&Collision::default(),1./60.,&mut group);
    assert_eq!(group.external_results()[0].impulse,Vec3::ZERO);assert!(group.external_contacts().is_empty());
    assert_eq!(group.external_placements()[0].admitted_position,Vec3::ZERO);
}
#[test]fn motor_contact_impulse_is_separate_from_motor_and_sees_driven_incoming_velocity(){
    use moving_obstacle_regression::{constants,original_ped_obstacle,original_ped_owner};
    constants();let at=Vec3::Z*10.;let owners=[original_ped_owner(1,5,at,Vec3::ZERO),original_ped_owner(2,5,at+Vec3::X*0.5,Vec3::ZERO)];
    let mut group=Group::default();group.stage_external(5,&owners).unwrap();
    group.stage_external_geometry(&[original_ped_obstacle(1,owners[0].com,Vec3::ZERO),original_ped_obstacle(2,owners[1].com,Vec3::ZERO)]).unwrap();
    let motor=Vec3::X*37.5;group.stage_external_drives(5,&[ExternalDrive{id:1,impulse:motor,torque:Vec3::ZERO}]).unwrap();
    Ragdoll::step_group(&mut[],&Collision::default(),1./60.,&mut group);
    let contacts=group.external_contacts();assert!(!contacts.is_empty());
    assert!(contacts.iter().all(|c|c.a==1&&c.b==2&&c.velocity_a.distance(Vec3::X*1.5)<1e-6&&c.velocity_b==Vec3::ZERO));
    let j=contacts.iter().map(|c|c.impulse).sum::<Vec3>();
    let a=group.external_results().iter().find(|r|r.id==1).unwrap();let b=group.external_results().iter().find(|r|r.id==2).unwrap();
    assert!((a.impulse-motor-j).length()<1e-4);assert!((b.impulse+j).length()<1e-4);
    assert!((a.impulse+b.impulse-motor).length()<1e-4,"only the actual motor may add system momentum");
}
#[test]fn world_resists_drive_without_overwriting_snapshot_or_hiding_the_drive_in_native_velocity(){
    moving_obstacle_regression::constants();let at=Vec3::Z*2.;let owner=ExternalOwner{id:1,epoch:9,com:at,velocity:Vec3::ZERO,spin:Vec3::ZERO,linear_inverse:Mat3::IDENTITY/300.,angular_inverse:Mat3::ZERO};
    let mut group=Group::default();group.stage_external(9,&[owner]).unwrap();
    group.stage_external_geometry(&[Obstacle{id:1,owner:Some(1),at,centre:at,com:at,half:Vec3::splat(0.5),..Default::default()}]).unwrap();
    group.stage_external_predictors(9,&[ExternalPredictor{id:1,pending_position:Vec3::ZERO,pending_rotation:Quat::IDENTITY}]).unwrap();
    group.stage_external_drives(9,&[ExternalDrive{id:1,impulse:Vec3::X*1200.,torque:Vec3::ZERO}]).unwrap();
    let mut world=Collision::default();world.add(&[[0.55,-4.,-4.],[0.55,-4.,4.],[0.55,4.,4.],[0.55,4.,-4.]],&[[0,1,2],[0,2,3]],&[0,0]);
    Ragdoll::step_group(&mut[],&world,1./30.,&mut group);
    let p=group.external_placements()[0];let r=group.external_results()[0];
    assert!(p.valid,"{p:?}");assert_eq!(p.admitted_position,Vec3::ZERO,"motor changes future momentum, not elapsed placement");
    assert!(r.impulse.x>=0.&&r.impulse.x<1200.,"world must reduce the drive inside the solve: {r:?}");
    assert!(group.external_contacts().is_empty(),"world motor resistance is not a character bump");
}
