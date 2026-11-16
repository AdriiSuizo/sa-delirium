# Historial / Changelog

## 0.4.7 · apuntado GTA IV, cobertura, caminar, físicas y correcciones / GTA IV aiming, cover, walk, physics & fixes

### Español

- **Apuntado completo de GTA IV integrado y retirada de *Advanced Aiming Mod* (`IvAim` / `Aiming` / `DriveByAim`)**: se retira *Advanced Aiming Mod* del instalador completo en favor del apuntado integrado de GTA IV a pie y desde vehículos (`IvAim=1` en `DeliriumSA.ini`), con cámara sobre el hombro, recorte suave contra paredes, retícula sincronizada y zoom mediante `R3` o botón central del ratón (`MMB`), además de la rueda (`AimWheelZoom=1`).
- **Mirar atrás con `R3` en vehículos y modo Lowrider (`DriveByControls` / `DeliriumSA.ini`)**: pulsar `R3` en mando (o `MMB` en ratón) mira atrás al conducir en lugar de `L1 + R1`. En vehículos con hidráulica (*lowriders*), pulsar **`G`** en teclado o **`D-Pad Arriba`** en mando XInput (`LowriderCameraToggleKey=71`, `LowriderCameraTogglePadButton=1`) alterna entre la hidráulica activa y el modo de cámara normal para usar `R3` al mirar atrás.
- **Cobertura de CJ y policías (`PlayerCover` / `CombatRuntime` / `PoliceCover`)**: se rehacen el acceso/aproximación a cobertura y la sincronización entre cámara, orientación y pose de disparo en esquinas bajas y altas (aplicando el rumbo de la cámara al ped y actualizando el esqueleto antes y después de `CCam::Process_AimWeapon`), se bloquea el disparo mientras está cubierto sin asomarse y se mejora la orientación de policías al asomarse desde bordes.
- **Caminar con CJ y postura con armas (`NikoMovement` / `DeliriumSA.ini`)**: se añade tecla dedicada para caminar en `DeliriumSA.ini` (`Alt` mantenido mediante `WalkKey=18` o alternar con `Bloq Mayús` mediante `WalkToggleKey=20` y `DefaultWalk=0`) conservando trote y sprint, junto con caché de postura calma para evitar que se rompa la parte superior de CJ al portar armas de dos manos.
- **Disparo en ragdoll y armas soltadas (`RagdollFire` / `FirearmBridge` / `DroppedWeapons` / `MeleeDisarm`)**: permite disparar manualmente durante el ragdoll en la dirección física del arma con retroceso aplicado sobre brazo, antebrazo y torso; restringe el disparo accidental al caer al suelo solo a armas arrancadas de la mano con impacto suficiente; y registra a los agentes desarmados para su respuesta posterior.
- **Policías desarmados (`PoliceResponse`)**: los agentes sin arma de fuego o desarmados en combate evitan responder a tiros frente a amenazas armadas y recurren a la huida nativa.
- **Daño en ragdoll, eyección por parabrisas, suelos y sangre (`PlayerHighFall` / `WindscreenEjection` / `Adapter` / `BloodDecals`)**: se reduce a un cuarto (`0.25x`) el daño de caída/ragdoll para CJ, sube un 20 % el umbral de velocidad para salir despedido por el parabrisas (`windscreenPlayerDv` de `18.0` a `21.6`), se refuerza `rejectGroundTunnel` frente a suelos y techos bajos para evitar atraviesos o muertes sin motivo, y `BloodDecals` fija al suelo las manchas estáticas y permanentes de sangre para que no acompañen al cuerpo al moverse.
- **Estabilidad de conducción IV (`vehicle_lab`)**: la simulación por rueda respeta los bits de `handling` de GTA IV (`CAP_SLIDE`, `NO_LOCK`, `CAP_SPIN`, `CLAMP_SLIP` y `BURNOUT`) y el guiado direccional a alta velocidad, estabilizando deportivos rápidos como el *Hotring* al acelerar en recta.

### English

