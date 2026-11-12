# Registro tÃ©cnico

## DeformaciÃ³n de carrocerÃ­a

- La ruta nativa de PC 1.0 US aplica el choque en `CAutomobile::VehicleDamage` (`0x6A7650`). El daÃ±o normal usa el impulso en `+0xD8`, la direcciÃ³n en `+0xE0` y el punto de choque en `+0xEC`. Se comprobÃ³ el ejecutable propio con Capstone y se contrastÃ³ con gta-reversed.
- El adaptador conserva la llamada nativa y toma una copia del choque que realmente daÃ±a el coche. La prueba usa una instalaciÃ³n aislada, el puente local sin captura del teclado y el checkpoint de pruebas protegido. La deformaciÃ³n afecta a la malla de RenderWare durante el dibujo.
- Tres choques nativos con el Admiral produjeron cuatro piezas deformadas en cada modo de manejo. Un Admiral de control conservÃ³ su vida y su geometrÃ­a. La reparaciÃ³n nativa y el interruptor del menÃº eliminaron el daÃ±o visual. Se conserva la protecciÃ³n de cinemÃ¡ticas y vehÃ­culos de misiÃ³n. La prueba de Ryder quedÃ³ interrumpida antes de completar la misiÃ³n por peticiÃ³n del usuario.

## Controles y combate de la previa 0.4.0

- Grinch Trainer 1.0 escribe la bandera de vida infinita en `0x96916D` y activa las protecciones fÃ­sicas. El impulso de explosiÃ³n de Delirium se puede generar con esa bandera activa. En juego, CJ entrÃ³ en ragdoll con 77 de vida y 33 de blindaje y conservÃ³ ambos valores.
- El atajo L3 + R3 se activa a los 600 ms y requiere soltarlo para volver a alternar. `CPhysical::ApplyGravity` estÃ¡ en `0x542FE0`. Las referencias nativas a `0x863984` multiplican por la gravedad. El mismo cambio se comunica al motor de cuerpos y al integrador de ruedas. La prueba nativa pasÃ³ de -0,004445 a 0 y restaurÃ³ el valor original.
- Un impacto crÃ­tico de AK en la cabeza usa la pose previa al disparo y conserva la amputaciÃ³n de SA. La prueba observÃ³ la marca de cabeza eliminada, cero de vida y un cuerpo activo de Delirium.
- La fijaciÃ³n cuerpo a cuerpo conserva el objetivo vÃ¡lido mientras se mantiene el control y vuelve a dibujar el indicador de salud nativo. En la prueba, un golpe de 7 puntos de IV restÃ³ 3,5 de SA y uno de 8 restÃ³ 4. El desplazamiento frontal y hacia atrÃ¡s en guardia usa una ganancia del 25 %. La locomociÃ³n con pistola conserva el brazo derecho de la pose nativa, incluso con el grupo base.

## Disparos y fijaciÃ³n en el ejecutable clÃ¡sico 1.0 US

- `CWeapon::Fire` (`0x742300`) aplica el intervalo de `weaponinfo.xml` despuÃ©s de un disparo confirmado. SA decide la animaciÃ³n, el permiso segÃºn nivel de arma, la recarga y el consumo de municiÃ³n. `CWeapon::DoBulletImpact` (`0x73B550`) entrega el punto y la pieza B del `CColPoint` en `+0x24`.
- Las escopetas trazan 16 rayos de CJ u 8 de un NPC con la dispersiÃ³n de GTA IV. Se agrupa el daÃ±o por vÃ­ctima y descarga. Cada contacto conserva su punto, direcciÃ³n y parte fÃ­sica. El resto de armas usa la fuerza durante 300 ms. La pistola conserva el impulso ajustado anteriormente.
- El arma arrancada de la mano por un disparo se registra como recogible. La entrada de `CPickup` mide `0x20`, con coordenadas comprimidas en `+0x10`, generaciÃ³n en `+0x1A` y tipo en `+0x1C`. Su presentaciÃ³n en `CPickups::DoPickUpEffects` (`0x455720`) la deja tumbada con brillo naranja. El contraataque de desarme conserva la entrega directa a CJ.
- La salud de TU8 arranca en 200 para el jugador y los NPC. El rango visible termina en 100. CJ usa `100 + salud SA` hasta su muerte y los NPC proyectan su reserva de herido con `2 Ã— salud SA`. El calculador de SA (`0x4B5AC0`) sigue escribiendo los valores de partidas y scripts. El daÃ±o de bala usa la tabla de armas de IV, la parte alcanzada y la absorciÃ³n del blindaje. Un disparo de pistola al torso causa 8,25 puntos visibles a CJ y 12,5 puntos nativos a un peatÃ³n normal sin blindaje.

