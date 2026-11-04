# VerificaciÃƒÂ³n de la previa

- CompilaciÃƒÂ³n x86 y pruebas de la transformaciÃƒÂ³n con datos sintÃƒÂ©ticos. Cobertura de filas de coche, exclusiÃƒÂ³n de motos y tipos compartidos, comentarios, lÃƒÂ­mites y entrada invÃƒÂ¡lida.
- Prueba de integridad del RAR y comprobaciÃƒÂ³n de rutas internas exactas. Se extrajo el RAR en una copia de la instalaciÃƒÂ³n de Steam build 459558. El ejecutable original conservÃƒÂ³ su SHA-256 y los archivos del juego se contrastaron con el origen.
- Arranque de la copia de Steam con y sin el cargador del paquete. Se confirmÃƒÂ³ la ventana del juego despuÃƒÂ©s de seleccionar el modo de vÃƒÂ­deo nativo en la instancia de laboratorio.
- La comparaciÃƒÂ³n leyÃƒÂ³ el gestor de handling del proceso de Steam. En LANDSTAL y BRAVURA se comprobÃƒÂ³ amortiguaciÃƒÂ³n de 0,0800 en el control y 0,0864 con el RAR. Se mantuvo la fuerza original de cada muelle. El ASI registrÃƒÂ³ 136 filas. VÃƒÂ©ase [steam-validation.json](steam-validation.json).

El ensayo comprueba carga del paquete y de los valores de suspensiÃƒÂ³n. Quedan por probar la conducciÃƒÂ³n en una partida nueva y la validaciÃƒÂ³n de cada futuro subsistema del port. Esta previa tiene alcance limitado a la suspensiÃƒÂ³n.

## Alpha.2 Ã‚Â· laboratorio

- CompilaciÃƒÂ³n x86 y pruebas de amortiguaciÃƒÂ³n y cabeceo. Las aserciones cubren cuatro casos de apoyo, aceleraciÃƒÂ³n y frenada, vehÃƒÂ­culo volcado, entrada invÃƒÂ¡lida, conservaciÃƒÂ³n de ejes ajenos al cabeceo y equivalencia de pasos con igual tiempo total.
- Se volviÃƒÂ³ a extraer el RAR desde cero sobre una copia de Steam. Coincidieron los hashes de los 416 archivos originales. El RAR tiene siete archivos, sin mods de desarrollo ni la sonda de pruebas.
- Se arrancÃƒÂ³ ese RAR con la configuraciÃƒÂ³n incluida y, en una segunda sesiÃƒÂ³n, con el cabeceo habilitado temporalmente. Solo la segunda instalÃƒÂ³ el hook. Se leyÃƒÂ³ la misma amortiguaciÃƒÂ³n y fuerza de los muelles en el gestor nativo de ambos procesos. El INI se restaurÃƒÂ³ tras las pruebas.
- En otra sesiÃƒÂ³n con una sonda, el juego aceptÃƒÂ³ entrada XInput de laboratorio y procesÃƒÂ³ al menos 600 actualizaciones nativas de coches durante el comienzo de una partida. Se observaron coches con cuatro ruedas en contacto, de las cuales dos eran motrices. La sonda no registrÃƒÂ³ a CJ conduciendo. En la repeticiÃƒÂ³n, la captura de ventana agotÃƒÂ³ el plazo y no hubo muestras de entrada o de vehÃƒÂ­culos. Estos casos no validan control aÃƒÂ©reo, aterrizajes ni conducciÃƒÂ³n.

El RAR de alpha.2 es un artefacto de laboratorio. El borrador de distribuciÃƒÂ³n permanece en alpha.1. VÃƒÂ©ase [steam-alpha2-validation.json](steam-alpha2-validation.json) para las comprobaciones realizadas.

## Lectura de peatones en Steam

La sonda de laboratorio intercalÃƒÂ³ `CPed::PreRender` despuÃƒÂ©s de su trabajo nativo. LeyÃƒÂ³ un civil y a CJ con jerarquÃƒÂ­as RenderWare de 32 nodos y matrices finitas. Se localizaron pelvis y cabeza por etiqueta en cada jerarquÃƒÂ­a. Esta prueba no aplica una pose fÃƒÂ­sica ni comprueba reacciones de juego.

