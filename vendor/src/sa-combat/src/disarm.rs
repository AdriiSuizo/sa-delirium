//! The player's weapon take (IV `DisarmOpponent`, meleetask.rs "His DisarmOpponent (0x822BF448)"; weapons.rs).
//! Chain in the original table: a timed block against a club swing plays Dodge_Club_*_Res, whose branch window
//! (from 0.6 s) offers Disarm_Bat_Act (priority 60) to an unarmed player pressing attack / attack2 with a club-holding ped
//! 0-1.6 m ahead; Disarm_Bat_Res (ForceImmediateReaction, DisarmOpponent) then takes the weapon at its clip's 0x200 event
//! while locked on. The runtime exports that as a hit record of kind 3 (see `runtime::finish_with_health`).
#[cfg(test)]
mod tests {
    use crate::{Data,hash,animation::Bank,controller::{self as ct,Ctx,Foe,Melee,Pad,WeaponView}};
    use bevy_math::Vec3;
    use lsc4_combat::{actiontable::Action,melee::Held};
    fn setup()->(Data,Bank){
        static INIT:std::sync::Once=std::sync::Once::new();
        INIT.call_once(||{let root=std::path::PathBuf::from(std::env::var_os("NM_TEST_ASSETS").expect("NM_TEST_ASSETS"));
            assert!(delirium_data::kv::unlock(&delirium_data::nm_asset::read(&std::fs::read(root.join("fred.xrt")).unwrap())));});
        let dir=std::path::PathBuf::from(std::env::var_os("NM_TEST_COMBAT").expect("NM_TEST_COMBAT"));let read=|n:&str|std::fs::read(dir.join(n)).unwrap();
        (Data::load(&read("meleeanims.dat"),&read("action_table.csv"),&read("pedpersonality.dat")).unwrap(),Bank::load(&dir).unwrap())
    }
    const FISTS:WeaponView=WeaponView{fire:"MELEE",name:"UNARMED",damage_type:"MELEE",slot:"UNARMED",held:Held{kind:0,flags:0,between_shots:0}};
    fn club()->Held{Held{kind:1,flags:1<<9,between_shots:0}}
    fn knife()->Held{Held{kind:3,flags:1<<10,between_shots:0}}
    /// Steps the player's melee from `dodge` with attack pressed from `press_at` seconds; returns (results seen, frames of disarm_foe).
    fn run(d:&Data,bank:&Bank,dodge:&str,foe_weapon:Option<Held>,lock_on:bool,press_at:f32)->(Vec<u32>,Vec<u32>){
        let mut m=Melee::default();m.on=true;
        ct::play_result(&mut m.mv,d,&Action{move_:hash(dodge),..Default::default()},true,&|k|bank.clips.get(k).map(|c|c.duration));
        assert!(m.mv.is_some(),"{dodge} not in the table");
        let (dt,mut pad,mut heading)=(1./30.,Pad::default(),0.0f32);
        let mut seen=vec![];let mut at=vec![];let mut t=0.;
        for f in 0..120u32{
            let buttons=if t>=press_at&&t<press_at+0.1{0x20}else{0};pad.feed(buttons,[127,127],[0,0],f*33);
            // The foe stands 1 m ahead (heading 0 faces +Y), holding its weapon, as the runtime's `Actor::foe` builds it.
            // Once the disarm plays, the runtime's ForceImmediateReaction has the foe on its Hit_disarmed reaction
            // (Disarm's Branch0 "Victim_Not_playing_disarm_*" ends the result if it is not).
            let reaction=[("Disarm_Bat_Res","Hit_disarmed_bat_Res"),("Disarm_bl_L_Res","Blade_Hit_Disarmed_L_Res"),("Disarm_bl_R_Res","Blade_Hit_Disarmed_R_Res"),("Disarm_bl_Back_Res","Blade_Hit_Disarmed_Back_Res")]
                .iter().find(|(taker,_)|seen.contains(&hash(taker))).map(|(_,foe)|(d.reaction_identity(hash(foe)),Some((0.5,dt))));
            let foe=Foe{feet:Vec3::new(0.,1.,0.)-Vec3::Z,health:200.,weapon:foe_weapon,task:reaction,..Default::default()};
            let ctx=Ctx{pad:&pad,pos:Vec3::ZERO,health:200.,weapon:&FISTS,foe:Some(foe),me:None,lock_on,flags_240:0,on_stairs:false,bones:[None;5],lies:4,foe_pad:None,foe_task:None,in_hand:true};
            m.step(d,&ctx,&mut heading,dt,&|k|bank.clips.get(k),&mut||0x1234);
            if let Some(mv)=&m.mv{let n=d.table.results[mv.result].name;if seen.last()!=Some(&n){seen.push(n);}}
            if m.disarm_foe{at.push(f);}
            t+=dt;
        }
        (seen,at)
    }
    #[test]
    #[ignore]
    fn probe_clips(){
        let (d,bank)=setup();
        for n in ["Disarm_Bat_Res","Disarm_bl_R_Res","Disarm_bl_L_Res","Disarm_bl_Back_Res","Hit_disarmed_bat_Res","Blade_Hit_Disarmed_L_Res"]{
            let r=d.result(hash(n)).unwrap();let k=d.clip(r);
            let c=k.as_ref().and_then(|k|bank.clips.get(&k.0));
            eprintln!("{n}: key={:?} in_bank={} dur={:?} events={:?} disarm={} stop_time={} force={}",k,c.is_some(),c.map(|c|c.duration),c.map(|c|c.events.iter().enumerate().filter(|e|*e.1!=0).map(|(i,e)|(i,*e)).collect::<Vec<_>>()),r.disarm_opponent,r.stop_time,r.force_immediate_reaction);
        }
    }
    /// The front of the chain: the foe's club swing, the player's block in its window, the dodge it picks.
    #[test]
    fn a_timed_block_against_each_club_swing_picks_a_club_dodge(){
        let (d,bank)=setup();
        for (swing,dodge) in [("Club_Hook_L_Res","Dodge_Club_L_Res"),("Club_Hook_R_Res","Dodge_Club_R_Res"),("Club_Uppercut_L_Res","Dodge_Club_Back_Res"),("Club_Batbutt_Res","Dodge_Club_L_Res")]{
            let (mut m,mut pad,mut heading)=(Melee::default(),Pad::default(),0.0f32);m.on=true;let mut got=None;
            for f in 0..12u32{
                pad.feed(if f>=4{0x40}else{0},[127,127],[0,0],f*33);
                let task=Some((d.reaction_identity(hash(swing)),Some((0.2,0.03))));
                let foe=Foe{feet:Vec3::new(0.,1.,0.)-Vec3::Z,health:200.,weapon:Some(club()),task,..Default::default()};
                let ctx=Ctx{pad:&pad,pos:Vec3::ZERO,health:200.,weapon:&FISTS,foe:Some(foe),me:None,lock_on:true,flags_240:0,on_stairs:false,bones:[None;5],lies:4,foe_pad:None,foe_task:None,in_hand:true};
                m.step(&d,&ctx,&mut heading,0.033,&|k|bank.clips.get(k),&mut||0x1234);
                if let Some(mv)=&m.mv{got=Some(d.table.results[mv.result].name);break;}
            }
            assert_eq!(got,Some(hash(dodge)),"{swing}");
        }
    }
    #[test]
    fn a_dodge_then_attack_takes_a_clubs_weapon(){
        let (d,bank)=setup();
        let id=|s:&str|hash(s);
        for dodge in ["Dodge_Club_L_Res","Dodge_Club_R_Res","Dodge_Club_Back_Res"]{
            let (seen,at)=run(&d,&bank,dodge,Some(club()),true,0.7);
            assert!(seen.contains(&id("Disarm_Bat_Res")),"{dodge}: no disarm picked, results {seen:?}");
            assert!(!at.is_empty(),"{dodge}: the disarm result never reached its 0x200 event while locked on");
            // Not locked on (+0x20 clear), the weapon stays (the original's own condition).
            assert!(run(&d,&bank,dodge,Some(club()),false,0.7).1.is_empty(),"{dodge}: took the weapon unlocked");
            // Too early (the branch opens at 0.6 s) or a foe with bare hands: no disarm result at all.
            assert!(!run(&d,&bank,dodge,Some(club()),true,0.2).0.contains(&id("Disarm_Bat_Res")),"{dodge}: disarmed from a press before the window");
            assert!(!run(&d,&bank,dodge,None,true,0.7).0.contains(&id("Disarm_Bat_Res")),"{dodge}: disarmed a bare-handed foe");
        }
    }
    /// The blade chain: a timed block against a knife swing picks a Dodge_bl_* (Dodge_bl_* rows: Blade_*_Res -> Dodge_bl_R / Back).
    #[test]
    fn a_timed_block_against_knife_swings_picks_a_blade_dodge(){
        let (d,bank)=setup();
        for (swing,dodge) in [("Blade_Near_S_A_Res","Dodge_bl_R_Res"),("Blade_Near_L_A_Res","Dodge_bl_Back_Res"),("Blade_Long_A_Res","Dodge_bl_R_Res")]{
            let (mut m,mut pad,mut heading)=(Melee::default(),Pad::default(),0.0f32);m.on=true;let mut got=None;
            for f in 0..12u32{
                pad.feed(if f>=4{0x40}else{0},[127,127],[0,0],f*33);
                let task=Some((d.reaction_identity(hash(swing)),Some((0.2,0.03))));
                let foe=Foe{feet:Vec3::new(0.,1.,0.)-Vec3::Z,health:200.,weapon:Some(knife()),task,..Default::default()};
                let ctx=Ctx{pad:&pad,pos:Vec3::ZERO,health:200.,weapon:&FISTS,foe:Some(foe),me:None,lock_on:true,flags_240:0,on_stairs:false,bones:[None;5],lies:4,foe_pad:None,foe_task:None,in_hand:true};
                m.step(&d,&ctx,&mut heading,0.033,&|k|bank.clips.get(k),&mut||0x1234);
                if let Some(mv)=&m.mv{got=Some(d.table.results[mv.result].name);break;}
            }
            assert_eq!(got,Some(hash(dodge)),"{swing}");
        }
    }
    /// Disarm_bl_*_Act needs a knife-holding foe (Test_is_char_armed_bl_Act, class `knife`); a club foe never gives one.
    #[test]
    fn a_dodge_then_attack_takes_a_knife(){
        let (d,bank)=setup();
        let blade=|seen:&[u32]|["Disarm_bl_L_Res","Disarm_bl_R_Res","Disarm_bl_Back_Res"].iter().any(|n|seen.contains(&hash(n)));
        for dodge in ["Dodge_bl_R_Res","Dodge_bl_Back_Res"]{
            let (seen,at)=run(&d,&bank,dodge,Some(knife()),true,0.7);
            assert!(blade(&seen),"{dodge}: no blade disarm picked, results {seen:?}");
            assert!(!at.is_empty(),"{dodge}: the blade disarm never reached its 0x200 event while locked on");
            assert!(run(&d,&bank,dodge,Some(knife()),false,0.7).1.is_empty(),"{dodge}: took the knife unlocked");
            assert!(!blade(&run(&d,&bank,dodge,Some(club()),true,0.7).0),"{dodge}: blade disarm against a club");
        }
    }
}
