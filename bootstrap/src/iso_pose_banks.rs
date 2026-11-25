// SPDX-License-Identifier: GPL-3.0-only
//! Bake vehicle drive-by poses (`DriverPoses.bin`), NPC gait retargets (`NPCGait.bin`),
//! and NPC mover curves (`NPCMovers.dat`) directly from the owner's GTA IV Xbox 360 ISO
//! and GTA San Andreas copy. No copyrighted game assets are distributed.

use bevy_math::{DMat3, DMat4, DQuat, DVec3};
use delirium_data::{anim, archive_key::ArchiveKey, disc::Disc, img, model, resource, skeleton};
use std::collections::{BTreeMap, HashMap, HashSet};
use std::fmt::Write as _;
use std::fs::{self, File};
use std::io::{Read, Seek, SeekFrom};
use std::path::Path;

pub const DRIVER_POSES: &str = "DriverPoses.bin";
pub const NPC_GAIT: &str = "NPCGait.bin";
pub const NPC_MOVERS: &str = "NPCMovers.dat";

const ANIM_IMG: &str = "xbox360/anim/anim.img";
const COMP_PEDS_IMG: &str = "xbox360/models/cdimages/componentpeds.img";

const DRIVEBY_GROUPS: [&str; 9] = [
    "std",
    "low",
    "van",
    "truck",
    "bike_spt",
    "bike_dirt",
    "bike_free",
    "bike_chop",
    "bike_scot",
];
const DRIVEBY_SEATS: [&str; 4] = ["ds", "ps", "bl", "br"];

const PROFILES: [(&str, &str, &str); 15] = [
    ("move_m@casual", "wmyst", "man"),
    ("move_f@casual", "bfyst", "woman"),
    ("move_m@casual_b", "hmyst", "man"),
    ("move_m@old_a", "wmopj", "oldman"),
    ("move_f@old_a", "ofori", "oldwoman"),
    ("move_m@fat", "smoke", "fatman"),
    ("move_f@fat", "wfost", "oldfatwoman"),
    ("move_m@swat", "swat", "swat"),
    ("move_cop", "lapd1", "swat"),
    ("move_gng@generic_a", "lsv1", "gang1"),
    ("move_gng@generic_b", "fam1", "gang2"),
    ("move_m@bness_a", "bmybu", "man"),
    ("move_f@bness_a", "wfybu", "busywoman"),
    ("move_f@sexy", "wfyri", "sexywoman"),
    ("move_m@bum", "wmotr1", "man"),
];

const GAIT_STATES: [&str; 4] = ["walk", "run", "sprint", "idle"];
const GAIT_TURNS: [&str; 6] = [
    "walk_turn_l",
    "walk_turn_r",
    "run_turn_l",
    "run_turn_r",
    "sprint_turn_l",
    "sprint_turn_r",
];
const GAIT_TRANSITIONS: [&str; 3] = ["wstart", "rstop_l", "rstop_r"];

const MOVER_CLIPS: [&str; 15] = [
    "walk",
    "run",
    "sprint",
    "wstart",
    "runstart_fwd",
    "rstop_l",
    "rstop_r",
    "wstop_l",
    "wstop_r",
    "walk_turn_l",
    "walk_turn_r",
    "run_turn_l",
    "run_turn_r",
    "sprint_turn_l",
    "sprint_turn_r",
];

// 21 SA ped bone tags mapped to IV Char_* names, sorted ascending.
const TAG_NAMES: [(i16, &str); 21] = [
    (1, "Char_Pelvis"),
    (2, "Char_Spine"),
    (3, "Char_Spine3"),
    (4, "Char_Neck"),
    (5, "Char_Head"),
    (21, "Char_R_Clavicle"),
    (22, "Char_R_UpperArm"),
    (23, "Char_R_Forearm"),
    (24, "Char_R_Hand"),
    (31, "Char_L_Clavicle"),
    (32, "Char_L_UpperArm"),
    (33, "Char_L_Forearm"),
    (34, "Char_L_Hand"),
    (41, "Char_L_Thigh"),
    (42, "Char_L_Calf"),
    (43, "Char_L_Foot"),
    (44, "Char_L_Toe0"),
    (51, "Char_R_Thigh"),
    (52, "Char_R_Calf"),
    (53, "Char_R_Foot"),
    (54, "Char_R_Toe0"),
];

const CHAINS: [(i16, i16); 10] = [
    (22, 23),
    (23, 24),
    (32, 33),
    (33, 34),
    (41, 42),
    (42, 43),
    (43, 44),
    (51, 52),
    (52, 53),
    (53, 54),
];

#[derive(Clone, Copy)]
struct WorldTransform {
    q: DQuat,
    t: DVec3,
}

struct PedSkeleton {
    bones: Vec<skeleton::Bone>,
    id_to_index: HashMap<u16, usize>,
    name_to_index: HashMap<String, usize>,
    rest_world: Vec<WorldTransform>,
}

fn read_img_table(disc: &Disc, key: &ArchiveKey, path: &str) -> Result<Vec<img::Entry>, String> {
    let header = disc.read_at(path, 0, 20)?;
    let len = img::table_len(&header, Some(key))
        .filter(|size| *size <= 8 << 20)
        .ok_or_else(|| format!("Índice incompatible en {path}"))?;
    img::read_table(&disc.read_at(path, 0, len)?, Some(key))
}

fn read_img_resource(
    disc: &Disc,
    entries: &[img::Entry],
    img_path: &str,
    member: &str,
) -> Result<resource::Resource, String> {
    let entry = entries
        .iter()
        .find(|e| e.name.eq_ignore_ascii_case(member))
        .ok_or_else(|| format!("Falta {member} en {img_path}"))?;
    let bytes = disc.read_at(img_path, entry.offset, usize::from(entry.used_blocks) * 2048)?;
    resource::parse(&bytes)
}

fn load_ped_skeleton(disc: &Disc, key: &ArchiveKey) -> Result<PedSkeleton, String> {
    let entries = read_img_table(disc, key, COMP_PEDS_IMG)?;
    let frag = read_img_resource(disc, &entries, COMP_PEDS_IMG, "m_m_fedco.xft")
        .or_else(|_| read_img_resource(disc, &entries, COMP_PEDS_IMG, "m_y_pmanhat_01.xft"))?;
    let bones = skeleton::read(&frag, model::fragment_root(&frag)?)?;
    let mut id_to_index = HashMap::with_capacity(bones.len());
    let mut name_to_index = HashMap::with_capacity(bones.len());
    let mut rest_world: Vec<WorldTransform> = Vec::with_capacity(bones.len());
    for (i, b) in bones.iter().enumerate() {
        id_to_index.insert(b.id, i);
        name_to_index.insert(b.name.clone(), i);
        let ql = DQuat::from_xyzw(
            b.rotation[0] as f64,
            b.rotation[1] as f64,
            b.rotation[2] as f64,
            b.rotation[3] as f64,
        )
        .normalize();
        let tl = DVec3::new(
            b.translation[0] as f64,
            b.translation[1] as f64,
            b.translation[2] as f64,
        );
        let wt = match b.parent {
            Some(p) if p < i => WorldTransform {
                q: (rest_world[p].q * ql).normalize(),
                t: rest_world[p].t + rest_world[p].q * tl,
            },
            _ => WorldTransform { q: ql, t: tl },
        };
        rest_world.push(wt);
    }
    Ok(PedSkeleton {
        bones,
        id_to_index,
        name_to_index,
        rest_world,
    })
}

