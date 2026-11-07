# Validation 0.3.6 — 2026-11-07

Changes since 0.3.5. Details and evidence per item in the development
`HANDOFF.md` §39–46.

- Run with X (`NikoMovement.hpp`): under Niko's gait SA keeps its own clips, and
  X plays `sprint_civi` at once. Arms come from the native pose only when a real
  armed group is playing (`nikoNativeHoldsWeapon`). `run_audit.rs` measured the
  retargeted CJ within about 6 cm of the IV skeleton for walk, run and sprint.
- Contact cost (`ragdoll.rs`, `ragdoll_child_bounds.rs`): child bounds prepared on
  first use, shared frame arithmetic, and a far cut on the obstacle's own box.
  `traffic_step_cost`, 12 car meshes at 3.2 m: 0.28 to 0.14 ms per step.
  All 93 `delirium` tests pass, including the bit-exact manifold comparison.
- Group run-over in game, same scenario: worst window 10.47 to 7.75 ms, peak frame
  23.56 to 18.48 ms. One run each. Not measured in real traffic in game.
- `cj-uzi`, `ground-shots` and `Test-ManualStartup` passed on the final build.
- Limit Adjuster 2.0.6 pinned in `source-catalog.json` and `original-inputs.json`.
- Literal EXE on a PC 1.0 US lab copy, empty cache: installed standalone in 184 s,
  started with `build=avx2` and `initialized`, restored. The author's Drive link
  for Urbanize answered "Quota exceeded" that day, so this run covers the new
  fallback: Urbanize is `optional` in the manifest and the game installs without
  it. The path with Urbanize downloaded was not exercised on this build.
- Real Steam install from this EXE not repeated: Steam already runs the same
  runtime18 ASI and bootstrap, installed by hand.
