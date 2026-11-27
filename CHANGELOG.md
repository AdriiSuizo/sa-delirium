# Historial / Changelog

## 0.6.5 · cámara, combate y conducción

- Apuntado a pie y disparo dual de SA, corrección de la proyección de cámara y de los efectos de contraataque.
- Amortiguamiento en taludes y comprobaciones de los blancos durante el disparo. [Notas de la versión](docs/RELEASE-0.6.5.md).

## 0.6.4 · corrección del modo de vídeo

- El arranque usa un modo disponible cuando no existe la resolución exacta solicitada. [Pruebas con DXVK y Direct3D 9](docs/AUDIT-0.6.4-video.md).

## 0.6.3 · tráfico, apuntado y combate

- Los conductores del tráfico simplificado y con física frenan ante el cuerpo de CJ, incluso mientras se levanta. Se conserva el daño físico por contacto.
- Recupera el movimiento direccional del spray y ajusta el paso al apuntar.
- Corrige el reposo, reduce la oscilación de la cabeza con armas a dos manos y recupera efectos de golpes de contraataque.
- La asistencia antivuelco usa la inclinación actual en cada paso de integración.
- [Pruebas con tráfico real y alcance](docs/AUDIT-0.6.3-traffic.md).

## 0.6.3 · tráfico, apuntado y combate

- Los conductores del tráfico simplificado y con física frenan ante el cuerpo de CJ, incluso mientras se levanta. Se conserva el daño físico por contacto.
- Recupera el movimiento direccional del spray y ajusta el paso al apuntar.
- Corrige el reposo, reduce la oscilación de la cabeza con armas a dos manos y recupera efectos de golpes de contraataque.
- La asistencia antivuelco usa la inclinación actual en cada paso de integración.
- [Pruebas con tráfico real y alcance](docs/AUDIT-0.6.3-traffic.md).

## 0.6.2 · interiores, recuperación junto al tráfico y marcha de NPCs

- Integra las correcciones pendientes de pertenencia de peatones a interiores y de recuperación de CJ junto al tráfico: la interrupción requiere contacto corporal confirmado, no mera proximidad.
- Corrige el avance de curvas cortas de marcha de NPC cuando un cuadro atraviesa varios ciclos.
- Conserva las funciones y componentes de 0.6.0. El paquete estable merge-Adrii del 24 de noviembre ya estaba integrado; no se restaura sobre correcciones posteriores.
- Instalación completa/Solo, restauración, arranque sin puente y casos dirigidos en partida comprobados. Alcance, pruebas ignoradas y limitaciones: [auditoría 0.6.2](docs/AUDIT-0.6.2-2026-11-26.md).

## 0.6.0 · locomoción de NPCs con 15 perfiles, poses y disparos GTA IV desde vehículos, cobertura mejorada, sigilo nativo, rifles a distancia, recuperación y audio de caídas / 15-profile NPC locomotion, GTA IV vehicle drive-by poses, improved cover, native stealth, long-range rifles, recovery & CJ fall audio

### Español

- **Locomoción de peatones GTA IV con 15 perfiles y paso armado (`NpcGait*`, `NpcStride`, `NpcFootwearHost`, `ArmedStep*`, `VisibleLegPose`)**:
  - 115 clips de GTA IV retargeteados sobre 21 articulaciones en 15 perfiles (civiles, mayores, corpulentos, bandas, policías, SWAT, negocios, etc.) con zancada, giros, frenadas y altura de suela adaptadas a cada modelo (incluidos skins originales y reemplazos).
  - Caminar/correr apuntando en 8 direcciones con CJ y NPCs, re-aplicando las piernas antes del render para evitar piernas congeladas mientras se preservan torso, manos y apuntado.
- **Disparos y lanzamientos estilo GTA IV desde coches y motos (`IVDriverPose.asi`, `DriveByAim`, `DriveByControls`, `MountedDraw`, `MountedWeaponModel`, `MountedWeaponPass`, `VehicleWeaponSelection`, `PlayerPoseDraw`)**:
  - Nuevo complemento x86 `IVDriverPose.asi` (enlazado con CRT estático `/MT`) con 24 perfiles de asiento y 96 clips originales de apuntado, entrada, salida y lanzamiento en coches y motos.
  - Corrige el brazo atravesando el torso, sincroniza el modelo del arma visible en la mano (compatible con SilentPatch), permite seleccionar pistolas/subfusiles/granada/molotov del inventario y evita disparos dobles de la rutina nativa.
- **Movimiento de CJ y asesinato sigiloso nativo (`sa-combat/locomotion*`, `NikoMovement`, `NativeStealth`)**:
  - Corrige el cierre/pánico al pulsar sprint desde cuclillas (`supported_ratio` limitado al rango agachado y `gait_step` seguro) manteniendo el fade-out rápido de `wstart` (~0,25 s) y `wstop` inmediato.
  - Mejora la transición al soltar apuntado o salir de cobertura (`nikoHandoffPose` + mezcla en espacio local articular) respetando caminata con teclado o inclinación suave del mando.
  - Reconoce el asesinato sigiloso nativo con cuchillo (agachado y de pie) cediendo limpiamente pose, empujes y físicas durante la secuencia emparejada.
- **Cobertura de CJ y NPCs armados (`PlayerCover`, `CoverPosture`, `CoverSkin`, `PoliceCover`)**:
  - Corrige la postura agachada en cobertura baja, la comprobación de espacio/apertura en esquinas y la entrega de pose al render.
  - Extiende la cobertura policial a otros NPCs ambientales armados con pasos visibles al acercarse y comprobaciones al perder el blanco.
- **Daño, rifles a distancia, recuperación y audio de caídas de CJ (`Adapter`, `CombatHealth`, `CombatRuntime`, `RifleCollisionRegions`, `Recovery`, `NpcRecoverySpace`, `CjRecovery*`, `PlayerHighFall`, `CjHighFall`, `BodyAudio`, `IvSampleAudio`, `CjGroundAudioGate`, `InjuredMove`)**:
  - Los rifles `33`/`34` activan ragdoll a distancia con regiones de colisión locales alrededor de la víctima confirmada.
  - Corrige el daño de bastón/porra a la cabeza de CJ (elimina el multiplicador crítico ×4 de NPCs).
  - La levantada de CJ ya no se reinicia por disparos no letales durante el ragdoll, acorta esperas/mezclas de recuperación y comprueba espacio alrededor de paredes o vehículos para NPCs.
  - Umbral de caída de CJ en `4,5 m` (`[Player] HighFallMinDropMeters`), ajuste de la variante de cojera del 50 % al 60 % y 12 muestras de impacto corporal de GTA IV (`cj-ground-audio/body01..13.wav`).
- **Generación 100 % automática desde la ISO de Xbox 360 (`sa-delirium-bootstrap`, `Solo`, `PortableCore`)**:
  - `sa-delirium-bootstrap` ahora genera automáticamente en el equipo del usuario `DriverPoses.bin`, `NPCGait.bin`, `NPCMovers.dat` y los 12 WAV de `cj-ground-audio` directamente desde su ISO de GTA IV para Xbox 360 (USA y Europa/Asia) y su copia de GTA SA, tanto en instalación completa como en *Solo Delirium*, e incluye `IVDriverPose.asi` en ambos modos.
  - Mantiene todos los arreglos de estabilidad e instalador de `0.5.1` (guarda `CBuilding` en `frictionStructureOther`, validación tras `VehicleDamage`, sangre solo con daño positivo, diagnóstico de integridad con SHA-256 y protección `E_NOTIMPL` en accesos directos).

### English

- **15-Profile GTA IV NPC Locomotion & Armed Stride (`NpcGait*`, `NpcStride`, `NpcFootwearHost`, `ArmedStep*`, `VisibleLegPose`)**:
  - 115 retargeted GTA IV locomotion clips across 21 joints and 15 pedestrian profiles (civilians, elders, heavy peds, gangs, cops, SWAT, business, etc.) with model-aware stride curves, turns, stops, and sole height alignment.
  - 8-way armed walk/run strafing for CJ and NPCs, re-applying leg pose before render to prevent frozen legs while keeping torso, hands, and aim intact.
- **GTA IV Vehicle Drive-By Poses & Weapon Sync (`IVDriverPose.asi`, `DriveByAim`, `DriveByControls`, `MountedDraw`, `MountedWeaponModel`, `MountedWeaponPass`, `VehicleWeaponSelection`, `PlayerPoseDraw`)**:
  - New static-CRT (`/MT`) x86 helper `IVDriverPose.asi` using 24 seat profiles and 96 GTA IV aim/entry/exit/throw clips across cars and motorcycles.
  - Fixes torso-clipping arms, keeps the equipped weapon visible in hand (including with SilentPatch), supports inventory cycling across pistols/SMGs/grenade/molotov, and prevents duplicate native firing.
