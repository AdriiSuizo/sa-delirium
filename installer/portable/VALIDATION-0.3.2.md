# Validation 0.3.2 — 2026-11-05

Change: the installer accepts the game by its files, not by store. A classic
San Andreas folder is accepted when every file it needs matches either its Steam
NewSteam R2 or its PC 1.0 US hash. Steam folders keep the 0.3.1 in-place install
and the Steam launcher. Any other accepted folder (PC 1.0, or a Steam copy outside
a Steam library) installs into its own destination, `%LOCALAPPDATA%\SA-Delirium\Juego`
by default, with Desktop and Start Menu shortcuts; the game folder is only read.

Manifest: 4020 recipes, the same as 0.3.1. Compared with the published 0.3.1
manifest, the only changed output is `Documentation/README.md`; 16 recipes gain
PC 1.0 alternatives (the executable under both names, audio streams, gta3.img,
gta_int.img, anim.img, main.scm, script.img, TrakLkup.dat). Runtime, plugin and
configuration hashes are unchanged. Setup EXE 862,951,424 bytes, SHA256
`146c3a84e0002d16575ba31361b61a7068a2a8b72d3d8db656915a861549ff74`.

## Unit tests

- `tests/InstallerTests.cs`: 47 assertions pass, with a short and a long root
  (long paths exercise the new extended-path moves).
- `tests/PortablePackages.Tests.cs`: 25 cases pass, including edition
  identification (Steam, downgraded Steam, PC 1.0, Definitive Edition, unknown
  executable, already installed), copy or patch per measured hash, standalone
  install and restore with shortcuts, save carry-over, and a full preparation
  with test doubles that records the outputs actually produced and stops an
  unattended install clearly when the DirectX runtime is missing.

## Literal EXE

All with the published owner ISO, a fresh cache and no development folder.

- PC 1.0 US copy (hard links to the private reference files): identified as
  "PC 1.0 US (Hoodlum)", standalone mode. Installed in 3 min 24 s with real
  Desktop and Start Menu shortcuts pointing at `Juego\SA\gta_sa.exe`
  (SHA `f01a00ce…`). Hidden start from the shortcut target: Delirium
  `initialized` (6 events). `--restore` removed both shortcuts and kept the
  profile under `SA-Delirium-backups`.
- Real Steam: `--restore` of the installed 0.3.1, then install of 0.3.2 in
  3 min 40 s. Both owner saves were carried from the 0.3.1 backup with
  unchanged hashes; the backup was left as it was. Hidden start through the
  Steam launcher `gta-sa.exe` (SHA `e4af63de…`): `initialized` (6 events), child
  process closed by its own PID. 0.3.2 remains installed on Steam.
- RAR (RAR5, stored): extracted EXE identical by SHA256.

Earlier in the day, with the same sources and a candidate build, a real
end-to-end run produced 4020 outputs identical to 0.3.1 from Steam, and from
PC 1.0 differed in exactly the 12 expected files (full radio, gta3.img,
gta_int.img), all equal to the development game.

## Development game checks

Izzy 1.6 and the Delirium pivot fix were applied to `game` (they had shipped in
0.3.0 without that step). Forced runs on 2026-11-05:

| Scenario | Result | Same as last run before these changes |
|---|---|---|
| mission-boundary | passed | yes (passed 28/10) |
| bike-hits | failed: physics error / upward motion | yes, 03/11 failed the same way 8 of 10 runs; the pre-pivot build also fails |
| player-physics | failed: get-up did not complete | yes (02/11) |
| native-recovery | failed: new hit did not interrupt recovery | yes (02/11) |
| recovery-supersession | failed: ped collision not restored | yes (01/11) |
| vehicle-contacts | failed: door_grab_feedback missing | yes (01/11, 02/11) |

No scenario regressed; none of the open failures is fixed by this release.

## Load crash

The checkpoint-load close seen in 0.3.1 testing has the same signature as five
development cases since 28/10 (one of them today): an access violation writing
0x24 in `RtlEnterCriticalSection`, called from the game's CRT while
`CFileMgr` reads from a file that failed to open. All happen 11-13 s after start,
while the test bridge loads the checkpoint, inside the game's restart
initialisation (`CGame::InitialiseWhenRestarting` → `ReInitGameObjectVariables`
or `CTheScripts::Init`) behind the load hooks of other plugins (GrinchTrainer,
V_HUD in development, CLEO+, FxsFuncs, Urbanize). The cause of the failed open
is not established; it is not attributed to any single plugin. No crash has been
observed outside the bridge's checkpoint load.

## Limits

Not tested: the DirectX setup elevation path on a PC without the runtime (only
simulated), editions without files on this PC (Rockstar Launcher, 1.0 EU, 1.01,
disc executable, Compact), physical controllers and desktop input, the full
campaign. Steam users keep the radio Steam distributes.
