//! Host-independent SA boundary for the pinned LSC4 combat rules.
//! No SA task IDs, pose heuristics, health guesses, or clip-name substitutions here.
//! The host must supply observed state and explicitly map its actors to IV profiles.
use lsc4_combat::{actiontable::{self, Action, ActionTable, ResultDef}, melee, pedpersonality::{self, Personality}};
use std::collections::HashMap;
pub mod query;
pub mod controller;
pub mod animation;
pub mod runtime;
pub mod geometry;
pub mod shove;
pub mod locomotion;
pub mod native_animation;
pub mod gym;
pub mod stance;
pub mod npc_decision;
// Source TU8 queue. SA accounting and source task adaptation are separate owners.
pub mod original_event_queue;
pub mod melee_health;

pub fn hash(s: &str) -> u32 { lsc4_combat::map::hash_bytes(s.as_bytes(), 0) }

#[derive(Clone)]pub struct Group {
    pub name: Vec<u8>, pub dictionary: Vec<u8>, pub anims: Vec<(Vec<u8>, u32, u32)>,
}
#[derive(Clone)]pub struct Data {
    pub player: Personality,
    pub player_groups: [i32;7],
    pub table: ActionTable,
    pub groups: Vec<Group>,
    personalities: HashMap<u32, Personality>,
    pub gym:gym::Catalog,
}
pub struct Profile { pub personality: Personality, pub groups: [i32; 7] }
#[derive(Clone,Copy)]
pub enum Pick { Dispatcher, Forced, Request }

impl Data {
    /// Audited host range overlay. Only initial player punches may change their
    /// distance bands; results, hit windows, damage and reaction choices stay intact.
    pub fn adapt_reach(&mut self,bytes:&[u8])->Result<(),String>{
        #[derive(serde::Deserialize)]struct Row{action:String,original:[f32;2],range:[f32;2]}
        #[derive(serde::Deserialize)]struct Profile{version:u32,rows:Vec<Row>}
        let p:Profile=serde_json::from_slice(bytes).map_err(|e|e.to_string())?;
        if p.version!=1||p.rows.is_empty(){return Err("unsupported combat range profile".into())}
        let mut table=self.table.clone();let mut seen=std::collections::HashSet::new();
        for row in p.rows{
            if !seen.insert(hash(&row.action))||row.range.iter().any(|x|!x.is_finite()||*x<0.)||row.range[0]>=row.range[1]{return Err("invalid combat range row".into())}
            let a=table.actions.iter_mut().find(|a|a.name==hash(&row.action)).ok_or("unknown range action")?;
            if a.distance_range!=row.original||!matches!(a.ai_player_filter,1|2)||a.kind!=hash("Melee")||!a.available_from_idle||
                a.flags[1..].iter().any(|b|*b)||a.opponent_res!=0||a.weapon_class!=hash("unarmed")||a.rel_point!=2{
                return Err(format!("range profile does not match an initial unarmed player or AI action: {}",row.action))
            }
            a.distance_range=row.range;
        }
        self.table=table;Ok(())
    }
    pub fn load(anims: &[u8], actions: &[u8], personalities: &[u8]) -> Result<Self, String> {
        // Same group ordering and entry IDs as pinned viewer/meleetask::load.
        let mut groups = vec![];
        for g in lsc4_combat::meleeanims::read(anims).ok_or("malformed melee animation groups")? {
            groups.push(Group { name: g.name.clone(), dictionary: g.dictionary.clone(),
                anims: g.anims.iter().map(|a| (a.name.clone(), a.entry[0], a.entry[1])).collect() });
            if g.trans { groups.push(Group { name: g.trans_name(), dictionary: g.dictionary, anims: vec![] }); }
        }
        if groups.is_empty() { return Err("no melee animation groups".into()); }
        let group = |n: &[u8]| groups.iter().position(|g| g.name.eq_ignore_ascii_case(n)).map_or(-1, |i| i as i32);
        let table = actiontable::read(actions, group, |g, n| {
            usize::try_from(g).ok().and_then(|i| groups.get(i))
                .and_then(|g| g.anims.iter().find(|a| a.0.eq_ignore_ascii_case(n))).map_or(-1, |a| a.1 as i32)
        });
        if !table.completed || table.actions.is_empty() || table.results.is_empty() {
            return Err("incomplete or empty action table".into());
        }
        let mut models = HashMap::new();
        // Register actual rows first. Unknown names must NOT silently receive a
        // default personality with no animation groups (which changes the NM gate).
        for line in String::from_utf8_lossy(personalities).lines() {
            let line = line.trim();
            if line.is_empty() || line.starts_with('#') { continue; }
            if let Some(name) = line.split(|c: char| c == ',' || c.is_whitespace()).find(|n| !n.is_empty()) {
                models.insert(hash(&name.to_ascii_lowercase()), Personality::default());
            }
        }
        pedpersonality::read(personalities, &mut models, |_, _| false);
        models.retain(|_, p| p.flags & pedpersonality::HAS_PERSONALITY != 0);
        if models.is_empty() { return Err("no resolved personalities".into()); }
        let player=models.get(&hash("player")).ok_or("player personality missing")?.clone();
        let player_groups=player.groups.clone().map(|g|group(&g));
        Ok(Self { player,player_groups,table, groups, personalities: models,gym:Default::default() })
    }
    pub fn opponent_depth(&self) -> usize {
        fn hops(t: &ActionTable, a: &Action, left: usize) -> usize {
            let find = |n: u32| (n != 0 && left > 0).then(|| t.actions.iter().find(|b| b.name == n)).flatten();
            let o = find(a.opponents_action_to_test).map_or(0, |b| 1 + hops(t, b, left - 1));
            o.max(find(a.additional_action_test).map_or(0, |b| hops(t, b, left - 1)))
        }
        let n = self.table.actions.len();
        1 + self.table.actions.iter().filter(|a| a.opponents_action_to_test != 0).map(|a| hops(&self.table, a, n)).max().unwrap_or(0)
    }

