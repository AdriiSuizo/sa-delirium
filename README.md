# SA Delirium

<p align="center">
  <img src="docs/media/sa-delirium-gameplay.gif" alt="Combate y reacciones físicas de Delirium en GTA San Andreas" width="640">
</p>

Delirium lleva físicas, combate y conducción inspirados en GTA IV a GTA San
Andreas clásico de PC. Puedes instalar el núcleo en tu juego o preparar un
perfil completo con los extras.

## Instalar

Descarga [SA-Delirium-0.6.4-Setup.exe](https://github.com/AdriiSuizo/sa-delirium/releases/latest)
o extrae el RAR de la misma versión. Elige la carpeta o el ejecutable de San
Andreas y selecciona tu ISO de GTA IV para Xbox 360.

- **Instalación completa** prepara un perfil con Delirium, los arreglos y los
  extras que se describen abajo. Admite Steam clásico y copias verificadas de
  PC 1.0 US. La descarga de Urbanize se hace desde su autor.
- **Solo Delirium** instala el núcleo en una carpeta de PC 1.0 US con un
  ejecutable admitido. Conserva los archivos existentes. Reutiliza un cargador
  ASI verificado o añade uno compatible si hace falta.

Necesitas Windows10/11 x64 con .NET Framework4.8, tu **GTA San Andreas clásico**
y una **ISO compatible propia de GTA IV para Xbox360**. Sirve la copia de Steam
(AppID12120) o una de PC 1.0 US, incluida una copia de Steam ya degradada. Lo que
cuenta es el juego: cada archivo se comprueba por su huella frente a su versión de
Steam o de PC 1.0. Con una copia 1.0 se conserva su radio completa; con Steam
queda la radio que Steam distribuye, sin las canciones que Rockstar retiró. Al elegir la
carpeta, el asistente dice qué edición ha encontrado y por qué sirve o no: Steam,
alemana, PC 1.0, otras versiones de disco, Definitive Edition o un ejecutable
desconocido, cuyo código SHA-256 muestra para poder añadirlo. Si algún archivo está
modificado o falta, se detiene sin tocar nada y dice cuál.

Para GTA IV, el asistente comprueba el ejecutable dentro de la ISO antes de
copiar San Andreas. Admite las compilaciones verificadas de Xbox 360
Europa/Asia (Media ID `4A53F9F6`) y USA (Media ID `6AC07221`). La comprobación
se basa en la huella del ejecutable dentro de la imagen.
No necesitas la carpeta de desarrollo ni otra instalación de GTA IV de PC.
Solo Urbanize requiere conexión durante la instalación; el asistente lo descarga
automáticamente de su distribución original, sin pedir otro archivo. Los recursos de GTA IV
se generan en tu equipo desde la ISO; después puedes quitarla de esa ruta.

Se requiere espacio para el perfil y su preparación temporal (aproximadamente
15GB adicionales). Las rutas de instalación deben admitir el límite nativo de
SA1.0 para los nombres de sus partidas; el asistente comprueba ese límite.
Si faltan DirectX9 D3DX/XInput, el asistente ofrece el redistribuible original de
Microsoft incluido; su instalador puede pedir permisos de administrador.

## Hotfix 0.6.4

- Si el modo de vídeo de arranque falta en la lista del adaptador, el juego elige uno compatible. [Pruebas y alcance](docs/AUDIT-0.6.4-video.md).

## Hotfix 0.6.4

- Si el modo de vídeo de arranque falta en la lista del adaptador, el juego elige uno compatible. [Pruebas y alcance](docs/AUDIT-0.6.4-video.md).

## Correcciones en 0.6.3

- El tráfico frena ante CJ en ragdoll y durante la levantada.
- Corrige movimiento con spray, velocidad al apuntar, animación de reposo, postura con armas a dos manos, efectos de contraataques y asistencia antivuelco. [Pruebas y alcance](docs/AUDIT-0.6.3-traffic.md).

## Correcciones en 0.6.3

- El tráfico frena ante CJ en ragdoll y durante la levantada.
- Corrige movimiento con spray, velocidad al apuntar, animación de reposo, postura con armas a dos manos, efectos de contraataques y asistencia antivuelco. [Pruebas y alcance](docs/AUDIT-0.6.3-traffic.md).

## Correcciones en 0.6.2

- Detección de peatones en interiores compartida entre físicas, poses y locomoción.
- CJ solo interrumpe su recuperación por tráfico cuando existe contacto corporal y velocidad de cierre.
- Avance correcto de curvas cortas de marcha de NPC en cuadros lentos.
- Se conservan las novedades de 0.6.0 y los modos completo/Solo. [Pruebas y límites](docs/AUDIT-0.6.2-2026-11-26.md).

## Novedades y correcciones en 0.6.0

- **Locomoción GTA IV para NPCs (15 perfiles)**: 115 clips retargeteados sobre 21 articulaciones con longitud de zancada, apoyo de pies/suelas adaptado a cada modelo, pasos al apuntar en 8 direcciones y corrección de piernas inmóviles.
- **Disparos y lanzamientos GTA IV desde coches y motos (`IVDriverPose.asi`)**: 24 perfiles de asiento y 96 clips originales, corrección del brazo que atravesaba el torso, arma visible en la mano (compatible con SilentPatch) y sincronización del disparo con la munición.
- **Movimiento de CJ, sigilo y cobertura mejorados**: arreglado el cierre al correr desde cuclillas, transición suave al dejar de apuntar o salir de cobertura, compatibilidad con el asesinato sigiloso nativo con cuchillo, postura agachada y esquinas para CJ, y extensión de la cobertura policial a otros NPCs armados con pasos visibles al acercarse.
- **Daño, rifles a distancia, recuperación y audio de impactos**: ragdoll con rifles (`33`/`34`) a distancia con geometría local, corrección del daño de bastón a la cabeza de CJ (ya no mata de 1 golpe), levantada sin reinicio por disparos no letales, recuperación de NPCs junto a paredes/vehículos, umbral de caída de CJ en `4,5 m`, cojera ajustada al 60 % y 12 sonidos de impacto contra el suelo de GTA IV.
- **Preparación automática desde la ISO de Xbox 360 y base 0.5.1**: el instalador (tanto completo como *Solo Delirium*) incluye `IVDriverPose.asi` y genera automáticamente `DriverPoses.bin`, `NPCGait.bin`, `NPCMovers.dat` y `cj-ground-audio` desde tu ISO de GTA IV para Xbox 360 y tu copia de San Andreas, conservando todos los arreglos de `0.5.1` (fade-out `wstart`/`wstop`, `frictionStructureOther`, `VehicleDamage`, diagnóstico de integridad y `E_NOTIMPL`).

### Resumen de 0.5.0–0.5.1, 0.4.8–0.4.9 y base 0.4.0–0.4.7

Esta base mantiene todas las novedades y correcciones de **0.5.1**, **0.5.0**, **0.4.9**, **0.4.8**, **0.4.7**, **0.4.6** y **0.4.0** (ver [historial](CHANGELOG.md) para el detalle completo):

- **Estabilidad y mejoras de 0.5.0**: golpes a vehículos sin crashes (`R6025`/`CReference`) con daño, abolladura en tiempo real (`dentMap`) y audio nativo; movimiento GTA IV en gimnasios e interiores (`authoredMissionProtected()`); subir a capós/techos sin empuje y rozamiento lateral sin frenado; continuidad al soltar apuntado en armas de 1 mano; contraataques con audio y sangre a la altura del pecho; y suspensión/estabilidad mejoradas en coches bajos.

- **Combate desarmado, saltos y recuperación Dive / Brace (0.4.8)**: puñetazos desarmados al aire y contra vehículos, trote continuo tras saltar (`suspend_jump`) y levantamiento limpio tras `Dive`/`Brace` (`ABORT_PRIORITY_IMMEDIATE` y altura de pie `1.00 m`).
- **Apuntado GTA IV integrado (0.4.7, sustituye a *Advanced Aiming Mod*)**: apuntado sobre el hombro estilo GTA IV a pie y desde vehículos (`IvAim=1`), zoom con `R3` / `MMB` y rueda (`AimWheelZoom=1`), `R3` para mirar atrás al conducir y alternancia de hidráulica/cámara en *lowriders* (`G` / `D-Pad Arriba`).
- **Cobertura, animaciones, caminar y físicas (0.4.0–0.4.7)**: cobertura con **`B`** en teclado (`R1`/`RB` en mando), sincronización de cámara/pose en esquinas, caminar con `Alt` / `Bloq Mayús`, disparo en ragdoll (`RagdollFire`), huida de policías desarmados, daño de caída en ragdoll al `0.25x`, umbral de eyección de `21.6`, sangre fija al suelo, anatomía de brazos de CJ (`301`/`302`), estabilidad en deportivos (*Hotring*) y menú en `Pausa → Opciones`.

## Funciones de 0.4.0

En **Pausa → Opciones** puedes cambiar entre Delirium GTA IV y Clásico retocado
en partida. La instalación completa también ofrece Izzy 1.6, que se aplica
al reiniciar. La deformación y el movimiento de CJ tienen sus propios ajustes.
Desde un vehículo, **L1** apunta y **R1** dispara. Mantén **L3 + R3** para
alternar la gravedad reducida.

Consulta el [historial](CHANGELOG.md) para ver los cambios de combate, cobertura,
físicas y conducción.

## Contenido de la instalación completa

Incluye Delirium y la integración de físicas/combate/movimientos, Izzy Lively
Vehicle Physics1.6 con sus ajustes originales de puertas, SkyGfx Extended1.7,
timecycle PS2, Project2DFX y FxsFuncs1.1. También CLEO/CLEO+, Mod Loader, SilentPatch, GInput, Widescreen Fix,
Windowed Mode, Open Limit Adjuster, Framerate Vigilante y PortableGTA.
DXVK se comprueba en el equipo; si no puede crear un dispositivo, se usa D3D9
de Windows. Los créditos y readmes originales se instalan en `SA/Documentation`.

Se conservan Urbanize y los eventos civiles de desarrollo (*Advanced Aiming Mod* se retira desde **0.4.7** al sustituirse por el apuntado IV integrado). **Grinch Trainer se incluye desactivado por defecto** debido a un crash de carga observado al combinarlo con Delirium. Su archivo original, verificado y sin modificar, queda en `SA/Documentation/Optional/GrinchTrainerSA.asi.disabled` para quien quiera activarlo manualmente (puede provocar el mismo fallo). El resto de herramientas del pack permanece activo.
La configuración de combate, físicas y movimientos de Delirium se conserva.

El asistente te deja elegir los modelos de personajes: los originales del juego, con codos
y rodillas suavizados en tu PC a partir de tu propia copia, o
**HD Skin Pack 299 de V@DO$**, que se descarga de su enlace original en GTAInside y
nunca va dentro del instalador.

Quedan fuera los mods visuales sin permiso de redistribución registrado (BSOR/LOD Vegetation, NextGen Grass, NextGen Remaster Effects, Enhance
ParticleTXD, SA Brightened, Jefferson Retextured, Parallax Interiors, neones/lámparas
y Rubbish GTA4). Puedes añadirlos tú desde sus autores con Mod Loader.

También quedan fuera **ambos packs de armas añadidos** (GTA V Weapons y NeXtGen Remaster
Weapons), RoSA/Proper Player Retex, GTA IV Textures and HQ Roads y el
HUD/minimapa/rueda de GTA V. Las armas usan los recursos de San Andreas de Steam.
Esta selección corresponde a la aclaración del usuario: «Todo menos las armas»,
conservando sus exclusiones anteriores. La entrega0.3.0 era un perfil más reducido.

Urbanize se mantiene separado según su licencia, que prohíbe reempaquetarlo en
modpacks desde octubre2026. El asistente obtiene su original y aplica los ajustes
locales verificados. Se instalan sus readmes/licencia y se acredita a
[Junior_Djjr](https://www.mixmods.com.br/2026/02/urbanize/).

## Compatibilidad de instalaciones existentes

El instalador admite variantes existentes de archivos de audio en
`audio/sfx/` y `audio/streams/`, vídeos introductorios `movies/*.mpg`,
idiomas `text/*.gxt` y `stream.ini` cuando la receta **solo los copia**:
registra y comprueba su hash real durante la preparación. Un archivo ausente,
vacío o excesivamente grande sigue bloqueando la instalación. Los ejecutables,
DLL del paquete, scripts, modelos y recursos que necesitan un parche binario
siguen utilizando las comprobaciones estrictas. El cambio no recupera archivos
que falten ni garantiza que una traducción o un vídeo modificado funcione en el juego.

**Solo Delirium** conserva los posibles cargadores ASI que ya existen en
`vorbisFile.dll`, `winmm.dll` o `dinput8.dll` cuando no coinciden con
las huellas conocidas. Se avisa de que un cargador desconocido **no está
validado**: instalar el mod no garantiza que ese cargador lo ejecute.
No se sobrescribe ni se añade otro cargador encima. Si faltan las bibliotecas
que necesita el juego o el archivo está vacío, se sigue notificando el error.
Para una instalación reproducible, utiliza una copia limpia verificada.

El comando `--verify-source` añade al informe
`copied_without_catalogued_hash`, separado de `missing_or_changed`.
Esto ayuda a distinguir una edición distinta de un archivo que realmente
falta.

**Si el juego se cierra al iniciar una partida:** comprueba si ocurre también
con una partida nueva y sin otros mods, guarda los registros de
`logs/manual-sessions` junto al perfil `SA` cuando existan y consulta
el módulo con errores en el Visor de eventos de Windows → Registros de Windows
→ Aplicación. Indica tu edición de GTA SA, modo de instalación, GPU y
el nombre del módulo; un cierre después de instalar no permite identificar
el plugin responsable por sí solo. No borres las partidas de `SA/userfiles`.

## Ejecutable original del disco de PC 1.0 US

El instalador puede identificar el ejecutable original de disco de
14.405.632 bytes con SHA-256
`8c609f108ad737deffbd0d17c702f5974d290c4379de742277b809f80350da1c`.
**Identificarlo no significa que sea compatible.** El ejecutable es distinto
del 1.0 US verificado que usa el runtime y no se aplica ninguna transformación
sin una receta binaria y comprobaciones de salida válidas.

Para incorporar esa edición hará falta analizar una copia legítima del ejecutable
original y producir un parche diferencial hacia el perfil existente. El proceso
de construcción ya admite ejecutables auditados mediante
`--classic-executable`, pero **el hash anterior no figura entre las fuentes
aprobadas para generar deltas**. No se incluyen ejecutables originales ni se
sustituyen archivos del usuario a ciegas. Hasta que exista una receta verificada,
se debe utilizar una edición ya compatible y se conservará intacta la instalación
original.

## Partidas, restauración y diagnóstico

En **Solo Delirium**, abre tu juego como siempre. **Restaurar** retira los
archivos registrados por el asistente y conserva las partidas de la instalación.

Con Steam, el mod crea su perfil independiente en **SA** dentro de la carpeta de
Steam. Se adapta la versión interna del juego para los plugins y se respalda el
`gta-sa.exe` original. Un lanzador espera a que termine el juego para mantener
su sesión en Steam. Con otra copia, el perfil se crea en la carpeta del mod que elijas, por
defecto `%LOCALAPPDATA%/SA-Delirium/Juego/SA`, con accesos directos en el escritorio y en
el menú Inicio; la carpeta de tu juego solo se lee. La carpeta de desarrollo y las partidas de Documentos se
conservan. El instalador no incluye ni importa partidas del autor.

Para desinstalar, abre el mismo asistente y pulsa **Restaurar**. Con Steam recupera
el ejecutable anterior; con otra copia quita los accesos directos. En ambos casos conserva el perfil y sus partidas en el respaldo
`SA-Delirium-backups`. Si existe una instalación anterior, restáurala primero;
no se actualizan perfiles encima de otros. Conserva siempre tus respaldos.
Verificar archivos desde Steam puede reemplazar el lanzador: restaura primero
con el asistente y después reinstala sobre los archivos verificados.

Las partidas del mod están en `SA/userfiles`. Los registros están en
`logs/manual-sessions` junto a `SA`, y en la caché local
`%LOCALAPPDATA%/SA-Delirium/Setup`. Tras una instalación terminada y con el
asistente cerrado, sus carpetas `prepared-*` y `helpers-*` son temporales y se
pueden quitar; conserva los registros si necesitas investigar un error.
No mezcles este perfil con otros loaders/renderizadores o SA-MP.

## Validación y límites

En el asistente se puede elegir el ejecutable directamente o pegar la
ruta de la carpeta. Antes de preparar el perfil, el asistente verifica los
archivos originales que necesita. Admite nombres alternativos y la variante
LAA de un ejecutable cuando puede reconstruir exactamente una entrada ya
verificada. También recupera `vorbisHooked.dll` o `vorbisFileHooked.dll` si
contienen la DLL original verificada.

Para comprobar una carpeta de referencia, basta con sus archivos. Este modo
devuelve la edición, cuántos archivos coinciden y cuáles hay que revisar.

```powershell
Start-Process -Wait .\SA-Delirium-0.6.4-Setup.exe -ArgumentList '--verify-source --target "D:\Juegos\GTA San Andreas" --report "D:\compatibilidad.json"'
```

Las huellas y las recetas verifican Steam clásico y PC 1.0 US. Otras
compilaciones con cambios en el código nativo, como 1.01, 2.0 y Rockstar Games
Launcher, requieren sus propias referencias. El asistente excluye Definitive
Edition. [Versiones de San Andreas en Plugin-SDK](https://github.com/DK22Pac/plugin-sdk/blob/master/shared/GameVersion.h).

Las pruebas históricas de la instalación con Steam se documentan en
[VALIDATION.md](installer/portable/VALIDATION-0.3.8.md). La entrega 0.4.0 se
comprobó también en una copia clásica de PC con instalación completa y
Solo Delirium.

Las pruebas son acotadas. No certifican todos los vehículos, misiones, mandos,
GPUs, idiomas o ediciones de ISO; tampoco equivalencia completa con GTA IV ni
120FPS sostenidos. Persisten límites de naturalidad, contactos de carrocería,
combate armado y ciertas combinaciones de actores/terreno.

## Fuentes y créditos

[Compilación del perfil portátil](installer/portable/BUILD.md),
[créditos de los autores](CREDITS.md), [licenciaGPLv3](LICENSE.md).
Las fuentes correspondientes acompañan la entrega. Los archivos originales
completos de los juegos, la ISO, recursos generados y partidas no se distribuyen.
Desarrollo asistido por OpenAI Codex (familiaGPT-6).

La entrega histórica `v0.2.0-dev-import.1` necesitaba una copia de desarrollo y
no sirve como instalador para otro PC. El port experimental R2 queda conservado
en [su documentación histórica](docs/NATIVE-R2-HISTORY.md).

---

## English — SA Delirium 0.6.4

**SA Delirium** brings GTA IV-inspired character physics (Euphoria/NaturalMotion reimplementation), melee and firearm combat, cover mechanics, and vehicle handling/deformation to classic PC **GTA San Andreas** (Steam AppID 12120 or PC 1.0 US).

### 0.6.4 hotfix

- The game selects an available mode if the requested startup video mode is missing. [Test results](docs/AUDIT-0.6.4-video.md).

### 0.6.4 hotfix

- The game selects an available mode if the requested startup video mode is missing. [Test results](docs/AUDIT-0.6.4-video.md).

### Fixes in 0.6.3

- Civilian traffic slows for CJ while ragdolling and getting up.
- Fixes spray-can movement, aiming pace, idle animation, two-handed weapon posture, counterattack effects and anti-roll assistance. [Tests and scope](docs/AUDIT-0.6.3-traffic.md).

### Fixes in 0.6.3

- Civilian traffic slows for CJ while ragdolling and getting up.
- Fixes spray-can movement, aiming pace, idle animation, two-handed weapon posture, counterattack effects and anti-roll assistance. [Tests and scope](docs/AUDIT-0.6.3-traffic.md).

### Fixes in 0.6.2

- Consistent interior membership for pedestrian physics, poses and gait.
- CJ recovery interruption requires actual vehicle contact and closing velocity.
- Short NPC gait cycles retain travel across slow frames.
- All 0.6.0 features and both installation modes are retained. [Tests and limits](docs/AUDIT-0.6.2-2026-11-26.md).

### What's New in 0.6.0

- **15-Profile GTA IV NPC Locomotion & Armed Stride**: 115 retargeted IV clips across 21 joints with model-adapted stride curves, sole alignment, 8-way armed steps, and frozen-leg fixes.
- **GTA IV Vehicle Drive-By Poses (`IVDriverPose.asi`)**: 24 seat profiles and 96 IV clips for cars and motorcycles, fixing arm-through-torso clipping, keeping the equipped weapon visible in hand (SilentPatch-compatible), and synchronizing firing with native ammo.
- **CJ Locomotion, Native Stealth & Cover Upgrades**: Fixes crouch-to-sprint crashes, refines aim/cover exit handoff, supports SA's native paired knife stealth kill, fixes crouched/corner cover posture for CJ, and extends police cover to other armed ambient NPCs with visible approach steps.
- **Long-Range Rifles, Recovery, Baton Fix & CJ Fall Audio**: Enables long-range rifle (`33`/`34`) ragdolls with local collision regions, removes the 4× NPC baton head-crit on CJ, stops non-lethal shots from resetting CJ's getup, improves NPC recovery near walls/vehicles, sets CJ's default high-fall threshold to `4.5 m`, tunes limp variant to 60%, and adds 12 GTA IV ground-impact audio samples.
- **Automatic Xbox 360 ISO Asset Generation & 0.5.1 Safety Base**: Generates `DriverPoses.bin`, `NPCGait.bin`, `NPCMovers.dat`, and `cj-ground-audio` automatically from the owner's GTA IV Xbox 360 ISO (USA & Europe/Asia) and SA files in both Full and *Solo Delirium* modes, keeping all `0.5.1` safety and installer diagnostic fixes.

### Highlights from 0.4.0–0.5.1

- **0.5.1 Polish & Safety**: Crisp ~0.25 s `wstart` fade-out & immediate `wstop`, `CBuilding` OOB guard in `frictionStructureOther`, post-`VehicleDamage` pointer check, positive-damage melee blood guard, actionable installer integrity diagnostics, and `E_NOTIMPL` shortcut-reader protection.

- **Deep Fixes & Stability (0.5.0 & 0.4.9)**: Crash-free vehicle melee strikes (`CleanUpOldReference` + driver/passenger matrix validation, real-time `dentMap` deformation, car-surface SFX), GTA IV movement inside gyms and interiors (`authoredMissionProtected()`), jumping onto car roofs without shove-back, no wall-grazing slowdown, 1-handed aim-release jog continuity, chest-level blood & audio on counterattacks, and low-car SA suspension (`ABI v11`, `rest_bias`, anti-roll, progressive bump stops).

- **Combat, Jumps & Dive Recovery (0.4.8)**: restored unarmed punches in air and against vehicles, smooth jog/run resumption after pure jumps (`suspend_jump`), and clean `Dive`/`Brace` stand-ups (`ABORT_PRIORITY_IMMEDIATE` + `1.00 m` standing clearance).
- **Integrated GTA IV Aiming & Vehicle Controls (0.4.7)**: built-in GTA IV over-the-shoulder aiming on foot and from vehicles (`IvAim=1`, *Advanced Aiming Mod* removed), `R3` / `MMB` zoom + scroll wheel (`AimWheelZoom=1`), `R3` vehicle look-behind, lowrider hydraulics/camera toggle (`G` / `D-Pad Up`), reworked cover approach & corner aim synchronization, dedicated walk hold/toggle (`Alt` / `Caps Lock`), manual ragdoll gunfire (`RagdollFire`), disarmed/unarmed cops flee (`PoliceResponse`), `0.25x` CJ ragdoll fall damage, raised windscreen ejection threshold (`21.6`), grounded static blood decals (`BloodDecals`), and IV straight-line stability fixes (*Hotring*).
- **CJ Upper-Arm Anatomy, Keyboard Controls & Installer (0.4.0–0.4.6)**: CJ upper-arm helper bones (`301`/`302`) attached to `UpperArm` (`22`/`32`), **Cover on `B`** (`R1`/`RB` on gamepad), PC keyboard IV movement & melee, **Pause → Options** live toggles (`Delirium GTA IV / Enhanced Classic / Izzy`, `CJ movement`, `Combat`, `Deformation`), corrupted cache recovery (`.invalid-<id>`), *Solo Delirium* write preflight/restore, `.cmd` shortcut fallback on `E_NOTIMPL`, optional *Urbanize* skip, and *Grinch Trainer* disabled by default. See [CHANGELOG.md](CHANGELOG.md) for full details.

### Installation & Restore

1. Download **[SA-Delirium-0.6.4-Setup.exe](https://github.com/AdriiSuizo/sa-delirium/releases/latest)** or extract the **RAR** archive (both contain the same setup assistant).
2. Choose **Full installation** (*Instalación completa*) for the complete standalone profile with extras, or **Delirium only** (*Solo Delirium*) to install only the physics/combat core into a compatible PC 1.0 US copy.
3. Select your classic PC **GTA San Andreas** folder/executable and your own **GTA IV Xbox 360 ISO** (USA `6AC07221` or Europe/Asia `4A53F9F6`). Requires 64-bit Windows 10/11 and .NET Framework 4.8. No official Rockstar game files or GTA IV assets are redistributed.
4. If updating from a previous version, click **Restore** (*Restaurar*) in the assistant first before installing the new version.
