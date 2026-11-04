//! weaponinfo.xml (common.rpf data/): one `<weapon type="...">` block per weapon, its numbers in attributes.

use crate::weaponinfoload::{self, Element, Row, EXPLOSIONS, FIRE_TYPES, PROJECTILE_TYPES, ROW, WEAPON_TYPES};

#[derive(Clone, Debug, Default, PartialEq)]
pub struct Weapon {
    /// PISTOL, AK47, ...
    pub name: String,
    /// MELEE, INSTANT_HIT, DELAYED_HIT, PROJECTILE, ...
    pub fire: String,
    /// Damage per hit.
    pub damage: f32,
    /// Damage per hit in first person (`<damage fps>`, else `base`): the getter 8229A1B0 returns it (+132) for a
    /// FIRST_PERSON-flagged weapon unless the player is running task 1054.
    pub damage_fps: f32,
    /// How far a shot reaches (m).
    pub range: f32,
    /// How far it locks on / aims (m); a throw's speed (AddProjectile 8236DE70).
    pub target_range: f32,
    pub clip: u32,
    pub ammo_max: u32,
    /// ms.
    pub between_shots: f32,
    /// ms.
    pub reload: f32,
    /// ms, crouched.
    pub reload_crouch: f32,
    /// ms, the player's when his anim group has the `p_load` clip (822675F8).
    pub reload_fast: f32,
    /// Push on what it hits.
    pub force: f32,
    pub accuracy: f32,
    /// `accuracyfps`: the accuracy scoped (8229A0A8; only the snipers set it, 0), else `accuracy`.
    pub accuracy_fps: f32,
    /// How much lower the aim is crouched (m): `<aiming><offset z>` - `<crouchedoffset z>`, 0 without them.
    pub crouch_drop: f32,
    /// `<aiming><offset>` (x, y, z; 0 without): where the gun points from, off the ped's position along its own axes
    /// (info +0x40; the torso's aim, `aimik`, takes x and z).
    pub aim_offset: [f32; 3],
    /// Fire clip speed (`<rates firerate>`, info +0x2C, 1 without).
    pub fire_rate: f32,
    /// Blind fire clip speed from cover (`<rates blindfirerate>`, 1 without).
    pub blind_rate: f32,
    /// Model in weapons.img, e.g. w_glock.
    pub model: Option<String>,
    /// UNARMED, MELEE, HANDGUN, SHOTGUN, SMG, RIFLE, SNIPER, HEAVY, THROWN, SPECIAL.
    pub slot: String,
    /// BULLET, MELEE, EXPLOSIVE, FIRE, ...
    pub damage_type: String,
    /// weaponfx.dat column: PISTOL_SMALL, SHOTGUN, RIFLE_ASSAULT, ...
    pub group: String,
    /// GUN, 2HANDED, FIRST_PERSON, ...
    pub flags: Vec<String>,
    /// `<projectile>`: what it launches (the weapon itself unless `<typetocreate>` names another).
    pub projectile: Option<Projectile>,
    /// Muzzle flash ptfx.
    pub muzzle: Option<String>,
    /// Ejected shell ptfx (weap_ejected_*).
    pub shell: Option<String>,
    /// Weapon anim dictionary, e.g. gun@handgun.
    pub anims: Option<String>,
    /// `<anim meleegroup1 meleegroup2>`: its melee anim groups (the reader 0x8229A3D0 puts them in +0xDC and +0xE0),
    /// e.g. firearm_core; the first is the move set a ped fighting with it takes (0x822C55B8).
    pub melee_groups: [Option<String>; 2],
    /// `<rumble duration intensity>`: the player's pad shakes this long (ms) this hard (0..1) per shot (82269810:
    /// info +0x70, +0x74).
    pub rumble: (f32, f32),
}

