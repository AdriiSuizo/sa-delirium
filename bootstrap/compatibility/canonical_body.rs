// SPDX-License-Identifier: GPL-3.0-only
// Reuses adapted Playroom idle preparation; resources come from the owned ISO.
use delirium::prelude::*;
use delirium::ragdoll::Rig;
use delirium_data::{archive_key::ArchiveKey,disc::Disc,img,model,resource,skeleton,anim,rpf};
pub fn build(disc:&Disc,key:&ArchiveKey,rig:Rig)->Result<String,Box<dyn std::error::Error>>{
    let header=disc.read_at("xbox360.rpf",0,20)?;
        let size=u32::from_le_bytes(header[4..8].try_into()?) as usize;
        let toc=rpf::read_toc(&disc.read_at("xbox360.rpf",0,0x800+size)?,&key)?;
        let e=toc.iter().find(|e|e.path.eq_ignore_ascii_case("models/cdimages/playerped.rpf")).ok_or("player archive unavailable")?;
        let archive=disc.read_at("xbox360.rpf",e.offset,e.stored as usize)?;
        let toc=rpf::read_toc(&archive,&key)?;
        let e=toc.iter().find(|e|e.path.eq_ignore_ascii_case("player.xft")).ok_or("player unavailable")?;
        let player=resource::parse(&rpf::read(&archive,e)?)?;
        let bones=skeleton::read(&player,model::fragment_root(&player)?)?;
        let header=disc.read_at("xbox360/anim/anim.img",0,20)?;
        let size=img::table_len(&header,Some(&key)).ok_or("animation index unavailable")?;
        let entries=img::read_table(&disc.read_at("xbox360/anim/anim.img",0,size)?,Some(&key))?;
        let e=entries.iter().find(|e|e.name.eq_ignore_ascii_case("move_player.xad")).ok_or("movement dictionary unavailable")?;
        let bytes=disc.read_at("xbox360/anim/anim.img",e.offset,usize::from(e.used_blocks)*2048)?;
        let dictionary=anim::read_dictionary(&resource::parse(&bytes)?)?;
        let idle=dictionary.iter().find(|a|a.name.eq_ignore_ascii_case("idle")).ok_or("idle unavailable")?;
        let mut pose:Vec<_>=bones.iter().map(|b|(Vec3::from(b.translation),Quat::from_array(b.rotation).normalize())).collect();
        pose[0]=(Vec3::ZERO,Quat::IDENTITY);
        for track in &idle.tracks {
            let value=track.sample(0);
            let bone=bones.iter().position(|b|b.id==track.bone).filter(|b|*b!=0);
            let rotation=||Quat::from_xyzw(value[0],value[1],value[2],value[3]).normalize();
            match(track.kind,bone,track.bone){
                (anim::BONE_ROTATION,Some(b),_)=>pose[b].1=rotation(),
                (anim::BONE_TRANSLATION,Some(b),_)=>pose[b].0=Vec3::from_slice(&value),
                (anim::BONE_ROTATION,None,0)=>pose[0].1=rotation(),
                (anim::BONE_TRANSLATION|anim::MOVER_TRANSLATION,None,0)=>pose[0].0+=Vec3::from_slice(&value),
                _=>{}
            }
        }
        let parents:Vec<_>=bones.iter().map(|b|b.parent).collect();
        let world=delirium::ragdoll::pose_frames(&parents,&pose,Transform::from_rotation(Quat::from_rotation_z(-std::f32::consts::FRAC_PI_2)));
        let mut build=rig.build;
        let mut frames:Vec<_>=build.parts.iter().map(|p|{
            let(t,q)=world[p.bone];Transform::from_translation(t+q*p.offset.translation).with_rotation(q*p.offset.rotation)
        }).collect();
        for(p,f)in build.parts.iter_mut().zip(&frames){p.bind=f.rotation;}
        delirium::nmrs::asset_layout(&mut build,&mut frames);
    Ok(build.dump(&frames))
}
