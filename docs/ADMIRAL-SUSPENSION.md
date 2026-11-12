# Suspensión de GTA IV en el Admiral

El perfil experimental se aplica al Admiral de SA (445), también cuando
lo conduce un NPC. El ejemplo `vehicle_iv_reference` lee el fragmento y
la ficha de manejo del Admiral de la copia propia de IV. El perfil se
genera en la caché local de Delirium y se carga una vez por proceso.

El coche conserva la masa, el centro de gravedad, la colocación de
las ruedas y la carrera de suspensión de SA. La inercia por eje conserva
el factor de forma del Admiral de IV, aplicado a las dimensiones y
la masa de SA. Se mantiene el punto de reposo que ya usaba el
Admiral de SA. En la primera prueba, trasladar el punto de reposo
de IV hizo que la carrocería bajara 14 cm al acelerar. La rigidez
relativa al peso escala por el
cociente de recorridos y la compresión y extensión por la raíz de
ese cociente, para conservar su relación con la amortiguación crítica.
Se conserva el reparto entre ejes del perfil importado.

En los pasos aceptados, el solver sustituye la pérdida angular medida
en `CPhysical::ApplyAirResistance` (SA 1.0 US, `0x544C40`). Aplica
un par opuesto al giro en ejes de carrocería, con los términos constante,
lineal y cuadrático del perfil de IV y el límite que impide que el
amortiguamiento invierta el giro por sí solo. El término lineal usa el
ajuste del Admiral con puertas articuladas de `CVehicleModelInfo`
(`0x8250BCE8`, TU8). En el fragmento del Admiral se comprobó que los
coeficientes constante y cuadrático tienen el mismo valor en los
tres ejes. La formulación del amortiguamiento procede del
análisis de `phArchetypeDamp` (`0x82999B10`, TU8) y su integración
en el visor de LSC4. La resistencia lineal y las colisiones de SA
siguen activas. La entrada de SA se contrastó con el ejecutable del
laboratorio y [plugin-sdk](https://github.com/DK22Pac/plugin-sdk/blob/master/plugin_sa/game_sa/CPhysical.cpp).

## Reproducción

1. Compilar el par x86 según [FIRST-CAR.md](FIRST-CAR.md) y el ejemplo
   `vehicle_iv_reference` de `bootstrap/Cargo.toml` para el mismo objetivo.
2. Ejecutar el ejemplo con cuatro argumentos: ruta de la ISO propia,
   `iv-program.bin` de la caché del laboratorio, `admiral` y la ruta
   `SA/DeliriumSA-local-v3/iv-admiral-suspension.json` como archivo nuevo.
   Una caché que carece de ese perfil sigue usando el ajuste anterior.
3. Ejecutar `tests/Test-VehicleLab.ps1 -Installed <lab> -Mode iv -Scenario roll`
   con y sin `-BaselineSuspension`. Ambos pases entran al primer giro a
   6 m/s y hacen el mismo cambio de volante sin gas. Se mide el ángulo con
   el volante girado, el pico al otro lado después de centrar el volante
   y antes de frenar, y el tiempo hasta quedar dentro de ±0,15° del
   reposo durante al menos 400 ms y hasta el final del ensayo. La prueba
   indica si el coche entró en la banda antes de frenar. Los casos
   guardan la telemetría en `car.csv` y `roll.json`.

El alcance de esta comparación es el Admiral. Los parámetros del resto
de los coches requieren su propia correspondencia y validación. La
comparación mide los dos ajustes en San Andreas, con sus contactos
de rueda y la geometría de su carrocería.

## Medición del 11 de noviembre de 2026

Mismo ejecutable, mismo checkpoint y entrada a 6,00 m/s en Grove Street.
El modo de referencia conserva la rotación de IV que ya integraba
el fork y usa su ajuste anterior de suspensión e inercia.

| Medida | Ajuste anterior | Ajuste de IV adaptado |
| --- | ---: | ---: |
| Primer pico con volante girado | −2,20° | −3,86° |
| Segundo pico con volante girado | +2,06° | +4,50° |
| Rebote al centrar el volante | 0,56° | 2,01° |
| Asentamiento dentro de ±0,15° | 0,94 s | 1,77 s |
| Inercia sobre el eje longitudinal | 1.201 kg·m² | 1.044 kg·m² |

Ambos se asentaron antes de frenar. La pasada adaptada sustituyó
el amortiguamiento angular de SA en los 1.057 pasos aceptados.
Con el punto de reposo de SA, la caída máxima en la prueba de
aceleración quedó en 5,8 cm y el cabeceo volvió a −0,16° con el
freno aplicado. También pasaron la rampa de 3,43 m con 157 pasos
de física IV sobre el objeto, el atropello con tráfico activo y
la persecución con dos patrullas. Los dos primeros intentos de
persecución registraron patrullas en movimiento. El tercero también
registró tráfico civil en movimiento y una promoción a física real.
El arranque normal cargó el perfil desde la caché. Pasaron seis
pruebas ordinarias del bootstrap y la de transmisión con la copia
propia de IV. La huella del checkpoint protegido coincidió al terminar.

Huellas SHA256 del par de prueba:

- `DeliriumSA.asi` `d15622304ddd9e61c1a1831b832e0fb6e1e17ccf5aba515e6652614f4273a8e5`
- `sa_delirium_bootstrap.dll` `c8adb58de81662492c68fe70926835e9e48296cb3879d466ebf067795172ccfe`
