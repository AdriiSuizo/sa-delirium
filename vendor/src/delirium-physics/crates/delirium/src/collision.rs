use std::collections::HashMap;
use std::sync::Arc;

use crate::prelude::*;
use crate::Fma;

#[allow(non_snake_case)]
#[inline]
fn CELL() -> f32 { delirium_data::kv!(61) }
const NEAR_SLACK: f32 = 2.0;

#[allow(non_snake_case)]
#[inline]
pub fn EYE() -> f32 { delirium_data::kv!(62) }
#[allow(non_snake_case)]
#[inline]
fn RADIUS() -> f32 { delirium_data::kv!(63) }
#[allow(non_snake_case)]
#[inline]
fn STEP() -> f32 { delirium_data::kv!(10) }
#[allow(non_snake_case)]
#[inline]
fn GRAVITY() -> f32 { delirium_data::kv!(31) }

#[derive(Default)]
#[cfg_attr(feature = "bevy", derive(bevy_ecs::prelude::Resource))]
pub struct Collision {
    chunks: Vec<Option<Chunk>>,
    keys: HashMap<u64, u32>,
    free: Vec<u32>,
    cells: HashMap<(i32, i32), Vec<(u32, u32)>>,
    count: usize,
    neighbours: std::sync::RwLock<HashMap<(u32, u32), [Option<(u32, u32)>; 3]>>,
    #[cfg(feature = "portal")]
    pub holes: Vec<Hole>,
}

#[cfg(feature = "portal")]
#[derive(Clone, Copy, Debug)]
pub struct Hole {
    pub at: Vec3,
    pub rot: Quat,
    pub half: Vec2,
}

#[cfg(feature = "portal")]
impl Hole {
    pub fn has(&self, p: Vec3) -> bool {
        let l = self.rot.inverse() * (p - self.at);
        l.z < delirium_data::kv!(64) && l.z > -1.0 && (l.x / self.half.x).powi(2) + (l.y / self.half.y).powi(2) < 1.0
    }
}

#[derive(Clone, Debug, PartialEq)]
pub struct Face {
    pub n: Vec3,
    pub across: [Option<Vec3>; 3],
    pub raw: Option<[Vec3; 3]>,
    pub half: u8,
    pub rank: u32,
    pub mesh: Option<(Arc<crate::bvh::Tree>, u32)>,
    pub place: Option<(Quat, Vec3)>,
}

impl Face {
    pub fn of(m: &delirium_data::bounds::CollisionMesh) -> Vec<Option<Face>> {
        let tree = crate::bvh::Tree::new(m);
        (0..m.triangles.len()).map(|k| {
            let across = m.neighbours.get(k)?.map(|j| m.normals.get(j as usize).map(|&v| Vec3::from(v)));
            let raw = m.corners.get(k).filter(|&&c| c != m.triangles[k]).map(|c| c.map(|i| Vec3::from(m.positions[i as usize])));
            Some(Face { n: Vec3::from(*m.normals.get(k)?), across, raw, half: m.halves.get(k).copied().unwrap_or(0), rank: m.order.get(k).copied().unwrap_or(k as u32), mesh: tree.clone().map(|t| (t, k as u32)), place: None })
        }).collect()
    }

    pub fn turned(self, q: Quat, place: impl Fn(Vec3) -> Vec3) -> Face {
        let at = place(Vec3::ZERO);
        Face { n: q * self.n, across: self.across.map(|a| a.map(|a| q * a)), raw: self.raw.map(|r| r.map(place)), place: Some((q, at)), ..self }
    }
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Poly {
    pub first: (u32, u32),
    pub n: Vec3,
    pub edges: [Option<(Vec3, Vec3, Vec3)>; 4],
}

impl Poly {
    pub fn clamp(&self, hits: &mut [(Vec3, Vec3)], margin: f32) {
        let tol = margin * margin * delirium_data::kv!(65);
        let (mut axis, mut cos) = (self.n, 1.0);
        for (a, b, m) in self.edges.into_iter().flatten() {
            if hits.iter().any(|&(p, _)| on_edge(a, b, p, tol)) {
                expand_cone(&mut axis, &mut cos, m);
            }
        }
        for (_, n) in hits.iter_mut() {
            *n = clamp_to_cone(*n, axis, cos);
        }
    }
}

fn on_edge(a: Vec3, b: Vec3, p: Vec3, tol: f32) -> bool {
    let (ap, e) = (p - a, b - a);
    let t = ap.dot(e);
    let c = if !(t > delirium_data::kv!(66)) {
        -ap
    } else if e.dot(e) - t > delirium_data::kv!(66) {
        e * (t * crate::gjk::recip(t + (e.dot(e) - t))) + -ap
    } else {
        e + -ap
    };
    let d = c + ap - ap;
    !(d.dot(d) > tol)
}

fn acos(x: f32) -> f32 {
    if x <= -1.0 { std::f32::consts::PI } else if x >= 1.0 { 0.0 } else { (x as f64).acos() as f32 }
}

fn turn(axis: Vec3, angle: f32, v: Vec3) -> Vec3 {
    let h = (angle * 0.5) as f64;
    let (w, q) = (h.cos() as f32, axis * h.sin() as f32);
    let d = v.z.fma(q.z, v.x.fma(q.x, q.y * v.y)) * 2.0;
    let c = Vec3::new(q.y.fma(v.z, -(q.z * v.y)), q.z.fma(v.x, -(v.z * q.x)), v.y.fma(q.x, -(q.y * v.x)));
    c * (w * 2.0) + (v * (w * w).fma(2.0, -1.0) + q * d)
}

fn expand_cone(axis: &mut Vec3, cos: &mut f32, v: Vec3) {
    let d = v.x.fma(axis.x, v.y.fma(axis.y, v.z * axis.z));
    if d >= *cos {
        return;
    }
    if *cos >= delirium_data::kv!(67) {
        let h = (v + *axis) * 0.5;
        let l2 = h.dot(h);
        *axis = if l2 > delirium_data::kv!(47) { h * crate::gjk::rsq(l2) } else { Vec3::Z };
        let c = (d + 1.0) * 0.5;
        *cos = if c > 0.0 { c.sqrt() } else { 0.0 };
    } else {
        let (a, b) = (acos(d), acos(*cos));
        let p = v.cross(*axis);
        *axis = turn(p * crate::gjk::rsq(p.dot(p)), (b - a) * 0.5, *axis);
        *cos = (((a + b) * 0.5) as f64).cos() as f32;
    }
}

fn clamp_to_cone(n: Vec3, axis: Vec3, cos: f32) -> Vec3 {
    if n.x.fma(axis.x, n.z.fma(axis.z, n.y * axis.y)) >= cos - delirium_data::kv!(68) {
        n
    } else if cos >= delirium_data::kv!(67) {
        axis
    } else {
        let p = axis.cross(n);
        turn(p * crate::gjk::rsq(p.dot(p)), acos(cos), axis)
    }
}

struct Chunk {
    triangles: Vec<[Vec3; 3]>,
    faces: Vec<Option<Face>>,
    materials: Vec<u8>,
    flags: Vec<u8>,
    host_two_sided:Vec<bool>,
    // Geometry is immutable during solves. Cache each face independently so
    // unrelated characters/surfaces never contend on a single writer lock.
    neighbours:Vec<std::sync::OnceLock<[Option<(u32,u32)>;3]>>,
    cells: Vec<(i32, i32)>,
    order: Vec<u32>,
    listed: Vec<bool>,
    meshes: Vec<ChunkMesh>,
    bounds: (Vec3, Vec3),
}

struct ChunkMesh {
    tree: Arc<crate::bvh::Tree>,
    ids: Vec<u32>,
    rot: Quat,
    pos: Vec3,
}

const FIXED: u64 = u64::MAX;

fn cell(v: f32) -> i32 {
    (v / CELL()).floor() as i32
}

impl Collision {
    pub fn add(&mut self, positions: &[[f32; 3]], triangles: &[[u32; 3]], materials: &[u8]) {
        self.add_chunk_flagged(FIXED, positions, triangles, materials, &[]);
    }