fn evaluate_clip(skel: &PedSkeleton, clip: &anim::Animation) -> Vec<Vec<WorldTransform>> {
    let n = clip.frames;
    let nb = skel.bones.len();
    let mut local_t: Vec<Vec<DVec3>> = (0..n)
        .map(|_| {
            skel.bones
                .iter()
                .map(|b| {
                    DVec3::new(
                        b.translation[0] as f64,
                        b.translation[1] as f64,
                        b.translation[2] as f64,
                    )
                })
                .collect()
        })
        .collect();
    let mut local_q: Vec<Vec<DQuat>> = (0..n)
        .map(|_| {
            skel.bones
                .iter()
                .map(|b| {
                    DQuat::from_xyzw(
                        b.rotation[0] as f64,
                        b.rotation[1] as f64,
                        b.rotation[2] as f64,
                        b.rotation[3] as f64,
                    )
                    .normalize()
                })
                .collect()
        })
        .collect();
    for track in &clip.tracks {
        if track.kind != anim::BONE_ROTATION && track.kind != anim::BONE_TRANSLATION {
            continue;
        }
        let Some(&idx) = skel.id_to_index.get(&track.bone) else {
            continue;
        };
        if track.kind == anim::BONE_ROTATION {
            for f in 0..n {
                let v = track.sample(f);
                if v.len() >= 4 {
                    local_q[f][idx] =
                        DQuat::from_xyzw(v[0] as f64, v[1] as f64, v[2] as f64, v[3] as f64)
                            .normalize();
                }
            }
        } else {
            for f in 0..n {
                let v = track.sample(f);
                if v.len() >= 3 {
                    local_t[f][idx] = DVec3::new(v[0] as f64, v[1] as f64, v[2] as f64);
                }
            }
        }
    }
    let mut world = Vec::with_capacity(n);
    for f in 0..n {
        let mut frame: Vec<WorldTransform> = Vec::with_capacity(nb);
        for (i, b) in skel.bones.iter().enumerate() {
            let wt = match b.parent {
                Some(p) if p < i => WorldTransform {
                    q: (frame[p].q * local_q[f][i]).normalize(),
                    t: frame[p].t + frame[p].q * local_t[f][i],
                },
                _ => WorldTransform {
                    q: local_q[f][i],
                    t: local_t[f][i],
                },
            };
            frame.push(wt);
        }
        world.push(frame);
    }
    world
}

fn unit_clamped(v: DVec3) -> DVec3 {
    v / v.length().max(1e-7)
}

struct BakedDriveByClip {
    frames: Vec<[f32; 26]>,
    duration: f32,
    aim_hand: u32,
    release: f32,
    launch: [f32; 3],
}

fn bake_driveby_clip(
    skel: &PedSkeleton,
    clip: &anim::Animation,
    forced_hand: Option<u32>,
) -> Result<BakedDriveByClip, String> {
    let poses = evaluate_clip(skel, clip);
    let n = poses.len();
    if !(2..=512).contains(&n) {
        return Err(format!("Conteo de fotogramas drive-by inválido en {}: {n}", clip.name));
    }
    let idx = |name: &str| -> Result<usize, String> {
        skel.name_to_index
            .get(name)
            .copied()
            .ok_or_else(|| format!("Falta hueso {name} en esqueleto IV"))
    };
    let i_r_thigh = idx("Char_R_Thigh")?;
    let i_l_thigh = idx("Char_L_Thigh")?;
    let i_r_upper = idx("Char_R_UpperArm")?;
    let i_r_fore = idx("Char_R_Forearm")?;
    let i_r_hand = idx("Char_R_Hand")?;
    let i_r_finger = idx("Char_R_Finger2")?;
    let i_l_upper = idx("Char_L_UpperArm")?;
    let i_l_fore = idx("Char_L_Forearm")?;
    let i_l_hand = idx("Char_L_Hand")?;
    let i_l_finger = idx("Char_L_Finger2")?;
    let i_head = idx("Char_Head")?;
    let i_spine = idx("Char_Spine")?;

    let mut right = poses[0][i_r_thigh].t - poses[0][i_l_thigh].t;
    right.z = 0.0;
    let right = unit_clamped(right);
    let up = DVec3::Z;
    let fwd = up.cross(right);
    let to_basis = |p: DVec3| DVec3::new(p.dot(right), p.dot(fwd), p.dot(up));

    let hand_travel = |i_hand: usize, i_upper: usize| -> f64 {
        let mut total = 0.0;
        let mut prev = to_basis(poses[0][i_hand].t) - to_basis(poses[0][i_upper].t);
        for frame in poses.iter().skip(1) {
            let cur = to_basis(frame[i_hand].t) - to_basis(frame[i_upper].t);
            total += (cur - prev).length();
            prev = cur;
        }
        total
    };
    let aim_hand = forced_hand.unwrap_or_else(|| {
        u32::from(hand_travel(i_l_hand, i_l_upper) > hand_travel(i_r_hand, i_r_upper))
    });
    let (s_upper, s_fore, s_hand, s_finger) = if aim_hand == 1 {
        (i_l_upper, i_l_fore, i_l_hand, i_l_finger)
    } else {
        (i_r_upper, i_r_fore, i_r_hand, i_r_finger)
    };

    let mut frames = Vec::with_capacity(n);
    let mut rel_hand = Vec::with_capacity(n);
    for frame in &poses {
        let r_u = to_basis(frame[i_r_upper].t);
        let r_f = to_basis(frame[i_r_fore].t);
        let r_h = to_basis(frame[i_r_hand].t);
        let l_u = to_basis(frame[i_l_upper].t);
        let l_f = to_basis(frame[i_l_fore].t);
        let l_h = to_basis(frame[i_l_hand].t);
        let side_u = to_basis(frame[s_upper].t);
        let side_f = to_basis(frame[s_fore].t);
        let side_h = to_basis(frame[s_hand].t);
        let side_fg = to_basis(frame[s_finger].t);
        rel_hand.push(side_h - side_u);

        let aim = unit_clamped(side_h - side_f);
        let shoulder = unit_clamped(r_u - l_u);
        let neck = unit_clamped(to_basis(frame[i_head].t) - to_basis(frame[i_spine].t));
        let palm = unit_clamped(side_fg - side_h);

        let arm_data = |u: DVec3, el: DVec3, w: DVec3| -> (DVec3, DVec3, f64) {
            let reach = unit_clamped(w - u);
            let fore = el - u;
            let ext = (w - u).length() / (fore.length() + (w - el).length());
            let pole = unit_clamped(fore - reach * fore.dot(reach));
            (pole, reach, ext)
        };
        let (r_pole, r_reach, r_ext) = arm_data(r_u, r_f, r_h);
        let (l_pole, l_reach, l_ext) = arm_data(l_u, l_f, l_h);
        let row = [
            aim.x as f32,
            aim.y as f32,
            aim.z as f32,
            shoulder.x as f32,
            shoulder.y as f32,
            shoulder.z as f32,
            neck.x as f32,
            neck.y as f32,
            neck.z as f32,
            palm.x as f32,
            palm.y as f32,
            palm.z as f32,
            r_pole.x as f32,
            r_pole.y as f32,
            r_pole.z as f32,
            r_reach.x as f32,
            r_reach.y as f32,
            r_reach.z as f32,
            r_ext as f32,
            l_pole.x as f32,
            l_pole.y as f32,
            l_pole.z as f32,
            l_reach.x as f32,
            l_reach.y as f32,
            l_reach.z as f32,
            l_ext as f32,
        ];
        if !row.iter().all(|v| v.is_finite()) {
            return Err(format!("Pose drive-by no finita en {}", clip.name));
        }
        frames.push(row);
    }

    // np.gradient along axis 0: forward at 0, central in interior, backward at n-1.
    let mut vel = Vec::with_capacity(n);
    for i in 0..n {
        let g = if i == 0 {
            rel_hand[1] - rel_hand[0]
        } else if i + 1 == n {
            rel_hand[n - 1] - rel_hand[n - 2]
        } else {
            (rel_hand[i + 1] - rel_hand[i - 1]) * 0.5
        };
        vel.push(g);
    }
    let lo = ((n as f64) * 0.1).round().max(1.0) as usize;
    let hi = (((n as f64) * 0.85).round().max(2.0) as usize).min(n);
    let mut peak = lo;
    let mut best = -1.0f64;
    for (i, v) in vel.iter().enumerate().take(hi).skip(lo) {
        let m = v.length();
        if m > best {
            best = m;
            peak = i;
        }
    }
    let launch_v = unit_clamped(vel[peak]);
    Ok(BakedDriveByClip {
        frames,
        duration: clip.duration,
        aim_hand,
        release: peak as f32 / (n - 1) as f32,
        launch: [launch_v.x as f32, launch_v.y as f32, launch_v.z as f32],
    })
}

