//! Pure combat decision ABI. Deliberately separate from task/pose ownership.
use super::*;
pub struct NmCombat(sa_combat::Data,pub(crate) Option<sa_combat::runtime::Runtime>,Option<sa_combat::shove::Shove>);

thread_local!{static COMBAT_CALLBACKS:std::cell::RefCell<std::collections::HashSet<usize>>=Default::default();}
fn combat_busy(c:*const NmCombat)->bool{COMBAT_CALLBACKS.with(|b|b.borrow().contains(&(c as usize)))}
unsafe fn combat_mut<'a>(c:*mut NmCombat)->Option<&'a mut NmCombat>{if combat_busy(c){return None}unsafe{c.as_mut()}}
unsafe fn combat_ref<'a>(c:*const NmCombat)->Option<&'a NmCombat>{if combat_busy(c){return None}unsafe{c.as_ref()}}
struct CombatCallbackGuard(usize);
impl CombatCallbackGuard{fn enter(c:*mut NmCombat)->Option<Self>{COMBAT_CALLBACKS.with(|b|b.borrow_mut().insert(c as usize)).then_some(Self(c as usize))}}
impl Drop for CombatCallbackGuard{fn drop(&mut self){COMBAT_CALLBACKS.with(|b|{b.borrow_mut().remove(&self.0);});}}
#[path="combat_health.rs"] mod health_abi;
pub use health_abi::*;


/// Load the three original IV data files from an explicit directory. Unknown
/// actor profiles and action bindings are errors, not neutral fallback profiles.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_combat_load(directory: *const c_char) -> *mut NmCombat {
    let result=(||->Result<_,String>{
        let dir=unsafe{text(directory)}.ok_or("missing combat directory")?;
        let dir=std::path::Path::new(dir);
        let read=|name:&str|std::fs::read(dir.join(name)).map_err(|e|format!("{name}: {e}"));
        let mut data=sa_combat::Data::load(&read("MeleeAnims.dat")?,&read("action_table.csv")?,&read("pedpersonality.dat")?)?;
        if dir.join("sa-contact-ranges.json").exists(){data.adapt_reach(&read("sa-contact-ranges.json")?)?;}
        Ok(data)
    })();
    match result {Ok(data)=>Box::into_raw(Box::new(NmCombat(data,None,None))),Err(e)=>{fail(NM_E_PARSE,e);std::ptr::null_mut()}}
}

