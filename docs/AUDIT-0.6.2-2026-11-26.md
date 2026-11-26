# SA Delirium 0.6.2-rc.1 — auditoría local

**Promoción autorizada a 0.6.2 el 26 de noviembre.** La versión final conserva exactamente el adaptador probado (`de5fca12ee5d6cc287138b2a98e78fd484d4122e0ede61a85b0749916ca913ac`); el nuevo EXE cambia la identificación del paquete a 0.6.2. El informe siguiente conserva el estado y las evidencias de la candidata antes de autorizar la publicación. No amplía el alcance de sus pruebas.

Estado: candidata local, sin publicar. Base pública: `f65fb8a4f47663feff24174dbccdd587513f799f` (0.6.0).
Esta revisión continúa el mod que requiere la ISO; no incorpora el experimento independiente.

## Comprobación del paquete merge-Adrii del 24 de noviembre

Archivo: `SA-Delirium-0.5.0-merge-Adrii-2026-11-24.rar`.
SHA-256: `8e120ba6d34f742cb3e9bae7be334495cccf92102878091fc4639b965bef4955`.

Se compararon sus fuentes estables con el adaptador **generado para distribución** de la 0.6.0, no solamente con vendor, porque el empaquetado aplica transformaciones adicionales.

- 40 de las 45 fuentes estables coinciden al normalizar saltos de línea.
- Las otras cinco conservan las mejoras del paquete y añaden correcciones posteriores: guardas de edificios/contactos en Adapter, validación de la identidad tras daño de vehículo y sangre con daño positivo en CombatRuntime, exclusión explícita del jugador en NikoMovement y ajustes de inicio/final de marcha en los dos módulos Rust de locomoción.
- El generador y geometría de DriverPose coinciden. La variante pública de DriverPose añade la búsqueda de su banco en `DeliriumSA-local-v3`.
- **No se aplicó de nuevo el parche antiguo**, para no retirar esas correcciones posteriores.
- `04-opcional-movimiento-libre` es una alternativa experimental separada. No está integrada ni se ha incorporado a esta candidata; no confundirla con los cambios estables ya presentes.

Evidencia local: `build/merge-1124/shipping-comparison.json` y la extracción aislada del RAR. Los binarios antiguos no se desplegaron.

## Correcciones incorporadas

El segundo archivo, `SA-Delirium-0.6.0-fixes-interiores-y-trafico-CJ.rar`, **no estaba aplicado** en la base pública revisada.
SHA-256: `e89209b4080899546d3bb48a03b2f078ad9d90b8c075a9f4060f263b7976db65`.

1. **Peatones de interiores.** Se conserva evidencia por referencia, modelo, clump y habitación de los peatones que SA etiqueta como área 0 pero realmente renderiza dentro del interior. Se acepta también el área compartida 13. Se invalida la pertenencia al cambiar de escena o reutilizar identidades. Esta política se comparte entre geometría, poses y locomoción; no se desactiva la protección de misiones.
2. **Recuperación de CJ cerca del tráfico.** La interrupción por un coche exige contacto con primitivas COL y la cápsula corporal renderizada, además de velocidad de cierre. La cercanía, una caja amplia, el tráfico que se aleja o una velocidad residual ya no bastan. Se mantiene la predicción anterior para los NPC.
3. **Reloj de marcha de NPC.** Se corrige la integración cuando un cuadro atraviesa varios ciclos cortos de una curva de marcha. La prueba fallaba antes y pasa después. No se cambian los bancos ni las velocidades generales.

Solo cambia el adaptador nativo dentro del archivo de herramientas. El empaquetador comprueba byte a byte que los demás miembros del ZIP permanecen iguales a la release pública verificada. Se conservan el motor normal/AVX2, IVDriverPose, importador, extras, licencias y configuración.

## Pruebas automatizadas

| Comprobación | Resultado y alcance |
| --- | --- |
| Integridad vendor | 550 registros y 88 modificaciones documentadas; PASS |
| Instalador C# | 22 casos sintéticos de paquetes, ediciones, rutas, cancelación, instalación/restauración y conservación; PASS |
| Progreso del instalador real | Compilación y pruebas contra el EXE candidato: límites, checklist y privacidad; PASS |
| Rust bootstrap | 12 pruebas PASS, 12 ignoradas que necesitan datos del propietario; no contarlas como aprobadas |
| Regresiones nativas | 10 programas C++ PASS: interiores, tráfico de CJ, melee, recuperación, caída, perfiles, pies, sigilo, dirección/espacio y reloj de marcha |
| Cámaras | Guardas de hooks y continuidad de cámara nativa; PASS |
| Coberturas | 4.601 muestras contra COL del juego, con 815 recuperaciones de uniones redondeadas; PASS |
| Metadatos | Fixture histórico PASS; sus números 0.4.x no certifican la candidata. El EXE real informa 0.6.2-rc.1 |