## Cobertura a pie

- El adaptador se basa en las tareas de cobertura de LSC4. Selecciona paredes, objetos y vehÃ­culos casi inmÃ³viles mediante colisiones de SA. La altura del obstÃ¡culo decide si el personaje se agacha. La ISO propia aporta las ocho variantes de cobertura y los saltos al parapeto.
- La entrada corta combina la pose nativa de pie o de apuntado con la pose de cobertura de IV mientras gira y se acerca al obstÃ¡culo. El desplazamiento y el giro se suavizan juntos. Al llegar corriendo desde mÃ¡s lejos, CJ usa la animaciÃ³n de salto correspondiente a la altura del parapeto.
- CJ puede desplazarse a los lados, cambiar de perfil, asomarse y disparar a ciegas desde cobertura baja o desde una esquina. El arma usa los clips originales de salida y regreso a cobertura. El apuntado, el disparo normal y la recarga conservan las animaciones de SA.
- Los policÃ­as armados reservan posiciones ante disparos recientes de su objetivo. Se acercan, giran, se esconden, se asoman, disparan y vuelven a cubrirse. El ciclo conserva su tarea nativa de combate y la protecciÃ³n de misiones. El adaptador no reproduce los puntos de cobertura puestos por scripts, las seÃ±ales de SWAT ni el lanzamiento de granadas desde cobertura.
- La prueba en juego libre comprobÃ³ la entrada de pie y con el apuntado nativo sostenido, el salto en carrera, la valla, la caja, la salida y regreso, el disparo a ciegas con consumo de municiÃ³n y el ciclo completo de un policÃ­a armado. El registro no contiene errores del motor. La entrada de cobertura se accionÃ³ desde el puente de pruebas.
- La previa 6 controla la velocidad lateral en metros por segundo. El paso consume la distancia recorrida, deja al motor de SA el desplazamiento del personaje y mezcla las poses en las transiciones. La distancia al muro tiene una banda de 4,5 cm para evitar correcciones continuas cerca del punto de apoyo. El disparo a ciegas usa la boca del arma en la pose de cobertura y la direcciÃ³n de la cÃ¡mara. SA consume la municiÃ³n y resuelve el disparo.
- Se registraron 1.939 frames en juego libre con cobertura alta y baja, entrada de pie y apuntando, carrera, inversiÃ³n, parada y disparo. El mayor cambio entre dos frames al cambiar de pose dentro de la cobertura fue de 2,1 cm en la cabeza y 4,1 cm en la cadera. Los disparos de pistola accionados con el botÃ³n de fuego conservaron el origen de la boca y la direcciÃ³n solicitada en la traza nativa. El policÃ­a disparÃ³ y volviÃ³ a cubrirse.

## Inicio y mando en la instalaciÃ³n real de Steam

- D3D9 se importa en RVA `0x4a0374`. La correcciÃ³n preserva la llamada anterior y convierte la creaciÃ³n y los resets del dispositivo a ventana con el modo de escritorio. Se validÃ³ la instalaciÃ³n real desde el cliente de Steam con 2560 Ã— 1440 antes y despuÃ©s.
- La consulta XInput de la ruta nativa estÃ¡ en RVA `0x37c982` y usa la importaciÃ³n visible para Steam Input. Se observÃ³ que la cÃ¡mara conserva el modo ratÃ³n mientras recibe los dos ejes analÃ³gicos. La adaptaciÃ³n selecciona temporalmente el camino nativo de stick durante la actualizaciÃ³n de cÃ¡mara, respetando los demÃ¡s consumidores de entrada. Las direcciones se validan para el ejecutable Steam 459558.

