# Proveniencia y dependencias

## En la previa

| Componente | Función | Licencia y origen |
| --- | --- | --- |
| SAStandalone | Aplica amortiguación adicional en las filas de coches de los datos nativos que el jugador ya tiene instalados. | Código de esta rama. Véase LICENSE.md. |
| Ultimate ASI Loader 10.0.2 Win32 | Carga el ASI y sirve la vista en memoria de handling.cfg. | MIT, ThirteenAG y colaboradores. [Release original](https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases/tag/v10.0.2) |

El empaquetador usa una lista cerrada. Solo contiene esos dos binarios, el INI y los textos de instalación y licencias. El juego aporta sus propios datos en cada arranque.

## Port completo

| Grupo | Situación de distribución |
| --- | --- |
| Adaptador y motor Delirium | El adaptador de desarrollo comprueba la imagen 0x1177000 y usa direcciones de esa revisión. Steam build 459558 tiene imagen 0x985000. Hace falta identificar ABI, sitios de hooks y ciclo nativo de Steam antes de activar esos sistemas. Delirium Physics es GPLv3. Una distribución del motor modificado necesita su correspondiente código fuente y avisos. |
| Datos requeridos por el motor | El preparador importa el programa, el personaje y los parámetros desde la ISO del jugador. El cuerpo de 21 segmentos se genera en esa instalación. Los datos importados permanecen en su caché local. |
| Combate, locomoción y decisiones | Las animaciones XAD, tablas de combate, perfiles y decisiones actuales incluyen datos derivados de GTA IV o de activos nativos. Hay que sustituirlos con animaciones propias o la evaluación de recursos de SA en la instalación del jugador, y con reglas autónomas. |
| Izzy's Lively Vehicle Physics | El ASI actual es externo. La suspensión de esta previa se implementa en código propio y conserva la dirección, motor, frenos y neumáticos originales. Balanceo dinámico, efectos de choque, suspensión dañada, motos, puertas y control en el aire requieren port o sustitución independiente. |
| Correcciones y controles | SilentPatch, Framerate Vigilante, GInput, Windowed Mode, Widescreen Fix, limit adjuster, PortableGTA y Advanced Aiming Mod siguen en desarrollo. Las piezas necesarias para el port completo requieren auditoría de licencias, conflicto de hooks y verificación individual en Steam. |
| Urbanize, CLEO y FxsFuncs | Sigue pendiente la separación de rutinas de peatones y efectos del contenido y cargadores externos de desarrollo. |
| Texturas, modelos, sonidos, HUD y trainer | Se excluyen de esta previa. Incluye la familia RoSA, BSOR, NextGen, mejoras de mapa y partículas, packs de armas, vehículos y peatones, HUD y trainer. |

Las copias de Steam y de desarrollo son referencias de prueba locales. La previa no contiene ejecutables originales ni adaptados, partidas, archivos de GTA IV, animaciones del juego ni mods del montaje de desarrollo.

## Comprobaciones de candidatos

- [Delirium Physics 0.6.0](https://github.com/PVCPC/delirium-physics/tree/v0.6.0) obtiene las constantes del programa de GTA IV. El preparador conserva ese requisito mediante la ISO del jugador y valida la caché con `nm_unlock`, `nm_init` y `nm_load_task_params`.
- [SilentPatchSA Build 34.1](https://github.com/CookiePLMonster/SilentPatch/releases/tag/1.1-BUILD34.1-SA) ofrece compatibilidad parcial con el ejecutable de Steam de diciembre de 2014. El paquete actual omite el ASI mientras se verifica la procedencia de su textura lunar embebida y el inventario de licencias de las bibliotecas que enlaza.
- La prueba del cabeceo aéreo de alpha.2 usa código propio, una tabla virtual comprobada y el contador nativo de las cuatro ruedas. Está desactivada por defecto mientras falta la conducción. El binario de Izzy sigue fuera del paquete.