    /// Explicit native host surface metadata, separate from original polygon
    /// flags/materials. Original IV imports and subsequent ordinary adds retain
    /// their exact one-sided semantics.
    pub fn add_two_sided(&mut self,positions:&[[f32;3]],triangles:&[[u32;3]],materials:&[u8]){
        self.add_chunk_two_sided(FIXED,positions,triangles,materials);
    }
    /// The same native host surface, kept as one chunk per host entity so a
    /// moving map window adds and removes only the entities that changed.
    pub fn add_chunk_two_sided(&mut self,key:u64,positions:&[[f32;3]],triangles:&[[u32;3]],materials:&[u8]){
        let first=self.keys.get(&key).and_then(|&i|self.chunks[i as usize].as_ref()).map_or(0,|c|c.triangles.len());
        self.add_chunk_flagged(key,positions,triangles,materials,&[]);
        if let Some(&i)=self.keys.get(&key){if let Some(c)=self.chunks[i as usize].as_mut(){c.host_two_sided[first..].fill(true);}}
    }
    pub fn add_floor(&mut self, x: f32, y: f32, z: f32, half: f32, material: u8) {
        let size = (half / delirium_data::kv!(28)).max(1.0);
        let n = (half / size).ceil().max(1.0) as i32;
        let (mut p, mut t) = (vec![], vec![]);
        for i in -n..n {
            for j in -n..n {
                let (a, b, k) = (x + i as f32 * size, y + j as f32 * size, p.len() as u32);
                p.extend([[a, b, z], [a + size, b, z], [a + size, b + size, z], [a, b + size, z]]);
                t.extend([[k, k + 1, k + 2], [k, k + 2, k + delirium_data::kv!(0 int)]]);
            }
        }
        self.add(&p, &t, &vec![material; t.len()]);
    }

    pub fn add_flagged(&mut self, positions: &[[f32; 3]], triangles: &[[u32; 3]], materials: &[u8], flags: &[u8]) {
        self.add_chunk_flagged(FIXED, positions, triangles, materials, flags);
    }

    pub fn add_chunk(&mut self, key: u64, positions: &[[f32; 3]], triangles: &[[u32; 3]], materials: &[u8]) {
        self.add_chunk_flagged(key, positions, triangles, materials, &[]);
    }

    pub fn add_chunk_flagged(&mut self, key: u64, positions: &[[f32; 3]], triangles: &[[u32; 3]], materials: &[u8], flags: &[u8]) {
        self.add_chunk_faces(key, positions, triangles, materials, flags, &[]);
    }