- **CJ Locomotion & Native Knife Stealth (`sa-combat/locomotion*`, `NikoMovement`, `NativeStealth`)**:
  - Fixes the crash/panic when sprinting from crouch (`supported_ratio` cap for crouched clips + fallible `gait_step`) while keeping the crisp ~0.25 s `wstart` fade-out and immediate `wstop`.
  - Smooth joint-local handoff (`nikoHandoffPose`) returning from aim or cover while respecting keyboard walk and gentle analog stick input.
  - Yields IV movement, shoves, and physics cleanly during SA's paired knife stealth kill (standing and crouched).
- **Improved Cover for CJ & Armed NPCs (`PlayerCover`, `CoverPosture`, `CoverSkin`, `PoliceCover`)**:
  - Fixes crouched low-cover posture, corner clearance checks, and render handoff for CJ, and extends police cover to other armed ambient NPCs with visible approach steps.
- **Long-Range Rifles, Recovery, Baton Fix & CJ Ground Audio (`Adapter`, `CombatHealth`, `CombatRuntime`, `RifleCollisionRegions`, `Recovery`, `NpcRecoverySpace`, `CjRecovery*`, `PlayerHighFall`, `CjHighFall`, `BodyAudio`, `IvSampleAudio`, `CjGroundAudioGate`, `InjuredMove`)**:
  - Rifles `33`/`34` trigger distant ragdolls with local collision regions around confirmed hits; removes the NPC 4× head critical multiplier on CJ from nightsticks/batons; prevents non-lethal shots from resetting CJ's getup; improves NPC obstacle-aware getup around walls/cars; sets CJ's default high-fall threshold to `4.5 m` (`[Player] HighFallMinDropMeters`), raises limp variant selection to 60%, and adds 12 GTA IV `BODYFALL_BODY` ground-impact samples.
- **Automatic Xbox 360 ISO Generation & Full/Solo Installer Support (`sa-delirium-bootstrap`, `Solo`, `PortableCore`)**:
  - `sa-delirium-bootstrap` generates `DriverPoses.bin`, `NPCGait.bin`, `NPCMovers.dat`, and all 12 `cj-ground-audio` WAVs locally from the user's GTA IV Xbox 360 ISO (USA & Europe/Asia) and SA files, bundling `IVDriverPose.asi` in both Full and *Solo Delirium* modes while retaining all `0.5.1` safety and installer diagnostic fixes.

## 0.5.1 · pulido de inicio/parada con Alt, mando al soltar apuntado, seguridad de memoria/punteros y diagnóstico del instalador / Alt walk start/stop polish, gamepad aim-release walk, pointer/memory safety & installer diagnostics

### Español

- **Transición `wstart` → caminar y frenado inmediato `wstop` (`sa-combat/locomotion_original` / `locomotion`)**: al terminar `wstart`/`runstart`, el desvanecimiento pasa de `-movestop::BLEND_OUT` (`1,0 s`) a `-movestart::BLEND_IN` (`~0,25 s`) y excluye el `wstart` ya acabado en fade-out de `playing`, evitando mantener la pose final congelada un segundo completo sobre `move_player/walk` y permitiendo que soltar caminar dispare `wstop` de inmediato en lugar de cortar en seco.
- **Lectura única de teclado/mando por fotograma y caminata con mando tras soltar apuntado (`NikoMovement`)**: `nikoKeyboardWalk` (`Caps Lock` / `Alt`) y `nikoPadActive` (`XInputGetState`) se consultan como máximo una vez por fotograma (`wasAimSuspended` o `nikoFrame != frame`), y al soltar el apuntado empujando suavemente el stick analógico (`<= 0.65`) se reanuda en caminata en vez de forzar trote (`>= 1.85`).
- **Guarda de tipo en `frictionStructureOther` (`Adapter`)**: se comprueba primero el tipo de entidad (`kind = other+0x36 & 7`) antes de leer campos de `CPhysical` (`>= 0x38`), evitando lecturas fuera de límites sobre `CBuilding` (`0x38` bytes), impidiendo clasificar un `CPed` como estructura y comprobando las banderas reales (`bIsStatic` en `0x1C & 4` y `bDisableMoveForce | bInfiniteMass | bDisableCollisionForce` en `0x40 & 0x64`).
- **Guarda de validez tras `CVehicle::VehicleDamage` y sangre solo con daño positivo (`CombatRuntime`)**: se comprueba `vehicleFromRef(vehRef) == entity` justo después de invocar `vt[0xE0/4]` para no desreferenciar vehículos destruidos/invalidados, y `combatFinish` exige `before > 0.f && harm > 0.f` para no generar sangre en objetivos inmunes al cuerpo a cuerpo con daño `0`.
- **Consistencia de protección de misiones en gravedad (`GravityControl`)**: unificada la comprobación de misión en interiores con `authoredMissionProtected()`.
- **Diagnóstico de integridad y tolerancia `E_NOTIMPL` en accesos directos del instalador (`PortableSetup` / `Standalone`)**:
  - `PortableSetup.Helpers` reinicia `archive.Position = 0` antes de calcular el SHA-256, sustituye `"El contenido del instalador ha cambiado."` por un mensaje claro de archivo incompleto/dañado (recomendando redescarga directa desde GitHub y verificación con `SHA256SUMS.txt`) e incluye el tamaño y los hashes real/esperado de `portable-tools.zip` en el diagnóstico técnico.
  - `Standalone.Install` y `Standalone.Restore` protegen la lectura de un `SA Delirium.lnk` preexistente frente a `E_NOTIMPL` (`0x80004001`) en sistemas donde `WScript.Shell` no está disponible, evitando revertir una instalación válida o bloquear la restauración.

### English

- **Crisper `wstart` → walk fade-out and immediate `wstop` (`sa-combat/locomotion_original` / `locomotion`)**: switches completed `wstart`/`runstart` fade-out from `-movestop::BLEND_OUT` (`1.0 s`) to `-movestart::BLEND_IN` (`~0.25 s`) and excludes completed fading-out start clips from `playing`, eliminating the 1-second frozen-pose blend over `move_player/walk` and allowing immediate `wstop` transitions instead of abrupt halts when releasing walk.
- **Single per-frame input poll & analog gamepad walk on aim release (`NikoMovement`)**: reads `nikoPadActive` and stateful `nikoKeyboardWalk` (`Caps Lock` / `Alt`) at most once per frame (`wasAimSuspended` or `nikoFrame != frame`), and respects gentle analog stick tilt (`<= 0.65`) when releasing aim instead of forcing a jog (`>= 1.85`).
- **Safe entity-type check in `frictionStructureOther` (`Adapter`)**: checks entity type (`other+0x36 & 7`) before reading `CPhysical` fields (`>= 0x38`), preventing out-of-bounds reads on `0x38`-byte `CBuilding` instances, never misclassifying `CPed` as a structure, and checking `bIsStatic` (`0x1C & 4`) plus `bDisableMoveForce | bInfiniteMass | bDisableCollisionForce` (`0x40 & 0x64`).
- **Post-`VehicleDamage` pointer guard & positive-damage blood check (`CombatRuntime`)**: validates `vehicleFromRef(vehRef) == entity` right after invoking `vt[0xE0/4]` (`CVehicle::VehicleDamage`) and requires `before > 0.f && harm > 0.f` before spawning melee blood particles so immune/zero-damage hits do not bleed.
- **Consistent interior mission protection (`GravityControl`)**: aligns interior mission gating with `authoredMissionProtected()`.
- **Actionable installer integrity diagnostics & `E_NOTIMPL` shortcut-reader safety (`PortableSetup` / `Standalone`)**:
  - `PortableSetup.Helpers` resets `archive.Position = 0` before hashing, replaces `"El contenido del instalador ha cambiado."` with a clear damaged/incomplete download message pointing to GitHub + `SHA256SUMS.txt`, and records the byte length plus actual vs expected `portable-tools.zip` SHA-256 in the diagnostic details.
  - `Standalone.Install` and `Standalone.Restore` guard pre-existing `SA Delirium.lnk` target reads against `WScript.Shell` `E_NOTIMPL` (`0x80004001`) so completed installs are not rolled back and restores still succeed.

## 0.5.0 · solución a fondo de crashes al golpear autos, GYM/interiores, techo de vehículos, inicio con Alt, fricción en paredes, contraataques, apuntado a 1 mano y coches bajos / deep fix for vehicle-strike crashes, gyms/interiors, car climbing, Alt walk start, wall friction, counterattacks, 1-handed aim jog & low cars