/// Original shove decision. Context: forwards 3+3, peer-minus-player xyz,
/// player right xyz, IV movement states 2, IV health, weaknesses 2 (17 floats).
/// Flags: friend=1, last peer=2, formation=4, occupied weapon hand=8.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_combat_shove_begin(c:*mut NmCombat,id:u32,context:*const f32,flags:u32)->i32 {
    let Some(c)=(unsafe{combat_mut(c)})else{return fail(NM_E_ARG,"null combat")};
    if context.is_null()||flags&!15!=0{return fail(NM_E_ARG,"invalid shove context")}
    let row=unsafe{std::slice::from_raw_parts(context,17)};
    if row.iter().any(|x|!x.is_finite()){return fail(NM_E_ARG,"invalid shove values")}
    let Some(clip)=sa_combat::shove::select(row,flags)else{return 0};
    if c.2.is_some(){return 0}
    let Some(rt)=c.1.as_ref()else{return fail(NM_E_ARG,"combat disabled")};
    let Some(a)=rt.actors.get(&id)else{return fail(NM_E_ARG,"missing shove rig")};
    c.2=sa_combat::shove::Shove::new(&rt.bank,a.rig.clone(),clip);
    if c.2.is_none(){return fail(NM_E_PARSE,"missing original shove clip")};clip as i32
}
/// Advance the original one-shot blend once per simulation frame. dt<0 cancels.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_combat_shove_step(c:*mut NmCombat,dt:f32)->i32 {
    let Some(c)=(unsafe{combat_mut(c)})else{return fail(NM_E_ARG,"null combat")};
    if !dt.is_finite()||dt>0.1{return fail(NM_E_ARG,"invalid shove step")}
    if dt<0.||c.2.as_mut().is_some_and(|s|!s.step(dt)){c.2=None;}
    c.2.as_ref().map_or(0,|s|s.clip as i32)
}
/// Blend the original clip over canonical current SA bone transforms (xyz,xyzw).
/// No mover, pelvis, legs or untracked arm are replaced.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_combat_shove_pose(c:*const NmCombat,input:*const f32,count:i32,output:*mut f32)->i32 {
    let Some(c)=(unsafe{combat_ref(c)})else{return fail(NM_E_ARG,"null combat")};
    let (Some(s),Some(rt))=(&c.2,&c.1)else{return 0};
    if input.is_null()||output.is_null()||count as usize!=s.rig.ids.len(){return fail(NM_E_ARG,"invalid shove palette")}
    let values=unsafe{std::slice::from_raw_parts(input,count as usize*7)};
    if values.iter().any(|x|!x.is_finite()){return fail(NM_E_ARG,"invalid shove pose")}
    let under:Vec<_>=values.chunks_exact(7).map(sa_combat::animation::Pose::read).collect();
    let Some(pose)=s.pose(&rt.bank,&under)else{return fail(NM_E_PARSE,"shove pose unavailable")};
    let out=unsafe{std::slice::from_raw_parts_mut(output,values.len())};
    for(t,r)in pose.iter().zip(out.chunks_exact_mut(7)){t.write(r)};s.clip as i32
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_combat_destroy(c: *mut NmCombat) {
    if !c.is_null()&&!combat_busy(c){drop(unsafe{Box::from_raw(c)})}
}
/// Evaluate an explicit v1 hit snapshot. Returns required UTF-8 buffer size,
/// including NUL. Same draw gives the same decision for both size/output calls.
/// Does not play a clip, start a behaviour, or apply damage/impulse.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_combat_evaluate_hit(c: *const NmCombat, json: *const c_char, buf: *mut c_char, len: i32) -> i32 {
    let Some(c)=(unsafe{combat_ref(c)}) else{return fail(NM_E_ARG,"null combat data")};
    let Some(json)=(unsafe{text(json)}) else{return fail(NM_E_ARG,"missing combat snapshot")};
    match sa_combat::query::evaluate(&c.0,json){Ok(out)=>unsafe{put(&out,buf,len)},Err(e)=>fail(NM_E_ARG,e)}
}

