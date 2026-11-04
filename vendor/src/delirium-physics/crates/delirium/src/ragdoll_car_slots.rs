//! Dense lookup only. The retained vector remains the owner and ordering of
//! contacts. Rebuilt after compaction; equal obstacle IDs share their slots.
use super::*;
pub(super) struct Index {
    offsets: Vec<usize>,
    stride: usize,
    slots: Vec<usize>,
}
impl Index {
    pub(super) fn new(bodies: usize, obstacles: &[Obstacle], held: &[(usize,i32,usize,Vec<Held>)]) -> Self {
        let mut identities=std::collections::HashMap::<i32,(usize,usize)>::new();
        // Equal IDs can expose different numbers of children. Reserve their
        // maximum, keeping the old key (body, ID, child) identity exactly.
        for ob in obstacles { identities.entry(ob.id).and_modify(|v|v.1=v.1.max(ob.parts.len())).or_insert((usize::MAX,ob.parts.len())); }
        // Assign in source order, never HashMap iteration order.
        let mut cursor=0;
        let offsets=obstacles.iter().map(|ob| {
            let entry=identities.get_mut(&ob.id).unwrap();
            if entry.0==usize::MAX {entry.0=cursor;cursor+=entry.1;}
            entry.0
        }).collect();
        let stride=cursor;
        let mut index=Self{offsets,stride,slots:vec![usize::MAX;bodies*stride]};
        for (slot,(body,id,child,_)) in held.iter().enumerate() {
            if let Some(&(offset,count))=identities.get(id) {
                if *body<bodies&&*child<count {
                    let to=&mut index.slots[body*stride+offset+child];
                    if *to==usize::MAX {*to=slot;}
                }
            }
        }
        index
    }
    pub(super) fn get(&self,body:usize,obstacle:usize,child:usize)->Option<usize> {
        let slot=self.slots[body*self.stride+self.offsets[obstacle]+child];
        (slot!=usize::MAX).then_some(slot)
    }
    pub(super) fn insert(&mut self,body:usize,obstacle:usize,child:usize,slot:usize) {
        let to=&mut self.slots[body*self.stride+self.offsets[obstacle]+child];
        if *to==usize::MAX {*to=slot;}
    }
}
#[cfg(test)] mod tests {
    use super::*;
    #[test] fn duplicate_ids_first_match_and_reordered_compaction() {
        let part=ObPart{shape:gjk::Shape::Capsule(0.1,0.2),m:Mat3::IDENTITY,at:Vec3::ZERO,lo:Vec3::splat(-0.3),hi:Vec3::splat(0.3),mesh:None,filter:Default::default()};
        let a=Box::leak(vec![part,part,part].into_boxed_slice());let b=Box::leak(vec![part].into_boxed_slice());
        let obs=[Obstacle{id:12,parts:a,..Default::default()},Obstacle{id:29,parts:b,..Default::default()},Obstacle{id:12,parts:b,..Default::default()}];
        let held=vec![(1,12,0,vec![]),(0,29,0,vec![]),(1,12,0,vec![]),(1,12,2,vec![])];
        let mut index=Index::new(2,&obs,&held);
        assert_eq!(index.get(1,0,0),Some(0));assert_eq!(index.get(1,2,0),Some(0));
        assert_eq!(index.get(0,1,0),Some(1));assert_eq!(index.get(1,0,2),Some(3));
        assert_eq!(index.get(0,0,0),None);index.insert(0,2,0,4);assert_eq!(index.get(0,0,0),Some(4));
        let reversed=[obs[1].clone(),obs[0].clone(),obs[2].clone()];
        let compacted=vec![held[3].clone(),held[1].clone()];
        let index=Index::new(2,&reversed,&compacted);
        assert_eq!(index.get(1,1,2),Some(0));assert_eq!(index.get(0,0,0),Some(1));assert_eq!(index.get(1,2,0),None);
    }
}
