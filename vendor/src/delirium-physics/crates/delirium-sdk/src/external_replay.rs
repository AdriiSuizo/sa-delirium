//! Error-only lossless vehicle placement admission capture. This does not
//! claim to replay the full articulated island or the native vehicle solver.
use super::*;
use delirium::{external_mobility::*,ragdoll::{Group,ObPart,CollisionFilter},gjk::Shape,curved::{Curved,CurvedSnapshot}};
use serde::{Serialize,Deserialize};
use serde_json::{json,Value};

#[derive(Serialize,Deserialize)]
enum ShapeData {Box([f32;3],f32),Capsule(f32,f32),Tri([[f32;3];3],f32),Hull(Vec<[f32;3]>,f32),Curved(Vec<[f32;3]>,Vec<([f32;3],[f32;3],f32,usize,usize)>,f32),Tapered(f32,[f32;2],f32)}
impl ShapeData {
 fn capture(s:Shape)->Self{match s{
  Shape::Box(h,r)=>Self::Box(h.to_array(),r),Shape::Capsule(h,r)=>Self::Capsule(h,r),Shape::Tri(v,r)=>Self::Tri(v.map(|v|v.to_array()),r),
  Shape::Hull(v,r)=>Self::Hull(v.iter().map(|v|v.to_array()).collect(),r),Shape::Tapered(h,r,m)=>Self::Tapered(h,r,m),
  Shape::Curved(c,r)=>{let s=c.snapshot();Self::Curved(s.verts.iter().map(|v|v.to_array()).collect(),s.faces.iter().map(|&(c,a,r,f,t)|(c.to_array(),a.to_array(),r,f,t)).collect(),r)}
 }}
 fn restore(&self)->Result<Shape,String>{Ok(match self{
  Self::Box(h,r)=>Shape::Box((*h).into(),*r),Self::Capsule(h,r)=>Shape::Capsule(*h,*r),Self::Tri(v,r)=>Shape::Tri(v.map(Vec3::from),*r),
  Self::Hull(v,r)=>Shape::Hull(Box::leak(v.iter().copied().map(Vec3::from).collect::<Vec<_>>().into_boxed_slice()),*r),Self::Tapered(h,r,m)=>Shape::Tapered(*h,*r,*m),
  Self::Curved(v,f,r)=>Shape::Curved(Box::leak(Box::new(Curved::from_snapshot(CurvedSnapshot{verts:v.iter().copied().map(Vec3::from).collect(),faces:f.iter().map(|&(c,a,r,f,t)|(c.into(),a.into(),r,f,t)).collect()})?)),*r)
 })}
}
#[derive(Serialize,Deserialize)]
struct MeshData {verts:Vec<[f32;3]>,polys:Vec<([f32;3],u8,[u16;4],[u16;4])>,mats:Vec<u32>,radius:f32,centre:[f32;3]}
impl MeshData {
 fn capture(m:&delirium::sphere::Mesh)->Self{Self{verts:m.verts.iter().map(|v|v.to_array()).collect(),polys:m.polys.iter().map(|p|(p.n.to_array(),p.mat,p.v,p.nb)).collect(),mats:m.mats.clone(),radius:m.radius,centre:m.cen.to_array()}}
 fn restore(&self)->delirium::sphere::Mesh{delirium::sphere::Mesh{verts:self.verts.iter().copied().map(Vec3::from).collect(),polys:self.polys.iter().map(|&(n,mat,v,nb)|delirium::sphere::Poly{n:n.into(),mat,v,nb}).collect(),mats:self.mats.clone(),radius:self.radius,cen:self.centre.into()}}
}
#[derive(Serialize,Deserialize)]
struct PartData {shape:ShapeData,m:[f32;9],at:[f32;3],lo:[f32;3],hi:[f32;3],filter:[u32;2],mesh:Option<MeshData>}
#[derive(Serialize,Deserialize)]
struct ObstacleData {id:i32,owner:Option<u64>,filter:[u32;2],at:[f32;3],rot:[f32;4],centre:[f32;3],half:[f32;3],vel:[f32;3],spin:[f32;3],com:[f32;3],parts:Vec<PartData>,last:Option<([f32;3],[f32;4])>}
impl ObstacleData {
 fn capture(o:&Obstacle)->Self{Self{id:o.id,owner:o.owner,filter:[o.filter.kind,o.filter.include],at:o.at.to_array(),rot:o.rot.to_array(),centre:o.centre.to_array(),half:o.half.to_array(),vel:o.vel.to_array(),spin:o.spin.to_array(),com:o.com.to_array(),last:o.last.map(|(p,q)|(p.to_array(),q.to_array())),parts:o.parts.iter().map(|p|PartData{shape:ShapeData::capture(p.shape),m:p.m.to_cols_array(),at:p.at.to_array(),lo:p.lo.to_array(),hi:p.hi.to_array(),filter:[p.filter.kind,p.filter.include],mesh:p.mesh.map(MeshData::capture)}).collect()}}
 fn restore(&self)->Result<Obstacle,String>{
  let parts=self.parts.iter().map(|p|Ok(ObPart{shape:p.shape.restore()?,m:Mat3::from_cols_array(&p.m),at:p.at.into(),lo:p.lo.into(),hi:p.hi.into(),filter:CollisionFilter{kind:p.filter[0],include:p.filter[1]},mesh:p.mesh.as_ref().map(|m|&*Box::leak(Box::new(m.restore())))})).collect::<Result<Vec<_>,String>>()?;
  Ok(Obstacle{id:self.id,owner:self.owner,filter:CollisionFilter{kind:self.filter[0],include:self.filter[1]},at:self.at.into(),rot:Quat::from_array(self.rot),centre:self.centre.into(),half:self.half.into(),vel:self.vel.into(),spin:self.spin.into(),com:self.com.into(),parts:parts.into(),last:self.last.map(|(p,q)|(p.into(),Quat::from_array(q)))})
 }
}
#[derive(Serialize,Deserialize)]
struct OwnerData{id:u64,epoch:u64,com:[f32;3],velocity:[f32;3],spin:[f32;3],linear_inverse:[f32;9],angular_inverse:[f32;9]}
#[derive(Serialize,Deserialize)]
struct TriangleData{vertices:[[f32;3];3],material:u8,dirs:[[f32;3];4],flags:u8,rank:u32,first:(u32,u32),normal:[f32;3],edges:[Option<([f32;3],[f32;3],[f32;3])>;4]}
#[derive(Serialize,Deserialize)]
struct QueryData{obstacle:i32,child:usize,lo:[f32;3],hi:[f32;3],triangles:Vec<TriangleData>}
#[derive(Serialize,Deserialize)]
struct ReplayData{owner:OwnerData,pending_position:[f32;3],pending_rotation:[f32;4],drive_impulse:[f32;3],drive_torque:[f32;3],geometry:Vec<ObstacleData>,dt:f32,delta:[f32;3],turn:[f32;3],requested:[f32;3],contacts:u32,expected:Value,queries:Vec<QueryData>,#[serde(default)]world_rows:Vec<([f32;3],[f32;3],f32,f32)>,#[serde(default)]world_row_angular:Vec<Option<[f32;3]>>,#[serde(default)]environment_override:bool}
#[derive(Serialize)]
struct CaptureData<'a>{schema:&'a str,scope:&'a str,live_diagnostics:Vec<Value>,replays:Vec<ReplayData>}
#[derive(Deserialize)]
struct CaptureInput{schema:String,replays:Vec<ReplayData>}
pub(super) fn diagnostic(d:&ExternalPlacementDiagnostic)->Value{json!({"id":d.id,"reason_flags":d.reason_flags,"obstacle":d.obstacle,"child":d.child,"triangle_chunk":d.triangle_chunk,"triangle_index":d.triangle_index,"base_gap":d.base_gap,"end_gap":d.end_gap,"cast_t":d.cast_t,"cast_gap":d.cast_gap,"cast_iterations":d.cast_iterations,"cast_error":d.cast_error,"query_count":d.query_count,"triangle_count":d.triangle_count,"gjk_count":d.gjk_count,"cast_count":d.cast_count,"cast_failures":d.cast_failures,"solve_us":d.solve_us,"witness_float_bits":[d.base_gap.to_bits(),d.end_gap.to_bits(),d.cast_t.to_bits(),d.cast_gap.to_bits()]})}
fn observed(p:&ExternalPlacement,d:&ExternalPlacementDiagnostic)->Value{json!({"valid":p.valid,"position":p.admitted_position.to_array(),"rotation":p.admitted_rotation.to_array(),"diagnostic":diagnostic(d)})}
impl ReplayData{
 fn capture(r:&ExternalPlacementReplay)->Self{let o=r.owner;Self{
  owner:OwnerData{id:o.id,epoch:o.epoch,com:o.com.to_array(),velocity:o.velocity.to_array(),spin:o.spin.to_array(),linear_inverse:o.linear_inverse.to_cols_array(),angular_inverse:o.angular_inverse.to_cols_array()},
  pending_position:r.predictor.pending_position.to_array(),pending_rotation:r.predictor.pending_rotation.to_array(),drive_impulse:r.drive.impulse.to_array(),drive_torque:r.drive.torque.to_array(),geometry:r.geometry.iter().map(ObstacleData::capture).collect(),dt:r.dt,delta:r.delta.to_array(),turn:r.turn.to_array(),requested:r.requested.to_array(),contacts:r.contacts,expected:observed(&r.placement,&r.diagnostic),
  queries:r.queries.iter().map(|q|QueryData{obstacle:q.obstacle,child:q.child,lo:q.lo.to_array(),hi:q.hi.to_array(),triangles:q.triangles.iter().map(|&(v,material,dirs,flags,rank,p)|TriangleData{vertices:v.map(|v|v.to_array()),material,dirs:dirs.map(|v|v.to_array()),flags,rank,first:p.first,normal:p.n.to_array(),edges:p.edges.map(|e|e.map(|(a,b,n)|(a.to_array(),b.to_array(),n.to_array())))}).collect()}).collect(),
  world_rows:r.world_rows.iter().map(|r|(r.point.to_array(),r.normal.to_array(),r.gap,r.native_gap)).collect(),world_row_angular:r.world_rows.iter().map(|r|r.angular.map(|v|v.to_array())).collect(),environment_override:r.environment_override
 }}
 fn restore(&self)->Result<ExternalPlacementReplay,String>{let o=&self.owner;Ok(ExternalPlacementReplay{
  owner:ExternalOwner{id:o.id,epoch:o.epoch,com:o.com.into(),velocity:o.velocity.into(),spin:o.spin.into(),linear_inverse:Mat3::from_cols_array(&o.linear_inverse),angular_inverse:Mat3::from_cols_array(&o.angular_inverse)},
  predictor:ExternalPredictor{id:o.id,pending_position:self.pending_position.into(),pending_rotation:Quat::from_array(self.pending_rotation)},drive:ExternalDrive{id:o.id,impulse:self.drive_impulse.into(),torque:self.drive_torque.into()},geometry:self.geometry.iter().map(ObstacleData::restore).collect::<Result<_,_>>()?,dt:self.dt,delta:self.delta.into(),turn:self.turn.into(),requested:self.requested.into(),contacts:self.contacts,
  placement:ExternalPlacement{id:o.id,admitted_position:Vec3::ZERO,admitted_rotation:Quat::IDENTITY,valid:false,world_contacts:self.contacts,requested_position:self.requested.into()},diagnostic:ExternalPlacementDiagnostic::default(),
  queries:self.queries.iter().map(|q|ExternalReplayQuery{obstacle:q.obstacle,child:q.child,lo:q.lo.into(),hi:q.hi.into(),triangles:q.triangles.iter().map(|t|(t.vertices.map(Vec3::from),t.material,t.dirs.map(Vec3::from),t.flags,t.rank,delirium::collision::Poly{first:t.first,n:t.normal.into(),edges:t.edges.map(|e|e.map(|(a,b,n)|(a.into(),b.into(),n.into())))})).collect()}).collect(),
  world_rows:self.world_rows.iter().enumerate().map(|(i,&(p,n,gap,native_gap))|ExternalWorldRowReplay{point:p.into(),normal:n.into(),angular:self.world_row_angular.get(i).copied().flatten().map(Vec3::from),gap,native_gap}).collect(),environment_override:self.environment_override
 })}
}

/// Called only on an explicit preflight failure, before any native commit or
/// new interval replaces the scene. `path` is a UTF-8 output JSON filename.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_group_dump_external_replay(g:*const NmGroup,w:*const NmWorld,path:*const c_char)->i32{
 let(Some(g),Some(w))=(unsafe{g.as_ref()},unsafe{w.as_ref()})else{return fail(NM_E_ARG,"null group/world for external replay")};
 if path.is_null(){return fail(NM_E_ARG,"null external replay path")}
 let path=match unsafe{std::ffi::CStr::from_ptr(path)}.to_str(){Ok(s)=>s,Err(_)=>return fail(NM_E_ARG,"external replay path is not UTF-8")};
 // A failed admission retained its already-returned query data. Do not run
 // thousands of additional GJK/world queries inside the error handler.
 let fallback=if g.solver.external_failed_placement_replays().is_empty(){g.solver.external_placement_replays(&w.0)}else{vec![]};
 let snapshots=if fallback.is_empty(){g.solver.external_failed_placement_replays()}else{&fallback};
 let data=CaptureData{schema:"delirium-external-placement-v1",scope:"vehicle final placement admission only; original ordered query triangles; not full island resimulation",live_diagnostics:g.solver.external_placement_diagnostics().iter().map(diagnostic).collect(),replays:snapshots.iter().map(ReplayData::capture).collect()};
 let write=||->std::io::Result<()>{
  use std::io::Write;
  let path=std::path::Path::new(path);let partial=path.with_extension("json.partial");
  {let mut out=std::io::BufWriter::with_capacity(1024*1024,std::fs::File::create(&partial)?);serde_json::to_writer(&mut out,&data).map_err(std::io::Error::other)?;out.flush()?;}
  std::fs::rename(partial,path)
 };
 match write(){Ok(())=>snapshots.len() as i32,Err(e)=>fail(NM_E_ARG,format!("external replay write: {e}"))}
}

