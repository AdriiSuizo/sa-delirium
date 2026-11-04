//! A ped's event queue (CEventGroup, intelligence +0x84) and the handler's once-a-pass walk of it (0x824760D0), on plain
//! data so any ped stand-in can have one (Xbox 360 exe, TU8; board V10: the viewer's computer drivers). Read in
//! `docs/events/decisions.md` ("The event queue", "Handling the queue"). `events::Events` is the same queue wired to the
//! task manager of a real `Ped`; this one leaves every event's own slots and every task to the caller ([`Asks`],
//! [`Handler`]).
//!
//! - [`Queue::add`] 0x824C25A8(queue, event, keep, replace): the network refusal, the decision maker's pick (asked when
//!   the event has one: vf +0x30), the blocking flag (ped +0x230 bit 15) and the "concerns me" test (0x82302630) say if
//!   it goes in ([`Admit`]); the side effects (the group's copy 0x82302C08, the friends told 0x8230B3F0, the vehicle's
//!   occupants 0x82302CA8, the "heard" note 0x823C1C68) are returned ([`Added`]). A copy is stored with byte +8 = `keep`
//!   while there is room (16); a CEventGivePedTask (124) first removes the others. Full: with `replace` the lowest event
//!   ([`Queue::lowest`] 0x824C21D8: the last of the lowest priorities from 0x69 down, never a script command) gives way
//!   to a strictly higher one. A queue with no ped (the global group) takes anything with room, nothing when full.
//! - [`Queue::handle`] 0x824760D0, once per intelligence update: communicate events (98) that are ready go (0x824C2A20);
//!   the event to answer is picked ([`Queue::pick`] 0x824C2270: the highest valid, ready priority that concerns the ped,
//!   the last of equals, a script command only when strictly higher; every other event is asked if it is ready); every
//!   event ages by 1 (0x824C2050); a picked event of the type whose response runs is dropped when that response may not
//!   be cut short (vf +0x48); else, if it may override (0x8246E1C0), its response is built (0x8246EF28), the tree
//!   running is asked to abort (0x82164FB8) and the event is answered (0x8246DFD8 / 0x8246E128 note it, 0x8246EB90
//!   gives the tasks) and leaves the queue: the response task starts in this same pass. Events no longer valid go at the
//!   end (0x824C2118, 0x824C2090).
//! - Expiry: an event is valid (default vf +0x44, 0x823026B0) while its age is at most its lifetime (vf +0xC, 0 for every
//!   class) or it was added with `keep`: so an event that is not answered is gone after the pass that saw it, unless kept
//!   (a script command, added with keep and replace by 0x821FFAF0, waits until it is answered). A refused answer gives
//!   the age back (-1), so the event stays for the next pass.
//! - The gunshot events' ready / valid (0x823031F0 / 0x82303300) are [`Wait`]: a ped with no task in its physical and
//!   event response slots waits rand(0, 0.3) s before it reacts.
//!
//! Checked by `tools/xbox360-re/checks/evqueue.py` (0x824C25A8, 0x824760D0 and the gunshot slots run in ppcrun on fake
//! events whose slots answer from the case) against `examples/evqueue_cases.rs`.
//!
//! ponytail: the response tasks, the decision maker and the events' own tests are the caller's; the record a response
//! leaves (0x8246DFD8 / 0x8246E128) is written by the caller's [`Handler::note`].

/// The queue's size.
pub const MAX: usize = 16;
/// The decision maker's "no response" (event +0x10).
pub const NO_RESPONSE: i32 = 200;
/// Event types the queue code names.
pub const DAMAGE: u32 = 9;
pub const SCRIPT_COMMAND: u32 = 32;
pub const COMMUNICATE: u32 = 98;
pub const GIVE_PED_TASK: u32 = 124;
pub const MELEE_ACTION: u32 = 125;
/// The longest gunshot wait, s (0x82A9B5A4).
pub const GUNSHOT_WAIT: f32 = 0.3;

/// A queued event: its type (vf +4), priority (vf +8), lifetime (vf +0xC), age (+4) and keep byte (+8).
#[derive(Clone, Debug)]
pub struct Ev<T> {
    pub kind: u32,
    pub priority: i32,
    pub lifetime: i32,
    pub age: i32,
    pub keep: bool,
    pub data: T,
}

impl<T> Ev<T> {
    pub fn new(kind: u32, priority: i32, data: T) -> Ev<T> {
        Ev { kind, priority, lifetime: 0, age: 0, keep: false, data }
    }

    /// 0x823026B0, the default vf +0x44.
    pub fn still_valid(&self) -> bool {
        self.keep || self.age <= self.lifetime
    }
}