### Español

- **Golpes a vehículos sin crashes (`R6025`/`CReference`) y con daño, audio y abolladura reales (`CombatRuntime` / `VehicleDeformation`)**:
  - Se libera de forma segura la referencia previa en `lastDamager` (`0x50C`) mediante `CleanUpOldReference` (`0x571A00`) antes de registrar el nuevo atacante (`0x571B70`) y se validan `occRef` y las matrices de conductor (`0x460`) y los 8 pasajeros (`0x464`) antes de emitir eventos.
  - Se añaden trazos de respaldo desde hombro y pecho a corta distancia y se amplía el alcance desarmado; se escala `damageIntensity` según la masa del vehículo (`>= 160.0` y proporción por encima del umbral nativo `0.35 * mass`) para aplicar daño y deformación de paneles (`1..10, 19`) más abolladura de malla en tiempo real (`dentMap`), y se corrige la llamada a `0x4E2BB0` (`0x42`/`0x43`) para reproducir el sonido metálico/superficie del coche.
- **Movimiento GTA IV, combate y físicas dentro de gimnasios e interiores (`ABI` / `Adapter` / `CombatRuntime` / `Prepare-VehicleLabRuntime` y módulos asociados)**: se sustituye el bloqueo directo de `scriptMissionActive()` por `authoredMissionProtected()` (`scriptMissionActive() && (area() == 0 || onMission())`) en todas las puertas de juego, eliminando el parche parcial `keepNiko` para que `GYMLS`/interiores mantengan las animaciones y el desplazamiento IV sin congelarse.
- **Subir y permanecer sobre coches (`ragdoll_external_ground`)**: en `pair`, se omiten colisiones externas no peatonales sobre superficies transitables (`n.z > 0.35`, incluidos parabrisas inclinados, capó y techo) para evitar que CJ sea empujado al suelo al saltar encima de un auto.
- **Rozamiento lateral con paredes o estructuras (`ragdoll_animated`)**: en `sweep_shapes_translation`, durante movimiento horizontal (`wish.z == 0.`) las normales de muros inclinados o con costura entre triángulos (`n.z.abs() < 0.7`) se proyectan al plano horizontal para no chocar con los planos verticales y conservar la velocidad tangencial.
- **Inicio al caminar con `Alt` desde parado sin tirón (`sa-combat/locomotion` / `locomotion_original`)**: al terminar `wstart`/`runstart`, la transición pasa a desvanecimiento suave (`-movestop::BLEND_OUT`) manteniendo el suavizado `max_dv` y evitando la caída de velocidad en el último paso del clip tanto a 60 Hz como a altas tasas de fotogramas (144 Hz).
- **Contraataques con audio y sangre a la altura correcta (`CombatRuntime`)**: los contraataques tras esquivar (`counter`), patadas y armas cuerpo a cuerpo entran en el filtro de impacto, reproducen `bat.wav` en armas contundentes y generan partículas de sangre desde el punto de contacto real `hit[4..6]` (altura del pecho).
- **Continuidad de trote al soltar apuntado con armas de una mano (`NikoMovement`)**: se registra `nikoPreAimRatio` y se conserva `nikoAimSuspended` a través de estados transitorios a pie, leyendo el pad antes de reanudar IV para seguir trotando suavemente con pistolas/subfusiles/recortada (`22, 23, 24, 26, 28, 29, 32`) sin repetir clips de inicio.
- **Coches bajos sin salto al entrar ni pérdida de giro en curva (`vehicle_lab`)**: captura de `rest_bias` al estacionar nivelado, tope progresivo de suspensión (`bump_stop_compression`), escalado `low_scale`, mayor transferencia antivuelco en ejes y recuperación de balanceo para deportivos bajos y *lowriders*.
- **Unificación de firma para compilación shipping (`Adapter` / `CombatRuntime`)**: corregida la sobrecarga ambigua de `combatClear(bool clearNiko=true)` entre la declaración adelantada y su definición al compilar `DeliriumSA.asi`.

### English

- **Crash-free, real-damage vehicle melee strikes (`CombatRuntime` / `VehicleDeformation`)**:
  - Calls `CleanUpOldReference` (`0x571A00`) on `lastDamager` (`0x50C`) before assigning `0x571B70` and validates `occRef` + ped matrices across the driver (`0x460`) and all 8 passenger slots (`0x464`) to eliminate `R6025` / `CReference` corruption crashes.
  - Adds shoulder/chest close-range backup rays and extended unarmed reach; scales `damageIntensity` with vehicle mass (`>= 160.0` and above `0.35 * mass`) with inferred vehicle components (`1..10, 19`) and real-time `dentMap` mesh deformation; and passes valid strike event/surface arguments to `0x4E2BB0` (`0x42`/`0x43`) for metallic/car collision SFX.
- **Consistent GTA IV locomotion, combat, and physics inside gyms and interiors (`ABI` / `Adapter` / `CombatRuntime` / `Prepare-VehicleLabRuntime` & bridges)**: replaces direct `scriptMissionActive()` gameplay gates with `authoredMissionProtected()` (`scriptMissionActive() && (area() == 0 || onMission())`), removing the `keepNiko` bandaid so `GYMLS` and interiors keep full IV animations and displacement without stalling.
- **Jumping onto vehicles (`ragdoll_external_ground`)**: skips non-animated-ped external pair forces over walkable surfaces (`n.z > 0.35`, covering hoods, roofs, and sloped windshields) so CJ stays cleanly atop cars.
- **No wall-grazing slowdown (`ragdoll_animated`)**: projects sloped or seamed wall normals (`n.z.abs() < 0.7`) onto the horizontal plane during horizontal sweeps (`wish.z == 0.`) in `sweep_shapes_translation` so tangential speed stays preserved instead of collapsing.
- **Twitch-free `Alt` walk start at 60 Hz & high FPS (`sa-combat/locomotion` / `locomotion_original`)**: fades out completed `wstart`/`runstart` clips via `-movestop::BLEND_OUT` with `max_dv` slew smoothing and fixes final-step clamping at both 60 Hz and 144 Hz.
- **Counterattack impact audio & chest-level blood (`CombatRuntime`)**: restores impact audio (`bat.wav` for clubs) and native blood FX at the actual `hit[4..6]` chest contact point for dodge counterattacks, kicks, and melee weapons.
- **1-handed gun aim-release jog continuity (`NikoMovement`)**: preserves `nikoPreAimRatio` and `nikoAimSuspended` across transient on-foot states and reads pad walk/push inputs before resuming IV locomotion on 1-handed firearms (`22, 23, 24, 26, 28, 29, 32`).
- **Low-car ride height, hop-free entry & cornering (`vehicle_lab`)**: captures stationary `rest_bias` on level spawns, tightens progressive bump-stop compression, adds `low_scale` stiffness, strengthens axle anti-roll transfer, and boosts roll recovery beyond `0.020` for low sports cars and lowriders (*Remington 534*).
- **Shipping compile signature fix (`Adapter` / `CombatRuntime`)**: resolves the ambiguous `combatClear(bool clearNiko=true)` forward-declaration overload when compiling `DeliriumSA.asi` with `--shipping`.

## 0.4.9 · continuidad al soltar apuntado, daño/audio al golpear autos, altura/giro de suspensión y movimiento IV en interiores / aim-release continuity, vehicle punch damage & audio, SA ride-height/cornering & interior IV locomotion

### Español

- **Continuidad de trote al dejar de apuntar (`NikoMovement` / `sa-combat/locomotion` / `delirium-sdk/locomotion`)**: al apuntar en movimiento con armas de una mano (`9mm`, `Tec-9`, `Uzi`, escopeta recortada, etc.) o soltar la mira mientras CJ sigue trotando, `NikoMovement` conserva la velocidad nativa (`suspend_moving` por `dt <= -2.5`) y mezcla la pose final del apuntado durante 120 ms sin repetir la animación de arrancar desde parado (`wstart`/`runstart`).
- **Golpear vehículos con daño real, audio nativo y sin crash `R6025` (`CombatRuntime`)**: al golpear un coche sin armas cuerpo a cuerpo o con un arma cuerpo a cuerpo, se reemplaza el audio humano de impacto por `CAEPedAudioEntity::AddAudioEvent` (`0x4E2BB0`) con la superficie del vehículo (`SURFACE_CAR` / superficie de `ColPoint`), se aplica daño real mediante `CVehicle::VehicleDamage` (`vtable[0xE0/4]`), chispas/partículas (`0x49F670`), registro de crimen (`0x532010`) y notificación concreta `CEventVehicleDamageWeapon` (`0x61C2E0` / `0x4AB420` / `0x61C310`) al conductor y pasajeros en lugar de la clase abstracta `CEventVehicleDamage`, evitando el error `R6025 - pure virtual function call`.
- **Altura real de suspensión, entrada sin saltos y estabilidad de chasis al girar (`vehicle_lab` / `VehicleLab`)**:
  - Actualizado `sa_vehicle_lab_abi` a `11` para recibir `m_fSuspensionForceLevel` (`handling+0xAC`) y `m_fSuspensionBiasBetweenFrontAndRear` (`handling+0xC0`), sustituyendo el reposo fijo `0.36 * travel` por el equilibrio estático SA por rueda (`sa_rest_fraction`) y eliminando tanto la caída de vehículos bajos como el pequeño salto al subirse.
  - Se evita ablandar los muelles en recorridos largos de SA (`stiffness_scale` con mínimo `1.0`), se añade tope progresivo de compresión (`bump_stop_compression`), se refuerza el acoplamiento antivuelco por eje (`0.25`) y se adelanta `excess_lean` (`0.035 rad`) para que deportivos y *lowriders* no rocen el suelo ni pierdan giro al doblar.
