# Delirium desde una ISO del jugador

La instalación usa una ISO compatible de GTA IV para Xbox 360 junto a `gta-sa.exe` de GTA San Andreas clásico de Steam, build 459558. El preparador actual funciona sin Internet desde el primer arranque. Cuando el registro diga `delirium_assets_ready imported_iso`, puedes cerrar GTA SA y retirar tu ISO. La caché actual es `DeliriumSA-local-v3`; los siguientes arranques registran `delirium_assets_ready cache_ready`. Las cachés v1/v2 se conservan, pero no sustituyen silenciosamente al banco completo de recursos v3.

El importador lee la ISO sin escribirla. Genera programa, anatomía, parámetros y un banco de combate sellado: 17 diccionarios de IV, tablas/decisiones, esqueleto y doce técnicas generadas de `models/gta3.img` y `data/melee.dat` del SA instalado. Son 418 clips en total. Valida integridad, parsers y motor antes de activar el nombre definitivo y publicar su disponibilidad. Esa caché contiene datos del propietario: consérvala localmente y exclúyela de repositorios y archivos de distribución. Para reconstruirla, cierra el juego, aparta esa carpeta y vuelve a colocar la ISO junto al ejecutable.

## Equipo sin conexión

El preparador incorpora la constante pública de formato XEX del proyecto Xenia bajo BSD de tres cláusulas, con su aviso y procedencia fijada. No utiliza `curl`, Internet ni `%APPDATA%/delirium/xex.key`, ni escribe en una caché compartida con otros proyectos. Los programas, animaciones y datos de GTA IV se siguen leyendo exclusivamente de la ISO del jugador. Se comprobaron importación nueva, reapertura en otro proceso sin ISO y comparación exacta del banco con PATH vacío, APPDATA nuevo y proxies inaccesibles.

## Compatibilidad comprobada

El lector de programas procede de Delirium 0.6.0, revisión `93eed4d6a0aafa2e84526730ea1f54dab48ac7d4`. El motor de jugabilidad conserva nuestras adaptaciones y su correspondencia verificada de 992 valores. La identidad se comprueba con BLAKE3 del ejecutable descifrado:

| Disco Xbox 360 | Media ID | Huella del ejecutable |
| --- | --- | --- |
| Europa/Asia | `4A53F9F6` | `e87cf5277aa90de585b1af9b4e85d422ea2a2331419146aefdef2c7877e29964` |
| USA, referencia original del SDK | `6AC07221` | `5d164b473a0ed043502e4fbb1af8dafc68f64e87b27d22aefe64dba3bd9a0a00` |

El mapa de Europa/Asia contiene 121 cambios únicos de desplazamiento. Se localizaron por igualdad de bytes y por contexto cuando estaba disponible. El SDK valida la huella de la edición y un segundo BLAKE3 de los 770 tramos que va a leer. Todos los tramos coincidieron byte por byte con la edición de referencia. La tabla de compatibilidad contiene desplazamientos, sin copiar los valores de la ISO al repositorio. El mapa original queda disponible para la edición admitida por Delirium.

## Pruebas de desarrollo

Desde la raíz del repositorio y con Python, Git, Rust y MSVC x86:

    python tools/Verify-Vendor.py
    cargo build --release --locked --manifest-path bootstrap/Cargo.toml --target i686-pc-windows-msvc

Para la prueba del motor sin ventana, indica en `SA_DELIRIUM_TEST_ROOT` una carpeta con la caché v3 que haya generado el preparador:

    cargo test --release --locked --manifest-path bootstrap/Cargo.toml --target i686-pc-windows-msvc --lib runs_an_initialized_delirium_agent -- --ignored --test-threads=1

La prueba separada `imports_owned_iso_then_runs_without_it` exige una raíz nueva con el ejecutable propio y un enlace local `owned.iso`. Importa, aparta únicamente ese enlace y comprueba la reapertura y 120 pasos. No ejecutar ambos tests en paralelo contra la misma raíz ni distribuir sus archivos de entrada.

El ASI carga `sa_delirium_bootstrap.dll` desde `scripts` y escribe el estado en `SAStandalone.log`. Si falta la ISO y la caché, muestra la ruta y permite reintentar sin relanzar el juego; Cancelar permite continuar sin preparar esos datos. El adaptador de físicas espera la disponibilidad real del mismo módulo, sin una segunda instancia del solver ni un plazo que venza mientras el jugador coloca la ISO. Las evidencias de jugabilidad nativa se distinguen de las del importador en `native/README.md`.

En dos arranques separados de una copia de Steam, el ASI registró primero `imported_iso` con la ISO comercial Europa/Asia y luego `cache_ready` con la imagen retirada y un perfil de laboratorio sin la clave que se usa para abrir XEX. En ambos se mantuvo la carga de suspensión. La prueba x86 de 120 pasos produjo la misma huella de trayectoria con esta caché y con la caché de la edición de referencia. Alterar un byte del programa almacenado provocó el rechazo de la caché. Véase [iso-bootstrap-validation.json](iso-bootstrap-validation.json).
