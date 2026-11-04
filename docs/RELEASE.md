# Preparar una versión

Se necesita PowerShell, Visual Studio Build Tools con C++ x86, Rust con el objetivo `i686-pc-windows-msvc`, Python, Git y Rar.exe de WinRAR. Desde la raíz del repositorio:

    powershell -ExecutionPolicy Bypass -File tools/Build-Release.ps1 -Version 0.1.0-alpha.5

El script comprueba el recibo de `vendor`, compila el preparador y los dos ASI propios, enlaza el adaptador con la biblioteca de importación de ESA compilación del preparador y conserva sus recibos. Verifica por SHA-256 el cargador y SilentPatch, incorpora sus licencias, comprueba dependencias y rutas permitidas y genera el RAR en **dist**. Acepta **-LoaderArchive** y **-SilentPatchArchive** con copias locales de los mismos ZIP oficiales. El motor procede de la fuente conservada; no se sustituye por otro checkout al construir.

El ejemplo indica un número de candidata, no acredita que esté generada o aceptada. La sonda y los archivos de investigación permanecen fuera del RAR. La receta actual incluye el núcleo nativo, color/iluminación PS2, suspensión, cámara/sin bordes y SilentPatch. El paquete gráfico ampliado y sus comprobaciones aún deben completarse antes de llamarlo modpack final.

El número se indica de forma explícita. Para reproducir alpha.1, usa su revisión `502a5c4`; para alpha.4, `89e8e0d`. Conserva los artefactos anteriores en vez de sobrescribirlos.

Actualiza el historial antes de generar otra versión y comprueba la extracción en una copia limpia. Adjunta el RAR y su SHA-256 a un borrador privado. La publicación pública requiere aprobación expresa del propietario.

## Data-only visual recipes

`tools/Prepare-VisualPayload.py --game <original-Steam-root> --profile <local-profile.json> --output <new-SAStandalone-assets>` prepares explicit priority-selected replacements and provenance. It never copies a base IMG. `tools/Verify-VisualPayload.py <payload>` rejects unreferenced files, path traversal, wrong sizes/hashes and executable payloads. Python/blake3 are build-time tools only.

A local profile supplies `archives` and `mods` (`id`, `path`, `priority`, `rights`, `url`, optional `extensions`/`archive`). Unknown/ambiguous model routes must be reviewed; loose models do not imply IDE/IPL/scripts are enabled.

The normal builder's optional `-VisualPayload` accepts redistributable content only. `-LocalPersonalContent` additionally requires a version containing `local` and includes a local-use notice. Never upload that local archive as a release. RAR uses a UTF-16 list file so large payloads do not hit Windows' command-line length limit.
