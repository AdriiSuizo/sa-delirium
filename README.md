# SA Delirium

San Andreas clásico de Steam con Delirium, fixes, aspecto PS2 y Lively Vehicle
Physics1.6 de Izzy. Se instala con un asistente y se abre con **Jugar** en Steam.
Los plugins seleccionados vienen dentro del instalador.

## Instalar

Descarga **SA-Delirium-0.3.0-Setup.exe** o el RAR de la
[entrega0.3.0](https://github.com/AdriiSuizo/sa-delirium/releases/tag/v0.3.0).
Extrae el RAR si elegiste ese formato y abre el EXE. El asistente detecta Steam;
elige la ISO si falta su ruta y pulsa **Instalar**. Después usa **Jugar**.

Necesitas Windows10/11 x64 con .NET Framework4.8, tu **GTA San Andreas clásico
de Steam, AppID12120**, y una **ISO compatible propia de GTA IV para Xbox360**.
La Definitive Edition y otras ediciones de SA no son esta entrega. La instalación
verifica la versión; si Steam cambia los archivos, se detiene con un error.
No necesitas la carpeta de desarrollo ni otra instalación de GTA IV de PC.
Los mods no requieren descargas durante la instalación. Los recursos de GTA IV
se generan en tu equipo desde la ISO; después puedes quitarla de esa ruta.

Se requiere espacio para el perfil y su preparación temporal (aproximadamente
11GB adicionales). Las rutas de instalación deben admitir el límite nativo de
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

Quedan fuera RoSA/Proper Player Retex, GTA IV Textures and HQ Roads, el
HUD/minimapa/rueda de armas de GTA V y los extras HD, Urbanize, trainer y armas
de GTA V del perfil de desarrollo. Este es el perfil clásico de fixes/PS2/físicas.

## Partidas, restauración y diagnóstico

El mod crea su perfil independiente en **SA** dentro de la carpeta de Steam.
Se adapta la versión interna del juego para los plugins y se respalda el
`gta-sa.exe` original. Un lanzador espera a que termine el juego para mantener
su sesión en Steam. La carpeta de desarrollo y las partidas de Documentos se
conservan. El instalador no incluye ni importa partidas del autor.

Para desinstalar, abre el mismo asistente y pulsa **Restaurar Steam**. Recupera
el ejecutable anterior y conserva el perfil y sus partidas en el respaldo
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
con caché vacía, mediante el EXE literal y con los mods embebidos. Se comprueban
arranque sin puente, intro de partida nueva, daño nativo, efectos, conservación
de partidas, restauración y Jugar en Steam. La cobertura concreta y las huellas
se documentan en [VALIDATION.md](installer/portable/VALIDATION.md).

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
