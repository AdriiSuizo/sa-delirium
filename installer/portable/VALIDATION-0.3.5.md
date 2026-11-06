# Validation 0.3.5 — 2026-11-06

Changes since 0.3.4, all in the adapter and the combat crate; the approved
physics are untouched. Details and evidence per item in the development
`HANDOFF.md` §34–37.

- Crash fixes: shop menu (GInput and Grinch Trainer both redirecting the same
  pad read, `TrainerChordGuard.hpp`), game exit (`ProcessExitGuard.hpp`) and
  restart (`FileManagerGuard.hpp`). The trainer binary ships unmodified.
- IV melee disarm (`MeleeDisarm.hpp`, `sa-combat/src/disarm.rs`): a timed block
  against an armed foe, then attack with lock-on, picks `Disarm_Bat_Res` or
  `Disarm_bl_*_Res` and hands the weapon over. SA melee weapons 2–8 go to the
  IV controller; knife and katana feed IV KNIFE (`nm_combat_feed_weapon`).
- IV police (`PoliceResponse.hpp`): `IsKillTaskAppropriate` (0x4BC3E0,
  `__cdecl` in the exe) answers true for an ambient cop attacked by a non-cop
  ped, after IV `Cop.ped`, which has no flee response. INI
  `[Combat] IVPoliceResponse`.
- IV windscreen ejection (`WindscreenEjection.hpp`, TU8 rules). INI
  `[Vehicle] WindscreenEjection`.
- §34–35: peer bumps, fast car exit, Circle dive, earlier shove, aiming while
  walking, run-overs over the windscreen, neck and wrist fixes; §37 physics
  sub-steps (no fall through the ground at speed).

vendor/ refreshed with `tools/Refresh-Vendor.py`. The SDK `combat.rs` release
modification (capture path from `NM_TEST_NATIVE_TOPOLOGY`) now lives in the
development copy, so the refresh merges it. `runtime_sources.py` also strips
`tickCopProbe`. Manifest: 4033 recipes, as in 0.3.4. Setup EXE 944,354,816
bytes, SHA256
`4da39b25aa265fa1b91489931b9d9e232512ce2889869ab679ed5245b5ec56d2`; production
ASI `6713a489f902dcfa4520048e2c3f70b05661dab17b3e7c14a4e48e45ffaf4ef3`.

## Development game

Same sources in `game` (Test-ManualStartup OK). `cargo test` in sa-combat: 52
pass, 1 ignored. In game, with save slot 2 and the combat probes (`HANDOFF.md`
§37): bat disarm (`combat_disarm taker_now=5`), cop nightstick disarm
(`taker_now=3`), knife disarm (`disarm_knife_r`, `taker_now=4`); a cop hit by
an armed civilian draws the pistol and kills the attacker (before: it fled); CJ and an
ambient front passenger leave through the windscreen and get up. The same ASI
ran on Steam before packaging (Test-SteamStart OK).

## Literal EXE

With the owner ISO and a fresh cache.

- PC 1.0 US copy (hard links to the private reference files): installed in
  285 s, standalone, without shortcuts. Start of the installed `gta_sa.exe`:
  `physics_engine_variant build=avx2`, audio guards installed, `initialized`.
  `--restore` correct. Lab deleted.
- Real Steam: `--restore` of the installed 0.3.4, then install of 0.3.5 in
  270 s. Both owner saves carried with unchanged hashes. Installed ASI matches
  the manifest. Start through the Steam launcher `gta-sa.exe`: `build=avx2`,
  `initialized`; child and launcher closed by their own PIDs. 0.3.5 remains
  installed on Steam.

## Limits

Golf club, shovel, pool cue and katana disarms were not run in game (same path
as bat and knife). The IV "communicate to nearby cops" response is not ported.
The crash scenes and the new combat are left to the user's play. Bounded
checks; not all vehicles, missions, controllers, GPUs or ISO editions.