    pub fn add_chunk_faces(&mut self, key: u64, positions: &[[f32; 3]], triangles: &[[u32; 3]], materials: &[u8], flags: &[u8],
                           faces: &[Option<Face>]) {
        let slot = match self.keys.get(&key) {
            Some(&s) => s,
            None => {
                let chunk = Chunk { triangles: vec![], faces: vec![], materials: vec![], flags: vec![], host_two_sided:vec![], neighbours:vec![], cells: vec![], order: vec![], listed: vec![], meshes: vec![], bounds: (Vec3::INFINITY, Vec3::NEG_INFINITY) };
                let s = match self.free.pop() {
                    Some(s) => { self.chunks[s as usize] = Some(chunk); s }
                    None => { self.chunks.push(Some(chunk)); self.chunks.len() as u32 - 1 }
                };
                self.keys.insert(key, s);
                s
            }
        };
        self.neighbours.get_mut().unwrap().clear();
        let chunk = self.chunks[slot as usize].as_mut().unwrap();
        let before = chunk.triangles.len();
        // An append can change the minimum adjacent triangle of an existing
        // face. Only this chunk participates in the original lookup predicate.
        chunk.neighbours.iter_mut().for_each(|c|{c.take();});
        let mut touched = std::collections::HashSet::<(i32, i32)>::from_iter(chunk.cells.iter().copied());
        for (k, t) in triangles.iter().enumerate() {
            let [Some(a), Some(b), Some(c)] = t.map(|i| positions.get(i as usize)) else { continue };
            let tri = [Vec3::from(*a), Vec3::from(*b), Vec3::from(*c)];
            let (lo, hi) = (tri[0].min(tri[1]).min(tri[2]), tri[0].max(tri[1]).max(tri[2]));
            if (hi - lo).truncate().max_element() > delirium_data::kv!(69) || !lo.is_finite() || !hi.is_finite() {
                continue;
            }
            let id = chunk.triangles.len() as u32;
            chunk.triangles.push(tri);
            chunk.neighbours.push(std::sync::OnceLock::new());
            chunk.faces.push(faces.get(k).cloned().flatten());
            chunk.order.push(faces.get(k).and_then(|f| f.as_ref()).map_or(id, |f| before as u32 + f.rank));
            let face = faces.get(k).and_then(|f| f.as_ref());
            chunk.listed.push(face.is_some_and(|f| f.mesh.is_some()));
            chunk.bounds = (chunk.bounds.0.min(lo), chunk.bounds.1.max(hi));
            if let Some(((tree, tri), place)) = face.and_then(|f| f.mesh.clone().map(|m| (m, f.place))) {
                let (rot, pos) = place.unwrap_or((Quat::IDENTITY, Vec3::ZERO));
                let m = match chunk.meshes.iter().position(|m| Arc::ptr_eq(&m.tree, &tree) && m.rot == rot && m.pos == pos) {
                    Some(m) => m,
                    None => {
                        chunk.meshes.push(ChunkMesh { ids: vec![u32::MAX; tree.count], tree, rot, pos });
                        chunk.meshes.len() - 1
                    }
                };
                if let Some(slot) = chunk.meshes[m].ids.get_mut(tri as usize) {
                    *slot = id;
                }
            }
            chunk.materials.push(materials.get(k).copied().unwrap_or(0));
            chunk.flags.push(flags.get(k).copied().unwrap_or(0));
            chunk.host_two_sided.push(false);
            for x in cell(lo.x)..=cell(hi.x) {
                for y in cell(lo.y)..=cell(hi.y) {
                    let occupants = self.cells.entry((x, y)).or_default();
                    // Consecutive triangles from this chunk already registered
                    // the cell. The set still handles appends across other chunks.
                    if occupants.last().is_none_or(|&(owner, _)| owner != slot) {
                        touched.insert((x, y));
                    }
                    occupants.push((slot, id));
                }
            }
        }
        chunk.cells = touched.into_iter().collect();
        self.count += chunk.triangles.len() - before;
    }

    pub fn has_chunk(&self, key: u64) -> bool {
        self.keys.contains_key(&key)
    }

    pub fn remove_chunk(&mut self, key: u64) {
        let Some(slot) = self.keys.remove(&key) else { return };
        let Some(chunk) = self.chunks[slot as usize].take() else { return };
        self.neighbours.get_mut().unwrap().clear();
        for c in &chunk.cells {
            if let Some(list) = self.cells.get_mut(c) {
                list.retain(|e| e.0 != slot);
                if list.is_empty() {
                    self.cells.remove(c);
                }
            }
        }
        self.count -= chunk.triangles.len();
        self.free.push(slot);
    }

    pub fn chunk_keys(&self) -> impl Iterator<Item = u64> + '_ {
        self.keys.keys().copied()
    }

    pub fn slot_key(&self, slot: u32) -> Option<u64> {
        self.keys.iter().find(|(_, s)| **s == slot).map(|(&k, _)| k)
    }

    /// Authored polygon metadata; bit0 is the original stair flag.
    pub fn poly_flags(&self,(slot,tri):(u32,u32))->u8 {
        self.chunks.get(slot as usize).and_then(Option::as_ref).and_then(|c|c.flags.get(tri as usize)).copied().unwrap_or(0)
    }

    pub fn len(&self) -> usize {
        self.count
    }

    pub fn triangles(&self) -> Vec<[Vec3; 3]> {
        self.chunks.iter().flatten().flat_map(|c| c.triangles.iter().copied()).collect()
    }

    pub fn tris_near(&self, lo: Vec3, hi: Vec3) -> Vec<([Vec3; 3], u8, [Vec3; 4], u8, u32, Poly)> {
        self.tuples(self.grid_near(lo, hi, NEAR_SLACK, |_, _| true),None)
    }

    /// Orient explicitly two-sided host faces toward the collider's accepted
    /// START position. A fast end pose beyond a face must not flip its CCD side.
    pub fn tris_near_from(&self,lo:Vec3,hi:Vec3,start:Vec3)->Vec<([Vec3;3],u8,[Vec3;4],u8,u32,Poly)>{
        self.tuples(self.grid_near(lo,hi,NEAR_SLACK,|_,_|true),Some(start))
    }
    /// Apply the caller's conservative shape bound before constructing face
    /// adjacency/cone metadata. Keep the original grid/slack selection and order.
    pub fn tris_near_from_bounded(&self,lo:Vec3,hi:Vec3,start:Vec3,blo:Vec3,bhi:Vec3)->Vec<([Vec3;3],u8,[Vec3;4],u8,u32,Poly)>{
        self.tuples(self.grid_near(lo,hi,NEAR_SLACK,|c,i|Self::tri_in_bounds(c.triangles[i as usize],blo,bhi)),Some(start))
    }
    fn tri_in_bounds(t:[Vec3;3],lo:Vec3,hi:Vec3)->bool{
        t[0].min(t[1]).min(t[2]).cmple(hi).all()&&t[0].max(t[1]).max(t[2]).cmpge(lo).all()
    }
    fn grid_near(&self, lo: Vec3, hi: Vec3, slack: f32, keep: impl Fn(&Chunk, u32) -> bool) -> Vec<(u32, u32)> {
        let chunk = |(s, _): (u32, u32)| self.chunks[s as usize].as_ref().unwrap();
        let mut ids: Vec<(u32, u32)> = (cell(lo.x)..=cell(hi.x)).flat_map(|x| (cell(lo.y)..=cell(hi.y)).map(move |y| (x, y)))
            .flat_map(|c| self.cells.get(&c).into_iter().flatten().copied()).filter(|&e| keep(chunk(e), e.1)).collect();
        let (blo, bhi) = (lo - Vec3::splat(slack), hi + Vec3::splat(slack));
        ids.retain(|&e| {
            let t = chunk(e).triangles[e.1 as usize];
            t[0].min(t[1]).min(t[2]).cmple(bhi).all() && t[0].max(t[1]).max(t[2]).cmpge(blo).all()
        });
        ids.sort_unstable();
        ids.dedup();
        ids.sort_by_key(|&(s, i)| (s, chunk((s, i)).order[i as usize]));
        ids
    }

