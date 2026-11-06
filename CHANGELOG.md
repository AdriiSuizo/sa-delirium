# Historial

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
