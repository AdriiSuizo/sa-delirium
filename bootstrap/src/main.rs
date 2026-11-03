// SPDX-License-Identifier: GPL-3.0-only
use std::path::PathBuf;

fn main() {
    let root = std::env::args_os().nth(1).map(PathBuf::from);
    let result = root
        .as_deref()
        .ok_or_else(|| "Uso: sa-delirium-bootstrap <carpeta de gta-sa.exe>".to_string())
        .and_then(sa_delirium_bootstrap::prepare);
    match result {
        Ok(status) => println!("{status}"),
        Err(error) => {
            eprintln!("{error}");
            std::process::exit(1);
        }
    }
}