## Filtro de color de Steam

- El lector de iluminaciÃ³n de Steam estÃ¡ en RVA `0x1d8f70`. Los datos originales incluyen `data/timecycp.dat`. El cargador virtual presenta ese archivo como entrada de `timecyc.dat` en memoria. La primera tabla alfa comprobada estÃ¡ en RVA `0x80d7a8`.
- La llamada a la pasada de color estÃ¡ en RVA `0x34438b` y originalmente llama a `0x3428d0`. Se comprueban la huella completa del ejecutable y los cinco bytes de la llamada. La pasada propia usa el raster de la escena y multiplica los canales conforme al rango de modulaciÃ³n PS2, sin desenfoque. Usa la ruta original para visiÃ³n nocturna e infrarroja.
- Se obtuvieron capturas A/B en la escena inicial a la misma hora de juego. Quedan por portar radiosidad, reflejos, vegetaciÃ³n y luces LOD. La selecciÃ³n visual y la procedencia estÃ¡n en `docs/DEPENDENCIES.md`.

- El port parte del proyecto de desarrollo y de una copia limpia del Steam clÃ¡sico build 459558. Ambas instalaciones originales se tratan como fuentes de solo lectura.
- El prototipo inicial de empaquetado aplastaba el Ã¡rbol de rutas en RAR. Se corrigiÃ³ y se aÃ±adiÃ³ una comprobaciÃ³n posterior sobre las rutas del archivo empaquetado.
- La previa aÃ­sla la primera adaptaciÃ³n de vehÃ­culos en el nuevo ejecutable sin resolver direcciones de la revisiÃ³n anterior. El cargador proporciona una vista temporal de los datos del propio juego. Los archivos originales se comprueban por hash.
- La integraciÃ³n completa se divide en port de ABI por subsistema, motor autÃ³nomo, equivalentes de animaciÃ³n y decisiones, y validaciÃ³n de paridad en Steam.

## ActualizaciÃ³n nativa de coches en Steam 459558

- Tabla virtual de `CAutomobile` en RVA `0x4bc49c`, con el mÃ©todo de actualizaciÃ³n en la entrada 10 y RVA `0x2dfe40`. Verificada mediante correspondencia de entradas adyacentes y comportamiento del mÃ©todo. La instalaciÃ³n comprueba la huella del ejecutable y el puntero original antes de cambiar la entrada.
- El bucle de ruedas incrementa el byte `+0x960` al detectar contacto y el byte `+0x961` solo para las ruedas motrices correspondientes a la transmisiÃ³n. El segundo valor puede ser cero con ruedas apoyadas. El cabeceo solo usa el primero. Una sonda de laboratorio llamÃ³ al mÃ©todo original y observÃ³ 4 contactos y 2 ruedas motrices durante el arranque de una partida.
- El reloj se identificÃ³ por el decremento de los temporizadores de rueda y su correspondencia con `ms_fTimeStep` en la revisiÃ³n anterior. El prototipo usa su paso nativo en unidades de 50 Hz y aÃ±ade una correcciÃ³n sobre la velocidad angular del eje lateral. La aceleraciÃ³n y el lÃ­mite son propios del prototipo. Conserva la velocidad angular previa fuera de ese eje y la velocidad lineal.
- La sonda es cÃ³digo de pruebas. Usa el retorno del mÃ©todo original, y un mando virtual limitado al proceso de laboratorio para navegar el menÃº. El RAR tiene una lista de archivos cerrada que excluye la sonda.


## Cobertura, cámara y armas en el suelo — previa privada 7, 2026-11-12

Se elimina la realimentación que hacía oscilar la raíz al asomarse, la alternancia de esquina/centro y los saltos de pose al reiniciar el disparo a ciegas. La mezcla distingue arma/acción, conserva orientación del mundo y se aplica después de orientar los brazos. Se mantiene la dirección de cámara al apuntar; la boca visual se alinea con el disparo. Las armas soltadas al morir pasan por la presentación de caída/suelo y luz naranja.

