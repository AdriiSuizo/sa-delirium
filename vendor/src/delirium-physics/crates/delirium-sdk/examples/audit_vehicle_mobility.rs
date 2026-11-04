//! Isolated diagnostic, not a gameplay test or production coupling change.
//! Same free bodies against one shared finite car, versus today's immutable
//! obstacle snapshot followed by applying its recorded impulses to that car.
use delirium::prelude::*;
use delirium::ragdoll::{Build,Drive,Group,Member,Obstacle,ObPart,Ragdoll};
use delirium::collision::Collision;
use serde_json::{json,Value};

fn build(source:&Build,mass:f32,half:Option<Vec3>)->Build{
    let mut p=source.parts[16].clone();p.parent=None;p.bone=0;p.mass=mass;
    p.size=1.;p.heft=1.;p.grown=1.;p.radius=0.2;p.half=0.;p.boxed=half;
    p.anchor=Vec3::ZERO;p.anchor_parent=Vec3::ZERO;p.offset=Transform::IDENTITY;
    p.bind=Quat::IDENTITY;p.rest=Quat::IDENTITY;p.name="DiagnosticBody".into();
    p.inertia=half.map_or(Vec3::splat(0.4*mass*0.2*0.2),|h|Vec3::new(h.y*h.y+h.z*h.z,h.x*h.x+h.z*h.z,h.x*h.x+h.y*h.y)*(mass/3.));
    Build{parts:vec![p],nm:source.nm}
}
fn kinetic(b:&Build,v:Vec3,w:Vec3)->f32{0.5*b.parts[0].mass*v.length_squared()+0.5*(b.parts[0].inertia*w).dot(w)}
fn substep_feedback(source:&Build,dt:f32)->Value{
    let steps=(dt*delirium_data::kv!(80)*delirium::ragdoll::SUBSTEPS as f32).round().clamp(1.,delirium_data::kv!(61)) as usize;
    let half=Vec3::new(0.8,1.8,0.65);let at=Vec3::Z*15.;let b=build(source,80.,None);
    let mut d=Ragdoll::new(&b,&[Transform::from_translation(at+Vec3::X*0.98)],Vec3::ZERO);
    let parts=Box::leak(vec![ObPart{shape:delirium::gjk::Shape::Box(half,0.),m:Mat3::IDENTITY,at:Vec3::ZERO,lo:-half,hi:half,mesh:None,filter:Default::default()}].into_boxed_slice());
    let ob=[Obstacle{id:47,at,centre:at,com:at,half,vel:Vec3::X*4.5,parts,..Default::default()}];
    let drive=Drive{lift:1.,..Default::default()};let world=Collision::default();let mut sum=Vec3::ZERO;let mut records=vec![];
    // This reproduces Agent::step_group's repeated Ragdoll calls without
    // draining, and keeps its immutable obstacle snapshot across substeps.
    for _ in 0..steps {d.step(&b,&world,dt/steps as f32,drive,&ob,&[]);let j=d.ob_hits.iter().map(|x|x.1).sum::<Vec3>();sum+=j;records.push(j.to_array());}
    let available=d.ob_hits.iter().map(|x|x.1).sum::<Vec3>();
    assert!(sum.length()>1.,"fixture must collide");
    if steps==1{assert!(available.distance(sum)<1e-6);}else{assert!(available.length()<sum.length()*0.9,"multi-substep fixture must expose discarded earlier feedback");}
    json!({"dt":dt,"internal_substeps":steps,"per_substep_impulse":records,"correct_accumulated_impulse":sum.to_array(),"available_after_agent_loop":available.to_array(),"lost_impulse":(sum-available).to_array(),"scope":"Current SA fixed60Hz path has ONE internal step and loses none here; multi-step callers have latent loss. Reproduces documented loop/clear ordering, not a game capture."})
}
fn run(source:&Build,car_mass:f32,ped_mass:f32,count:usize,eccentric:bool,finite:bool,reverse:bool)->Value{
    let h=1./600.;let velocity_only=std::env::var("NM_VELONLY").is_ok();
    // Existing diagnostic exits middle() before damping and split correction.
    let damp=if velocity_only {1.}else{1.-delirium_data::kv!(73)*h};
    let half=Vec3::new(0.8,1.8,0.65);let car_at=Vec3::new(0.,0.,15.);let speed=Vec3::X*4.5;
    let car_build=build(source,car_mass,Some(half));let ped_build=build(source,ped_mass,None);
    let positions:Vec<_>=(0..count).map(|i|car_at+Vec3::new(half.x+0.18,if count==1{if eccentric{0.9}else{0.}}else{(i as f32-(count-1) as f32/2.)*0.5},if eccentric{0.4}else{0.})).collect();
    let mut dolls:Vec<_>=positions.iter().map(|&p|Ragdoll::new(&ped_build,&[Transform::from_translation(p)],Vec3::ZERO)).collect();
    let geometry=Box::leak(vec![ObPart{shape:delirium::gjk::Shape::Box(half,0.),m:Mat3::IDENTITY,at:Vec3::ZERO,lo:-half,hi:half,mesh:None,filter:Default::default()}].into_boxed_slice());
    let obstacle=Obstacle{id:47,at:car_at,centre:car_at,com:car_at,half,vel:speed,parts:geometry,..Default::default()};
    let obstacles=[obstacle];let world=Collision::default();let drive=Drive{lift:1.,..Default::default()};let mut group=Group::default();
    let (mut car_v,mut car_w)=(speed,Vec3::ZERO);
    let mut impulse=Vec3::ZERO;let mut torque=Vec3::ZERO;let contacts;
    let mut repair_momentum=Vec3::ZERO;let mut car_repair=Vec3::ZERO;
    if finite{
        let mut car=Ragdoll::new(&car_build,&[Transform::from_translation(car_at)],speed);
        let mut members:Vec<_>=dolls.iter_mut().enumerate().map(|(i,d)|Member{doll:d,id:100+i,build:&ped_build,drive,obstacles:&[],others:&[]}).collect();
        members.push(Member{doll:&mut car,id:47,build:&car_build,drive,obstacles:&[],others:&[]});
        if reverse{members.reverse();}
        Ragdoll::step_group(&mut members,&world,h,&mut group);drop(members);
        (car_v,car_w)=car.vel(0);car_v/=damp;car_w/=damp;
        car_repair=car.frames().next().unwrap().translation-car_at-speed*h;
        repair_momentum+=car_w.cross(car_repair)*car_mass;
        impulse=(car_v-speed)*car_mass;torque=car_build.parts[0].inertia*car_w;
        contacts=group.contact_summary()[1] as usize;
    }else{
        let mut members:Vec<_>=dolls.iter_mut().enumerate().map(|(i,d)|Member{doll:d,id:100+i,build:&ped_build,drive,obstacles:&obstacles,others:&[]}).collect();
        if reverse{members.reverse();}
        Ragdoll::step_group(&mut members,&world,h,&mut group);drop(members);
        contacts=dolls.iter().map(|d|d.ob_hits.len()).sum();
        for d in &dolls{for &(_,j,p) in &d.ob_hits{impulse+=j;torque+=(p-car_at).cross(j);}}
        car_v+=impulse/car_mass;car_w+=torque/car_build.parts[0].inertia;
    }
    let mut energy=kinetic(&car_build,car_v,car_w);let mut momentum=car_v*car_mass;
    let mut angular=car_build.parts[0].inertia*car_w;let mut body_v=vec![];
    for (d,&p) in dolls.iter().zip(&positions){let(v,w)=d.vel(0);let(v,w)=(v/damp,w/damp);energy+=kinetic(&ped_build,v,w);momentum+=v*ped_mass;angular+=(p-car_at).cross(v*ped_mass)+ped_build.parts[0].inertia*w;body_v.push(v.to_array());repair_momentum+=w.cross(d.frames().next().unwrap().translation-p)*ped_mass;}
    assert!(contacts>0,"fixture did not make contact");assert!(energy.is_finite());
    json!({"mode":if finite{"shared_finite_island"}else{"kinematic_then_feedback"},"reverse":reverse,"car_mass":car_mass,"ped_mass":ped_mass,"bodies":count,"eccentric":eccentric,"contacts":contacts,"car_impulse":impulse.to_array(),"car_torque_impulse":torque.to_array(),"car_velocity":car_v.to_array(),"car_spin":car_w.to_array(),"body_velocity":body_v,"energy_before":0.5*car_mass*speed.length_squared(),"energy_after":energy,"linear_momentum_error":(momentum-speed*car_mass).length(),"linear_momentum_residual":(momentum-speed*car_mass).to_array(),"root_rebase_momentum_predicted":repair_momentum.to_array(),"car_position_repair":car_repair.to_array(),"angular_momentum_error":angular.length(),"damping_removed":damp,"velocity_only":velocity_only})
}
fn main(){
    let args:Vec<_>=std::env::args().collect();let assets=std::path::Path::new(&args[1]);
    assert!(delirium_data::kv::unlock(&delirium_data::nm_asset::read(&std::fs::read(assets.join("fred.xrt")).unwrap())));
    let(source,_)=Build::undump(&std::fs::read_to_string(assets.join("fred-build.txt")).unwrap());
    let mut rows=vec![];
    for car_mass in [80.,300.,1200.]{for ped_mass in [40.,80.,120.]{for count in [1,4]{for eccentric in [false,true]{for finite in [false,true]{for reverse in [false,true]{rows.push(run(&source,car_mass,ped_mass,count,eccentric,finite,reverse));}}}}}}
    let velocity_only=std::env::var("NM_VELONLY").is_ok();
    let substeps=if velocity_only{vec![]}else{vec![substep_feedback(&source,1./60.),substep_feedback(&source,1./30.)]};
    let report=json!({"status":"isolated_diagnostic_not_gameplay_acceptance","time_step":1./600.,"gravity":false,"motors":false,"external_propulsion":false,"velocity_only":velocity_only,"damping":"existing uniform damping analytically removed from measured velocities; NM_VELONLY skips damping and positional repair","obstacle_restitution":delirium_data::kv!(106)*delirium_data::kv!(73),"finite_peer_restitution":delirium_data::kv!(106)*delirium_data::kv!(106),"substep_feedback":substeps,"limitations":["One physical contact step; no world/tyre constraints or SA integration","Finite reference uses existing Group solver with one rigid car-shaped body; collision material coefficients differ from obstacle path and are not tuned","Contact0.020m overlap seeds a real manifold past the allowed skin; explicit before/after position repair diagnostic exposes any coupling to physical velocities"],"cases":rows});
    std::fs::write(&args[2],serde_json::to_string_pretty(&report).unwrap()).unwrap();println!("vehicle mobility diagnostic: {} measured cases",rows.len());
}
