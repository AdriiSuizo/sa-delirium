# Native Steam adapter — work in progress

The existing adapter remains unchanged in `vendor/src/sa-delirium`. The native
source overlay connects its prepared data to the same bootstrap DLL and local
v3 cache. It waits for readiness and does not initialize a second solver.

`Prepare-Adapter.py --output <fresh-directory>` creates a separate source copy
and records original hashes, reviewed bindings, host implementations and every
unresolved reference. It does not patch an executable or install a module.
The entire module is blocked before native access while any binding is missing.
The host must also pass the original Steam file SHA256 and PE layout checks.
Live pointers use the loaded image base. Instruction fingerprints normalize the
owner's PE relocations; ASLR stays enabled and the executable stays unchanged.

`Build-Adapter.ps1` compiles and links `DeliriumSA.asi` with the x86 static CRT,
MinHook and the shared bootstrap import library. It writes preparation and link
receipts but never installs anything. `-CompileOnly` stops after object checks.
The production overlay omits scripted fixture exports, experimental skin
capture and GPU readback services; all original authoring sources remain in
vendor. Normal physics, retargeting and native recovery remain connected.

All 148 bindings referenced by the production include closure are resolved.
The registry additionally contains reviewed fixture helpers. The actual C++
address/fingerprint code passed 384 checks over 128 functions at three simulated
ASLR bases, three changed-code negative controls and relative-call checks.

The geometry views replace the full external RenderWare header dependency. An
independent x86 probe checked twenty field/size measurements against the local
SDK; all agree. `NativeTaskIds.hpp` contains only the 25 task identifiers used by
the source. Its input hash and reference link are in `task-id-provenance.json`.
Neither the external SDK nor game assets are required to compile this overlay.

The narrow CEntity transform wrapper uses Steam's actual CMatrix and
CSimpleTransform implementations, following its native CWorld::Add path.
It follows the real native matrix ownership; no guessed matrix is substituted.
The live NPC case below exercises actor movement and rendering, but does not
separately certify every simple-transform variant. See `host-implementations.json`.

`steam-bindings.json` is a reviewed subset. Candidate files, generated matchers
and local decompiled C must never be used as a runtime address provider. The
development project's research directory keeps those materials separate.

Steam case `slot2-20261104-175106` loaded the converted slot 2 with original
scripts, spawned an ordinary native NPC, delivered nonlethal/lethal/corpse shots,
observed a completed native get-up and retained a physical corpse. There were
2,825 physics steps, 34,848 bone writes, zero reported errors and zero missed
physics frames. The owner's save/preferences were restored. This is scoped actor
acceptance; melee, vehicles, controller hardware and the full modpack still
require their own final-package checks. Alpha.4 remains an outdated laboratory
RAR. Resource evidence is in `../docs/steam-resource-validation.json`.
