//! Decision makers (board PD4a / PD10c): the `.ped` and `.cmb` files of `common:/data/decision/`, the store the game
//! loads them into (0x82573A88) and the response pick (0x82573808 -> 0x82571AE0). Read in `rust/docs/events/decisions.md`.
//!
//! Every shipped file is version 19 (`data values19:`); older versions' layouts are not read here.
use crate::rng::Rand;

/// A response: its task type (-1 unused, 200 none), its chance per column (0x82682398: 0 anyone, 1 the player, 2 a
/// friend, 3 a threat), whether it may answer on foot / in a vehicle, and its 12 situation bits (combat files).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Response {
    pub task: i32,
    pub chance: [u8; 4],
    pub on_foot: bool,
    pub in_vehicle: bool,
    pub flags: u16,
}

impl Default for Response {
    fn default() -> Self {
        Response { task: -1, chance: [0; 4], on_foot: false, in_vehicle: false, flags: 0 }
    }
}

/// An event slot (0x58 bytes): the event type (-1 empty) and its six responses.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Slot {
    pub event: i32,
    pub responses: [Response; 6],
}

/// The 14 attributes of line 2 (+0x8F0..+0x8FB), each kept to the bits the game stores it in.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Attributes {
    pub target_loss_response: u8,
    pub can_change_target: bool,
    pub fire_rate: u8,
    pub caution: u8,
    pub low_health: u8,
    pub movement_style: u8,
    pub standing_style: u8,
    pub navigation_style: u8,
    pub retreating_behaviour: u8,
    pub teamwork: u8,
    pub sight_range: u8,
    pub weapon_accuracy: u8,
    pub target_injured_reaction: u8,
    /// +0x8F8 bits 6-7 (no setter); the combat fire subtask (0x82492FF0) needs it 1 or more.
    pub fire_ok: u8,
}

impl Default for Attributes {
    fn default() -> Self {
        Attributes::from_values(&[1, 0, 0, 1, 33, 3, 1, 0, 1, 2, 40, 60, 1, 1])
    }
}

impl Attributes {
    fn from_values(v: &[i32; 14]) -> Self {
        let bits = |x: i32, n: u32| (x as u32 & ((1 << n) - 1)) as u8;
        Attributes {
            target_loss_response: bits(v[0], 2),
            can_change_target: v[1] & 1 != 0,
            fire_rate: bits(v[2], 2),
            caution: bits(v[3], 2),
            low_health: v[4] as u8,
            movement_style: bits(v[5], 3),
            standing_style: bits(v[6], 2),
            navigation_style: bits(v[7], 2),
            retreating_behaviour: bits(v[8], 2),
            teamwork: bits(v[9], 2),
            sight_range: v[10] as u8,
            weapon_accuracy: v[11] as u8,
            target_injured_reaction: bits(v[12], 2),
            fire_ok: bits(v[13], 2),
        }
    }
}

/// What a decision maker answers events with: 26 slots and the attributes.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct DecisionMaker {
    pub slots: [Slot; 26],
    pub attributes: Attributes,
}

impl Default for DecisionMaker {
    fn default() -> Self {
        DecisionMaker { slots: [Slot { event: -1, responses: [Response::default(); 6] }; 26], attributes: Attributes::default() }
    }
}

/// The kind of a store index (byte 0x8307FD40 + index).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Kind {
    Ped = 0,
    Group = 1,
    Combat = 2,
    GroupCombat = 3,
}

