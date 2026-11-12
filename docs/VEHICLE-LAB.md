# Laboratorio privado de conducción

La base es SA Delirium 0.3.13, commit `ab8b395a50b2d6fbac3fa94fe241d376bbdce866`.
El perfil `vehicle-lab-baseline-1` usa la conducción nativa de SA y conserva el
runtime de personajes de esa versión. Sirve como control para medir la futura
conducción de IV.

## Preparar la base

Usa los archivos `portable-manifest.json` y `portable-tools.zip` del build
local verificado de 0.3.13. El script exige las huellas de ese build y elimina
las recetas, la entrada de inventario y los tres archivos de Izzy del paquete
derivado. El paquete base utiliza el `data/handling.cfg` del juego y no carga
`SAStandalone.asi`, que aplica el ajuste de amortiguación en otros perfiles.

```powershell
python installer/portable/Prepare-VehicleLabBaseline.py --source $stableBuild --output $candidate
./installer/portable/Build-LocalSetup.ps1 -Candidate $candidate
```

El segundo comando genera `SA-Delirium-LocalVerification.exe` junto al ZIP
verificado. Ejecuta el EXE con `--install --lab --standalone --unattended
--no-shortcuts`, indicando `--target` con una copia clásica verificada,
`--iso` con tu disco de GTA IV de Xbox 360, y `--destination`, `--cache`
y `--report` con rutas de laboratorio. Usa una carpeta de destino nueva
y corta para respetar el límite nativo de la ruta de las partidas.

La protección histórica de descarga de Izzy sigue en el ASI original y
queda inactiva en este perfil. Se podrá retirar cuando se reconstruya el
adaptador y se verifique su arranque. Las funciones de Izzy que se pierden
incluyen el control aéreo, las reacciones ampliadas a pinchazos, la suspensión
dañada, las puertas sólidas, los efectos y los cambios de conducción en motos.

## Referencias para el primer coche

Inspección local de LSC4 en `5900dacd35920cc7f2264126f8807e9ab0b51031`.
Las versiones de `suspension.rs`, `tyre.rs`, `transmission.rs`,
`wheelstep.rs` y `vehicles.rs` de `gta4-data` difieren de ambas copias
incluidas en 0.3.13. El visor y las notas de LSC4 sirven para identificar
los límites de cada implementación. Las comparaciones de TU8 en
`tools/xbox360-re/checks` permiten verificar cálculos concretos, con las
salvedades de `rust/docs/vehicles/port.md`.

El primer ensayo de conducción tomará de SA las dimensiones, apoyos de
rueda y controles de un turismo. Antes de aplicar fuerzas nuevas se
registrarán velocidad, giro, contacto por rueda y tiempo de paso de la
base clásica. La medición permitirá localizar qué fases nativas deben
ceder el cálculo de ruedas y cuáles conservan colisiones y tareas del juego.

## Verificación de la base (2026-11-11)

- Se compararon las 1645 recetas y las 434 entradas restantes del ZIP con
  0.3.13. Coinciden. Los tres archivos retirados pertenecen a Izzy.
- Se instaló el candidato en una carpeta nueva, desde la copia Steam de
  referencia y el disco de GTA IV del usuario, con caché inicialmente vacía.
  El instalador terminó en modo autónomo. El `handling.cfg` instalado
  coincide con el de la copia Steam.
- El arranque sin puente observó Delirium inicializado y ausencia de los
  módulos de Izzy y `SAStandalone.asi`.
- Un caso separado cargó el checkpoint protegido en Grove Street. Un disparo
  nativo activó Delirium, aplicó poses a los huesos del peatón y llegó a
  `upright_collision_handoff`. Se observaron 1415 fotogramas de actualización
  en 15.87 segundos de juego libre, sin cierre. La valla oculta parte del
  peatón en las capturas. La confirmación de la transición procede del
  registro nativo.

Este ensayo cubre instalación, arranque y reacción a un disparo. La
conducción de IV, el menú y la regresión completa de combate, misiones y
atropellos quedan para sus respectivos casos.

## Sonidos de impacto de GTA IV