    pub fn tris_in_part(&self, lo: Vec3, hi: Vec3, a: &crate::bvh::Frame, a_last: &crate::bvh::Frame, bmin: Vec3, bmax: Vec3)
        -> Vec<([Vec3; 3], u8, [Vec3; 4], u8, u32, Poly)> {
        self.tris_in_part_selected(lo,hi,a,a_last,bmin,bmax,|_|true)
    }
    pub fn tris_in_part_bounded(&self,lo:Vec3,hi:Vec3,a:&crate::bvh::Frame,a_last:&crate::bvh::Frame,bmin:Vec3,bmax:Vec3,blo:Vec3,bhi:Vec3)
        -> Vec<([Vec3;3],u8,[Vec3;4],u8,u32,Poly)>{
        self.tris_in_part_selected(lo,hi,a,a_last,bmin,bmax,|t|Self::tri_in_bounds(t,blo,bhi))
    }
    /// The exact caller-owned rejection predicate runs before metadata. It may
    /// inspect triangle vertices only; adjacency still comes from the full mesh.
    pub fn tris_in_part_filtered(&self,lo:Vec3,hi:Vec3,a:&crate::bvh::Frame,a_last:&crate::bvh::Frame,bmin:Vec3,bmax:Vec3,keep:impl Fn([Vec3;3])->bool)
        -> Vec<([Vec3;3],u8,[Vec3;4],u8,u32,Poly)>{
        self.tris_in_part_selected(lo,hi,a,a_last,bmin,bmax,keep)
    }
    fn tris_in_part_selected(&self,lo:Vec3,hi:Vec3,a:&crate::bvh::Frame,a_last:&crate::bvh::Frame,bmin:Vec3,bmax:Vec3,keep:impl Fn([Vec3;3])->bool)
        -> Vec<([Vec3;3],u8,[Vec3;4],u8,u32,Poly)>{
        let mut ids = self.grid_near(lo, hi, 0.0, |c, i| !c.listed[i as usize]);
        let mut listed = vec![];
        for (s, c) in self.chunks.iter().enumerate() {
            let Some(c) = c else { continue };
            if c.meshes.is_empty() || !(c.bounds.0 - Vec3::splat(0.5)).cmple(hi).all() || !(c.bounds.1 + Vec3::splat(0.5)).cmpge(lo).all() {
                continue;
            }
            for m in &c.meshes {
                let b = crate::bvh::Frame { rows: [m.rot * Vec3::X, m.rot * Vec3::Y, m.rot * Vec3::Z], t: m.pos };
                let q = crate::bvh::swept_obb(a, a_last, &b, &b, bmin, bmax);
                for p in crate::bvh::polygons(&m.tree.bvh, &q) {
                    listed.extend(m.tree.tris.get(p as usize).into_iter().flatten().filter_map(|&k| m.ids.get(k as usize).copied())
                        .filter(|&id| id != u32::MAX).map(|id| (s as u32, id)));
                }
            }
        }
        listed.sort_unstable();
        listed.dedup();
        ids.extend(listed);
        ids.sort_by_key(|&(s, i)| (s, self.chunks[s as usize].as_ref().unwrap().order[i as usize]));
        ids.retain(|&(s,i)|{
            let c=self.chunks[s as usize].as_ref().unwrap();let t=c.triangles[i as usize];
            // A caller's predicate must see the same winding as its old late
            // filter, including the accepted START side of native COL faces.
            let t=if c.host_two_sided[i as usize]{
                let n=c.faces[i as usize].as_ref().map_or_else(||(t[1]-t[0]).cross(t[2]-t[0]).normalize_or_zero(),|f|f.n);
                if (a_last.t-t[0]).dot(n)<0.{[t[0],t[2],t[1]]}else{t}
            }else{t};keep(t)
        });
        self.tuples(ids,Some(a_last.t))
    }

    fn tuples(&self, ids: Vec<(u32, u32)>, side:Option<Vec3>) -> Vec<([Vec3; 3], u8, [Vec3; 4], u8, u32, Poly)> {
        let chunk = |(s, _): (u32, u32)| self.chunks[s as usize].as_ref().unwrap();
        let tri = |e: (u32, u32)| chunk(e).triangles[e.1 as usize];
        let normal = |[a, b, c]: [Vec3; 3]| (b - a).cross(c - a).normalize_or_zero();
        let near: Vec<((u32, u32), [Vec3; 3])> = ids.into_iter().map(|e| (e, tri(e))).collect();
        let face = |e: (u32, u32)| chunk(e).faces.get(e.1 as usize).and_then(|f| f.as_ref());
        let quad = |a: (u32, u32)| face(a).is_some_and(|f| f.half == 1) && face((a.0, a.1 + 1)).is_some_and(|f| f.half == 2);
        near.into_iter().map(|(e, t)| {
            let (n, across) = match &chunk(e).faces[e.1 as usize] {
                Some(f) => (f.n, f.across),
                None => {
                    let find = || std::array::from_fn(|k| {
                        let (p, q) = (t[k], t[(k + 1) % delirium_data::kv!(0 int)]);
                        self.cells.get(&(cell(p.x), cell(p.y))).into_iter().flatten().copied()
                            .filter(|&o| o.0 == e.0).filter(|&e| { let o = tri(e); o != t && o.contains(&p) && o.contains(&q) }).min()
                    });
                    #[cfg(test)] let baseline=NEIGHBOUR_CACHE_BASELINE.with(|v|v.get());
                    #[cfg(not(test))] let baseline={static BASELINE:std::sync::OnceLock<bool>=std::sync::OnceLock::new();*BASELINE.get_or_init(||std::env::var_os("GTASA_PASSIVE_TEST_WINDOW").is_some()&&std::env::var_os("GTASA_SHARED_NEIGHBOUR_CACHE_BASELINE").is_some())};
                    let found=if baseline{
                        let cached=self.neighbours.read().unwrap().get(&e).copied();
                        cached.unwrap_or_else(||{let f=find();self.neighbours.write().unwrap().insert(e,f);f})
                    }else{*chunk(e).neighbours[e.1 as usize].get_or_init(find)};
                    (normal(t), found.map(|o| o.map(|o| normal(tri(o)))))
                }
            };
            let edge = |k: usize| across[k].map(|m| if n.dot(m.cross(t[(k + 1) % delirium_data::kv!(0 int)] - t[k])) > 0.0 {
                let h = n + m;
                h * crate::gjk::rsq(h.length_squared())
            } else {
                n
            });
            let edges = [edge(0), edge(1), edge(2)];
            let mask = (0..delirium_data::kv!(0 int)).filter(|&k| edges[k].is_some()).fold(0, |m, k| m | 1 << k);
            let first = if e.1 > 0 && quad((e.0, e.1 - 1)) { (e.0, e.1 - 1) } else { e };
            let sides = |t: [Vec3; 3], across: [Option<Vec3>; 3], k: usize| across[k].map(|m| (t[k], t[(k + 1) % delirium_data::kv!(0 int)], m));
            let poly = Poly { first, n, edges: if quad(first) {
                let (a, b) = (first, (first.0, first.1 + 1));
                let (fa, fb) = (face(a).unwrap().across, face(b).unwrap().across);
                [sides(tri(a), fa, 0), sides(tri(a), fa, 1), sides(tri(b), fb, 1), sides(tri(b), fb, 2)]
            } else {
                [sides(t, across, 0), sides(t, across, 1), sides(t, across, 2), None]
            } };
            let dirs=[n,edges[0].unwrap_or(n),edges[1].unwrap_or(n),edges[2].unwrap_or(n)];
            if chunk(e).host_two_sided[e.1 as usize]&&side.is_some_and(|p|(p-t[0]).dot(n)<0.){
                let poly=Poly{n:-poly.n,edges:poly.edges.map(|e|e.map(|(a,b,n)|(b,a,-n))),..poly};
                ([t[0],t[2],t[1]],chunk(e).materials[e.1 as usize],[-dirs[0],-dirs[3],-dirs[2],-dirs[1]],(mask&1)<<2|mask&2|(mask&4)>>2,e.0,poly)
            }else{(t,chunk(e).materials[e.1 as usize],dirs,mask,e.0,poly)}
        }).collect()
    }