impl DecisionMaker {
    /// A file (0x82572128): `data values19:`, the attribute line, then blocks of `event, 4,` and six response lines
    /// (`%d, %f x4, %d, %d, %f x6, %d, %d x12`, 0x820266F8). The chances are cut to bytes; the six floats and the 12 are
    /// dropped. A block goes to the slot holding its event, else the first empty one (0x82571620).
    pub fn parse(text: &str) -> Result<Self, String> {
        let mut lines = text.lines().map(str::trim).filter(|l| !l.is_empty());
        let head = lines.next().ok_or("empty decision file")?;
        let version: i32 = head.strip_prefix("data values").and_then(|v| v.trim_end_matches(':').trim().parse().ok())
            .ok_or_else(|| format!("bad decision header {head:?}"))?;
        if version != 19 {
            return Err(format!("decision file version {version}, only 19 is read"));
        }
        let ints = |l: &str| -> Result<Vec<f32>, String> {
            l.split(',').map(str::trim).filter(|s| !s.is_empty()).map(|s| s.parse::<f32>().map_err(|e| format!("{s:?}: {e}"))).collect()
        };
        let a = ints(lines.next().ok_or("no attribute line")?)?;
        let a: [i32; 14] = a.iter().map(|&x| x as i32).collect::<Vec<_>>().try_into().map_err(|_| "attribute line is not 14 values")?;
        let mut dm = DecisionMaker { attributes: Attributes::from_values(&a), ..Default::default() };
        while let Some(l) = lines.next() {
            let event = ints(l)?.first().copied().ok_or("empty event line")? as i32;
            let mut slot = Slot { event, responses: [Response::default(); 6] };
            for r in slot.responses.iter_mut() {
                let v = ints(lines.next().ok_or("block cut short")?)?;
                if v.len() < 26 {
                    return Err(format!("response line of {} values", v.len()));
                }
                // The file's chances c1..c4 are columns 2, 3, 1, 0 (stored c4, c3, c1, c2).
                let byte = |x: f32| x as i64 as u8;
                *r = Response {
                    task: v[0] as i32,
                    chance: [byte(v[4]), byte(v[3]), byte(v[1]), byte(v[2])],
                    in_vehicle: v[5] as i32 != 0,
                    on_foot: v[6] as i32 != 0,
                    flags: (0..12).filter(|&k| v[14 + k] as i32 != 0).fold(0, |f, k| f | 1 << k),
                };
            }
            let at = dm.slots.iter().position(|s| s.event == event).or_else(|| dm.slots.iter().position(|s| s.event == -1))
                .ok_or("more than 26 events")?;
            dm.slots[at] = slot;
        }
        Ok(dm)
    }

    /// The response to `event` (0x82573328 -> 0x82571AE0): each of the slot's responses with a chance in `column`, a
    /// task, the `on_foot` / `in_vehicle` byte set, every set bit of `mask` among its flags, and a `weights` weight above 0
    /// (none listed: 1) and in `allowed` when that is not empty, takes its chance (times the weight when under 1, cut to
    /// a byte); one is drawn by `rand` / 32767 against the running shares (0x825716B0, 0x82571750). None: 200.
    pub fn pick(&self, event: i32, column: usize, in_vehicle: bool, mask: Option<u16>, weights: &[(i32, f32)], allowed: &[i32],
                rand: &mut Rand) -> i32 {
        let Some(slot) = self.slots.iter().find(|s| s.event == event) else { return 200 };
        let mut taken: Vec<(i32, u8)> = Vec::with_capacity(6);
        for r in &slot.responses {
            let weight = if weights.is_empty() { 1.0 } else { weights.iter().find(|w| w.0 == r.task).map_or(1.0, |w| w.1) };
            let chance = r.chance[column];
            if chance == 0 || mask.is_some_and(|m| m & !r.flags != 0) || r.task == -1 || weight <= 0.0
                || (!allowed.is_empty() && !allowed.contains(&r.task)) || !if in_vehicle { r.in_vehicle } else { r.on_foot } {
                continue;
            }
            taken.push((r.task, if weight < 1.0 { (chance as f32 * weight) as i64 as u8 } else { chance }));
        }
        if taken.is_empty() {
            return 200;
        }
        let total: f32 = taken.iter().map(|t| t.1 as f32).sum();
        let roll = rand.next() as f32 * f32::from_bits(0x3800_0100);
        let mut sum = 0.0f32;
        for (task, chance) in taken {
            sum += chance as f32;
            if roll <= sum * (1.0 / total) {
                return task;
            }
        }
        200
    }
}