#[derive(Clone, Debug, PartialEq)]
pub struct Projectile {
    /// FUSE, IMPACT, ROCKET, NONE; or the weapon type to create (RLAUNCHER -> ROCKET).
    pub kind: String,
    /// ms, for FUSE.
    pub fuse: f32,
    /// explosionfx.dat row.
    pub explosion: Option<String>,
    pub force: f32,
    /// `explodeimpactthreshold`: a ROCKET touching something blows up only when |normal . nose| is over this (8236FC00).
    pub explode_impact: f32,
    /// +0xB0 / +0xC0, `<offset>` / `<rotoffset>` (radians): where a CREATE_VISIBLE_ORDNANCE weapon's loaded round (its
    /// `typetocreate` model) sits on the ped's right hand bone (0x823D7318).
    pub offset: [f32; 3],
    pub rot_offset: [f32; 3],
}

impl Weapon {
    pub fn has(&self, flag: &str) -> bool {
        self.flags.iter().any(|f| f == flag)
    }
}

/// The value of ` name="..."` in `block` (the leading space keeps `time` from matching `fasttime`).
/// weaponinfo's name tables (0x82A994D8, 0x82A99528, 0x82A99568, 0x82A99618), as long as each lookup walks them.
pub const SLOTS: [&str; 12] = ["UNARMED", "MELEE", "HANDGUN", "SHOTGUN", "SMG", "RIFLE", "SNIPER", "HEAVY", "THROWN", "SPECIAL",
    "PARACHUTE", "UNKNOWN"];
/// The tenth, a second NONE, is never reached.
pub const DAMAGE_TYPES: [&str; 10] = ["NONE", "MELEE", "BULLET", "EXPLOSIVE", "FIRE", "COLLISION", "FALL", "DROWN", "UNKNOWN", "NONE"];
pub const GROUPS: [&str; 18] = ["PUNCH_KICK", "MELEE_WOOD", "MELEE_METAL", "MELEE_SHARP", "MELEE_GENERIC", "PISTOL_SMALL",
    "PISTOL_LARGE", "PISTOL_SILENCED", "SMG", "SHOTGUN", "RIFLE_ASSAULT", "RIFLE_SNIPER", "ROCKET", "GRENADE", "MOLOTOV", "FIRE",
    "EXPLOSION", "UNKNOWN"];
/// ponytail: the game's loop reads a 26th word past these (a float); an unknown flag name is not looked up there.
pub const FLAGS: [&str; 25] = ["CAN_AIM", "CAN_AIM_WITH_ARM", "CAN_FREE_AIM", "FIRST_PERSON", "KEEP_CAMERA_BEHIND", "GUN", "THROWN",
    "HEAVY", "SILENCED", "MELEE_CLUB", "MELEE_BLADE", "ARMOUR_PENETRATING", "2HANDED", "TREAT_AS_2HANDED_IN_COVER", "ANIM_RELOAD",
    "ANIM_CROUCH_FIRE", "CREATE_VISIBLE_ORDNANCE", "EXPLOSION_BASED_ON_IMPACT", "ADD_SMOKE_ON_EXPLOSION", "INSTANT_KILL_IN_MP",
    "HIGHER_BREAK_FORCE", "CAN_BE_USED_AS_DRIVEBY", "HEAVY_WEAPON_USES_RIFLE_ANIMS", "MISSION_CAN_FORCE_DRIVEBY_USE",
    "DONT_RUMBLE_WHEN_DOING_DRIVE_BY"];

/// A name's index in a table as the lookups give it: the first entry equal ignoring the case of A-Z (0x829FFCF0); an empty
/// or unknown name is `none`.
fn lookup(table: &[&str], name: &str, none: i32) -> i32 {
    if name.is_empty() {
        return none;
    }
    table.iter().position(|t| t.eq_ignore_ascii_case(name)).map_or(none, |i| i as i32)
}

/// 0x82299B40: a slot name's index (UNARMED when missing).
pub fn slot_index(name: &str) -> i32 { lookup(&SLOTS, name, 0) }
/// 0x82299C60: a damage type name's index (UNKNOWN when missing).
pub fn damage_type_index(name: &str) -> i32 { lookup(&DAMAGE_TYPES, name, 8) }
/// 0x82299CF0: a group name's weaponfx column (PUNCH_KICK when missing).
pub fn group_index(name: &str) -> i32 { lookup(&GROUPS, name, 0) }
/// 0x82299D80: a flag name's bit (-1, none, when missing).
pub fn flag_bit(name: &str) -> i32 { lookup(&FLAGS, name, -1) }