4.752 frames en la última comprobación, pruebas de centro/esquinas, giros, pistola/AK a ciegas y muertes; regresiones de disparo/desarme/policías comprobadas en la revisión inmediatamente anterior al último ajuste del mezclador. Entrega instalada en la copia D de pruebas, arranque normal comprobado y cerrado. No se publica 0.4.0 ni se repiten misiones. Evidencias, iteraciones fallidas y límites en `docs/cover-preview7-20261112.md`. Pendiente valoración con el mando del usuario.

## Revisión de Morpheme 5.2 y Euphoria en WarZ

La revisión fijó el commit `5262ea65e0baaf3f37ffaede5f41c9b7eafee7c1`.
Los módulos relevantes usan contactos del paso resuelto, un predictor de
amenazas, apoyos ponderados y objetivos articulares con límites. El SDK
integra PhysX para cuerpos y articulaciones. La carpeta se consultó como
referencia para desarrollos propios. [Versión y cabecera](https://github.com/bizkut/warz-client/blob/5262ea65e0baaf3f37ffaede5f41c9b7eafee7c1/External/NaturalMotion/morpheme/SDK/sharedDefines/mVersion.h)

- Primero, registrar punto, normal, velocidad relativa e impulso resuelto
  por pieza y propietario. Separar la previsión del impacto confirmado.
  La revisión encontró que `VehicleContacts.hpp` admitía contactos usando
  la dirección desde el centro del coche. `selectSolvedRoadReaction` ya
  elegía la reacción con el impulso del paso resuelto.
- Segundo, probar apoyos ponderados, velocidad relativa al soporte y
  candidatos de paso con alcance y suelo válido. Delirium ya tiene punto
  de captura y ajuste de la pose de levantarse. La extensión se ensayará
  en suelo irregular y apoyos que se mueven, con la regla de recuperación
  del capó como caso de regresión.
- Tercero, probar el apoyo de manos en dos fases, alcance y amortiguación,
  con objetivos limitados de orientación de palma y antebrazo. La
  corrección visible de `WristPose.hpp` sirve como referencia. Medir la
  muñeca física y la visible para localizar el origen del giro.
- La primera comparación en juego será atropello frontal, roce lateral y
  pasada cercana sin choque. Registrar también contactos simultáneos
  con pared o bordillo, coste por frame y apoyos de recuperación.

### Contactos, apoyos y melee

- San Andreas admite el impacto a partir del punto, la pieza y la normal
  devueltos por sus rutas de resolución ya interceptadas. La velocidad de
  cierre usa la traslación y el giro de ambos cuerpos en ese punto. Una
  barrida corta contra el COL permite anticipar la postura del peatón.
- El selector de Delirium acumula el impulso de las piezas que pertenecen
  al vehículo que golpeó al peatón. Los contactos de otros propietarios
  siguen en la simulación y en el registro de daño. La recepción con las
  manos usa velocidad relativa y los límites físicos de cada muñeca.
- Los pies usan la carga normal resuelta para estimar el plano y la
  velocidad de apoyo. Los pasos correctivos comprueban alcance, suelo y
  separación de la pierna apoyada. El golpe nativo de melee guarda la
  fase del ataque al recibir el daño, antes de que avance la animación.
  Las variantes y el impulso de los golpes de GTA IV siguen su selector.
- En modo libre se probaron frontal, roce, pasada cercana y anticipación a
  baja velocidad. La pasada quedó sin daño, activación ni anticipación.
  La comparación con `a2ceddc` usó las mismas escenas con la captura de
  imágenes desactivada y un límite de 90 FPS. La versión nueva midió
  89,70–89,94 FPS y 3,14–3,24 ms de p95 en `CGame::Process`. La versión
  anterior midió 89,76–89,91 FPS y 3,12–3,23 ms en ese p95.
- Las pruebas de combate cubren el puñetazo y la patada de CJ contra un NPC,
  y la reacción de CJ al recibir un golpe de la IA.
  La caja se ensayó con el peatón en medio a 54, 126 y 162 km/h de salida.
  El hidrante se ensayó en la misma disposición a 54 km/h.
- Pasaron 162 pruebas de Rust. La suite dejó una prueba marcada como
  ignorada. Se compilaron las variantes x86 y AVX2 y el adaptador final.