fn build_driver_poses(
    disc: &Disc,
    anim_entries: &[img::Entry],
    skel: &PedSkeleton,
) -> Result<Vec<u8>, String> {
    let mut out = Vec::with_capacity(512 * 1024);
    out.extend_from_slice(b"DBV4");
    for (group_idx, group) in DRIVEBY_GROUPS.iter().enumerate() {
        let dict_name = format!("veh@driveby{group}.xad");
        let res = read_img_resource(disc, anim_entries, ANIM_IMG, &dict_name)?;
        let clips = anim::read_dictionary(&res)?;
        let by_name: HashMap<String, &anim::Animation> = clips
            .iter()
            .map(|c| (c.name.to_ascii_lowercase(), c))
            .collect();
        for seat in DRIVEBY_SEATS {
            let loop_key = format!("{seat}_aim_loop");
            let Some(&aim_clip) = by_name.get(&loop_key) else {
                if seat == "ds" || seat == "ps" || matches!(*group, "std" | "van" | "truck") {
                    return Err(format!("Falta {loop_key} en {dict_name}"));
                }
                out.extend_from_slice(&0u32.to_le_bytes());
                continue;
            };
            let get = |k: String| -> Result<&anim::Animation, String> {
                by_name
                    .get(&k)
                    .copied()
                    .ok_or_else(|| format!("Falta {k} en {dict_name}"))
            };
            let aim_hand = u32::from(group_idx < 4 && (seat == "ds" || seat == "bl"));
            let aim = bake_driveby_clip(skel, aim_clip, Some(aim_hand))?;
            let entry = bake_driveby_clip(skel, get(format!("{seat}_aim_in"))?, Some(aim_hand))?;
            let leave = bake_driveby_clip(skel, get(format!("{seat}_aim_out"))?, Some(aim_hand))?;
            let throw_name = if seat == "ds" {
                "throw_grenade".to_string()
            } else {
                format!("{seat}_throw_grenade")
            };
            let throwing = bake_driveby_clip(skel, get(throw_name)?, None)?;
            for count in [
                aim.frames.len() as u32,
                entry.frames.len() as u32,
                leave.frames.len() as u32,
                throwing.frames.len() as u32,
                aim.aim_hand,
                throwing.aim_hand,
            ] {
                out.extend_from_slice(&count.to_le_bytes());
            }
            for f in [
                entry.duration,
                leave.duration,
                throwing.duration,
                throwing.release,
                throwing.launch[0],
                throwing.launch[1],
                throwing.launch[2],
            ] {
                out.extend_from_slice(&f.to_le_bytes());
            }
            for bank in [&aim.frames, &entry.frames, &leave.frames, &throwing.frames] {
                for row in bank {
                    for val in row {
                        out.extend_from_slice(&val.to_le_bytes());
                    }
                }
            }
        }
    }
    Ok(out)
}

// --- San Andreas readers for NPCGait.bin ---

struct SaDffSkeleton {
    rest_r: Vec<DMat3>,
    rest_t: Vec<DVec3>,
    parents: Vec<i32>,
    tags: Vec<i16>,
}

fn rw_chunks(data: &[u8], mut start: usize, end: usize) -> Result<Vec<(u32, usize, usize)>, String> {
    let mut out = Vec::new();
    while start + 12 <= end {
        let kind = u32::from_le_bytes(data[start..start + 4].try_into().unwrap());
        let size = u32::from_le_bytes(data[start + 4..start + 8].try_into().unwrap()) as usize;
        let stop = start
            .checked_add(12)
            .and_then(|v| v.checked_add(size))
            .ok_or("Desbordamiento de chunk RenderWare")?;
        if stop > end {
            return Err("Chunk RenderWare truncado".into());
        }
        out.push((kind, start + 12, stop));
        start = stop;
    }
    Ok(out)
}

fn rw_child(data: &[u8], start: usize, end: usize, kind: u32) -> Result<(usize, usize), String> {
    for (k, s, e) in rw_chunks(data, start, end)? {
        if k == kind {
            return Ok((s, e));
        }
    }
    Err(format!("Falta chunk RenderWare {kind}"))
}

