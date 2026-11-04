//! Original ordinary-ped support contract, connected by opt-in Group claims.
//! TU8 822F7118 (callback), 822F74F0 (force), 822FB96C (history),
//! 822F6678 (grounded transition). Read-only reference: Claude WT
//! astra15-stream-focus, collision::Walker. No ray substitute or root snap.
use crate::prelude::*;
use crate::{Vd,Vx};

pub const ROOT_HEIGHT:f32=1.0;
pub const GRAVITY:f32=9.8;
const STIFFNESS:f32=58.86; // 8200F3A8; 82A5F438 initializes damping -2*sqrt.

#[derive(Clone,Copy,Debug,PartialEq)]
pub enum OtherBody {
    Missing,
    Fixed { excluded_type15:bool },
    Dynamic { mass:f32,excluded_type15:bool },
}
impl OtherBody {
    fn is_fixed(self)->bool{matches!(self,Self::Fixed{..})}
    fn allows_support(self)->bool{matches!(self,Self::Fixed{excluded_type15:false}|Self::Dynamic{excluded_type15:false,..})}
}
#[derive(Clone,Copy,Debug,PartialEq)]
pub struct Support {
    pub owner:u64,pub point:Vec3,pub normal:Vec3,pub velocity:Vec3,pub stairs:bool,
}
#[derive(Clone,Copy,Debug)]
pub struct Contact {
    pub child:usize,pub other:OtherBody,pub support:Support,
}
#[derive(Clone,Copy,Debug,PartialEq)]
pub struct Disposition {
    pub clear_manifold:bool,pub solve:bool,pub selected_support:bool,
}
#[derive(Clone,Debug,Default)]
pub struct State {
    pub grounded:bool,pub before:bool,pub jumping:bool,
    pub stairs:bool,pub anchors:[Vec3;2],pub ground:Option<Support>,
}
#[derive(Clone,Copy,Debug)]
pub struct Input {
    pub root:Vec3,pub velocity:Vec3,pub authored_xy:Vec2,
    pub normal_instance:bool,pub simulator_enabled:bool,
    /// CPedMoveBlendOnFoot vf20 (826041E8) returns true. Do not assume this
    /// for jump, climb, swimming, slide-to-cover or another SA task.
    pub opening_damping:bool,
}
#[derive(Clone,Copy,Debug,Default)]
pub struct Forces {
    /// These are separate on purpose: a host retaining native gravity must
    /// not also apply gravity_acceleration. Runtime ownership remains gated.
    pub support_acceleration:Vec3,pub gravity_acceleration:Vec3,
    pub slope_velocity:f32,pub had_ground:bool,
}
impl State {
    /// Previous substep's contact drives this substep; callback finds the next
    /// support afterwards. Force output never rewrites position or velocity.
    pub fn begin(&mut self,input:Input)->Forces {
        let mut out=Forces{gravity_acceleration:-Vec3::Z*GRAVITY,had_ground:self.grounded,..Default::default()};
        if let Some(s)=self.ground.filter(|_|self.grounded){
            out.slope_velocity=-(s.normal.x*input.authored_xy.x+s.normal.y*input.authored_xy.y)/s.normal.z;
            if input.normal_instance&&input.simulator_enabled&&s.point.z-input.root.z> -2.0 {
                let lift=GRAVITY*((s.point.z-input.root.z+ROOT_HEIGHT)*6.0+1.0).min(10.0);
                let (direction,relative)=if self.stairs&&self.anchors[0]!=Vec3::ZERO {
                    let d=s.point-self.anchors[0];
                    let d=if d.vd(d)<0.0625&&self.anchors[1]!=Vec3::ZERO{s.point-self.anchors[1]}else{d};
                    let direction=if d.y.mul_add(d.y,d.x*d.x)>0.05{crate::nmdbm::vnorm(d.vx(Vec3::Z).vx(d))}else{s.normal};
                    (direction,input.velocity)
                }else{(s.normal,input.velocity-s.velocity)};
                let mut speed=direction.z.mul_add(relative.z,direction.y.mul_add(relative.y,direction.x*relative.x));
                if s.normal.z>0.1{speed/=direction.z;}
                let damping=if speed<0.0||input.opening_damping{-2.0*STIFFNESS.sqrt()}else{0.0};
                let damp=(damping*speed).clamp(-50.0,50.0);
                out.support_acceleration.z=lift+damp;
            }
        }
        // 822FB96C: two grounded frames, not jumping; the 0.25m anchor history
        // belongs to the contact point, not the character's animation bones.
        self.anchors=match self.ground.filter(|_|self.grounded&&self.before&&!self.jumping){
            Some(s) if self.anchors[0]==Vec3::ZERO=>[s.point,s.point],
            Some(s) if (s.point-self.anchors[0]).vd(s.point-self.anchors[0])>0.0625=>[s.point,self.anchors[0]],
            Some(_)=>self.anchors,
            None=>[Vec3::ZERO;2],
        };
        self.ground=None;
        out
    }
    /// Must run before rows enter the shared solve, for map AND other bodies.
    /// Returning clear means erase cached impulses as well as this step's row.
    pub fn contact(&mut self,root:Vec3,c:Contact,simulator_enabled:bool)->Disposition {
        if c.child==1 {
            let s=c.support;
            let ceiling=if c.other.is_fixed(){root.z}else{root.z-0.5};
            let selected=simulator_enabled&&c.other.allows_support()&&s.normal.z>=0.5&&s.point.z<=ceiling&&
                self.ground.is_none_or(|old|s.point.z>old.point.z);
            if selected{self.ground=Some(s);self.stairs=s.stairs;}
            return Disposition{clear_manifold:true,solve:false,selected_support:selected};
        }
        // 822F7428..74B4: child3 is off for fixed/no instance and mass>200.
        // Its contacts with a finite 25kg ped remain genuine body contacts.
        let off=c.child==3&&match c.other{OtherBody::Missing|OtherBody::Fixed{..}=>true,OtherBody::Dynamic{mass,..}=>mass>200.0};
        Disposition{clear_manifold:false,solve:!off,selected_support:false}
    }
    pub fn finish(&mut self,root:Vec3,had_ground:bool){
        self.before|=had_ground;
        self.grounded=self.ground.is_some_and(|s|{
            let height=s.point.z-root.z;height> -1.0||(self.before&&height> -1.3)
        });
        if self.grounded{self.jumping=false;}
    }
}

