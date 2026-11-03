# Preparar una versión

Se necesita PowerShell, Visual Studio Build Tools con C++ x86 y Rar.exe de WinRAR. Desde la raíz del repositorio:

    powershell -ExecutionPolicy Bypass -File tools/Build-Release.ps1 -Version 0.1.0-alpha.1

El script compila el ASI, verifica por SHA-256 la descarga oficial fijada, empaqueta solo la lista permitida y comprueba el RAR. El resultado se guarda en **dist**. Se puede pasar **-LoaderArchive** con una copia local del mismo archivo oficial.

Actualiza el historial antes de generar otra versión y comprueba la extracción en una copia limpia. Adjunta el RAR y su SHA-256 a un borrador privado. La publicación pública requiere aprobación expresa del propietario.