fn parse_sa_dff(data: &[u8]) -> Result<SaDffSkeleton, String> {
    let (a, b) = rw_child(data, 0, data.len(), 16)?;
    let (fs, fe) = rw_child(data, a, b, 14)?;
    let (gls, gle) = rw_child(data, a, b, 26)?;
    let (gs, ge) = rw_child(data, gls, gle, 15)?;
    let (body, _) = rw_child(data, gs, ge, 1)?;
    if body + 16 > data.len() {
        return Err("Geometría DFF truncada".into());
    }
    let nv = u32::from_le_bytes(data[body + 8..body + 12].try_into().unwrap()) as usize;
    let (es, ee) = rw_child(data, gs, ge, 3)?;
    let (skin_off, _) = rw_child(data, es, ee, 278)?;
    if skin_off + 4 > data.len() {
        return Err("Skin PLG truncado".into());
    }
    let nb = data[skin_off] as usize;
    let used = data[skin_off + 1] as usize;
    let inv_off = skin_off + 4 + used + 20 * nv;
    if inv_off + nb * 64 > data.len() {
        return Err("Matrices inversas de Skin PLG truncadas".into());
    }
    let mut rest_r = Vec::with_capacity(nb);
    let mut rest_t = Vec::with_capacity(nb);
    for i in 0..nb {
        let base = inv_off + i * 64;
        let mut raw = [0.0f64; 16];
        for k in 0..16 {
            raw[k] = f32::from_le_bytes(data[base + k * 4..base + k * 4 + 4].try_into().unwrap())
                as f64;
        }
        // RW stores row-major 4x4 with column 3 = [0, 0, 0, 1].
        raw[3] = 0.0;
        raw[7] = 0.0;
        raw[11] = 0.0;
        raw[15] = 1.0;
        let inv_mat = DMat4::from_cols_array(&raw).transpose();
        let rest = inv_mat.inverse();
        let r0 = rest.row(0).truncate();
        let r1 = rest.row(1).truncate();
        let r2 = rest.row(2).truncate();
        let t = rest.row(3).truncate();
        // Column-vector rotation is M[:3,:3]^T -> columns are rows of M.
        rest_r.push(DMat3::from_cols(r0, r1, r2));
        rest_t.push(t);
    }

    let mut nodes: Option<Vec<(i32, i32, i32)>> = None;
    let frame_children = rw_chunks(data, fs, fe)?;
    for &(_, s, e) in frame_children.iter().skip(1) {
        for (kind, h, _) in rw_chunks(data, s, e)? {
            if kind == 286 && h + 12 <= data.len() {
                let count = u32::from_le_bytes(data[h + 8..h + 12].try_into().unwrap()) as usize;
                if count == nb && h + 20 + 12 * nb <= data.len() {
                    let mut list = Vec::with_capacity(nb);
                    for i in 0..nb {
                        let o = h + 20 + 12 * i;
                        let tag = i32::from_le_bytes(data[o..o + 4].try_into().unwrap());
                        let index = i32::from_le_bytes(data[o + 4..o + 8].try_into().unwrap());
                        let flags = i32::from_le_bytes(data[o + 8..o + 12].try_into().unwrap());
                        list.push((tag, index, flags));
                    }
                    nodes = Some(list);
                }
            }
        }
    }
    let nodes = nodes.ok_or("Falta jerarquía HAnim en DFF de SA")?;
    let mut parents = Vec::with_capacity(nb);
    let mut tags = Vec::with_capacity(nb);
    let mut stack = Vec::new();
    let mut parent = -1i32;
    for (tag, index, flags) in nodes {
        parents.push(parent);
        tags.push(tag as i16);
        if (flags & 2) != 0 {
            stack.push(parent);
        }
        parent = index;
        if (flags & 1) != 0 {
            parent = stack.pop().unwrap_or(-1);
        }
    }
    Ok(SaDffSkeleton {
        rest_r,
        rest_t,
        parents,
        tags,
    })
}

struct SaImgArchive {
    file: File,
    dir: HashMap<String, (u32, u16)>,
}

impl SaImgArchive {
    fn open(path: &Path) -> Result<Self, String> {
        let mut file = File::open(path).map_err(|e| format!("{}: {e}", path.display()))?;
        let mut head = [0u8; 8];
        file.read_exact(&mut head).map_err(|e| e.to_string())?;
        if &head[..4] != b"VER2" {
            return Err("gta3.img no tiene cabecera VER2".into());
        }
        let count = u32::from_le_bytes(head[4..8].try_into().unwrap()) as usize;
        let mut table = vec![0u8; count * 32];
        file.read_exact(&mut table).map_err(|e| e.to_string())?;
        let mut dir = HashMap::with_capacity(count);
        for e in table.chunks_exact(32) {
            let sector = u32::from_le_bytes(e[0..4].try_into().unwrap());
            let stream = u16::from_le_bytes(e[4..6].try_into().unwrap());
            let size = u16::from_le_bytes(e[6..8].try_into().unwrap());
            let used = if stream != 0 { stream } else { size };
            let raw_name = &e[8..32];
            let end = raw_name.iter().position(|&b| b == 0).unwrap_or(24);
            let name = String::from_utf8_lossy(&raw_name[..end]).to_ascii_lowercase();
            dir.insert(name, (sector, used));
        }
        Ok(Self { file, dir })
    }

    fn read_dff(&mut self, name: &str) -> Result<SaDffSkeleton, String> {
        let key = format!("{name}.dff").to_ascii_lowercase();
        let &(sector, size) = self
            .dir
            .get(&key)
            .ok_or_else(|| format!("Falta {key} en models/gta3.img"))?;
        self.file
            .seek(SeekFrom::Start(u64::from(sector) * 2048))
            .map_err(|e| e.to_string())?;
        let mut bytes = vec![0u8; usize::from(size) * 2048];
        self.file.read_exact(&mut bytes).map_err(|e| e.to_string())?;
        parse_sa_dff(&bytes)
    }
}

type NativeClip = HashMap<i16, (Vec<f64>, Vec<DQuat>)>;

