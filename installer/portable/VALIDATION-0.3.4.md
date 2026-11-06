# Validation 0.3.4 — 2026-11-06

Change: two crashes the user hit in the Ryder mission on the installed Steam
copy, both fixed in the adapter without touching the physics.

- Leaving Grove Street: `CAEStreamThread::MainLoop` divides the start offset by
  the decoder's stream length (0x4F145C, `div ecx`); a length of 0 raised a
  division by zero. `audio_stream_directory::installLengthGuard` replaces that
  sequence with a call that returns offset 0 for an unknown length, so the track
  starts at its beginning. Event `audio_stream_length_guard_installed`.
- Buying food: Grinch Trainer 1.0 redirects the `CPad::GetPad` call of a script
  button opcode handler (0x48AEEC) to its hook (+0x329C0). In the running game
  the hook object's original pointer (+0x18) stays null while its mode is 1, and
  the callback chain called it (EIP 0, trainer frames +0x32AE0 … +0x32751 in
  `modloader.log`). `TrainerPadGuard.hpp` points that call site at a guard that
  enters the trainer only when its original is known and otherwise calls
  `CPad::GetPad` (0x53FB70). The trainer binary is shipped unmodified; the guard
  checks its module name and code bytes first. A scan of gta_sa's code found no
  other CALL/JMP into the trainer. Event `trainer_pad_guard_installed`.

vendor/ refreshed with `tools/Refresh-Vendor.py` (Adapter.cpp and the new
TrainerPadGuard.hpp). Manifest: 4033 recipes, as in 0.3.3. Setup EXE
944,318,464 bytes, SHA256
`b8a88790d1ec7e772f83aac8f3d7146327f1ddd56e06670bef93cfe4d33e78ee`; production
ASI `6934974585530e431cd9081d783f720b82305ff881d496f54e19756df61cc249`.

## Development game

The same sources run in `game`. Test start with save slot 2, free roam:
`trainer_pad_guard_installed`, the call site points into DeliriumSA.asi, and the
trainer's hook object reads original 0 with mode 1 (the crash condition). The
game kept running.

## Literal EXE

With the owner ISO and a fresh cache.

- PC 1.0 US copy (hard links to the private reference files): installed in
  201 s, standalone, without shortcuts. Start of the installed `gta_sa.exe`:
  `physics_engine_variant build=avx2`, `audio_stream_length_guard_installed`,
  `initialized`. `--restore` correct.
- Real Steam: `--restore` of the installed 0.3.3, then install of 0.3.4 in
  207 s. Both owner saves carried with unchanged hashes. Start through the Steam
  launcher `gta-sa.exe`: `build=avx2`, audio guard installed, `initialized`;
  child closed by its own PID. 0.3.4 remains installed on Steam.

## Limits

The two crash scenes were not replayed (the mission start and the shop menu
need the user's play); the trainer guard installs on the first gameplay frame,
so the installed checks stop at the menu. The trainer's own handling of that
one button check is skipped while its original is null. Bounded checks; not all
vehicles, missions, controllers, GPUs or ISO editions.