El mÃƒÂ©todo compartido estÃƒÂ¡ en RVA `0x204f20`. Se verificÃƒÂ³ la entrada 17 de las tablas de civil y jugador y se comprobÃƒÂ³ el puntero original antes de instalar la sonda. La identificaciÃƒÂ³n parte de los mÃƒÂ©todos y tablas documentados en [gta-reversed](https://github.com/gta-reversed/gta-reversed/tree/master/source/game_sa/Entity/Ped).

## Alpha.3 Ã‚Â· importador y color PS2

- La copia limpia se obtuvo de Steam build 459558. Se compararon los hashes de los archivos originales despuÃƒÂ©s de extraer y arrancar el RAR.
- Sin cachÃƒÂ© ni ISO, se observÃƒÂ³ la ventana de primera instalaciÃƒÂ³n. Cancelar dejÃƒÂ³ continuar el juego. Reintentar despuÃƒÂ©s de colocar la ISO de GTA IV Europa/Asia preparÃƒÂ³ la cachÃƒÂ©. El siguiente proceso abriÃƒÂ³ esa cachÃƒÂ© con la ISO fuera de la raÃƒÂ­z.
- Se comparÃƒÂ³ la escena inicial a las 06:36 en dos arranques a 1280 Ãƒâ€” 720. El perfil PS2 registrÃƒÂ³ la primera pasada de color. El control con `Ps2TimeCycle=0` usÃƒÂ³ el posprocesado nativo. Las capturas muestran el cambio en el cielo y en la iluminaciÃƒÂ³n del entorno con el HUD original.
- Steam duplica y guarda en un byte los canales alfa del posprocesado. Las primeras ocho muestras de alfa fueron 254 en ambos modos. La diferencia visual comprobada procede del filtro, con la tabla local como entrada.
- La fixture XInput y los observadores son de laboratorio. Quedan por probar el mando fÃƒÂ­sico, el rendimiento en juego y el port de peatones de Delirium.

## Alpha.4 Ã‚Â· arranque real desde Steam

- Se comprobÃƒÂ³ la instalaciÃƒÂ³n real con el cliente de Steam y el AppID 12120. El proceso abriÃƒÂ³ `gta-sa.exe` en la biblioteca de Steam y leyÃƒÂ³ la cachÃƒÂ© del usuario. El dispositivo D3D9 registrÃƒÂ³ 2560 Ãƒâ€” 1440 en ventana. El escritorio conservÃƒÂ³ esa resoluciÃƒÂ³n y el archivo de preferencias mantuvo su huella.
- En una copia aislada con el mismo ejecutable, la fixture XInput moviÃƒÂ³ el stick derecho con el juego en modo ratÃƒÂ³n. El giro pasÃƒÂ³ de 3,10 a 1,21 radianes durante el tramo positivo. Tras centrar el stick, la vista se estabilizÃƒÂ³. La comparaciÃƒÂ³n desactivada con el mismo modo sin bordes no alcanzÃƒÂ³ la ventana de mediciÃƒÂ³n. Una prueba anterior del ejecutable sin el arreglo sÃƒÂ­ registrÃƒÂ³ entrada positiva y varios fotogramas con la vista inmÃƒÂ³vil.
- La entrada fÃƒÂ­sica del usuario sigue pendiente de prueba en juego. La fixture y su ASI quedan fuera del RAR.


## Native Steam candidate: visual and gameplay regressions

The historical suspension-only results above do not describe the full adapter.
`visual-native-validation.json` records later actor/visual/vehicle cases, pinned
by binary hashes. `dev.6.local` passed clean extraction/cold import/no-ISO warm
startup and the same real actor checks in the actual Steam directory. A separate
Steam-client launch imported resources and reached the menu; it did not inherit
the fixture environment, so it is startup-only evidence.

Native road fixtures create ordinary vehicles/peds through the host, seed one
initial vehicle velocity, then let normal game collision and our production
adapter take over. They do not inject a knock-off event, damage, a ped impulse
or repeated car motion. 15/25 m/s standing-ped cases exercise native contact;
4 m/s at the original 8 m separation missed and remains inconclusive. Rear bike
15 m/s leaves the rider mounted; 25 m/s detached the real motor-cop and completed
physical falling with zero reported errors. The melee fixture supplies only a
native aggression task; the controller and normal geometry produce its attacks,
contacts, native damage and reactions. These scopes are not complete 1:1 gameplay
or high-FPS certification.

Additional fixture ABI was checked against primary SA declarations and the
owner's original Steam code. The fixture uses module-relative addresses and
never hooks the entries below:

| Entry (preferred Steam VA) | Contract |
| --- | --- |
| 434170 | CCarCtrl::CreateCarForScript(int, CVector by value, bool), cdecl |
| 5FFF30 | CPed::Teleport(CVector by value, bool), thiscall RET16 |
| 40FF50 | CStreaming::LoadSceneCollision(const CVector&), cdecl |
| 582A50 | CWorld::FindGroundZFor3DCoord(x,y,z,bool*,entity**), cdecl/x87 |
| 631570 | CPopulation::AddPedInCar(vehicle,bool,rating,seat,bool,bool), cdecl |
| 640C70 | CTaskComplexKillPedOnFoot constructor, six stack arguments, thiscall |

The native stream.ini reader 5DA210 sets budget 914074, and native texture/model
stream records are observed in tests. New streaming/post-effect and sparse lamp
table integration must be tested on the next candidate before acceptance; adding
this documentation is not a pass. No >4 GiB IMG or LAA claim is made. Development
and external reference projects remain unchanged.


## Proved lamp dependency and native lifecycle

Candidate7 originally crashed at preferred4D086B with count71/null vtable. The
same failure occurred with new memory/post-effects disabled. DamageAtomicModels
has a fixed70-entry store; the development profile had OLA's unlimited store.

`native/steam-model-store-bindings.json` records the reviewed Steam backend,
constructor4CF610 and the five lifecycle references. CRT originals stay intact.
The backend constructs real native objects, invokes their native Init and lets
the relocated native Shutdown walk the same allocation. Case211822 loaded74;
normal WM_CLOSE produced count0 and zero remaining RW objects. This is actual
cleanup evidence, not just allocation success. Private development/Steam's good
candidate6 were kept intact while the failed package was diagnosed.

The same case confirms the new zenlamps dictionary is read from its generated
sector and assigned to model1297. It also confirms512MiB after Init2's reset,
using the configured initializer rather than a per-frame budget write. Native
post-effect flag1 is observed; this does not establish full SkyGfx pipeline parity.
The case used a manual host replacement after RAR7, so do not label RAR7 accepted.


The exact dev.8.local RAR subsequently passed cold213822, warm/noISO214027 and actual installed Steam214244. See `visual-native-dev8-validation.json`. These include model74/nativecleanup,512MiB and actor damage/recovery/corpse checks; they do not close the full pack or hardware/FPS coverage.
