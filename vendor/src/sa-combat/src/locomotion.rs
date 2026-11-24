//! On-foot Niko movement. Original transition helpers are generated separately.
//! Host supplies intent and owns world collisions, jumps, vehicles and missions.
use crate::animation::{Bank,Rig,Pose,mover,mover_q,movement_flags};
use delirium_data::anim::Animation;
use bevy_math::Vec2;
use lsc4_combat::{animblend,onfoot,movestart,movestop,moveturn};
use std::collections::HashMap;
struct Clip<'a>{anim:&'a Animation,key:&'a str,speed:f32}
struct Clips<'a>{clips:[Clip<'a>;4],extra:HashMap<&'a str,Clip<'a>>}
fn mover_yaw(a:&Animation,phase:f32)->f32{let q=mover_q(a,phase);2.*q.z.atan2(q.w)}
include!("locomotion_original.rs");
impl<'a> Clips<'a>{
    fn new(bank:&'a Bank)->Result<Self,String>{
        let clip=|key:&'a str|->Result<Clip<'a>,String>{let a=bank.clips.get(key).ok_or_else(||format!("Missing original movement clip {key}"))?;
            Ok(Clip{anim:a,key,speed:(mover(a,1.).y-mover(a,0.).y)/a.duration})};
        let clips=[clip("move_player/idle")?,clip("move_player/walk")?,clip("move_player/run")?,clip("move_player/sprint")?];
        let mut extra=HashMap::new();
        for (key,a) in &bank.clips {extra.insert(key.as_str(),Clip{anim:a,key,speed:(mover(a,1.).y-mover(a,0.).y)/a.duration});}
        for key in STARTS.iter().chain(&STOPS).chain(&TURNS).chain(&SPOTS).chain(&CROUCH_STARTS).chain(&CROUCH_STOPS).chain(&CROUCH_SPOTS).map(|p|p.1).chain(CROUCH){clip(key)?;}
        Ok(Self{clips,extra})
    }
}