- **Built-in GTA IV aiming & *Advanced Aiming Mod* removal (`IvAim` / `Aiming` / `DriveByAim`)**: removes *Advanced Aiming Mod* from the full installer in favor of built-in GTA IV over-the-shoulder aiming both on foot and from vehicles (`IvAim=1` in `DeliriumSA.ini`), complete with wall clipping prevention, synced reticle, `R3` / `MMB` toggleable aim zoom, and scroll wheel zoom (`AimWheelZoom=1`).
- **`R3` vehicle look-behind & lowrider hydraulics toggle (`DriveByControls` / `DeliriumSA.ini`)**: pressing `R3` on controller (or `MMB` on mouse) looks behind while driving instead of `L1 + R1`. On vehicles equipped with hydraulics (*lowriders*), press **`G`** on keyboard or **`D-Pad Up`** on XInput gamepads (`LowriderCameraToggleKey=71`, `LowriderCameraTogglePadButton=1`) to switch between active hydraulics and normal camera mode (`R3` look-behind).
- **CJ & police cover (`PlayerCover` / `CombatRuntime` / `PoliceCover`)**: reworks cover approach/pathing and syncs camera aim heading and animation pose when aiming out of low and high corners (writing aiming yaw and updating bone frames around `CCam::Process_AimWeapon`), blocks accidental gunfire while hidden behind cover, and improves police peek orientation along cover edges.
- **Walking toggle & armed upper-body stability (`NikoMovement` / `DeliriumSA.ini`)**: adds dedicated walk controls (`Alt` hold via `WalkKey=18` or `Caps Lock` toggle via `WalkToggleKey=20` with `DefaultWalk=0`) while preserving jog/sprint, plus calm two-handed weapon local pose caching during slow movement.
- **Ragdoll gunfire & dropped-weapon misfires (`RagdollFire` / `FirearmBridge` / `DroppedWeapons` / `MeleeDisarm`)**: lets CJ fire along the weapon's physical direction while in ragdoll with recoil impulse applied to hand, forearm, and chest; restricts accidental floor misfires strictly to firearms knocked out of a ped's hand on heavy impact; and tracks disarmed cops.
- **Unarmed police response (`PoliceResponse`)**: disarmed officers and cops lacking a firearm fall back to native flee behavior instead of engaging with gunfire.
- **Ragdoll fall damage, windscreen ejection, ground anti-tunneling & fixed blood decals (`PlayerHighFall` / `WindscreenEjection` / `Adapter` / `BloodDecals`)**: cuts CJ's ragdoll fall damage to one quarter (`0.25x`), increases CJ's windscreen ejection delta-v threshold by 20% (`18.0` to `21.6`), strengthens `rejectGroundTunnel` against floor penetration and low ceilings, and anchors static/permanent blood splatters to the ground so they no longer slide with moving peds.
- **GTA IV vehicle stability (`vehicle_lab`)**: applies GTA IV tyre and handling flags (`CAP_SLIDE`, `NO_LOCK`, `CAP_SPIN`, `CLAMP_SLIP`, `BURNOUT`) plus high-speed steering behavior so sports cars such as the *Hotring* accelerate straight cleanly.

## 0.4.6 · cobertura, cámara, anatomía de CJ, motor e instalador / cover, camera, CJ anatomy, engine & installer

### Español

- **Anatomía de hombros y brazos de CJ (`torso.dff` / `sa-combat` / `Rig`)**: en las mallas de ropa y torso de CJ (`player.img`), los huesos `301` y `302` pesan la mitad superior del bíceps y deltoides y comparten el *socket* de `UpperArm` (`22`/`32`), mientras que en `ped.ifp` contienen una rotación estática pectoral de NPC girada 96° hacia delante que partía el brazo de CJ en zig-zag. Ahora `301`/`302` se enlazan directamente al brazo superior (`22`/`32`) tal como se exportaron en `torso.dff`, se incluyen en las máscaras de mezcla de tren superior, dejan de sobrescribirse con la pista pectoral de `ped.ifp` y omiten la sobre-elevación de reposo en las clavículas (`21`/`31`).
- **Controles de teclado para cobertura, movimiento IV, combate y gravedad (`PlayerCover` / `NikoMovement` / `CombatRuntime` / `GravityControl`)**:
  - **Cubrirse en la tecla `B`** (`CoverKey=B` configurable en `DeliriumSA.ini`, manteniendo `R1`/`RB` en mando XInput): se elimina el respaldo sobre `CPad::RightShoulder1` (`pad + 0xC`), que en teclado/ratón de PC coincidía con el Click Derecho (Apuntar) y provocaba que CJ entrase o saliese de cobertura al intentar apuntar.
  - **Movimiento a pie de GTA IV con teclado**: `W, A, S, D` trota/corre normal, `Alt + W, A, S, D` camina despacio y mantener `Espacio` esprinta directamente sin necesidad de aporrearlo (conservando el comportamiento analógico original cuando se usa mando XInput).
  - **Combate cuerpo a cuerpo y gravedad en teclado**: al fijar objetivo con Click Derecho se admiten también los atajos de GTA IV PC (`R` para ataque secundario/contraataque, `Q` para patada y `Alt` para esquivar/bloquear, además de `F`/`Enter`, `Espacio` y `Shift`), y mantener **`K`** durante 0,6 s alterna la gravedad reducida en teclado (además de `L3 + R3` en mando).
