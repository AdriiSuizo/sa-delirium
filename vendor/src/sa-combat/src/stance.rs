//! The original four-way melee strafe blend, with its clips' own root travel.
//! Reference: pinned main.rs melee stance, player.rs strafe_velocity/strafe_pose.
use crate::animation::{Bank,Rig,Pose,mover,movement_flags};
use bevy_math::Vec2;
use lsc4_combat::{animblend,strafe,meleeanims};
pub struct Frame{pub pose:Vec<Pose>,pub velocity:Vec2,pub key:String,pub phase:f32,pub flags:u32}
/// Locked melee uses the view-relative stick projected on the actor's rows.
/// Keep the raw controls for action selection; only the mover uses this frame.
pub fn locked_input(stick:Vec2,front:Vec2,heading:f32)->Vec2{
    let front=front.normalize_or_zero();
    let wish=Vec2::new(front.y,-front.x)*stick.x+front*stick.y;
    let wish=wish.clamp_length_max(1.);
    Vec2::new(wish.dot(Vec2::new(heading.cos(),heading.sin())),wish.dot(Vec2::new(-heading.sin(),heading.cos())))*2.
}
pub fn step(bank:&Bank,rig:&Rig,ratios:Vec2,phase:f32,dt:f32)->Result<Frame,String>{
    let rows=if ratios==Vec2::ZERO{vec![(0x0f,1.,1.)]}else{strafe::strafe_clips(ratios.x,ratios.y,3.,false,false,1.)};
    let mut clips=vec![];
    for(id,weight,rate)in rows{
        let m=meleeanims::MOVES.iter().find(|m|m.1[0]==id).ok_or("unknown stance move")?;
        let key=format!("move_melee/{}",m.0).to_ascii_lowercase();
        let a=bank.clips.get(&key).ok_or_else(||format!("missing original stance {key}"))?;
        clips.push((key,a,weight,rate));
    }
    let mut clocks:Vec<_>=clips.iter().map(|(_,a,w,_)|animblend::Player{phase,weight:*w,active:true,loops:true,synced:true,playing:true,..animblend::Player::new(a.duration)}).collect();
    animblend::sync_phases(&mut clocks,dt);let phase=clocks[0].phase;
    let samples:Vec<_>=clips.iter().map(|(key,_,w,_)|(key.as_str(),phase,*w)).collect();
    let pose=bank.retarget_mix(rig,&samples).ok_or("stance blend unavailable")?;
    let velocity=clips.iter().map(|(_,a,w,rate)|(mover(a,1.)-mover(a,0.)).truncate()*(*w**rate/a.duration)).sum();
    let key=clips.iter().max_by(|a,b|a.2.total_cmp(&b.2)).unwrap().0.clone();
    let flags=movement_flags(&key).ok_or("stance flags unavailable")?;
    Ok(Frame{pose,velocity,key,phase,flags})
}
#[cfg(test)]mod tests{
    use super::*;
    #[test]fn locked_stick_follows_camera_at_every_actor_heading(){
        for camera in [0f32,0.4,1.57,-2.7]{for heading in [0f32,0.8,3.14,-2.1]{
            let front=Vec2::new(-camera.sin(),camera.cos());
            for stick in [Vec2::ZERO,Vec2::Y,Vec2::X,Vec2::new(-0.4,0.6),Vec2::ONE]{
                let local=locked_input(stick,front*3.,heading);
                let world=Vec2::from_angle(heading).rotate(local);
                let wanted=(Vec2::new(front.y,-front.x)*stick.x+front*stick.y).clamp_length_max(1.)*2.;
                assert!(world.distance(wanted)<1e-5);
            }
        }}
    }
    #[test]fn diagonal_stance_keeps_both_axes_and_has_no_dominant_clip_snap(){
        let assets=std::path::PathBuf::from(std::env::var("NM_TEST_ASSETS").unwrap());
        assert!(delirium_data::kv::unlock(&delirium_data::nm_asset::read(&std::fs::read(assets.join("fred.xrt")).unwrap())));
        let bank=Bank::load(std::path::Path::new(&std::env::var("NM_TEST_COMBAT").unwrap())).unwrap();
        let rig=Rig::from_json(&std::fs::read(std::env::var("NM_TEST_RIG").unwrap()).unwrap()).unwrap();
        let a=step(&bank,&rig,Vec2::new(0.699,0.701),0.35,1./120.).unwrap();
        let b=step(&bank,&rig,Vec2::new(0.701,0.699),0.35,1./120.).unwrap();
        assert!(a.velocity.x>0.&&a.velocity.y>0.);
        assert!(a.velocity.distance(b.velocity)<0.02);
        for(p,q)in a.pose.iter().zip(b.pose){assert!(p.p.distance(q.p)<0.01);assert!(p.q.dot(q.q).abs()>0.999);}
        let walk=step(&bank,&rig,Vec2::Y,0.35,1./120.).unwrap();
        let run=step(&bank,&rig,Vec2::Y*2.,0.35,1./120.).unwrap();
        assert!(run.velocity.y>walk.velocity.y);assert!(run.key.ends_with("/run"));
        // The original idle mover has sub-micrometre quantization drift.
        assert!(step(&bank,&rig,Vec2::ZERO,0.,1./120.).unwrap().velocity.length()<1e-5);
    }
}
