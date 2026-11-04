# Proveniencia y dependencias

## En la previa

| Componente | FunciÃ³n | Licencia y origen |
| --- | --- | --- |
| SAStandalone | Aplica amortiguaciÃ³n adicional en las filas de coches de los datos nativos que el jugador ya tiene instalados. | CÃ³digo de esta rama. VÃ©ase LICENSE.md. |
| Ciclo de iluminaciÃ³n y color PS2 | Presenta el `timecycp.dat` de la instalaciÃ³n local al lector de Steam y aplica la modulaciÃ³n en la pasada nativa de posprocesado. | CÃ³digo propio y datos del juego leÃ­dos en el equipo del jugador. |
| Preparador de Delirium | Valida la ISO de Xbox 360 y prepara la cachÃ© canÃ³nica para el motor adaptado existente. La rama nueva comparte una sola DLL y cachÃ© v3: incluye el banco de movimiento/combate y genera localmente las tÃ©cnicas de los gimnasios desde el SA instalado. | GPLv3 y avisos de Delirium. CÃ³digo en `bootstrap` y `vendor`, huellas en `tools/Verify-Vendor.py`. Alpha.4 conserva el preparador anterior. |
| Ventana sin bordes y cÃ¡mara XInput | Usa el escritorio actual para el dispositivo D3D9 y activa la ruta de cÃ¡mara de mando de Steam al mover el stick derecho. | CÃ³digo propio. |
| Ultimate ASI Loader 10.0.2 Win32 | Carga el ASI y sirve la vista en memoria de handling.cfg. | MIT, ThirteenAG y colaboradores. [Release original](https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases/tag/v10.0.2) |

El empaquetador usa una lista cerrada de tres binarios, un INI y los textos de instalaciÃ³n, crÃ©ditos y licencias. El juego aporta sus datos en cada arranque.

## SelecciÃ³n visual para la futura versiÃ³n

