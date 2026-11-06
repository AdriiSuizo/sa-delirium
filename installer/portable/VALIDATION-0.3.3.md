# Validation 0.3.3 — 2026-11-06

Change: the adapter, the physics engine and the combat runtime are the current
development sources (GTA IV bike throws and jacks, run-overs of riders, hand
brace before slow cars and bikes, NPC long hooks, smoothed fight animation,
attack turns, CJ getting up through blows, the side stand cleared on a shot
knock-off, per-part vehicle no-collision release). vendor/ was refreshed with
`tools/Refresh-Vendor.py` (24 changed and 3 new sources; the pivot fix was
already in the development ragdoll). The engine is also built for AVX2 CPUs as
`scripts/sa_delirium_bootstrap_avx2.dll` and chosen at start; the baseline DLL
is kept for every other CPU. `bootstrap/config/sa-contact-ranges.json` carries
the AI strike ranges of 05/11. `modloader/Delirium Texture Fixes` (priority 99)
rejoins 11 texture dictionaries that two embedded mods ship separately, built
only from the embedded mods' own textures (`installer/portable/Fix-TxdConflicts.py
--only-mods-from full-manifest.json --mods-only` in the project).

Manifest: 4033 recipes, 4020 of 0.3.2 plus the AVX2 engine and 12 texture-fix
files. Setup EXE 944,317,440 bytes, SHA256
`ac58e0d17564cd98194f78cad5e0856316cceb5d48fbe7d6c7371f09c677c679`.

## Development game

The same sources run in `game`: bike falls, jacks, shots and run-overs were
approved in game by the user on 06/11 after their cases; the chase case
(`tools/Test-ChaseInterrupt.ps1`, with a native-fall control) shows script
tasks survive the ragdoll. The texture fix was checked in game with before and
after captures of the Jefferson alley. Manual start (no test bridge):
`physics_engine_variant build=avx2`, `initialized`.

## Literal EXE

With the owner ISO and a fresh cache.

- PC 1.0 US copy (hard links to the private reference files): identified as
  "PC 1.0 US (Hoodlum)", standalone mode, installed in 217 s without shortcuts.
  Start of the installed `gta_sa.exe`: owned resources prepared,
  `physics_engine_variant build=avx2`, `initialized`. `--restore` correct.
- Real Steam: `--restore` of the installed 0.3.2, then install of 0.3.3 in
  210 s. Both owner saves carried with unchanged hashes. Start through the Steam
  launcher `gta-sa.exe`: `build=avx2`, `initialized`; child closed by its own PID.
  0.3.3 remains installed on Steam.
- RAR (RAR5, stored): integrity test passed; same EXE, CREDITS, LICENSE, README.

## Limits

Not repeated in the installed profile: the gameplay physics cases (they ran on
the development game with the same sources), the baseline engine on a CPU
without AVX2 (it is the 0.3.2 path, now delay-loaded beside the ASI), and a PC
without the DirectX runtime. Bounded checks; not all vehicles, missions,
controllers, GPUs or ISO editions.