/// Load owned IV clips and their source skeleton after nm_init. Atomic: a failed
/// bank load leaves the prior runtime intact. Pure hit queries need no bank.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_combat_enable(c: *mut NmCombat, directory: *const c_char) -> i32 {
    let Some(c)=(unsafe{combat_mut(c)}) else{return fail(NM_E_ARG,"null combat")};
    let Some(dir)=(unsafe{text(directory)}) else{return fail(NM_E_ARG,"null clip directory")};
    match sa_combat::animation::Bank::load(std::path::Path::new(dir)){
        Ok(bank)=>{let mut data=c.0.clone();if let Err(e)=data.install_gym(&bank){return fail(NM_E_PARSE,e)}c.0=data;c.1=Some(sa_combat::runtime::Runtime::new(bank));0},Err(e)=>fail(NM_E_PARSE,e)
    }
}
/// Register a generation-qualified actor. Bind rows are canonical SA xyz,xyzw;
/// parents are indices, UINT32_MAX for roots. Explicit IV profile for NPCs.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_combat_register(c: *mut NmCombat, id: u32, player: i32, profile: *const c_char, ids: *const u32, parents: *const u32, bind: *const f32, count: i32) -> i32 {
    let Some(c)=(unsafe{combat_mut(c)}) else{return fail(NM_E_ARG,"null combat")};
    let Some(rt)=c.1.as_mut() else{return fail(NM_E_ARG,"combat clips not enabled")};
    if !(20..=128).contains(&count)||ids.is_null()||parents.is_null()||bind.is_null(){return fail(NM_E_ARG,"invalid combat rig buffers")}
    let Some(profile)=(unsafe{text(profile)}) else{return fail(NM_E_ARG,"missing combat profile")};
    let n=count as usize;
    let result=sa_combat::animation::Rig::new(unsafe{std::slice::from_raw_parts(ids,n)},unsafe{std::slice::from_raw_parts(parents,n)},unsafe{std::slice::from_raw_parts(bind,n*7)})
        .and_then(|rig|rt.register(&c.0,id,player!=0,profile,rig));
    match result{Ok(())=>0,Err(e)=>fail(NM_E_ARG,e)}
}
/// Attach the already fitted NM build to the combat rig, including virtual spine
/// joints and the head offset. Never extend a wrist along its forearm as a fist.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_combat_bind_geometry(c:*mut NmCombat,id:u32,b:*const NmBuild,nodes:*const i32,spine:*const f32,head:*const f32)->i32 {
    let Some(a)=(unsafe{combat_mut(c)}).and_then(|c|c.1.as_mut()).and_then(|r|r.actors.get_mut(&id)) else{return fail(NM_E_ARG,"unknown fighter")};
    let Some(b)=(unsafe{b.as_ref()}) else{return fail(NM_E_ARG,"missing fitted build")};
    if nodes.is_null()||spine.is_null()||head.is_null()||b.build.parts.len()!=21||b.frames.len()!=21{return fail(NM_E_ARG,"invalid fitted geometry")}
    let nodes=unsafe{std::slice::from_raw_parts(nodes,21)};let spine=unsafe{std::slice::from_raw_parts(spine,4)};let head=unsafe{std::slice::from_raw_parts(head,7)};
    if head.iter().any(|x|!x.is_finite()){return fail(NM_E_ARG,"invalid head anchor")}
    match fitted_combat_parts(&a.rig.bind,b,nodes,spine).and_then(|parts|a.rig.set_parts(parts)){Ok(())=>0,Err(e)=>fail(NM_E_ARG,e)}
}

fn fitted_combat_parts(bind:&[sa_combat::animation::Pose],b:&NmBuild,nodes:&[i32],spine:&[f32])->Result<Vec<sa_combat::geometry::Part>,String>{
    if nodes.len()!=21||spine.len()!=4||b.build.parts.len()!=21||b.frames.len()!=21||
       spine.iter().any(|x|!x.is_finite()||!(0.0..1.0).contains(x))||spine.windows(2).any(|w|w[1]-w[0]<1e-6)||
       nodes.iter().enumerate().any(|(i,&n)|i!=8&&i!=10&&(n<0||n as usize>=bind.len())) {return Err("invalid geometry anchors".into())}
    use sa_combat::animation::Pose;
    // Must match Rig::jointFrames/nativeTorsoJoints: SA spine is coincident
    // with the pelvis attachment, chest is a real bend, 8/10 are subdivisions.
    // Their position interpolates within one rigid segment, not pelvis->neck;
    // their orientation belongs to that segment, never a quaternion average.
    let lower=(spine[1]-spine[0])/(spine[2]-spine[0]);
    let upper=(spine[3]-spine[2])/(1.-spine[2]);
    const TAGS:[u16;21]=[0x1a1,0x1a2,0x1a3,0x1a4,0x1a7,0x1a8,0x1a9,0x4b2,0x4b3,0x36a0,0x36a1,0x4b4,0x4b5,0x4c0,0x4c1,0x4c2,0x4c3,0x4c7,0x4c8,0x4c9,0x4d0];
    let mut parts=vec![];
    for i in 0..21 {
        let (anchors,weight)=match i{
            8=>([nodes[7] as usize,nodes[9] as usize],lower),
            10=>([nodes[9] as usize,nodes[11] as usize],upper),
            _=>([nodes[i] as usize;2],0.)
        };
        let frame=&b.frames[i];let body=Pose{p:frame.translation,q:frame.rotation};
        // Body frames are already fitted using headOffset. Express their final
        // placement against the actual rendered head bone, not the virtual pivot.
        let p=&b.build.parts[i];let mut part=sa_combat::geometry::Part{tag:TAGS[i],nodes:anchors,weight,rotation_node:Some(anchors[0]),offset:[0.,0.,0.,0.,0.,0.,1.],radius:p.radius,half:p.half,boxed:p.boxed.map(|e|e.to_array())};
        part.joint(bind).inverse().mul(body).write(&mut part.offset);parts.push(part);
    }
    Ok(parts)
}