#[cfg(test)]mod tests{
    use super::*;
    fn support(z:f32)->Support{Support{owner:7,point:Vec3::new(2.,3.,z),normal:Vec3::Z,velocity:Vec3::ZERO,stairs:false}}
    fn input(root:Vec3,velocity:Vec3)->Input{Input{root,velocity,authored_xy:Vec2::ZERO,normal_instance:true,simulator_enabled:true,opening_damping:true}}
    fn state(z:f32)->State{State{grounded:true,before:true,ground:Some(support(z)),..Default::default()}}
    #[test]fn equilibrium_is_force_balance_and_impulse_survives_without_snap(){
        let mut s=state(0.);let root=Vec3::new(2.,3.,1.);let f=s.begin(input(root,Vec3::ZERO));
        assert_eq!(f.support_acceleration+f.gravity_acceleration,Vec3::ZERO);
        let mut s=state(0.);let f=s.begin(input(root,Vec3::Z));
        let dt=1./120.;let velocity=Vec3::Z+(f.support_acceleration+f.gravity_acceleration)*dt;
        assert!(velocity.z>0.8&&velocity.z<0.9,"external vertical impulse decays continuously; never reset to zero");
        assert_eq!(root,Vec3::new(2.,3.,1.));
    }
    #[test]fn platform_point_velocity_is_the_damping_frame(){
        let root=Vec3::new(2.,3.,1.);let mut s=state(0.);s.ground.as_mut().unwrap().velocity=Vec3::new(3.,-2.,2.);
        let f=s.begin(input(root,Vec3::new(3.,-2.,2.)));assert_eq!(f.support_acceleration.z,GRAVITY);
    }
    #[test]fn original_capsule_callback_selects_highest_and_never_pushes(){
        let root=Vec3::Z;let mut s=State::default();
        for z in [-0.2,0.1,0.0]{let d=s.contact(root,Contact{child:1,other:OtherBody::Fixed{excluded_type15:false},support:support(z)},true);assert!(d.clear_manifold&&!d.solve);}
        assert_eq!(s.ground.unwrap().point.z,0.1);
        let d=s.contact(root,Contact{child:1,other:OtherBody::Dynamic{mass:25.,excluded_type15:false},support:support(0.7)},true);assert!(!d.selected_support&&d.clear_manifold&&!d.solve);
        assert_eq!(s.ground.unwrap().point.z,0.1);
    }
    #[test]fn lower_curved_contact_uses_real_other_mass_and_state(){
        for (other,solve) in [(OtherBody::Missing,false),(OtherBody::Fixed{excluded_type15:false},false),(OtherBody::Dynamic{mass:25.,excluded_type15:false},true),(OtherBody::Dynamic{mass:200.,excluded_type15:false},true),(OtherBody::Dynamic{mass:200.01,excluded_type15:false},false)]{
            let mut s=State::default();let d=s.contact(Vec3::Z,Contact{child:3,other,support:support(0.)},true);assert_eq!(d.solve,solve);assert!(!d.clear_manifold);
        }
    }
    #[test]fn grounded_history_uses_source_thresholds_not_distance_to_animation(){
        let root=Vec3::Z;
        let mut s=state(-0.2);s.finish(root,true);assert!(s.grounded);
        s.ground=Some(support(-0.31));s.finish(root,true);assert!(!s.grounded);
        let mut fresh=State{ground:Some(support(-0.2)),..Default::default()};fresh.finish(root,false);assert!(!fresh.grounded);
    }
    #[test]fn source_force_gate_and_opening_damping_are_explicit(){
        let root=Vec3::Z;for (normal,enabled) in [(false,true),(true,false)]{let mut s=state(0.);let mut i=input(root,Vec3::ZERO);i.normal_instance=normal;i.simulator_enabled=enabled;assert_eq!(s.begin(i).support_acceleration,Vec3::ZERO);}
        let mut s=state(0.);let mut i=input(root,Vec3::Z);i.opening_damping=false;assert_eq!(s.begin(i).support_acceleration.z,GRAVITY);
        let mut s=state(0.);i.velocity=-Vec3::Z;assert!(s.begin(i).support_acceleration.z>GRAVITY);
    }
    #[test]fn support_history_is_transferred_without_changing_world_root(){
        let mut s=state(0.);let root=Vec3::new(2.,3.,1.);s.begin(input(root,Vec3::ZERO));assert_eq!(s.anchors,[support(0.).point;2]);
        let mut next=support(0.);next.point.x+=0.3;s.ground=Some(next);s.begin(input(root,Vec3::ZERO));assert_eq!(s.anchors,[next.point,support(0.).point]);
        s.jumping=true;s.ground=Some(next);s.begin(input(root,Vec3::ZERO));assert_eq!(s.anchors,[Vec3::ZERO;2]);
    }
    #[test]fn stairs_use_contact_history_direction_and_own_velocity(){
        let mut s=state(0.3);s.stairs=true;s.anchors=[Vec3::new(1.,3.,0.),Vec3::new(0.7,3.,0.)];
        let i=input(Vec3::new(2.,3.,1.),Vec3::new(1.,0.,0.));let mut moving=s.clone();moving.ground.as_mut().unwrap().velocity=Vec3::Z*8.;
        let a=s.begin(i);let b=moving.begin(i);
        assert_eq!(a.support_acceleration,b.support_acceleration,"original stairs/history branch uses own speed, not platform subtraction");
        assert!(a.support_acceleration.z>GRAVITY*(6.*0.3+1.),"slope approach produces downward relative speed and upward damping");
        assert!(a.support_acceleration.is_finite());
    }
}