    pub fn segment(&self, from: Vec3, to: Vec3) -> Option<(Vec3, Vec3)> {
        let (lo, hi) = (from.min(to), from.max(to));
        let mut best: Option<(f32, Vec3)> = None;
        for x in cell(lo.x)..=cell(hi.x) {
            for y in cell(lo.y)..=cell(hi.y) {
                for &(slot, id) in self.cells.get(&(x, y)).into_iter().flatten() {
                    let Some(chunk) = self.chunks[slot as usize].as_ref() else { continue };
                    let test = |id: u32| {
                        let face = chunk.faces[id as usize].as_ref();
                        let t = face.and_then(|f| f.raw).unwrap_or(chunk.triangles[id as usize]);
                        let n = face.map_or_else(|| (t[1] - t[0]).cross(t[2] - t[0]).normalize_or_zero(), |f| f.n);
                        (segment_triangle(t, n, from, to), n)
                    };
                    if chunk.faces[id as usize].as_ref().is_some_and(|f| f.half == 2) && test(id - 1).0.err().is_none_or(|c| c < delirium_data::kv!(1 int)) {
                        continue;
                    }
                    if let (Ok(f), n) = test(id) {
                        if best.is_none_or(|b| f < b.0) {
                            best = Some((f, n));
                        }
                    }
                }
            }
        }
        best.map(|(f, n)| (from + (to - from) * f, n))
    }

    pub fn cast(&self, from: Vec3, dir: Vec3, len: f32) -> Option<(f32, Vec3)> {
        self.hit(from, dir, len, false).map(|(t, n, _)| (t, n))
    }

    pub fn contact(&self, from: Vec3, dir: Vec3, len: f32) -> Option<(f32, Vec3, u8)> {
        self.hit(from, dir, len, false).map(|(t, n, (slot, id))| (t, n, self.chunks[slot as usize].as_ref().map_or(0, |c| c.materials[id as usize])))
    }

    pub fn blocked(&self, from: Vec3, dir: Vec3, len: f32) -> bool {
        let d = dir * len;
        let (mut x, mut y, end) = (cell(from.x), cell(from.y), (cell(from.x + d.x), cell(from.y + d.y)));
        let axis = |p: f32, d: f32, c: i32| {
            if d == 0.0 {
                return (0, f32::INFINITY, f32::INFINITY);
            }
            let s = if d > 0.0 { 1 } else { -1 };
            (s, (((c + (s > 0) as i32) as f32 * CELL()) - p) / d, CELL() / d.abs())
        };
        let ((sx, mut tx, dx), (sy, mut ty, dy)) = (axis(from.x, d.x, x), axis(from.y, d.y, y));
        loop {
            if self.cells.get(&(x, y)).into_iter().flatten().any(|&(slot, id)| {
                self.chunks[slot as usize].as_ref().is_some_and(|c| ray_triangle(c.triangles[id as usize], from, dir, len, None).is_some())
            }) {
                return true;
            }
            if (x, y) == end || tx.min(ty) > 1.0 {
                return false;
            }
            if tx < ty {
                x += sx;
                tx += dx;
            } else {
                y += sy;
                ty += dy;
            }
        }
    }

    fn hit(&self, from: Vec3, dir: Vec3, len: f32, ground: bool) -> Option<(f32, Vec3, (u32, u32))> {
        let to = from + dir * len;
        let (lo, hi) = (from.min(to), from.max(to));
        let mut best: Option<(f32, Vec3, (u32, u32))> = None;
        for x in cell(lo.x)..=cell(hi.x) {
            for y in cell(lo.y)..=cell(hi.y) {
                for &(slot, id) in self.cells.get(&(x, y)).into_iter().flatten() {
                    let Some(chunk) = self.chunks[slot as usize].as_ref() else { continue };
                    if let Some((t, n)) = ray_triangle(chunk.triangles[id as usize], from, dir, len, best.map(|b| b.0)) {
                        if ground && n.z.abs() < delirium_data::kv!(70) {
                            continue;
                        }
                        #[cfg(feature = "portal")]
                        if self.in_hole(from + dir * t) {
                            continue;
                        }
                        best = Some((t, n, (slot, id)));
                    }
                }
            }
        }
        best
    }