- **Movimiento GTA IV en gimnasios e interiores sin bloqueos (`Prepare-VehicleLabRuntime` / `NikoMovement` / `OriginalPedMotorBranch` / `Adapter`)**: `nikoMovementAllowed()` mantiene activa la locomoción IV en interiores (`area() != 0`) aunque el juego ejecute un script de interior marcado como misión (`GYMLS`, `GYMSF`, `GYMLV`, `SHRANGE`, casinos), `combatClear(!keepNiko)` conserva el estado de Niko dentro del mismo interior y `original_ped_motor_branch::allow` evita bloquear `0x5E1E4C` cuando `OriginalPedMotorImpulse` no está activo para que CJ no quede trotando en el sitio con animaciones de SA.
- **Subir y saltar encima de vehículos sin empuje lateral (`ragdoll_external_ground` / `ragdoll_external_world`)**: se aplican las reglas de GTA IV a las sondas inferior y de piernas (`child == 1` y `child == 3`) también cuando el ped animado aún no tiene `ground_claimed`, permitiendo que CJ salte encima de un coche sin ser expulsado hasta tocar el suelo sólido.
- **Inicio suave al caminar con `Alt` sostenido desde parado (`sa-combat/locomotion_original` / `locomotion`)**: al completar `wstart`/`runstart` se descuenta el avance ya consumido (`(s.phase - advance).max(0.0)`) y se limita la aceleración por fotograma al pasar a caminata/trote continuo para evitar el tirón de un fotograma en la animación inicial.
- **Sin frenado por fricción al rozar paredes o estructuras (`Adapter`)**: los hooks en `CPhysical::ApplyFriction` (`0x5454C0`) y `FrictionPair` (`0x545980`) suprimen la fricción tangencial contra paredes y estructuras estáticas (`|normal.z| < 0.7`) mientras el motor a pie IV está activo, conservando la tracción sobre el suelo y techos de coches.
- **Audio de impacto y partículas de sangre en contraataques (`CombatRuntime`)**: los contraataques tras esquivar (`hit[15] != 0`) también reproducen `playIvMeleeImpact` y generan salpicaduras de sangre nativas mediante `Fx_c::AddBlood` (`0x49EB00`).

### English

- **Jog/run continuity on firearm aim release (`NikoMovement` / `sa-combat/locomotion` / `delirium-sdk/locomotion`)**: releasing firearm aim (`22..38`, including one-handed weapons like `9mm`, `Tec-9`, `Uzi`, and sawn-off shotgun) while moving now seeds IV locomotion with CJ's native speed (`Locomotion::suspend_moving` via `dt <= -2.5`) and blends smoothly over 120 ms from the ending aim pose without replaying a start-from-still animation (`wstart`/`runstart`).
- **Vehicle melee strikes with native impact sound, real damage, and no `R6025` crash (`CombatRuntime`)**: `combatCheckPlayerVehicleStrike` replaces the human-flesh audio trigger with `CAEPedAudioEntity::AddAudioEvent` (`0x4E2BB0`, `ColPoint` vehicle surface / `SURFACE_CAR`), inflicts real deformation and health loss through `CVehicle::VehicleDamage` (`vtable[0xE0/4]`), spawns native impact FX (`0x49F670`), reports the strike (`0x532010`), and constructs concrete `CEventVehicleDamageWeapon` (`0x61C2E0` / `0x61C310`) instead of abstract `CEventVehicleDamage` to eliminate `R6025 - pure virtual function call` crashes.
- **SA ride height, hop-free parked entry, and anti-scrape cornering (`vehicle_lab` / `VehicleLab`)**:
  - Bumps `sa_vehicle_lab_abi` to `11` to pass SA `m_fSuspensionForceLevel` and `m_fSuspensionBiasBetweenFrontAndRear`, computing per-wheel SA static equilibrium (`sa_rest_fraction`) instead of a hardcoded `0.36 * travel` so low cars neither sit too low nor hop when entered.
  - Clamps `stiffness_scale` to `>= 1.0`, adds progressive bump-stop compression (`bump_stop_compression`), strengthens axle anti-roll (`0.25`), and lowers the `excess_lean` threshold (`0.035 rad`) so low-clearance sports cars and lowriders corner cleanly without scraping their underside `COL` on asphalt.
- **GTA IV on-foot movement inside gyms and interiors without stalls (`Prepare-VehicleLabRuntime` / `NikoMovement` / `OriginalPedMotorBranch` / `Adapter`)**: keeps IV locomotion active inside interiors (`area() != 0`, including Ganton/SF/LV Gyms, Ammu-Nation ranges, and casinos that set `scriptMissionActive()`) while preserving `ProtectMissions` outdoors, preserves Niko state across same-interior mission boundaries (`combatClear(!keepNiko)`), and gates `0x5E1E4C` via `original_ped_motor_branch::allow` so CJ never reverts to SA animations or gets stuck jogging in place.
- **Jump onto vehicles cleanly (`ragdoll_external_ground` / `ragdoll_external_world`)**: extends the IV `child == 1` ground-probe and `child == 3` leg-probe filtering to `animated_ped` agents even before `ground_claimed`, stopping cars from shoving CJ off when jumping onto roofs or hoods.
- **Twitch-free walk start when holding `Alt` from idle (`sa-combat/locomotion_original` / `locomotion`)**: fixes `time: (s.phase - advance).max(0.0)` when completing `wstart`/`runstart` and smooths post-start per-frame speed deltas (`max_dv`), eliminating the single-frame hitch entering continuous walk.
- **No wall-grazing slowdown (`Adapter`)**: hooks `CPhysical::ApplyFriction` (`0x5454C0`) and `FrictionPair` (`0x545980`) to suppress lateral wall/structure Coulomb friction (`|normal.z| < 0.7`) during active IV on-foot movement while keeping ground and car-roof traction intact.
- **Counterattack hit audio & blood particles (`CombatRuntime`)**: includes dodge-counter hits (`hit[15] != 0`) in `playIvMeleeImpact` and invokes `Fx_c::AddBlood` (`0x49EB00`) when damage lands (`before > 0`).

## 0.4.8 · puñetazos sin objetivo/a vehículos, salto continuo y levantamiento Dive/Brace / unarmed & vehicle punches, jump continuity & Dive/Brace getup

### Español

- **Puñetazos desarmados al aire y contra vehículos (`CombatRuntime` / `sa-combat/runtime`)**: al pulsar golpe sin armas cerca de ningún NPC o al golpear un coche, CJ vuelve a ejecutar el movimiento y la animación de golpear desarmado en lugar de solo cerrar los puños.
- **Continuidad de trote/carrera tras saltar (`NikoMovement` / `sa-combat/locomotion` / `delirium-sdk/locomotion`)**: durante un salto puro sin ragdoll ni caída física, la locomoción IV suspende el ritmo sin reiniciarse a parado (`suspend_jump`) y al aterrizar reanuda el trote o carrera suavemente sin repetir la animación de arranque desde cero.
- **Aterrizaje y levantamiento limpio tras Dive/Brace (`Recovery` / `NativeIdlePose` / `Adapter` / `RecoveryMath`)**:
  - Al iniciar el Dive/Brace desde un salto, `retireNativePrimaryTasks` aborta la tarea nativa de salto con prioridad inmediata (`ABORT_PRIORITY_IMMEDIATE = 2`), limpia `bIsInTheAir`/`bIsLanding` (`0x46D`) y retira clips residuales de salto o carrera (`RUN_CIVI`) dejando la base en reposo (`IDLE_STANCE`).
  - Se mide y aplica la holgura real al suelo de pie (`1.00 m` frente al offset más alto del rig) en recuperaciones aéreas para evitar la pequeña caída residual y el destello de animación de salto al levantarse tras `GETUP_0`/`GETUP_FRONT`.