#[cfg(test)] mod native_torso_geometry_tests{
    use super::*;
    use sa_combat::animation::Pose;
    fn poses(path:impl AsRef<std::path::Path>)->Vec<Pose>{
        let values:Vec<f32>=std::fs::read_to_string(path).unwrap().split_whitespace().map(|x|x.parse().unwrap()).collect();
        values.chunks_exact(7).map(Pose::read).collect()
    }
    #[test]fn combat_bounds_follow_fitted_native_torso_in_compound_flexion(){
        let assets=std::path::PathBuf::from(std::env::var("NM_TEST_ASSETS").expect("NM_TEST_ASSETS"));
        assert!(delirium_data::kv::unlock(&delirium_data::nm_asset::read(&std::fs::read(assets.join("fred.xrt")).unwrap())));
        // Owned captures are local test inputs, never vendored game assets.
        let capture=std::env::var_os("NM_TEST_NATIVE_TOPOLOGY").map(std::path::PathBuf::from)
            .unwrap_or_else(||std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../../../../research/door-grip-20261101/native-topology-20261101-0908"));
        let bind=poses(capture.join("model-7-native-bind-joints.txt"));
        let captured=poses(capture.join("model-7-native-pose-joints.txt"));
        let report:serde_json::Value=serde_json::from_slice(&std::fs::read(capture.join("comparison.json")).unwrap()).unwrap();
        let fractions:Vec<f32>=report["fractions"].as_array().unwrap().iter().map(|v|v.as_f64().unwrap() as f32).collect();
        let (source,frames)=Build::undump(&std::fs::read_to_string(assets.join("fred-build.txt")).unwrap());
        let points:Vec<_>=bind.iter().map(|p|p.p).collect();let(build,frames)=source.fit(&frames,&points);let fitted=NmBuild{build,frames};
        let mut nodes:Vec<i32>=(0..21).collect();nodes[8]=-1;nodes[10]=-1;
        let parts=fitted_combat_parts(&bind,&fitted,&nodes,&fractions).unwrap();
        let check=|pose:&[Pose],position_tolerance:f32|{
            for i in 7..=10{
                // The independent physical seed: real joint * fitted rest offset.
                let body=Pose{p:fitted.frames[i].translation,q:fitted.frames[i].rotation};
                let expected=pose[i].mul(bind[i].inverse().mul(body));let actual=parts[i].frame(pose);
                assert!(actual.p.distance(expected.p)<position_tolerance,"body {i} drift {}",actual.p.distance(expected.p));
                assert!(actual.q.dot(expected.q).abs()>0.99999,"body {i} rotation disagrees with physical seed");
                let (a,b,r,_)=parts[i].capsule(pose);let model=&fitted.build.parts[i];
                let (ea,eb,er)=sa_combat::controller::frag_capsule(|v|expected.p+expected.q*v,model.radius,model.half,model.boxed);
                assert!(a.distance(ea)<position_tolerance&&b.distance(eb)<position_tolerance&&(r-er).abs()<1e-7);
            }
        };
        check(&bind,2e-5);
        // Captured float32 world positions are at x=2430m (one ULP=0.244mm).
        // The independently exported virtual pivots already carry that rounding.
        check(&captured,8e-4);
        let lower=(fractions[1]-fractions[0])/(fractions[2]-fractions[0]);let upper=(fractions[3]-fractions[2])/(1.-fractions[2]);
        let mut cases=0;
        for yaw in [-2.2,0.,1.4]{for x in [-0.8,-0.3,0.,0.4,0.9]{for y in [-0.7,0.,0.6]{for z in [-0.4,0.,0.5]{
            let low=Quat::from_rotation_z(yaw)*Quat::from_rotation_x(x);let high=low*Quat::from_rotation_y(y)*Quat::from_rotation_z(z);
            let mut pose=bind.clone();pose[7]=Pose{p:Vec3::new(14.,-7.,2.)+Quat::from_rotation_z(yaw)*bind[7].p,q:low*bind[7].q};
            pose[9]=Pose{p:pose[7].p+low*(bind[9].p-bind[7].p),q:high*bind[9].q};
            pose[11]=Pose{p:pose[9].p+high*(bind[11].p-bind[9].p),q:high*bind[11].q};
            pose[8]=Pose{p:pose[7].p.lerp(pose[9].p,lower),q:pose[7].q};
            pose[10]=Pose{p:pose[9].p.lerp(pose[11].p,upper),q:pose[9].q};
            check(&pose,2e-5);cases+=1;
        }}}}
        assert_eq!(cases,135);
        let mut invalid=nodes.clone();invalid[7]=-1;assert!(fitted_combat_parts(&bind,&fitted,&invalid,&fractions).is_err());
        let mut invalid=fractions.clone();invalid[2]=invalid[0];assert!(fitted_combat_parts(&bind,&fitted,&nodes,&invalid).is_err());
        println!("native torso combat: captured seed plus {cases} compound poses; pivots, orientations and capsules agree with fitted physical seed");
    }
}
/// Export the same fitted bounds for deterministic offline reproductions.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_combat_geometry(c:*const NmCombat,id:u32,buf:*mut c_char,len:i32)->i32 {
    let Some(a)=(unsafe{combat_ref(c)}).and_then(|c|c.1.as_ref()).and_then(|r|r.actors.get(&id)) else{return fail(NM_E_ARG,"unknown fighter")};
    match serde_json::to_string(&a.rig.parts){Ok(json)=>unsafe{put(&json,buf,len)},Err(e)=>fail(NM_E_ARG,e.to_string())}
}
/// Signed clearance of the accepted swept sample to the fitted body surface.
/// Non-positive means contact. Absent for authored damage or a paired start.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_combat_contact_gap(c:*const NmCombat,source:u32,victim:u32,out:*mut f32)->i32 {
    let Some(gap)=(unsafe{combat_ref(c)}).and_then(|c|c.1.as_ref()).and_then(|r|r.contact_gaps.get(&(source,victim))) else{return 0};
    if out.is_null(){return fail(NM_E_ARG,"null contact clearance")};unsafe{*out=*gap};1
}
/// Feed one actor, before the shared step. Frame is 20 floats: root xyz,yaw,
/// health in IV units, flags(player=1,fight=2,lock=4,physical=8,dead=16,
/// motor_reaction=32,fallen=64,native_exit_intent=128), target
/// pool ref or -1, IV button bits, stick xy [0,255], triggers [0,255], velocity
/// xyz m/s,gang membership flag,native attack/defend scalars,stats-valid flag,
/// native learned fighting style (player only). Pose contains count world-space xyz,xyzw rows.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_combat_feed_club(c:*mut NmCombat,id:u32,bounds:*const f32)->i32{
    let Some(c)=(unsafe{combat_mut(c)})else{return fail(NM_E_ARG,"null combat")};
    let Some(r)=c.1.as_mut()else{return fail(NM_E_ARG,"combat runtime is not enabled")};
    let bounds=(!bounds.is_null()).then(||unsafe{std::slice::from_raw_parts(bounds,8)});
    match r.feed_club(id,bounds){Ok(())=>0,Err(e)=>fail(NM_E_ARG,e)}
}
/// As nm_combat_feed_club, with blade != 0 when the held weapon is a blade (IV KNIFE, the action table's `knife` class).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_combat_feed_weapon(c:*mut NmCombat,id:u32,bounds:*const f32,blade:i32)->i32{
    let Some(c)=(unsafe{combat_mut(c)})else{return fail(NM_E_ARG,"null combat")};
    let Some(r)=c.1.as_mut()else{return fail(NM_E_ARG,"combat runtime is not enabled")};
    let bounds=(!bounds.is_null()).then(||unsafe{std::slice::from_raw_parts(bounds,8)});
    match r.feed_weapon(id,bounds,blade!=0){Ok(())=>0,Err(e)=>fail(NM_E_ARG,e)}
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_combat_feed(c: *mut NmCombat, id: u32, now: u32, frame: *const f32, pose: *const f32, count: i32) -> i32 {
    let Some(c)=(unsafe{combat_mut(c)}) else{return fail(NM_E_ARG,"null combat")};
    let Some(rt)=c.1.as_mut() else{return fail(NM_E_ARG,"combat clips not enabled")};
    if frame.is_null()||pose.is_null()||!(20..=128).contains(&count){return fail(NM_E_ARG,"invalid combat feed buffers")}
    let flags=unsafe{*frame.add(5)};let style=unsafe{*frame.add(19)};
    if flags.is_finite()&&(flags as u32)&1!=0&&style.is_finite(){c.0.gym_style(style as u32);}
    match rt.feed(id,unsafe{std::slice::from_raw_parts(frame,20)},unsafe{std::slice::from_raw_parts(pose,count as usize*7)},now){Ok(())=>0,Err(e)=>fail(NM_E_ARG,e)}
}
/// Step all fed actors once, then sweep all authored strike windows against the
/// same frame's bodies. Does not apply SA damage or move native entities.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_combat_step(c: *mut NmCombat, now: u32, dt: f32) -> i32 {
    let Some(c)=(unsafe{combat_mut(c)}) else{return fail(NM_E_ARG,"null combat")};
    let Some(rt)=c.1.as_mut() else{return fail(NM_E_ARG,"combat clips not enabled")};
    match rt.step(&c.0,now,dt){Ok(())=>rt.hits.len() as i32,Err(e)=>fail(NM_E_ARG,e)}
}
/// Supply the native active camera's world XY forward. Raw pad history remains
/// unchanged; the locked stance projects its wish onto the actor's current rows.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_combat_view(c:*mut NmCombat,x:f32,y:f32)->i32{
    let Some(c)=(unsafe{combat_mut(c)})else{return fail(NM_E_ARG,"null combat")};
    let Some(rt)=c.1.as_mut()else{return fail(NM_E_ARG,"combat clips not enabled")};
    match rt.set_view(x,y){Ok(())=>0,Err(e)=>fail(NM_E_ARG,e)}
}
/// Begin a two-phase host frame: advance clips/mover, without querying strikes.
/// After native movement, place every actor and call nm_combat_finish once.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_combat_begin(c:*mut NmCombat,now:u32,dt:f32)->i32{
    let Some(c)=(unsafe{combat_mut(c)})else{return fail(NM_E_ARG,"null combat")};
    let Some(rt)=c.1.as_mut()else{return fail(NM_E_ARG,"combat clips not enabled")};
    match rt.advance(&c.0,now,dt){Ok(())=>0,Err(e)=>fail(NM_E_ARG,e)}
}
/// Move the prepared world palette by the native collision-resolved root delta.
/// xyz contains three floats; this does not move a native entity or advance time.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_combat_place(c:*mut NmCombat,id:u32,xyz:*const f32)->i32{
    let Some(rt)=(unsafe{combat_mut(c)}).and_then(|c|c.1.as_mut())else{return fail(NM_E_ARG,"combat not enabled")};
    if xyz.is_null(){return fail(NM_E_ARG,"null combat root")}
    let root=Vec3::from_slice(unsafe{std::slice::from_raw_parts(xyz,3)});
    match rt.place(id,root){Ok(())=>0,Err(e)=>fail(NM_E_ARG,e)}
}
/// Query contacts after the prepared poses and native roots agree. Returns hit
/// count, or error if this frame was already finished. No second clip advance.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_combat_finish(c:*mut NmCombat)->i32{
    let Some(c)=(unsafe{combat_mut(c)})else{return fail(NM_E_ARG,"null combat")};
    let Some(rt)=c.1.as_mut()else{return fail(NM_E_ARG,"combat clips not enabled")};
    match rt.finish(&c.0){Ok(())=>rt.hits.len() as i32,Err(e)=>fail(NM_E_ARG,e)}
}
/// Copy the actor's world-space pose and 16-float state: active,yaw,local mover
/// vx/vy,result index,clip seconds,rate,strike bits,clip flags,blend,waiting,
/// reaction source ref or -1,request accepted,request source or -1,
/// given-reaction flag,reserved zero. Returns ownership:
/// 0=none,1=animated attacker,2=physical motor targets only; negative on error.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_combat_pose(c: *const NmCombat, id: u32, pose: *mut f32, count: i32, state: *mut f32) -> i32 {
    let Some(a)=(unsafe{combat_ref(c)}).and_then(|c|c.1.as_ref()).and_then(|r|r.actors.get(&id)) else{return fail(NM_E_ARG,"unknown combat actor")};
    if pose.is_null()||state.is_null()||count as usize!=a.pose.len(){return fail(NM_E_ARG,"invalid pose output")}
    let out=unsafe{std::slice::from_raw_parts_mut(pose,a.pose.len()*7)};
    for (p,row) in a.pose.iter().zip(out.chunks_exact_mut(7)){p.write(row)}
    unsafe{std::ptr::copy_nonoverlapping(a.state.as_ptr(),state,16)};a.state[0] as i32
}
/// Current authored clip key for trace/verification.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_combat_clip(c: *const NmCombat, id: u32, buf: *mut c_char, len: i32) -> i32 {
    let Some(a)=(unsafe{combat_ref(c)}).and_then(|c|c.1.as_ref()).and_then(|r|r.actors.get(&id)) else{return fail(NM_E_ARG,"unknown combat actor")};
    unsafe{put(&a.key,buf,len)}
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_combat_gym_diagnostics(c:*const NmCombat,id:u32,buf:*mut c_char,len:i32)->i32{
    let Some(c)=(unsafe{combat_ref(c)})else{return 0};
    let Some(json)=c.1.as_ref().and_then(|r|r.gym_diagnostics(&c.0,id))else{return 0};
    unsafe{put(&json,buf,len)}
}
/// Live pending/given reaction state, including requests queued AFTER pose
/// output. Pure query; zero for an absent actor. No duration or damage changes.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_combat_receiving(c: *const NmCombat, id: u32) -> i32 {
    (unsafe{combat_ref(c)}).and_then(|c|c.1.as_ref()).and_then(|r|r.actors.get(&id))
        .is_some_and(|a|a.receiving()) as i32
}