/// The value of attribute `key` (exact name) of `el`, as text.
fn text_attr<'a>(el: &'a Element, key: &str) -> Option<&'a str> {
    el.attrs.iter().find(|(k, _)| k == key).map(|(_, v)| v.as_str())
}

/// The first element named `name` under `el` (depth first).
fn find<'a>(el: &'a Element, name: &str) -> Option<&'a Element> {
    el.children.iter().find_map(|c| if c.name == name { Some(c) } else { find(c, name) })
}

/// Attribute `key` of the first element named `tag` under `el`, as text.
fn desc_attr<'a>(el: &'a Element, tag: &str, key: &str) -> Option<&'a str> {
    find(el, tag).and_then(|t| text_attr(t, key))
}

/// The `key="value"` (or single-quoted) pairs of a tag's attribute text, in file order.
fn attributes(mut s: &str) -> Vec<(String, String)> {
    let mut out = Vec::new();
    while let Some(eq) = s.find('=') {
        let key = s[..eq].trim();
        let after = s[eq + 1..].trim_start();
        let Some(q) = after.chars().next().filter(|c| *c == '"' || *c == '\'') else { break };
        let body = &after[1..];
        let Some(end) = body.find(q) else { break };
        out.push((key.to_string(), body[..end].to_string()));
        s = &body[end + 1..];
    }
    out
}

/// Reads weaponinfo.xml into a tree; the root is a synthetic element (empty name) holding the file's top-level elements.
/// A small tolerant reader: quoted attributes, text, comments, `<?..?>` and `<!..>` skipped, self-closing tags. A closing
/// tag closes the nearest open element of its name (the ones left open inside it close with it); one with no open
/// element of its name is ignored. Every element has kind 1: the loader counts a `<flag>` child unless its kind is 0, and
/// the parser's own kinds are not known here.
fn tree(xml: &str) -> Element {
    fn close_top(stack: &mut Vec<Element>) {
        let el = stack.pop().unwrap();
        stack.last_mut().unwrap().children.push(el);
    }
    let mut stack = vec![Element { kind: 1, ..Default::default() }];
    let mut rest = xml;
    while let Some(lt) = rest.find('<') {
        let text = rest[..lt].trim();
        if !text.is_empty() {
            let top = stack.last_mut().unwrap();
            top.text = Some(match top.text.take() {
                Some(old) => format!("{old} {text}"),
                None => text.to_string(),
            });
        }
        rest = &rest[lt..];
        if rest.starts_with("<!--") {
            rest = rest.find("-->").map_or("", |i| &rest[i + 3..]);
            continue;
        }
        let Some(gt) = rest.find('>') else { break };
        let tag = &rest[1..gt];
        rest = &rest[gt + 1..];
        if tag.starts_with(['?', '!']) {
            continue;
        }
        if let Some(name) = tag.strip_prefix('/') {
            let name = name.trim();
            if let Some(i) = (1..stack.len()).rev().find(|&i| stack[i].name == name) {
                while stack.len() > i {
                    close_top(&mut stack);
                }
            }
            continue;
        }
        let selfclose = tag.ends_with('/');
        let body = tag.trim_end_matches('/');
        let (name, attrs) = body.split_once(char::is_whitespace).unwrap_or((body, ""));
        let el = Element { name: name.to_string(), kind: 1, text: None, attrs: attributes(attrs), children: Vec::new() };
        if selfclose {
            stack.last_mut().unwrap().children.push(el);
        } else {
            stack.push(el);
        }
    }
    while stack.len() > 1 {
        close_top(&mut stack);
    }
    stack.pop().unwrap()
}

/// Every `weapon` element under `el`, in file order (not nested in another weapon).
fn weapons_in<'a>(el: &'a Element, out: &mut Vec<&'a Element>) {
    for c in &el.children {
        if c.name == "weapon" {
            out.push(c);
        } else {
            weapons_in(c, out);
        }
    }
}

