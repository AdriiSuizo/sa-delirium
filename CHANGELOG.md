# Historial

## En desarrollo Â· port nativo de Steam

- ConexiÃ³n visual propia mediante la API de rutas fÃ­sicas de UAL: composiciÃ³n streaming de los IMG del propietario, integridad, cachÃ© y registro transaccional. 733 reemplazos locales probados con un actor fÃ­sico nativo; son contenido personal, no el modpack final.
- Constructor de RAR con manifiesto cerrado de datos y lista UTF-16, evitando el lÃ­mite de longitud de comandos de Windows.
- El capturador de pruebas arranca sin ventana de consola; se conserva el caso anterior que pausÃ³ el juego antes del Ãºltimo disparo.

- Adaptador de juego enlazado con 148 referencias verificadas, direcciÃ³n relativa al mÃ³dulo y huellas compatibles con ASLR, sin modificar el EXE. Se conserva el cÃ³digo de juego existente y se separan las herramientas de captura experimentales.
- CachÃ© v3 completa: 17 diccionarios originales de IV, tablas/decisiones, esqueleto y doce tÃ©cnicas generadas desde el SA del propietario; 418 clips en total. ValidaciÃ³n de integridad antes de publicar la disponibilidad del motor.
- Primer importado y reapertura comprobados sin red ni clave en APPDATA, con soporte de formato XEX de Xenia acreditado bajo BSD.
- Prueba real de Steam con un NPC nativo: daÃ±o no letal, recuperaciÃ³n completa, muerte y reacciÃ³n fÃ­sica del mismo cadÃ¡ver al recibir otro disparo. Conserva su alcance: todavÃ­a no certifica combate, vehÃ­culos o modpack completo.
- Conservado el cÃ³digo del motor y del combate existentes en una copia fuente independiente, compilable para x86 sin el antiguo payload de constantes.
- Incorporada la correcciÃ³n de pivotes adaptados de Delirium 0.6.0, con prueba que falla antes del cambio y 93 pruebas del nÃºcleo y 68 del SDK correctas despuÃ©s.
- Comprobadas en Steam original la lectura del esqueleto de CJ, la importaciÃ³n de la colisiÃ³n real del interior y una simulaciÃ³n de 240 pasos. Son pruebas delimitadas del laboratorio, no jugabilidad completa ni contenido del RAR alpha.4.
- Ranura de pruebas convertida de forma reversible a Steam; no requiere sustituir `main.scm` ni tocar la partida original.

## 0.1.0-alpha.4 Â· laboratorio

- Modo sin bordes que crea el dispositivo Direct3D en ventana y mantiene el modo del escritorio.
- La cÃ¡mara nativa recibe el stick derecho XInput durante la entrada analÃ³gica aunque estÃ© seleccionado el modo de ratÃ³n. Se conservan las preferencias originales fuera del procesamiento de cÃ¡mara.
- Se mantiene el lÃ­mite nativo de fotogramas. Los fixes de temporizaciÃ³n de la copia de desarrollo necesitan adaptaciÃ³n a Steam antes de cambiar ese lÃ­mite.

## 0.1.0-alpha.3 Â· laboratorio

- El RAR incorpora el preparador x86 de Delirium y su licencia. La ventana del primer arranque permite reintentar despuÃ©s de colocar la ISO.
- El ajuste de iluminaciÃ³n usa `data/timecycp.dat` de la instalaciÃ³n local mediante una superposiciÃ³n en memoria. Un filtro propio aplica la modulaciÃ³n de color PS2 sin desenfoque. Ambos pueden desactivarse independientemente de la suspensiÃ³n.
- Sigue pendiente conectar Delirium a los peatones de Steam. Esta previa no se considera una versiÃ³n jugable del port completo.

## Desarrollo Â· importaciÃ³n de la ISO

- Preparador del SDK Delirium 0.6.0 desde una imagen de GTA IV de Xbox 360 aportada por el usuario.
- CachÃ© local por instalaciÃ³n. El segundo arranque puede inicializar el SDK sin la ISO ni los archivos de la instalaciÃ³n de GTA IV.
- ValidaciÃ³n de la ediciÃ³n mediante la huella comprobada por el propio SDK.
- Mapa de lecturas para la ediciÃ³n comercial Europa/Asia y prueba de primer arranque con esa ISO. La sonda de Steam ya lee las jerarquÃ­as animadas de un civil y de CJ.

## 0.1.0-alpha.2 Â· laboratorio

- Identificada la actualizaciÃ³n nativa de coches en Steam build 459558.
- Implementado un prototipo propio de cabeceo aÃ©reo con el acelerador y el freno. Se mantiene desactivado por defecto.
- AÃ±adidas pruebas de ejes de rotaciÃ³n, contacto de las ruedas y paso temporal.

## 0.1.0-alpha.1

- Primera previa tÃ©cnica para el binario original de Steam clÃ¡sico.
- AdaptaciÃ³n propia de amortiguaciÃ³n de coches mediante datos nativos cargados en memoria.
- ConstrucciÃ³n x86 y creaciÃ³n del RAR con lista cerrada de archivos.
