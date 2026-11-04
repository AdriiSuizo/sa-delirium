# Preparar una versión

Se necesita PowerShell, Visual Studio Build Tools con C++ x86, Rust con el objetivo `i686-pc-windows-msvc`, Python, Git y Rar.exe de WinRAR. Desde la raíz del repositorio:

    powershell -ExecutionPolicy Bypass -File tools/Build-Release.ps1 -Version 0.1.0-alpha.3

El script compila el ASI y el preparador del SDK fijado, verifica por SHA-256 el cargador, empaqueta solo la lista permitida y comprueba el RAR. El resultado se guarda en **dist**. Se puede pasar **-LoaderArchive** con una copia local del mismo archivo oficial y **-DeliriumCheckout** con una copia Git de la revisión fijada del SDK.

El número se indica de forma explícita. Para reproducir alpha.1, usa su revisión `502a5c4`. Alpha.2 es la versión de laboratorio y conserva el prototipo de control aéreo desactivado.

Actualiza el historial antes de generar otra versión y comprueba la extracción en una copia limpia. Adjunta el RAR y su SHA-256 a un borrador privado. La publicación pública requiere aprobación expresa del propietario.
