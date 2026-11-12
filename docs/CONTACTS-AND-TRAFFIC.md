# Contactos, dirección y respuesta de conducción

El laboratorio mantiene la suspensión de IV cuando un ragdoll se acerca al
coche. La selección anterior devolvía todo el coche a la suspensión de SA
si había un cuerpo a menos de cuatro metros.

Las ruedas que pisan la carretera siguen en el integrador de IV. Una rueda
que pisa un cuerpo usa el contacto de masa finita existente. Su impulso
se conserva en el coche y se transmite al cuerpo con signo opuesto. El
par se convierte con la inercia por eje de ese coche. La consulta de
contactos y la aplicación de fuerzas se realizan por separado, para
conservar ambos tipos de apoyo en el mismo paso.

Los contactos de carrocería con peatones también usan el tensor de
inercia adaptado, compartido con el propietario físico del coche. Las
colisiones entre coches siguen entrando por el sistema nativo de SA
y la suspensión responde a las velocidades e impactos resultantes.

## Dirección y velocidad

El ajuste actual aumenta la respuesta del volante de forma gradual
a partir de 25 km/h, sobre todo a medio recorrido del stick. Los
neumáticos delanteros ganan agarre en curva y las ruedas motrices
aprovechan un 10% más de fuerza del motor. Cada modelo conserva su
perfil, sus marchas y su velocidad objetivo. La suspensión, la masa y
los parámetros de amortiguación son los mismos.

En `tests/Test-VehicleLab.ps1`, los escenarios `curve-admiral` y
`curve-comet` aplican el mismo pulso de acelerador y el mismo giro
al 55% del stick. A 57 km/h, el radio aproximado del Admiral pasó
de 24,2 a 21,4 m. A 86 km/h, el del Comet pasó de 48,7 a 41,0 m.
En el pulso de salida, el Admiral pasó de 17,2 a 19,5 km/h y el
Comet quedó en torno a 51 km/h en ambas pasadas.

### Calibración anterior

Los automóviles compatibles reciben un 25% más de ángulo de dirección,
con un límite de 0,9 radianes. Los conductores civiles y policías
conservan los mandos y las rutas de la IA nativa.

En los perfiles por modelo, la entrega del motor, el rango de velocidad y
la frenada aumentan un 20%. El agarre se toma del equivalente de GTA IV.
En la calibración anterior, el primer ensayo con más motor y el mismo agarre
apenas modificó la aceleración medida. Al adaptar también el agarre,
la velocidad alcanzada en el mismo pulso de acelerador pasó de
25,42 a 30,41 km/h, un 19,6% más.

El ensayo de referencia se repite con `-PreviousDriving` en
`tests/Test-VehicleLab.ps1`. Ese interruptor actúa solo en el proceso
de pruebas y conserva el acoplamiento físico nuevo.

## Casos reproducibles

- `-Mode iv -Scenario hood` deja caer un cuerpo sobre el capó y comprueba
  el intercambio de fuerzas y la respuesta de la carrocería.
- `-Mode iv -Scenario wheel` coloca una rueda junto a un cuerpo físico
  asentado y deja que el coche ruede a 2 m/s. Debe aparecer el impulso
  recíproco del contacto con el resto de ruedas en IV.
- `-Mode iv -Scenario crash` inicia un choque descentrado contra otro
  coche y comprueba el movimiento y la inclinación del segundo.
- `-Mode traffic -Scenario route` encarga un trayecto al conductor
  nativo hasta un destino sobre la carretera. Registra llegada,
  distancia recorrida y tiempo inmóvil con acelerador.

Estos ensayos usan el checkpoint protegido del laboratorio y el puente
de pruebas. La cobertura de rutas corresponde al trayecto ensayado.

## Atropellos, audio y explosiones

`tests/Test-PlayerShove.ps1` verifica que el empujón de CJ arranque
con el contacto resuelto por los cuerpos. La predicción de acercamiento
ya no inicia el clip.

`tests/Test-RoadPropFixture.ps1 -RequirePhysical` verifica que el
impacto conserve el daño nativo y entregue el movimiento del peatón
a Delirium. El reemplazo visual se limita a impactos admitidos con
soporte físico y conserva las exclusiones de misiones y cinemáticas.

En los ensayos con el peatón entre el coche y el objeto, el hidrante a
54 km/h y la caja a 126 y 162 km/h salieron de la colisión fija tras su
primer movimiento. Se comprobó en la base 0.3.14 con el arreglo local
y en la compilación privada con la conducción IV activa.

El nitro se activa con N o con la cruceta abajo en XInput. El caso del
Cheetah confirmó que el acelerador y la entrada de disparo conservan
las tres cargas y que N consume una. La prueba cubre la ruta nativa de
teclado. Los golpes del cuerpo y el grito en vuelo usan el audio de SA.

En la explosión de prueba a cinco metros de CJ, el pico de velocidad
vertical subió de 1,00 a 1,57 m/s y el centro de masa pasó de elevarse
6,4 a 13,8 cm. La salud inmediatamente después de la explosión fue
53,75 en ambos casos. `Test-ExplosionMotion.ps1` registra la trayectoria,
el audio de aterrizaje y la aceptación de la voz por el motor de SA.

## Comparación del 11 de noviembre de 2026

Estas mediciones corresponden a los ajustes anteriores a los perfiles
por modelo.

| Medida | Conducción anterior | Ajuste nuevo |
| --- | ---: | ---: |
| Velocidad al final del mismo pulso de gas | 25,42 km/h | 30,41 km/h |
| Tiempo hasta entrar en la zona de llegada de la ruta | 18,25 s | 15,97 s |
| Distancia recorrida en el ensayo de ruta | 161,75 m | 156,76 m |
| Tiempo inmóvil con gas en esa ruta | 0,20 s | 0,20 s |

La llegada se mide al entrar a menos de ocho metros del punto de
carretera. En ambos casos el coche pasó a menos de 14 cm del objetivo.
El nuevo ajuste llegó un 12,5% antes. El trayecto incluye el cambio
de sentido inicial, curvas y una subida, con mandos nativos del NPC.

En la caída sobre el capó se registraron 147 transferencias de fuerza
del cuerpo al coche, 172 pasos con suspensión IV y un cabeceo máximo
de 0,70°. La rueda sobre el cuerpo produjo un impulso recíproco con
147 pasos IV y un paso de reserva nativo. En el choque descentrado,
el segundo coche llegó a 2,90 m/s y reaccionó con su carrocería.
El atropello con tráfico activo conservó el contacto y daño nativos,
con 247 pasos IV y ocho de reserva.
La rampa de 3,43 m completó subida, cresta y aterrizaje, con 173 pasos
IV sobre el objeto. La persecución registró 13 muestras de civiles
en movimiento, ocho de una patrulla en persecución y tres promociones
de tráfico simplificado a física real. El arranque normal activó
la conducción y el perfil de suspensión del Admiral desde la caché.

Pasaron las seis pruebas ordinarias del bootstrap y la de transmisión
con el programa importado de la copia propia de IV. La partida
protegida conservó su huella SHA256.

Huellas SHA256 del par probado:

- `DeliriumSA.asi` `01f8b87b04bd1c9176d659bf6e75cd59419ba81116ce90ea8c55ba45ff498ef8`
- `sa_delirium_bootstrap.dll` `36943b1144e764ce2d76885f8a6af204e4be951f2a5b00d3a61063d273410286`