fn read_sa_native_cycles(
    sa_root: &Path,
) -> Result<(HashMap<String, HashMap<&'static str, String>>, HashMap<String, NativeClip>), String> {
    let raw = fs::read_to_string(sa_root.join("data/animgrp.dat"))
        .map_err(|e| format!("data/animgrp.dat: {e}"))?;
    let lines: Vec<String> = raw
        .lines()
        .map(|l| l.split('#').next().unwrap_or("").trim().to_ascii_lowercase())
        .filter(|l| !l.is_empty())
        .collect();
    let mut groups: HashMap<String, HashMap<&'static str, String>> = HashMap::new();
    for (i, line) in lines.iter().enumerate() {
        let header: Vec<&str> = line.split(',').map(str::trim).collect();
        if header.len() == 4 && header[2] == "walkcycle" && i + 4 < lines.len() {
            let mut map = HashMap::new();
            for (k, &state) in GAIT_STATES.iter().enumerate() {
                map.insert(state, lines[i + 1 + k].clone());
            }
            groups.insert(header[0].to_string(), map);
        }
    }
    let mut needed = HashSet::new();
    for &(_, _, group) in &PROFILES {
        let g = groups
            .get(group)
            .ok_or_else(|| format!("Falta grupo {group} en data/animgrp.dat"))?;
        for &state in &GAIT_STATES[..3] {
            needed.insert(g[state].clone());
        }
    }
    let override_ifp = sa_root.join("modloader/Immersive Weapons Pack/VI - Animations/anim/ped.ifp");
    let ifp_path = if override_ifp.is_file() {
        override_ifp
    } else {
        sa_root.join("anim/ped.ifp")
    };
    let bytes = fs::read(&ifp_path).map_err(|e| format!("{}: {e}", ifp_path.display()))?;
    if bytes.len() < 36 || &bytes[..4] != b"ANP3" {
        return Err("anim/ped.ifp no tiene cabecera ANP3".into());
    }
    let count = u32::from_le_bytes(bytes[32..36].try_into().unwrap()) as usize;
    let mut pos = 36usize;
    let mut clips = HashMap::new();
    for _ in 0..count {
        if pos + 36 > bytes.len() {
            return Err("anim/ped.ifp truncado".into());
        }
        let raw_name = &bytes[pos..pos + 24];
        let end = raw_name.iter().position(|&b| b == 0).unwrap_or(24);
        let name = String::from_utf8_lossy(&raw_name[..end]).to_ascii_lowercase();
        let bones = u32::from_le_bytes(bytes[pos + 24..pos + 28].try_into().unwrap()) as usize;
        pos += 36;
        let want = needed.contains(&name);
        let mut tracks: NativeClip = HashMap::new();
        for _ in 0..bones {
            if pos + 36 > bytes.len() {
                return Err("Pista truncada en anim/ped.ifp".into());
            }
            let kind = u32::from_le_bytes(bytes[pos + 24..pos + 28].try_into().unwrap());
            let length = u32::from_le_bytes(bytes[pos + 28..pos + 32].try_into().unwrap()) as usize;
            let tag = u32::from_le_bytes(bytes[pos + 32..pos + 36].try_into().unwrap()) as i16;
            pos += 36;
            let stride = match kind {
                1 => 20,
                2 => 32,
                3 => 10,
                4 => 16,
                _ => return Err(format!("Formato de pista ANP3 desconocido: {kind}")),
            };
            let block_len = length * stride;
            if pos + block_len > bytes.len() {
                return Err("Bloque de fotogramas truncado en anim/ped.ifp".into());
            }
            let block = &bytes[pos..pos + block_len];
            pos += block_len;
            if !want || length == 0 {
                continue;
            }
            if kind != 3 && kind != 4 {
                return Err(format!("Formato de pista inesperado {kind} en {name}"));
            }
            let mut times = Vec::with_capacity(length);
            let mut rots = Vec::with_capacity(length);
            for k in 0..length {
                let o = k * stride;
                let qx = i16::from_le_bytes(block[o..o + 2].try_into().unwrap()) as f64;
                let qy = i16::from_le_bytes(block[o + 2..o + 4].try_into().unwrap()) as f64;
                let qz = i16::from_le_bytes(block[o + 4..o + 6].try_into().unwrap()) as f64;
                let qw = i16::from_le_bytes(block[o + 6..o + 8].try_into().unwrap()) as f64;
                let t = i16::from_le_bytes(block[o + 8..o + 10].try_into().unwrap()) as f64 / 60.0;
                times.push(t);
                rots.push(DQuat::from_xyzw(qx, qy, qz, qw).normalize());
            }
            tracks.insert(tag, (times, rots));
        }
        if want {
            clips.insert(name, tracks);
        }
    }
    for req in &needed {
        if !clips.contains_key(req) {
            return Err(format!("Falta animación nativa de SA {req} en ped.ifp"));
        }
    }
    Ok((groups, clips))
}

fn slerp_track(times: &[f64], rots: &[DQuat], t: f64) -> DQuat {
    if times.len() <= 1 || t <= times[0] {
        return rots[0];
    }
    let last = times.len() - 1;
    if t >= times[last] {
        return rots[last];
    }
    let hi = times.partition_point(|&x| x < t).clamp(1, last);
    let lo = hi - 1;
    let span = times[hi] - times[lo];
    let alpha = if span > 1e-12 { (t - times[lo]) / span } else { 0.0 };
    rots[lo].slerp(rots[hi], alpha)
}

fn native_foot_gap(dff: &SaDffSkeleton, native: &NativeClip, phases: &[f64]) -> Result<Vec<[f64; 2]>, String> {
    let nb = dff.tags.len();
    let idx44 = dff
        .tags
        .iter()
        .position(|&t| t == 44)
        .ok_or("Falta hueso 44 en DFF SA")?;
    let idx54 = dff
        .tags
        .iter()
        .position(|&t| t == 54)
        .ok_or("Falta hueso 54 en DFF SA")?;
    let mut local_r = vec![DMat3::IDENTITY; nb];
    let mut local_t = vec![DVec3::ZERO; nb];
    for i in 0..nb {
        let p = dff.parents[i];
        if p >= 0 {
            let p = p as usize;
            let rp_inv = dff.rest_r[p].transpose();
            local_r[i] = rp_inv * dff.rest_r[i];
            local_t[i] = rp_inv * (dff.rest_t[i] - dff.rest_t[p]);
        } else {
            local_r[i] = dff.rest_r[i];
            local_t[i] = dff.rest_t[i];
        }
    }
    let duration = native
        .values()
        .filter_map(|(t, _)| t.last().copied())
        .fold(0.0f64, f64::max);
    let mut out = Vec::with_capacity(phases.len());
    for &ph in phases {
        let t_sec = ph * duration;
        let mut wr = vec![DMat3::IDENTITY; nb];
        let mut wt = vec![DVec3::ZERO; nb];
        for i in 0..nb {
            let lr = if let Some((times, rots)) = native.get(&dff.tags[i]) {
                DMat3::from_quat(slerp_track(times, rots, t_sec))
            } else {
                local_r[i]
            };
            let p = dff.parents[i];
            if p >= 0 {
                let p = p as usize;
                wr[i] = wr[p] * lr;
                wt[i] = wt[p] + wr[p] * local_t[i];
            } else {
                wr[i] = lr;
                wt[i] = local_t[i];
            }
        }
        let d = wt[idx44] - wt[idx54];
        out.push([d.y, d.z]);
    }
    Ok(out)
}

fn entry_phase(native_gap: &[[f64; 2]], donor_gap: &[[f64; 2]], phases: &[f64]) -> f64 {
    let m = phases.len();
    let n = donor_gap.len();
    let mut a = vec![[0.0f64; 2]; m];
    for (i, &ph) in phases.iter().enumerate() {
        let u = ph * (n - 1) as f64;
        let lo = (u.floor() as usize).min(n - 1);
        let hi = (lo + 1).min(n - 1);
        let f = u - lo as f64;
        a[i][0] = donor_gap[lo][0] * (1.0 - f) + donor_gap[hi][0] * f;
        a[i][1] = donor_gap[lo][1] * (1.0 - f) + donor_gap[hi][1] * f;
    }
    let mean = |arr: &[[f64; 2]]| -> [f64; 2] {
        let mut s = [0.0; 2];
        for v in arr {
            s[0] += v[0];
            s[1] += v[1];
        }
        [s[0] / arr.len() as f64, s[1] / arr.len() as f64]
    };
    let ma = mean(&a);
    let mb = mean(native_gap);
    for v in &mut a {
        v[0] -= ma[0];
        v[1] -= ma[1];
    }
    let b: Vec<[f64; 2]> = native_gap
        .iter()
        .map(|v| [v[0] - mb[0], v[1] - mb[1]])
        .collect();
    let mut best_i = 0usize;
    let mut best_score = f64::NEG_INFINITY;
    for shift in 0..m {
        let mut score = 0.0;
        for k in 0..m {
            let ak = a[(k + shift) % m];
            score += ak[0] * b[k][0] + ak[1] * b[k][1];
        }
        if score > best_score {
            best_score = score;
            best_i = shift;
        }
    }
    phases[best_i]
}

