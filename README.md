# SA Delirium

San Andreas clásico con Delirium, fixes, aspecto PS2, extras de desarrollo
y Lively Vehicle Physics1.6 de Izzy. Se instala con un asistente desde tu copia del
juego, de Steam o de PC 1.0 US. Con Steam se abre con **Jugar**; con otra copia, con
el acceso directo **SA Delirium**.
Los mods seleccionados vienen incluidos, salvo Urbanize, que se obtiene automáticamente de su autor.

## Instalar

Descarga **SA-Delirium-0.3.3-Setup.exe** o el RAR de la
[entrega0.3.3](https://github.com/AdriiSuizo/sa-delirium/releases/tag/v0.3.3).
Extrae el RAR si elegiste ese formato y abre el EXE. El asistente detecta Steam;
si tu juego está en otra carpeta, elígela. Elige la ISO si falta su ruta y pulsa
**Instalar**.

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
No necesitas la carpeta de desarrollo ni otra instalación de GTA IV de PC.
Solo Urbanize requiere conexión durante la instalación; el asistente lo descarga
automáticamente de su distribución original, sin pedir otro archivo. Los recursos de GTA IV
se generan en tu equipo desde la ISO; después puedes quitarla de esa ruta.

Se requiere espacio para el perfil y su preparación temporal (aproximadamente
15GB adicionales). Las rutas de instalación deben admitir el límite nativo de
SA1.0 para los nombres de sus partidas; el asistente comprueba ese límite.
Si faltan DirectX9 D3DX/XInput, el asistente ofrece el redistribuible original de
Microsoft incluido; su instalador puede pedir permisos de administrador.

## Contenido

Incluye Delirium y la integración de físicas/combate/movimientos, Izzy Lively
Vehicle Physics1.6 con sus ajustes originales de puertas, SkyGfx Extended1.7,
timecycle PS2, Project2DFX, NextGen Remaster Effects/FxsFuncs1.1 y Enhance
ParticleTXD. También CLEO/CLEO+, Mod Loader, SilentPatch, GInput, Widescreen Fix,
Windowed Mode, Open Limit Adjuster, Framerate Vigilante y PortableGTA.
DXVK se comprueba en el equipo; si no puede crear un dispositivo, se usa D3D9
de Windows. Los créditos y readmes originales se instalan en `SA/Documentation`.

Se conservan Urbanize, Grinch Trainer, Advanced Aiming, HD Skin Pack299,
BSOR/LOD Vegetation, NextGen Grass, SA Brightened, Jefferson Retextured, Parallax
Interiors, neones/lámparas, Rubbish GTA4 y los eventos civiles de desarrollo.
La configuración de combate, físicas y movimientos de Delirium se conserva.

Quedan fuera **ambos packs de armas añadidos** (GTA V Weapons y NeXtGen Remaster
Weapons), RoSA/Proper Player Retex, GTA IV Textures and HQ Roads y el
HUD/minimapa/rueda de GTA V. Las armas usan los recursos de San Andreas de Steam.
Esta selección corresponde a la aclaración del usuario: «Todo menos las armas»,
conservando sus exclusiones anteriores. La entrega0.3.0 era un perfil más reducido.

Urbanize se mantiene separado según su licencia, que prohíbe reempaquetarlo en
modpacks desde octubre2026. El asistente obtiene su original y aplica los ajustes
locales verificados. Se instalan sus readmes/licencia y se acredita a
[Junior_Djjr](https://www.mixmods.com.br/2026/02/urbanize/).

## Partidas, restauración y diagnóstico

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

La entrega se comprueba desde416 archivos propios de Steam y una ISO externa,
con caché vacía, mediante el EXE literal y con los extras embebidos y Urbanize descargado de su autor. Se comprueban
arranque sin puente, intro de partida nueva, daño nativo, efectos, conservación
de partidas, restauración y Jugar en Steam. La cobertura concreta y las huellas
se documentan en [VALIDATION.md](installer/portable/VALIDATION-0.3.3.md).

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