    pub fn near(&self, lo: Vec3, hi: Vec3) -> Near {
        let mut tris = vec![];
        let mut seen = std::collections::HashSet::new();
        for x in cell(lo.x)..=cell(hi.x) {
            for y in cell(lo.y)..=cell(hi.y) {
                for &(slot, id) in self.cells.get(&(x, y)).into_iter().flatten() {
                    let Some(chunk) = self.chunks[slot as usize].as_ref() else { continue };
                    let t = chunk.triangles[id as usize];
                    let (tlo, thi) = (t[0].min(t[1]).min(t[2]), t[0].max(t[1]).max(t[2]));
                    if tlo.cmple(hi).all() && thi.cmpge(lo).all() && seen.insert((slot, id)) {
                        tris.push(t);
                    }
                }
            }
        }
        Near { tris }
    }

    pub fn ground(&self, from: Vec3, len: f32) -> Option<f32> {
        self.hit(from, Vec3::NEG_Z, len, true).map(|(t, _, _)| from.z - t)
    }

    pub fn ground_material(&self, from: Vec3, len: f32) -> Option<(f32, u8)> {
        self.hit(from, Vec3::NEG_Z, len, true).map(|(t, _, (slot, id))| (from.z - t, self.chunks[slot as usize].as_ref().map_or(0, |c| c.materials[id as usize])))
    }

    pub fn ground_hit(&self, from: Vec3, len: f32) -> Option<(f32, u8, u64)> {
        let (t, _, (slot, id)) = self.hit(from, Vec3::NEG_Z, len, true)?;
        let key = self.keys.iter().find(|(_, s)| **s == slot).map(|(&k, _)| k)?;
        Some((from.z - t, self.chunks[slot as usize].as_ref().map_or(0, |c| c.materials[id as usize]), key))
    }

    #[cfg(feature = "portal")]
    pub fn in_hole(&self, p: Vec3) -> bool {
        self.holes.iter().any(|h| h.has(p))
    }

    pub fn chunks_near(&self, at: Vec3, r: f32) -> Vec<u64> {
        self.keys.iter().filter(|(_, s)| self.chunks[**s as usize].as_ref()
            .is_some_and(|c| c.bounds.0.cmple(c.bounds.1).all() && (at.clamp(c.bounds.0, c.bounds.1) - at).length_squared() <= r * r)).map(|(&k, _)| k).collect()
    }

    pub fn ground_stairs(&self, from: Vec3, len: f32) -> Option<(f32, bool)> {
        self.hit(from, Vec3::NEG_Z, len, true).map(|(t, _, (slot, id))| (from.z - t, self.chunks[slot as usize].as_ref().is_some_and(|c| c.flags[id as usize] & 1 != 0)))
    }
}

fn segment_triangle([a, b, c]: [Vec3; 3], n: Vec3, s: Vec3, e: Vec3) -> Result<f32, u8> {
    let (d, sa, ba, ca) = (e - s, s - a, b - a, c - a);
    let (ns, ne) = (n.dot(sa), n.dot(e - a));
    let w = d.cross(ca);
    let (u, v, area) = (w.dot(sa), d.dot(sa.cross(ba)), w.dot(ba));
    let den = ns - ne;
    let y = 1.0 / den;
    let t = ns * (y * (1.0 - y * den) + y);
    match () {
        _ if !(ns >= 0.0) || ne >= 0.0 => Err(2),
        _ if !(t >= 0.0) || t > 1.0 => Err(delirium_data::kv!(0 int)),
        _ if !(area >= delirium_data::kv!(71)) => Err(1),
        _ if !(u >= 0.0) || u > area => Err(delirium_data::kv!(1 int)),
        _ if !(v >= 0.0) || u + v > area => Err(delirium_data::kv!(2 int)),
        _ => Ok(t),
    }
}

fn ray_triangle([a, b, c]: [Vec3; 3], from: Vec3, dir: Vec3, len: f32, best: Option<f32>) -> Option<(f32, Vec3)> {
    let (e1, e2) = (b - a, c - a);
    let p = dir.cross(e2);
    let det = e1.dot(p);
    if det.abs() < delirium_data::kv!(72) {
        return None;
    }
    let s = (from - a) / det;
    let u = s.dot(p);
    let q = s.cross(e1);
    let v = dir.dot(q);
    let t = e2.dot(q);
    if u < 0.0 || v < 0.0 || u + v > 1.0 || t < 0.0 || t > len || best.is_some_and(|bt| t >= bt) {
        return None;
    }
    let n = e1.cross(e2).normalize_or_zero();
    Some((t, if n.dot(dir) > 0.0 { -n } else { n }))
}

pub struct Near {
    tris: Vec<[Vec3; 3]>,
}

impl Near {
    pub fn cast(&self, from: Vec3, dir: Vec3, len: f32) -> Option<(f32, Vec3)> {
        let mut best: Option<(f32, Vec3)> = None;
        for &t in &self.tris {
            if let Some(hit) = ray_triangle(t, from, dir, len, best.map(|b| b.0)) {
                best = Some(hit);
            }
        }
        best
    }
}

#[derive(Clone, Copy, Debug)]
pub struct Walker {
    pub feet: Vec3,
    pub fall: f32,
    pub grounded: bool,
}

