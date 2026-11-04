//! Adapt host polygon geometry to the original 82367988 sphere probe.
//! Do not substitute an unsigned closest-point ranking or invent a palm offset.
use super::*;
use delirium::sphere::{self, Mesh, Poly};
use std::collections::{BTreeMap, HashMap};
use std::sync::{Arc, Mutex};

// Shapes are immutable, registered once and capped by VEHICLE_SHAPES. This
// separate query mesh does not alter the GJK physical collider or its margin.
static PROBE_MESHES: Mutex<BTreeMap<i32, Arc<Mesh>>> = Mutex::new(BTreeMap::new());

fn mesh_from_rows(rows: &[f32]) -> Mesh {
    let mut mesh=Mesh::default();
    // IV's fourth corner == 0 identifies a triangle. Reserve index zero so a
    // real fourth corner on an imported box can never alias that sentinel.
    mesh.verts.push(Vec3::ZERO);
    let mut vertices=HashMap::<[u32;3],u16>::new();
    let mut polygon=|points:&[Vec3]| {
        let mut ids=[0;4];
        for (i,p) in points.iter().enumerate() {
            let key=p.to_array().map(|x|if x==0. {0}else{x.to_bits()});
            ids[i]=*vertices.entry(key).or_insert_with(|| {
                let id=mesh.verts.len() as u16;mesh.verts.push(*p);id
            });
        }
        let n=(points[1]-points[0]).cross(points[2]-points[0]).normalize();
        mesh.polys.push(Poly{n,mat:0,v:ids,nb:[u16::MAX;4]});
    };
    for row in rows.chunks_exact(10) {
        let c=Vec3::from_slice(&row[1..4]);
        if row[0]==0. {
            polygon(&[c,Vec3::from_slice(&row[4..7]),Vec3::from_slice(&row[7..10])]);
        } else if row[0]==1. {
            // Exact six planar faces, not a tessellated sphere approximation.
            let h=Vec3::from_slice(&row[4..7]);
            let corners=[
                c+Vec3::new(-h.x,-h.y,-h.z),c+Vec3::new(h.x,-h.y,-h.z),
                c+Vec3::new(h.x,h.y,-h.z),c+Vec3::new(-h.x,h.y,-h.z),
                c+Vec3::new(-h.x,-h.y,h.z),c+Vec3::new(h.x,-h.y,h.z),
                c+Vec3::new(h.x,h.y,h.z),c+Vec3::new(-h.x,h.y,h.z),
            ];
            for face in [[3,2,1,0],[4,5,6,7],[0,1,5,4],[1,2,6,5],[2,3,7,6],[3,0,4,7]] {
                polygon(&face.map(|i|corners[i]));
            }
        }
        // The source probe consumes polygon/curved-bound polygons, not a
        // stand-in polyhedron for SA's analytic spheres. These stay in physical
        // collision; they cannot provide a polygon grab surface through this API.
    }
    // Exact welded edges recover the original across-edge normal arbitration.
    // Nonmanifold edges have no unique neighbour: keep the explicit boundary.
    let mut edges=BTreeMap::<(u16,u16),Vec<(usize,usize)>>::new();
    for (p,poly) in mesh.polys.iter().enumerate() {
        let n=if poly.v[3]==0 {3}else{4};
        for e in 0..n {
            let (a,b)=(poly.v[e],poly.v[(e+1)%n]);
            edges.entry((a.min(b),a.max(b))).or_default().push((p,e));
        }
    }
    for pair in edges.values().filter(|p|p.len()==2) {
        let ((a,ea),(b,eb))=(pair[0],pair[1]);
        mesh.polys[a].nb[ea]=b as u16;mesh.polys[b].nb[eb]=a as u16;
    }
    if mesh.verts.len()>1 {
        let lo=mesh.verts[1..].iter().copied().fold(Vec3::splat(f32::INFINITY),Vec3::min);
        let hi=mesh.verts[1..].iter().copied().fold(Vec3::splat(f32::NEG_INFINITY),Vec3::max);
        mesh.cen=(lo+hi)*0.5;
        mesh.radius=mesh.verts[1..].iter().map(|p|p.distance(mesh.cen)).fold(0.,f32::max);
    }
    mesh
}

fn local_probe(mesh:&Mesh,centre:Vec3,radius:f32)->Option<sphere::Hit> {
    let identity=[Vec3::X,Vec3::Y,Vec3::Z,Vec3::ZERO];
    sphere::probe(&identity,&[(&identity,Some(mesh))],centre,radius)
}

/// Original sphere probe in registered shape-local coordinates. Output:
/// surface xyz, source normal xyz, signed depth. Returns 1 hit / 0 miss.
/// Across host components, compare source rank: depth>radius ? radius-depth :
/// depth; positive-depth hits only, ties retain the first component.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nm_vehicle_shape_sphere(shape:i32,centre:*const f32,radius:f32,out:*mut f32)->i32 {
    if shape<0||centre.is_null()||out.is_null()||!radius.is_finite()||radius<=0. {
        return fail(NM_E_ARG,"invalid vehicle sphere query");
    }
    if !delirium_data::kv::open() {return fail(NM_E_STATE,"sphere query requires nm_init");}
    let c=unsafe{v3(centre)};
    if !c.is_finite(){return fail(NM_E_ARG,"nonfinite vehicle sphere centre");}
    let mesh={
        let mut meshes=PROBE_MESHES.lock().unwrap_or_else(|e|e.into_inner());
        if let Some(m)=meshes.get(&shape){m.clone()} else {
            let shapes=VEHICLE_SHAPES.lock().unwrap_or_else(|e|e.into_inner());
            let Some((key,_))=shapes.get(shape as usize)else{return fail(NM_E_ARG,"unknown vehicle shape");};
            let rows:Vec<f32>=key.iter().copied().map(f32::from_bits).collect();
            let m=Arc::new(mesh_from_rows(&rows));meshes.insert(shape,m.clone());m
        }
    };
    let Some(hit)=local_probe(&mesh,c,radius)else{return 0;};
    unsafe{put3(out,hit.at);put3(out.add(3),hit.n);*out.add(6)=hit.depth;}
    1
}

