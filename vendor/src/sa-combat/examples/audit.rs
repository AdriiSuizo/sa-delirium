use sa_combat::{Data, hash};
use lsc4_combat::melee::{Ask,Fighter,Special,Opponent,Target};
use std::{fs, path::PathBuf};
fn main() -> Result<(), Box<dyn std::error::Error>> {
    let args: Vec<_> = std::env::args_os().skip(1).collect();
    if args.len()!=2 {return Err("audit DATA_DIR OUTPUT_JSON".into());}
    let dir=PathBuf::from(&args[0]);
    let data=Data::load(&fs::read(dir.join("MeleeAnims.dat"))?, &fs::read(dir.join("action_table.csv"))?, &fs::read(dir.join("pedpersonality.dat"))?)?;
    let profile=data.profile("player")?;
    let base=Ask{fighter:Fighter{player:false,health:200.,attack:profile.personality.attack,defend:profile.personality.defend,
        martial:profile.personality.melee_percent,weapon:None},groups:profile.groups,loaded:&|_|true,receiving_hit:false,blocking:false,
        just_struck:false,target:None,lock_on:true,impulses:false,controls:None,setting:0,kind:None,idle:Some(true),
        pos:[0.;3],heading:0.,opponent_res:0,special:Special::default()};
    let attacker=Ask{fighter:Fighter{player:true,..base.fighter},pos:[0.,1.,0.],..base};
    let opp=Opponent{task:None,lies:4,crouched:false,ask:&attacker};
    let point=|kind:u32,_:[f32;3]| if kind<=7 {Some([0.,1.,match kind {4=>1.6,5=>1.,6|7=>0.5,_=>0.}])} else {None};
    let mut cases=vec![];
    for attack in data.table.results.iter().filter(|r|r.strike_bones.iter().any(|b|b.0)) {
        for health in [200.,124.,0.] {for combat in [true,false] {for blocking in [false,true] {
            let ask=Ask{fighter:Fighter{health,..base.fighter},blocking,opponent_res:attack.name,
                target:Some(Target{entity_type:3,point:&point,opponent:Some(&opp),special:Special::default()}),..base};
            let reaction=data.hit(&ask,true,combat,false,0x4000)?;
            if !combat && health <125. {assert!(reaction.ragdoll);}
            let gate_off=data.hit(&ask,false,combat,false,0x4000)?;
            assert!(!gate_off.ragdoll,"closed NM gate must not create a ragdoll");
            cases.push(serde_json::json!({"attack":format!("{:08x}",attack.name),"attack_clip":data.clip(attack).map(|c|c.0),
                "health_iv":health,"combat":combat,"blocking":blocking,"nm":reaction.ragdoll,
                "reaction":reaction.result.map(|r|format!("{:08x}",r.name)),"clip":reaction.result.and_then(|r|data.clip(r)).map(|c|c.0)}));
        }}}
    }
    assert!(!cases.is_empty());
    assert!(data.profile("SA_MODEL_NOT_EXPLICITLY_MAPPED").is_err());
    assert!(data.hit(&Ask{opponent_res:hash("unmapped_sa_punch"),..base},true,true,false,0).is_err());
    assert!(data.hit(&Ask{fighter:Fighter{health:f32::NAN,..base.fighter},..base},true,true,false,0).is_err());
    // The ABI snapshot must retain the dispatch/forced/request distinction.
    // A forced animated reaction is not another run through the health gate.
    let actor=serde_json::json!({"profile":"player","player":false,"health_iv":124.,"weapon":null,
        "position":[0.,0.,0.],"heading":0.,"velocity":[0.,0.,0.],"forward":[0.,1.,0.],
        "blocking":false,"just_struck":false,"lock_on":false,"on_stairs":false,"flags_240":0,"move_from_idle":false,
        "lies":4,"crouched":false,"points":[[0.,0.,0.],[0.,0.,0.],[0.,0.,0.],[0.,0.,0.],[0.,0.,1.6],[0.,0.,1.],[0.,0.,0.5],[0.,0.,0.5]],
        "result":null,"clip_time_and_step":null});
    let mut input=serde_json::json!({"version":1,"phase":"dispatcher","victim":actor,"attacker":actor,
        "combat_task":true,"retreat_task":false,"nm_gate":true,"draw":16384});
    let abi_snapshot=input.clone();
    let mut query_cases=vec![];
    for phase in ["dispatcher","forced","request"] {
        input["phase"]=serde_json::json!(phase);
        let json=input.to_string();let result=sa_combat::query::evaluate(&data,&json)?;
        assert_eq!(result,sa_combat::query::evaluate(&data,&json)?,"query must not consume mutable RNG");
        let out:serde_json::Value=serde_json::from_str(&result)?;
        if phase=="dispatcher" {assert_eq!(out["route"],"natural_motion")}
        else {assert_ne!(out["route"],"natural_motion")}
        query_cases.push(out);
    }
    input["attacker"]["clip_time_and_step"]=serde_json::json!([0.8,1./60.]);
    assert!(sa_combat::query::evaluate(&data,&input.to_string()).is_ok(),"clip time is not limited by this frame's dt");
    input["attacker"]["profile"]=serde_json::json!("SA_unmapped");
    assert!(sa_combat::query::evaluate(&data,&input.to_string()).is_err());
    let mut inventory=data.inventory();inventory["decision_cases"]=serde_json::json!(cases);
    inventory["phase_cases"]=serde_json::json!(query_cases);
    fs::write(PathBuf::from(&args[1]).with_file_name("hit-snapshot.json"),serde_json::to_vec_pretty(&abi_snapshot)?)?;
    fs::write(&args[1],serde_json::to_vec_pretty(&inventory)?)?;
    println!("{} actions; {} results; {} deterministic decision cases; unknown mappings rejected",data.table.actions.len(),data.table.results.len(),cases.len());
    Ok(())
}