/// Offline process only: restored static shape storage intentionally lives
/// for the diagnostic process lifetime, just like production bound assets.
pub fn replay_file(path:&std::path::Path)->Result<Value,String>{
 let data:CaptureInput=serde_json::from_reader(std::io::BufReader::new(std::fs::File::open(path).map_err(|e|e.to_string())?)).map_err(|e|e.to_string())?;
 if data.schema!="delirium-external-placement-v1"{return Err("unsupported external replay schema".into())}
 let mut out=vec![];
 for stored in data.replays{
  let replay=stored.restore()?;let(p,d)=Group::replay_external_placement(&replay)?;let actual=observed(&p,&d);
  let fields=["reason_flags","obstacle","child","triangle_chunk","triangle_index","cast_error","cast_iterations","witness_float_bits","query_count","triangle_count","gjk_count","cast_count","cast_failures"];
  let f32_equal=|a:&Value,b:&Value|a.as_array().zip(b.as_array()).is_some_and(|(a,b)|a.len()==b.len()&&a.iter().zip(b).all(|(a,b)|a.as_f64().zip(b.as_f64()).is_some_and(|(a,b)|(a as f32).to_bits()==(b as f32).to_bits())));
  let matches=stored.expected["valid"]==actual["valid"]&&f32_equal(&stored.expected["position"],&actual["position"])&&f32_equal(&stored.expected["rotation"],&actual["rotation"])&&fields.iter().all(|k|stored.expected["diagnostic"][k]==actual["diagnostic"][k]);
  out.push(json!({"id":replay.owner.id,"epoch":replay.owner.epoch,"exact_match":matches,"actual":actual,"expected":stored.expected}));
 }
 Ok(json!({"schema":"delirium-external-placement-result-v1","replays":out}))
}

