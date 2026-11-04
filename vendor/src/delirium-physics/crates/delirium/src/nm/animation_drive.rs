//! Animation targets for a balanced physical character, leased for one host step.
//! Reuse activePose's angle conversion and the current NM motor gains. A copy of
//! the effectors is used: the animation never becomes a persistent controller
//! target, never poses a body, and never replaces balance's feet/support forces.
use super::*;

pub(crate) struct AnimationDrive {
    frames: Vec<Transform>,
    joints: Vec<usize>,
    weight: f32,
}

impl Agent {
    pub fn submit_animation_pose(&mut self, frames: Vec<Transform>, mask: &str, weight: f32) -> Result<(), &'static str> {
        self.animation_drive = None;
        // A full-body reaction also articulates hips, knees and ankles. This
        // selects finite joint motors, never a root/body pin. Native support,
        // contact impulses, gains and behaviour arbitration remain in force.
        if !matches!(mask,"fb"|"ub"|"ua"|"ul"|"ur"|"uk"|"un"|"us"|"ut"|"uw"|"uc") {
            return Err("animation drive requires an explicit body mask");
        }
        if !weight.is_finite() || !(0.0..=1.0).contains(&weight)
            || frames.len()!=self.doll.frames().count()
            || frames.iter().any(|f|!f.translation.is_finite() || !f.rotation.is_finite() || (f.rotation.length_squared()-1.0).abs()>0.01) {
            return Err("invalid animation pose");
        }
        if weight>0.0 {
            self.animation_drive=Some(AnimationDrive{frames,joints:crate::nmrs::Character::mask(mask),weight});
        }
        Ok(())
    }
    pub fn cancel_animation_pose(&mut self) { self.animation_drive=None; }
    /// Last host step: 0 absent, 1 applied, 2 yielded to a conflicting behaviour.
    pub fn animation_pose_status(&self) -> i32 { self.animation_drive_status }

    pub(super) fn apply_animation_drive(&mut self, held:&mut crate::ragdoll::Kept, dt:f32) {
        let Some(d)=self.animation_drive.as_ref() else {return};
        self.animation_drive_status=2;
        if self.fallen || self.balance.is_none() || self.incoming_state!=0
            || self.nm_shot.is_some() || self.nm_hit.is_some() || self.catch_fall.is_some()
            || self.nm_cf.as_ref().is_some_and(|f|f.active) || self.nm_ru.active || self.nm_rds.active
            || self.nm_pd.active || self.nm_bf.active || self.nm_hf.active() || self.nm_br.active
            || self.nm_bw.as_ref().is_some_and(|w|w.active) || self.roll_up.is_some()
            || !self.extras.on.is_empty() || self.extras.hard.is_some() {return;}
        let Some(mut ch)=self.ch.clone() else {return};
        let rows=crate::nmrs::incoming_rows(self.doll.nm,&d.frames);
        for &j in &d.joints {
            if j>=ch.eff.len() {continue;}
            let limits=ch.limits(j);
            let target=ch.incoming_angle(j,&rows);
            for axis in 0..3 {
                // Joint limits, not a post-solve body clamp. Motor strength,
                // damping, torque, contacts and momentum remain the NM ones.
                let target=target[axis].clamp(limits[axis*2],limits[axis*2+1]);
                ch.eff[j].desired[axis]+=(target-ch.eff[j].desired[axis])*d.weight;
            }
        }
        let pelvis=self.roles.pelvis;
        held.muscles=ch.muscles(Muscle{target:self.zero[pelvis],stiffness:0.,damping:0.});
        held.muscles[pelvis].stiffness=0.;
        held.rigid=ch.rigid(dt,self.strength,self.most);
        self.animation_drive_status=1;
    }
}
