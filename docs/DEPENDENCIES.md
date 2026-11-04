# Proveniencia y dependencias

## En la previa

| Componente | Función | Licencia y origen |
| --- | --- | --- |
| SAStandalone | Aplica amortiguación adicional en las filas de coches de los datos nativos que el jugador ya tiene instalados. | Código de esta rama. Véase LICENSE.md. |
| Ciclo de iluminación y color PS2 | Presenta el `timecycp.dat` de la instalación local al lector de Steam y aplica la modulación en la pasada nativa de posprocesado. | Código propio y datos del juego leídos en el equipo del jugador. |
| Preparador de Delirium | Valida la ISO de Xbox 360 y prepara la caché canónica para el motor adaptado existente. La rama nueva comparte una sola DLL y caché v2. | GPLv3 y avisos de Delirium. Código en `bootstrap` y `vendor`, huellas en `tools/Verify-Vendor.py`. Alpha.4 conserva el preparador anterior. |
| Ventana sin bordes y cámara XInput | Usa el escritorio actual para el dispositivo D3D9 y activa la ruta de cámara de mando de Steam al mover el stick derecho. | Código propio. |
| Ultimate ASI Loader 10.0.2 Win32 | Carga el ASI y sirve la vista en memoria de handling.cfg. | MIT, ThirteenAG y colaboradores. [Release original](https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases/tag/v10.0.2) |

El empaquetador usa una lista cerrada de tres binarios, un INI y los textos de instalación, créditos y licencias. El juego aporta sus datos en cada arranque.

## Selección visual para la futura versión

| Grupo | Estado en Steam |
| --- | --- |
| PS2 Time Cycle | Activado en alpha.3 con la tabla de la instalación del jugador. |
| SkyGfx Extended | Alpha.3 implementa su propio filtro de color sin desenfoque. Quedan pendientes radiosidad y pipelines de edificios y vehículos, así como acreditar la redistribución del binario de terceros. [Fuente](https://www.mixmods.com.br/2024/03/sa-skygfx/) |
| RoSA Evolved y Proper Player Retex | La publicación de RoSA prohíbe volver a subir el paquete. Para distribuir texturas dentro del RAR hacen falta equivalentes propios o con licencia verificable. [Licencia publicada por la autora](https://www.mixmods.com.br/2023/07/rosa-project-evolved-remaster-texturas-hd/) |
| Project2DFX | Código MIT. Su implementación de SA usa direcciones fijas de otro ejecutable. Las luces lejanas requieren un port y una prueba en el Steam original. [Código del plugin](https://github.com/ThirteenAG/III.VC.SA.IV.Project2DFX/blob/master/source/SALodLights/dllmain.cpp) |
| BSOR, LOD Vegetation y NextGen Grass | Pendientes los shaders de viento, el cargador compatible y la licencia de cada paquete. |
| FxsFuncs, NextGen Effects y Enhance ParticleTXD | Pendientes la compatibilidad de hooks, el cargador de scripts y la procedencia de recursos. |
| Improved Lamps, Brightened, LV Neon, Parallax y Jefferson | Pendientes los reemplazos de modelos, las capas de mapa y sus licencias. |
| GTA IV Textures/HQ Roads, GTA IV Rubbish, packs de armas y skins | Revisión pendiente de los archivos derivados de juegos y de la licencia de cada aporte original. |
| Rueda de armas de GTA V y DLSS | Fuera de la selección de distribución. |
| Resto de la interfaz activa de desarrollo | Pendiente separar mapa y HUD de la rueda de armas, acreditar permisos y probar las opciones de interfaz en Steam. |

## Port completo

| Grupo | Situación de distribución |
| --- | --- |
| Adaptador y motor Delirium | El adaptador de desarrollo comprueba la imagen 0x1177000 y usa direcciones de esa revisión. Steam build 459558 tiene imagen 0x985000. Hace falta identificar ABI, sitios de hooks y ciclo nativo de Steam antes de activar esos sistemas. Delirium Physics es GPLv3. Una distribución del motor modificado necesita su correspondiente código fuente y avisos. |
| Datos requeridos por el motor | El preparador importa el programa, el personaje y los parámetros desde la ISO del jugador. El cuerpo de 21 segmentos se genera en esa instalación. Los datos importados permanecen en su caché local. |
| Combate, locomoción y decisiones | Las fuentes adaptadas existentes se conservan en `vendor`. Los recursos derivados de GTA IV deben importarse de la ISO del jugador durante la preparación local, sin incluirlos en el RAR ni reemplazar las reglas existentes por aproximaciones. Falta ampliar el importador y conectar los enlaces nativos de Steam. |
| Izzy's Lively Vehicle Physics | El ASI actual es externo. La suspensión de esta previa se implementa en código propio y conserva la dirección, motor, frenos y neumáticos originales. Balanceo dinámico, efectos de choque, suspensión dañada, motos, puertas y control en el aire requieren port o sustitución independiente. |
| Correcciones y controles | SilentPatch, Framerate Vigilante, GInput, Windowed Mode, Widescreen Fix, limit adjuster, PortableGTA y Advanced Aiming Mod siguen en desarrollo. Las piezas necesarias para el port completo requieren auditoría de licencias, conflicto de hooks y verificación individual en Steam. |
| Urbanize, CLEO y FxsFuncs | Sigue pendiente la separación de rutinas de peatones y efectos del contenido y cargadores externos de desarrollo. |
| Texturas, modelos, sonidos, HUD y trainer | Se excluyen de esta previa. Incluye la familia RoSA, BSOR, NextGen, mejoras de mapa y partículas, packs de armas, vehículos y peatones, HUD y trainer. |

Las copias de Steam y de desarrollo son referencias de prueba locales. La previa no contiene ejecutables originales ni adaptados, partidas, archivos de GTA IV, animaciones del juego ni mods del montaje de desarrollo.

## Comprobaciones de candidatos

- [Delirium Physics 0.6.0](https://github.com/PVCPC/delirium-physics/tree/v0.6.0) obtiene las constantes del programa de GTA IV. El preparador conserva ese requisito mediante la ISO del jugador y valida la caché con `nm_unlock`, `nm_init` y `nm_load_task_params`.
- [SilentPatchSA Build 34.1](https://github.com/CookiePLMonster/SilentPatch/releases/tag/1.1-BUILD34.1-SA) ofrece compatibilidad parcial con el ejecutable de Steam de diciembre de 2014. El ZIP del laboratorio coincide con el digest oficial. La fuente y el recurso lunar se publican en el repositorio MIT del autor. Está en el laboratorio, todavía fuera de alpha.4; queda integrar su licencia y probar el paquete final. No atribuirle compatibilidad de partidas con scripts distintos: la ranura de pruebas se convierte por separado al formato nativo de Steam.
- La prueba del cabeceo aéreo de alpha.2 usa código propio, una tabla virtual comprobada y el contador nativo de las cuatro ruedas. Está desactivada por defecto mientras falta la conducción. El binario de Izzy sigue fuera del paquete.