#[cfg(test)]mod tests{
 use super::*;
 #[test]fn real_niko_assets_start_stop_turn_crouch_and_reenter_on_cj(){
    let assets=std::path::PathBuf::from(std::env::var("NM_TEST_ASSETS").expect("owned assets"));
    let bytes=std::fs::read(assets.join("fred.xrt")).unwrap();
    assert!(delirium_data::kv::unlock(&delirium_data::nm_asset::read(&bytes)));
    let bank=Bank::load(std::path::Path::new(&std::env::var("NM_TEST_COMBAT").expect("movement assets"))).unwrap();
    let rig=Rig::from_json(&std::fs::read(std::env::var("NM_TEST_RIG").unwrap()).unwrap()).unwrap();
    // The idle mover contains the actor's facing. Treating every gait as a
    // carried-yaw run removes it and turns the visible head across the body.
    let key="move_player/idle";let flags=movement_flags(key).unwrap();
    assert_eq!(flags&0x3000,0);
    let mixed=bank.retarget_mix(&rig,&[(key,0.,1.)]).unwrap();
    let direct=bank.retarget(&rig,key,0.,flags).unwrap();
    for(a,b)in mixed.iter().zip(&direct){assert!(a.p.distance(b.p)<0.0001);assert!(a.q.dot(b.q).abs()>0.99999);}
    let head=rig.ids.iter().position(|&id|id==8).unwrap();
    let forward=(mixed[head].q*rig.bind[head].q.inverse())*bevy_math::Vec3::Y;
    assert!(forward.y>0.9,"idle lost its authored mover yaw: {forward:?}");
    let mut p=Locomotion::new(rig,0.);let mut t=0u32;let mut seen=std::collections::BTreeSet::new();let mut distance=0.;
    for (want,goal,crouch,seconds) in [(0.,0.,false,1.),(1.,0.,false,2.),(2.,0.,false,2.),(3.,0.,false,2.),(2.,3.1,false,3.),(0.,3.1,false,3.),(1.,2.8,true,2.),(2.,1.,true,2.),(0.,1.,true,2.)]{
      for _ in 0..(seconds*120.) as usize{
        t+=8;p.step(&bank,want,goal,p.state()[0],crouch,0,t,1./120.).unwrap();
        seen.insert(p.key.clone());distance+=p.velocity.length()/120.;
        assert!(p.state().iter().all(|x|x.is_finite()));
        for(i,x)in p.pose.iter().enumerate(){assert!(x.p.is_finite()&&x.q.is_finite());
          if let Some(parent)=p.rig.parents[i]{assert!((x.p.distance(p.pose[parent].p)-p.rig.local[i].p.length()).abs()<0.001,"segment {} changed",p.rig.ids[i]);}
        }
      }
    }
    assert!(distance>10.,"mover must actually travel");
    assert!(seen.iter().any(|s|s.contains("start")),"no start: {seen:?}");
    assert!(seen.iter().any(|s|s.contains("stop")),"no stop: {seen:?}");
    assert!(seen.iter().any(|s|s.contains("turn")),"no turn: {seen:?}");
    assert!(seen.iter().any(|s|s.contains("move_crouch")));
    assert!(p.velocity.length()<0.01,"must settle after stopping");
    p.cancel();assert!(!p.active);assert_eq!(p.velocity,Vec2::ZERO);
    p.step(&bank,0.,-1.,-1.,false,0,t+100,1./60.).unwrap();assert!(p.velocity.length()<0.001);
    println!("Movement clips exercised: {seen:?}; travel={distance}");
 }
 #[test]fn jump_suspension_resumes_running_without_wstart_or_runstart(){
    let assets=std::path::PathBuf::from(std::env::var("NM_TEST_ASSETS").expect("owned assets"));
    let bytes=std::fs::read(assets.join("fred.xrt")).unwrap();
    assert!(delirium_data::kv::unlock(&delirium_data::nm_asset::read(&bytes)));
    let bank=Bank::load(std::path::Path::new(&std::env::var("NM_TEST_COMBAT").expect("movement assets"))).unwrap();
    let rig=Rig::from_json(&std::fs::read(std::env::var("NM_TEST_RIG").unwrap()).unwrap()).unwrap();
    let mut p=Locomotion::new(rig,0.);let mut t=0u32;
    for _ in 0..240{t+=16;p.step(&bank,2.,0.,p.state()[0],false,0,t,1./60.).unwrap();if p.state()[3]>1.5{break;}}
    assert!(p.state()[3]>1.5,"expected established jog before jump, got state={:?} key={}",p.state(),p.key);
    p.suspend_jump();assert!(!p.active);assert_eq!(p.velocity,Vec2::ZERO);
    let mut post_jump_keys=Vec::new();
    for _ in 0..30{t+=16;p.step(&bank,2.,0.,0.,false,0,t,1./60.).unwrap();post_jump_keys.push(p.key.clone());}
    assert!(!post_jump_keys.iter().any(|k|k.contains("wstart")||k.contains("runstart")),
        "resuming after running jump must not replay walk/run start: {post_jump_keys:?}");
    assert!(p.state()[3]>1.5,"expected continuous jog ratio on land, got {}",p.state()[3]);
    // Full cancel must still reset cleanly to idle.
    p.cancel();t+=16;p.step(&bank,2.,0.,0.,false,0,t,1./60.).unwrap();
    assert!(p.key.contains("wstart")||p.key.contains("runstart")||p.state()[5]==1.0);
 }
 #[test]fn aim_release_while_moving_resumes_jog_without_start_replay(){
    let assets=std::path::PathBuf::from(std::env::var("NM_TEST_ASSETS").expect("owned assets"));
    let bytes=std::fs::read(assets.join("fred.xrt")).unwrap();
    assert!(delirium_data::kv::unlock(&delirium_data::nm_asset::read(&bytes)));
    let bank=Bank::load(std::path::Path::new(&std::env::var("NM_TEST_COMBAT").expect("movement assets"))).unwrap();
    let rig=Rig::from_json(&std::fs::read(std::env::var("NM_TEST_RIG").unwrap()).unwrap()).unwrap();
    let mut p=Locomotion::new(rig,0.);let mut t=0u32;
    p.cancel();
    p.suspend_moving(2.0);
    assert!(!p.active);
    let mut post_aim_keys=Vec::new();
    for _ in 0..30{t+=16;p.step(&bank,2.,0.,0.,false,0,t,1./60.).unwrap();post_aim_keys.push(p.key.clone());}
    assert!(!post_aim_keys.iter().any(|k|k.contains("wstart")||k.contains("runstart")),
        "releasing aim while moving must not replay walk/run start: {post_aim_keys:?}");
    assert!(p.state()[3]>1.5,"expected continuous jog ratio after aim release, got {}",p.state()[3]);
    // Standing still on aim release (or cancel) must still allow normal start.
    p.cancel();p.suspend_moving(0.0);t+=16;p.step(&bank,2.,0.,0.,false,0,t,1./60.).unwrap();
    assert!(p.key.contains("wstart")||p.key.contains("runstart")||p.state()[5]==1.0);
 }
 #[test]fn walk_start_from_idle_transitions_smoothly_without_frame_skip_or_speed_spike(){
    let assets=std::path::PathBuf::from(std::env::var("NM_TEST_ASSETS").expect("owned assets"));
    let bytes=std::fs::read(assets.join("fred.xrt")).unwrap();
    assert!(delirium_data::kv::unlock(&delirium_data::nm_asset::read(&bytes)));
    let bank=Bank::load(std::path::Path::new(&std::env::var("NM_TEST_COMBAT").expect("movement assets"))).unwrap();
    let rig=Rig::from_json(&std::fs::read(std::env::var("NM_TEST_RIG").unwrap()).unwrap()).unwrap();
    for (step_ms, dt, steps) in [(17u32, 1./60., 60usize), (7u32, 1./144., 160usize)] {
        let mut p=Locomotion::new(rig.clone(),0.);let mut t=0u32;
        for _ in 0..10{t+=step_ms;let h=p.state()[0];p.step(&bank,0.,h,h,false,0,t,dt).unwrap();}
        let mut prev=p.pose.clone();let mut prev_v=p.state()[2];
        let(mut max_dp_at_cut,mut max_dq_at_cut,mut max_dv_at_cut)=(0.0f32,0.0f32,0.0f32);
        let(mut steady_dp,mut steady_dq)=(0.0f32,0.0f32);
        let(mut saw_walk,mut walk_frames,mut fade_frames)=(false,0usize,0usize);
        for _ in 0..(steps * 2){
            t+=step_ms;let h=p.state()[0];
            p.step(&bank,1.,0.,h,false,0,t,dt).unwrap();
            let st=p.state();
            let dp=prev.iter().zip(&p.pose).fold(0.0f32,|m,(a,b)|m.max(a.p.distance(b.p)));
            let dq=prev.iter().zip(&p.pose).fold(0.0f32,|m,(a,b)|m.max(a.q.angle_between(b.q)));
            let dv=(st[2]-prev_v).abs();
            if p.key=="move_player/walk"{
                saw_walk=true;walk_frames+=1;
                if p.p.start.is_some(){fade_frames+=1;}
                if walk_frames<=steps/2{
                    max_dp_at_cut=max_dp_at_cut.max(dp);max_dq_at_cut=max_dq_at_cut.max(dq);max_dv_at_cut=max_dv_at_cut.max(dv);
                }else{
                    steady_dp=steady_dp.max(dp);steady_dq=steady_dq.max(dq);
                }
            }
            prev=p.pose.clone();prev_v=st[2];
        }
        eprintln!("walk_start dt={dt:.5}: fade_s={:.3} cut(dp={max_dp_at_cut:.4},dq={max_dq_at_cut:.4},dv={max_dv_at_cut:.4}) steady(dp={steady_dp:.4},dq={steady_dq:.4})", fade_frames as f32 * dt);
        assert!(saw_walk);
        assert!(fade_frames as f32 * dt < 0.35,"dt={dt}: finished wstart lingered too long ({} frames)",fade_frames);
        assert!(p.p.start.is_none(),"dt={dt}: finished wstart never cleared");
        assert!(max_dp_at_cut<=(steady_dp*1.25).max(dt*5.5).max(0.045),"dt={dt}: wstart->walk pose jumped: dp={max_dp_at_cut} steady={steady_dp}");
        assert!(max_dq_at_cut<=(steady_dq*1.25).max(dt*9.0).max(0.14),"dt={dt}: wstart->walk rotation jumped: dq={max_dq_at_cut} steady={steady_dq}");
        assert!(max_dv_at_cut<(dt*15.0).max(0.16),"dt={dt}: wstart->walk velocity jumped: dv={max_dv_at_cut}");
        // Releasing walk even right after wstart finishes must trigger wstop (not an abrupt halt).
        let mut p2=Locomotion::new(rig.clone(),0.);let mut t2=0u32;
        for _ in 0..10{t2+=step_ms;let h=p2.state()[0];p2.step(&bank,0.,h,h,false,0,t2,dt).unwrap();}
        for _ in 0..steps{t2+=step_ms;let h=p2.state()[0];p2.step(&bank,1.,0.,h,false,0,t2,dt).unwrap();if p2.key=="move_player/walk"{break;}}
        for _ in 0..4{t2+=step_ms;let h=p2.state()[0];p2.step(&bank,1.,0.,h,false,0,t2,dt).unwrap();}
        let mut saw_stop=false;
        for _ in 0..20{t2+=step_ms;let h=p2.state()[0];p2.step(&bank,0.,h,h,false,0,t2,dt).unwrap();if p2.key.contains("wstop"){saw_stop=true;break;}}
        assert!(saw_stop,"dt={dt}: releasing walk right after wstart must trigger wstop, got {}",p2.key);
    }
 }
}
#[cfg(test)]mod continuity_unit_tests{
 use super::*;
 fn test_rig()->Rig{
    let ids:Vec<u32>=(0..20).collect();
    let parents:Vec<u32>=std::iter::once(u32::MAX).chain(0..19).collect();
    let mut bind=Vec::with_capacity(20*7);
    for _ in 0..20{bind.extend_from_slice(&[0.,0.,0.,0.,0.,0.,1.]);}
    Rig::new(&ids,&parents,&bind).unwrap()
 }
 #[test]fn suspend_moving_seeds_and_clears_resume_ratio_without_assets(){
    let mut p=Locomotion::new(test_rig(),0.);
    p.suspend_moving(2.0);
    assert!(!p.active);
    assert_eq!(p.resume_ratio,Some(2.0));
    assert!(!p.jump_resume);
    // Clamps low moving speeds cleanly and ignores zero/non-finite speeds.
    p.suspend_moving(0.3);
    assert_eq!(p.resume_ratio,Some(0.6));
    p.suspend_moving(0.0);
    assert_eq!(p.resume_ratio,None);
    p.suspend_moving(f32::NAN);
    assert_eq!(p.resume_ratio,None);
    // Cancel or jump suspend must clear any stale aim-resume ratio.
    p.suspend_moving(1.8);
    p.cancel();
    assert_eq!(p.resume_ratio,None);
    p.suspend_moving(1.8);
    p.suspend_jump();
    assert_eq!(p.resume_ratio,None);
 }
}
#[derive(Default)]struct Slope{slope:i16}
#[derive(Default)]pub struct Player{
    forced:Option<Gait>,cover:Option<()>,ratio:f32,state:f32,phase:f32,gait_w:f32,
    pub heading:f32,goal:f32,wish_heading:Option<f32>,turning:onfoot::Turn,turned:f32,
    ms:u32,turn_at:u32,turn_end:bool,turn_col:f32,turn_ramp:f32,yaw_was:[f32;2],
    stop:Option<Stop>,start:Option<Start>,turn:Option<Turn>,spot:Option<Spot>,
    stand_step:Option<f32>,spot_turn:f32,slope:Slope,
}
pub struct Locomotion{p:Player,stamina:Stamina,control_at:Option<u32>,pub rig:Rig,pub pose:Vec<Pose>,pub velocity:Vec2,pub key:String,
    pub active:bool,jump_resume:bool,resume_ratio:Option<f32>,pub elapsed:f32,set:usize,fade:Option<(usize,f32)>,last_heading:f32}