fn support_phase_origin(y_left_minus_right: &[f64], shift: f64) -> Result<f32, String> {
    if y_left_minus_right.len() < 2 {
        return Err("Clip móvil demasiado corto".into());
    }
    let gap = &y_left_minus_right[..y_left_minus_right.len() - 1];
    let n = gap.len();
    let mean = gap.iter().sum::<f64>() / n as f64;
    let mut re = 0.0f64;
    let mut im = 0.0f64;
    for (i, &g) in gap.iter().enumerate() {
        let ang = std::f64::consts::TAU * (i as f64 / n as f64);
        let d = g - mean;
        re += d * ang.cos();
        im += d * ang.sin();
    }
    let mag = re.hypot(im);
    if !mag.is_finite() || mag / (n as f64) < 0.01 {
        return Err("Sin zancada izquierda/derecha en clip móvil de IV".into());
    }
    let raw = im.atan2(re) / std::f64::consts::TAU - shift;
    Ok(raw.rem_euclid(1.0) as f32)
}

fn quantile_linear(mut values: Vec<f64>, q: f64) -> f64 {
    values.sort_by(|a, b| a.total_cmp(b));
    let pos = q * (values.len() - 1) as f64;
    let lo = pos.floor() as usize;
    let hi = (lo + 1).min(values.len() - 1);
    let frac = pos - lo as f64;
    values[lo] * (1.0 - frac) + values[hi] * frac
}

fn foot_height(poses: &[Vec<WorldTransform>], donor: &HashMap<i16, usize>) -> f64 {
    let feet = [donor[&43], donor[&44], donor[&53], donor[&54]];
    let mins: Vec<f64> = poses
        .iter()
        .map(|f| {
            feet.iter()
                .map(|&idx| f[idx].t.z)
                .fold(f64::INFINITY, f64::min)
        })
        .collect();
    quantile_linear(mins, 0.10)
}

fn retarget_clip(
    dff: &SaDffSkeleton,
    donor: &HashMap<i16, usize>,
    ref_world: &[WorldTransform],
    poses: &[Vec<WorldTransform>],
) -> Result<(Vec<[[f32; 4]; 21]>, f64), String> {
    let n = poses.len();
    let nb = dff.tags.len();
    let ref0_r = DMat3::from_quat(ref_world[0].q);
    let rest0_inv = dff.rest_r[0].transpose();
    let fit_r: Vec<DMat3> = (0..nb).map(|i| ref0_r * rest0_inv * dff.rest_r[i]).collect();

    let tag_to_sa: HashMap<i16, usize> = dff
        .tags
        .iter()
        .enumerate()
        .map(|(i, &t)| (t, i))
        .collect();
    let chain_map: HashMap<i16, i16> = CHAINS.into_iter().collect();
    let tag_to_out: HashMap<i16, usize> = TAG_NAMES
        .iter()
        .enumerate()
        .map(|(i, &(t, _))| (t, i))
        .collect();

    let mut samples = vec![[[0.0f32; 4]; 21]; n];
    let mut max_err = 0.0f64;

    for f in 0..n {
        let mut worlds = vec![DMat3::IDENTITY; nb];
        for i in 0..nb {
            let parent = dff.parents[i];
            let tag = dff.tags[i];
            if parent < 0 {
                worlds[i] = DMat3::from_quat(poses[f][0].q);
                continue;
            }
            let p = parent as usize;
            let Some(&j) = donor.get(&tag) else {
                worlds[i] = worlds[p] * (fit_r[p].transpose() * fit_r[i]);
                continue;
            };
            let pose_r = DMat3::from_quat(poses[f][j].q);
            let ref_r = DMat3::from_quat(ref_world[j].q);
            let mut desired = pose_r * ref_r.transpose() * fit_r[i];
            if let Some(&child_tag) = chain_map.get(&tag) {
                let child_sa = tag_to_sa[&child_tag];
                let axis = dff.rest_r[i].transpose() * (dff.rest_t[child_sa] - dff.rest_t[i]);
                let current = (desired * axis).normalize();
                let target = (poses[f][donor[&child_tag]].t - poses[f][j].t).normalize();
                let dot = current.dot(target);
                if dot < -0.9999 {
                    return Err(format!("Ejes antiparalelos en tag {tag}"));
                }
                let cross = current.cross(target);
                let norm = cross.length();
                if norm > 1e-12 {
                    let angle = norm.atan2(dot);
                    let turn = DMat3::from_quat(DQuat::from_axis_angle(cross / norm, angle));
                    desired = turn * desired;
                }
                let err = ((desired * axis).normalize() - target).abs().max_element();
                if err > max_err {
                    max_err = err;
                }
            }
            worlds[i] = desired;
            let rel = worlds[p].transpose() * desired;
            let q = DQuat::from_mat3(&rel).normalize();
            if let Some(&out_i) = tag_to_out.get(&tag) {
                samples[f][out_i] = [q.x as f32, q.y as f32, q.z as f32, q.w as f32];
            }
        }
    }
    if max_err > 1e-5
        || samples
            .iter()
            .any(|f| f.iter().any(|q| !q.iter().all(|v| v.is_finite())))
    {
        return Err(format!("Error de dirección de extremidad al retargetear: {max_err:.6e}"));
    }
    Ok((samples, max_err))
}

fn shift_tracks(local: &mut [[[f32; 4]; 21]], root: &mut [[f32; 7]], shift: f64) {
    if shift == 0.0 {
        return;
    }
    let n = root.len();
    let old_local = local.to_vec();
    let old_root = root.to_vec();
    let phases: Vec<f64> = (0..n).map(|i| i as f64 / (n - 1) as f64).collect();
    for (dst_i, &ph) in phases.iter().enumerate() {
        let shifted = (ph + shift).rem_euclid(1.0);
        let u = shifted * (n - 1) as f64;
        let lo = (u.floor() as usize).min(n - 1);
        let hi = (lo + 1).min(n - 1);
        let f = u - lo as f64;
        for j in 0..21 {
            let qa = DQuat::from_xyzw(
                old_local[lo][j][0] as f64,
                old_local[lo][j][1] as f64,
                old_local[lo][j][2] as f64,
                old_local[lo][j][3] as f64,
            )
            .normalize();
            let qb = DQuat::from_xyzw(
                old_local[hi][j][0] as f64,
                old_local[hi][j][1] as f64,
                old_local[hi][j][2] as f64,
                old_local[hi][j][3] as f64,
            )
            .normalize();
            let q = qa.slerp(qb, f);
            local[dst_i][j] = [q.x as f32, q.y as f32, q.z as f32, q.w as f32];
        }
        let rqa = DQuat::from_xyzw(
            old_root[lo][0] as f64,
            old_root[lo][1] as f64,
            old_root[lo][2] as f64,
            old_root[lo][3] as f64,
        )
        .normalize();
        let rqb = DQuat::from_xyzw(
            old_root[hi][0] as f64,
            old_root[hi][1] as f64,
            old_root[hi][2] as f64,
            old_root[hi][3] as f64,
        )
        .normalize();
        let rq = rqa.slerp(rqb, f);
        root[dst_i][0] = rq.x as f32;
        root[dst_i][1] = rq.y as f32;
        root[dst_i][2] = rq.z as f32;
        root[dst_i][3] = rq.w as f32;
        for k in 4..7 {
            root[dst_i][k] =
                (old_root[lo][k] as f64 * (1.0 - f) + old_root[hi][k] as f64 * f) as f32;
        }
    }
}