Los casos de interiores incluyen identidades recicladas y cambios de área. Los de tráfico incluyen frontal, lateral, tangencia, alejamiento y 1.053 cuadros de pasadas paralelas. Son pruebas de políticas reales del código, no una afirmación de haber visitado todos los interiores en partida.

## Instalación y pruebas en el juego

Laboratorio aislado `qa062`, fuera de las instalaciones habituales. Origen: copia clásica 1.0 US con Essentials y ejecutable LAA `f01a00ce…`; ISO del propietario, edición USA de Xbox 360.

- Instalación completa real de la 0.6.0 pública como control; restauración mediante el instalador; instalación completa real de la candidata: correctas.
- Instalación real **Solo Delirium** en otra copia con Essentials/Silent ASI Loader: correcta. Se generan `NPCGait.bin`, `NPCMovers.dat`, `DriverPoses.bin` y los 12 WAV de impacto. Restauración mediante el mismo EXE: correcta.
- Adaptador de distribución: carga de checkpoint, salida nativa de la casa de CJ, mundo exterior con NPC, apuntado y disparo de pistola/rifle. Sin errores del motor ni fallos de proceso registrados en esa sesión. Captura revisada en `qa062/evidence/candidate-aim.png`.
- Adaptador diagnóstico, generado de las mismas fuentes con exportaciones de prueba: pasada sin contacto sin activar ragdoll; impacto frontal con contacto y levantada nativa completada; roce lateral con contacto, física y liberación; caída de CJ con aterrizaje, voz, impacto corporal y levantada completada. Evidencia por víctima, no inferida solamente del contador global.
- Tras esas pruebas se repuso el **adaptador de distribución** y se retiró GameTestBridge. Arranque sin puente ni variables de prueba: inicialización correcta tanto en instalación completa como en Solo. Este ensayo de arranque no carga una partida por sí solo.
- La copia del checkpoint conservó su SHA-256. No se modificaron las partidas originales, la instalación fuente, el proyecto independiente ni otros checkouts.

Tras restaurar Solo se compararon 532 archivos con la copia fuente: 530 permanecen idénticos. Los otros dos son la fecha `LastUpdate` de CrashInfo y su catálogo público `CrashList.txt`, actualizados automáticamente por ese mod de Essentials durante el arranque. No son un informe de cierre ni modificaciones del instalador a datos de GTA. Se conserva el resultado inicial y la revisión de esas dos diferencias.

La comprobación del paquete no encontró archivos originales de GTA idénticos a los de la copia fuente. Las 27 coincidencias revisadas son bibliotecas/mods de terceros y documentación que Essentials también incluye (BASS, CLEO y extensiones, Mod Loader, enlace de Framerate Vigilante). Se mantienen sus avisos originales. La herramienta `um publish` no está disponible: se emplearon la verificación del proveedor, comparación de hashes, inspección del manifiesto y conservación de todos los demás bytes del paquete público.

La primera partida probada produjo un fallo repetido del procesador de scripts en la **0.6.0 pública**, antes de desplegar cambios. Se conservó esa evidencia. Un checkpoint distinto y compatible cargó en la pública y en la candidata. No se ha establecido la causa ni se presenta esta candidata como corrección de aquel fallo.

Evidencias locales: `qa062/evidence`, `qa062/install-candidate.json`, `qa062/install-solo.json`, `qa062/restore-solo.json`; inicialización sin puente en `qa062/game/logs/manual-startup-*.json` y `qa062/logs/manual-startup-*.json`. No se distribuyen logs privados ni partidas.

## Límites y siguiente fase

No se ha recorrido la campaña completa, probado cada mod de terceros, medido un benchmark de FPS presentados ni repetido esta candidata sobre Steam real o ISO Europa/Asia. No se amplía la lista de ejecutables compatibles por extrapolación. El tiempo interno del adaptador observado no equivale al FPS final.

Permanece la dependencia de una ISO compatible de GTA IV Xbox 360 y de los archivos del propietario de GTA SA clásico. El sustituto independiente sigue separado: se retomará como una fase propia, conservando esta candidata y sus evidencias.

No se ha creado una release, etiqueta remota ni push. Las notas de publicación son un borrador local.

Cambios y comprobaciones asistidos por Codex. Se conservan las licencias, autorías y fuentes de los componentes existentes.
