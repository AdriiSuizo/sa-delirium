# Validation0.3.1 — 2026-11-05

Selection confirmed: all development extras except added weapon packs; retain
prior exclusions of RoSA/ProperPlayerRetex, GTAIVtextures/HQroads and VHUD.
The full manifest contains4020 recipes:412 Steam copies,11 Steam deltas,
3361 embedded payloads and236 original-download member/delta recipes. Urbanize
is the ONLY provider package, downloaded automatically from its author8,164,839
bytes/SHA21217a20e7..., with its original readmes/license. No Urbanize original
or stock game original is embedded. The owner supplies Steam12120 and Xbox360
IV ISO; no development folder, extra owner archives or compiler on destination.

Two literal candidate EXE installs passed from416 SHA-verified stock Steam files,
external owner ISO, empty appdata/cache, PATH System32. All3360 candidate payloads
and the tools ZIP were verified. The final refresh adds the trainer's original
license and credits ONLY: all runtime/plugin/config hashes remain identical.
The final EXE/RAR additionally passed extraction/integrity/hash comparison.
Final EXE SHA256:721baf4cb14453fcc4638e688ea318c37af674dcfa2983ab79d246cef56f9b21.

Native normal startup without bridge passed with GrinchTrainer, Urbanize and
AdvancedAiming actually loaded. A save-free first-new-game profile entered the
native intro for15s, with capture reviewed. A freshly reinstalled profile loaded
the protected checkpoint directly BEFORE any new-game action and passed native
damage,4 real pistol rounds,457 physics steps,31616 bone writes, one completed
native get-up and errors0. The original trainer menu opened, navigated and closed;
643 module-local XInput reads, screenshots reviewed. No Windows input/focus or
virtual controller; physical hardware control remains untested. Original owner
save hashes were preserved. Both IV civilian decision files were regenerated
from ISO, matching protected development content after line-ending normalization.

The production adapter169681d9... and shared runtime9a17dc20... are unchanged.
The shipping ASI exports only InitializeASI. Izzy1.6 original ASI e371cef5...,
config/readme and SolidDoors=1 remain unchanged. FxsFuncs1.1 original cd9ccf96...
remains; the blocked1.2 provider source is not used. GrinchTrainer1.0 ASI14d2da38...
is original/unmodified and ships with its original noncommercial license.

Earlier failure retained: full-fire-trainer1 exited during initial checkpoint
load, with a native main.scm null-file read. Cause was NOT established. A
no-trainer comparison passed; default configuration and original configuration
then BOTH passed, so configuration was not proved causal. Subsequent direct-load,
physics and trainer cases passed, including the fresh-profile case above.
A test readiness failure also exposed an early FxsFuncs module snapshot; the
runner now waits for actual loading. A test-only reentry guard was added, but
final counter0 means it does not establish the original crash cause. These
results do not certify universal startup/load or crash-free gameplay.

Existing tests cover package boundaries; PortableCore was not changed here.
No full campaign, all missions/vehicles/skins/GPUs/ISO editions, hardware input,
complete GTAIV fidelity or guaranteed120FPS claim. Existing contact/naturalness,
armed-melee and NPC decision limitations remain.

The FINAL EXE extracted from the FINAL RAR also installed successfully in the
actual Steam library with empty cache/appdata and PATH System32. All owner save
slots were preserved from the old profile, which remains backed up. Actual Steam Play passed without a bridge with all selected extras loaded;
report full-steam-play1/report.json. Cleanup and its final startup check follow
publication and are recorded in the local handoff/full-steam-after-cleanup report.
Private failures, owner files and DM screenshots are not release assets.
