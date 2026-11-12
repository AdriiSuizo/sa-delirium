# Rotación de la carrocería

El laboratorio aplica al coche la orientación que devuelve el integrador de
ruedas de IV. Antes, el adaptador solo trasladaba las velocidades lineal y
angular y SA calculaba la orientación con su propia rutina.

En un paso aceptado, el adaptador sustituye `CPhysical::ApplyTurnSpeed`
(`0x542E20`, SA 1.0 US). Si coinciden los ejes, la velocidad angular y el
intervalo del paso, escribe la orientación de RK4. Cuando SA prueba otra
pose durante una colisión, usa el avance de IV con la velocidad angular y
el intervalo de esa prueba. El giro conserva la posición del centro de masa.
SA conserva el movimiento lineal, la detección de colisiones y el daño.

## Referencia y prueba en juego

El avance de TU8 está en `0x824FBE40` y llama a la rotación de eje y ángulo
en `0x822F3180`. Se comparó el ejecutable original mediante el intérprete de
P-code con el modelo VMX128 de LSC4. La implementación que enlaza el
bootstrap coincidió en los 16 componentes de la matriz, bit a bit, en los
3.000 casos. El oráculo compara el avance de una pose, dado su estado de
entrada.

La prueba en juego aceleró el Admiral, aplicó dos giros en sentidos opuestos
y frenó. La carrocería llegó a −2,16° y +2,49° respecto al reposo y acabó
a 0,002° del nivel inicial. Se registraron 1.018 aplicaciones de la rotación
de IV, 403 con la orientación completa del paso RK4.

Con ese mismo par pasaron la frenada, el atropello con tráfico activo,
la rampa de 3,43 m y la persecución nativa con dos patrullas. El arranque
normal activó el tráfico IV desde el INI. La huella del checkpoint
protegido coincidió al terminar. Pasaron las cinco pruebas ordinarias
del bootstrap y la de cambios de marcha con el programa de IV importado.

Huellas SHA256 del par probado:

- `DeliriumSA.asi` `54544e6194d3bcf306d7480d9fbbca5b1e377251cb96b4408aad4cc30a15d67c`
- `sa_delirium_bootstrap.dll` `c8d810390c897f4d120026d65e2e1b74d61459a460745956a5fbcc4b91df95d5`

La trayectoria completa sigue dependiendo del amortiguamiento de SA, de
la geometría y el centro de masa de sus coches y del perfil de suspensión
adaptado. La prueba en SA mide la respuesta del montaje actual. Para
establecer paridad del balanceo queda comprobar la amplitud, la velocidad
de retorno y el rebote con el amortiguamiento y los parámetros de IV.
La siguiente prueba aplica esos parámetros al
[Admiral de SA](ADMIRAL-SUSPENSION.md).

## Reproducción

- Con el par x86 preparado según [FIRST-CAR.md](FIRST-CAR.md), ejecutar
  `tests/Test-VehicleLab.ps1 -Installed <lab> -Mode iv -Scenario roll`.
  El caso escribe `car.csv` y `roll.json`.
- Compilar el ejemplo `vehicle_advance_cases` del bootstrap para x86.
  Ejecutar `tests/Test-IVBodyAdvance.py` con `--tools` apuntando a
  `tools/xbox360-re` de LSC4, `--image` a la imagen TU8 propia, `--program`
  al programa importado por Delirium, `--replay` al ejemplo compilado,
  `--output` a un directorio local nuevo y `--cases 3000`. El oráculo
  requiere `pypcode`.
- El ejemplo `vehicle_iv_reference` recibe la ISO propia, el programa
  importado y el nombre del modelo. Lee las cotas de la colisión, las
  inercias, los coeficientes del fragmento y los parámetros de suspensión
  para comparar modelos localmente.
