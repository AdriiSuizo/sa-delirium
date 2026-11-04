# Instalación

Candidata privada para GTA San Andreas clásico de Steam, build 459558, con el ejecutable original de 32 bits **gta-sa.exe**. Conserva una copia de cualquier mod que ya utilices antes de sustituir archivos.

1. Cierra el juego y abre su carpeta desde Steam: **Administrar → Explorar archivos locales**.
2. Extrae el RAR directamente junto a **gta-sa.exe**. Si ya existe **dinput8.dll**, conserva primero tu copia.
3. Para el primer arranque, coloca en esa carpeta la ISO compatible de tu copia de GTA IV para Xbox 360. Si falta, la ventana indica la ruta y permite **Reintentar** o **Cancelar**.
4. Abre el juego desde Steam o su ejecutable. El primer importado funciona sin Internet y genera **DeliriumSA-local-v3** usando también los archivos de tu SA instalado.
5. Cuando **SAStandalone.log** muestre `delirium_assets_ready imported_iso`, puedes retirar la ISO. Los siguientes arranques utilizan la caché local.

El paquete actual conecta el motor de físicas y combate, el importador, la iluminación/color PS2, el modo sin bordes, el stick derecho XInput, el ajuste de suspensión y SilentPatch. Sigue siendo una candidata: el paquete gráfico ampliado y las pruebas finales de combate y vehículos continúan en desarrollo.

Las opciones generales están en **DeliriumSA.ini**. El modo sin bordes, la cámara, el filtro PS2 y la suspensión propia se configuran en **scripts/SAStandalone.ini**. El control aéreo experimental permanece desactivado. La instalación no sustituye el ejecutable ni incluye programas, animaciones, partidas o archivos originales extraídos de juegos.

## Desinstalación

Cierra el juego y retira **scripts/DeliriumSA.asi**, **DeliriumSA.ini**, **scripts/SAStandalone.asi**, **scripts/SAStandalone.ini**, **scripts/sa_delirium_bootstrap.dll**, **scripts/SilentPatchSA.asi**, **scripts/SilentPatchSA.ini**, **SAStandalone-LEEME.md**, **SAStandalone-CREDITOS.md** y **SAStandalone-licenses**. Restaura los componentes que ya tenías si procedían de otra instalación.

Puedes borrar **DeliriumSA-local-v3**, **SAStandalone.log** y **DeliriumSA-native-startup.log** para retirar los datos y registros generados. Las cachés antiguas **DeliriumSA-local-v1/v2** también son opcionales. Retira **dinput8.dll** solo si venía de este RAR y ningún otro mod lo utiliza.

`SAStandalone-CONTENTS.json` identifica exactamente la candidata y sus componentes. También se puede retirar al desinstalar. El presupuesto de streaming usa el lector nativo de `stream.ini`, sin sustituir ese archivo: `Streaming/MemoryMiB` admite 0 para conservar el valor nativo, o 50–512 MiB en el ejecutable Steam sin LAA. `Visuals/Ps2PostEffects` controla los ajustes nativos adicionales; no equivale a todos los pipelines de SkyGfx.

## Capa visual opcional

Si el RAR incluye `SAStandalone-assets`, el primer inicio compone `SAStandalone-asset-cache` desde tus archivos originales sin sustituirlos. Los siguientes inicios reutilizan esa caché. Retira también esas dos carpetas, `SAStandalone-assets-active.tsv` y `LOCAL-CONTENT.md` al desinstalar esa capa. Un archivo marcado `.local` es una candidata personal; no concede permisos de redistribución ni representa el paquete completo.