- **Cámara en cobertura independiente del apuntado en vehículo**: desactivar el apuntado desde vehículos (`DriveByShoulderAim=0`) ya no apaga la corrección de cámara al cubrirse a pie.
- **Encuadre al cambiar de hombro en cobertura**: al apuntar por el hombro contrario (esquina alta izquierda), se recupera el punto nativo de cámara previo al recorte de colisión de San Andreas y se aplica el barrido del lado activo. CJ ya no queda fuera del encuadre.
- **Continuidad en paredes bajas y cajas**: CJ puede desplazarse y cambiar de dirección a lo largo de coberturas bajas con esferas de colisión redondeadas (`woodenbox1224`) sin atascarse a mitad del recorrido, manteniendo las comprobaciones de bordes, huecos y objetos móviles.
- **Seguridad al parchear memoria (`FrozenThreads`)**: ningún parche de código nativo se escribe si falla la enumeración o suspensión de algún hilo; los hilos ya detenidos se reanudan de forma segura ante cualquier fallo.
- **Centro de colisión de vehículos en el SDK (`delirium-sdk`)**: transformar la caja de un vehículo actualiza también el offset local de su centro de colisión en `room_clear`.
- **Recuperación automática de caché en el instalador**: si un paquete descargado o en caché está incompleto o dañado, el asistente lo aparta como `.invalid-<id>` y vuelve a obtener y verificar el componente original en lugar de bloquear todos los reintentos posteriores.
- **Comprobación anticipada de permisos en Solo Delirium**: el asistente verifica que Windows permite escribir en la carpeta del juego antes de extraer la ISO, mostrando un aviso claro si faltan permisos de administrador.
- **Unificación con las mejoras del instalador 0.4.1–0.4.5**: incluye respaldo `.cmd` cuando Windows devuelve `E_NOTIMPL` al crear accesos directos `.lnk` (0.4.5), continuación sin *Urbanize* si falla su parche opcional (0.4.4), compatibilidad con variantes copiadas de idioma `.gxt`, vídeos `.mpg` y audio (0.4.3), *Grinch Trainer* desactivado por defecto en el pack completo y restauración segura de *Solo Delirium* (0.4.2), y preservación de cargadores ASI existentes junto con detección del ejecutable original de disco retail (0.4.1).

### English

