# SA Standalone

IntegraciÃ³n privada de Delirium para GTA San Andreas clÃ¡sico de Steam, con el ejecutable original. El adaptador nativo conserva nuestras fÃ­sicas y combate existentes y comparte un Ãºnico motor con el importador. La cachÃ© v3 se genera desde la ISO de GTA IV de Xbox 360 del jugador y su instalaciÃ³n de SA; el primer arranque ya se ha comprobado sin Internet ni cachÃ© previa.

El adaptador enlazado pasÃ³ una prueba de Steam con daÃ±o no letal, recuperaciÃ³n, muerte y disparo al mismo cadÃ¡ver fÃ­sico. Las pruebas de combate, vehÃ­culos y del modpack completo siguen abiertas. **El RAR histÃ³rico alpha.4 todavÃ­a es una previa incompleta**; no confundir sus archivos con las capacidades de las fuentes actuales. VÃ©ase [el adaptador nativo](native/README.md).

La rama de laboratorio incluye un prototipo de control aÃ©reo desactivado por defecto. VÃ©ase la [verificaciÃ³n de Steam](docs/TESTING.md) para distinguir las pruebas de arranque de la conducciÃ³n pendiente.

- [InstalaciÃ³n y desinstalaciÃ³n](docs/INSTALL.md)
- [ImportaciÃ³n desde la ISO](docs/ISO-BOOTSTRAP.md)
- [CompilaciÃ³n y Releases](docs/RELEASE.md)
- [Proveniencia y dependencias](docs/DEPENDENCIES.md)
- [Pruebas](docs/TESTING.md)
- [Historial](CHANGELOG.md)
- [CrÃ©ditos](CREDITS.md) y [texto para YouTube](docs/YOUTUBE.md)

## Gameplay y guÃ­a

Espacios reservados para aÃ±adir el vÃ­deo de gameplay y la guÃ­a rÃ¡pida.

La candidata **local dev.5** ya incluye el adaptador real y SilentPatch. Se extrajo sobre una base limpia de 416 archivos originales: primer importado offline y segundo arranque sin ISO pasaron las pruebas delimitadas de actor. No es aún el modpack gráfico completo. [Recibos y límites](docs/native-gameplay-validation.json).

El constructor también admite paquetes visuales de datos mediante una receta explícita y genera sus IMG desde los originales del usuario. La candidata `dev.6.local` añade 733 reemplazos locales de skins/armas para validar esa conexión; no es el modpack completo ni un paquete autorizado para redistribución. Véase [contenido local](docs/LOCAL-CONTENT.md).