Para la build privada, importa las muestras desde tu ISO de Xbox 360 después de
preparar la instalación. Usa el ejecutable de [vgmstream][vgmstream] con las DLL
que lo acompañan. Las rutas de este ejemplo apuntan a la carpeta `SA` del
laboratorio, a tu ISO y al decodificador.

```powershell
cargo run --manifest-path bootstrap/Cargo.toml --locked --release --target i686-pc-windows-msvc --example import_iv_melee_audio -- $game $iso $decoder
```

El importador guarda cinco WAV cortos en `DeliriumSA-local-v3/melee-audio`.
El instalador unificado ejecuta este paso con el decodificador incluido.
Las muestras se generan en tu equipo a partir de la ISO que seleccionas.

## Instalador unificado privado

**Instalación completa** prepara el perfil con los extras y las tres opciones
de conducción. **Solo Delirium** añade el adaptador y sus recursos al juego
seleccionado. Usa un ejecutable 1.0 US verificado, los recursos de gimnasio
compatibles y un cargador ASI compatible. En una copia con la biblioteca de
audio original añade Ultimate ASI Loader como `winmm.dll`. Si el juego ya
tiene un cargador verificado, lo reutiliza. Restaurar archiva los archivos de
Delirium y la configuración que hayas modificado.

En **Pausa → Opciones** puedes elegir Clásico retocado, Izzy o Delirium
GTA IV. Clásico y Delirium IV se alternan durante la partida. Clásico usa
la física de conducción de San Andreas y añade cabeceo, balanceo y
hundimiento de la carrocería en el dibujo. Los controles de disparo
desde el coche y el botón separado de nitro son comunes a los modos.

Delirium IV usa los perfiles de sus equivalentes por modelo y conserva
la geometría y la masa de San Andreas. La simulación de ruedas,
transmisión y suspensión avanza a 1,3×. Se reduce la ganancia cerca
del centro del stick y la ayuda aumenta en giros deliberados. El
tráfico sigue el ciclo nativo de aparición y entra en Delirium cuando
el juego le asigna física completa.

Movimiento de CJ se aplica desde el menú. Combate y los cambios que
entran o salen de Izzy se aplican al reiniciar. El instalador completo
incluye Izzy en su directorio propio. Solo Delirium ofrece los dos modos
que incluye.

En modo de gravedad reducida, CJ y el vehículo que conduce tienen
la mitad de su gravedad habitual. Los peatones y el resto de los
vehículos conservan el 1 %. El modo se activa con L3 + R3.

Las mediciones que siguen documentan calibraciones anteriores.

En la prueba de salida y frenada, el Admiral llegó a 33,09 km/h frente a
30,91 km/h antes del ajuste, con el mismo pulso de acelerador. Ambas pasadas
registraron un paso de reserva nativo. El ajuste añade par y agarre a baja
velocidad. La medición corresponde al Admiral en el tramo llano de prueba.

La segunda vista previa eleva un 15 % el límite de velocidad hacia delante
del modo GTA IV respecto a la primera. Conserva la relación efectiva entre
velocidad de rueda y régimen en marcha atrás y primera cuando hay varias
marchas. Los vehículos de una sola marcha ganan el recorrido en esa marcha.
A alta velocidad aumenta progresivamente el empuje para compensar la
resistencia del aire de SA.

### Carga de peatones sobre la carrocería

En Delirium GTA IV, la suspensión de un coche parado sigue activa mientras
recibe peso de un cuerpo físico. El solucionador existente transmite el
impulso al chasis en el punto de contacto. Durante la carga y el rebote se
conserva la velocidad vertical de la suspensión cuando SA intenta dejarla a
cero por estar casi parado. La masa, la inercia y los cuatro apoyos de cada
coche determinan la compresión. No se vuelve a aplicar el golpe.

`tests/Test-VehicleLab.ps1` tiene dos casos sobre el capó izquierdo del
Admiral, `hood-high` y `hood-low`. Registran la compresión por rueda, la bajada
del chasis y su retorno hacia nivel al retirar el cuerpo. Si el coche estaba
aparcado a la altura nativa inicial, la posición final es la de equilibrio
del perfil de suspensión IV.

Los casos `cj-light`, `cj-heavy` y `cj-short` verifican también a CJ. La
caída alta usa su cuerpo físico. En la caída corta, el contacto compartido
transmite peso al coche mientras CJ conserva su animación. Ambas rutas usan
los impulsos que ya resolvió el contacto.