/// The files the store loads (0x82573A88), from `common:/data/decision/`: the player's (-2), the population's default
/// (-1), the mission peds' default, then indices 0-33 in order.
pub const PLAYER: &str = "m_plyr.ped";
pub const DEFAULT: &str = "R_Norm.ped";
pub const MISSION_DEFAULT: &str = "m_norm.ped";
pub const INDEXED: [(&str, Kind); 34] = [
    ("GP_Norm.ped", Kind::Group), ("GP_JustFollow.ped", Kind::Group), ("Cop.ped", Kind::Ped), ("R_Norm.ped", Kind::Ped),
    ("Fireman.ped", Kind::Ped), ("m_empty.ped", Kind::Ped), ("Combat/gp_cr.cmb", Kind::GroupCombat),
    ("Combat/gp_no.cmb", Kind::GroupCombat), ("Combat/gp_pr.cmb", Kind::GroupCombat), ("Combat/gp_hide.cmb", Kind::GroupCombat),
    ("Combat/m_no_no.cmb", Kind::Combat), ("Combat/m_Coward.cmb", Kind::Combat), ("Combat/m_no_no.cmb", Kind::Combat),
    ("Combat/m_no_no.cmb", Kind::Combat), ("Combat/cop_nrm.cmb", Kind::Combat), ("Combat/cop_wl1.cmb", Kind::Combat),
    ("Combat/cop_wl2.cmb", Kind::Combat), ("Combat/cop_wl3.cmb", Kind::Combat), ("m_empty.ped", Kind::Ped),
    ("m_norm.ped", Kind::Ped), ("m_tough.ped", Kind::Ped), ("m_weak.ped", Kind::Ped), ("Combat/m_Empty.cmb", Kind::Combat),
    ("Combat/m_Coward.cmb", Kind::Combat), ("Combat/m_no_de.cmb", Kind::Combat), ("Combat/m_no_fi.cmb", Kind::Combat),
    ("Combat/m_hide.cmb", Kind::Combat), ("Combat/m_cr_de.cmb", Kind::Combat), ("Combat/m_cr_no.cmb", Kind::Combat),
    ("Combat/m_cr_fi.cmb", Kind::Combat), ("Combat/m_pr_de.cmb", Kind::Combat), ("Combat/m_pr_no.cmb", Kind::Combat),
    ("Combat/m_pr_fi.cmb", Kind::Combat), ("Combat/m_boss.cmb", Kind::Combat),
];
/// The store's size (0x36): the 34 indexed decision makers, then the scripts' copies.
pub const STORE_SIZE: usize = 0x36;

impl DecisionMaker {
    /// 0x82572060 (CLEAR_*_DECISION_MAKER_EVENT_RESPONSE): the first of the 26 slots whose event is `event` is freed
    /// (event -1, its six responses cleared).
    pub fn clear_event(&mut self, event: i32) {
        if let Some(s) = self.slots.iter_mut().find(|s| s.event == event) {
            *s = Slot { event: -1, responses: [Response::default(); 6] };
        }
    }

    /// 0x825720A8 -> 0x82571350 (ADD_*_DECISION_MAKER_EVENT_RESPONSE): the slot for `event`, else the first free one
    /// (made the event's); the response put into its first empty place (task -1, chances and flags 0).
    pub fn add_response(&mut self, event: i32, task: i32, chance: [u8; 4], on_foot: bool, in_vehicle: bool) {
        let k = self.slots.iter().position(|s| s.event == event).or_else(|| self.slots.iter().position(|s| s.event == -1));
        let Some(k) = k else { return };
        let s = &mut self.slots[k];
        s.event = event;
        let empty = |r: &Response| r.task == -1 && r.chance == [0; 4] && !r.on_foot && !r.in_vehicle;
        if let Some(r) = s.responses.iter_mut().find(|r| empty(r)) {
            *r = Response { task, chance, on_foot, in_vehicle, flags: 0 };
        }
    }
}

