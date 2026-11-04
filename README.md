# SA Delirium

El mod de desarrollo, con un asistente para instalarlo y abrirlo desde el botón
**Jugar** de San Andreas clásico en Steam. Conserva las físicas, combate,
movimientos, controles y el contenido de la copia de desarrollo. Retira el
minimapa y la rueda de armas del paquete GTA V HUD; conserva GTA V Weapons.

## Instalar

Descarga **SA-Delirium-Setup.exe** o el RAR de la
[última entrega](https://github.com/AdriiSuizo/sa-delirium/releases/tag/v0.2.0-dev-import.1).
Abre el asistente, revisa las rutas detectadas y pulsa **Comprobar → Instalar**.
Después usa **Jugar** en Steam. No hay que copiar archivos a mano.

Necesitas Windows 10/11 con .NET Framework 4.8, San Andreas clásico de Steam
(AppID 12120) y **la copia local de desarrollo correspondiente a esta entrega**.
El asistente incluye nuestro adaptador y motor de Delirium; importa desde esa
copia los archivos del juego y el resto de los mods, sin redistribuirlos.
Esta entrega no descarga ni reconstruye desde cero el modpack en otro PC.

El asistente busca las bibliotecas de Steam y el acceso directo del escritorio.
Si falta una ruta, permite elegirla con **Examinar**. Copia unos 20,7 GiB y
comprueba los archivos con SHA-256. [Instrucciones completas](installer/LEEME.txt).

## Qué conserva

El índice contiene 39 componentes solicitados del catálogo, incluidos Urbanize,
modos gráficos, mapas y vegetación, efectos, texturas, skins, armas y sonidos,
trainer, controles, ajustes de FPS y las físicas existentes. También conserva
los archivos jugables encontrados fuera del catálogo. Se excluyen el paquete
GTA V HUD y los registros, capturas y sondas de pruebas.

La instalación crea una copia independiente en la carpeta **SA** dentro de la
instalación de Steam. El ejecutable original se respalda y un lanzador propio
inicia el juego de desarrollo, esperando a que termine para mantener su duración
en Steam. Los plugins del port experimental R2 quedan fuera de ese proceso.
La versión interna cambia a la que utiliza desarrollo; esta entrega no utiliza
el ejecutable original R2 para ejecutar el juego.

Por defecto se copian las partidas y ajustes a ese perfil independiente.
El origen y las partidas de Documentos permanecen intactos. **Restaurar Steam**
recupera el ejecutable anterior y conserva el perfil retirado y sus partidas
en el respaldo. La instalación rechaza bibliotecas con rutas que excedan el
límite nativo de los nombres de partida de SA 1.0.

## Fuentes, compilación y pruebas

- [Asistente y lanzador](installer/), [compilación](tools/Build-Installer.ps1),
  [empaquetado](tools/Package-Installer.py).
- [Contrato de contenido](installer/profile-index.json) y
  [petición confirmada](installer/REQUEST.txt).
- [Pruebas del instalador](tests/InstallerTests.cs) y
  [verificación del juego](tests/Verify-DevelopmentInstall.py).
- [Revisión de forks y upstream](installer/fork-update-review.json).
- [Créditos](CREDITS.md), [licencia GPLv3](LICENSE.md) y avisos en `licenses/`.

La entrega acompaña las fuentes correspondientes a los binarios de desarrollo.
Los ejecutables de juegos, recursos extraídos, partidas y paquetes externos
no se publican. Desarrollo asistido por Codex.

El port anterior al ejecutable original de Steam se conserva como investigación
separada: [estado histórico R2](docs/NATIVE-R2-HISTORY.md),
[adaptador nativo](native/README.md). Los RAR `dev.8.local` y el laboratorio
gráfico ampliado pertenecen a ese trabajo anterior.
