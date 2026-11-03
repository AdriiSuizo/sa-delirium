# Verificación de la previa

- Compilación x86 y pruebas de la transformación con datos sintéticos. Cobertura de filas de coche, exclusión de motos y tipos compartidos, comentarios, límites y entrada inválida.
- Prueba de integridad del RAR y comprobación de rutas internas exactas. Se extrajo el RAR en una copia de la instalación de Steam build 459558. El ejecutable original conservó su SHA-256 y los archivos del juego se contrastaron con el origen.
- Arranque de la copia de Steam con y sin el cargador del paquete. Se confirmó la ventana del juego después de seleccionar el modo de vídeo nativo en la instancia de laboratorio.
- La comparación leyó el gestor de handling del proceso de Steam. En LANDSTAL y BRAVURA se comprobó amortiguación de 0,0800 en el control y 0,0864 con el RAR. Se mantuvo la fuerza original de cada muelle. El ASI registró 136 filas. Véase [steam-validation.json](steam-validation.json).

El ensayo comprueba carga del paquete y de los valores de suspensión. Quedan por probar la conducción en una partida nueva y la validación de cada futuro subsistema del port. Esta previa tiene alcance limitado a la suspensión.

## Alpha.2 · laboratorio

- Compilación x86 y pruebas de amortiguación y cabeceo. Las aserciones cubren cuatro casos de apoyo, aceleración y frenada, vehículo volcado, entrada inválida, conservación de ejes ajenos al cabeceo y equivalencia de pasos con igual tiempo total.
- Se volvió a extraer el RAR desde cero sobre una copia de Steam. Coincidieron los hashes de los 416 archivos originales. El RAR tiene siete archivos, sin mods de desarrollo ni la sonda de pruebas.
- Se arrancó ese RAR con la configuración incluida y, en una segunda sesión, con el cabeceo habilitado temporalmente. Solo la segunda instaló el hook. Se leyó la misma amortiguación y fuerza de los muelles en el gestor nativo de ambos procesos. El INI se restauró tras las pruebas.
- En otra sesión con una sonda, el juego aceptó entrada XInput de laboratorio y procesó al menos 600 actualizaciones nativas de coches durante el comienzo de una partida. Se observaron coches con cuatro ruedas en contacto, de las cuales dos eran motrices. La sonda no registró a CJ conduciendo. En la repetición, la captura de ventana agotó el plazo y no hubo muestras de entrada o de vehículos. Estos casos no validan control aéreo, aterrizajes ni conducción.

El RAR de alpha.2 es un artefacto de laboratorio. El borrador de distribución permanece en alpha.1. Véase [steam-alpha2-validation.json](steam-alpha2-validation.json) para las comprobaciones realizadas.