- **CJ shoulder & upper-arm anatomy (`torso.dff` / `sa-combat` / `Rig`)**: on CJ's clothing and body meshes (`player.img`), bones `301` and `302` weight the upper half of the bicep/deltoid and share the exact bind socket of `UpperArm` (`22`/`32`), whereas `ped.ifp` drives them with a static NPC breast track rotated 96° forward that bent CJ's upper arms into a Z-zig-zag crease. `301`/`302` now attach to `UpperArm` (`22`/`32`) as authored in `torso.dff`'s `FrameList`, participate in upper-body blend masks, are no longer overwritten by `ped.ifp`'s chest track, and skip rest-pose elevation correction on the neck-rooted clavicles (`21`/`31`).
- **Keyboard controls for cover, IV locomotion, melee & gravity (`PlayerCover` / `NikoMovement` / `CombatRuntime` / `GravityControl`)**:
  - **Cover bound to `B` on keyboard** (`CoverKey=B` configurable in `DeliriumSA.ini`, while keeping `R1`/`RB` on XInput controllers): removes the fallback on `CPad::RightShoulder1` (`pad + 0xC`), which on PC keyboard/mouse maps to Right Mouse Button (Aim) and caused CJ to enter or leave cover whenever aiming.
  - **GTA IV on-foot movement on PC keyboard**: `W, A, S, D` jogs normally, `Alt + W, A, S, D` walks, and holding `Space` sprints directly without button mashing (preserving original Xbox 360 stick/button behavior when an XInput pad is active).
  - **Keyboard melee & reduced gravity shortcuts**: while locked on with Right Mouse Button, `R` triggers alternate punch/counter, `Q` kicks, and `Alt` dodges/blocks (alongside `F`/`Enter`, `Space`, and `Shift`), and holding **`K`** for 0.6s toggles reduced gravity on keyboard (alongside `L3 + R3` on controller).
- **Foot cover camera independent from vehicle drive-by aim**: disabling vehicle shoulder aiming (`DriveByShoulderAim=0`) no longer disables the foot cover camera callback.
- **Opposite-shoulder cover framing**: aiming over the opposite shoulder (high-left corner) recovers the pre-collision native camera anchor before running the active cover sweep, keeping CJ properly framed instead of clipped out of view.
- **Low-wall cover continuity**: CJ can slide and reverse direction across low cover obstacles whose rounded COL spheres slightly protrude (`woodenbox1224`), while preserving strict edge, gap, and moving-obstacle checks.
- **Thread-safe native code patching (`FrozenThreads`)**: native instruction patches abort cleanly and resume already-suspended threads if any thread handle or suspension call fails.
- **Vehicle collision center transform in `delirium-sdk`**: transforming a vehicle box now updates its local collision-center offset alongside the center of mass in `room_clear`.
- **Corrupted installer cache recovery**: damaged or incomplete cached archives are quarantined as `.invalid-<id>` and re-acquired/verified instead of permanently blocking subsequent install attempts.
- **Early write-access preflight in Solo Delirium**: checks folder write permissions before extracting the GTA IV ISO and explains if Administrator privileges are required.
- **Unified with 0.4.1–0.4.5 installer fixes**: includes the `.cmd` launcher fallback when Windows COM shortcuts return `E_NOTIMPL` (0.4.5), optional *Urbanize* skip on delta failure (0.4.4), copy-only localized `.gxt`/`.mpg`/audio tolerance (0.4.3), *Grinch Trainer* disabled by default in the full pack and accurate *Solo Delirium* restore target selection (0.4.2), and existing ASI loader preservation plus retail-disc executable identification (0.4.1).

## 0.4.5 · compatibilidad de accesos directos en instalaciones standalone

- Evita deshacer la instalación completa cuando el componente COM de accesos directos de Windows devuelve `NotImplementedException` / `E_NOTIMPL` (`0x80004001`), creando un lanzador `SA Delirium.cmd` de respaldo que se verifica y limpia al restaurar.
- Los informes de error incluyen la pila de llamadas gestionada y las excepciones internas para facilitar el diagnóstico.

## 0.4.4 · respaldo opcional de Urbanize

- Si falla el parche local sobre `Root.cs` de *Urbanize*, el instalador limpia sus archivos parciales y completa la instalación de SA Delirium sin ese extra opcional.

## 0.4.3 · variantes de idioma e introducción

- El instalador acepta bases de datos de texto `text/*.gxt` y vídeos `movies/*.mpg` de distintas ediciones o traducciones cuando la receta solo los copia desde el GTA San Andreas del usuario.

## 0.4.2 · Grinch Trainer desactivado por defecto y restauración Solo Delirium

- *Grinch Trainer* se entrega desactivado por defecto en `SA/Documentation/Optional/GrinchTrainerSA.asi.disabled` para evitar cierres al cargar partida combinándolo con el motor de físicas.
- Corregida la selección de carpeta al restaurar una instalación de *Solo Delirium*.

## 0.4.1 · cargadores ASI existentes y detección de ejecutable de disco