/// The scripts' decision makers (the store's indices 34 to 53, made by 0x82573468), their kinds and the threads
/// that hold them, with the handles (0x82202AC0, kind 7: generation << 16 | index; 0x82202DA0 turns one back).
#[derive(Clone, Debug, Default)]
pub struct ScriptStore {
    slots: Vec<Option<(DecisionMaker, Kind, u32)>>,
    generation: Vec<u16>,
}

impl ScriptStore {
    pub const fn new() -> ScriptStore {
        ScriptStore { slots: Vec::new(), generation: Vec::new() }
    }

    fn ensure(&mut self) {
        if self.slots.is_empty() {
            self.slots = vec![None; STORE_SIZE - INDEXED.len()];
            self.generation = vec![0; STORE_SIZE - INDEXED.len()];
        }
    }

    /// 0x82573468: a copy of `of` of `kind` in the first free slot, held by thread `owner`; its handle, or 0 when full.
    pub fn make(&mut self, of: DecisionMaker, kind: Kind, owner: u32) -> i32 {
        self.ensure();
        let Some(k) = self.slots.iter().position(Option::is_none) else { return 0 };
        self.generation[k] = self.generation[k].wrapping_add(1).max(1);
        self.slots[k] = Some((of, kind, owner));
        (self.generation[k] as i32) << 16 | (INDEXED.len() + k) as i32
    }

    /// 0x82202DA0(handle, 7): the decision maker number, -1 for a stale or bad handle (an indexed number passes).
    pub fn number(&self, handle: i32) -> i32 {
        let index = (handle & 0xFFFF) as usize;
        if index < INDEXED.len() {
            return index as i32;
        }
        let k = index - INDEXED.len();
        match (self.slots.get(k), self.generation.get(k)) {
            (Some(Some(_)), Some(&g)) if g as i32 == (handle >> 16) & 0xFFFF => index as i32,
            _ => -1,
        }
    }

    pub fn get(&self, number: i32) -> Option<&(DecisionMaker, Kind, u32)> {
        let k = usize::try_from(number).ok()?.checked_sub(INDEXED.len())?;
        self.slots.get(k)?.as_ref()
    }

    pub fn get_mut(&mut self, number: i32) -> Option<&mut DecisionMaker> {
        let k = usize::try_from(number).ok()?.checked_sub(INDEXED.len())?;
        self.slots.get_mut(k)?.as_mut().map(|s| &mut s.0)
    }

    /// REMOVE_DECISION_MAKER (0x82573A18).
    pub fn remove(&mut self, number: i32) {
        if let Some(k) = usize::try_from(number).ok().and_then(|n| n.checked_sub(INDEXED.len())) {
            if let Some(s) = self.slots.get_mut(k) {
                *s = None;
            }
        }
    }

    /// A thread's end: the decision makers it held go (its kind-10 records).
    pub fn release(&mut self, owner: u32) {
        for s in &mut self.slots {
            if s.as_ref().is_some_and(|x| x.2 == owner) {
                *s = None;
            }
        }
    }
}

/// The empty ped decision maker a dying ped answers with (0x82573940) unless its own is a combat one.
pub const EMPTY: i32 = 5;
/// The intelligence's starting combat (+0xD8) and group combat (+0xDC) indices (0x823C2F0C): m_no_no and gp_no.
pub const COMBAT_DEFAULT: i32 = 12;
pub const GROUP_COMBAT_DEFAULT: i32 = 7;
/// The combat index a ped of the Cop decision maker gets (0x822EFF94: cop_nrm), and a mission ped's (0x822EFF50).
pub const COMBAT_COP: i32 = 14;
pub const COMBAT_MISSION: i32 = 10;

/// The store (word 0x8307FE1C): the indexed decision makers with their kinds, and the three defaults.
#[derive(Clone, Debug)]
pub struct Store {
    pub indexed: Vec<(DecisionMaker, Kind)>,
    pub default: DecisionMaker,
    pub mission_default: DecisionMaker,
    pub player: DecisionMaker,
}

