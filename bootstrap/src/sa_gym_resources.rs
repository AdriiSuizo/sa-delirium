// SPDX-License-Identifier: GPL-3.0-only
//! Decode the owner's original ANP3 gym clips in place. No copied animation pack.
use serde_json::{Value,json};
use std::{collections::BTreeMap,fs,io::{Read,Seek,SeekFrom},path::Path};

struct Cursor<'a>{bytes:&'a[u8],at:usize}
impl<'a> Cursor<'a>{
    fn take(&mut self,n:usize)->Result<&'a[u8],String>{
        let end=self.at.checked_add(n).ok_or("ANP3 offset overflow")?;
        let out=self.bytes.get(self.at..end).ok_or("ANP3 truncado")?;self.at=end;Ok(out)
    }
    fn u32(&mut self)->Result<u32,String>{Ok(u32::from_le_bytes(self.take(4)?.try_into().unwrap()))}
    fn short(&mut self)->Result<f64,String>{Ok(i16::from_le_bytes(self.take(2)?.try_into().unwrap()) as f64)}
    fn float(&mut self)->Result<f64,String>{
        let n=f32::from_le_bytes(self.take(4)?.try_into().unwrap()) as f64;
        if !n.is_finite(){return Err("ANP3 no finito".into())}Ok(n)
    }
    fn name(&mut self)->Result<String,String>{
        let n=self.take(24)?;let end=n.iter().position(|b|*b==0).unwrap_or(n.len());
        String::from_utf8(n[..end].to_vec()).map_err(|e|e.to_string())
    }
}

fn decode(bytes:&[u8])->Result<BTreeMap<String,(Vec<Value>,f64)>,String>{
    let mut r=Cursor{bytes,at:0};
    if r.take(4)?!=b"ANP3"{return Err("Se requieren las animaciones ANP3 originales de SA".into())}
    let length=r.u32()? as usize;
    if length+8!=bytes.len(){return Err("Longitud ANP3 incompatible".into())}
    r.take(24)?;let count=r.u32()?;
    if !(1..10000).contains(&count){return Err("Número de clips ANP3 incompatible".into())}
    let mut out=BTreeMap::new();
    for _ in 0..count {
        let name=r.name()?.to_ascii_lowercase();let count=r.u32()?;r.take(8)?;
        if !(1..=128).contains(&count){return Err("Número de huesos ANP3 incompatible".into())}
        let mut tracks=vec![];let mut duration=0f64;
        for _ in 0..count {
            r.take(24)?;let kind=r.u32()?;let n=r.u32()?;let bone=r.u32()?;
            if !(1..=4).contains(&kind)||n==0||n>100000{return Err("Pista ANP3 incompatible".into())}
            let mut frames=vec![];let mut previous=-1f64;
            for _ in 0..n {
                let mut q=[0f64;4];let time;
                if kind>=3 {
                    for v in &mut q{*v=r.short()?/4096.;}time=r.short()?/60.;
                }else{for v in &mut q{*v=r.float()?;}time=r.float()?;}
                if time<previous{return Err("Reloj ANP3 no monotónico".into())}
                previous=time;duration=duration.max(time);
                let p=if kind==2||kind==4 {
                    let mut p=[0f64;3];for v in &mut p{*v=if kind==4{r.short()?/1024.}else{r.float()?};}Some(p)
                }else{None};
                frames.push(json!({"time":time,"q":q,"p":p}));
            }
            tracks.push(json!({"bone":bone,"frames":frames}));
        }
        if out.insert(name,(tracks,duration)).is_some(){return Err("Nombre ANP3 duplicado".into())}
    }
    if r.at!=bytes.len(){return Err("Datos ANP3 sobrantes".into())}Ok(out)
}

