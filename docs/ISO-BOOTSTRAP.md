# Delirium desde una ISO del jugador

El flujo de la futura versión jugable usa una ISO compatible de GTA IV para Xbox 360 junto a `gta-sa.exe` de GTA San Andreas clásico de Steam, build 459558. La primera vez el lector de XEX de Delirium puede necesitar Internet. Cuando el registro diga `delirium_assets_ready imported_iso`, el jugador puede cerrar GTA SA y retirar su ISO. Los arranques siguientes leen `DeliriumSA-local-v1` y registran `delirium_assets_ready cache_ready`.

El importador lee la ISO sin escribirla. Genera una caché local de cuatro archivos y solo le da su nombre definitivo después de que el SDK haya abierto los datos. La caché contiene datos de la copia del jugador. Consérvala localmente y exclúyela de repositorios y archivos de distribución. Para reconstruirla, cierra el juego, aparta esa carpeta y vuelve a colocar la ISO junto al ejecutable.

## Compatibilidad comprobada

El SDK fijado es Delirium 0.6.0, revisión `93eed4d6a0aafa2e84526730ea1f54dab48ac7d4`. La huella BLAKE3 del ejecutable Xbox 360 abierto que valida esta revisión es `5d164b473a0ed043502e4fbb1af8dafc68f64e87b27d22aefe64dba3bd9a0a00`.

La ISO de GTA IV Europa/Asia ensayada se abre correctamente, pero su ejecutable tiene otra huella y 122 referencias del SDK contienen bytes distintos en los mismos desplazamientos. La huella sigue siendo una condición para inicializar. La prueba positiva usó una imagen XDVDFS de laboratorio creada localmente con los cuatro archivos necesarios de una copia extraída compatible. La imagen de laboratorio y la caché quedan fuera de Git y del RAR.

## Pruebas de desarrollo

Desde la raíz del repositorio y con Rust y MSVC x86:

    cargo build --release --locked --manifest-path bootstrap/Cargo.toml --target i686-pc-windows-msvc

Para la prueba del motor sin ventana, indica en `SA_DELIRIUM_TEST_ROOT` una carpeta con la caché que haya generado el preparador:

    cargo test --release --locked --manifest-path bootstrap/Cargo.toml --target i686-pc-windows-msvc --lib -- --ignored

El ASI de laboratorio carga `sa_delirium_bootstrap.dll` desde `scripts` y escribe el estado en `SAStandalone.log`. El paquete alpha.1 y el RAR de laboratorio alpha.2 son anteriores a este preparador. La inicialización del SDK y la prueba de un agente acreditan los datos y el motor. La integración de contactos, poses y tareas de peatones durante una partida sigue pendiente en Steam.

En dos arranques separados de una copia de Steam, el ASI registró primero `imported_iso` y luego `cache_ready` con la imagen retirada y un perfil de laboratorio sin la clave que se usa para abrir XEX. En ambos se mantuvo la carga de suspensión. Véase [iso-bootstrap-validation.json](iso-bootstrap-validation.json).
