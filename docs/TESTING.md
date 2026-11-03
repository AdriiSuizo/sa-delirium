# Verificación de la previa

- Compilación x86 y pruebas de la transformación con datos sintéticos. Cobertura de filas de coche, exclusión de motos y tipos compartidos, comentarios, límites y entrada inválida.
- Prueba de integridad del RAR y comprobación de rutas internas exactas. Se extrajo el RAR en una copia de la instalación de Steam build 459558. El ejecutable original conservó su SHA-256 y los archivos del juego se contrastaron con el origen.
- Arranque de la copia de Steam con y sin el cargador del paquete. Se confirmó la ventana del juego después de seleccionar el modo de vídeo nativo en la instancia de laboratorio.
- La comparación leyó el gestor de handling del proceso de Steam. En LANDSTAL y BRAVURA se comprobó amortiguación de 0,0800 en el control y 0,0864 con el RAR. Se mantuvo la fuerza original de cada muelle. El ASI registró 136 filas. Véase [steam-validation.json](steam-validation.json).

El ensayo comprueba carga del paquete y de los valores de suspensión. Quedan por probar la conducción en una partida nueva y la validación de cada futuro subsistema del port. Esta previa tiene alcance limitado a la suspensión.