fn word(row: &Row, at: usize) -> u32 {
    u32::from_be_bytes([row[at], row[at + 1], row[at + 2], row[at + 3]])
}

fn half(row: &Row, at: usize) -> u16 {
    u16::from_be_bytes([row[at], row[at + 1]])
}

fn float(row: &Row, at: usize) -> f32 {
    f32::from_bits(word(row, at))
}

/// The name at the index held in the word at `at`, in `table`; "" when the index is not one of the table's.
fn name_at(row: &Row, table: &[&str], at: usize) -> String {
    usize::try_from(word(row, at) as i32).ok().and_then(|i| table.get(i)).map_or(String::new(), |s| s.to_string())
}

/// The weapon table from weaponinfo.xml. Each `<weapon type>` with a `<data>` child has that child loaded by the 1:1
/// loader (`weaponinfoload::load`, 0x8229ADE0) into a zeroed row, and every field below is read back from the row, so
/// the numbers are the game's. A weapon with no `<data>` child is dropped (the loader's null-element return, 0x8229ADF8,
/// is not ported). The fields the loader does not write (`model`, `muzzle`, `shell`, `anims`, `fire_rate`,
/// `blind_rate`) are read from the tree.
pub fn read(xml: &str) -> Vec<Weapon> {
    let root = tree(xml);
    let mut ws = Vec::new();
    weapons_in(&root, &mut ws);
    ws.into_iter().filter_map(|w| {
        let name = text_attr(w, "type")?;
        let data = w.children.iter().find(|c| c.name == "data")?;
        let mut row = [0u8; ROW];
        weaponinfoload::load(data, &mut row);
        Some(from_row(w, data, name, &row))
    }).collect()
}

/// The weapon from its loaded row (offsets as in docs/weapons/weaponinfoload.md). `w` is the `<weapon>` element, `data`
/// the element the loader read.
fn from_row(w: &Element, data: &Element, name: &str, row: &Row) -> Weapon {
    let fire = FIRE_TYPES.get(word(row, 8) as usize).copied().unwrap_or("").to_string();
    let word20 = word(row, 0x20);
    let flags = FLAGS.iter().enumerate().filter(|&(bit, _)| (word20 >> bit) & 1 != 0).map(|(_, f)| f.to_string()).collect();
    // The loader reads a projectile only for PROJECTILE. Its `force` is the physics child's, which the loader wrote over
    // the range at +0x18; without a physics child +0x18 is the weapon's range, so the force is 0 then.
    let projectile = data.children.iter().find(|c| c.name == "projectile").filter(|_| fire == "PROJECTILE").map(|p| {
        let explosion = word(row, 0xAC) as i32;
        Projectile {
            kind: if p.children.iter().any(|c| c.name == "typetocreate") {
                name_at(row, &WEAPON_TYPES, 0xA8)
            } else {
                name_at(row, &PROJECTILE_TYPES, 0xA0)
            },
            fuse: word(row, 0xA4) as i32 as f32,
            explosion: (explosion >= 0).then(|| EXPLOSIONS.get(explosion as usize).copied().unwrap_or("").to_string()),
            force: if p.children.iter().any(|c| c.name == "physics") { float(row, 0x18) } else { 0.0 },
            explode_impact: float(row, 0xD0),
            offset: [float(row, 0xB0), float(row, 0xB4), float(row, 0xB8)],
            rot_offset: [float(row, 0xC0), float(row, 0xC4), float(row, 0xC8)],
        }
    });
    Weapon {
        name: name.to_string(),
        fire,
        damage: half(row, 0x82) as f32,
        damage_fps: half(row, 0x84) as f32,
        range: float(row, 0x18),
        target_range: float(row, 0x14),
        clip: half(row, 0x86) as u32,
        // ammomax: the loader's sign-extended, clamped value (25000 when absent).
        ammo_max: half(row, 0x88) as u32,
        between_shots: word(row, 0x8C) as i32 as f32,
        reload: word(row, 0x94) as i32 as f32,
        reload_crouch: word(row, 0x9C) as i32 as f32,
        reload_fast: word(row, 0x98) as i32 as f32,
        force: float(row, 0x90),
        accuracy: float(row, 0x34),
        accuracy_fps: float(row, 0x38),
        // The loader stores both vectors; the consumer subtracts them (offset.z - crouchedoffset.z).
        crouch_drop: float(row, 0x48) - float(row, 0x58),
        aim_offset: [float(row, 0x40), float(row, 0x44), float(row, 0x48)],
        // Not written by the loader (its `rates` case is not dispatched): read from `<rates>` as before, 1 without.
        fire_rate: desc_attr(w, "rates", "firerate").and_then(|v| v.parse().ok()).unwrap_or(1.0),
        blind_rate: desc_attr(w, "rates", "blindfirerate").and_then(|v| v.parse().ok()).unwrap_or(1.0),
        model: desc_attr(w, "assets", "model").map(str::to_string),
        slot: name_at(row, &SLOTS, 4),
        damage_type: name_at(row, &DAMAGE_TYPES, 0xC),
        group: name_at(row, &GROUPS, 0x10),
        flags,
        projectile,
        muzzle: desc_attr(w, "muzzle", "fx").map(str::to_string),
        shell: desc_attr(w, "shell", "fx").map(str::to_string),
        anims: desc_attr(w, "anim", "group").map(str::to_string),
        melee_groups: ["meleegroup1", "meleegroup2"].map(|k| desc_attr(w, "anim", k).map(str::to_string)),
        rumble: (word(row, 0x70) as i32 as f32, float(row, 0x74)),
    }
}