/// Feed original non-cop outer-combat facts. Eight floats: flags, target ref
/// (-1 absent), target health in IV units, target weapon state 0/1/2, distance,
/// line state 0..3, nearest fighting gang members 0..16, reserved zero.
/// Flags are defined in sa_combat::npc_decision; separate from the pose ABI.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_combat_decision_feed(c:*mut NmCombat,id:u32,now:u32,facts:*const f32)->i32{
    let Some(rt)=(unsafe{combat_mut(c)}).and_then(|c|c.1.as_mut())else{return fail(NM_E_ARG,"combat disabled")};
    if facts.is_null(){return fail(NM_E_ARG,"missing decision facts")}
    match rt.feed_decision(id,now,unsafe{std::slice::from_raw_parts(facts,sa_combat::npc_decision::FACT_FLOATS)}){Ok(())=>0,Err(e)=>fail(NM_E_ARG,e)}
}
/// Original outer decision: 1905 Charge, 1904 Retreat, 1901 Fire, 0 native/unmapped.
/// No mutation; a physical response may still hold ownership while it is active.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_combat_decision(c:*const NmCombat,id:u32)->i32{
    (unsafe{combat_ref(c)}).and_then(|c|c.1.as_ref()).and_then(|r|r.actors.get(&id)).map_or(0,|a|a.decision.code)
}
/// A hit is 16 floats: attacker ref,victim ref,IV bone,damage IV,point xyz,
/// physical flag,reaction index,direction xyz,actual striking IV bone tag,
/// forced,blocked,kind (0=sweep,1=authored counter damage,2=paired motor start).
/// Kind 2 has no damage/impulse; it leases a reaction from its first frame. Each sweep occurs
/// once per authored swing/victim; counter damage occurs once at its clip event.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_combat_contact(c: *const NmCombat, index: i32, out: *mut f32) -> i32 {
    let Some(rt)=(unsafe{combat_ref(c)}).and_then(|c|c.1.as_ref()) else{return fail(NM_E_ARG,"combat not enabled")};
    if out.is_null()||index<0{return fail(NM_E_ARG,"invalid contact output")}
    let Some(h)=rt.hits.get(index as usize)else{return 0};unsafe{std::ptr::copy_nonoverlapping(h.as_ptr(),out,16)};1
}
/// Remove one actor by pool generation ref; id=UINT32_MAX clears the scene.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_combat_remove(c: *mut NmCombat, id: u32) -> i32 {
    let Some(rt)=(unsafe{combat_mut(c)}).and_then(|c|c.1.as_mut()) else{return fail(NM_E_ARG,"combat not enabled")};
    if id==u32::MAX{rt.actors.clear();rt.hits.clear()}else{rt.actors.remove(&id);}0
}

