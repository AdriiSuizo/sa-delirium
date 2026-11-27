# SA Delirium 0.6.5-rc.1 con ISO

Candidata local basada en la 0.6.4. Las pruebas se hicieron en GTA San Andreas clásico de PC 1.0 US, con una copia de juego aislada y una partida de prueba. Se revisaron las cinco grabaciones del 27 de noviembre.

## Cámara y movimiento al apuntar

La 0.6.4 modificaba el campo de visión al terminar el cálculo de cámara. Para entonces SA ya había calculado los planos que usa para decidir qué objetos pueden entrar en la imagen. Además, el mod desplazaba la cámara a pie sobre el hombro. CJ vuelve a usar la cámara y el desplazamiento de SA al apuntar a pie con armas de fuego. El zoom opcional se aplica antes de calcular los planos. La cobertura y el apuntado desde vehículos conservan sus controles.

Medianas de 70 muestras por estado en Grove Street. Se leyó el ángulo de dibujo y se reconstruyó el de visibilidad a partir de la normal del plano lateral del motor.

| Versión y estado | Dibujo | Visibilidad |
| --- | ---: | ---: |
| 0.6.4, reposo | 86,07° | 86,07° |
| 0.6.4, apuntando con la 9 mm | 61,39° | 86,07° |
| Candidata, apuntando con la 9 mm | 86,07° | 86,07° |
| Candidata, tres pasos de zoom | 69,53° | 69,53° |
| Candidata, al soltar | 86,07° | 86,07° |

También se siguió a un civil y a un policía, de pie y después de un disparo. Se comprobó la identidad del peatón, la presencia de su objeto RenderWare y si SA lo preparaba para el dibujo. Ambos conservaron su identidad mientras estuvieron fuera de cuadro y volvieron a recibir la llamada de dibujo al entrar en campo. La discrepancia de proyección está demostrada. La esquina exacta de la grabación, la oclusión detrás de esa pared y posibles problemas de streaming en otras zonas siguen sin aislar.

Se midieron avances, retrocesos y pasos laterales con la 9 mm, el AK y el espray. Con la 9 mm se midieron 3,80 m hacia delante y 3,01 m hacia atrás en aproximadamente un segundo. El espray conserva su tratamiento específico.

## Armas y contraataques

El disparo dual respeta los eventos de cada mano cuando la ficha nativa indica que CJ tiene la habilidad necesaria. Se comprobó en partida que disparar cada mano consume su propia bala con la 9 mm, la recortada, la Micro SMG y la Tec-9. Las armas individuales conservan su intervalo y las escopetas conservan los perdigones.

Se valida la identidad del ped, vehículo u objeto antes de reutilizar un impacto de perdigones. La restauración de huesos comprueba la identidad de la jerarquía y la paleta. Se completaron 24 secuencias con cuatro armas, cambios de objetivo e impactos posteriores y letales. El cierre intermitente del usuario sigue sin una traza de la 0.6.4 que confirme su causa.

En la primera prueba de contraataques se registraron parejas de sonidos separadas por 11 y 12 ms. El contacto del puño y la marca de daño de la animación podían emitir efectos para el mismo golpe. Ahora comparten un registro de impacto entre quien golpea y quien lo recibe. Al repetir tres secuencias hubo 21 sonidos y la separación mínima para una misma víctima fue de 166 ms. El daño se procesa por separado. Las medidas corresponden a eventos de reproducción del juego.

## Coches

El amortiguamiento del vuelco sigue funcionando en taludes transitables, con al menos dos ruedas apoyadas. Se limita el par que intenta enderezar el coche cuando cambia de golpe la normal del suelo. En pendientes pronunciadas se reduce ese par para que la suspensión pueda seguir el terreno. También se corrigió la reutilización del simulador de otro vehículo cuando cambiaba una entrada de la caché de tráfico.

Entradas oblicuas al talud del canal de Los Santos. La inclinación se mide respecto a la horizontal del mundo y la subida respecto al punto de entrada.

| Modelo | Inclinación máx. 0.6.4 | Inclinación máx. candidata | Subida candidata | Velocidad máx. candidata |
| --- | ---: | ---: | ---: | ---: |
| Admiral | 20,8° | 25,4° | 4,30 m | 51 km/h |
| Comet | 21,7° | 23,9° | 6,75 m | 72 km/h |
| Huntley | 22,0° | 31,4° | 7,31 m | 70 km/h |

Los tres coches completaron el recorrido en las dos configuraciones. La candidata subió más por el talud en las tres pasadas. El control usó las bibliotecas físicas 0.6.4 y su adaptador de suspensión con los mismos comandos de prueba. El vuelco de la grabación contra la unión del talud y el muro sigue sin reproducirse con su trayectoria exacta.

## Verificación y paquete

- Pasaron 59 pruebas de la biblioteca de combate. Una exploración manual de clips está marcada para omitirse.
- Pasaron las 13 pruebas predeterminadas del bootstrap y las 10 pruebas de vehículos, incluidas las que usan datos del juego del propietario. Estos grupos se solapan.
- Pasaron 11 pruebas de comportamiento nativo, las pruebas del orden de llamadas de cámara y las de cámara de cobertura.
- El comprobador de procedencia revisó 550 registros. Se comparó el contenido de cada miembro del paquete con la 0.6.4. Cambian el ASI y las dos variantes de la biblioteca física. El ASI distribuido tiene dos exportaciones y ninguna de diagnóstico.
- Se verificó que el manifiesto, el paquete y el lanzador incrustados en el instalador corresponden al material compilado. La comprobación de origen aceptó 415 archivos del juego clásico de prueba. Las pruebas de partida usaron los recursos ya preparados de la ISO.
- SHA-256 del instalador: `7ee1eafb612854ac4875d6b2e6a1bb081fe2c8539c7632990cb2d436d32fb03a`.

El fork de EuphoriaConnect contiene documentación y capturas. Las notas de su autor sobre peatones encima de objetos y durante la recuperación orientaron la comprobación de identidad al cambiar de vista. [Fork revisado](https://github.com/AdriiSuizo/EuphoriaConnect) · [Notas del autor](https://github.com/Marii-dev/EuphoriaConnect/releases/tag/1.0.7).
