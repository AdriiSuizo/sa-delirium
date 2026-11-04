# Instalación

Previa técnica privada para GTA San Andreas clásico de Steam, build 459558, con ejecutable original de 32 bits **gta-sa.exe**. Incluye el importador de Delirium, un filtro de color PS2 con la tabla de iluminación del propio juego y el ajuste de suspensión. En esta previa, los peatones de Steam todavía no usan el motor de Delirium.

En alpha.4, el juego abre en ventana sin bordes al tamaño del escritorio y la cámara usa el stick derecho de un mando XInput activo. Puedes desactivar cada ajuste en `scripts/SAStandalone.ini` con `Borderless=0` o `RightStickCamera=0`. El control aéreo experimental sigue desactivado.

1. Cierra el juego.
2. En Steam abre la carpeta de instalación desde **Administrar → Explorar archivos locales**.
3. Extrae el RAR directamente junto a **gta-sa.exe**. Si ya tienes un **dinput8.dll**, conserva tu copia antes de instalar.
4. En el primer arranque, coloca junto al ejecutable la ISO de tu copia de GTA IV de Xbox 360. Si falta, una ventana muestra la carpeta y ofrece **Reintentar** o **Cancelar**. Windows localiza los botones según su idioma. La apertura inicial de la imagen puede requerir Internet.
5. Una vez que **SAStandalone.log** muestre `delirium_assets_ready imported_iso`, puedes retirar la ISO. Los arranques siguientes usan la caché local.

En **scripts/SAStandalone.ini**, usa `Ps2TimeCycle=0` para restaurar la iluminación y el filtro de PC o `DampingIncreasePercent=0` para desactivar el ajuste de suspensión. El ciclo usa el archivo local de Steam. Los demás efectos de SkyGfx y los packs visuales siguen pendientes.

## Desinstalación

Cierra el juego y retira **scripts/SAStandalone.asi**, **scripts/SAStandalone.ini**, **scripts/sa_delirium_bootstrap.dll**, **SAStandalone.log**, **SAStandalone-LEEME.md**, **SAStandalone-CREDITOS.md** y **SAStandalone-licenses**. Retira **DeliriumSA-local-v2** si quieres borrar la caché generada; las previas antiguas usaban **DeliriumSA-local-v1**. Retira **dinput8.dll** si venía de este RAR y ningún otro mod lo usa. Si sustituiste un cargador existente, restaura su copia.
