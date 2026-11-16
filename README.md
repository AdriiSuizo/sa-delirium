# SA Delirium

<p align="center">
  <img src="docs/media/sa-delirium-gameplay.gif" alt="Combate y reacciones físicas de Delirium en GTA San Andreas" width="640">
</p>

Delirium lleva físicas, combate y conducción inspirados en GTA IV a GTA San
Andreas clásico de PC. Puedes instalar el núcleo en tu juego o preparar un
perfil completo con los extras.

## Instalar

Descarga [SA-Delirium-0.4.6-Setup.exe](https://github.com/AdriiSuizo/sa-delirium/releases/latest)
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

## Novedades y correcciones en 0.4.6

Esta versión unifica las correcciones del motor, cobertura y cámara sobre la base **0.4.0** con todas las mejoras de compatibilidad del instalador introducidas entre **0.4.1** y **0.4.5**:

- **Anatomía de brazos y hombros de CJ**: en `torso.dff` y las prendas de `player.img`, los huesos `301` y `302` forman la mitad superior del bíceps/deltoides en el mismo *socket* que `UpperArm` (`22`/`32`). Se enlazan ahora a `22`/`32` sin sobrescribirse con la pista pectoral de `ped.ifp` (que giraba la mitad superior del brazo 96° hacia el pecho creando un pliegue en zig-zag) y se evita encoger las clavículas (`21`/`31`) hacia el cuello.
- **Controles de teclado y menú de opciones en `Pausa → Opciones`**:
  - **Cubrirse con la tecla `B`** en teclado (`CoverKey=B` en `DeliriumSA.ini`, y `R1`/`RB` en mando): ya no interfiere con el Click Derecho (Apuntar) al jugar con teclado y ratón.
  - **Movimiento de GTA IV adaptado a teclado**: `W, A, S, D` trota normal, `Alt + W, A, S, D` camina despacio y mantener `Espacio` esprinta directamente sin tener que aporrearlo.
  - **Combate, nitro y gravedad en teclado**: al fijar objetivo con Click Derecho puedes usar `Click Izquierdo` (puñetazo), `R` o `F` (ataque secundario/contraataque), `Q` o `Espacio` (patada) y `Shift` o `Alt` (esquivar/bloquear); `N` activa el nitro separado del disparo y mantener `K` (0,6 s) alterna la gravedad reducida.
  - **Menú en `Pausa → Opciones`**: permite alternar en partida entre `Manejo: Delirium GTA IV / Clasico retocado / Izzy`, `Movimiento CJ: IV / SA`, `Combate: Delirium / SA` y `Deformacion: si / no`.
- **Cobertura y cámara**: la cámara al cubrirse a pie ya funciona aunque se desactive el apuntado desde vehículos (`DriveByShoulderAim=0`). Al apuntar por el hombro contrario (esquina alta izquierda), se recupera el punto nativo de cámara previo al recorte de colisión de San Andreas para que CJ no desaparezca del encuadre. Además, CJ puede avanzar y cambiar de sentido a lo largo de coberturas bajas con uniones redondeadas (`woodenbox1224`) sin atascarse.
- **Motor y parches nativos**: `FrozenThreads` verifica que todos los hilos relevantes se hayan abierto y suspendido antes de escribir parches de código nativo (reanudando los hilos si alguno falla), y la API de transformación de vehículos del SDK actualiza el centro de colisión junto con el centro de masa.
- **Instalador y compatibilidad (0.4.1–0.4.6)**:
  - Si un paquete en caché está incompleto o dañado, el asistente lo aparta como `.invalid-<id>` y vuelve a obtener una copia verificada sin bloquear reintentos.
  - Comprobación anticipada de permisos de escritura en *Solo Delirium* antes de extraer la ISO y restauración segura sobre la carpeta correcta del juego.
  - Si el componente COM de Windows devuelve `E_NOTIMPL` al crear accesos directos `.lnk` en modo standalone, crea un lanzador `SA Delirium.cmd` de respaldo.
  - Si falla el parche opcional sobre `Root.cs` de *Urbanize*, el instalador limpia los archivos parciales de ese extra y termina la instalación de Delirium.
  - Admite variantes copiadas de textos de idioma (`text/*.gxt`), vídeos (`movies/*.mpg`), sonidos (`audio/*`) y `stream.ini`, conserva cargadores ASI existentes en *Solo Delirium*, identifica el ejecutable original de disco retail 1.0 US y deja *Grinch Trainer* desactivado por defecto en el pack completo para evitar cierres al cargar partida.

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

Se conservan Urbanize, Advanced Aiming y los eventos civiles de desarrollo. **Grinch Trainer se incluye desactivado por defecto** debido a un crash de carga observado al combinarlo con Delirium. Su archivo original, verificado y sin modificar, queda en `SA/Documentation/Optional/GrinchTrainerSA.asi.disabled` para quien quiera activarlo manualmente (puede provocar el mismo fallo). El resto de herramientas del pack permanece activo.
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
Start-Process -Wait .\SA-Delirium-0.4.6-Setup.exe -ArgumentList '--verify-source --target "D:\Juegos\GTA San Andreas" --report "D:\compatibilidad.json"'
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

## English — SA Delirium 0.4.6

**SA Delirium** brings GTA IV-inspired character physics (Euphoria/NaturalMotion reimplementation), melee and firearm combat, cover mechanics, and vehicle handling/deformation to classic PC **GTA San Andreas** (Steam AppID 12120 or PC 1.0 US).

### What's New in 0.4.6 (Unified 0.4.0 Deep Fixes + 0.4.1–0.4.5 Installer Updates)

- **CJ Upper-Arm & Shoulder Anatomy**:
  - On CJ's torso and clothing meshes (`player.img`), bones `301` and `302` weight the upper bicep and deltoid at the `UpperArm` (`22`/`32`) socket. They now follow `UpperArm` (`22`/`32`) as authored in `torso.dff` instead of being overwritten by `ped.ifp`'s 96°-rotated NPC breast track (eliminating the Z-zig-zag crease in CJ's arms), and neck-rooted clavicles (`21`/`31`) keep their natural rest slope without shrugging upward.
- **Keyboard Controls & In-Game `Pause → Options` Menu**:
  - **Cover on `B`** on keyboard (`CoverKey=B` in `DeliriumSA.ini`, `R1`/`RB` on gamepad): no longer collides with Right Mouse Button aiming on keyboard/mouse.
  - **PC Keyboard GTA IV Locomotion**: `W, A, S, D` jogs normally, `Alt + W, A, S, D` walks, and holding `Space` sprints directly without button mashing.
  - **Keyboard Melee, Nitro & Gravity**: while aiming/locked on with Right Mouse Button, use `LMB` (punch), `R` or `F` (alternate punch/counter), `Q` or `Space` (kick), and `Shift` or `Alt` (dodge/block); `N` fires nitro separately from vehicle weapons, and holding `K` (0.6s) toggles reduced gravity.
  - **`Pause → Options` Menu**: switch live between `Delirium GTA IV / Enhanced Classic / Izzy` driving, `CJ movement: IV / SA`, `Combat: Delirium / SA`, and `Deformation: on / off`.
- **Cover Camera & Low-Wall Continuity**:
  - Foot cover camera framing now works even when vehicle drive-by shoulder aiming is disabled (`DriveByShoulderAim=0`).
  - Aiming over the opposite shoulder in cover (such as high-left corners) recovers the pre-collision native camera anchor before running the active cover sweep, preventing CJ from disappearing out of frame.
  - CJ can move and reverse direction smoothly along low cover obstacles with rounded COL spheres (`woodenbox1224`) without getting stuck midway, while keeping strict edge, gap, and moving-obstacle checks.
- **Engine & Native Patch Safety**:
  - `FrozenThreads` verifies that all target threads are opened and suspended before writing any native instruction patch, resuming already-paused threads if any step fails.
  - The SDK vehicle box transform API now updates the local collision-center offset alongside the center of mass in `room_clear`.
- **Installer & Compatibility (0.4.1–0.4.6)**:
  - **Corrupted cache recovery**: damaged or incomplete cached archives are renamed to `.invalid-<id>` and re-acquired/verified instead of blocking retries.
  - **Solo Delirium write-access preflight & restore**: checks folder write permissions before extracting the GTA IV ISO and ensures **Restore** targets the Solo game folder accurately.
  - **Standalone shortcut fallback (0.4.5)**: if Windows COM returns `E_NOTIMPL` (`0x80004001`) while creating `.lnk` shortcuts, the installer completes and writes a tracked `SA Delirium.cmd` launcher fallback.
  - **Optional Urbanize fallback (0.4.4)**: if patching *Urbanize*'s `Root.cs` fails, partial files are cleaned up and installation finishes without aborting the full modpack.
  - **Broadened source & loader tolerance (0.4.1–0.4.3)**: accepts copy-only localized `text/*.gxt`, `movies/*.mpg`, `audio/*`, and `stream.ini` variants, preserves existing third-party ASI loaders in **Solo Delirium**, identifies the unpatched retail-disc 1.0 US executable (`8c609f10...`), and keeps *Grinch Trainer* disabled by default (`SA/Documentation/Optional/GrinchTrainerSA.asi.disabled`) to prevent load-time crashes.

### Highlights from 0.4.0

- **Driving & Damage**: Switch live in **Pause → Options** between **Delirium GTA IV** (per-model wheel, transmission, and suspension simulation at 130%) and **Touched-up Classic** (San Andreas handling with visual pitch/roll synchronized with headlights and deferred parts), or select **Izzy 1.6** (applies on restart). Use **L1** to aim and **R1** to shoot from vehicles. Localized panel deformation and pedestrian weight loading on suspension work across modes.
- **Physics, Combat & Movement**: Transient vehicle collision geometry is recycled during long sessions. Hold **L3 + R3** for 0.6s to toggle reduced gravity (50% for CJ and his driven vehicle, 1% for ambient peds and traffic). CJ and police use cover with shoulder-swapping and blind fire.

### Installation & Restore

1. Download **[SA-Delirium-0.4.6-Setup.exe](https://github.com/AdriiSuizo/sa-delirium/releases/latest)** or extract the **RAR** archive (both contain the same setup assistant).
2. Choose **Full installation** (*Instalación completa*) for the complete standalone profile with extras, or **Delirium only** (*Solo Delirium*) to install only the physics/combat core into a compatible PC 1.0 US copy.
3. Select your classic PC **GTA San Andreas** folder/executable and your own **GTA IV Xbox 360 ISO** (USA `6AC07221` or Europe/Asia `4A53F9F6`). Requires 64-bit Windows 10/11 and .NET Framework 4.8. No official Rockstar game files or GTA IV assets are redistributed.
4. If updating from a previous version, click **Restore** (*Restaurar*) in the assistant first before installing the new version.