- *Solo Delirium* conserva cargadores ASI ya existentes en `vorbisFile.dll`, `winmm.dll` o `dinput8.dll` y tolera archivos de audio y `stream.ini` copiados sin parche binario.
- Identificación explícita del ejecutable original de disco retail 1.0 US (`8c609f10...`), indicando que requiere el parche 1.0 verificado.

## 0.4.0

- Delirium GTA IV simula ruedas, transmisión y suspensión al 130 %. Recupera
  los perfiles por modelo y suaviza las correcciones pequeñas de dirección.
- Clásico retocado conserva el manejo de SA y añade balanceo visual. Los
  faros, sus haces y las piezas transparentes acompañan a la carrocería.
- Cambiar entre Clásico y Delirium IV en partida conserva el tráfico.
  L1 apunta y R1 dispara desde el vehículo en los tres modos.
- El movimiento de CJ cambia desde el menú durante la partida.
- La geometría temporal de vehículos se recicla para evitar que se pare
  el motor de físicas en sesiones largas. Se reduce el trabajo al activar
  un cuerpo físico.
- En gravedad reducida, CJ y el vehículo que conduce quedan al 50 %.
  Los peatones y el resto de vehículos quedan al 1 %.

- Instalación completa o Solo Delirium desde un único asistente. Incluye
  progreso por etapas e informes de error que se pueden copiar.
- Cobertura de CJ y policías, cambio de hombro y mejoras de combate y daño.
- La suspensión responde al peso en el punto de contacto. La deformación
  localizada se puede activar por separado.

## 0.4.0 · previa privada 10

- Mantén pulsados L3 y R3 para poner al 10 % la gravedad de los NPC y los
  vehículos ajenos. CJ y el vehículo que conduce conservan la gravedad normal.

## 0.4.0 · previa privada 9

- Conducción Delirium. Cada modelo conserva su transmisión de referencia y el
  reparto de tracción del chasis de San Andreas. Se corrige el par cuando ambos
  usan un número distinto de ruedas motrices y se refuerza el arranque.
- El stick responde antes al girar a velocidad de carretera. El efecto de un
  toque de freno de mano decae también durante los pasos de suspensión nativa.
- La suspensión de Delirium sigue activa durante los fotogramas lentos dentro
  del paso máximo del temporizador de San Andreas.
- Una esquina exterior del mapa puede hundir localmente el panel que recibe
  el golpe. La deformación conserva su ajuste independiente en el menú.

## 0.3.14 · GTA IV USA para Xbox 360

- El instalador admite la compilación USA de GTA IV para Xbox 360 con Media ID `6AC07221`, además de la compilación Europa/Asia ya compatible.
- El asistente comprueba la ISO antes de copiar San Andreas. Si falla la instalación, muestra el paso y la ruta del registro en un aviso.

## 0.3.13 · barra de progreso del instalador

- Corregidos los avisos de error al mostrar el progreso durante la instalación.
- El asistente muestra el progreso completo al terminar.

## 0.3.12 · instalación en copias clásicas sin archivos de Steam

- El instalador admite copias compatibles de PC 1.0 US sin `installscript.vdf`.

## 0.3.11 · CJ borracho como Niko y sin atravesar el suelo

- Borracho: cuando el juego pone a CJ borracho (por ejemplo, con el "Efecto borracho" de Grinch Trainer), CJ se comporta como Niko totalmente borracho en GTA IV durante 180 s:
  - va con su cuerpo físico, tambaleándose con el equilibrio de GTA IV, y el stick lo inclina hacia donde quieras ir;
  - si se cae, espera quieto en el suelo, se levanta y sigue tambaleándose hasta que se acaba el tiempo;
  - la cámara de borracho dura los mismos 180 s y luego se desvanece en 10 s;
  - en las misiones se mantiene el efecto de San Andreas.
- Atropellos: CJ ya no atraviesa el suelo. Si un golpe empuja el cuerpo a través del suelo, se deshace ese paso. Si aun así queda bajo el mapa, vuelve encima del suelo.

## 0.3.10 · daño a CJ, atropellos rápidos y sin cierre sobre el agua