/// The pad's two motor speeds (0..255) for a shake of `intensity` (821B4540): clamped to 0..1; none at 0; under 0.1
/// only the second, 50 + 10i x 70; else the first, 15 + 185i, and the second 120 from 0.9 up.
pub fn pad_motors(intensity: f32) -> (u8, u8) {
    let i = intensity.max(0.0).min(1.0);
    if i == 0.0 {
        (0, 0)
    } else if i < 0.1 {
        (0, (i * 10.0).mul_add(70.0, 50.0) as u8)
    } else {
        (i.mul_add(185.0, 15.0) as u8, if i >= 0.9 { 120 } else { 0 })
    }
}

/// Where the line `from`-`to` crosses a water quad (8228F368, as far as its quads go): a quad is flat at its first
/// corner's height; when that lies between the ends' heights the crossing point is the hit if it is inside the quad's
/// x / y rectangle. ponytail: the quads in file order, not by the 500 m cells; the triangles aren't kept.
pub fn water_hit(quads: &[[[f32; 3]; 4]], from: [f32; 3], to: [f32; 3]) -> Option<[f32; 3]> {
    quads.iter().find_map(|q| {
        let h = q[0][2];
        if (from[2] - h) * (to[2] - h) > 0.0 || from[2] == to[2] {
            return None;
        }
        let t = (h - from[2]) / (to[2] - from[2]);
        let (x, y) = (from[0] + (to[0] - from[0]) * t, from[1] + (to[1] - from[1]) * t);
        let (lo, hi) = q.iter().fold(([f32::MAX; 2], [f32::MIN; 2]), |(lo, hi), v| ([lo[0].min(v[0]), lo[1].min(v[1])], [hi[0].max(v[0]), hi[1].max(v[1])]));
        (x >= lo[0] && x <= hi[0] && y >= lo[1] && y <= hi[1]).then_some([x, y, h])
    })
}