### English

- **Unarmed punches without an NPC target and against vehicles (`CombatRuntime` / `sa-combat/runtime`)**: pressing unarmed melee attack with no nearby ped or when punching a vehicle now plays CJ's full punch motion and animation instead of only clenching his fists.
- **Continuous jog/run resumption after jumping (`NikoMovement` / `sa-combat/locomotion` / `delirium-sdk/locomotion`)**: pure jumps suspend IV locomotion pace (`suspend_jump`) instead of cancelling back to idle, blending cleanly back into running/jogging upon landing without replaying a start-from-still animation.
- **Clean landing and stand-up transition after Dive/Brace (`Recovery` / `NativeIdlePose` / `Adapter` / `RecoveryMath`)**:
  - Aborts native jump/in-air tasks with immediate priority (`ABORT_PRIORITY_IMMEDIATE = 2`), clears `bIsInTheAir`/`bIsLanding` (`0x46D`) and stale jump/run clips (`RUN_CIVI`), and normalizes the base clump layer to idle stance when entering Dive/HighFall and completing get-up.
  - Captures/applies SA's true standing ground clearance (`1.00 m` vs the taller rig sole offset) on mid-air recoveries so CJ no longer drops slightly or flashes a jump-like pose right as `GETUP_0`/`GETUP_FRONT` finishes.

## 0.4.7 · apuntado GTA IV, cobertura, caminar, físicas y correcciones / GTA IV aiming, cover, walk, physics & fixes

### Español

- **Apuntado completo de GTA IV integrado y retirada de *Advanced Aiming Mod* (`IvAim` / `Aiming` / `DriveByAim`)**: se retira *Advanced Aiming Mod* del instalador completo en favor del apuntado integrado de GTA IV a pie y desde vehículos (`IvAim=1` en `DeliriumSA.ini`), con cámara sobre el hombro, recorte suave contra paredes, retícula sincronizada y zoom mediante `R3` o botón central del ratón (`MMB`), además de la rueda (`AimWheelZoom=1`).
- **Mirar atrás con `R3` en vehículos y modo Lowrider (`DriveByControls` / `DeliriumSA.ini`)**: pulsar `R3` en mando (o `MMB` en ratón) mira atrás al conducir en lugar de `L1 + R1`. En vehículos con hidráulica (*lowriders*), pulsar **`G`** en teclado o **`D-Pad Arriba`** en mando XInput (`LowriderCameraToggleKey=71`, `LowriderCameraTogglePadButton=1`) alterna entre la hidráulica activa y el modo de cámara normal para usar `R3` al mirar atrás.
- **Cobertura de CJ y policías (`PlayerCover` / `CombatRuntime` / `PoliceCover`)**: se rehacen el acceso/aproximación a cobertura y la sincronización entre cámara, orientación y pose de disparo en esquinas bajas y altas (aplicando el rumbo de la cámara al ped y actualizando el esqueleto antes y después de `CCam::Process_AimWeapon`), se bloquea el disparo mientras está cubierto sin asomarse y se mejora la orientación de policías al asomarse desde bordes.
- **Caminar con CJ y postura con armas (`NikoMovement` / `DeliriumSA.ini`)**: se añade tecla dedicada para caminar en `DeliriumSA.ini` (`Alt` mantenido mediante `WalkKey=18` o alternar con `Bloq Mayús` mediante `WalkToggleKey=20` y `DefaultWalk=0`) conservando trote y sprint, junto con caché de postura calma para evitar que se rompa la parte superior de CJ al portar armas de dos manos.
- **Disparo en ragdoll y armas soltadas (`RagdollFire` / `FirearmBridge` / `DroppedWeapons` / `MeleeDisarm`)**: permite disparar manualmente durante el ragdoll en la dirección física del arma con retroceso aplicado sobre brazo, antebrazo y torso; restringe el disparo accidental al caer al suelo solo a armas arrancadas de la mano con impacto suficiente; y registra a los agentes desarmados para su respuesta posterior.
- **Policías desarmados (`PoliceResponse`)**: los agentes sin arma de fuego o desarmados en combate evitan responder a tiros frente a amenazas armadas y recurren a la huida nativa.
- **Daño en ragdoll, eyección por parabrisas, suelos y sangre (`PlayerHighFall` / `WindscreenEjection` / `Adapter` / `BloodDecals`)**: se reduce a un cuarto (`0.25x`) el daño de caída/ragdoll para CJ, sube un 20 % el umbral de velocidad para salir despedido por el parabrisas (`windscreenPlayerDv` de `18.0` a `21.6`), se refuerza `rejectGroundTunnel` frente a suelos y techos bajos para evitar atraviesos o muertes sin motivo, y `BloodDecals` fija al suelo las manchas estáticas y permanentes de sangre para que no acompañen al cuerpo al moverse.
- **Estabilidad de conducción IV (`vehicle_lab`)**: la simulación por rueda respeta los bits de `handling` de GTA IV (`CAP_SLIDE`, `NO_LOCK`, `CAP_SPIN`, `CLAMP_SLIP` y `BURNOUT`) y el guiado direccional a alta velocidad, estabilizando deportivos rápidos como el *Hotring* al acelerar en recta.

### English

- **Built-in GTA IV aiming & *Advanced Aiming Mod* removal (`IvAim` / `Aiming` / `DriveByAim`)**: removes *Advanced Aiming Mod* from the full installer in favor of built-in GTA IV over-the-shoulder aiming both on foot and from vehicles (`IvAim=1` in `DeliriumSA.ini`), complete with wall clipping prevention, synced reticle, `R3` / `MMB` toggleable aim zoom, and scroll wheel zoom (`AimWheelZoom=1`).
- **`R3` vehicle look-behind & lowrider hydraulics toggle (`DriveByControls` / `DeliriumSA.ini`)**: pressing `R3` on controller (or `MMB` on mouse) looks behind while driving instead of `L1 + R1`. On vehicles equipped with hydraulics (*lowriders*), press **`G`** on keyboard or **`D-Pad Up`** on XInput gamepads (`LowriderCameraToggleKey=71`, `LowriderCameraTogglePadButton=1`) to switch between active hydraulics and normal camera mode (`R3` look-behind).
- **CJ & police cover (`PlayerCover` / `CombatRuntime` / `PoliceCover`)**: reworks cover approach/pathing and syncs camera aim heading and animation pose when aiming out of low and high corners (writing aiming yaw and updating bone frames around `CCam::Process_AimWeapon`), blocks accidental gunfire while hidden behind cover, and improves police peek orientation along cover edges.
- **Walking toggle & armed upper-body stability (`NikoMovement` / `DeliriumSA.ini`)**: adds dedicated walk controls (`Alt` hold via `WalkKey=18` or `Caps Lock` toggle via `WalkToggleKey=20` with `DefaultWalk=0`) while preserving jog/sprint, plus calm two-handed weapon local pose caching during slow movement.
- **Ragdoll gunfire & dropped-weapon misfires (`RagdollFire` / `FirearmBridge` / `DroppedWeapons` / `MeleeDisarm`)**: lets CJ fire along the weapon's physical direction while in ragdoll with recoil impulse applied to hand, forearm, and chest; restricts accidental floor misfires strictly to firearms knocked out of a ped's hand on heavy impact; and tracks disarmed cops.
- **Unarmed police response (`PoliceResponse`)**: disarmed officers and cops lacking a firearm fall back to native flee behavior instead of engaging with gunfire.
- **Ragdoll fall damage, windscreen ejection, ground anti-tunneling & fixed blood decals (`PlayerHighFall` / `WindscreenEjection` / `Adapter` / `BloodDecals`)**: cuts CJ's ragdoll fall damage to one quarter (`0.25x`), increases CJ's windscreen ejection delta-v threshold by 20% (`18.0` to `21.6`), strengthens `rejectGroundTunnel` against floor penetration and low ceilings, and anchors static/permanent blood splatters to the ground so they no longer slide with moving peds.
- **GTA IV vehicle stability (`vehicle_lab`)**: applies GTA IV tyre and handling flags (`CAP_SLIDE`, `NO_LOCK`, `CAP_SPIN`, `CLAMP_SLIP`, `BURNOUT`) plus high-speed steering behavior so sports cars such as the *Hotring* accelerate straight cleanly.

## 0.4.6 · cobertura, cámara, anatomía de CJ, motor e instalador / cover, camera, CJ anatomy, engine & installer

