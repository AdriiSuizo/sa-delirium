# Existing adapted source

This is a source snapshot of the existing SA integration, not a rewrite of its gameplay. `SOURCE-RECEIPT.json` records the source hashes at import. Subsequent deliberate changes are the ISO-backed data reader, the standalone Cargo workspace, Delirium v0.6.0's fitted-pivot correction, and an explicit environment variable for the native topology test capture.

The raw old `kv.bin`, all game programs, character files, animations, user saves and captures are excluded. The two upstream `.map` files describe locations to read from the player's own program; they are public Delirium SDK inputs, not game programs. Data still comes from the player's owned copy.

Build the x86 SDK from `src/delirium-physics`:

```
cargo build -p delirium-sdk --lib --release --target i686-pc-windows-msvc
```

Tests require real local inputs through `NM_TEST_ASSETS`, `SA_DELIRIUM_PROGRAM`, and `NM_TEST_NATIVE_TOPOLOGY`. Missing data is a test failure. There are no fabricated stand-ins or automatic test exclusions.

The C++ adapter still contains the development executable's bindings. The opt-in Steam lab translates only individually established bindings and refuses unmapped entries. It is not yet the production Steam adapter.

Delirium and the adapted integration are GPLv3; retain the upstream license and credits. MinHook carries its own license in `third-party/minhook`. See the repository's credits and dependency inventory before distributing a binary.