fn read_ifps(root:&Path)->Result<BTreeMap<String,Vec<u8>>,String>{
    let mut f=fs::File::open(root.join("models/gta3.img")).map_err(|e|format!("models/gta3.img: {e}"))?;
    let mut head=[0u8;8];f.read_exact(&mut head).map_err(|e|e.to_string())?;
    if &head[..4]!=b"VER2"{return Err("gta3.img original incompatible".into())}
    let count=u32::from_le_bytes(head[4..].try_into().unwrap()) as usize;
    if count==0||count>100000{return Err("Índice gta3.img incompatible".into())}
    let mut table=vec![0u8;count*32];f.read_exact(&mut table).map_err(|e|e.to_string())?;
    let mut out=BTreeMap::new();
    for e in table.chunks_exact(32){
        let name=&e[8..];let end=name.iter().position(|b|*b==0).unwrap_or(name.len());
        let name=String::from_utf8_lossy(&name[..end]).to_ascii_lowercase();
        if !["fight_b.ifp","fight_c.ifp","fight_d.ifp"].contains(&name.as_str()){continue}
        let sector=u32::from_le_bytes(e[..4].try_into().unwrap()) as u64;
        let stream=u16::from_le_bytes(e[4..6].try_into().unwrap());let size=u16::from_le_bytes(e[6..8].try_into().unwrap());
        let len=usize::from(if stream!=0{stream}else{size})*2048;
        if len<8||len>8<<20{return Err("Tamaño IFP incompatible".into())}
        let mut bytes=vec![0u8;len];f.seek(SeekFrom::Start(sector*2048)).map_err(|e|e.to_string())?;
        f.read_exact(&mut bytes).map_err(|e|e.to_string())?;
        let used=8+u32::from_le_bytes(bytes[4..8].try_into().unwrap()) as usize;
        if used>bytes.len(){return Err("IFP truncado".into())}bytes.truncate(used);
        if out.insert(name,bytes).is_some(){return Err("IFP duplicado".into())}
    }
    if out.len()!=3{return Err("Faltan las animaciones de gimnasio originales de SA".into())}Ok(out)
}

pub fn import(root:&Path,dir:&Path)->Result<(),String>{
    let ifps=read_ifps(root)?;
    let timing=fs::read_to_string(root.join("data/melee.dat")).map_err(|e|format!("data/melee.dat: {e}"))?;
    let calibration:Value=serde_json::from_slice(include_bytes!("../config/sa-gym-calibration.json")).map_err(|e|e.to_string())?;
    let mut clips=vec![];let mut sources=vec![];
    for (style,name) in [(5,"fight_b.ifp"),(6,"fight_c.ifp"),(7,"fight_d.ifp")] {
        let bytes=&ifps[name];let bank=decode(bytes)?;
        let digest=blake3::hash(bytes).to_hex().to_string();
        // The forward-stroke calibration belongs to these exact owned tracks.
        // A replacement IFP is an unsupported input, never a reason to guess.
        if calibration["ifps"][name]!=digest{return Err(format!("{name} no coincide con la calibración anatómica"))}
        sources.push(json!({"path":format!("models/gta3.img/{name}"),"blake3":digest}));
        let mut active=false;let mut reach=None;let mut markers=BTreeMap::new();
        let combo=format!("UNARMED_{}",style-3);
        for line in timing.lines(){
            let words:Vec<_>=line.split_whitespace().collect();if words.is_empty(){continue}
            if words[0]=="START_COMBO" {active=words.get(1)==Some(&combo.as_str());continue}
            if !active{continue}if words[0]=="END_COMBO"{break}
            let number=|i:usize|->Result<f64,String>{words.get(i).ok_or("Marcador melee incompleto")?.parse::<f64>().map_err(|e|e.to_string())};
            if words[0]=="RANGES"{reach=Some(number(1)?);}
            for step in 1..=3 {if words[0]==format!("ATTACK{step}"){markers.insert(step,(number(1)?/30.,number(2)?/30.));}}
        }
        let reach=reach.ok_or("Falta el rango de gimnasio en melee.dat")?;
        for receiver in [false,true] {if receiver&&style!=7{continue}
            for step in 1..=3 {
                let name=if receiver{format!("hitd_{step}")}else{format!("fight{}_{step}",['b','c','d'][(style-5)as usize])};
                let (tracks,duration)=bank.get(&name).ok_or_else(||format!("Falta {name}"))?;
                let key=format!("sa_gym_{style}/{}{step}",if receiver{"hit"}else{"attack"});
                let &(hit,chain)=markers.get(&step).ok_or("Falta marcador de ataque")?;
                let c=&calibration["clips"][&key];let start=c["strike_start"].as_f64().ok_or("Falta calibración del contacto")?;
                let mut clip=json!({"key":key,"style":style,"step":step,"receiver":receiver,"reach":reach,"duration":duration,
                    "hit":hit,"chain":chain,"strike_start":start,"tracks":tracks});
                if !c["strike_bone_sa"].is_null(){clip["strike_bone_sa"]=c["strike_bone_sa"].clone();}
                clips.push(clip);
            }
        }
    }
    fs::write(dir.join("sa-gym-clips.json"),serde_json::to_vec(&json!({"version":1,"source":"Owned original SA gym tracks; kick button only","sources":sources,"clips":clips})).map_err(|e|e.to_string())?).map_err(|e|e.to_string())
}