/// Independent separating-plane oracle for a captured exact pair. It checks
/// the penetration fallback, not the solver or a replacement vehicle bound.
pub fn audit_replay_planes(path:&std::path::Path)->Result<Value,String>{
 let data:CaptureInput=serde_json::from_reader(std::io::BufReader::new(std::fs::File::open(path).map_err(|e|e.to_string())?)).map_err(|e|e.to_string())?;
 let mut count=0;let mut witnesses=vec![];
 for stored in data.replays{let r=stored.restore()?;
  for query in &r.queries{
   let Some(ob)=r.geometry.iter().find(|o|o.id==query.obstacle)else{continue};let Some(part)=ob.parts.get(query.child)else{continue};
   let current=(ob.at+ob.rot*part.at,ob.rot*Quat::from_mat3(&part.m));let inv=r.predictor.pending_rotation.inverse();let base=r.owner.com-r.predictor.pending_position;
   let start=(base+inv*(current.0-r.owner.com),inv*current.1);let rotation=(Quat::from_scaled_axis(r.turn)*r.predictor.pending_rotation).normalize();
   let end=(base+r.predictor.pending_position+r.delta+rotation*(start.0-base),rotation*start.1);
   let margin=match part.shape{Shape::Box(_,m)|Shape::Capsule(_,m)|Shape::Tri(_,m)|Shape::Hull(_,m)|Shape::Curved(_,m)|Shape::Tapered(_,_,m)=>m};
   for &(t,_,dirs,_,_,poly) in &query.triangles{for (label,(at,q)) in [("start",start),("end",end)]{
    let centre=at+q*((part.lo+part.hi)*0.5);let side=if dirs[0].dot(centre-t[0])<0.{-1.}else{1.};let dirs=dirs.map(|d|d*side);let n=dirs[0];let m=Mat3::from_quat(q);
    let support=(at-t[0])+m*part.shape.support(m.transpose()*-n);let separation=n.dot(support)-margin-delirium_data::kv!(565);
    if separation<=1e-4{continue}
    let tri=Shape::Tri(t.map(|p|p-t[0]),delirium_data::kv!(565));
    let with=delirium::gjk::closest(&part.shape,m,at-t[0],&tri,Mat3::IDENTITY,Vec3::ZERO,n,f32::INFINITY,Some(delirium::gjk::Pen::Tri(dirs)));
    if let Some(hit)=with.filter(|c|c.dist<0.){count+=1;if witnesses.len()<24{
     let without=delirium::gjk::closest(&part.shape,m,at-t[0],&tri,Mat3::IDENTITY,Vec3::ZERO,n,f32::INFINITY,None);
     witnesses.push(json!({"owner":r.owner.id,"obstacle":ob.id,"child":query.child,"triangle":poly.first,"frame":label,"separation_lower_bound":separation,"reported_gap":hit.dist,"reported_normal":hit.n.to_array(),"without_penetration_gap":without.map(|c|c.dist)}));
    }}
   }}
  }
 }
 Ok(json!({"false_penetrations":count,"witnesses":witnesses}))
}

