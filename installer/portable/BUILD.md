# Portable package source

This builds the Steam SA1.0-compatible profile. It does not build the unfinished
native R2 renderer/model-store port. The original development game is not needed
on the destination PC.

Build prerequisites: Windows10/11 x64, .NET Framework4.8 compiler, Visual Studio
C++ x86 tools, Rust with the i686-pc-windows-msvc target, Python3.12, and7-Zip.
The public plugin-sdk interface-header closure and MinHook sources are included.
Rust versions are locked by bootstrap/Cargo.lock. Preserve all original notices.

1. Verify the vendored sources with tools/Verify-Vendor.py.
2. Build the shared DLL and CLI with cargo build --manifest-path
   bootstrap/Cargo.toml --locked --release --target i686-pc-windows-msvc
   --target-dir build/bootstrap-portable --bin sa-delirium-bootstrap --lib.
3. Run Prepare-DevelopmentRuntime.py --out build/portable-production-runtime2,
   then Build-DevelopmentRuntime.cmd with that absolute directory, VsDevCmd.bat,
   this repository, an unused fourth argument, and the shared DLL import library.
   Production source generation removes authoring-only hooks and exports. Do not
   use --include-test-diagnostics for a release. InitializeASI is the only export.
4. Build DeltaTool.exe and RendererProbe.exe with their included build scripts.
5. BrowserDownload is an optional historical download backend. The full
   embedded preset does not ship or need it or WebView2.
6. Run Prepare-RustNotices.py. Prepare-PlatformSupport.py creates the documented
   minimal DirectSetup set from the signed official June2010 redistribution.
7. Build-PortablePackage.py requires the maintained build reference, pinned
   original-author archives and the approved original Izzy1.6 files. Those are
   build-time inputs. Use --preset full --embed-mods --version 0.3.1 and an absolute
   --izzy-source path. It resolves selected plugins at build time, fails on
   unaccounted files and emits source-checked Steam deltas and embedded files
   and their redistribution basis. It never embeds game executables, game IMG
   banks, owner saves, generated IV resources, RoSA or the GTA V HUD.
   The game, not the store, is the source. --classic-source with the PC 1.0 US
   versions of the files that differ from Steam (the gtasa-open-downgrader
   output layout, tools/downgrade-output) adds each as an accepted original of
   its recipe: identical outputs are copied, the executable needs one small
   delta. A PC copy outside a Steam library installs to its own folder
   (Standalone.cs) with shortcuts and is only read.
   To accept another Steam build of gta-sa.exe (German R2, or a future Steam
   update), pass --steam-edition "Name=C:/owned/copy/gta-sa.exe" once per build.
   Only the executable differs between those builds, so the package gains one
   verified delta to the same 1.0 output and the profile lists the extra hash.
   Every other Steam file must still match the stock reference. Install and
   restore journal the exact executable found. Editions.cs only names known
   builds for the user; acceptance always comes from the manifest.
   For an audited classic PC executable, pass `--classic-executable PATH`.
   The builder accepts only a SHA-256 in `VERIFIED_CLASSIC_EXECUTABLES` and
   creates a delta to the existing profile executable. The audited SA DirectX
   Optimiser variant uses this route. The installer still verifies the other
   files and installs the variant in a separate folder.
   Before it: build the AVX2 engine (same cargo command with RUSTFLAGS=-C
   target-cpu=haswell and --target-dir build/bootstrap-portable-avx2 --lib),
   and the texture fixes with Fix-TxdConflicts.py --game <development game>
   --only-mods-from full-manifest.json --mods-only --out build/texture-fixes-0.3.3.
   Refresh vendor/ from development with tools/Refresh-Vendor.py.
8. Build-PortableSetup.ps1 embeds the manifest, freshly compiled current Steam
   launcher and small verified tools ZIP. No development sidecar is used.

For the 0.4.0 unified package, generate the adapter with
`tests/Prepare-VehicleLabRuntime.py --shipping --out <native>` and compile it
with the same native build command. Run `Build-UnifiedCandidate.py` with the
verified base package, native build, updated bootstrap CLI and DLL, and
vgmstream release directory. Pass the matching AVX2 DLL with `--avx2`.
Pass `--version 0.4.0` and compile its output with `Build-PortableSetup.ps1`. The install CLI selects the
minimal profile with `--solo`. See [Vehicle Lab](../../docs/VEHICLE-LAB.md).

Do not publish a package merely because these commands complete. Validate the
literal EXE/RAR from owned stock Steam files and an external owner ISO with an
empty archive cache; inspect every acquired component, normal startup, native
physics/gameplay, restoration and Steam Play. Provider validation, cancellation
and changed source files must stop safely. A warmed private source fixture is
not evidence of an automatic clean-PC download.

Original build inputs are pinned in source-catalog.json and the private routing
receipt. The full preset embeds all selected extra packages except Urbanize. Its single
original HTTPS download is automatic and pinned; no provider-page interaction or
additional owner file is required. No destination development/cache inputs. Keep original plugin
notices and separate author terms. Do not embed the prohibited Urbanize/RoSA
packs. Keep Urbanize as an original-download recipe, including its license. Public archive passwords apply only to build-time author containers.

The shared runtime, adapter and integration code use GPL3 with the preserved
Delirium additional permission. Include corresponding source for the same
release and the generated dependency notices. Separate original-author plugins
retain their terms. Izzy1.6 is included by explicit author permission, with its
ASI, configuration and readme unchanged; retain the public author link in
CREDITS.md. Private permission screenshots and local test reports are not release
assets.