| Grupo | Estado en Steam |
| --- | --- |
| PS2 Time Cycle | Activado en alpha.3 con la tabla de la instalaciÃ³n del jugador. |
| SkyGfx Extended | Alpha.3 implementa su propio filtro de color sin desenfoque. Quedan pendientes radiosidad y pipelines de edificios y vehÃ­culos, asÃ­ como acreditar la redistribuciÃ³n del binario de terceros. [Fuente](https://www.mixmods.com.br/2024/03/sa-skygfx/) |
| RoSA Evolved y Proper Player Retex | La publicaciÃ³n de RoSA prohÃ­be volver a subir el paquete. Para distribuir texturas dentro del RAR hacen falta equivalentes propios o con licencia verificable. [Licencia publicada por la autora](https://www.mixmods.com.br/2023/07/rosa-project-evolved-remaster-texturas-hd/) |
| Project2DFX | CÃ³digo MIT. Su implementaciÃ³n de SA usa direcciones fijas de otro ejecutable. Las luces lejanas requieren un port y una prueba en el Steam original. [CÃ³digo del plugin](https://github.com/ThirteenAG/III.VC.SA.IV.Project2DFX/blob/master/source/SALodLights/dllmain.cpp) |
| BSOR, LOD Vegetation y NextGen Grass | Pendientes los shaders de viento, el cargador compatible y la licencia de cada paquete. |
| FxsFuncs, NextGen Effects y Enhance ParticleTXD | Pendientes la compatibilidad de hooks, el cargador de scripts y la procedencia de recursos. |
| Improved Lamps, Brightened, LV Neon, Parallax y Jefferson | Pendientes los reemplazos de modelos, las capas de mapa y sus licencias. |
| GTA IV Textures/HQ Roads, GTA IV Rubbish, packs de armas y skins | RevisiÃ³n pendiente de los archivos derivados de juegos y de la licencia de cada aporte original. |
| Rueda de armas de GTA V y DLSS | Fuera de la selecciÃ³n de distribuciÃ³n. |
| Resto de la interfaz activa de desarrollo | Pendiente separar mapa y HUD de la rueda de armas, acreditar permisos y probar las opciones de interfaz en Steam. |

## Port completo

| Grupo | SituaciÃ³n de distribuciÃ³n |
| --- | --- |
| Adaptador y motor Delirium | El adaptador de desarrollo comprueba la imagen 0x1177000 y usa direcciones de esa revisiÃ³n. Steam build 459558 tiene imagen 0x985000. Hace falta identificar ABI, sitios de hooks y ciclo nativo de Steam antes de activar esos sistemas. Delirium Physics es GPLv3. Una distribuciÃ³n del motor modificado necesita su correspondiente cÃ³digo fuente y avisos. |
| Datos requeridos por el motor | El preparador importa el programa, el personaje y los parÃ¡metros desde la ISO del jugador. El cuerpo de 21 segmentos se genera en esa instalaciÃ³n. Los datos importados permanecen en su cachÃ© local. |
| Combate, locomociÃ³n y decisiones | Fuentes existentes en `vendor`. La cachÃ© v3 importa de la ISO 17 diccionarios y las tablas/decisiones necesarias, y genera doce tÃ©cnicas desde el SA instalado. Los 418 clips totales, el esqueleto y las tablas coinciden con el banco de desarrollo; las decisiones solo cambian de saltos de lÃ­nea. Se verificÃ³ tambiÃ©n `nm_combat_load/enable` en los dos arranques de Steam. Sigue pendiente conectar la propiedad de actores, daÃ±o, IA y vehÃ­culos: cargar el banco no acredita esa jugabilidad. |
| Izzy's Lively Vehicle Physics | El ASI actual es externo. La suspensiÃ³n de esta previa se implementa en cÃ³digo propio y conserva la direcciÃ³n, motor, frenos y neumÃ¡ticos originales. Balanceo dinÃ¡mico, efectos de choque, suspensiÃ³n daÃ±ada, motos, puertas y control en el aire requieren port o sustituciÃ³n independiente. |
| Correcciones y controles | SilentPatch, Framerate Vigilante, GInput, Windowed Mode, Widescreen Fix, limit adjuster, PortableGTA y Advanced Aiming Mod siguen en desarrollo. Las piezas necesarias para el port completo requieren auditorÃ­a de licencias, conflicto de hooks y verificaciÃ³n individual en Steam. |
| Urbanize, CLEO y FxsFuncs | Sigue pendiente la separaciÃ³n de rutinas de peatones y efectos del contenido y cargadores externos de desarrollo. |
| Texturas, modelos, sonidos, HUD y trainer | Se excluyen de esta previa. Incluye la familia RoSA, BSOR, NextGen, mejoras de mapa y partÃ­culas, packs de armas, vehÃ­culos y peatones, HUD y trainer. |

Las copias de Steam y de desarrollo son referencias de prueba locales. La previa no contiene ejecutables originales ni adaptados, partidas, archivos de GTA IV, animaciones del juego ni mods del montaje de desarrollo.

## Comprobaciones de candidatos

- [Delirium Physics 0.6.0](https://github.com/PVCPC/delirium-physics/tree/v0.6.0) obtiene las constantes del programa de GTA IV. El preparador conserva ese requisito mediante la ISO del jugador y valida la cachÃ© con `nm_unlock`, `nm_init` y `nm_load_task_params`.
- [SilentPatchSA Build 34.1](https://github.com/CookiePLMonster/SilentPatch/releases/tag/1.1-BUILD34.1-SA) ofrece compatibilidad parcial con el ejecutable de Steam de diciembre de 2014. El ZIP del laboratorio coincide con el digest oficial. La fuente y el recurso lunar se publican en el repositorio MIT del autor. EstÃ¡ en el laboratorio, todavÃ­a fuera de alpha.4; queda integrar su licencia y probar el paquete final. No atribuirle compatibilidad de partidas con scripts distintos: la ranura de pruebas se convierte por separado al formato nativo de Steam.
- La prueba del cabeceo aÃ©reo de alpha.2 usa cÃ³digo propio, una tabla virtual comprobada y el contador nativo de las cuatro ruedas. EstÃ¡ desactivada por defecto mientras falta la conducciÃ³n. El binario de Izzy sigue fuera del paquete.

La importación offline actual usa soporte público de formato XEX de Xenia, BSD-3-Clause, con aviso completo y revisión fijada. No necesita una clave en APPDATA ni una descarga en el primer arranque. `licenses/Rust-Dependencies.txt` conserva los avisos de los 129 paquetes activos del grafo Win32 bloqueado. Los recursos de las ISOs se generan solo en el equipo del propietario.