En la misma caída de 5 m sobre el lado izquierdo del capó, la telemetría
registró estas diferencias con Delirium GTA IV activo.

| Coche | Masa | Compresión delantera izquierda | Recuperación de recorrido | Cabeceo al comprimir |
|---|---:|---:|---:|---:|
| Manana | 1 000 kg | 7,7 cm | 7,2 cm | 1,57° |
| Patriot | 2 500 kg | 4,3 cm | 5,0 cm | 0,48° |

La recuperación de recorrido se mide desde la máxima compresión hasta la
mayor extensión en los 1,4 s siguientes.

Los casos `cj-moving-roof`, `cj-moving-trunk` y `road-medium` prueban el
Admiral en movimiento. Para las caídas, CJ empieza con la velocidad
horizontal del coche y cae libremente. El registro identifica el punto
de contacto en las coordenadas del chasis y mide cada rueda con respecto
al instante previo al golpe.

| Contacto | Velocidad en el golpe | Compresión adicional |
|---|---:|---|
| CJ en el lado izquierdo del techo | 21,5 km/h | 4,3 cm delante y 4,8 cm detrás, a la izquierda |
| CJ en el lado derecho del maletero | 21,4 km/h | 8,6 cm en la rueda trasera derecha, 1,6 cm en la delantera derecha |
| Peatón en el parachoques delantero | 52,7 km/h | 3,3 cm delante y 0,5 cm detrás |

En las caídas el banco comprueba también el rebote posterior. En el
atropello el peatón de prueba tiene más salud para poder observar la
reacción física tras el golpe. El juego calcula el contacto y el daño.

### Giros al 75 % de la velocidad punta

El banco `tests/Run-VehicleSpeedRegression.py <laboratorio> [modelos...]`
usa la pista sur del aeropuerto de Los Santos con el manejo Delirium
activo. La pasada recta mantiene el acelerador y repite el mismo tramo
de asfalto conservando la velocidad al volver al comienzo. Se comprueba
que los últimos diez segundos se acercan a una velocidad estable. Después
se prepara cada giro al 75 % de la punta observada y se registra la
velocidad real al empezar a girar.

Cada modelo hace una pasada sin freno y otra con 0,3 s de freno de mano.
Ambas reciben el mismo giro durante un máximo de 2,5 s. Los CSV y el
resumen de cada caso guardan por separado el rumbo del chasis y la
dirección de avance, la pérdida de velocidad, la deriva, el balanceo
y los pasos que recurrieron a la suspensión nativa.

En el Savanna la carga delantera estática es de aproximadamente un 25 %.
El ajuste de los coches con ese reparto aplica más agarre a los
neumáticos delanteros durante el giro. El peso de cada rueda y el
balanceo siguen saliendo de la geometría, las cargas y los muelles del
modelo.

| Modelo | Entrada | Giro de trayectoria | Giro con freno de mano |
| --- | ---: | ---: | ---: |
| Banshee | 138 km/h | 55,4° | 57,1° |
| Comet | 151 km/h | 45,4° | 46,9° |
| Admiral | 104 km/h | 46,4° | 46,9° |
| Glendale | 88 km/h | 73,3° | 93,4° |
| Savanna | 93 km/h | 48,0° | 54,1° |
| Patrulla de Los Santos | 118 km/h | 50,9° | 52,6° |
| Cement Truck | 78 km/h | 38,8° | 41,9° |

Las filas del Savanna y el Cement Truck corresponden a la repetición con
el control de balanceo activo también sobre dos ruedas. Los cuatro
giros de esa repetición usaron el integrador IV en todos los pasos. El
Glendale de la primera pasada salió a 14,9 km/h tras el giro con freno
de mano. El banco guarda esa pérdida junto al ángulo.

### Ajuste general de giro

El mismo cálculo de neumáticos se aplica a todos los vehículos de cuatro
ruedas compatibles con el manejo Delirium. La ayuda aumenta con la
velocidad relativa al perfil de manejo y la demanda de giro. La inercia
del chasis determina cuánto se permite perder velocidad al cerrar una
curva. Tras un toque de freno de mano, la ayuda disminuye progresivamente
y se desplaza hacia el eje trasero cuando aumenta la deriva. El refuerzo
de balanceo compensa la mayor parte del par añadido por la ayuda de giro.