/// The event's slots the queue asks.
pub trait Asks<T> {
    /// vf +0x14 (asked with the ped).
    fn ready(&mut self, e: &mut Ev<T>) -> bool;
    /// vf +0x44.
    fn valid(&mut self, e: &Ev<T>) -> bool;
    /// 0x82302630: vf +0x20 then vf +0x24 with the ped.
    fn concerns(&mut self, e: &Ev<T>) -> bool;
}

/// What 0x824C25A8 learns before it stores the event.
#[derive(Clone, Copy, Debug, Default)]
pub struct Admit {
    /// The queue has a ped (+4); a ped's own queue always does.
    pub ped: bool,
    /// The ped's network object refuses events (`tasks::NetObject::refuses_events`).
    pub refused: bool,
    /// vf +0x30: the event asks the decision maker.
    pub decision: bool,
    /// The trial picks (0x8230B258) for 1200 and 1700.
    pub trials: [bool; 2],
    /// The real pick (0x8230B0D0, event +0x10).
    pub response: i32,
    /// Event byte +0xC.
    pub byte_c: bool,
    /// Event byte +0x38 bit 7 (a damage event's).
    pub bit38: bool,
    /// Ped +0x230 bit 15: blocking of non-temporary events.
    pub blocking: bool,
    /// vf +0x4C.
    pub temporary: bool,
    /// 0x82302630.
    pub concerns: bool,
}

/// What 0x824C25A8 did.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Added {
    pub stored: bool,
    /// 0x82302C08: a copy to the ped's group (1200 picked).
    pub group: bool,
    /// 0x8230B3F0: nearby friends told (1700 picked, not a damage event with +0x38 bit 7).
    pub friends: bool,
    /// 0x82302CA8: copies to the vehicle's driver and passengers (its own tests).
    pub vehicle: bool,
    /// 0x823C1C68(intelligence, type, priority): the "heard" note.
    pub heard: bool,
}

/// The handler's records (+0x20 ..): the response root noted (+0x20), the events whose responses run (+0x24
/// non-temporary, +0x28 temporary), one waiting to become the non-temporary one (+0x2C) and the +0x34 response's (+0x30).
#[derive(Clone, Debug)]
pub struct Records<R> {
    pub note: u32,
    pub nontemp: Option<R>,
    pub temp: Option<R>,
    pub pending: Option<R>,
    pub other: Option<R>,
}

impl<R> Default for Records<R> {
    fn default() -> Self {
        Records { note: 0, nontemp: None, temp: None, pending: None, other: None }
    }
}

/// A record's slots: its type (vf +4) and vf +0x48 (an event of its type may cut its response short).
pub trait Record {
    fn kind(&self) -> u32;
    fn stacks(&self) -> bool;
}

/// The ped's task tree as the pass sees it: the five primary slots' roots (0 none) and secondary slot 0's
/// (0x82419A48(tree, 0)).
#[derive(Clone, Copy, Debug, Default)]
pub struct Tree {
    pub slots: [u32; 5],
    pub secondary: u32,
}

/// What the dispatcher (0x8246EF28) built: a main task (handler +0xC) and a secondary one (+8).
#[derive(Clone, Copy, Debug, Default)]
pub struct Reply {
    pub main: bool,
    pub secondary: bool,
}

/// What the pass asks of the handler, the tree and the tasks.
pub trait Handler<T, R>: Asks<T> {
    /// 0x82305F68: does `e` override the response of the event `r`?
    fn overrides(&mut self, e: &Ev<T>, r: &R) -> bool;
    /// vf +0x4C.
    fn temporary(&mut self, e: &Ev<T>) -> bool;
    /// 0x8246EF28; may set handler +0x1C (`slot2`).
    fn respond(&mut self, e: &Ev<T>, slot2: &mut bool) -> Reply;
    /// 0x821778A0(handler +0x34): the third response is set.
    fn other(&mut self) -> bool;
    /// 0x82158ED8(tree): the task it is asked of (0 none).
    fn other_tree(&mut self) -> u32;
    /// 0x82164FB8(task, ped, urgency, event): may it abort? A task's vf +0x14 may write the event (the Combat's refusal
    /// 0x8248B394 ages it a step).
    fn ask(&mut self, task: u32, urgency: u32, e: &mut Ev<T>) -> bool;
    /// The ragdoll retry's tests (0x82476410..0x824764C4): the ped is on its ragdoll (+0x38 = +0x704, not 0), the event's
    /// vf +0x18, the leaf (0x8220B3D0) not type 0x840, no CTaskSimpleNM (0x82A009C0), not type 218.
    fn ragdoll(&mut self, e: &Ev<T>) -> bool;
    /// 0x824D9B90(handler +0x34).
    fn other_stop(&mut self);
    /// 0x8246DFD8(records, ped, event, slot2): the main task's record.
    fn note(&mut self, recs: &mut Records<R>, e: &Ev<T>, slot2: bool);
    /// 0x8246E128(records, ped, event).
    fn note_other(&mut self, recs: &mut Records<R>, e: &Ev<T>);
    /// 0x8246EB90: the tasks go to the tree.
    fn give(&mut self, e: &Ev<T>);
    /// 0x8246E2F0: the built tasks dropped (it also clears `slot2`, done here).
    fn clear(&mut self);
}