- CJ recibe daño de GTA IV con su cuerpo físico: al caer, al saltar de un coche en marcha, al salir despedido por una explosión y al ser atropellado. El daño sale del cálculo original de IV para cada golpe; San Andreas aplica el chaleco como siempre.
- Los atropellos a mucha velocidad ya no pierden la física: el cuerpo atropellado se quedaba fuera de la zona de física por el retraso del coche y volvía a la animación de San Andreas.
- Arreglado un cierre al volar bajo sobre el agua: el instalador llevaba unos ajustes de FxsFuncs pensados para NextGen Effects, que ya no se instala. Ahora va el archivo original del autor.
- Límite de 90 fps (FramerateVigilante). Se aplica con el limitador de fotogramas del juego activado.

## 0.3.9 · reacciones correctas con los modelos originales

- Los personajes ya no se doblan por la cintura al recibir disparos. Muchos modelos originales guardan el esqueleto inclinado 14-20° y las físicas lo tomaban como postura recta; ahora usan sus mismos huesos en postura recta.
- Un cadáver ya no se queda congelado doblado y sin colisiones: solo se congela cuando está tumbado y quieto.
- La opción de modelos originales suaviza codos y rodillas en tu PC, a partir de los modelos de tu propio juego. No se reparte ningún modelo.

## 0.3.8 · elige los modelos de personajes

- El asistente pregunta qué modelos quieres: los originales del juego o HD Skin Pack 299 de V@DO$, que se descarga sin cambios de su enlace original en GTAInside (278 MB). Si la descarga falla, se instala con los originales.

## 0.3.7 · instalador solo con lo que se puede redistribuir

- Salen del instalador los mods visuales sin permiso de redistribución registrado: HD Skin Pack, Jefferson Retextured, SA Brightened, Parallax Interiors, BSOR y LOD Vegetation, NextGen Grass, NextGen Remaster Effects, Enhance ParticleTXD, Improved Lamps, LV Gas Station Neon y Rubbish GTA 4. También sus parches de mapa y texturas.
- La jugabilidad no cambia. Se puede añadir cada mod desde su autor con Mod Loader.

## 0.3.6 · más fps, correr como en GTA IV y reacciones más sólidas

- Correr: pulsar X ya no mueve los brazos como en un sprint. Con los puños, los brazos siguen la carrera de GTA IV; los clips armados de IV no están, así que las armas a dos manos conservan los brazos de San Andreas.
- Rendimiento: los cuerpos con física ya no preparan todos los coches de la escena, solo los que pueden tocar. Menos tirones y sin el salto de fps al pasar de uno a dos pasos de física.
- Disparos: una ráfaga de Uzi o MP5 empuja el cuerpo en vez de reiniciar la reacción con cada bala. Las escopetas reparten los perdigones por las partes del cuerpo. La fuerza de cada arma sale de los datos de GTA IV de tu ISO.
- Balas cercanas: los NPC se encogen si una bala les pasa a menos de 1,5 m.
- Heridos: un NPC malherido puede quedarse tumbado y luego marcharse cojeando con las animaciones de GTA IV.
- Física y control: las animaciones de San Andreas solo vuelven cuando la física devuelve el control. Un empujón a un NPC que corre ya no acaba al instante, y un coche que golpea a alguien mientras se levanta lo vuelve a tirar.
- Choques: sin lanzamientos imposibles por cuerpos encajados bajo un coche. El parabrisas se rompe al salir despedido y las piernas no se enganchan al pasar.
- Bloqueos: guardia de cierres del juego con registro.
- Limit Adjuster 2.0.6.

## 0.3.5 · combate, policía y choques como en GTA IV