impl Walker {
    pub fn step(&mut self, world: &Collision, wish: Vec2, jump: bool, dt: f32) {
        let mut motion = wish.extend(0.0) * dt;
        for _ in 0..2 {
            let len = motion.length();
            if len < delirium_data::kv!(47) {
                break;
            }
            let dir = motion / len;
            let hit = [STEP() + delirium_data::kv!(73), delirium_data::kv!(74)].into_iter().filter_map(|h| world.cast(self.feet + Vec3::Z * h, dir, len + RADIUS())).min_by(|a, b| a.0.total_cmp(&b.0));
            let Some((t, n)) = hit else { break };
            let n = Vec3::new(n.x, n.y, 0.0).normalize_or_zero();
            if n == Vec3::ZERO {
                break;
            }
            let free = (t - RADIUS() / dir.dot(n).abs().max(delirium_data::kv!(73))).clamp(0.0, len);
            let rest = motion - dir * free;
            motion = dir * free + (rest - n * rest.dot(n));
        }
        if self.grounded && let Some((_, n)) = world.cast(self.feet + Vec3::Z * 0.5, Vec3::NEG_Z, delirium_data::kv!(20)).filter(|(_, n)| n.z >= 0.5) {
            motion.z = -(n.x * motion.x + n.y * motion.y) / n.z;
        }
        self.feet += motion;

        if jump && self.grounded {
            self.fall = -delirium_data::kv!(60);
            self.grounded = false;
        }
        if self.grounded && let Some(z) = world.ground(self.feet + Vec3::Z * STEP(), STEP() + delirium_data::kv!(24)) {
            let up = -self.fall;
            let a = GRAVITY() * (delirium_data::kv!(32) * (z - self.feet.z) + 1.0).min(delirium_data::kv!(27)) - GRAVITY() - (2.0 * delirium_data::kv!(75 f32).sqrt() * up).clamp(-delirium_data::kv!(76), delirium_data::kv!(76));
            let up = up + a * dt;
            self.feet.z += up * dt;
            self.fall = -up;
            return;
        }
        self.fall += GRAVITY() * dt;
        let drop = self.fall * dt;
        match world.ground(self.feet + Vec3::Z * STEP(), STEP() + drop.max(0.0) + delirium_data::kv!(64)) {
            Some(z) if self.fall >= 0.0 && z >= self.feet.z - drop.max(0.0) - delirium_data::kv!(64) => {
                self.feet.z = z;
                self.fall = 0.0;
                self.grounded = true;
            }
            _ => {
                self.feet.z -= drop;
                self.grounded = false;
            }
        }
    }
}

pub fn street_spawn(world: &Collision, around: Vec2) -> Option<Vec3> {
    let tops: Vec<Vec3> = (-delirium_data::kv!(77 int)..=delirium_data::kv!(77 int))
        .flat_map(|i| (-delirium_data::kv!(77 int)..=delirium_data::kv!(77 int)).map(move |j| around + Vec2::new(i as f32, j as f32) * delirium_data::kv!(27)))
        .filter_map(|p| world.ground(p.extend(delirium_data::kv!(78)), delirium_data::kv!(79)).map(|z| p.extend(z)))
        .filter(|p| p.z > 1.0)
        .filter(|p| world.cast(*p + Vec3::Z * 0.5, Vec3::Z, delirium_data::kv!(80)).is_none())
        .filter(|p| (0..delirium_data::kv!(23 int)).all(|i| {
            let a = i as f32 * std::f32::consts::FRAC_PI_4;
            world.cast(*p + Vec3::Z * 1.0, Vec3::new(a.cos(), a.sin(), 0.0), 2.0).is_none()
        }))
        .collect();
    let mut bins: HashMap<i32, usize> = HashMap::new();
    tops.iter().for_each(|p| *bins.entry(p.z.round() as i32).or_default() += 1);
    let (&street, _) = bins.iter().max_by_key(|&(z, n)| (*n, -z))?;
    tops.into_iter()
        .filter(|p| p.z.round() as i32 == street)
        .min_by(|a, b| a.truncate().distance(around).total_cmp(&b.truncate().distance(around)))
}


#[cfg(test)]
thread_local!{static NEIGHBOUR_CACHE_BASELINE:std::cell::Cell<bool>=const{std::cell::Cell::new(false)};}