#[cfg(test)]
mod tests {
    use super::*;
    fn init(){
        let root=std::path::PathBuf::from(std::env::var("NM_TEST_ASSETS").expect("NM_TEST_ASSETS"));
        let path=std::ffi::CString::new(root.join("fred.xrt").to_str().unwrap()).unwrap();
        assert!(matches!(unsafe{nm_init(path.as_ptr())},0|1));
    }
    fn down_face(z:f32)->[f32;10]{[0.,-1.,-1.,z,0.,1.,z,1.,-1.,z]}
    #[test]
    fn signed_original_ranking_rejects_nearest_back_face_when_front_face_exists(){
        init();
        let rows=[down_face(0.),down_face(0.3)].concat();
        let mesh=mesh_from_rows(&rows);
        let hit=local_probe(&mesh,Vec3::new(0.,0.,0.05),0.6).unwrap();
        // Unsigned closest surface chooses z=0 at 5cm. Original ranking takes
        // the farther front-facing surface at z=0.3 (depth=0.35 beats -0.05).
        assert!((hit.at.z-0.3).abs()<1e-6,"{hit:?}");
        assert!(hit.n.distance(-Vec3::Z)<1e-6);
        assert!((hit.depth-0.35).abs()<1e-6);
    }
    #[test]
    fn original_back_face_keeps_signed_depth_and_face_normal(){
        init();let mesh=mesh_from_rows(&down_face(0.));
        let hit=local_probe(&mesh,Vec3::new(0.,0.,0.05),0.6).unwrap();
        assert!((hit.depth-0.65).abs()<1e-6);
        assert!(hit.n.distance(-Vec3::Z)<1e-6,"must not fabricate centre-point normal");
        assert!(local_probe(&mesh,Vec3::new(0.,0.,0.61),0.6).is_none());
    }
    #[test]
    fn box_faces_keep_quad_sentinel_and_adjacency_without_changing_collider(){
        init();let mesh=mesh_from_rows(&[1.,0.1,0.2,0.3,0.5,0.6,0.7,0.,0.,0.]);
        assert_eq!(mesh.polys.len(),6);
        assert!(mesh.polys.iter().all(|p|p.v[3]!=0&&p.nb.iter().all(|n|*n!=u16::MAX)));
        let hit=local_probe(&mesh,Vec3::new(0.7,0.2,0.3),0.2).unwrap();
        assert!(hit.at.distance(Vec3::new(0.6,0.2,0.3))<1e-6);
        assert!(hit.n.distance(Vec3::X)<1e-6);
    }
    #[test]
    fn exact_welded_triangle_edges_and_rigid_frame_covariance(){
        init();
        let rows=[0.,-1.,-1.,0.,1.,-1.,0.,1.,1.,0., 0.,-1.,-1.,0.,1.,1.,0.,-1.,1.,0.];
        let mesh=mesh_from_rows(&rows);
        assert_eq!(mesh.verts.len(),5);
        assert_eq!(mesh.polys[0].nb.iter().filter(|x|**x!=u16::MAX).count(),1);
        let c=Vec3::new(0.2,0.1,0.1);let hit=local_probe(&mesh,c,0.6).unwrap();
        let q=Quat::from_rotation_y(0.7)*Quat::from_rotation_z(0.4);let at=Vec3::new(20.,-6.,3.);
        let root=[q*Vec3::X,q*Vec3::Y,q*Vec3::Z,at];
        let id=[Vec3::X,Vec3::Y,Vec3::Z,Vec3::ZERO];
        let world=sphere::probe(&root,&[(&id,Some(&mesh))],at+q*c,0.6).unwrap();
        assert!(world.at.distance(at+q*hit.at)<2e-5);
        assert!(world.n.distance(q*hit.n)<2e-5);
    }
    #[test]
    fn public_api_uses_registered_geometry_and_does_not_invent_sphere_polygons(){
        init();let row=down_face(0.);
        let shape=unsafe{nm_vehicle_shape_register(row.as_ptr(),1)};assert!(shape>=0);
        let mut out=[f32::NAN;7];
        assert_eq!(unsafe{nm_vehicle_shape_sphere(shape,[0.,0.,0.05].as_ptr(),0.6,out.as_mut_ptr())},1);
        assert!(out[5]< -0.99&&out[6]>0.6);
        let s=unsafe{nm_vehicle_shape_register([2.,0.,0.,0.,1.,0.,0.,0.,0.,0.].as_ptr(),1)};
        assert_eq!(unsafe{nm_vehicle_shape_sphere(s,[0.,0.,1.1].as_ptr(),0.6,out.as_mut_ptr())},0);
        assert_eq!(unsafe{nm_vehicle_shape_sphere(-1,[0.,0.,0.].as_ptr(),0.6,out.as_mut_ptr())},NM_E_ARG);
    }
}