    pub fn profile(&self, name: &str) -> Result<Profile, String> {
        let p = self.personalities.get(&hash(&name.to_ascii_lowercase())).ok_or_else(|| format!("unmapped IV profile: {name}"))?;
        let groups = p.groups.clone().map(|n| self.groups.iter().position(|g| g.name.eq_ignore_ascii_case(&n)).map_or(-1, |i| i as i32));
        Ok(Profile { personality: p.clone(), groups })
    }
    pub fn result(&self, name: u32) -> Option<&ResultDef> { self.table.results.iter().rfind(|r| r.name == name) }
    pub fn result_of(&self,action:&actiontable::Action)->Option<usize>{ (action.move_!=0).then(||self.table.results.iter().rposition(|r|r.name==action.move_)).flatten() }
    pub fn clip_key(&self,r:&ResultDef)->Option<String>{self.clip(r).map(|c|c.0)}
    pub fn clip(&self, r: &ResultDef) -> Option<(String, u32)> {
        if let Some(k)=self.gym.keys.get(&r.name){return Some((k.clone(),0x600))}
        let g = self.groups.get(usize::try_from(r.anim_group).ok()?)?;
        let a = g.anims.iter().find(|a| a.1 as i32 == r.anim)?;
        Some((format!("{}/{}", String::from_utf8_lossy(&g.dictionary), String::from_utf8_lossy(&a.0)).to_ascii_lowercase(), a.2))
    }
    pub fn inventory(&self) -> serde_json::Value {
        let rows: Vec<_> = self.table.results.iter().map(|r| {
            let clip = self.clip(r);
            serde_json::json!({"result":format!("{:08x}",r.name), "clip":clip.as_ref().map(|c|&c.0),
                "flags":clip.map(|c|c.1), "strike_bones":r.strike_bones, "damage":r.on_hit_damage,
                "branches":r.branches, "priority":r.priority, "special_result":format!("{:08x}",r.special_result),
                "start_time":r.start_time,"playback_rate":r.playback_rate,"partial_mapping_required":true})
        }).collect();
        serde_json::json!({"actions":self.table.actions.len(),"results":rows,"groups":self.groups.len(),
            "personalities":self.personalities.len(),"source_revision":"380c8cb7d49a9b0c941d0ce8ffb58a0963e4c21e",
            "sa_animation_bindings_verified":false})
    }
    /// Exact upstream selector. State is in IV units; conversion belongs to the
    /// host and is recorded there. Never infer Combat from an active strike alone.
    pub fn hit<'a>(&'a self, ask: &melee::Ask, gate: bool, combat: bool, retreat: bool, draw: u32) -> Result<melee::Reaction<'a>, String> {
        self.select(ask,Pick::Dispatcher,gate,combat,retreat,draw)
    }
    pub fn select<'a>(&'a self, ask: &melee::Ask, pick: Pick, gate: bool, combat: bool, retreat: bool, draw: u32) -> Result<melee::Reaction<'a>, String> {
        if !ask.fighter.health.is_finite() || !ask.fighter.attack.is_finite() || !ask.fighter.defend.is_finite()
            || !ask.fighter.martial.is_finite() || !ask.heading.is_finite() || !ask.pos.iter().all(|x|x.is_finite()) {
            return Err("non-finite combat state".into());
        }
        if ask.opponent_res != 0 && self.result(ask.opponent_res).is_none() {
            return Err("unknown attacker result; cannot select a matching hit reaction".into());
        }
        if ask.groups.iter().any(|&i| i < -1 || i as usize >= self.groups.len() && i != -1) {
            return Err("animation group outside loaded table".into());
        }
        if matches!(pick,Pick::Dispatcher) {
            return Ok(melee::hit_reaction(&self.table, ask, gate, [combat, retreat], || draw));
        }
        // Pinned viewer/meleetask::react: the result ctor's forced pick is not
        // the dispatcher's idle/receiving-hit query. An existing melee task's
        // request carries its own block/struck/lock state and no second NM gate.
        let next=match pick {
            Pick::Forced=>melee::Ask{kind:Some(hash("HitReaction")),idle:None,receiving_hit:false,blocking:false,just_struck:false,lock_on:false,impulses:ask.fighter.player,..*ask},
            Pick::Request=>melee::Ask{kind:Some(hash("HitReaction")),idle:Some(true),receiving_hit:true,..*ask},
            Pick::Dispatcher=>unreachable!(),
        };
        let action=melee::choose(&self.table,&next,||draw);
        let result=action.and_then(|a|self.result(a.move_));
        Ok(melee::Reaction{action,result,ragdoll:false})
    }
}

/// Priority arbitration is shared with Claude; the host keeps the current clip
/// and its clock if a lower-priority request is refused. No arbitrary time delay.
pub use lsc4_combat::melee_task::{request_takes, request_way};

pub use lsc4_combat::pedbodies;
pub use lsc4_combat::combatabort;
