// SPDX-License-Identifier: GPL-3.0-only
use std::path::PathBuf;

fn main() -> Result<(), String> {
    let args: Vec<_> = std::env::args_os().skip(1).map(PathBuf::from).collect();
    let [game, iso, decoder] = args.as_slice() else {
        return Err(
            "Uso: import_iv_melee_audio <carpeta GTA SA> <ISO Xbox 360> <vgmstream-cli.exe>".into(),
        );
    };
    sa_delirium_bootstrap::import_melee_audio(game, iso, decoder)
}