/// The queue.
#[derive(Clone, Debug)]
pub struct Queue<T> {
    pub evs: Vec<Ev<T>>,
}

impl<T> Default for Queue<T> {
    fn default() -> Self {
        Queue { evs: Vec::new() }
    }
}

impl<T> Queue<T> {
    /// 0x824C25A8.
    pub fn add(&mut self, mut e: Ev<T>, keep: bool, replace: bool, a: &Admit) -> Added {
        let mut o = Added::default();
        if a.ped {
            if a.refused {
                return o;
            }
            let (mut store, mut pass) = (true, true);
            if a.decision && a.response == NO_RESPONSE && !(matches!(e.kind, DAMAGE | MELEE_ACTION) && a.byte_c) {
                store = false;
            }
            if a.blocking && !a.temporary && e.priority < 0x47 && !matches!(e.kind, SCRIPT_COMMAND | 54 | 55) {
                (store, pass) = (false, false);
            }
            if !a.concerns {
                (store, pass) = (false, false);
            }
            if pass {
                if a.decision {
                    o.group = a.trials[0];
                    o.friends = a.trials[1] && !(e.kind == DAMAGE && a.bit38);
                    o.vehicle = true;
                }
                o.heard = true;
            }
            if !store {
                return o;
            }
        }
        e.keep = keep;
        if self.evs.len() < MAX {
            if e.kind == GIVE_PED_TASK {
                self.evs.retain(|x| x.kind != GIVE_PED_TASK);
            }
        } else {
            let Some(i) = self.lowest().filter(|_| replace && a.ped) else { return o };
            if self.evs[i].priority >= e.priority {
                return o;
            }
            self.evs.remove(i);
        }
        self.evs.push(e);
        o.stored = true;
        o
    }

    /// 0x824C21D8: the last event of the lowest priority (from 0x69 down) that is not a script command.
    pub fn lowest(&self) -> Option<usize> {
        let mut best = (None, 0x69);
        for (i, e) in self.evs.iter().enumerate() {
            if e.kind != SCRIPT_COMMAND && e.priority <= best.1 {
                best = (Some(i), e.priority);
            }
        }
        best.0
    }

    /// 0x824C2270.
    pub fn pick(&mut self, v: &mut impl Asks<T>) -> Option<usize> {
        let (mut best, mut at) = (None, -1);
        for (i, e) in self.evs.iter_mut().enumerate() {
            let p = e.priority;
            if e.kind == SCRIPT_COMMAND {
                if p <= at {
                    continue;
                }
            } else if p < at {
                v.ready(e);
                continue;
            }
            if v.ready(e) && v.valid(e) && v.concerns(e) {
                (best, at) = (Some(i), p);
            }
        }
        best
    }

    /// 0x824C2050.
    pub fn age(&mut self) {
        self.evs.iter_mut().for_each(|e| e.age = e.age.wrapping_add(1));
    }

    /// 0x824C2118(queue, 0) then 0x824C2090: the invalid events go. (Its flush, every event but a script command, isn't
    /// used here.)
    pub fn sweep(&mut self, v: &mut impl Asks<T>) {
        self.evs.retain(|e| v.valid(e));
    }

    /// 0x824C2A20: ready communicate events go.
    pub fn drop_communicate(&mut self, v: &mut impl Asks<T>) {
        self.evs.retain_mut(|e| !(e.kind == COMMUNICATE && v.ready(e)));
    }

