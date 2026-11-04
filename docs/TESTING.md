# VerificaciÃ³n de la previa

- CompilaciÃ³n x86 y pruebas de la transformaciÃ³n con datos sintÃ©ticos. Cobertura de filas de coche, exclusiÃ³n de motos y tipos compartidos, comentarios, lÃ­mites y entrada invÃ¡lida.
- Prueba de integridad del RAR y comprobaciÃ³n de rutas internas exactas. Se extrajo el RAR en una copia de la instalaciÃ³n de Steam build 459558. El ejecutable original conservÃ³ su SHA-256 y los archivos del juego se contrastaron con el origen.
- Arranque de la copia de Steam con y sin el cargador del paquete. Se confirmÃ³ la ventana del juego despuÃ©s de seleccionar el modo de vÃ­deo nativo en la instancia de laboratorio.
- La comparaciÃ³n leyÃ³ el gestor de handling del proceso de Steam. En LANDSTAL y BRAVURA se comprobÃ³ amortiguaciÃ³n de 0,0800 en el control y 0,0864 con el RAR. Se mantuvo la fuerza original de cada muelle. El ASI registrÃ³ 136 filas. VÃ©ase [steam-validation.json](steam-validation.json).

El ensayo comprueba carga del paquete y de los valores de suspensiÃ³n. Quedan por probar la conducciÃ³n en una partida nueva y la validaciÃ³n de cada futuro subsistema del port. Esta previa tiene alcance limitado a la suspensiÃ³n.

## Alpha.2 Â· laboratorio

- CompilaciÃ³n x86 y pruebas de amortiguaciÃ³n y cabeceo. Las aserciones cubren cuatro casos de apoyo, aceleraciÃ³n y frenada, vehÃ­culo volcado, entrada invÃ¡lida, conservaciÃ³n de ejes ajenos al cabeceo y equivalencia de pasos con igual tiempo total.
- Se volviÃ³ a extraer el RAR desde cero sobre una copia de Steam. Coincidieron los hashes de los 416 archivos originales. El RAR tiene siete archivos, sin mods de desarrollo ni la sonda de pruebas.
- Se arrancÃ³ ese RAR con la configuraciÃ³n incluida y, en una segunda sesiÃ³n, con el cabeceo habilitado temporalmente. Solo la segunda instalÃ³ el hook. Se leyÃ³ la misma amortiguaciÃ³n y fuerza de los muelles en el gestor nativo de ambos procesos. El INI se restaurÃ³ tras las pruebas.
- En otra sesiÃ³n con una sonda, el juego aceptÃ³ entrada XInput de laboratorio y procesÃ³ al menos 600 actualizaciones nativas de coches durante el comienzo de una partida. Se observaron coches con cuatro ruedas en contacto, de las cuales dos eran motrices. La sonda no registrÃ³ a CJ conduciendo. En la repeticiÃ³n, la captura de ventana agotÃ³ el plazo y no hubo muestras de entrada o de vehÃ­culos. Estos casos no validan control aÃ©reo, aterrizajes ni conducciÃ³n.

El RAR de alpha.2 es un artefacto de laboratorio. El borrador de distribuciÃ³n permanece en alpha.1. VÃ©ase [steam-alpha2-validation.json](steam-alpha2-validation.json) para las comprobaciones realizadas.

## Lectura de peatones en Steam

La sonda de laboratorio intercalÃ³ `CPed::PreRender` despuÃ©s de su trabajo nativo. LeyÃ³ un civil y a CJ con jerarquÃ­as RenderWare de 32 nodos y matrices finitas. Se localizaron pelvis y cabeza por etiqueta en cada jerarquÃ­a. Esta prueba no aplica una pose fÃ­sica ni comprueba reacciones de juego.

El mÃ©todo compartido estÃ¡ en RVA `0x204f20`. Se verificÃ³ la entrada 17 de las tablas de civil y jugador y se comprobÃ³ el puntero original antes de instalar la sonda. La identificaciÃ³n parte de los mÃ©todos y tablas documentados en [gta-reversed](https://github.com/gta-reversed/gta-reversed/tree/master/source/game_sa/Entity/Ped).

## Alpha.3 Â· importador y color PS2

- La copia limpia se obtuvo de Steam build 459558. Se compararon los hashes de los archivos originales despuÃ©s de extraer y arrancar el RAR.
- Sin cachÃ© ni ISO, se observÃ³ la ventana de primera instalaciÃ³n. Cancelar dejÃ³ continuar el juego. Reintentar despuÃ©s de colocar la ISO de GTA IV Europa/Asia preparÃ³ la cachÃ©. El siguiente proceso abriÃ³ esa cachÃ© con la ISO fuera de la raÃ­z.
- Se comparÃ³ la escena inicial a las 06:36 en dos arranques a 1280 Ã— 720. El perfil PS2 registrÃ³ la primera pasada de color. El control con `Ps2TimeCycle=0` usÃ³ el posprocesado nativo. Las capturas muestran el cambio en el cielo y en la iluminaciÃ³n del entorno con el HUD original.
- Steam duplica y guarda en un byte los canales alfa del posprocesado. Las primeras ocho muestras de alfa fueron 254 en ambos modos. La diferencia visual comprobada procede del filtro, con la tabla local como entrada.
- La fixture XInput y los observadores son de laboratorio. Quedan por probar el mando fÃ­sico, el rendimiento en juego y el port de peatones de Delirium.

## Alpha.4 Â· arranque real desde Steam

- Se comprobÃ³ la instalaciÃ³n real con el cliente de Steam y el AppID 12120. El proceso abriÃ³ `gta-sa.exe` en la biblioteca de Steam y leyÃ³ la cachÃ© del usuario. El dispositivo D3D9 registrÃ³ 2560 Ã— 1440 en ventana. El escritorio conservÃ³ esa resoluciÃ³n y el archivo de preferencias mantuvo su huella.
- En una copia aislada con el mismo ejecutable, la fixture XInput moviÃ³ el stick derecho con el juego en modo ratÃ³n. El giro pasÃ³ de 3,10 a 1,21 radianes durante el tramo positivo. Tras centrar el stick, la vista se estabilizÃ³. La comparaciÃ³n desactivada con el mismo modo sin bordes no alcanzÃ³ la ventana de mediciÃ³n. Una prueba anterior del ejecutable sin el arreglo sÃ­ registrÃ³ entrada positiva y varios fotogramas con la vista inmÃ³vil.
- La entrada fÃ­sica del usuario sigue pendiente de prueba en juego. La fixture y su ASI quedan fuera del RAR.


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
