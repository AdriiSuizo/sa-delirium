//! Replay the owned TU8 body-move reference cases with the vehicle runtime's IV data.
use delirium_data::{kv, wheelstep};
use std::io::{self, BufRead};

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let program =
        std::fs::read(std::env::var_os("SA_IV_PROGRAM").ok_or("SA_IV_PROGRAM required")?)?;
    if !kv::unlock_program(&program) {
        return Err("unsupported owned IV program".into());
    }
    for line in io::stdin().lock().lines() {
        let values: Vec<_> = line?
            .split_whitespace()
            .map(|word| u32::from_str_radix(word, 16).map(f32::from_bits))
            .collect::<Result<_, _>>()?;
        if values.len() != 25 {
            return Err("expected 25 input values".into());
        }
        let row = |index: usize| values[index * 4..index * 4 + 4].try_into().unwrap();
        let result = wheelstep::advance(
            &[row(0), row(1), row(2), row(3)],
            row(4),
            row(5),
            values[24],
        );
        let words: Vec<_> = result
            .into_iter()
            .flatten()
            .map(|f| format!("{:08x}", f.to_bits()))
            .collect();
        println!("{}", words.join(" "));
    }
    Ok(())
}
