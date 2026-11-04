# Delirium desde una ISO del jugador

El flujo de la futura versión jugable usa una ISO compatible de GTA IV para Xbox 360 junto a `gta-sa.exe` de GTA San Andreas clásico de Steam, build 459558. La primera vez el lector de XEX de Delirium puede necesitar Internet. Cuando el registro diga `delirium_assets_ready imported_iso`, el jugador puede cerrar GTA SA y retirar su ISO. Los arranques siguientes leen `DeliriumSA-local-v1` y registran `delirium_assets_ready cache_ready`.

El importador lee la ISO sin escribirla. Genera una caché local de cuatro archivos y solo le da su nombre definitivo después de que el SDK haya abierto los datos. La caché contiene datos de la copia del jugador. Consérvala localmente y exclúyela de repositorios y archivos de distribución. Para reconstruirla, cierra el juego, aparta esa carpeta y vuelve a colocar la ISO junto al ejecutable.

## Equipo sin conexión

El primer descifrado de una ISO cifrada requiere la clave XEX que usa el SDK. El SDK busca su copia local en `%APPDATA%/delirium/xex.key`. Si falta, intenta obtenerla de la fuente fijada por Delirium. El RAR contiene el importador y las licencias. Para preparar un equipo sin Internet, se puede generar antes la caché con la ISO del jugador y llevar esa caché por separado. Los arranques desde la caché validada prescinden de la red y de la clave.

## Compatibilidad comprobada

El SDK fijado es Delirium 0.6.0, revisión `93eed4d6a0aafa2e84526730ea1f54dab48ac7d4`. La huella BLAKE3 del ejecutable Xbox 360 de referencia es `5d164b473a0ed043502e4fbb1af8dafc68f64e87b27d22aefe64dba3bd9a0a00`. La edición comercial Europa/Asia ensayada tiene la huella `e87cf5277aa90de585b1af9b4e85d422ea2a2331419146aefdef2c7877e29964`.

El mapa de Europa/Asia contiene 121 cambios únicos de desplazamiento. Se localizaron por igualdad de bytes y por contexto cuando estaba disponible. El SDK valida la huella de la edición y un segundo BLAKE3 de los 770 tramos que va a leer. Todos los tramos coincidieron byte por byte con la edición de referencia. La tabla de compatibilidad contiene desplazamientos, sin copiar los valores de la ISO al repositorio. El mapa original queda disponible para la edición admitida por Delirium.

## Pruebas de desarrollo

Desde la raíz del repositorio y con Python, Git, Rust y MSVC x86:

    python tools/Prepare-DeliriumSdk.py
    cargo build --release --locked --manifest-path bootstrap/Cargo.toml --target i686-pc-windows-msvc

Para la prueba del motor sin ventana, indica en `SA_DELIRIUM_TEST_ROOT` una carpeta con la caché que haya generado el preparador:

    cargo test --release --locked --manifest-path bootstrap/Cargo.toml --target i686-pc-windows-msvc --lib -- --ignored

El ASI de alpha.3 carga `sa_delirium_bootstrap.dll` desde `scripts` y escribe el estado en `SAStandalone.log`. Cuando falta la ISO y aún no existe la caché, muestra la ruta de instalación y permite reintentar sin relanzar el juego. Cancelar permite iniciar el juego sin preparar esos datos. Un error de la imagen o de la caché muestra su diagnóstico. La integración de contactos, poses y tareas de peatones durante una partida sigue pendiente en Steam.

En dos arranques separados de una copia de Steam, el ASI registró primero `imported_iso` con la ISO comercial Europa/Asia y luego `cache_ready` con la imagen retirada y un perfil de laboratorio sin la clave que se usa para abrir XEX. En ambos se mantuvo la carga de suspensión. La prueba x86 de 120 pasos produjo la misma huella de trayectoria con esta caché y con la caché de la edición de referencia. Alterar un byte del programa almacenado provocó el rechazo de la caché. Véase [iso-bootstrap-validation.json](iso-bootstrap-validation.json).
