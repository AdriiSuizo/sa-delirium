//! SA's learned attacks occupy IV's kick input. Original Niko actions stay intact.
use crate::{Data,Group,hash,animation::Bank};
use std::collections::HashMap;
#[derive(Clone,Default)]pub struct Catalog{
    pub keys:HashMap<u32,String>,pub aliases:HashMap<u32,u32>,groups:HashMap<u32,i32>,kick:Vec<(usize,u32)>,style:u32,
}
impl Data{
    pub fn install_gym(&mut self,bank:&Bank)->Result<(),String>{
        if bank.native.is_empty(){return Ok(())}
        if !self.gym.keys.is_empty(){return Err("gym catalog already installed".into())}
        if self.player_groups[6]!=-1{return Err("original player's spare gym group is occupied".into())}
        let template=self.table.actions.iter().find(|a|a.name==hash("low_kick_R_Act")).ok_or("missing kick action")?.clone();
        let specs=[
          (5,["Jab_Res","cross_R_Res","uppercut_L_Res"],[[0x4c3,0x4c2],[0x4d0,0x4c9],[0x4c3,0x4c2]]),
          (6,["low_kick_R_Res","hook_L_Res","hook_R_Res"],[[0x1a9,0x1a8],[0x1a4,0x1a3],[0x1a9,0x1a8]]),
          (7,["knee_Res","knee_Res","knee_Res"],[[0x1a8,0x1a7],[0x4d0,0x4c3],[0x1a8,0x1a7]])];
        for(style,names,bones)in specs{
            let group=self.groups.len() as i32;self.gym.groups.insert(style,group);
            self.groups.push(Group{name:format!("sa_gym_{style}").into_bytes(),dictionary:format!("sa_gym_{style}").into_bytes(),anims:vec![]});
            for step in 1..=3{
                let key=format!("sa_gym_{style}/attack{step}");let clip=bank.native.get(&key).ok_or(format!("missing {key}"))?;
                let name=hash(&format!("sa_gym_{style}_{step}_res"));let act_name=hash(&format!("sa_gym_{style}_{step}_act"));
                let mut result=self.result(hash(names[step-1])).ok_or("missing original reaction family")?.clone();
                result.name=name;result.anim_group=group;result.anim=step as i32;result.playback_rate=1.;result.player_playback_mult=1.;
                result.force_immediate_reaction=style==7&&step>=2;result.strike_bones=[(true,bones[step-1][0],0.07),(false,bones[step-1][1],0.07)];
                result.branches=[(false,0,[0.;2]);3];result.desired_distance=[if style==7{0.65}else{0.85},10.];
                result.distance_range=[0.35,1.5];result.stop_on_crit_frame=false;
                if step<3{result.branches[0]=(true,hash(&format!("sa_gym_{style}_{}_act",step+1)),[clip.chain.min(clip.duration-0.10),clip.duration]);}
                if style==7&&step>=2{result.strike_bones=[(false,0,0.);2];}
                else if style!=7{self.gym.aliases.insert(name,hash(names[step-1]));}
                if style==7&&step>=2{
                    let mut abort=self.table.actions.iter().find(|a|a.name==hash("Victim_Not_playing_counter_Back_Act")).ok_or("missing victim abort")?.clone();
                    abort.name=hash(&format!("sa_gym_{style}_{step}_abort"));abort.opponent_res=hash(&format!("sa_gym_{style}_{step}_receive"));
                    result.branches[1]=(true,abort.name,[0.05,clip.duration]);self.table.actions.push(abort);
                }
                self.gym.keys.insert(name,key);self.table.results.push(result);
                let mut action=template.clone();action.name=act_name;action.move_=name;action.available_from_idle=step==1;action.flags[0]=step==1;
                action.flags[3]=style==7&&step==2; // grab needs the preceding real knee contact
                action.distance_range=[0.,if style==7{1.1}else{1.4}];action.priority=14;action.opponent_pose=2;
                if style==7&&step>=2{
                    // This is a paired transition, not another strike test.
                    // Use the actors' origins for reach: hitd_1 bends its pelvis
                    // below the ordinary upright kick's height window.
                    action.opponent_pose=1;action.rel_point=0;
                }
                if style==7&&step==3{action.opponent_res=hash("sa_gym_7_2_receive");action.distance_range=[0.,clip.reach];}
                self.table.actions.push(action);
                if style==7&&step==1{
                    // The native LV knee and its receiver are a matched setup
                    // for the following grab. Keep IV's front/back/block tests,
                    // replacing only the accepted frontal reaction's clip.
                    let receive=hash("sa_gym_7_1_receive");let key="sa_gym_7/hit1";
                    if !bank.native.contains_key(key){return Err("missing native LV knee receiver".into())}
                    let mut r=self.result(hash("Hit_knee_Res")).ok_or("missing knee reaction")?.clone();
                    r.name=receive;r.playback_rate=1.;r.player_playback_mult=1.;
                    self.gym.keys.insert(receive,key.into());self.table.results.push(r);
                    let reactions:Vec<_>=self.table.actions.iter().filter(|a|a.kind==hash("HitReaction")&&a.opponent_res==hash("knee_Res")).cloned().collect();
                    for mut a in reactions{
                        a.name=hash(&format!("sa_gym_7_knee_reaction_{}",a.name));a.opponent_res=name;
                        if a.move_==hash("Hit_knee_Res"){a.move_=receive;}
                        self.table.actions.push(a);
                    }
                }
                if style==7&&step>=2{
                    let receive=hash(&format!("sa_gym_{style}_{step}_receive"));let key=format!("sa_gym_{style}/hit{step}");
                    if !bank.native.contains_key(&key){return Err(format!("missing paired clip {key}"))}
                    let mut r=self.result(hash("Hit_counter_Back_Res")).ok_or("missing original paired result")?.clone();
                    r.name=receive;r.anim_group=self.groups.iter().position(|g|g.name==b"counters_player").ok_or("missing counters")? as i32;
                    r.branches=[(false,0,[0.;2]);3];r.self_damage=4.;r.sync_start_time=0;r.playback_rate=1.;r.player_playback_mult=1.;
                    // Original paired-result abort contract, with this pair's identities.
                    let mut abort=self.table.actions.iter().find(|a|a.name==hash("Attacker_Not_playing_counter_Back_Act")).ok_or("missing paired abort")?.clone();
                    abort.name=hash(&format!("sa_gym_{style}_{step}_receive_abort"));abort.opponent_res=name;
                    r.branches[0]=(true,abort.name,[0.05,clip.duration]);self.table.actions.push(abort);
                    self.gym.keys.insert(receive,key);self.table.results.push(r);
                    let mut a=self.table.actions.iter().find(|a|a.kind==hash("HitReaction")&&a.opponent_res==hash("counter_Back_Res")).ok_or("missing original paired reaction")?.clone();
                    a.name=hash(&format!("sa_gym_{style}_{step}_receive_act"));a.opponent_res=name;a.move_=receive;a.ai_player_filter=0;a.priority=100;
                    self.table.actions.push(a);
                }
            }
        }
        self.gym.kick=self.table.actions.iter().enumerate().filter(|(_,a)|a.ai_player_filter==1&&a.kind==hash("Melee")&&a.weapon_class==hash("unarmed")&&a.impulses.iter().any(|i|i.check&&i.impulse==5)&&!self.gym.keys.contains_key(&a.move_)).map(|(i,a)|(i,a.ai_player_filter)).collect();
        Ok(())
    }
    pub fn gym_style(&mut self,style:u32){
        if self.gym.style==style{return}self.gym.style=style;
        let group=self.gym.groups.get(&style).copied().unwrap_or(-1);self.player_groups[6]=group;
        for &(i,filter)in &self.gym.kick{self.table.actions[i].ai_player_filter=if group<0{filter}else{3};}
    }
    pub fn reaction_identity(&self,name:u32)->u32{self.gym.aliases.get(&name).copied().unwrap_or(name)}
}