#[cfg(test)]
mod tests {
    /// The four name lookups against the game's (nm-workbench oracle/p_wnames.py): table names in their case, lower, mixed,
    /// one longer, one shorter, empty and unknown.
    #[test]
    fn name_lookups_as_the_game() {
        let Some(dir) = std::env::var("NM_COVA_P").ok() else { return };
        let Ok(t) = std::fs::read_to_string(std::path::Path::new(&dir).join("p_wnames.txt")) else { return };
        let mut n = 0;
        for l in t.lines() {
            let w: Vec<&str> = l.split_whitespace().collect();
            let name = if w[3] == "-" { String::new() } else {
                String::from_utf8((0..w[3].len()).step_by(2).map(|i| u8::from_str_radix(&w[3][i..i + 2], 16).unwrap()).collect()).unwrap()
            };
            let ours = match w[1] {
                "82299b40" => super::slot_index(&name),
                "82299c60" => super::damage_type_index(&name),
                "82299cf0" => super::group_index(&name),
                "82299d80" => super::flag_bit(&name),
                f => panic!("{f}"),
            };
            assert_eq!(ours as u32, u32::from_str_radix(w[2], 16).unwrap(), "{} {name:?}", w[1]);
            n += 1;
        }
        assert!(n > 250);
    }

    #[test]
    fn rumble() {
        // weaponinfo's pistol (0.02), AK47 (0.1) and shotgun (1.0) rumbles.
        assert_eq!(super::pad_motors(0.0), (0, 0));
        assert_eq!(super::pad_motors(0.05), (0, 85));
        assert_eq!(super::pad_motors(0.2), (52, 0));
        assert_eq!(super::pad_motors(1.0), (200, 120));
        assert_eq!(super::pad_motors(7.0), (200, 120));
    }

    #[test]
    fn water() {
        let q = [[[0.0, 0.0, 1.0], [10.0, 0.0, 1.0], [0.0, 10.0, 1.0], [10.0, 10.0, 1.0]]];
        assert_eq!(super::water_hit(&q, [2.0, 2.0, 3.0], [6.0, 2.0, -1.0]), Some([4.0, 2.0, 1.0]));
        assert_eq!(super::water_hit(&q, [2.0, 2.0, 3.0], [6.0, 2.0, 2.0]), None);
        assert_eq!(super::water_hit(&q, [9.0, 2.0, 3.0], [13.0, 2.0, -1.0]), None);
    }

    #[test]
    fn pistol() {
        let xml = r#"<weaponinfo version="1">
	<weapon type="PISTOL">
		<data slot="HANDGUN" firetype="INSTANT_HIT" damagetype="BULLET" group="PISTOL_SMALL" targetrange="45.0" weaponrange="50.0" clipsize="17" ammomax="1500" timebetweenshots="333">
			<damage base="25" fps="75" networkplayermod="3.0" networkpedmod="1.0"/>
			<physics force="50.0"/>
			<reload time="2000" fasttime="1500" crouchtime="2000"/>
			<aiming accuracy="0.5">
				<offset         x="0.15" y="1.0" z="0.55"/>
				<crouchedoffset x="0.2"  y="1.0" z="0.05"/>
			</aiming>
			<flags>
				<flag>GUN</flag>
				<flag>CAN_AIM</flag>
			</flags>
			<rates firerate="1.0" blindfirerate="1.4"/>
			<controller>
				<rumble duration="110" intensity="0.02"/>
			</controller>
		</data>
		<assets model="w_glock">
			<anim group="gun@handgun" meleegroup1="firearm_core"/>
		</assets>
	</weapon>
	<weapon type="UNARMED">
		<data slot="UNARMED" firetype="MELEE" damagetype="MELEE" group="PUNCH_KICK" targetrange="10.0" weaponrange="1.6"/>
	</weapon>
</weaponinfo>"#;
        let w = super::read(xml);
        assert_eq!(w.len(), 2);
        assert_eq!((w[0].name.as_str(), w[0].fire.as_str(), w[0].damage, w[0].range, w[0].clip, w[0].between_shots, w[0].reload,
                    w[0].force, w[0].model.as_deref()),
                   ("PISTOL", "INSTANT_HIT", 25.0, 50.0, 17, 333.0, 2000.0, 50.0, Some("w_glock")));
        assert_eq!((w[1].fire.as_str(), w[1].model.as_deref()), ("MELEE", None));
        assert_eq!((w[0].slot.as_str(), w[0].has("GUN"), w[0].has("2HANDED"), w[0].anims.as_deref()), ("HANDGUN", true, false, Some("gun@handgun")));
        assert_eq!(w[0].melee_groups, [Some("firearm_core".to_string()), None]);
        assert_eq!(w[0].projectile, None);
        assert_eq!((w[0].reload_crouch, w[1].reload_crouch), (2000.0, 0.0));
        assert_eq!((w[0].crouch_drop, w[1].crouch_drop), (0.5, 0.0));
        assert_eq!((w[0].aim_offset, w[1].aim_offset), ([0.15, 1.0, 0.55], [0.0; 3]));
        assert_eq!((w[0].blind_rate, w[1].blind_rate), (1.4, 1.0));
        assert_eq!((w[0].damage_fps, w[1].damage_fps), (75.0, 0.0));
        assert_eq!((w[0].group.as_str(), w[1].group.as_str()), ("PISTOL_SMALL", "PUNCH_KICK"));
        assert_eq!((w[0].rumble, w[1].rumble), ((110.0, 0.02), (0.0, 0.0)));
    }

