# Validation 0.3.0 — 2026-11-05

Final installer SHA256:
`ea4095bb7f5bb2623e8a318f2b91f6b86122d1bfe3d7cb4335dfd4a0a5958b86`.
The final EXE was copied alone into an inbox and installed from 416 verified
owned Steam files and an external owner ISO. Cache/appdata were empty; PATH was
Windows System32. No development folder was supplied. Its manifest has 412
Steam copies, 4 Steam deltas, 492 embedded payloads and zero provider packages.
No mod download was needed. All embedded payloads/tools hashes were checked.

Normal startup without a bridge passed on the literal final EXE installation.
A preceding EXE with exactly the same runtime hashes (only documentation added
in the final refresh) passed the first-new-game intro with no saves, and a
native-damage/firearms case: 4 real pistol rounds, 382 physical steps, 22592 bone
writes, one native get-up completed, errors 0. Captures were reviewed: stock CJ
and environment, PS2 appearance, SA HUD and physical response. Tests were
scripted/PID-scoped; no saving or desktop input.

Restore recovered exact original Steam EXE15e3cfed... and retained the owner
checkpoint unchanged. The final EXE installed in the actual Steam library;
Jugar/AppID12120 passed normal startup without a bridge. Slots1/2 were copied
to the new profile while the old profile and originals remained backed up.
No author saves/generated resources are distributed.

Adapter: `169681d9a2aec81813c0d9f3f96da9cb29219ec83e015adc086d25df9cabcad1`.
Shared runtime: `9a17dc20e2a8e7a31a41efc8aeb6fb12b40317e724cec0c6a10a3baab3aca216`.
The production adapter exports only InitializeASI, without authoring fixtures.
Izzy1.6: `e371cef5938d69ac720297cc867c22285a3da8a1170089d61dd74116403ab71c`.
Original ASI/config/readme unchanged; SolidDoors=1 retained.
FxsFuncs1.1: `cd9ccf96e6a20a721825608636e88dc15a11bc988616dc8f8c8c9a8d6cb0eaf4`.

Ten package boundary tests pass: pins, changed content, traversal, links,
duplicates, cancellation, public-password archives and wrong-password refusal.
DXVK and Windows native D3D9 startup/physics passed on this PC. The installer
probes DXVK and falls back to Windows D3D9 on device creation failure. This is
not older-GPU certification. The RAR integrity test passed.

Limits: one PC/ISO edition; no whole campaign, all missions, physical controls,
all vehicles/doors/skins/terrain or universal crash-free guarantee. Prior Izzy
bike/BMX/car-contact cases are scoped to their recorded revision/profile.
Delirium retains contact/skin-COL, naturalness, armed-melee and NPC decision
limitations. No complete GTA IV equivalence or guaranteed 120FPS.

Detailed evidence remains locally in release-work/evidence/portable-installer,
including earlier failures. Private DM screenshots and game files are excluded.
