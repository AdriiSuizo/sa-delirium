# Registro técnico

## Inicio y mando en la instalación real de Steam

- D3D9 se importa en RVA `0x4a0374`. La corrección preserva la llamada anterior y convierte la creación y los resets del dispositivo a ventana con el modo de escritorio. Se validó la instalación real desde el cliente de Steam con 2560 × 1440 antes y después.
- La consulta XInput de la ruta nativa está en RVA `0x37c982` y usa la importación visible para Steam Input. Se observó que la cámara conserva el modo ratón mientras recibe los dos ejes analógicos. La adaptación selecciona temporalmente el camino nativo de stick durante la actualización de cámara, respetando los demás consumidores de entrada. Las direcciones se validan para el ejecutable Steam 459558.

## Filtro de color de Steam

- El lector de iluminación de Steam está en RVA `0x1d8f70`. Los datos originales incluyen `data/timecycp.dat`. El cargador virtual presenta ese archivo como entrada de `timecyc.dat` en memoria. La primera tabla alfa comprobada está en RVA `0x80d7a8`.
- La llamada a la pasada de color está en RVA `0x34438b` y originalmente llama a `0x3428d0`. Se comprueban la huella completa del ejecutable y los cinco bytes de la llamada. La pasada propia usa el raster de la escena y multiplica los canales conforme al rango de modulación PS2, sin desenfoque. Usa la ruta original para visión nocturna e infrarroja.
- Se obtuvieron capturas A/B en la escena inicial a la misma hora de juego. Quedan por portar radiosidad, reflejos, vegetación y luces LOD. La selección visual y la procedencia están en `docs/DEPENDENCIES.md`.

- El port parte del proyecto de desarrollo y de una copia limpia del Steam clásico build 459558. Ambas instalaciones originales se tratan como fuentes de solo lectura.
- El prototipo inicial de empaquetado aplastaba el árbol de rutas en RAR. Se corrigió y se añadió una comprobación posterior sobre las rutas del archivo empaquetado.
- La previa aísla la primera adaptación de vehículos en el nuevo ejecutable sin resolver direcciones de la revisión anterior. El cargador proporciona una vista temporal de los datos del propio juego. Los archivos originales se comprueban por hash.
- La integración completa se divide en port de ABI por subsistema, motor autónomo, equivalentes de animación y decisiones, y validación de paridad en Steam.

## Actualización nativa de coches en Steam 459558

- Tabla virtual de `CAutomobile` en RVA `0x4bc49c`, con el método de actualización en la entrada 10 y RVA `0x2dfe40`. Verificada mediante correspondencia de entradas adyacentes y comportamiento del método. La instalación comprueba la huella del ejecutable y el puntero original antes de cambiar la entrada.
- El bucle de ruedas incrementa el byte `+0x960` al detectar contacto y el byte `+0x961` solo para las ruedas motrices correspondientes a la transmisión. El segundo valor puede ser cero con ruedas apoyadas. El cabeceo solo usa el primero. Una sonda de laboratorio llamó al método original y observó 4 contactos y 2 ruedas motrices durante el arranque de una partida.
- El reloj se identificó por el decremento de los temporizadores de rueda y su correspondencia con `ms_fTimeStep` en la revisión anterior. El prototipo usa su paso nativo en unidades de 50 Hz y añade una corrección sobre la velocidad angular del eje lateral. La aceleración y el límite son propios del prototipo. Conserva la velocidad angular previa fuera de ese eje y la velocidad lineal.
- La sonda es código de pruebas. Usa el retorno del método original, y un mando virtual limitado al proceso de laboratorio para navegar el menú. El RAR tiene una lista de archivos cerrada que excluye la sonda.