### Español

- **Anatomía de hombros y brazos de CJ (`torso.dff` / `sa-combat` / `Rig`)**: en las mallas de ropa y torso de CJ (`player.img`), los huesos `301` y `302` pesan la mitad superior del bíceps y deltoides y comparten el *socket* de `UpperArm` (`22`/`32`), mientras que en `ped.ifp` contienen una rotación estática pectoral de NPC girada 96° hacia delante que partía el brazo de CJ en zig-zag. Ahora `301`/`302` se enlazan directamente al brazo superior (`22`/`32`) tal como se exportaron en `torso.dff`, se incluyen en las máscaras de mezcla de tren superior, dejan de sobrescribirse con la pista pectoral de `ped.ifp` y omiten la sobre-elevación de reposo en las clavículas (`21`/`31`).
- **Controles de teclado para cobertura, movimiento IV, combate y gravedad (`PlayerCover` / `NikoMovement` / `CombatRuntime` / `GravityControl`)**:
  - **Cubrirse en la tecla `B`** (`CoverKey=B` configurable en `DeliriumSA.ini`, manteniendo `R1`/`RB` en mando XInput): se elimina el respaldo sobre `CPad::RightShoulder1` (`pad + 0xC`), que en teclado/ratón de PC coincidía con el Click Derecho (Apuntar) y provocaba que CJ entrase o saliese de cobertura al intentar apuntar.
  - **Movimiento a pie de GTA IV con teclado**: `W, A, S, D` trota/corre normal, `Alt + W, A, S, D` camina despacio y mantener `Espacio` esprinta directamente sin necesidad de aporrearlo (conservando el comportamiento analógico original cuando se usa mando XInput).
  - **Combate cuerpo a cuerpo y gravedad en teclado**: al fijar objetivo con Click Derecho se admiten también los atajos de GTA IV PC (`R` para ataque secundario/contraataque, `Q` para patada y `Alt` para esquivar/bloquear, además de `F`/`Enter`, `Espacio` y `Shift`), y mantener **`K`** durante 0,6 s alterna la gravedad reducida en teclado (además de `L3 + R3` en mando).
- **Cámara en cobertura independiente del apuntado en vehículo**: desactivar el apuntado desde vehículos (`DriveByShoulderAim=0`) ya no apaga la corrección de cámara al cubrirse a pie.
- **Encuadre al cambiar de hombro en cobertura**: al apuntar por el hombro contrario (esquina alta izquierda), se recupera el punto nativo de cámara previo al recorte de colisión de San Andreas y se aplica el barrido del lado activo. CJ ya no queda fuera del encuadre.
- **Continuidad en paredes bajas y cajas**: CJ puede desplazarse y cambiar de dirección a lo largo de coberturas bajas con esferas de colisión redondeadas (`woodenbox1224`) sin atascarse a mitad del recorrido, manteniendo las comprobaciones de bordes, huecos y objetos móviles.
- **Seguridad al parchear memoria (`FrozenThreads`)**: ningún parche de código nativo se escribe si falla la enumeración o suspensión de algún hilo; los hilos ya detenidos se reanudan de forma segura ante cualquier fallo.
- **Centro de colisión de vehículos en el SDK (`delirium-sdk`)**: transformar la caja de un vehículo actualiza también el offset local de su centro de colisión en `room_clear`.
- **Recuperación automática de caché en el instalador**: si un paquete descargado o en caché está incompleto o dañado, el asistente lo aparta como `.invalid-<id>` y vuelve a obtener y verificar el componente original en lugar de bloquear todos los reintentos posteriores.
- **Comprobación anticipada de permisos en Solo Delirium**: el asistente verifica que Windows permite escribir en la carpeta del juego antes de extraer la ISO, mostrando un aviso claro si faltan permisos de administrador.
- **Unificación con las mejoras del instalador 0.4.1–0.4.5**: incluye respaldo `.cmd` cuando Windows devuelve `E_NOTIMPL` al crear accesos directos `.lnk` (0.4.5), continuación sin *Urbanize* si falla su parche opcional (0.4.4), compatibilidad con variantes copiadas de idioma `.gxt`, vídeos `.mpg` y audio (0.4.3), *Grinch Trainer* desactivado por defecto en el pack completo y restauración segura de *Solo Delirium* (0.4.2), y preservación de cargadores ASI existentes junto con detección del ejecutable original de disco retail (0.4.1).

### English

- **CJ shoulder & upper-arm anatomy (`torso.dff` / `sa-combat` / `Rig`)**: on CJ's clothing and body meshes (`player.img`), bones `301` and `302` weight the upper half of the bicep/deltoid and share the exact bind socket of `UpperArm` (`22`/`32`), whereas `ped.ifp` drives them with a static NPC breast track rotated 96° forward that bent CJ's upper arms into a Z-zig-zag crease. `301`/`302` now attach to `UpperArm` (`22`/`32`) as authored in `torso.dff`'s `FrameList`, participate in upper-body blend masks, are no longer overwritten by `ped.ifp`'s chest track, and skip rest-pose elevation correction on the neck-rooted clavicles (`21`/`31`).
- **Keyboard controls for cover, IV locomotion, melee & gravity (`PlayerCover` / `NikoMovement` / `CombatRuntime` / `GravityControl`)**:
  - **Cover bound to `B` on keyboard** (`CoverKey=B` configurable in `DeliriumSA.ini`, while keeping `R1`/`RB` on XInput controllers): removes the fallback on `CPad::RightShoulder1` (`pad + 0xC`), which on PC keyboard/mouse maps to Right Mouse Button (Aim) and caused CJ to enter or leave cover whenever aiming.
  - **GTA IV on-foot movement on PC keyboard**: `W, A, S, D` jogs normally, `Alt + W, A, S, D` walks, and holding `Space` sprints directly without button mashing (preserving original Xbox 360 stick/button behavior when an XInput pad is active).
  - **Keyboard melee & reduced gravity shortcuts**: while locked on with Right Mouse Button, `R` triggers alternate punch/counter, `Q` kicks, and `Alt` dodges/blocks (alongside `F`/`Enter`, `Space`, and `Shift`), and holding **`K`** for 0.6s toggles reduced gravity on keyboard (alongside `L3 + R3` on controller).
- **Foot cover camera independent from vehicle drive-by aim**: disabling vehicle shoulder aiming (`DriveByShoulderAim=0`) no longer disables the foot cover camera callback.
- **Opposite-shoulder cover framing**: aiming over the opposite shoulder (high-left corner) recovers the pre-collision native camera anchor before running the active cover sweep, keeping CJ properly framed instead of clipped out of view.
- **Low-wall cover continuity**: CJ can slide and reverse direction across low cover obstacles whose rounded COL spheres slightly protrude (`woodenbox1224`), while preserving strict edge, gap, and moving-obstacle checks.
- **Thread-safe native code patching (`FrozenThreads`)**: native instruction patches abort cleanly and resume already-suspended threads if any thread handle or suspension call fails.
- **Vehicle collision center transform in `delirium-sdk`**: transforming a vehicle box now updates its local collision-center offset alongside the center of mass in `room_clear`.
- **Corrupted installer cache recovery**: damaged or incomplete cached archives are quarantined as `.invalid-<id>` and re-acquired/verified instead of permanently blocking subsequent install attempts.
- **Early write-access preflight in Solo Delirium**: checks folder write permissions before extracting the GTA IV ISO and explains if Administrator privileges are required.
- **Unified with 0.4.1–0.4.5 installer fixes**: includes the `.cmd` launcher fallback when Windows COM shortcuts return `E_NOTIMPL` (0.4.5), optional *Urbanize* skip on delta failure (0.4.4), copy-only localized `.gxt`/`.mpg`/audio tolerance (0.4.3), *Grinch Trainer* disabled by default in the full pack and accurate *Solo Delirium* restore target selection (0.4.2), and existing ASI loader preservation plus retail-disc executable identification (0.4.1).

## 0.4.5 · compatibilidad de accesos directos en instalaciones standalone

- Evita deshacer la instalación completa cuando el componente COM de accesos directos de Windows devuelve `NotImplementedException` / `E_NOTIMPL` (`0x80004001`), creando un lanzador `SA Delirium.cmd` de respaldo que se verifica y limpia al restaurar.
- Los informes de error incluyen la pila de llamadas gestionada y las excepciones internas para facilitar el diagnóstico.

## 0.4.4 · respaldo opcional de Urbanize

- Si falla el parche local sobre `Root.cs` de *Urbanize*, el instalador limpia sus archivos parciales y completa la instalación de SA Delirium sin ese extra opcional.

## 0.4.3 · variantes de idioma e introducción