Para comparar dos versiones a la misma velocidad de entrada, el banco
acepta las puntas medidas mediante `--reference <vehicle-speed-results.json>`.
Una entrada fuera del margen del 75 % o una caída brusca de más de 4 m/s
entre muestras hace que se repita la pasada. Se conservan los CSV de
los intentos interrumpidos.

La comparación al 75 % de la punta medida dio **+15,4° de media sin freno**
y **+26,0° con un toque de 0,3 s**. Los ángulos de la tabla son cambios de
trayectoria en 2,5 s. La última calibración del toque se repitió por
separado porque solo cambió el coeficiente activo tras pulsar el freno
de mano.

| Modelo | Sin freno, antes → después | Con freno de mano, antes → después |
| --- | ---: | ---: |
| Banshee | 55,6° → 68,8° | 57,1° → 90,1° |
| Comet | 45,3° → 55,5° | 46,6° → 67,0° |
| Admiral | 46,7° → 61,5° | 47,0° → 68,1° |
| Glendale | 75,2° → 92,6° | 89,9° → 108,1° |
| Savanna | 43,9° → 72,0° | 54,1° → 85,8° |
| Patrulla de Los Santos | 50,8° → 65,2° | 52,3° → 82,9° |
| Cement Truck | 38,7° → 48,4° | 41,7° → 68,8° |

Los siete modelos completaron ambas maniobras sin vuelcos ni trompos.
La velocidad de salida bajó de media 1,1 km/h sin freno y 7,1 km/h con
el toque. El Cement Truck perdió más velocidad con el toque, de 38,2 a
22,6 km/h a la salida. Los 146 perfiles compatibles también pasaron la
comprobación automatizada de asignación de manejo.

### Entrada nativa y choques contra esquinas

La opción `--native-pad` pasa un 55 % de stick y el toque de freno de mano
por `CPad`. Mide los primeros 1,4 s del giro a la misma velocidad de
entrada que la pasada de referencia. A continuación mide la aceleración
desde que el juego acepta el pedal. `--launch-model` permite añadir
modelos con un reparto de tracción distinto al de su equivalente de IV.

| Modelo | Previa 8 a los 2 s | Previa 9 a los 2 s |
| --- | ---: | ---: |
| Elegy | 24,8 km/h | 54,0 km/h |
| Vincent | 15,4 km/h | 39,7 km/h |
| Turismo | 69,2 km/h | 59,8 km/h |

El Turismo deja de recibir la multiplicación de par adicional del
chasis con cuatro ruedas motrices. La corrección también recupera el
par que se perdía al adaptar un equivalente con cuatro ruedas motrices
a un chasis con dos.

Con el limitador de pruebas a 20 FPS, `--expect-slow` comprueba que
la mayoría de las muestras superan 40 ms y que el integrador IV sigue
activo. El adaptador respeta el máximo nativo de 60 ms y usa los
subpasos del integrador IV. [Referencia de CTimer][sa-timer]

En el Comet, con una entrada de 150,5 km/h, la recuperación tras soltar
el freno de mano bajó la deriva máxima de 62,2° a 31,0° entre los dos
ensayos a 20 FPS. La dirección de avance giró 29,9° en la segunda prueba
y los 27 pasos medidos se resolvieron con IV.

La opción `--corner-damage --driving-mode iv` lanza un Banshee contra el
pico de un contenedor fijo. Comprueba que se dibuja la abolladura, el
coche de control queda intacto y reparar el Banshee restablece su forma.
La detección comprueba dos caras en el modelo de colisión del objeto y
solo añade deformación visual. Se usan el impulso y el punto de contacto
registrados por el juego. [Referencia de CPhysical][sa-physical]

[sa-physical]: https://github.com/gta-reversed/gta-reversed/blob/master/source/game_sa/Entity/Physical.cpp#L649
[sa-timer]: https://github.com/gta-reversed/gta-reversed/blob/master/source/game_sa/Timer.cpp#L175
[vgmstream]: https://github.com/vgmstream/vgmstream