impl Store {
    /// Load every file through `read` (a path under `data/decision/`, case as the game names it).
    pub fn load(read: impl Fn(&str) -> Option<String>) -> Result<Self, String> {
        let one = |name: &str| DecisionMaker::parse(&read(name).ok_or_else(|| format!("no {name}"))?).map_err(|e| format!("{name}: {e}"));
        Ok(Store {
            indexed: INDEXED.iter().map(|&(n, k)| Ok((one(n)?, k))).collect::<Result<_, String>>()?,
            default: one(DEFAULT)?,
            mission_default: one(MISSION_DEFAULT)?,
            player: one(PLAYER)?,
        })
    }

    /// The decision maker of an index (0x825717F8): -2 the player's, -1 the default (the mission one for a mission
    /// ped, byte +0x9B0 2), else the indexed one.
    pub fn get(&self, index: i32, mission: bool) -> Option<&DecisionMaker> {
        match index {
            -2 => Some(&self.player),
            -1 if mission => Some(&self.mission_default),
            -1 => Some(&self.default),
            i => self.indexed.get(usize::try_from(i).ok()?).map(|d| &d.0),
        }
    }

    pub fn kind(&self, index: i32) -> Option<Kind> {
        self.indexed.get(usize::try_from(index).ok()?).map(|d| d.1)
    }

    /// The pick (0x82573808): a ped with health under 100 answers with `EMPTY` unless `index` is a combat kind; the
    /// situation mask is only made for combat kinds (2 or 3); then [`DecisionMaker::pick`].
    #[allow(clippy::too_many_arguments)]
    pub fn pick(&self, index: i32, mission: bool, health: f32, event: i32, column: usize, in_vehicle: bool, mask: u16,
                weights: &[(i32, f32)], allowed: &[i32], rand: &mut Rand) -> i32 {
        self.pick_with(None, index, mission, health, event, column, in_vehicle, mask, weights, allowed, rand)
    }

    /// `pick` with the scripts' decision makers too (numbers 34 and up).
    #[allow(clippy::too_many_arguments)]
    pub fn pick_with(&self, scripts: Option<&ScriptStore>, index: i32, mission: bool, health: f32, event: i32, column: usize, in_vehicle: bool,
                     mask: u16, weights: &[(i32, f32)], allowed: &[i32], rand: &mut Rand) -> i32 {
        let script = scripts.and_then(|s| s.get(index));
        let kind = script.map(|s| s.1).or_else(|| self.kind(index));
        if let Some((dm, k, _)) = script.filter(|_| !(health < 100.0 && kind != Some(Kind::Combat))) {
            let mask = matches!(k, Kind::Combat | Kind::GroupCombat).then_some(mask);
            return dm.pick(event, column, in_vehicle, mask, weights, allowed, rand);
        }
        let index = if health < 100.0 && kind != Some(Kind::Combat) { EMPTY } else { index };
        let mask = matches!(kind, Some(Kind::Combat | Kind::GroupCombat)).then_some(mask);
        self.get(index, mission).map_or(200, |d| d.pick(event, column, in_vehicle, mask, weights, allowed, rand))
    }
}

/// The 12 situation bits of a combat pick (0x825734A0).
pub mod situation {
    pub const LOW_HEALTH: u16 = 1 << 0;
    pub const NOT_LOW_HEALTH: u16 = 1 << 1;
    pub const UNARMED: u16 = 1 << 2;
    pub const ONE_HANDED: u16 = 1 << 3;
    pub const TWO_HANDED: u16 = 1 << 4;
    pub const TARGET_UNARMED: u16 = 1 << 5;
    pub const TARGET_GUN: u16 = 1 << 6;
    pub const TARGET_ARMED: u16 = 1 << 7;
    pub const NOT_GROUP_TARGET: u16 = 1 << 8;
    pub const GROUP_TARGET: u16 = 1 << 9;
    pub const TARGET_IN_VEHICLE: u16 = 1 << 10;
    pub const TARGET_ON_FOOT: u16 = 1 << 11;

