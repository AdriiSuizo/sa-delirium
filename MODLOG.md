# Registro técnico

- El port parte del proyecto de desarrollo y de una copia limpia del Steam clásico build 459558. Ambas instalaciones originales se tratan como fuentes de solo lectura.
- El prototipo inicial de empaquetado aplastaba el árbol de rutas en RAR. Se corrigió y se añadió una comprobación posterior sobre las rutas del archivo empaquetado.
- La previa aísla la primera adaptación de vehículos en el nuevo ejecutable sin resolver direcciones de la revisión anterior. El cargador proporciona una vista temporal de los datos del propio juego. Los archivos originales se comprueban por hash.
- La integración completa se divide en port de ABI por subsistema, motor autónomo, equivalentes de animación y decisiones, y validación de paridad en Steam.
