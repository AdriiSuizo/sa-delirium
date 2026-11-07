# Historial

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
