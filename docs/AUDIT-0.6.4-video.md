# Pruebas del hotfix de vídeo de SA Delirium 0.6.4

El Widescreen Fix incluido puede solicitar una resolución exacta durante el arranque. El ejecutable clásico PC 1.0 US muestra el aviso y termina si la búsqueda agota la lista de modos. El adaptador de Delirium interviene al terminar esa búsqueda. Consulta la lista del adaptador de RenderWare, selecciona un modo válido y devuelve el control a la ruta de vídeo normal. La ruta en la que el modo exacto aparece conserva el comportamiento del juego. [Código de Widescreen Fix incluido](https://github.com/ThirteenAG/WidescreenFixesPack/blob/f77291e/source/GTASA.WidescreenFix/Legacy.ixx).

La prueba utilizó una instalación completa aislada con el ejecutable clásico 1.0 US. Se forzó el selector de un adaptador a buscar un tamaño no enumerado en una pantalla de 2560 × 1440, sin alterar el monitor. La 0.6.3 mostró «Cannot find 2560x1440x32 video mode». Con la misma prueba, la 0.6.4 eligió el modo de 2560 × 1440 e inició el vídeo de entrada. Se repitió la búsqueda forzada en ventana con tamaños próximos a 1080p y 720p. Las demás pruebas en ventana y sin bordes usaron el arranque normal. Las dimensiones se comprobaron en el cliente y en RenderWare.

| Renderizador | Ventana | Solicitud de prueba | Tamaño observado |
| --- | --- | --- | --- |
| DXVK | Pantalla completa | 2561 × 1441 | 2560 × 1440 |
| DXVK | Ventana | 1280 × 720 | 1280 × 720 |
| DXVK | Ventana | 1920 × 1080 | 1920 × 1080 |
| DXVK | Sin bordes | Automática | 2560 × 1440 |
| Direct3D 9 nativo | Pantalla completa | 2561 × 1441 | 2560 × 1440 |
| Direct3D 9 nativo | Ventana | 1024 × 768 | 1024 × 768 |
| Direct3D 9 nativo | Ventana | 1280 × 720 | 1280 × 720 |
| DXVK | Ventana | 1366 × 768 | 1366 × 768 |
| DXVK | Ventana | 1920 × 1200 | 1920 × 1200 |
| DXVK | Ventana, modo no enumerado | 1921 × 1081 | 1920 × 1080 |
| Direct3D 9 nativo | Ventana, modo no enumerado | 1281 × 721 | 1280 × 720 |

Se comprobaron el arranque y la creación de la ventana en los dos renderizadores en el mismo PC.