fn build_npc_gaits_and_movers(
    sa_root: &Path,
    disc: &Disc,
    anim_entries: &[img::Entry],
    skel: &PedSkeleton,
) -> Result<(Vec<u8>, String), String> {
    let (groups, native) = read_sa_native_cycles(sa_root)?;
    let mut gta3 = SaImgArchive::open(&sa_root.join("models/gta3.img"))?;
    let mut donor = HashMap::with_capacity(TAG_NAMES.len());
    for &(tag, name) in &TAG_NAMES {
        let idx = skel
            .name_to_index
            .get(name)
            .copied()
            .ok_or_else(|| format!("Falta hueso {name} en esqueleto IV"))?;
        donor.insert(tag, idx);
    }

    // Load all 15 movement dictionaries once and reuse for both NPCGait.bin and NPCMovers.dat.
    let mut dicts: BTreeMap<&str, Vec<anim::Animation>> = BTreeMap::new();
    for &(profile, _, _) in &PROFILES {
        let res = read_img_resource(disc, anim_entries, ANIM_IMG, &format!("{profile}.xad"))?;
        dicts.insert(profile, anim::read_dictionary(&res)?);
    }

    // 1. Build NPCMovers.dat (sorted by profile name, matching iv-extractor --npc-movers).
    let mut movers = String::from("NPM1\n");
    for (&profile, clips) in &dicts {
        for clip in clips {
            let name = clip.name.to_ascii_lowercase();
            if !MOVER_CLIPS.contains(&name.as_str()) {
                continue;
            }
            let Some(track) = clip.tracks.iter().find(|t| t.kind == anim::MOVER_TRANSLATION) else {
                continue;
            };
            let _ = write!(movers, "{profile} {name} {} {}", clip.duration, clip.frames);
            let mut prev = track.sample(0);
            let mut dist = 0.0f32;
            for f in 0..clip.frames {
                let p = track.sample(f);
                dist += (p[0] - prev[0]).hypot(p[1] - prev[1]);
                let _ = write!(movers, " {dist}");
                prev = p;
            }
            movers.push('\n');
        }
    }

    // 2. Build NPCGait.bin (NG03).
    let phases: Vec<f64> = (0..120).map(|i| i as f64 / 120.0).collect();
    let leg_l = (skel.rest_world[donor[&42]].t - skel.rest_world[donor[&41]].t).length()
        + (skel.rest_world[donor[&43]].t - skel.rest_world[donor[&42]].t).length();
    let leg_r = (skel.rest_world[donor[&52]].t - skel.rest_world[donor[&51]].t).length()
        + (skel.rest_world[donor[&53]].t - skel.rest_world[donor[&52]].t).length();
    let leg_length = ((leg_l + leg_r) * 0.5) as f32;

    let mut clip_blobs: Vec<Vec<u8>> = Vec::with_capacity(120);
    let all_states: Vec<&str> = GAIT_STATES
        .iter()
        .chain(GAIT_TURNS.iter())
        .chain(GAIT_TRANSITIONS.iter())
        .copied()
        .collect();

    for &(profile, model_name, group) in &PROFILES {
        let dff = gta3.read_dff(model_name)?;
        let by_name: HashMap<String, &anim::Animation> = dicts[profile]
            .iter()
            .map(|c| (c.name.to_ascii_lowercase(), c))
            .collect();
        let walk_clip = by_name
            .get("walk")
            .copied()
            .ok_or_else(|| format!("Falta walk en {profile}.xad"))?;
        let walk_poses = evaluate_clip(skel, walk_clip);
        let stance_plane = foot_height(&walk_poses, &donor);

        for &state in &all_states {
            let Some(&clip) = by_name.get(state) else {
                if matches!(state, "walk" | "run" | "idle") {
                    return Err(format!("Falta {state} en {profile}.xad"));
                }
                continue;
            };
            let mut stride = 0.0f32;
            if let Some(track) = clip.tracks.iter().find(|t| t.kind == anim::MOVER_TRANSLATION) {
                let mut prev = track.sample(0);
                for f in 1..clip.frames {
                    let pt = track.sample(f);
                    stride += (pt[0] - prev[0]).hypot(pt[1] - prev[1]);
                    prev = pt;
                }
            }
            let turn_rate = if GAIT_TURNS.contains(&state) {
                let track = clip
                    .mover_rotation
                    .as_ref()
                    .ok_or_else(|| format!("Giro sin mover_rotation en {profile}/{state}"))?;
                let yaw = |frame: usize| -> f32 {
                    let q = track.sample(frame);
                    let angle = 2.0 * q[2].atan2(q[3]);
                    angle.sin().atan2(angle.cos())
                };
                let raw = yaw(clip.frames - 1) - yaw(0);
                let angle = if state.ends_with("_l") {
                    raw.max(-std::f32::consts::PI)
                } else {
                    raw.min(std::f32::consts::PI)
                };
                let rate = angle.abs() / clip.duration;
                if !(rate > 0.0 && rate <= 15.0) {
                    return Err(format!("Velocidad de giro inválida en {profile}/{state}: {rate}"));
                }
                rate
            } else {
                0.0
            };

            let mut poses = evaluate_clip(skel, clip);
            let (mut local, _err) = retarget_clip(&dff, &donor, &skel.rest_world, &poses)?;

            // Relative idle clips have an identity root and inherit WStart's entry carrier.
            if state == "idle" {
                let is_identity = poses.iter().all(|f| {
                    let r = DMat3::from_quat(f[0].q);
                    let id = DMat3::IDENTITY;
                    let r_close = (r.x_axis - id.x_axis).abs().max_element() < 1e-4
                        && (r.y_axis - id.y_axis).abs().max_element() < 1e-4
                        && (r.z_axis - id.z_axis).abs().max_element() < 1e-4;
                    r_close && f[0].t.abs().max_element() < 1e-4
                });
                if is_identity {
                    if let Some(&wstart_clip) = by_name.get("wstart") {
                        let start = evaluate_clip(skel, wstart_clip);
                        let carrier = start[0][0];
                        for frame in &mut poses {
                            for b in frame.iter_mut() {
                                b.q = (carrier.q * b.q).normalize();
                                b.t = carrier.t + carrier.q * b.t;
                            }
                        }
                    }
                }
            }

            let z_shift = stance_plane - foot_height(&poses, &donor);
            let mut root_keys: Vec<[f32; 7]> = poses
                .iter()
                .map(|f| {
                    let q = f[0].q;
                    let t = f[0].t;
                    [
                        q.x as f32,
                        q.y as f32,
                        q.z as f32,
                        q.w as f32,
                        t.x as f32,
                        t.y as f32,
                        (t.z + z_shift) as f32,
                    ]
                })
                .collect();

            let mut phase_origin = 0.0f32;
            if state != "idle" && !GAIT_TRANSITIONS.contains(&state) {
                if !(stride > 0.1 && stride < 8.0) {
                    return Err(format!("Desplazamiento IV inválido en {profile}/{state}: {stride}"));
                }
                let gap: Vec<[f64; 2]> = poses
                    .iter()
                    .map(|f| {
                        let d = f[donor[&44]].t - f[donor[&54]].t;
                        [d.y, d.z]
                    })
                    .collect();
                let base_state = state.split('_').next().unwrap_or(state);
                let native_name = &groups[group][base_state];
                let native_gap = native_foot_gap(&dff, &native[native_name], &phases)?;
                let shift = entry_phase(&native_gap, &gap, &phases);
                let y_gap: Vec<f64> = gap.iter().map(|v| v[0]).collect();
                phase_origin = support_phase_origin(&y_gap, shift)?;
                shift_tracks(&mut local, &mut root_keys, shift);
            }

            if !(2..=512).contains(&local.len()) || !(clip.duration > 0.0 && clip.duration < 30.0) {
                return Err(format!("Duración o fotogramas inválidos en {profile}/{state}"));
            }
            for frame in &local {
                for q in frame {
                    let norm = (q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]).sqrt();
                    if (norm - 1.0).abs() > 2e-5 {
                        return Err(format!("Cuaternión no unitario en {profile}/{state}"));
                    }
                }
            }

            let key_str = format!("{profile}/{state}");
            let mut key_buf = [0u8; 48];
            let kb = key_str.as_bytes();
            if kb.len() > 48 {
                return Err(format!("Clave demasiado larga: {key_str}"));
            }
            key_buf[..kb.len()].copy_from_slice(kb);

            let mut blob = Vec::with_capacity(66 + local.len() * (21 * 16 + 28));
            blob.extend_from_slice(&key_buf);
            blob.extend_from_slice(&(local.len() as u16).to_le_bytes());
            blob.extend_from_slice(&clip.duration.to_le_bytes());
            blob.extend_from_slice(&stride.to_le_bytes());
            blob.extend_from_slice(&turn_rate.to_le_bytes());
            blob.extend_from_slice(&phase_origin.to_le_bytes());
            for frame in &local {
                for q in frame {
                    for v in q {
                        blob.extend_from_slice(&v.to_le_bytes());
                    }
                }
            }
            for rk in &root_keys {
                for v in rk {
                    blob.extend_from_slice(&v.to_le_bytes());
                }
            }
            clip_blobs.push(blob);
        }
    }

    let mut payload = Vec::with_capacity(2 * 1024 * 1024);
    payload.extend_from_slice(b"NG03");
    payload.extend_from_slice(&(clip_blobs.len() as u16).to_le_bytes());
    payload.extend_from_slice(&(TAG_NAMES.len() as u16).to_le_bytes());
    for &(tag, _) in &TAG_NAMES {
        payload.extend_from_slice(&tag.to_le_bytes());
    }
    payload.extend_from_slice(&leg_length.to_le_bytes());
    for blob in clip_blobs {
        payload.extend_from_slice(&blob);
    }
    Ok((payload, movers))
}

