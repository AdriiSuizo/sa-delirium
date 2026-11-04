//! Immutable current-geometry witness. Does not run contact response, promote
//! an actor, or confuse its rendered/targeting skeleton with a physical owner.
use super::*;

impl Ragdoll {
    pub fn query_obstacle_pose_contacts(&self, build:&Build, pose:&[Transform], obstacle:&Obstacle)->Vec<PartPairContact>{
        if self.bodies.len()!=build.parts.len() || pose.len()!=build.parts.len() ||
            !obstacle.filter.meets(CollisionFilter::NM_FRAGMENT){return vec![]}
        let rot=Mat3::from_quat(obstacle.rot);let mut out=vec![];
        for (a,(body,p)) in self.bodies.iter().zip(&build.parts).enumerate(){
            if (self.contact_off|self.gone)>>a&1!=0{continue}
            let mut oriented=body.clone();oriented.set_q(pose[a].rotation);
            let frame=(pose[a].translation,turn(&oriented.bound()));
            for (b,part) in obstacle.parts.iter().enumerate(){
                if !part.filter.meets(CollisionFilter::NM_FRAGMENT) || matches!(part.shape,gjk::Shape::Hull(v,_) if v.is_empty()){continue}
                let other=(obstacle.at+rot*part.at,rot*part.m);
                if let Some(gjk::Closest{a:point_a,b:point_b,n:normal,dist:gap})=Self::shape_contact((&part_shape(p),&part.shape),frame,other,MARGIN(),Vec3::Z){
                    out.push(PartPairContact{a,b,point_a,point_b,normal,gap});
                }
            }
        }
        out
    }
}