    #[test]
    fn grenade_and_rpg() {
        let xml = r#"<weapon type="GRENADE">
		<data slot="THROWN" firetype="PROJECTILE" damagetype="EXPLOSIVE" group="GRENADE" targetrange="35.0" clipsize="1" ammomax="25">
			<flags><flag>THROWN</flag></flags>
			<projectile type="FUSE" fusetime="5000">
				<explosion type="GRENADE"/>
				<physics force="33.0" vehiclevelocity="5.0"/>
			</projectile>
		</data>
		<assets model="w_grenade"><anim group="grenade"/></assets>
	</weapon>
	<weapon type="RLAUNCHER">
		<data slot="HEAVY" firetype="PROJECTILE" damagetype="EXPLOSIVE" group="ROCKET" weaponrange="100.0" clipsize="1" ammomax="8" timebetweenshots="800">
			<reload time="2066" fasttime="2066" crouchtime="2066"/>
			<projectile><typetocreate type="ROCKET"/></projectile>
		</data>
	</weapon>"#;
        let w = super::read(xml);
        // The physics force is written over the range (+0x18) by the loader, and the grenade has no top-level physics.
        assert_eq!(w[0].projectile, Some(super::Projectile { kind: "FUSE".into(), fuse: 5000.0, explosion: Some("GRENADE".into()), force: 33.0, explode_impact: 0.0, offset: [0.0; 3], rot_offset: [0.0; 3] }));
        assert_eq!((w[0].slot.as_str(), w[0].has("THROWN"), w[0].force, w[0].target_range, w[0].range), ("THROWN", true, 0.0, 35.0, 33.0));
        assert_eq!(w[1].projectile.as_ref().map(|p| p.kind.as_str()), Some("ROCKET"));
        assert_eq!((w[1].range, w[1].reload, w[1].projectile.as_ref().map(|p| p.force)), (100.0, 2066.0, Some(0.0)));
    }

    #[test]
    fn loader_defaults_and_clamps() {
        // No firetype: INSTANT_HIT (kept). No ammomax: 25000. ammomax 40000 (16-bit sign extension) is 0.
        let xml = r#"<weapon type="A"><data slot="HANDGUN"><damage base="7" fps="9"/></data></weapon>
	<weapon type="B"><data firetype="INSTANT_HIT" ammomax="40000"><reload time="5"/></data></weapon>
	<weapon type="C"><data firetype="INSTANT_HIT"><damage base="7"/><reload time="5"/></data></weapon>"#;
        let w = super::read(xml);
        assert_eq!(w.len(), 3);
        assert_eq!((w[0].fire.as_str(), w[0].ammo_max, w[0].damage_fps), ("INSTANT_HIT", 25000, 9.0));
        assert_eq!((w[1].ammo_max, w[1].slot.as_str(), w[1].damage_type.as_str()), (0, "UNARMED", "UNKNOWN"));
        // No `fps` and no `fasttime` / `crouchtime`: 0, not the base or `time`.
        assert_eq!((w[2].damage_fps, w[2].reload_fast, w[2].reload_crouch, w[2].reload), (0.0, 0.0, 0.0, 5.0));
    }
}
