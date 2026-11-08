# Validation 0.3.11 — 2026-11-08

Runtime 21. Two changes in the game.

- Drunk CJ as drunk Niko (`Drunk.hpp`, engine `drunk.rs`, SDK `nm_agent_drunk_balance` and `nm_agent_player_control`):
  - Trigger: SA's drunk level (CPlayerData +0x40, opcode 052C) starts IV's level 5, the most drinks. Grinch Trainer's option sets 100 every frame from plugin-sdk's processScriptsEvent; Cheat-Menu source, `src/pages/player.cpp`.
  - Hook: the adapter wraps the call at 0x53C077 once in play. It reads the level after the scripts and writes what SA should see.
  - Duration (romandrink 0x4BA0, 0x4A88): 180 s on the drunk ragdoll and SA's drunk camera for the same 180 s, then a 10 s fade. IV's own sway (0x82AAA238) is not traced, so the sway on screen is SA's.
  - The ragdoll: the drink scripts' bodyBalance (0x4CD5, LSC4 `gta4_data::drunk::BALANCE`) with GIVE_PLAYER_RAGDOLL_CONTROL's stick push (`playerctl`).
  - The fall watch (0x104CD): down when balance fails or the head is 1.2 m or less above the ground. Still for 1 s, or 3 s at most, then 100 ms later the native get-up; back on the ragdoll for the time left. Off the ragdoll in a car. Missions keep SA's own effect. `DrunkAsNiko=0` in `[Player]` turns it off.
  - Dev game with `GTASA_DRUNK_TEST`: `drunk_started`, `drunk_ragdoll_started`, and captures of CJ staggering with arms out.
- Ground tunnel guard (`rejectGroundTunnel`):
  - The cause: in Adrii's session, a car's box pushed CJ out through its bottom and under Grove Street's road, where nothing held him, and he fell to z -43.
  - Now a step that takes the pelvis, chest or head down through an upward surface of the physics world, 0.15 m or more, is undone, keeping only upward motion.
  - A falling body with no SA ground below it and ground above is put back on that ground.
- Regressions passed: impact `Group` 35 m/s with CJ driving (0 `outside_world`), `ground-shots`, `Test-PlayerPhysics contact`. Engine compiles; dev startup `initialized`.
- FPS when shot at: Adrii's session dropped to about 63 fps in a police shootout with 5-6 physical bodies. Physics took 3-4 ms a frame; the adapter outside physics took 0.5 ms. Not changed here.

Literal EXE on a PC 1.0 US lab copy, original models, empty cache: 109 s, 329 softened models, `initialized`, restored.

Not checked by eye: walking drunk with the stick, and a drunk fall and get-up in the real game.