- El instalador acepta bases de datos de texto `text/*.gxt` y vídeos `movies/*.mpg` de distintas ediciones o traducciones cuando la receta solo los copia desde el GTA San Andreas del usuario.

## 0.4.2 · Grinch Trainer desactivado por defecto y restauración Solo Delirium

- *Grinch Trainer* se entrega desactivado por defecto en `SA/Documentation/Optional/GrinchTrainerSA.asi.disabled` para evitar cierres al cargar partida combinándolo con el motor de físicas.
- Corregida la selección de carpeta al restaurar una instalación de *Solo Delirium*.

## 0.4.1 · cargadores ASI existentes y detección de ejecutable de disco

- *Solo Delirium* conserva cargadores ASI ya existentes en `vorbisFile.dll`, `winmm.dll` o `dinput8.dll` y tolera archivos de audio y `stream.ini` copiados sin parche binario.
- Identificación explícita del ejecutable original de disco retail 1.0 US (`8c609f10...`), indicando que requiere el parche 1.0 verificado.

## 0.4.0

- Delirium GTA IV simula ruedas, transmisión y suspensión al 130 %. Recupera
  los perfiles por modelo y suaviza las correcciones pequeñas de dirección.
- Clásico retocado conserva el manejo de SA y añade balanceo visual. Los
  faros, sus haces y las piezas transparentes acompañan a la carrocería.
- Cambiar entre Clásico y Delirium IV en partida conserva el tráfico.
  L1 apunta y R1 dispara desde el vehículo en los tres modos.
- El movimiento de CJ cambia desde el menú durante la partida.
- La geometría temporal de vehículos se recicla para evitar que se pare
  el motor de físicas en sesiones largas. Se reduce el trabajo al activar
  un cuerpo físico.
- En gravedad reducida, CJ y el vehículo que conduce quedan al 50 %.
  Los peatones y el resto de vehículos quedan al 1 %.

- Instalación completa o Solo Delirium desde un único asistente. Incluye
  progreso por etapas e informes de error que se pueden copiar.
- Cobertura de CJ y policías, cambio de hombro y mejoras de combate y daño.
- La suspensión responde al peso en el punto de contacto. La deformación
  localizada se puede activar por separado.

## 0.4.0 · previa privada 10

- Mantén pulsados L3 y R3 para poner al 10 % la gravedad de los NPC y los
  vehículos ajenos. CJ y el vehículo que conduce conservan la gravedad normal.

## 0.4.0 · previa privada 9

- Conducción Delirium. Cada modelo conserva su transmisión de referencia y el
  reparto de tracción del chasis de San Andreas. Se corrige el par cuando ambos
  usan un número distinto de ruedas motrices y se refuerza el arranque.
- El stick responde antes al girar a velocidad de carretera. El efecto de un
  toque de freno de mano decae también durante los pasos de suspensión nativa.
- La suspensión de Delirium sigue activa durante los fotogramas lentos dentro
  del paso máximo del temporizador de San Andreas.
- Una esquina exterior del mapa puede hundir localmente el panel que recibe
  el golpe. La deformación conserva su ajuste independiente en el menú.

## 0.3.14 · GTA IV USA para Xbox 360

- El instalador admite la compilación USA de GTA IV para Xbox 360 con Media ID `6AC07221`, además de la compilación Europa/Asia ya compatible.
- El asistente comprueba la ISO antes de copiar San Andreas. Si falla la instalación, muestra el paso y la ruta del registro en un aviso.

## 0.3.13 · barra de progreso del instalador

- Corregidos los avisos de error al mostrar el progreso durante la instalación.
- El asistente muestra el progreso completo al terminar.

## 0.3.12 · instalación en copias clásicas sin archivos de Steam

- El instalador admite copias compatibles de PC 1.0 US sin `installscript.vdf`.

## 0.3.11 · CJ borracho como Niko y sin atravesar el suelo

- Borracho: cuando el juego pone a CJ borracho (por ejemplo, con el "Efecto borracho" de Grinch Trainer), CJ se comporta como Niko totalmente borracho en GTA IV durante 180 s:
  - va con su cuerpo físico, tambaleándose con el equilibrio de GTA IV, y el stick lo inclina hacia donde quieras ir;
  - si se cae, espera quieto en el suelo, se levanta y sigue tambaleándose hasta que se acaba el tiempo;
  - la cámara de borracho dura los mismos 180 s y luego se desvanece en 10 s;
  - en las misiones se mantiene el efecto de San Andreas.
- Atropellos: CJ ya no atraviesa el suelo. Si un golpe empuja el cuerpo a través del suelo, se deshace ese paso. Si aun así queda bajo el mapa, vuelve encima del suelo.

## 0.3.10 · daño a CJ, atropellos rápidos y sin cierre sobre el agua

- CJ recibe daño de GTA IV con su cuerpo físico: al caer, al saltar de un coche en marcha, al salir despedido por una explosión y al ser atropellado. El daño sale del cálculo original de IV para cada golpe; San Andreas aplica el chaleco como siempre.
- Los atropellos a mucha velocidad ya no pierden la física: el cuerpo atropellado se quedaba fuera de la zona de física por el retraso del coche y volvía a la animación de San Andreas.
- Arreglado un cierre al volar bajo sobre el agua: el instalador llevaba unos ajustes de FxsFuncs pensados para NextGen Effects, que ya no se instala. Ahora va el archivo original del autor.
- Límite de 90 fps (FramerateVigilante). Se aplica con el limitador de fotogramas del juego activado.

## 0.3.9 · reacciones correctas con los modelos originales

- Los personajes ya no se doblan por la cintura al recibir disparos. Muchos modelos originales guardan el esqueleto inclinado 14-20° y las físicas lo tomaban como postura recta; ahora usan sus mismos huesos en postura recta.
- Un cadáver ya no se queda congelado doblado y sin colisiones: solo se congela cuando está tumbado y quieto.
- La opción de modelos originales suaviza codos y rodillas en tu PC, a partir de los modelos de tu propio juego. No se reparte ningún modelo.

## 0.3.8 · elige los modelos de personajes

- El asistente pregunta qué modelos quieres: los originales del juego o HD Skin Pack 299 de V@DO$, que se descarga sin cambios de su enlace original en GTAInside (278 MB). Si la descarga falla, se instala con los originales.

## 0.3.7 · instalador solo con lo que se puede redistribuir

- Salen del instalador los mods visuales sin permiso de redistribución registrado: HD Skin Pack, Jefferson Retextured, SA Brightened, Parallax Interiors, BSOR y LOD Vegetation, NextGen Grass, NextGen Remaster Effects, Enhance ParticleTXD, Improved Lamps, LV Gas Station Neon y Rubbish GTA 4. También sus parches de mapa y texturas.
- La jugabilidad no cambia. Se puede añadir cada mod desde su autor con Mod Loader.

## 0.3.6 · más fps, correr como en GTA IV y reacciones más sólidas

- Correr: pulsar X ya no mueve los brazos como en un sprint. Con los puños, los brazos siguen la carrera de GTA IV; los clips armados de IV no están, así que las armas a dos manos conservan los brazos de San Andreas.
- Rendimiento: los cuerpos con física ya no preparan todos los coches de la escena, solo los que pueden tocar. Menos tirones y sin el salto de fps al pasar de uno a dos pasos de física.
- Disparos: una ráfaga de Uzi o MP5 empuja el cuerpo en vez de reiniciar la reacción con cada bala. Las escopetas reparten los perdigones por las partes del cuerpo. La fuerza de cada arma sale de los datos de GTA IV de tu ISO.
- Balas cercanas: los NPC se encogen si una bala les pasa a menos de 1,5 m.
- Heridos: un NPC malherido puede quedarse tumbado y luego marcharse cojeando con las animaciones de GTA IV.
- Física y control: las animaciones de San Andreas solo vuelven cuando la física devuelve el control. Un empujón a un NPC que corre ya no acaba al instante, y un coche que golpea a alguien mientras se levanta lo vuelve a tirar.
- Choques: sin lanzamientos imposibles por cuerpos encajados bajo un coche. El parabrisas se rompe al salir despedido y las piernas no se enganchan al pasar.
- Bloqueos: guardia de cierres del juego con registro.
- Limit Adjuster 2.0.6.

## 0.3.5 · combate, policía y choques como en GTA IV

