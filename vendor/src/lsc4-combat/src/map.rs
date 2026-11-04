// Unmodified functions extracted from LSC4; see upstream.json and LICENSE.
pub fn hash_bytes(name: &[u8], seed: u32) -> u32 {
    let quoted = name.first() == Some(&b'"');
    let mut h = seed;
    for &b in name.iter().skip(quoted as usize) {
        if b == 0 || (quoted && b == b'"') {
            break;
        }
        let c = match b {
            b'A'..=b'Z' => b + 32,
            b'\\' => b'/',
            _ => b,
        };
        h = h.wrapping_add(c as i8 as i32 as u32);
        h = h.wrapping_add(h << 10);
        h ^= h >> 6;
    }
    h = h.wrapping_add(h << 3);
    h ^= h >> 11;
    h.wrapping_add(h << 15)
}
