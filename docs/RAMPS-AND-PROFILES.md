# Rampas y perfiles por vehículo

El modo experimental usa la masa, el centro de gravedad, la caja de
colisión y las líneas de suspensión del vehículo de SA. Estima un momento
de inercia distinto para cada eje a partir de la masa y las dimensiones.
La distancia entre ruedas y el centro de gravedad determinan la fracción
de carga de cada rueda. Esto afecta a la suspensión y al agarre.

## Manejo por modelo

El laboratorio asigna 124 modelos de SA a equivalentes concretos de GTA IV.
Por ejemplo, el Cheetah usa el Turismo, el Elegy usa el Sultan RS, el
Stafford usa el Cognoscenti y la carretilla usa el Forklift. La tabla por
modelo está en [vehicle_counterparts.rs](../bootstrap/src/vehicle_counterparts.rs).
El instalador extrae `data/handling.dat` de la ISO propia a la caché local.
Una caché anterior puede incorporarlo al volver a preparar con la ISO.

Del equivalente se toman las marchas, el motor, los frenos, los neumáticos,
el ángulo de giro y el tarado de suspensión. Los ejes motrices, la masa,
la geometría y el rango de velocidad pertenecen al vehículo de SA. La
suspensión conserva su recorrido físico y ajusta el muelle y el
amortiguador a ese recorrido. La opción de ritmo incrementa el empuje,
el rango de velocidad y el frenado un 20%. El agarre conserva los
valores del equivalente de IV.

Los 22 modelos sin un equivalente cercano, entre ellos el kart, los RC,
el quad y los monster trucks, tienen ajustes propios. Rhino, Dozer y
RC Tiger conservan su control nativo de orugas.

`Test-VehicleGripBatch.ps1 -Fleet` comprueba el perfil cargado y los pasos
de física de cada modelo. El mismo script sin `-Fleet` mide lanzamiento,
giro y deslizamiento de deportivos, berlinas y coches de lujo.

La prueba corta de lanzamiento completó los 146 modelos en SA 1.0 US
con pasos IV y movimiento hacia delante en cada caso. Los cinco
monster trucks y camiones que usan discos de colisión se comprobaron
por separado con su adaptador de rueda. Las ISO europea y estadounidense
entregaron el mismo archivo de ajustes.

El deslizamiento lateral con gas durante la curva de prueba fue de
3,99° para el Comet, 3,15° para el Infernus y 2,33° para el Cheetah.
El Admiral completó la rampa 1634 con 154 pasos IV sobre la rampa.
La persecución observó al jugador, coches civiles y una patrulla
bajo control de la IA nativa.

## Contacto en rampas

SA registra algunas rampas como objetos físicos aunque estén fijas.
El prototipo anterior devolvía ese contacto a SA al encontrar cualquier
objeto bajo la rueda. Ahora admite objetos estáticos sin movimiento ni
enganche a otra entidad. Los apoyos móviles y los peatones físicos
conservan el tratamiento nativo. Al volver a tocar el suelo, cada rueda
reinicia la referencia de compresión para que el amortiguador no use
la extensión del paso anterior en el aire.

Los casos `ramp` y `steep` de [Test-VehicleLab.ps1](../tests/Test-VehicleLab.ps1)
colocan las rampas originales 1633 y 1634 sobre el mismo tramo de Grove
Street. Comprueban su orientación y desnivel con rayos nativos, y exigen
contacto de rueda durante la subida, paso por la cresta y recepción con
ruedas apoyadas. En modo IV exigen además pasos de fuerza registrados
sobre la propia rampa.

## Medidas del Admiral (2026-11-11)

El juego entregó una masa de 1650 kg. La caja de colisión produjo
momentos estimados de 4706, 1201 y 4952 kg·m² en cabeceo, balanceo y giro
respectivamente. Antes se aplicaba el mismo momento a los tres ejes.

| Caso | Desnivel medido | Velocidad en la cresta | Pasos IV sobre la rampa |
| --- | ---: | ---: | ---: |
| 1633 clásico | 2,13 m | 38,5 km/h | 0 |
| 1633 IV | 2,13 m | 19,7 km/h | 171 |
| 1634 IV | 3,43 m | 11,2 km/h | 191 |

Las tres pasadas completaron la subida y recuperaron apoyo al otro
lado. Se aplicó acelerador en el mismo tramo. Las velocidades
resultantes describen la calibración actual del Admiral.

El mismo par de binarios también pasó el ensayo de aceleración, giro y
frenada en llano y el atropello frontal. En el atropello, la velocidad
de cierre fue de 7,41 m/s. Se observó la reacción física, la animación de
levantarse y la devolución del control. El arranque normal sin puente
inicializó Delirium y la partida de prueba mantuvo su huella.

La prueba de caja con datos propios volvió a recorrer las cinco marchas.
Las cinco pruebas ordinarias del bootstrap pasaron en serie. La primera
ejecución en paralelo produjo una colisión de nombre de directorio
temporal en un caso visual existente.

Huellas SHA256 del par comprobado:

- `DeliriumSA.asi` `6f0e0c41615361e0b1a18e14c301e2fcb4d4dffdd24fd50e59d670fc93b86d99`
- `sa_delirium_bootstrap.dll` `2bed8f8c7810c92a7f9b591f3e6218fc68a669bb8ef4314b0fdc382a816d91ed`