- Arreglados tres cierres del juego: en la tienda (GInput y Grinch Trainer cambiaban a la vez la misma lectura del mando), al salir del juego y al reiniciar una partida. El trainer va sin modificar.
- Pelea como en GTA IV: CJ puede quitar el arma cuerpo a cuerpo a los NPC y a los policías si esquiva su golpe y contraataca con el objetivo fijado. Vale para bate, porra, palo de golf, pala, taco, cuchillo y katana; los NPC con cuchillo o katana pelean con los movimientos de cuchillo de GTA IV.
- Policía como en GTA IV: un policía al que ataca un NPC, aunque vaya armado, saca la pistola y va a por él en vez de huir. Se apaga con `IVPoliceResponse=0` en `[Combat]`.
- Choques como en GTA IV: en un choque de frente fuerte, CJ y los NPC de los asientos delanteros salen disparados por el parabrisas. Se apaga con `WindscreenEjection=0` en `[Vehicle]`.
- Atropellos: los peatones ruedan por el capó y el parabrisas en vez de pasar por debajo del coche.
- CJ: se tira de cabeza solo con Círculo en el aire (no con dos Cuadrados), con su peso real; los empujones a peatones empiezan antes; apunta bien mientras anda; sale rápido del coche; ya no atraviesa el suelo a mucha velocidad.
- NPC: empujones en cadena entre peatones; muñecas sin dobleces imposibles; Ryder sin el cuello torcido.
- El menú de Grinch Trainer solo se abre con X + Cuadrado + Círculo.

## 0.3.4 · sin cierres en la misión de Ryder

- Arreglado un cierre del juego con la copia de Steam: al empezar una pista de radio o de audio a mitad, si su duración salía 0 el juego dividía entre cero (visto en la misión de Ryder al salir de Grove Street). Esa pista empieza ahora desde el principio.
- Arreglado un cierre al comprar comida en la misión de Ryder: Grinch Trainer 1.0 engancha la lectura del mando que usan las tiendas y, al pasarla al juego, llamaba a una dirección vacía. Delirium deja pasar esa lectura directamente al juego mientras el trainer no tenga esa dirección. El trainer va sin modificar.

## 0.3.3 · físicas de GTA IV en motos, combate y atropellos

- Motos y bicis como en GTA IV: al robar una moto, el conductor sale despedido con el ragdoll y empuja la moto al suelo; disparar o golpear a un motorista lo tira con física completa, parado o en marcha. Los atropellos de motoristas por delante, por detrás y de lado reaccionan según la velocidad.
- Los peatones se apoyan con la mano ante coches y motos lentas que se les acercan.
- Combate: los golpes largos de los NPC llegan a la cara, las animaciones de pelea van fluidas a 120 FPS y solo uno o dos atacantes pegan a la vez. CJ en el suelo recibe daño pero termina de levantarse aunque le sigan pegando.
- Caer en física no borra lo que hacía el personaje: un criminal que tropieza sigue huyendo y el policía sigue persiguiéndolo.
- El motor de físicas tiene una segunda versión para procesadores con AVX2, 1,8 veces más rápida con varios cuerpos; se elige sola al arrancar y, si el equipo no la admite, se usa la normal.
- Texturas: donde dos mods traen su propio archivo de texturas para la misma zona, se juntan las que faltaban. Arregla el suelo liso beige de Jefferson junto a Idlewood/Ganton y otras 10 zonas.

## 0.3.2 · el juego, no la tienda

- El instalador acepta cualquier copia verificable de San Andreas clásico: Steam o PC 1.0 US, incluida una copia de Steam ya degradada. Cada archivo se comprueba por su huella frente a su versión de Steam o de PC 1.0.
- Una copia que no es de Steam se instala en su propia carpeta, elegible en la ventana, con accesos directos en el escritorio y en Inicio. La carpeta del juego solo se lee.
- Desde una copia 1.0 se conserva su radio completa y sus archivos de modelos originales. Con Steam el resultado es idéntico a 0.3.1.
- El asistente dice qué edición ha encontrado y por qué sirve o no, y muestra el código de un ejecutable desconocido para poder añadirlo.
- Al reinstalar se copian las partidas de la instalación anterior; su respaldo queda intacto.
- Restaurar funciona también con rutas largas.

## 0.3.1 · todo menos las armas

- Vuelven todos los extras HD, el trainer y Urbanize. Se retiran los dos packs de armas añadidos. Siguen fuera RoSA, Proper Player Retex, las texturas de GTA IV y el HUD de GTA V.
- Urbanize se descarga automáticamente de su autor; el resto de mods va incluido.

## 0.3.0 · instalador desde Steam y la ISO

- Instalador que parte del San Andreas de Steam y de la ISO propia de GTA IV para Xbox 360, sin carpeta de desarrollo.
- Izzy's Lively Vehicle Physics 1.6 incluido con permiso de su autora.

## En desarrollo · port nativo de Steam

- Conexión visual propia mediante la API de rutas físicas de UAL: composición streaming de los IMG del propietario, integridad, caché y registro transaccional. 733 reemplazos locales probados con un actor físico nativo; son contenido personal, no el modpack final.
- Constructor de RAR con manifiesto cerrado de datos y lista UTF-16, evitando el límite de longitud de comandos de Windows.
- El capturador de pruebas arranca sin ventana de consola; se conserva el caso anterior que pausó el juego antes del último disparo.

- Adaptador de juego enlazado con 148 referencias verificadas, dirección relativa al módulo y huellas compatibles con ASLR, sin modificar el EXE. Se conserva el código de juego existente y se separan las herramientas de captura experimentales.
- Caché v3 completa: 17 diccionarios originales de IV, tablas/decisiones, esqueleto y doce técnicas generadas desde el SA del propietario; 418 clips en total. Validación de integridad antes de publicar la disponibilidad del motor.
- Primer importado y reapertura comprobados sin red ni clave en APPDATA, con soporte de formato XEX de Xenia acreditado bajo BSD.
- Prueba real de Steam con un NPC nativo: daño no letal, recuperación completa, muerte y reacción física del mismo cadáver al recibir otro disparo. Conserva su alcance: todavía no certifica combate, vehículos o modpack completo.
- Conservado el código del motor y del combate existentes en una copia fuente independiente, compilable para x86 sin el antiguo payload de constantes.
- Incorporada la corrección de pivotes adaptados de Delirium 0.6.0, con prueba que falla antes del cambio y 93 pruebas del núcleo y 68 del SDK correctas después.
- Comprobadas en Steam original la lectura del esqueleto de CJ, la importación de la colisión real del interior y una simulación de 240 pasos. Son pruebas delimitadas del laboratorio, no jugabilidad completa ni contenido del RAR alpha.4.
- Ranura de pruebas convertida de forma reversible a Steam; no requiere sustituir `main.scm` ni tocar la partida original.

## 0.1.0-alpha.4 · laboratorio

- Modo sin bordes que crea el dispositivo Direct3D en ventana y mantiene el modo del escritorio.
- La cámara nativa recibe el stick derecho XInput durante la entrada analógica aunque esté seleccionado el modo de ratón. Se conservan las preferencias originales fuera del procesamiento de cámara.
- Se mantiene el límite nativo de fotogramas. Los fixes de temporización de la copia de desarrollo necesitan adaptación a Steam antes de cambiar ese límite.

## 0.1.0-alpha.3 · laboratorio

- El RAR incorpora el preparador x86 de Delirium y su licencia. La ventana del primer arranque permite reintentar después de colocar la ISO.
- El ajuste de iluminación usa `data/timecycp.dat` de la instalación local mediante una superposición en memoria. Un filtro propio aplica la modulación de color PS2 sin desenfoque. Ambos pueden desactivarse independientemente de la suspensión.
- Sigue pendiente conectar Delirium a los peatones de Steam. Esta previa no se considera una versión jugable del port completo.

## Desarrollo · importación de la ISO

- Preparador del SDK Delirium 0.6.0 desde una imagen de GTA IV de Xbox 360 aportada por el usuario.
- Caché local por instalación. El segundo arranque puede inicializar el SDK sin la ISO ni los archivos de la instalación de GTA IV.
- Validación de la edición mediante la huella comprobada por el propio SDK.
- Mapa de lecturas para la edición comercial Europa/Asia y prueba de primer arranque con esa ISO. La sonda de Steam ya lee las jerarquías animadas de un civil y de CJ.

## 0.1.0-alpha.2 · laboratorio

- Identificada la actualización nativa de coches en Steam build 459558.
- Implementado un prototipo propio de cabeceo aéreo con el acelerador y el freno. Se mantiene desactivado por defecto.
- Añadidas pruebas de ejes de rotación, contacto de las ruedas y paso temporal.

## 0.1.0-alpha.1

- Primera previa técnica para el binario original de Steam clásico.
- Adaptación propia de amortiguación de coches mediante datos nativos cargados en memoria.
- Construcción x86 y creación del RAR con lista cerrada de archivos.
