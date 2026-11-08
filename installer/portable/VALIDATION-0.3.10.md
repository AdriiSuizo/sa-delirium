# Validation 0.3.10 — 2026-11-08

Runtime 20. Three fixes in the game, one in the installer and the 90 fps cap.

- CJ impact damage: the ragdoll keeps a second contact record (`Ragdoll::hurt`) of every solved world and obstacle contact, ground included. The adapter reads it each step (`nm_agent_take_impact`) and computes IV's per-impact damage (82465360, `OriginalImpactDamage.hpp`, player cut .66, vehicle x6). SA calculates armour and attribution natively. Dev game, `Test-PlayerPhysics contact`: 100 to 95.5 HP from the car hit and the landing. `motorcycle` exit at 7 m/s: 100 to 69.8 HP, recovered with the native get-up. The motorcycle check still looks for `accepted_native_bike_ejection`, an event the adapter has not produced since 01/11, so that check is stale.
- Fast runovers: `physicsWorldOrigin` now adds the nearest bodies first and only while CJ, the pending hits and every taken body still fit the physics box. The old midpoint lagged behind a fast car and released hit bodies as `outside_world`, which brought back SA's animation. Impact `Group`, 45 m/s, CJ driving, 4 peds: 7 solved road reactions, 4 relaxed rolls, 0 `outside_world`, mean frame 11.2 ms.
- Crash over water: the installer took FxsFuncs settings written for NextGen Effects (`HeliDustWaterPrims = 4`), and NextGen Effects is no longer shipped. Low flights over water crashed at gta_sa+0xA9F54. The package now carries the author's own ini from the same archive (`HeliDustWaterPrims = -1`), asserted at build time.
- FramerateVigilante `FPSlimit=90` (frozen hash updated). It applies with the in-game frame limiter on.
- Engine `cargo test -p delirium --lib --release`: 93 passed, 0 failed. Dev startup `initialized`.
- Literal EXE on a PC 1.0 US lab copy, original models, empty cache: 110 s, 329 softened models, no HD files, `initialized`, restored.

Not checked by eye: how strong the new impact damage feels in long falls and blasts. Two startup crashes from Adrii's recording session were in an unloaded module with `apphelp` and `gameoverlay` on the stack, likely from the recording or overlay injection, not from the mod.