    /// 0x824760D0.
    pub fn handle<R: Record, H: Handler<T, R>>(&mut self, recs: &mut Records<R>, tree: &Tree, slot2: &mut bool, h: &mut H) {
        if recs.pending.is_some() && recs.temp.is_none() {
            recs.nontemp = recs.pending.take();
        }
        let note0 = recs.note;
        let first = |s: &[u32]| s.iter().copied().find(|&t| t != 0).unwrap_or(0);
        let root = first(&tree.slots[..3]);
        let nontemp_slot = if tree.slots[2] != 0 { tree.slots[2] } else { tree.slots[0] };
        self.drop_communicate(h);
        let picked = self.pick(h);
        self.age();
        recs.note = if note0 == root { note0 } else { 0 };
        if tree.slots[1] == 0 {
            recs.temp = None;
        }
        if nontemp_slot == 0 {
            recs.nontemp = None;
        }
        if tree.secondary == 0 {
            recs.other = None;
        }
        let drop_records = |q: &mut Self, recs: &mut Records<R>, h: &mut H| {
            // 0x8246EAF0 drops the note, temp, nontemp and other records; with no root the first three are gone already.
            if root == 0 {
                recs.other = None;
            }
            q.sweep(h);
        };
        let Some(i) = picked else { return drop_records(self, recs, h) };
        if let Some(r) = recs.temp.as_ref().or(recs.nontemp.as_ref()).or(recs.other.as_ref()) {
            if !r.stacks() && r.kind() == self.evs[i].kind {
                self.evs.remove(i);
                return drop_records(self, recs, h);
            }
        }
        if may_override(recs, &self.evs[i], *slot2, h) {
            let reply = h.respond(&self.evs[i], slot2);
            let mut ok = true;
            let top = first(&tree.slots);
            let built = reply.main || reply.secondary;
            if built {
                if top != 0 && !h.ask(top, 1, &mut self.evs[i]) {
                    ok = h.ragdoll(&self.evs[i]) && h.ask(top, 2, &mut self.evs[i]);
                }
            } else if h.other() {
                let t = h.other_tree();
                if t != 0 && !h.ask(t, 1, &mut self.evs[i]) {
                    h.other_stop();
                    ok = false;
                }
            }
            let accept = if root == 0 { note0 != 0 || ok } else { ok };
            if accept {
                recs.note = 0;
                if reply.main {
                    h.note(recs, &self.evs[i], *slot2);
                } else if h.other() {
                    h.note_other(recs, &self.evs[i]);
                }
                h.give(&self.evs[i]);
                self.evs.remove(i);
            } else if root == 0 {
                self.evs[i].age = self.evs[i].age.wrapping_sub(1);
                clear(h, slot2);
            } else if built {
                clear(h, slot2);
                self.evs[i].age = self.evs[i].age.wrapping_sub(1);
                h.ask(top, 0, &mut self.evs[i]);
                recs.note = root;
            }
        }
        clear(h, slot2);
        self.sweep(h);
    }
}

fn clear<T, R, H: Handler<T, R>>(h: &mut H, slot2: &mut bool) {
    h.clear();
    *slot2 = false;
}

/// 0x8246E1C0(records, event, slot2).
fn may_override<T, R, H: Handler<T, R>>(recs: &Records<R>, e: &Ev<T>, slot2: bool, h: &mut H) -> bool {
    if let Some(r) = &recs.nontemp {
        return h.overrides(e, r);
    }
    let Some(t) = &recs.temp else { return true };
    if h.temporary(e) && !slot2 {
        return h.overrides(e, t);
    }
    h.overrides(e, t) && recs.pending.as_ref().is_none_or(|p| h.overrides(e, p))
}

/// The gunshot events' ready test (vf +0x14, 0x823031F0) and validity (vf +0x44, 0x82303300); the ctor's bytes +0x41 1,
/// +0x42 / +0x43 0.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Wait {
    /// +0x41: the wait is still to be drawn.
    pub first: bool,
    /// +0x42.
    pub ready: bool,
    /// +0x43: the age was reset.
    pub aged: bool,
    /// +0x44: seconds left.
    pub left: f32,
}

impl Default for Wait {
    fn default() -> Wait {
        Wait { first: true, ready: false, aged: false, left: 0.0 }
    }
}

impl Wait {
    /// 0x823031F0: `responding` a task in the physical or an event response slot (0..2); `dt` the frame's time
    /// (0x82C6C2AC); `draw` 0x82158E78(0, [`GUNSHOT_WAIT`]).
    pub fn ready(&mut self, age: &mut i32, responding: bool, dt: f32, draw: impl FnOnce() -> f32) -> bool {
        if responding {
            (self.ready, self.aged) = (true, false);
            return true;
        }
        if self.ready {
            self.aged = false;
            return true;
        }
        if self.first {
            (self.left, self.first) = (draw(), false);
        }
        self.left -= dt;
        // `ble`: a NaN is ready too.
        self.ready = !(self.left > 0.0);
        if self.ready && !self.aged {
            (*age, self.aged) = (0, true);
        }
        self.ready
    }

    /// 0x82303300: still waiting, or made ready this pass: valid; else the default.
    pub fn valid<T>(&self, e: &Ev<T>) -> bool {
        !(self.ready && !self.aged) || e.still_valid()
    }
}