    /// The mask (0x825734A0): `low` health under the decision maker's LOW_HEALTH; `own` the ped's weapon bit (UNARMED,
    /// ONE_HANDED or TWO_HANDED); the target's armed state (0 nothing damaging, 1 a melee weapon, 2 a damaging one).
    /// ponytail: any damaging weapon of the target counts as a gun (bit 6); the group's relation (0x825E9CE8) is taken
    /// as not 2 (no group fights here).
    pub fn mask(low: bool, own: u16, target_armed: u8, target_in_vehicle: bool) -> u16 {
        let health = if low { LOW_HEALTH } else { NOT_LOW_HEALTH };
        let target = if target_armed == 2 { TARGET_GUN | TARGET_ARMED } else { TARGET_UNARMED };
        health | own | target | NOT_GROUP_TARGET | if target_in_vehicle { TARGET_IN_VEHICLE } else { TARGET_ON_FOOT }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const FILE: &str = "data values19:\n1, 1, 2, 1, 133, 3, 1, 0, 1, 2, 40, 60, 1, 1, \n108,4,\n\
        1901,38.000000,10.0,20.0,30.0,0,1,0,0,0,0,0,0,12,1,1,0,1,1,1,1,1,1,0,1,1,\n\
        1902,100.000000,100.0,100.0,100.0,0,1,0,0,0,0,0,0,12,1,1,1,1,1,1,1,1,0,1,1,1,\n\
        1904,66.9,66.0,66.0,66.0,1,0,0,0,0,0,0,0,12,1,1,1,1,1,1,1,1,1,1,1,1,\n\
        -1,0.0,0.0,0.0,0.0,0,0,0,0,0,0,0,0,12,0,0,0,0,0,0,0,0,0,0,0,0,\n\
        -1,0.0,0.0,0.0,0.0,0,0,0,0,0,0,0,0,12,0,0,0,0,0,0,0,0,0,0,0,0,\n\
        -1,0.0,0.0,0.0,0.0,0,0,0,0,0,0,0,0,12,0,0,0,0,0,0,0,0,0,0,0,0,\n";

    #[test]
    fn reads_and_picks_as_the_game() {
        let dm = DecisionMaker::parse(FILE).unwrap();
        assert_eq!((dm.attributes.fire_rate, dm.attributes.low_health, dm.attributes.can_change_target), (2, 133, true));
        let fire = dm.slots[0].responses[0];
        // c1..c4 = 38, 10, 20, 30 go to columns 2, 3, 1, 0.
        assert_eq!((fire.task, fire.chance, fire.on_foot, fire.in_vehicle), (1901, [30, 20, 38, 10], true, false));
        assert_eq!(fire.flags, 0b1101_1111_1011);
        assert_eq!(dm.slots[0].responses[2].chance[0], 66);
        let mut r = Rand::new(7);
        // The retreat is vehicle-only; with no mask the draw is fire 30 against advance 100 in column 0.
        let picks: Vec<i32> = (0..400).map(|_| dm.pick(108, 0, false, None, &[], &[], &mut r)).collect();
        let fires = picks.iter().filter(|&&t| t == 1901).count();
        assert!(picks.iter().all(|&t| t == 1901 || t == 1902) && (60..140).contains(&fires), "{fires}");
        // A set mask bit missing from the flags rules a response out: bit 2 (unarmed) leaves the advance.
        assert_eq!(dm.pick(108, 0, false, Some(situation::UNARMED), &[], &[], &mut r), 1902);
        // A weight of 0 rules it out; the allowed list too; an unknown event answers 200.
        assert_eq!(dm.pick(108, 0, false, None, &[(1902, 0.0)], &[], &mut r), 1901);
        assert_eq!(dm.pick(108, 0, false, None, &[], &[1904], &mut r), 200);
        assert_eq!(dm.pick(107, 0, false, None, &[], &[], &mut r), 200);
        // The draw: rand / 32767 against the running shares 30/130 and 1.
        let mut a = Rand::new(99);
        let mut b = a;
        let roll = b.next() as f32 * f32::from_bits(0x3800_0100);
        assert_eq!(dm.pick(108, 0, false, None, &[], &[], &mut a), if roll <= 30.0 / 130.0 { 1901 } else { 1902 });
    }

    #[test]
    fn a_weight_under_one_scales_the_chance_to_a_byte() {
        let dm = DecisionMaker::parse(FILE).unwrap();
        // Fire 30 x 0.01 = 0.3 cuts to 0: it stays eligible but is never drawn.
        let mut r = Rand::new(3);
        assert!((0..200).all(|_| dm.pick(108, 0, false, None, &[(1901, 0.01)], &[], &mut r) == 1902));
    }

    /// A street ped's combat file: unarmed against an unarmed (or club-holding) target it charges; against a gun it
    /// retreats; holding a gun it fires.
    #[test]
    fn the_situation_mask_picks_charge_or_retreat() {
        let file = "data values19:\n1, 1, 1, 1, 133, 3, 1, 0, 1, 2, 40, 60, 1, 1, \n108,4,\n\
            1901,38.0,38.0,38.0,38.0,0,1,0,0,0,0,0,0,12,1,1,0,1,1,1,1,1,1,0,1,1,\n\
            1904,66.0,66.0,66.0,66.0,0,1,0,0,0,0,0,0,12,1,1,1,0,0,0,1,1,1,1,1,1,\n\
            1905,100.0,100.0,100.0,100.0,0,1,0,0,0,0,0,0,12,1,1,1,0,0,1,0,0,1,1,1,1,\n";
        let empty = "-1,0.0,0.0,0.0,0.0,0,0,0,0,0,0,0,0,12,0,0,0,0,0,0,0,0,0,0,0,0,\n";
        let dm = DecisionMaker::parse(&format!("{file}{empty}{empty}{empty}")).unwrap();
        let mut r = Rand::new(5);
        let pick = |m: u16, r: &mut Rand| dm.pick(108, 0, false, Some(m), &[], &[], r);
        assert_eq!(pick(situation::mask(false, situation::UNARMED, 0, false), &mut r), 1905);
        assert_eq!(pick(situation::mask(true, situation::UNARMED, 1, false), &mut r), 1905);
        assert_eq!(pick(situation::mask(false, situation::UNARMED, 2, false), &mut r), 1904);
        assert_eq!(pick(situation::mask(false, situation::ONE_HANDED, 0, false), &mut r), 1901);
    }

    /// A response added to an event goes in its slot's first empty place (a new slot for a new event), clearing the
    /// event frees the slot; a script copy's handle names it until it is removed or its thread ends.
    #[test]
    fn script_decision_makers() {
        let mut d = DecisionMaker::default();
        d.add_response(15, 911, [0, 0, 0, 100], true, false);
        d.add_response(15, 415, [100, 0, 0, 0], true, true);
        let s = d.slots.iter().find(|s| s.event == 15).unwrap();
        assert_eq!((s.responses[0].task, s.responses[1].task, s.responses[1].in_vehicle), (911, 415, true));
        d.clear_event(15);
        assert!(d.slots.iter().all(|s| s.event == -1));
        let mut st = ScriptStore::new();
        let h = st.make(d.clone(), Kind::Combat, 7);
        assert_eq!((h & 0xFFFF, st.number(h)), (34, 34));
        assert_eq!(st.number(3), 3);
        st.remove(34);
        assert_eq!(st.number(h), -1);
        let h2 = st.make(d, Kind::Ped, 7);
        assert!(h2 != h && st.number(h2) == 34);
        st.release(7);
        assert_eq!(st.number(h2), -1);
    }
}
