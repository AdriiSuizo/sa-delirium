# Adapted-core compatibility experiment

These files are source for an isolated compatibility experiment, **not enabled
by the alpha.4 release build**. They do not add Delirium gameplay to Steam.

`legacy-kv.rs` replaces the adapted SDK's encrypted tuning payload with reads
through the pinned 0.6 data reader. The 992 legacy 32-bit and 64-bit words match
for the verified Europe/Asia retail program and the original USA reference.
The reader requires one of those exact program fingerprints. `legacy-slot-map.json`
records the typed mapping; it contains slot numbers and conversion kinds, not
extracted game values.

Eight legacy entries use constants already present in Delirium's GPL sources:

| Legacy entries | Origin |
| --- | --- |
| 202 | RNG range, `nm.rs:2197` at `e118a1dddef99a535d4acd1dd6399a4af15a7bcf` |
| 219–221 | `OWN_AT`, `nmbb.rs:341`, same revision |
| 546 | Shot phase, `nmshot.rs:702`, same revision |
| 571 | Contact epsilon, `ragdoll.rs:1522`, same revision |
| 895–896 | `MOVE_SWIM` and `USE_MOBILE_PHONE`, `delirium-data/src/tasks.rs:54–55` at `93eed4d6a0aafa2e84526730ea1f54dab48ac7d4` |

`canonical_body.rs` performs the same idle-pose preparation used by the adapted
Playroom's `getup::Clips::stand_up`, reading the player's skeleton and actual idle
clip from their ISO before applying `nmrs::asset_layout`. Importing only the model
bind pose failed the existing horizontal floor-sweep regression. The corrected
preparation passes that test without changing collision tolerances or forces.

Local evidence lives in the development project's
`release-work/evidence/legacy-iso-abi`: separate baseline and ISO SDK snapshots,
source receipts, 68 SDK tests, four contact examples per variant, a comparison
of all fields in 144 vehicle-contact cases, an x86 build, and first-import/cache
tests. The tests that consume character data require the owner's local files.
No ISO, character, animation, decrypted executable or prepared body is included
in these source files.

Limitations: native Steam actor integration, native skeleton fitting and gameplay
acceptance remain separate work. A successfully simulated SDK agent does not
establish those connections. The no-network fresh-ISO setup is also unfinished.

Credits: PVCPC and Delirium contributors, GPL-3.0-only and the upstream notices;
local SA adaptation and compatibility work retain that licence.
