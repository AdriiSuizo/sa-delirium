//! Finite native participants share one mobility across all contact shapes.
//! This registry does not integrate the host a second time. It returns a
//! reciprocal impulse and a separate geometric correction for one host commit.
use crate::prelude::*;
use std::collections::BTreeMap;

#[derive(Clone,Copy,Debug,PartialEq)]
pub struct ExternalOwner {
    pub id:u64,pub epoch:u64,pub com:Vec3,pub velocity:Vec3,pub spin:Vec3,
    pub linear_inverse:Mat3,pub angular_inverse:Mat3,
}
#[derive(Clone,Copy,Debug,Default)]
pub struct ExternalResult {
    pub id:u64,pub epoch:u64,pub impulse:Vec3,pub angular_impulse:Vec3,
    pub delta_velocity:Vec3,pub delta_spin:Vec3,
    pub position_correction:Vec3,pub orientation_correction:Vec3,
}
/// Actual solved pair evidence; impulse acts on A, with the opposite on B.
#[derive(Clone,Copy,Debug)]
pub struct ExternalContact {
    pub a:u64,pub b:u64,pub impulse:Vec3,pub point:Vec3,pub normal:Vec3,
    pub child_a:usize,pub child_b:usize,
    pub velocity_a:Vec3,pub velocity_b:Vec3,
}
/// Native accepted baseline -> currently staged geometry, in WORLD axes.
/// This is already-predicted displacement, not another velocity to integrate.
#[derive(Clone,Copy,Debug,PartialEq)]
pub struct ExternalPredictor {pub id:u64,pub pending_position:Vec3,pub pending_rotation:Quat}
/// A source-authored motor impulse applied once before the shared contact
/// solve. The snapshot remains the unmodified native baseline.
#[derive(Clone,Copy,Debug,Default,PartialEq)]
pub struct ExternalDrive {pub id:u64,pub impulse:Vec3,pub torque:Vec3}
/// Explicit ownership of ordinary support/gravity. Never inferred from an
/// animated shape; hosts must release native support as one atomic policy.
#[derive(Clone,Copy,Debug,PartialEq)]
pub struct ExternalGroundClaim {pub id:u64,pub authored_xy:Vec2,pub initial_grounded:bool,pub opening_damping:bool,pub readd:bool}
/// Source instance semantics, explicitly adapted by host. Vehicle +12E4==2
/// is a boat: only this branch subtracts its root-point velocity in support
/// damping. Unknown entities must not be guessed from collision geometry.
#[derive(Clone,Copy,Debug,PartialEq)]
pub struct ExternalGroundBody {pub id:u64,pub boat:bool,pub excluded_type15:bool,pub fixed:bool,pub mass:f32}
#[derive(Clone,Copy,Debug,Default)]
pub struct ExternalGroundResult {
    pub id:u64,pub epoch:u64,pub grounded:bool,pub before:bool,
    pub support_owner:u64,pub support_member:usize,pub support_part:usize,
    pub point:Vec3,pub normal:Vec3,pub velocity:Vec3,
    pub support_impulse:Vec3,pub gravity_impulse:Vec3,pub slope_velocity:f32,
}
#[derive(Clone,Copy,Debug)]
pub struct ExternalPlacement {
    pub id:u64,pub admitted_position:Vec3,pub admitted_rotation:Quat,pub valid:bool,
    pub world_contacts:u32,pub requested_position:Vec3,
}
/// Reason bits: 1 missing predictor, 2 missing geometry, 4 nonfinite proposal,
/// 8 GJK query failure,16 deeper baseline overlap,32 clear-to-clear tunnel,
/// 64 uncertified cast,128 non-upright animated owner,256 missing replay query.
/// Geometry fields identify the FIRST failure; counters cover the whole step.
#[derive(Clone,Copy,Debug,Default)]
pub struct ExternalPlacementDiagnostic {
    pub id:u64,pub reason_flags:u32,pub obstacle:i32,pub child:u32,
    pub triangle_chunk:u32,pub triangle_index:u32,pub base_gap:f32,pub end_gap:f32,
    pub cast_t:f32,pub cast_gap:f32,pub cast_iterations:u32,pub cast_error:u32,
    pub query_count:u32,pub triangle_count:u32,pub gjk_count:u32,pub cast_count:u32,
    pub cast_failures:u32,pub solve_us:u64,
}
/// Exact ordered query output used by placement admission; no replacement
/// floor, convex hull approximation or reconstructed adjacency is allowed.
pub type ExternalReplayTriangle=([Vec3;3],u8,[Vec3;4],u8,u32,crate::collision::Poly);
#[derive(Clone,Debug)]
pub struct ExternalReplayQuery {pub obstacle:i32,pub child:usize,pub lo:Vec3,pub hi:Vec3,pub triangles:Vec<ExternalReplayTriangle>}
#[derive(Clone,Copy,Debug)]
pub struct ExternalWorldRowReplay {pub point:Vec3,pub normal:Vec3,pub angular:Option<Vec3>,pub gap:f32,pub native_gap:f32}
#[derive(Clone,Debug)]
pub struct ExternalPlacementReplay {
    pub owner:ExternalOwner,pub predictor:ExternalPredictor,pub drive:ExternalDrive,
    pub geometry:Vec<crate::ragdoll::Obstacle>,pub dt:f32,
    pub delta:Vec3,pub turn:Vec3,pub requested:Vec3,pub contacts:u32,
    pub placement:ExternalPlacement,pub diagnostic:ExternalPlacementDiagnostic,
    pub queries:Vec<ExternalReplayQuery>,
    pub world_rows:Vec<ExternalWorldRowReplay>,
    pub environment_override:bool,
}
#[derive(Clone,Copy,Debug,PartialEq,Eq)]
pub enum ExternalError { WrongEpoch(u64),InvalidSnapshot(u64),DivergentSnapshot(u64),MissingSnapshot(u64),InvalidPredictor(u64),MissingPredictor(u64),InvalidDrive(u64),DivergentDrive(u64),ConsumedDriveEpoch(u64),InvalidEnvironment(u64),DuplicateEnvironment(i32),InvalidGround(u64),DivergentGround(u64) }
impl std::fmt::Display for ExternalError {
    fn fmt(&self,f:&mut std::fmt::Formatter<'_>)->std::fmt::Result {write!(f,"{self:?}")}
}
impl std::error::Error for ExternalError {}
fn valid_mobility(m:Mat3)->bool {
    if !m.is_finite(){return false;}
    let scale=m.to_cols_array().into_iter().map(f32::abs).fold(0f32,f32::max);
    if scale==0.{return true;}
    let a=m/scale;
    if (a-a.transpose()).to_cols_array().into_iter().any(|x|x.abs()>1e-6){return false;}
    // All principal minors nonnegative is the PSD criterion for symmetric3x3.
    let d=Vec3::new(a.x_axis.x,a.y_axis.y,a.z_axis.z);
    d.min_element()>=-1e-6&&d.x*d.y-a.x_axis.y*a.x_axis.y>=-1e-6&&
        d.x*d.z-a.x_axis.z*a.x_axis.z>=-1e-6&&d.y*d.z-a.y_axis.z*a.y_axis.z>=-1e-6&&a.determinant()>=-1e-6
}
impl ExternalOwner {
    pub fn valid(&self)->bool {self.com.is_finite()&&self.velocity.is_finite()&&self.spin.is_finite()&&valid_mobility(self.linear_inverse)&&valid_mobility(self.angular_inverse)}
    pub fn point_velocity(&self,p:Vec3)->Vec3 {self.velocity+self.spin.cross(p-self.com)}
    pub(crate) fn response(&self,j:[f64;6])->[f64;6] {
        let v=self.linear_inverse*Vec3::new(j[0] as f32,j[1] as f32,j[2] as f32);
        let w=self.angular_inverse*Vec3::new(j[3] as f32,j[4] as f32,j[5] as f32);
        [v.x as f64,v.y as f64,v.z as f64,w.x as f64,w.y as f64,w.z as f64]
    }
    pub(crate) fn qd(&self)->[f64;6] {[self.velocity.x as f64,self.velocity.y as f64,self.velocity.z as f64,self.spin.x as f64,self.spin.y as f64,self.spin.z as f64]}
}
#[derive(Default)]
pub(crate) struct ExternalRegistry {
    pub epoch:u64,
    pub owners:BTreeMap<u64,ExternalOwner>,
    pub results:Vec<ExternalResult>,
}
impl ExternalRegistry {
    pub fn stage(&mut self,epoch:u64,owners:&[ExternalOwner])->Result<(),ExternalError> {
        let mut next=BTreeMap::new();
        for &o in owners {
            if o.epoch!=epoch{return Err(ExternalError::WrongEpoch(o.id));}
            if !o.valid(){return Err(ExternalError::InvalidSnapshot(o.id));}
            if let Some(old)=next.insert(o.id,o){if old!=o{return Err(ExternalError::DivergentSnapshot(o.id));}}
        }
        self.epoch=epoch;self.owners=next;self.results.clear();Ok(())
    }
    pub fn commit(&mut self,id:u64,impulse:Vec3,angular_impulse:Vec3,position:Vec3,orientation:Vec3) {
        let Some(owner)=self.owners.get_mut(&id) else{return};
        let dv=owner.linear_inverse*impulse;let dw=owner.angular_inverse*angular_impulse;
        owner.velocity+=dv;owner.spin+=dw;
        let at=self.results.iter().position(|r|r.id==id).unwrap_or_else(||{
            self.results.push(ExternalResult{id,epoch:owner.epoch,..Default::default()});self.results.len()-1
        });
        let r=&mut self.results[at];r.impulse+=impulse;r.angular_impulse+=angular_impulse;
        r.delta_velocity+=dv;r.delta_spin+=dw;r.position_correction+=position;r.orientation_correction+=orientation;
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    fn owner()->ExternalOwner {ExternalOwner{id:7,epoch:11,com:Vec3::ZERO,velocity:Vec3::X,spin:Vec3::ZERO,linear_inverse:Mat3::IDENTITY/25.,angular_inverse:Mat3::ZERO}}
    #[test]fn registry_rejects_divergent_epoch_generation_and_invalid_mobility(){
        let a=owner();let mut r=ExternalRegistry::default();assert!(r.stage(11,&[a,a]).is_ok());assert_eq!(r.owners.len(),1);
        assert_eq!(r.stage(12,&[a]),Err(ExternalError::WrongEpoch(7)));
        assert_eq!(r.stage(11,&[a,ExternalOwner{velocity:Vec3::Y,..a}]),Err(ExternalError::DivergentSnapshot(7)));
        for angular in [Mat3::from_diagonal(Vec3::new(1.,-1.,1.)),Mat3::from_cols(Vec3::ONE,Vec3::Y,Vec3::Z)]{
            assert_eq!(r.stage(11,&[ExternalOwner{angular_inverse:angular,..a}]),Err(ExternalError::InvalidSnapshot(7)));
        }
        assert_eq!(r.owners[&7],a,"rejected stage must not partially replace a snapshot");
        let next=ExternalOwner{id:8,epoch:12,..a};assert!(r.stage(12,&[next]).is_ok());assert!(!r.owners.contains_key(&7));
    }
    #[test]fn no_spin_ped_and_shared_rotating_vehicle_have_finite_point_mobility(){
        let mut r=ExternalRegistry::default();let a=owner();r.stage(11,&[a]).unwrap();
        r.commit(7,Vec3::X*25.,Vec3::Y*100.,Vec3::Y*0.01,Vec3::ZERO);
        r.commit(7,Vec3::X*25.,Vec3::Y*100.,Vec3::ZERO,Vec3::ZERO);
        assert_eq!(r.results.len(),1);assert_eq!(r.results[0].delta_velocity,Vec3::X*2.);assert_eq!(r.results[0].delta_spin,Vec3::ZERO);
        assert_eq!(r.owners[&7].velocity,Vec3::X*3.);
        let car=ExternalOwner{angular_inverse:Mat3::IDENTITY/100.,spin:Vec3::Z,..a};
        assert_eq!(car.point_velocity(Vec3::Y),Vec3::ZERO);
        assert_eq!(car.response([0.,0.,0.,0.,0.,100.]),[0.,0.,0.,0.,0.,1.]);
    }
}
