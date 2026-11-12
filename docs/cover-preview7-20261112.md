# Cobertura: previa 7 privada (2026-11-12)

CorrecciÃ³n motivada por los tirones observados jugando con mando, no una certificaciÃ³n de todas las misiones ni de todos los modelos.

## Fallos encontrados y cambios

- El movimiento hacia la posiciÃ³n de asomarse pedÃ­a corregir toda la distancia en un solo paso, mientras SA integraba su velocidad con retraso. En las trazas anteriores seguÃ­a oscilando 6â€“10 cm en reposo. Ahora usa una aproximaciÃ³n proporcional limitada, manteniendo la resoluciÃ³n nativa de colisiones.
- La detecciÃ³n de esquina utilizaba dos condiciones de entrada y permanencia que podÃ­an contradecirse en un mismo punto. La condiciÃ³n de permanencia incluye ahora la de entrada, y se comprueba tambiÃ©n mÃ¡s allÃ¡ de una posible junta entre colisiones.
- La pose anterior se mezclaba en un marco que giraba con CJ; ademÃ¡s, se sobrescribÃ­an despuÃ©s los brazos. La transiciÃ³n se mezcla finalmente en coordenadas con orientaciÃ³n del mundo y origen del personaje. Se distinguen el cambio de movimiento, arma y acciÃ³n, incluyendo una segunda rÃ¡faga durante la salida de la anterior.
- La salida y el regreso conservan la esquina y el lado con los que empezaron. El desplazamiento horizontal del clip no se suma al desplazamiento del personaje.
- El apuntado conserva la direcciÃ³n previa de cÃ¡mara y comprueba la obstrucciÃ³n de su plano cercano. La cadena superior nativa se orienta hacia el rayo de la mira. El disparo a ciegas mantiene su fase expuesta mientras se pulsa fuego, orienta la cadena del brazo (ambos brazos y pecho para rifle), y dispara desde la boca visible conservada del frame anterior. SA conserva municiÃ³n, recarga y colisiones.
- Los pickups creados por la funciÃ³n nativa de muerte se incorporan a la presentaciÃ³n ya usada por el desarme: caÃ­da, reposo sin giro y luz naranja sobre el suelo. Se conserva el ciclo de vida, la municiÃ³n y los pickups ajenos a esas muertes.

## Pruebas y lÃ­mites

Las sondas guardan todos los frames evaluados: reloj, posiciÃ³n de CJ, destino, velocidad, orientaciÃ³n, estado de cobertura, cÃ¡mara y posiciones de pelvis, cabeza, pecho, hombro, codo y manos. El anÃ¡lisis incluye los frames interiores del movimiento, no solo los cambios de estado. Las lecturas de pantalla introducen algunos pasos de 35â€“70 ms; el informe conserva el desplazamiento bruto y usa tambiÃ©n el tiempo del frame para detectar saltos.

Se conservaron las iteraciones fallidas. Una fila de cajas de laboratorio se desplazÃ³ y perdiÃ³ cobertura; la repeticiÃ³n fija Ãºnicamente esos obstÃ¡culos de prueba. La cÃ¡mara de mando se activa dentro del proceso aislado, sin entrada al escritorio. Las pruebas de pistola, AK, escopeta (16/8 rayos), seguimiento de impactos, muerte y desarme pasaron en la revisiÃ³n previa a la Ãºltima correcciÃ³n de mezcla; esa Ãºltima modificaciÃ³n solo cambia el mezclador de poses y se vuelve a probar en cobertura.

La compilaciÃ³n de entrega excluye las sondas de pruebas. No se publica 0.4.0 ni se modifica el repositorio pÃºblico. Queda pendiente la valoraciÃ³n del usuario con su mando y su situaciÃ³n concreta. No se repitieron misiones.

## Resultado de entrega

- Caso final `20261112-013153-cover-final-transition-check`: 4.752 frames evaluados. Centro bajo, dos sentidos de desplazamiento/giro, pistola y AK a ciegas, y cuatro esquinas baja/alta. Las ventanas de apuntado estable no presentan desplazamiento lateral/normal medible en la precisión de la traza; desviación del cañón respecto al rayo de prueba inferior a 0,33°. Regreso a cobertura comprobado. Dos armas creadas por una muerte nativa llegan al suelo.
- `20261112-012553-cover-preview7-final`: regresión de cobertura y policía completa, seguida por armas/cadencias, escopeta, cadena de impactos, muerte, desarme y cambio de objetivo, sin errores del motor. La primera escena central de esa pasada perdió su cobertura; no se cuenta como aprobada y se sustituyó por la escena fija del caso final.
- Instalada únicamente la ASI de entrega en `SA/scripts/DeliriumSA.asi`. DLL de físicas sin cambios. Respaldo de la previa 6 junto a la instalación. SHA-256: `56ddaa8194558f68ba7db07b3edd601fb24a011a1536df377f7bb1dba3a05509`.
- Arranque normal sin GameTestBridge ni variables de pruebas: proceso 23100, evento `initialized`, permanece vivo durante cinco segundos adicionales; cerrado después de comprobarlo. El launcher de prueba manual permanece disponible.
- Las capturas se inspeccionaron visualmente. Los registros de la sesión permanecen en el laboratorio.
