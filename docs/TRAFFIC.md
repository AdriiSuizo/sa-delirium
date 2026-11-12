# Conducción experimental del tráfico

En la compilación privada, `[VehicleLab]` y `Enabled=1` en
`DeliriumSA.ini` activan la conducción de IV para los automóviles de
cuatro ruedas con geometría compatible en un radio de 95 m de CJ.
Funciona con los mandos del jugador, los conductores civiles y la
policía. SA genera vehículos, elige sus rutas y entrega los mandos
que el adaptador ajusta a las ruedas de IV.
Los coches simplificados cercanos pasan a física real mediante
`CCarCtrl::SwitchVehicleToRealPhysics` de SA.

Cada coche conserva el estado de su caja, neumáticos y suspensión
durante su vida activa. Se comprueba la generación del pool y se
liberan los estados fuera del área activa. Las patrullas usan
`POLICE` o `POLICE2`, el Ranger y el FBI Rancher usan `POLPAT`
y los blindados usan `STOCKADE`. Véase la
[asignación por modelo](RAMPS-AND-PROFILES.md#manejo-por-modelo).

Los vehículos distantes conservan la simulación simplificada de SA.
También conservan el tratamiento nativo las secuencias protegidas,
vehículos especiales sin una geometría compatible y contactos de rueda
con plataformas móviles o cuerpos físicos. La integración actual
mantiene los efectos de rueda y el audio nativos.

## Curvas y persecuciones

El adaptador limita la velocidad según la batalla, el agarre de los
neumáticos y el ángulo necesario para seguir la ruta. Lee la dirección
de los dos enlaces próximos del mapa de SA para empezar a frenar antes
de girar. La IA conserva el control de semáforos, obstáculos, maniobras
de marcha atrás y rutas de misión.
La asistencia de giro cerrado actúa en las maniobras lentas y se
reduce progresivamente al ganar velocidad.

La selección policial sigue los niveles y el reparto por patrulla
observados en GTA IV TU8 (0x82425A98). A una estrella sigue al objetivo
con distancia. Desde dos estrellas alterna el rumbo directo y el
adelanto al objetivo según el nivel y la semilla de cada coche. Los
vehículos pesados de policía mantienen la distancia. La puntería de
la persecución se ajusta cerca y con visión del objetivo. Los demás
conductores usan el mismo cálculo de curvas y distancias para sus
tareas de perseguir o seguir. El límite de volante de la IA sigue
0x82557DC8 de TU8.

`Test-VehicleLab.ps1 -Mode traffic -Scenario route -NativeNpcDriving`
permite comparar la IA de SA con la adaptación usando el mismo motor
de ruedas. `Test-DrivingControls.ps1` contiene pruebas separadas de
seguimiento policial, persecución y seguimiento civil.

En el recorrido de Grove Street, con el Admiral y las mismas físicas,
el percentil 95 de deslizamiento lateral en curvas por encima de
6 m/s pasó de 8,46° a 5,82°. El coche llegó a la zona de destino en
19,18 s frente a 19,99 s. En ambas pasadas estuvo unos 0,22 s casi
inmóvil con el acelerador aplicado.

## Controles de disparo y choque con dos ruedas

Con un mando XInput, R1 dispara el arma admitida por el drive-by
nativo. Al mantener L1, el stick derecho gira y eleva una cámara
libre con mira centrada. El motor encuentra el punto bajo la mira
y traza el disparo desde el arma hasta ese punto. R2 acelera también
durante el apuntado. En ese modo se neutralizan el eje de accesorios
y la entrada de los hidráulicos.

`tests/Test-DrivingControls.ps1` comprueba el drive-by en un Voodoo
con `-Scenario aim` y en una Freeway con `-Scenario aim-bike`.
`-Scenario aim-inputs` verifica los disparos hacia arriba, las
respuestas simultáneas de acelerador y disparo de CPad, la
restauración de los ejes y la salida del apuntado al cambiar a
un arma no admitida. Los casos usan entradas simuladas en la copia
de laboratorio.

El registro de impacto de SA identifica las motos y bicis por clase.
Su golpe y el rebote inmediato se excluyen de la decisión del
parabrisas. Un contacto frontal nuevo con un obstáculo rígido puede
volver a activar la expulsión.

## Frenado

Al detenerse, la goma virtual de los neumáticos podía mantener
tensión longitudinal y lateral en asfalto llano. El coche ya no
avanzaba, pero esa tensión sostenía la inclinación de frenada. El
ajuste renueva la referencia del contacto en llano, con el coche
prácticamente parado y sin acelerar. La fuerza del freno sigue
aplicándose y el anclaje se conserva en pendiente.

## Reproducción

Construir el par de laboratorio como se indica en [FIRST-CAR.md](FIRST-CAR.md).
En el puente de pruebas, `-Mode traffic -Scenario pursuit` usa la
búsqueda policial nativa con tres estrellas. Registra el coche del
jugador y pasos en los que IV aplica fuerzas bajo los mandos reales
de conductores civiles y policía. `-Mode traffic -Scenario road`
repite el atropello con el resto del tráfico activo.

El caso `-Mode iv -Scenario drive` mantiene el freno después de
parar y comprueba que la suspensión delantera y el cabeceo regresen
cerca de su posición de reposo.

## Resultados del 2026-11-11

- En el ensayo de frenada, diez segundos después del inicio y con el
  mismo freno aplicado, el morro quedó a −0,14° de cabeceo frente a
  −1,97° en la ejecución previa. La compresión delantera media bajó
  de 206 a 140 mm. El coche siguió prácticamente inmóvil.
- En la escena con tres estrellas se registraron 35 muestras del
  coche de CJ, 11 muestras en movimiento de tráfico civil y 22
  muestras de una patrulla 596 en persecución. Se observó la
  promoción nativa de un coche simplificado. La patrulla aceleró y
  giró bajo su misión nativa de perseguir al jugador.
- Pasaron el atropello con tráfico activo y la rampa de 3,43 m. El
  atropello registró una velocidad de cierre de 7,42 m/s, daño
  nativo, reacción física y recuperación. La rampa registró 189
  pasos IV sobre el objeto.
- El arranque normal leyó `Enabled=1` sin el puente. Pasaron las
  cinco pruebas ordinarias del bootstrap en serie y la prueba
  de transmisión con el programa importado de la copia de IV.

Huellas SHA256 del par comprobado:

- `DeliriumSA.asi` `0aef894f6ca9fbd3a2f631768bb38b377b1cc3e8e8f39bb73bdcb4a977781e41`
- `sa_delirium_bootstrap.dll` `ff53641d4a17b7fb1e127f267cf2f0f626e8b35402123177fd86ef6054e5c2b4`