/// Update the already prepared frame with this frame's native palette. No
/// decisions/animation advance; passive actors' contact geometry becomes current.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_combat_refresh_native_pose(c:*mut NmCombat,id:u32,pose:*const f32,count:i32)->i32{
    let Some(rt)=(unsafe{combat_mut(c)}).and_then(|c|c.1.as_mut())else{return fail(NM_E_ARG,"combat not enabled")};
    if pose.is_null()||!(20..=128).contains(&count){return fail(NM_E_ARG,"invalid native combat palette")}
    match rt.refresh_native_pose(id,unsafe{std::slice::from_raw_parts(pose,count as usize*7)}){Ok(())=>0,Err(e)=>fail(NM_E_ARG,e)}
}

/// An accepted harmful hit on a mapped ambient on-foot civilian. Flags:
/// 1 source player,2 source hated,4 source friend. Caller preserves prior panic.
/// Returns original event9 response;1900 owns its new Combat,0 already owned.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_combat_civilian_harm(c:*mut NmCombat,id:u32,source:u32,health:f32,flags:u32)->i32{
    let Some(rt)=(unsafe{combat_mut(c)}).and_then(|c|c.1.as_mut())else{return fail(NM_E_ARG,"combat disabled")};
    match rt.civilian_harm(id,source,health,flags){Ok(code)=>code,Err(e)=>fail(NM_E_ARG,e)}
}

/// 0=no authored mover,1=right,2=forward,4=yaw; only relevant to standalone owner3.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_combat_mover_axes(c:*const NmCombat,id:u32)->u32{
    (unsafe{combat_ref(c)}).and_then(|c|c.1.as_ref()).map_or(0,|rt|rt.mover_axes(id))
}
/// Same-frame ordinary on-foot root and world palette under a standalone result.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_combat_compose_standalone(c:*mut NmCombat,id:u32,root:*const f32,pose:*const f32,count:i32)->i32{
    let Some(rt)=(unsafe{combat_mut(c)}).and_then(|c|c.1.as_mut())else{return fail(NM_E_ARG,"combat disabled")};
    if root.is_null()||pose.is_null()||!(20..=128).contains(&count){return fail(NM_E_ARG,"invalid standalone palette")}
    match rt.compose_standalone(id,unsafe{std::slice::from_raw_parts(root,7)},unsafe{std::slice::from_raw_parts(pose,count as usize*7)}){Ok(())=>0,Err(e)=>fail(NM_E_ARG,e)}
}
