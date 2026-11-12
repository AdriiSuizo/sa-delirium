# Admiral experimental

El ensayo usa el Admiral de SA (modelo 445), en la copia de laboratorio
sin Izzy. El adaptador funciona únicamente con el puente de pruebas y
selecciona el coche creado para ese proceso por su referencia del pool.
El ejecutable objetivo es SA clásico 1.0 US del perfil 0.3.13.

SA entrega la posición y normal de contacto de las ruedas, el recorrido
de suspensión, el radio, la masa, los mandos y el estado de la carrocería.
El módulo Rust llama a las rutinas de suspensión, neumáticos, transmisión
e integración de ruedas de Delirium. El flujo sigue el coche de Playroom.
La configuración de manejo es una adaptación inicial al Admiral.

El adaptador conserva las actualizaciones nativas de ruedas y efectos.
Para los pasos admitidos sustituye las fuerzas de suspensión y neumáticos
y aplica al coche las velocidades calculadas. SA sigue moviendo la
carrocería y resolviendo sus colisiones y el daño. La gravedad que SA ya
aplicó se descuenta en el integrador para que el paso experimental use
la gravedad de IV. Cerca de cuerpos físicos, sobre apoyos dinámicos
o ante un paso inválido, vuelven las fuerzas y contactos nativos.

## Reproducción

1. Preparar la [base de laboratorio](VEHICLE-LAB.md) y el puente portátil
   en una instalación privada con un checkpoint protegido. Usar los
   scripts del proyecto `Run-Case.ps1`, `Test-Harness.ps1` y
   `Send-NativeCommand.ps1` adaptados al directorio `SA` de esa instalación.
2. Compilar la biblioteca base con Cargo mediante
   `cargo build --manifest-path bootstrap/Cargo.toml --locked --release
   --target i686-pc-windows-msvc --target-dir build/vehicle-rust --lib`.
3. Ejecutar `python tests/Prepare-VehicleLabRuntime.py --out build/vehicle-native`
   y compilar ese directorio con `installer/portable/Build-DevelopmentRuntime.cmd`
   como indica [BUILD.md](../installer/portable/BUILD.md). Usar la biblioteca de
   importación generada en el paso anterior.
4. Con el juego cerrado, respaldar y sustituir solo el ASI y la DLL base
   de la copia de pruebas. Apartar la DLL AVX2 de esa copia para que el
   adaptador cargue la biblioteca experimental de ambos modos.
5. Ejecutar `tests/Test-VehicleLab.ps1 -Installed <lab> -Mode <classic|iv>
   -Scenario <drive|road>` para las cuatro combinaciones. El caso de
   conducción aplica la misma secuencia de pedal, giro y freno. El caso
   de atropello da una velocidad inicial de 8 m/s, luego el juego resuelve
   el acercamiento, el golpe y el daño.

Cada caso registra huellas de módulos, telemetría del vehículo, eventos
de Delirium y capturas. Los datos de la ISO se leen desde el caché local
creado por el instalador. Las capturas, partidas y datos del juego se
guardan en el laboratorio local.

## Resultado en juego (2026-11-11)

Cuatro casos pasaron con el mismo par de binarios x86. Se cargó el mismo
checkpoint y se usó el tramo de Grove Street sin Izzy. En la prueba de
conducción el coche recibió 2,3 s de acelerador y un giro corto antes de frenar.
El arranque de ese par sin el puente también inicializó Delirium. La
huella de la partida protegida coincidió antes y después.

| Medida | Clásico | IV experimental |
| --- | ---: | ---: |
| Velocidad máxima del ensayo | 71,0 km/h | 23,5 km/h |
| Velocidad al empezar a frenar | 70,1 km/h | 23,5 km/h |
| Tiempo de frenada hasta 0,15 m/s | 0,76 s | 0,81 s |
| Desplazamiento durante la frenada | 7,28 m | 2,67 m |
| Compresión delantera izquierda antes de acelerar | 112 mm | 117 mm |
| Giro durante el pulso de volante | 5,15° | 0,90° |

Las frenadas y los giros se hicieron a velocidades distintas. Las cifras
describen la respuesta a los mismos mandos y sirven para ajustar la próxima
calibración. En la pasada IV se registraron 959 pasos con fuerzas nuevas,
1916 sustituciones de pares de ruedas y 2 pasos nativos de reserva. La
carga delantera izquierda al principio fue de 2944 N y se midieron hasta
854 N de fuerza lateral neta de neumáticos durante el pulso de volante.
La caja experimental se quedó en primera durante esta pasada corta.
En una prueba Rust con la velocidad de rodadura controlada, el mismo
módulo engranó segunda, tercera, cuarta y quinta a 15,39, 20,75, 28,04
y 37,89 m/s respectivamente. Se puede repetir con el caché del usuario:

```powershell
cargo test --manifest-path bootstrap/Cargo.toml --locked --release --target i686-pc-windows-msvc --lib transmission_shifts_on_a_rolling_admiral -- --ignored --nocapture
```

Apuntar `SA_DELIRIUM_TEST_ROOT` al directorio `SA` del laboratorio. El
resto de pruebas ordinarias del bootstrap dio 5 aprobadas.

Las primeras capturas mostraron roce del chasis con el suelo. El valor
de gravedad nativa, `0.008` por unidad de tiempo de SA, equivale a
20 m/s² tras convertir las unidades de velocidad y tiempo. La corrección
deja que el integrador experimental use la gravedad de IV. En la última
pasada la carrocería bajó como máximo 4,8 cm durante la aceleración y las
capturas de revisión ya no mostraron el roce observado.

### Atropello

En cada modo el Admiral partió a 8 m/s hacia un peatón de pie con salud
elevada para poder observar la recuperación. La velocidad de cierre
medida fue de 5,80 m/s en clásico y 7,42 m/s en IV. Ambos aceptaron el
contacto nativo, activaron el cuerpo físico, seleccionaron la reacción
`brace`, aplicaron 33 puntos de daño y devolvieron el control a la
animación. En el modo IV se comprobó que el coche ya usaba las fuerzas
nuevas durante la aproximación. El acoplamiento nativo de las ruedas
se conserva cerca de un peatón físico.

La prueba cubre asfalto seco, un turismo y un atropello frontal. Quedan
por calibrar la aceleración, el agarre a velocidades comparables, las
superficies, los efectos de rueda y la inercia por eje. El modo sigue
disponible mediante el puente de pruebas, pendiente del menú y del
control libre del jugador.

Huellas SHA256 del par comprobado:

- `DeliriumSA.asi` `7799eb9d7dda439924e24ce4ea84168e875b75fc4a7324507949180a6d8860d3`
- `sa_delirium_bootstrap.dll` `386fb1cbf56a03e046c0a241a9b5f0780f3c625ca839e54cc03d894837e7fb5e`