- Arreglados tres cierres del juego: en la tienda (GInput y Grinch Trainer cambiaban a la vez la misma lectura del mando), al salir del juego y al reiniciar una partida. El trainer va sin modificar.
- Pelea como en GTA IV: CJ puede quitar el arma cuerpo a cuerpo a los NPC y a los policías si esquiva su golpe y contraataca con el objetivo fijado. Vale para bate, porra, palo de golf, pala, taco, cuchillo y katana; los NPC con cuchillo o katana pelean con los movimientos de cuchillo de GTA IV.
- Policía como en GTA IV: un policía al que ataca un NPC, aunque vaya armado, saca la pistola y va a por él en vez de huir. Se apaga con `IVPoliceResponse=0` en `[Combat]`.
- Choques como en GTA IV: en un choque de frente fuerte, CJ y los NPC de los asientos delanteros salen disparados por el parabrisas. Se apaga con `WindscreenEjection=0` en `[Vehicle]`.
- Atropellos: los peatones ruedan por el capó y el parabrisas en vez de pasar por debajo del coche.
- CJ: se tira de cabeza solo con Círculo en el aire (no con dos Cuadrados), con su peso real; los empujones a peatones empiezan antes; apunta bien mientras anda; sale rápido del coche; ya no atraviesa el suelo a mucha velocidad.
- NPC: empujones en cadena entre peatones; muñecas sin dobleces imposibles; Ryder sin el cuello torcido.
- El menú de Grinch Trainer solo se abre con X + Cuadrado + Círculo.

## 0.3.4 · sin cierres en la misión de Ryder

- Arreglado un cierre del juego con la copia de Steam: al empezar una pista de radio o de audio a mitad, si su duración salía 0 el juego dividía entre cero (visto en la misión de Ryder al salir de Grove Street). Esa pista empieza ahora desde el principio.
- Arreglado un cierre al comprar comida en la misión de Ryder: Grinch Trainer 1.0 engancha la lectura del mando que usan las tiendas y, al pasarla al juego, llamaba a una dirección vacía. Delirium deja pasar esa lectura directamente al juego mientras el trainer no tenga esa dirección. El trainer va sin modificar.

## 0.3.3 · físicas de GTA IV en motos, combate y atropellos

- Motos y bicis como en GTA IV: al robar una moto, el conductor sale despedido con el ragdoll y empuja la moto al suelo; disparar o golpear a un motorista lo tira con física completa, parado o en marcha. Los atropellos de motoristas por delante, por detrás y de lado reaccionan según la velocidad.
- Los peatones se apoyan con la mano ante coches y motos lentas que se les acercan.
- Combate: los golpes largos de los NPC llegan a la cara, las animaciones de pelea van fluidas a 120 FPS y solo uno o dos atacantes pegan a la vez. CJ en el suelo recibe daño pero termina de levantarse aunque le sigan pegando.
- Caer en física no borra lo que hacía el personaje: un criminal que tropieza sigue huyendo y el policía sigue persiguiéndolo.
- El motor de físicas tiene una segunda versión para procesadores con AVX2, 1,8 veces más rápida con varios cuerpos; se elige sola al arrancar y, si el equipo no la admite, se usa la normal.
- Texturas: donde dos mods traen su propio archivo de texturas para la misma zona, se juntan las que faltaban. Arregla el suelo liso beige de Jefferson junto a Idlewood/Ganton y otras 10 zonas.

## 0.3.2 · el juego, no la tienda

- El instalador acepta cualquier copia verificable de San Andreas clásico: Steam o PC 1.0 US, incluida una copia de Steam ya degradada. Cada archivo se comprueba por su huella frente a su versión de Steam o de PC 1.0.
- Una copia que no es de Steam se instala en su propia carpeta, elegible en la ventana, con accesos directos en el escritorio y en Inicio. La carpeta del juego solo se lee.
- Desde una copia 1.0 se conserva su radio completa y sus archivos de modelos originales. Con Steam el resultado es idéntico a 0.3.1.
- El asistente dice qué edición ha encontrado y por qué sirve o no, y muestra el código de un ejecutable desconocido para poder añadirlo.
- Al reinstalar se copian las partidas de la instalación anterior; su respaldo queda intacto.
- Restaurar funciona también con rutas largas.

## 0.3.1 · todo menos las armas

- Vuelven todos los extras HD, el trainer y Urbanize. Se retiran los dos packs de armas añadidos. Siguen fuera RoSA, Proper Player Retex, las texturas de GTA IV y el HUD de GTA V.
- Urbanize se descarga automáticamente de su autor; el resto de mods va incluido.

## 0.3.0 · instalador desde Steam y la ISO

- Instalador que parte del San Andreas de Steam y de la ISO propia de GTA IV para Xbox 360, sin carpeta de desarrollo.
- Izzy's Lively Vehicle Physics 1.6 incluido con permiso de su autora.

