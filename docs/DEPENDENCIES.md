# Components and current scope

The original Steam executable remains unchanged. The distribution target is
classic Steam build 459558 (`gta-sa.exe`, SHA-256 beginning `15e3cfed`). These
statuses describe tested artifacts, not everything present in development.

| Component | Current candidate | Origin / license |
| --- | --- | --- |
| Existing Delirium engine and SA adapter | Native Steam bindings, shared core, ASLR-safe; real NPC damage/recovery/corpse cases pass | Delirium GPL-3.0, MinHook BSD; complete notices and retained sources |
| IV movement/combat resources | First-run ISO import, 418 clips including locally generated SA gym techniques | Read from the owner's games; never included in source or RAR |
| Offline XEX format support | No network or shared key cache needed | Public Xenia support code, BSD-3-Clause |
| Ultimate ASI Loader 10.0.2 | Bundled; loader plus memory and physical-path VFS | ThirteenAG and contributors, MIT |
| SilentPatch SA 34.1 | Bundled official binary and INI; original Steam supported paths | Silent / CookiePLMonster, MIT |
| PS2 time cycle and colour | Own native integration; uses installed `timecycp.dat` | Own code; no game table distributed |
| Borderless window and right-stick camera | Own native integration; preferences/desktop preserved in scoped tests | Own code; physical controller coverage still incomplete |
| Vehicle suspension | Own data overlay; SA steering/engine/brakes retained | Own code, original handling data read locally |
| Native damageable-model storage | Steam backend follows Open Limit Adjuster lifecycle; original constructor, stable model addresses and native cleanup. Case211822 creates74 and cleans every RW resource on normal exit | Open Limit Adjuster reference, MIT; scoped backend source and notice included |
| Visual archive integration | Own streaming IMG composer and existing UAL path API; no old Mod Loader ASI | Own code; source provenance recorded per payload |

The streaming INI is read before Init2 resets its budget. The native backend configures the verified initializer once; it does not rewrite a live budget every frame. Case211822 confirms512MiB and native post-effect activation. The unmodified executable remains non-LAA.

## Visual payloads

`dev.5` is a core-only candidate. `dev.6.local` additionally exercises a local
personal data payload: 733 model/texture replacements from HD Skin Pack 299 and
NeXtGen Remaster Weapons. It is **not the complete visual modpack**. Forty other
model/texture files need explicit archive/map routing; weapon WAVs are not yet
integrated. No unsupported scripts or ASIs are copied just to make an inventory
look complete.

The local builder records source, priority, conflicts, ignored paths and rights
in `SAStandalone-assets/provenance.json`. It excludes entries identical to the
owner's original resource. The runtime composes a separate IMG on the owner's
machine; originals remain unchanged. Generated archives/cache contain local game
data and must not be uploaded. The local-only payload is excluded from GitHub.
`Build-Release.ps1` rejects it unless explicitly building a `.local` personal
candidate. This does not confer redistribution rights.

| Requested group | Remaining connection |
| --- | --- |
| RoSA Evolved / Proper Player Retex | Included license files explicitly forbid modpacks. Local import only; no assets included in a distributable RAR. Large archive splitting/host limits also need validation. |
| SkyGfx Extended | Own colour/timecycle implemented; complete radiosity and building/vehicle pipelines not yet ported. |
| BSOR / LOD Vegetation / NextGen Grass | Data/map composition, wind shader and permissions need validation. |
| Improved Lamps / Brightened / Jefferson / LV Neon / Parallax | Models, TXD parent relations, IDE/IPL/COL layers and effect/script connections remain. |
| GTA IV Textures / HQ Roads / Rubbish | Asset provenance and map/script connections remain; no extracted IV assets distributed. |
| NextGen Effects / Enhance ParticleTXD / FxsFuncs | Particle/effect resources plus a compatible native/script implementation remain. |
| Weapon sounds | SA audio bank import/overlay remains. Visual models alone do not enable sounds. |
| Project2DFX | MIT source available; old fixed-address SA plugin is not a Steam R2 port. |
| Urbanize / CLEO | Existing CLEO5 `GV_STEAM` table targets the older Steam executable, not this R2 image. Do not install it on the strength of its version name. |
| Framerate Vigilante | MIT source inspected; 85 fixed addresses need native review before carrying its high-FPS fixes. Native Steam frame limit currently retained. |
| GInput / trainer / advanced aiming / other QoL | Own right-stick path is active; complete hardware input and these systems still need explicit verification/port. |
| GTA V HUD, minimap and weapon wheel / DLSS / inactive experiments | Excluded by request. |

## Evidence and limits

`native-gameplay-validation.json` records core cases. Visual archive validation
is separate: the host must read the generated IMG and its actual directory/model,
then the same actor must pass real physics, recovery and corpse damage. A loaded
DLL, VFS registration or pretty screenshot is not enough. Native vehicle/melee
regressions, complete input coverage and the full requested pack remain open.

Sources consulted: [Delirium](https://github.com/PVCPC/delirium-physics),
[UAL VFS API](https://github.com/ThirteenAG/Ultimate-ASI-Loader/blob/v10.0.2/source/loader/vfs/api.cpp),
[SilentPatch](https://github.com/CookiePLMonster/SilentPatch),
[Mod Loader](https://github.com/thelink2012/modloader),
[CLEO5 version table](https://github.com/cleolibrary/CLEO5/blob/master/source/CGameVersionManager.cpp),
[Framerate Vigilante](https://github.com/GTAmodding/FramerateVigilante).

The failed dev.7.local artifact is quarantined: its added damageable lamp models overflowed the stock70-entry store. The manual post-artifact correction passed startup, native TXD/model routing, road physics and normal model cleanup. The next packaged revision must repeat those checks before acceptance.
