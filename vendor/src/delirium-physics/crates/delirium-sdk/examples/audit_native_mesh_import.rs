//! Benchmark the exact native two-sided import and compare observable geometry.
use delirium_physics::*;
use std::{env, fs, time::Instant};

fn main() {
    let args: Vec<_> = env::args_os().collect();
    assert_eq!(args.len(), 4);
    let constants = std::ffi::CString::new(args[3].to_str().unwrap()).unwrap();
    assert_eq!(unsafe { nm_init(constants.as_ptr()) }, 0);
    let data = fs::read(&args[1]).unwrap();
    let count = u32::from_le_bytes(data[..4].try_into().unwrap()) as usize;
    assert_eq!(data.len(), 4 + count * 37);
    let positions: Vec<f32> = data[4..4 + count * 36].chunks_exact(4)
        .map(|x| f32::from_le_bytes(x.try_into().unwrap())).collect();
    let materials = &data[4 + count * 36..];
    let indices: Vec<u32> = (0..count as u32 * 3).collect();
    let mut times = Vec::new();
    let mut evidence = Vec::new();
    for run in 0..31 {
        let world = nm_world_create();
        unsafe {
            let start = Instant::now();
            let status = nm_world_add_mesh_two_sided(world, positions.as_ptr(), (count * 3) as i32, indices.as_ptr(), count as i32, materials.as_ptr());
            let elapsed = start.elapsed().as_secs_f64() * 1000.0;
            assert_eq!(status, 0);
            assert_eq!(nm_world_triangle_count(world), count as i32);
            if run != 0 { times.push(elapsed); }
            if run == 0 {
                let centre = [positions[0], positions[1], positions[2]];
                for x in -36..=36 {
                    for y in -36..=36 {
                        let from = [centre[0] + x as f32 * 0.75, centre[1] + y as f32 * 0.75, centre[2] + 25.0];
                        let (mut distance, mut normal) = (0.0f32, [0.0f32; 3]);
                        let status = nm_world_raycast(world, from.as_ptr(), [0.0f32, 0.0, -1.0].as_ptr(), 50.0, &mut distance, normal.as_mut_ptr());
                        evidence.push([status as u32, distance.to_bits(), normal[0].to_bits(), normal[1].to_bits(), normal[2].to_bits()]);
                    }
                }
            }
            nm_world_destroy(world);
        }
    }
    assert!(evidence.iter().any(|row| row[0] == 1));
    let result = serde_json::json!({"triangles": count, "timings_ms": times, "rays": evidence});
    fs::write(&args[2], serde_json::to_vec(&result).unwrap()).unwrap();
}