impl Locomotion{
    pub fn new(rig:Rig,heading:f32)->Self{Self{pose:rig.bind.clone(),rig,stamina:Stamina::default(),control_at:None,p:Player{heading,goal:heading,turn_col:3.,gait_w:1.,..Default::default()},velocity:Vec2::ZERO,key:String::new(),active:false,jump_resume:false,resume_ratio:None,elapsed:0.,set:0,fade:None,last_heading:heading}}
    pub fn control(&mut self,push:f32,tapped:bool,held:bool,now:u32,dt:f32)->Result<f32,String>{
        if !push.is_finite()||!(0.0..=1.0).contains(&push)||!dt.is_finite()||dt<=0.||dt>0.1{return Err("invalid on-foot control".into())}
        if let Some(at)=self.control_at{if now<at{self.control_at=None;}else if now-at>100{
            // Advance the ordinary idle recovery at frame-sized steps while
            // native-only mechanics own him; never grant a huge one-frame refill.
            let mut left=(now-at)as f32/1000.;while left>0.&&self.stamina.left<600.{let step=left.min(0.02);self.stamina.recover(Gait::Idle,step);left-=step;}self.stamina.pressure=0.;
        }}
        self.control_at=Some(now);
        let run=self.stamina.run(tapped,held,dt);
        self.stamina.recover(Gait::of(self.p.ratio),dt);
        Ok(animblend::move_ratio(push,run))
    }
    pub fn state(&self)->[f32;8]{[self.p.heading,self.velocity.x,self.velocity.y,self.p.ratio,self.p.phase,self.p.state,self.elapsed,self.set as f32]}
    pub fn cancel(&mut self){self.active=false;self.jump_resume=false;self.resume_ratio=None;self.velocity=Vec2::ZERO;}
    /// Preserve gait continuity across a pure jump/air/land handoff so landing
    /// while moving resumes jogging/sprinting instead of replaying wstart/runstart.
    pub fn suspend_jump(&mut self){
        if self.active{self.jump_resume=true;}
        self.resume_ratio=None;
        self.active=false;
        self.velocity=Vec2::ZERO;
    }
    /// Seed continuous locomotion from native on-foot speed (e.g. releasing
    /// one-handed gun aim while jogging) instead of replaying wstart/runstart.
    pub fn suspend_moving(&mut self,native_ratio:f32){
        self.active=false;
        self.jump_resume=false;
        self.velocity=Vec2::ZERO;
        self.resume_ratio=(native_ratio.is_finite()&&native_ratio>0.).then_some(native_ratio.clamp(0.6,2.5));
    }
    /// wanted 0..3 is analog gait intent, goal is the camera-relative heading.
    pub fn step(&mut self,bank:&Bank,wanted:f32,goal:f32,heading:f32,crouched:bool,slope:i16,now:u32,dt:f32)->Result<(),String>{
        if !wanted.is_finite()||!goal.is_finite()||!heading.is_finite()||!dt.is_finite()||dt<=0.||dt>0.1||!(0.0..=3.0).contains(&wanted){return Err("Invalid locomotion frame".into())}
        let clips=Clips::new(bank)?;
        if !self.active{
            let resuming_jump=std::mem::take(&mut self.jump_resume);
            let resuming_moving=self.resume_ratio.take();
            if resuming_moving.is_some()&&wanted>0.{
                self.p.start=None;self.p.stop=None;self.p.turn=None;self.p.spot=None;
                self.p.turning=Default::default();self.p.turned=0.;self.p.stand_step=None;
                self.p.heading=heading;self.p.goal=goal;self.p.turn_col=3.;self.p.gait_w=1.;self.p.state=2.;
                let target=resuming_moving.unwrap();
                self.p.ratio=target.min(wanted).max(wanted.min(0.85));
                self.last_heading=heading;self.fade=None;self.active=true;
            }else if resuming_jump&&wanted>0.&&(self.p.ratio>0.||self.p.start.is_some()){
                self.p.start=None;self.p.stop=None;self.p.turn=None;self.p.spot=None;
                self.p.turning=Default::default();self.p.turned=0.;self.p.stand_step=None;
                self.p.heading=heading;self.p.goal=goal;self.p.turn_col=3.;self.p.gait_w=1.;self.p.state=2.;
                if self.p.ratio<=0.{self.p.ratio=wanted.min(2.);}
                self.last_heading=heading;self.fade=None;self.active=true;
            }else{
                self.p=Player{heading,goal:heading,turn_col:3.,gait_w:1.,..Default::default()};
                self.elapsed=0.;self.last_heading=heading;self.active=true;
            }
        }
        let set=if crouched{CROUCHED}else{ON_FOOT};if set!=self.set{self.fade=Some((self.set,0.));self.set=set;}
        let p=&mut self.p;p.ms=now;p.goal=goal;p.wish_heading=(wanted>0.).then_some(goal);p.slope.slope=slope;
        // Respect external heading changes while retaining the original rate history.
        p.heading+=lsc4_combat::actiontable::wrap(heading-self.last_heading);
        turn_new(&clips,p,set);
        if let Some(fixed)=p.start.as_ref().filter(|s|s.rate>0.).map(|s|s.fixed(p.goal)){
            let(h,_,t,_)=onfoot::heading_turn(p.heading,p.goal,TURN_SPEED,true,false,dt,p.turning);p.turning=t;
            if !fixed{p.heading=h+t.left.clamp(-std::f32::consts::PI,std::f32::consts::PI)*dt;}
        }else if p.turn.as_ref().is_some_and(Turn::holds){p.turning=Default::default();}
        else{let(h,_,t,done)=onfoot::heading_turn(p.heading,p.goal,TURN_SPEED,true,false,dt,p.turning);let was=p.heading;
            p.heading=h+done.unwrap_or(p.stand_step.unwrap_or(t.rate*dt));p.turning=t;p.turned=p.heading-was;}
        let wanted=onfoot::sharp_turn(p.ratio,wanted,lsc4_combat::actiontable::wrap(p.goal-p.heading),true);
        let before=p.ratio;p.ratio=animblend::ease_ratio(p.ratio,wanted,dt,true,3.);
        let under=p.stop.as_ref().filter(|s|s.rate>0.&&s.weight<1.).map(|s|s.from.0)
            .or(p.turn.as_ref().filter(|t|t.rate>0.&&t.weight<1.).map(|t|t.from)).unwrap_or(gait_ratio(before,p.state));
        p.turn_col=turn_column(&clips,p,set,dt);
        let mut v=Vec2::new(0.,clips.speed_at(under,set,slope,p.turn_col));
        p.phase=gait_step(&clips,gait_ratio(p.ratio,p.state),p.turn_col,p.phase,set,slope,dt);gait_left(p,dt);
        let late=p.yaw_was[0];p.yaw_was=[p.yaw_was[1],0.];
        for(step,w)in [stop(&clips,p,set,dt),start(&clips,p,wanted,set,dt),turn(&clips,p,wanted,set,dt)].into_iter().flatten(){v=v.lerp(step,w);}
        if wanted>0.&&p.start.as_ref().is_none_or(|s|s.rate<0.)&&p.stop.is_none()&&p.turn.is_none()&&p.forced.is_none()&&dt>0.{
            let prev_speed=self.velocity.length();
            let target_speed=v.length();
            let max_dv=(8.+4.*(wanted-1.).max(0.))*dt;
            if prev_speed>0.01&&(target_speed-prev_speed).abs()>max_dv&&target_speed>1e-4{
                v*=((prev_speed+(target_speed-prev_speed).clamp(-max_dv,max_dv))/target_speed);
            }
        }
        p.heading+=late;pick(&clips,p,before,wanted,set,dt);
        if let Some(s)=p.stop.as_mut().filter(|s|movestop::stop_fades(p.state,s.id)){s.rate=-movestop::STOP_FADE;}
        start_new(&clips,p,wanted,set);spot(&clips,p,set,dt);
        let samples=|ratio:f32,phase:f32,set:usize|clips.gait_parts(ratio,set,slope,p.turn_col).into_iter().map(|(c,w)|(c.key,phase,w)).collect::<Vec<_>>();
        let mut list=samples(gait_ratio(p.ratio,p.state),p.phase,set);
        let mut pose=bank.retarget_mix(&self.rig,&list).ok_or("gait pose unavailable")?;
        if let Some((old,t))=self.fade{let t=t+dt/0.2;if t>=1.{self.fade=None}else{self.fade=Some((old,t));let old=bank.retarget_mix(&self.rig,&samples(gait_ratio(p.ratio,p.state),p.phase,old)).ok_or("old gait unavailable")?;pose=self.rig.blend(&old,&pose,t,0);}}
        if let Some(s)=&p.stop{if s.rate>0.&&s.weight<1.{list=samples(s.from.0,s.from.1,set);pose=bank.retarget_mix(&self.rig,&list).ok_or("stop underlay unavailable")?;}let over=bank.retarget(&self.rig,s.key,s.phase,movement_flags(s.key).ok_or("unknown movement flags")?).ok_or("stop unavailable")?;pose=self.rig.blend(&pose,&over,s.weight,0);}
        for layer in [p.start.as_ref().map(|s|(s.key,s.phase,s.weight)),p.spot.as_ref().map(|s|(s.key,s.phase,s.weight)),p.turn.as_ref().map(|s|(s.key,s.phase,s.weight))].into_iter().flatten(){let over=bank.retarget(&self.rig,layer.0,layer.1,movement_flags(layer.0).ok_or("unknown transition flags")?).ok_or("transition unavailable")?;pose=self.rig.blend(&pose,&over,layer.2,0);}
        self.key=p.turn.as_ref().map(|s|s.key).or(p.start.as_ref().filter(|s|s.rate>0.).map(|s|s.key)).or(p.stop.as_ref().map(|s|s.key)).or(p.spot.as_ref().map(|s|s.key)).unwrap_or_else(||list.iter().max_by(|a,b|a.2.total_cmp(&b.2)).unwrap().0).into();
        self.pose=pose;self.velocity=v;self.elapsed+=dt;self.last_heading=p.heading;Ok(())
    }
}
