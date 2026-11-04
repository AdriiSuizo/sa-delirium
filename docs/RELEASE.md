# Preparar una versión

Se necesita PowerShell, Visual Studio Build Tools con C++ x86, Rust con el objetivo `i686-pc-windows-msvc`, Python, Git y Rar.exe de WinRAR. Desde la raíz del repositorio:

    powershell -ExecutionPolicy Bypass -File tools/Build-Release.ps1 -Version 0.1.0-alpha.5

El script comprueba el recibo de las fuentes adaptadas en `vendor`, compila el ASI y el preparador, verifica por SHA-256 el cargador, empaqueta solo la lista permitida y comprueba el RAR. El resultado se guarda en **dist**. Se puede pasar **-LoaderArchive** con una copia local del mismo archivo oficial. El motor procede de la copia fuente del repositorio; no se sustituye por un checkout nuevo al construir.

El ejemplo indica el siguiente número de laboratorio, no acredita que esa versión esté generada o aceptada. La sonda nativa de Steam se conserva fuera del empaquetador mientras faltan sus conexiones de jugabilidad.

El número se indica de forma explícita. Para reproducir alpha.1, usa su revisión `502a5c4`; para alpha.4, `89e8e0d`. Conserva los artefactos anteriores en vez de sobrescribirlos.

Actualiza el historial antes de generar otra versión y comprueba la extracción en una copia limpia. Adjunta el RAR y su SHA-256 a un borrador privado. La publicación pública requiere aprobación expresa del propietario.