#[cfg(test)]
mod bounded_query_tests {
    use super::*;
    fn kept(t:&([Vec3;3],u8,[Vec3;4],u8,u32,Poly),lo:Vec3,hi:Vec3)->bool{
        Collision::tri_in_bounds(t.0,lo,hi)
    }
    #[test]
    fn early_bound_preserves_complete_tuples_order_sides_and_authored_bvh(){
        let assets=std::path::PathBuf::from(std::env::var("NM_TEST_ASSETS").expect("NM_TEST_ASSETS"));
        assert!(delirium_data::kv::unlock(&delirium_data::nm_asset::read(&std::fs::read(assets.join("fred.xrt")).unwrap())));
        let mut world=Collision::default();
        // Adjacent raw triangles exercise neighbour lookup, material/flags,
        // two-sided winding, duplicated grid cells, and independent chunks.
        for x in -4..=4{for y in -4..=4{
            let(x,y)=(x as f32,y as f32);
            world.add_two_sided(&[[x,y,0.],[x+1.,y,0.],[x+1.,y+1.,0.],[x,y+1.,0.]],&[[0,1,2],[0,2,3]],&[3,7]);
        }}
        let vertices=[[0.,0.,0.],[0.,1.,0.],[0.,1.,2.],[0.,0.,2.]];
        let triangles=[[0,1,2],[0,2,3]];
        let size=delirium_data::kv!(51 int) as usize;
        let mut node=vec![0u8;size];
        for k in 0..3{node[6+k*2..8+k*2].copy_from_slice(&u16::MAX.to_be_bytes());}
        let mut sub=node.clone();
        sub[delirium_data::kv!(53 int) as usize..delirium_data::kv!(53 int) as usize+2].copy_from_slice(&1u16.to_be_bytes());
        node[delirium_data::kv!(53 int) as usize]=2;
        let mesh=delirium_data::bounds::CollisionMesh{
            positions:vertices.to_vec(),triangles:triangles.to_vec(),normals:vec![[1.,0.,0.];2],
            neighbours:vec![[u32::MAX,u32::MAX,1],[0,u32::MAX,u32::MAX]],corners:vec![[0,1,3];2],
            halves:vec![1,2],order:vec![0,1],poly:vec![0,1],
            bvh:Some(delirium_data::bounds::Bvh{origin:[-10.;3],scale:[100.;3],subs:sub,nodes:node,..Default::default()}),..Default::default()
        };
        world.add_chunk_faces(1000,&vertices,&triangles,&[4,5],&[1,2],&Face::of(&mesh));
        assert!(world.chunks.iter().flatten().any(|c|!c.meshes.is_empty()));
        let mut checks=0;let mut authored=0;
        for side in [-1.,1.]{for x in [-4.,-0.1,0.,0.1,2.,4.]{for z in [-0.5,0.,0.6,2.]{for half in [0.1,0.7,2.]{
            let at=Vec3::new(x,0.5,z);let lo=at-Vec3::splat(3.);let hi=at+Vec3::splat(3.);
            let blo=at-Vec3::splat(half);let bhi=at+Vec3::splat(half);
            let start=at+Vec3::X*side;
            NEIGHBOUR_CACHE_BASELINE.with(|v|v.set(true));
            let old=world.tris_near_from(lo,hi,start).into_iter().filter(|t|kept(t,blo,bhi)).collect::<Vec<_>>();
            world.neighbours.write().unwrap().clear();
            NEIGHBOUR_CACHE_BASELINE.with(|v|v.set(false));
            let new=world.tris_near_from_bounded(lo,hi,start,blo,bhi);
            assert_eq!(old,new,"near at={at:?} half={half} side={side}");
            let a=crate::bvh::Frame{rows:[Vec3::X,Vec3::Y,Vec3::Z],t:start};
            let b=crate::bvh::Frame{t:at,..a};
            NEIGHBOUR_CACHE_BASELINE.with(|v|v.set(true));
            let old=world.tris_in_part(lo,hi,&b,&a,Vec3::splat(-3.),Vec3::splat(3.)).into_iter().filter(|t|kept(t,blo,bhi)).collect::<Vec<_>>();
            authored+=old.iter().filter(|t|world.slot_key(t.5.first.0)==Some(1000)).count();
            world.neighbours.write().unwrap().clear();
            NEIGHBOUR_CACHE_BASELINE.with(|v|v.set(false));
            let new=world.tris_in_part_bounded(lo,hi,&b,&a,Vec3::splat(-3.),Vec3::splat(3.),blo,bhi);
            assert_eq!(old,new,"part at={at:?} half={half} side={side}");
            // Deliberately winding-sensitive: verifies START orientation before
            // the generic SAT hook, not just min/max invariance of bounds.
            let keep=|t:[Vec3;3]|t[1].y<t[2].y||t[1].x>t[2].x;
            NEIGHBOUR_CACHE_BASELINE.with(|v|v.set(true));
            let old=world.tris_in_part(lo,hi,&b,&a,Vec3::splat(-3.),Vec3::splat(3.)).into_iter().filter(|t|keep(t.0)).collect::<Vec<_>>();
            NEIGHBOUR_CACHE_BASELINE.with(|v|v.set(false));
            let new=world.tris_in_part_filtered(lo,hi,&b,&a,Vec3::splat(-3.),Vec3::splat(3.),keep);
            assert_eq!(old,new,"oriented predicate at={at:?} side={side}");checks+=3;
        }}}}
        assert_eq!(checks,432);assert!(authored>0,"authored BVH path must actually select faces");
    }
    #[test]
    fn interleaved_chunk_appends_track_every_cell_for_removal_and_slot_reuse() {
        let assets = std::path::PathBuf::from(std::env::var("NM_TEST_ASSETS").unwrap());
        assert!(delirium_data::kv::unlock(&delirium_data::nm_asset::read(&std::fs::read(assets.join("fred.xrt")).unwrap())));
        let mut world = Collision::default();
        let near = [[0., 0., 0.], [0.5, 0., 0.], [0., 0.5, 0.]];
        let far = near.map(|[x, y, z]| [x + CELL() * 8., y, z]);
        world.add_chunk(1, &near, &[[0, 1, 2]], &[1]);
        world.add_chunk(2, &near, &[[0, 1, 2]], &[2]);
        world.add_chunk(1, &near, &[[0, 1, 2], [0, 1, 3]], &[3, 8]);
        world.add_chunk(1, &far, &[[0, 1, 2], [0, 1, 2]], &[4, 5]);
        let origin_cells = world.chunks[world.keys[&1] as usize].as_ref().unwrap();
        assert_eq!(origin_cells.cells.len(), 2);
        assert_eq!(world.len(), 5);
        world.remove_chunk(1);
        world.add_chunk(3, &far, &[[0, 1, 2]], &[6]);
        let materials = |lo, hi| world.tris_near(lo, hi).into_iter().map(|row| row.1).collect::<Vec<_>>();
        assert_eq!(materials(Vec3::splat(-1.), Vec3::splat(1.)), [2]);
        let far_x = Vec3::X * CELL() * 8.;
        assert_eq!(materials(far_x - Vec3::ONE, far_x + Vec3::ONE), [6]);
    }
    #[test]
    fn indexed_neighbours_invalidate_on_append_and_survive_parallel_queries(){
        let assets=std::path::PathBuf::from(std::env::var("NM_TEST_ASSETS").unwrap());
        assert!(delirium_data::kv::unlock(&delirium_data::nm_asset::read(&std::fs::read(assets.join("fred.xrt")).unwrap())));
        let mut world=Collision::default();let lo=Vec3::splat(-3.);let hi=Vec3::splat(3.);
        world.add(&[[-0.,0.,0.],[1.,0.,0.],[0.,1.,0.]],&[[0,1,2]],&[1]);
        assert!(world.tris_near(lo,hi)[0].5.edges.iter().all(Option::is_none));
        // These share a previously cached edge; the append must invalidate it.
        world.add(&[[0.,0.,0.],[1.,0.,0.],[0.,0.,1.]],&[[0,1,2],[2,1,0]],&[2,3]);
        NEIGHBOUR_CACHE_BASELINE.with(|v|v.set(true));let old=world.tris_near(lo,hi);
        NEIGHBOUR_CACHE_BASELINE.with(|v|v.set(false));assert_eq!(old,world.tris_near(lo,hi));
        assert!(old[0].5.edges.iter().any(Option::is_some));
        // Force cold independent cells before concurrently requesting neighbours.
        world.chunks.iter_mut().flatten().for_each(|c|c.neighbours.iter_mut().for_each(|v|{v.take();}));
        std::thread::scope(|s|{for _ in 0..8{let w=&world;let want=&old;s.spawn(move||{for _ in 0..30{assert_eq!(*want,w.tris_near(lo,hi));}});}});
        world.remove_chunk(FIXED);world.add(&[[0.,0.,1.],[1.,0.,1.],[0.,1.,1.]],&[[0,1,2]],&[4]);
        NEIGHBOUR_CACHE_BASELINE.with(|v|v.set(true));let old=world.tris_near(lo,hi);
        NEIGHBOUR_CACHE_BASELINE.with(|v|v.set(false));assert_eq!(old,world.tris_near(lo,hi));assert!(old[0].5.edges.iter().all(Option::is_none));
    }

}
