# Upstream review — 4 November 2026

The public Delirium main and latest release remain `93eed4d6a0aafa2e84526730ea1f54dab48ac7d4`, v0.6.0. The importer already pins this revision. The gameplay engine remains our existing adapted core; replacing that entire engine would discard the SA integration and its measured fixes.

## Incorporated in the distribution source

[Delirium b1c6b1e](https://github.com/PVCPC/delirium-physics/commit/b1c6b1e8826d6ba675b1c77f5673adfa9bb58756) fixes cached joint pivots on fitted bodies. Our fitted body also changed the measured parent/child anchors, while `Ragdoll::jconst` could return the source character's cached pivots unchanged. The regression test fails on the previous code at the first fitted joint. The port retains the cached angular matrices and uses the fitted anchors expressed in each body's local inertia frame. Unscaled cached constraints remain bit identical.

The change lives in `vendor/src/delirium-physics/crates/delirium/src/ragdoll.rs`. It is not installed in the stable development game. Tests: 93 core and 68 SDK pass on x86 with the player's local canonical body. A first SDK run passed 67 tests and failed to locate the native torso capture. `NM_TEST_NATIVE_TOPOLOGY` now supplies that real capture; no test was skipped or replaced by constants. Live Steam validation is recorded separately.

## Reviewed without replacing gameplay rules

- v0.5.2's sky improvement concerns the Playroom's Bevy renderer, which SA does not use.
- v0.6.0's PC asset endianness/export fix concerns the PC source format. This candidate imports the player's Xbox 360 disc; its body/parser compatibility has separate measured evidence.
- Custom/random body types, sliders and per-behaviour overrides are optional SDK features. They do not justify changing CJ's proportions, masses or existing reaction rules.
- [SuperSloppers/large-steal-car-4](https://github.com/SuperSloppers/large-steal-car-4) now has main `5900dacd35920cc7f2264126f8807e9ab0b51031`. The changes since the previously checked `fcb01cc55` concern render-ready routines and their checks. They do not provide a new bike/melee correction to transplant into this port. Only our reference clone was fetched; Claude's working directory was not modified.

No release has been made public. Do not equate these scoped tests with complete Steam gameplay or a finished visual modpack.