## En desarrollo · port nativo de Steam

- Conexión visual propia mediante la API de rutas físicas de UAL: composición streaming de los IMG del propietario, integridad, caché y registro transaccional. 733 reemplazos locales probados con un actor físico nativo; son contenido personal, no el modpack final.
- Constructor de RAR con manifiesto cerrado de datos y lista UTF-16, evitando el límite de longitud de comandos de Windows.
- El capturador de pruebas arranca sin ventana de consola; se conserva el caso anterior que pausó el juego antes del último disparo.

- Adaptador de juego enlazado con 148 referencias verificadas, dirección relativa al módulo y huellas compatibles con ASLR, sin modificar el EXE. Se conserva el código de juego existente y se separan las herramientas de captura experimentales.
- Caché v3 completa: 17 diccionarios originales de IV, tablas/decisiones, esqueleto y doce técnicas generadas desde el SA del propietario; 418 clips en total. Validación de integridad antes de publicar la disponibilidad del motor.
- Primer importado y reapertura comprobados sin red ni clave en APPDATA, con soporte de formato XEX de Xenia acreditado bajo BSD.
- Prueba real de Steam con un NPC nativo: daño no letal, recuperación completa, muerte y reacción física del mismo cadáver al recibir otro disparo. Conserva su alcance: todavía no certifica combate, vehículos o modpack completo.
- Conservado el código del motor y del combate existentes en una copia fuente independiente, compilable para x86 sin el antiguo payload de constantes.
- Incorporada la corrección de pivotes adaptados de Delirium 0.6.0, con prueba que falla antes del cambio y 93 pruebas del núcleo y 68 del SDK correctas después.
- Comprobadas en Steam original la lectura del esqueleto de CJ, la importación de la colisión real del interior y una simulación de 240 pasos. Son pruebas delimitadas del laboratorio, no jugabilidad completa ni contenido del RAR alpha.4.
- Ranura de pruebas convertida de forma reversible a Steam; no requiere sustituir `main.scm` ni tocar la partida original.

## 0.1.0-alpha.4 · laboratorio

- Modo sin bordes que crea el dispositivo Direct3D en ventana y mantiene el modo del escritorio.
- La cámara nativa recibe el stick derecho XInput durante la entrada analógica aunque esté seleccionado el modo de ratón. Se conservan las preferencias originales fuera del procesamiento de cámara.
- Se mantiene el límite nativo de fotogramas. Los fixes de temporización de la copia de desarrollo necesitan adaptación a Steam antes de cambiar ese límite.

## 0.1.0-alpha.3 · laboratorio

- El RAR incorpora el preparador x86 de Delirium y su licencia. La ventana del primer arranque permite reintentar después de colocar la ISO.
- El ajuste de iluminación usa `data/timecycp.dat` de la instalación local mediante una superposición en memoria. Un filtro propio aplica la modulación de color PS2 sin desenfoque. Ambos pueden desactivarse independientemente de la suspensión.
- Sigue pendiente conectar Delirium a los peatones de Steam. Esta previa no se considera una versión jugable del port completo.

## Desarrollo · importación de la ISO

- Preparador del SDK Delirium 0.6.0 desde una imagen de GTA IV de Xbox 360 aportada por el usuario.
- Caché local por instalación. El segundo arranque puede inicializar el SDK sin la ISO ni los archivos de la instalación de GTA IV.
- Validación de la edición mediante la huella comprobada por el propio SDK.
- Mapa de lecturas para la edición comercial Europa/Asia y prueba de primer arranque con esa ISO. La sonda de Steam ya lee las jerarquías animadas de un civil y de CJ.

## 0.1.0-alpha.2 · laboratorio

- Identificada la actualización nativa de coches en Steam build 459558.
- Implementado un prototipo propio de cabeceo aéreo con el acelerador y el freno. Se mantiene desactivado por defecto.
- Añadidas pruebas de ejes de rotación, contacto de las ruedas y paso temporal.

## 0.1.0-alpha.1

- Primera previa técnica para el binario original de Steam clásico.
- Adaptación propia de amortiguación de coches mediante datos nativos cargados en memoria.
- Construcción x86 y creación del RAR con lista cerrada de archivos.
