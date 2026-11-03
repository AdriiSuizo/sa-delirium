# Instalación

Previa técnica privada para GTA San Andreas clásico de Steam, build 459558, con ejecutable original de 32 bits **gta-sa.exe**. Incluye la adaptación de suspensión de coches. El port completo se desarrolla por separado.

En alpha.2, deja `AirPitch.Experimental=0`. El control aéreo requiere pruebas de conducción y viene desactivado.

1. Cierra el juego.
2. En Steam abre la carpeta de instalación desde **Administrar → Explorar archivos locales**.
3. Extrae el RAR directamente junto a **gta-sa.exe**. Si ya tienes un **dinput8.dll**, conserva tu copia antes de instalar.
4. Arranca el juego como siempre. **SAStandalone.log**, junto al ejecutable, muestra el resultado. Ajusta la amortiguación en **scripts/SAStandalone.ini**. Usa 0 para desactivar el cambio.

## Desinstalación

Cierra el juego y retira **scripts/SAStandalone.asi**, **scripts/SAStandalone.ini**, **SAStandalone.log**, **SAStandalone-LEEME.md**, **SAStandalone-CREDITOS.md** y **SAStandalone-licenses**. Retira **dinput8.dll** si venía de este RAR y ningún otro mod lo usa. Si sustituiste un cargador existente, restaura su copia.
