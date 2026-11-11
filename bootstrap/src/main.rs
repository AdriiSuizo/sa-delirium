// SPDX-License-Identifier: GPL-3.0-only
use std::path::PathBuf;

fn main() {
    let mut args = std::env::args_os().skip(1);
    let root = args.next().map(PathBuf::from);
    if root.as_deref() == Some(std::path::Path::new("--check-iso")) {
        let result = match (args.next(), args.next()) {
            (Some(path), None) => sa_delirium_bootstrap::check_iso(PathBuf::from(path).as_path()),
            _ => Err("Uso: sa-delirium-bootstrap --check-iso <ISO externa>".to_string()),
        };
        report(result.map(|()| "ISO de GTA IV para Xbox 360 comprobada"));
        return;
    }
    let iso = match (args.next(), args.next(), args.next()) {
        (None, None, None) => None,
        (Some(flag), Some(path), None) if flag == "--iso" => Some(PathBuf::from(path)),
        _ => {
            eprintln!("Uso: sa-delirium-bootstrap <carpeta de gta-sa.exe> [--iso <ISO externa>]");
            std::process::exit(2);
        }
    };
    let result = root
        .as_deref()
        .ok_or_else(|| "Uso: sa-delirium-bootstrap <carpeta de gta-sa.exe>".to_string())
        .and_then(|root| sa_delirium_bootstrap::prepare_with_iso(root, iso.as_deref()));
    report(result);
}

fn report(result: Result<&str, String>) {
    match result {
        Ok(status) => println!("{status}"),
        Err(error) => {
            eprintln!("{error}");
            std::process::exit(1);
        }
    }
}