#[cfg(test)]
mod tests{
 use super::*;
 fn constants(){static ONCE:std::sync::Once=std::sync::Once::new();ONCE.call_once(||{
  let assets=std::path::PathBuf::from(std::env::var_os("NM_TEST_ASSETS").expect("NM_TEST_ASSETS for exact geometry replay"));
  assert!(delirium_data::kv::unlock(&delirium_data::nm_asset::read(&std::fs::read(assets.join("fred.xrt")).unwrap())));
 });}
 #[test]fn shape_codec_keeps_curved_faces_and_every_supported_bound(){
  constants();
  let curved=Box::leak(Box::new(delirium::curved::ped_part(0.15,1.27,0.1)));
  let hull=Box::leak(vec![Vec3::ZERO,Vec3::X,Vec3::Y,Vec3::Z].into_boxed_slice());
  for original in [Shape::Curved(curved,0.1),Shape::Hull(hull,0.02),Shape::Box(Vec3::new(0.3,0.4,0.5),0.01),Shape::Capsule(0.3,0.2),Shape::Tri([Vec3::ZERO,Vec3::X,Vec3::Y],0.01),Shape::Tapered(0.6,[0.2,0.1],0.01)]{
   let stored=ShapeData::capture(original);let bytes=serde_json::to_vec(&stored).unwrap();let restored:ShapeData=serde_json::from_slice(&bytes).unwrap();let shape=restored.restore().unwrap();
   for d in [Vec3::X,Vec3::Y,Vec3::Z,-Vec3::X,Vec3::ONE.normalize(),Vec3::new(-0.3,0.8,-0.2).normalize()]{assert_eq!(original.support(d),shape.support(d),"actual curved support must not become a vertex hull");}
  }
 }
 #[test]fn error_dump_roundtrips_original_owner_exact_scene_and_proposal(){
  constants();let mut world=NmWorld(Collision::default(),vec![],vec![]);
  world.0.add(&[[1.,-4.,-4.],[1.,-4.,4.],[1.,4.,4.],[1.,4.,-4.]],&[[0,1,2],[0,2,3]],&[7,8]);
  let owner=ExternalOwner{id:0x200000101,epoch:1234,com:Vec3::X*2.,velocity:Vec3::X*4.,spin:Vec3::ZERO,linear_inverse:Mat3::IDENTITY/300.,angular_inverse:Mat3::ZERO};
  let mut g=NmGroup::default();g.solver.stage_external(owner.epoch,&[owner]).unwrap();
  g.solver.stage_external_predictors(owner.epoch,&[ExternalPredictor{id:owner.id,pending_position:Vec3::X*2.,pending_rotation:Quat::IDENTITY}]).unwrap();
  g.solver.stage_external_geometry(&[Obstacle{id:17,owner:Some(owner.id),at:owner.com,centre:owner.com,com:owner.com,vel:owner.velocity,half:Vec3::splat(0.25),..Default::default()}]).unwrap();
  Ragdoll::step_group(&mut[],&world.0,1./60.,&mut g.solver);
  let path=std::env::temp_dir().join(format!("delirium-external-placement-{}-test.json",std::process::id()));
  let c=std::ffi::CString::new(path.to_str().unwrap()).unwrap();assert_eq!(unsafe{nm_group_dump_external_replay(&g,&world,c.as_ptr())},1);
  let data:Value=serde_json::from_reader(std::fs::File::open(&path).unwrap()).unwrap();
  assert_eq!(data["replays"][0]["owner"]["velocity"],json!([4.,0.,0.]));
  assert_eq!(data["replays"][0]["owner"]["epoch"],1234);assert_eq!(data["replays"][0]["queries"][0]["triangles"].as_array().unwrap().len(),2);
  let result=replay_file(&path).unwrap();assert_eq!(result["replays"][0]["exact_match"],true,"{result}");
  // Leave the generated exact fixture in the task-specific temp file for
  // the offline command test; no game/save files are touched.
 }
}
