//! Versioned, deterministic host snapshots. Missing task/weapon/target state is
//! an error, not a reason to assume the victim should ragdoll. JSON is evaluated
//! only on accepted hits, never on the render or solver loop.
use crate::{Data,hash,Pick};
use serde::Deserialize;
use lsc4_combat::melee::{Ask,Fighter,Held,Opponent,Target,Special};

#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
struct Weapon { kind:u32, flags:u32, between_shots:i32 }
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
struct Actor {
    profile:String, player:bool, health_iv:f32, weapon:Option<Weapon>,
    position:[f32;3], heading:f32, velocity:[f32;3], forward:[f32;3],
    blocking:bool, just_struck:bool, lock_on:bool, on_stairs:bool, flags_240:u32, move_from_idle:bool,
    lies:u8, crouched:bool,
    // Entry is None when that real point is unavailable. Do not fabricate a
    // vertical column for a body lying on the floor. Slots are IV point 0..7.
    points:[Option<[f32;3]>;8],
    // Seconds: elapsed clip time and this frame's step (NOT clip duration).
    result:Option<String>, clip_time_and_step:Option<[f32;2]>,
}
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
struct Input {
    version:u32, phase:String, victim:Actor, attacker:Actor,
    combat_task:bool, retreat_task:bool, nm_gate:bool, draw:u32,
}
impl Actor {
    fn result_hash(&self,data:&Data)->Result<u32,String> {
        let Some(r)=&self.result else {return Ok(0)};
        let id=u32::from_str_radix(r.trim_start_matches("0x"),16).map_err(|_|"result must be an explicit hex hash")?;
        data.result(id).ok_or("result has no loaded table row")?;
        Ok(id)
    }
    fn ask(&self,data:&Data)->Result<Ask<'_>,String> {
        if !self.position.iter().chain(&self.velocity).chain(&self.forward).all(|v|v.is_finite())
            || self.points.iter().flatten().flatten().any(|v|!v.is_finite())
            || !self.heading.is_finite() || !self.health_iv.is_finite() || self.health_iv<0.
            || self.clip_time_and_step.is_some_and(|[t,d]|!t.is_finite()||!d.is_finite()||t<0.||d<0.||d>0.25)
            || self.lies>4 {return Err("invalid actor state".into())}
        let profile=data.profile(&self.profile)?;
        let p=profile.personality;
        Ok(Ask{fighter:Fighter{player:self.player,health:self.health_iv,attack:p.attack,defend:p.defend,martial:p.melee_percent,
                weapon:self.weapon.as_ref().map(|w|Held{kind:w.kind,flags:w.flags,between_shots:w.between_shots})},
            groups:profile.groups,loaded:&|_|true,receiving_hit:false,blocking:self.blocking,just_struck:self.just_struck,target:None,lock_on:self.lock_on,
            impulses:false,controls:None,setting:0,kind:None,idle:None,pos:self.position,heading:self.heading,opponent_res:0,
            special:Special{on_stairs:self.on_stairs,velocity:self.velocity,flags_240:self.flags_240,
                move_from_idle:self.move_from_idle,point:self.position,forward:self.forward}})
    }
}

/// Identical input + data + draw yields identical output, including buffer-size
/// queries through the SDK. Does not deal damage, create tasks, or play a clip.
pub fn evaluate(data:&Data,json:&str)->Result<String,String> {
    let input:Input=serde_json::from_str(json).map_err(|e|e.to_string())?;
    if input.version!=1{return Err("unsupported combat snapshot version".into())}
    let victim=input.victim.ask(data)?;
    let attacker=Ask{blocking:false,just_struck:false,lock_on:false,..input.attacker.ask(data)?};
    let attacker_result=input.attacker.result_hash(data)?;
    let _victim_result=input.victim.result_hash(data)?;
    let clock=input.attacker.clip_time_and_step.map(|[a,b]|(a,b));
    let opponent=Opponent{task:(attacker_result!=0).then_some((attacker_result,clock)),
        lies:input.attacker.lies,crouched:input.attacker.crouched,ask:&attacker};
    let point=|k:u32,_:[f32;3]|input.attacker.points.get(k as usize).copied().flatten();
    let pick=match input.phase.as_str(){"dispatcher"=>Pick::Dispatcher,"forced"=>Pick::Forced,"request"=>Pick::Request,_=>return Err("unknown reaction phase".into())};
    let ask=Ask{opponent_res:attacker_result,lock_on:matches!(pick,Pick::Request)&&victim.lock_on,
        target:Some(Target{entity_type:3,point:&point,opponent:Some(&opponent),special:attacker.special}),..victim};
    let r=data.select(&ask,pick,input.nm_gate,input.combat_task,input.retreat_task,input.draw)?;
    let clip=r.result.and_then(|r|data.clip(r));
    let route=if r.ragdoll {"natural_motion"}else if r.result.is_some(){"animation"}else{"none"};
    Ok(serde_json::json!({"version":1,"phase":input.phase,"route":route,"action":r.action.map(|a|format!("{:08x}",a.name)),
        "result":r.result.map(|r|format!("{:08x}",r.name)),"clip":clip.as_ref().map(|c|&c.0),"animation_flags":clip.map(|c|c.1),
        "priority":r.result.map(|r|r.priority),"force_immediate":r.result.map(|r|r.force_immediate_reaction),
        "special":r.result.filter(|r|r.special_result!=hash("none")).map(|r|format!("{:08x}",r.special_result)),
        "start_time":r.result.map(|r|r.start_time),"playback_rate":r.result.map(|r|r.playback_rate),
        "blend_in":r.result.map(|r|r.blend_in),"blend_out":r.result.map(|r|r.blend_out),
        "sa_clip_binding_required":route=="animation","damage_applied":false}).to_string())
}