fn write_atomic(path: &Path, bytes: &[u8]) -> Result<(), String> {
    let tmp = path.with_extension(format!("tmp-{}", std::process::id()));
    fs::write(&tmp, bytes).map_err(|e| format!("{}: {e}", path.display()))?;
    if let Err(e) = fs::rename(&tmp, path) {
        let _ = fs::remove_file(&tmp);
        return Err(format!("{}: {e}", path.display()));
    }
    Ok(())
}

/// Generate `DriverPoses.bin`, `NPCGait.bin`, and `NPCMovers.dat` into `cache`
/// from the already-opened Xbox 360 ISO and the owner's GTA San Andreas folder.
pub fn import(sa_root: &Path, cache: &Path, disc: &Disc, key: &ArchiveKey) -> Result<(), String> {
    let anim_entries = read_img_table(disc, key, ANIM_IMG)?;
    let skel = load_ped_skeleton(disc, key)?;
    let driver_poses = build_driver_poses(disc, &anim_entries, &skel)?;
    write_atomic(&cache.join(DRIVER_POSES), &driver_poses)?;
    let (npc_gait, npc_movers) = build_npc_gaits_and_movers(sa_root, disc, &anim_entries, &skel)?;
    write_atomic(&cache.join(NPC_GAIT), &npc_gait)?;
    write_atomic(&cache.join(NPC_MOVERS), npc_movers.as_bytes())?;
    Ok(())
}

pub fn ready(cache: &Path) -> bool {
    cache.join(DRIVER_POSES).is_file()
        && cache.join(NPC_GAIT).is_file()
        && cache.join(NPC_MOVERS).is_file()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn support_phase_origin_tracks_first_harmonic_and_shift() {
        let n = 60usize;
        let mut wave: Vec<f64> = (0..=n)
            .map(|i| (std::f64::consts::TAU * (i as f64 / n as f64 - 0.25)).cos())
            .collect();
        let o0 = support_phase_origin(&wave, 0.0).unwrap();
        assert!((o0 - 0.25).abs() < 1e-4, "expected 0.25, got {o0}");
        let o1 = support_phase_origin(&wave, 0.10).unwrap();
        assert!((o1 - 0.15).abs() < 1e-4, "expected 0.15, got {o1}");
        wave.fill(0.0);
        assert!(support_phase_origin(&wave, 0.0).is_err());
    }

    #[test]
    #[ignore = "requires an owned GTA IV Xbox 360 ISO and SA directory via env vars"]
    fn bakes_all_three_banks_from_xbox360_iso() {
        let iso = std::path::PathBuf::from(std::env::var_os("SA_TEST_XBOX360_ISO").unwrap());
        let sa = std::path::PathBuf::from(std::env::var_os("SA_DELIRIUM_TEST_ROOT").unwrap());
        let out = std::path::PathBuf::from(std::env::var_os("SA_TEST_BANKS_OUT").unwrap());
        fs::create_dir_all(&out).unwrap();
        let (disc, _, key) = crate::supported_disc(&iso).unwrap();
        import(&sa, &out, &disc, &key).unwrap();
        assert!(ready(&out));
        let poses = fs::read(out.join(DRIVER_POSES)).unwrap();
        assert_eq!(&poses[..4], b"DBV4");
        let gait = fs::read(out.join(NPC_GAIT)).unwrap();
        assert_eq!(&gait[..4], b"NG03");
        let clips = u16::from_le_bytes(gait[4..6].try_into().unwrap());
        assert_eq!(clips, 115);
        let movers = fs::read_to_string(out.join(NPC_MOVERS)).unwrap();
        assert!(movers.starts_with("NPM1\n"));
        assert!(movers.lines().count() > 100);
        println!(
            "Baked from {:?}: DriverPoses={} bytes, NPCGait={} bytes ({clips} clips), NPCMovers={} lines",
            iso.file_name().unwrap(),
            poses.len(),
            gait.len(),
            movers.lines().count()
        );
    }
}
