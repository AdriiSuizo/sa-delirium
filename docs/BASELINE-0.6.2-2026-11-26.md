# Base de trabajo para una posible 0.6.2

**Registro inicial, conservado como histórico.** El trabajo posterior, las correcciones aplicadas y las pruebas en partida están en [AUDIT-0.6.2-2026-11-26.md](AUDIT-0.6.2-2026-11-26.md). Las afirmaciones de esta página sobre pruebas todavía no realizadas corresponden al inicio de la revisión.

## Alcance confirmado

El usuario elige continuar primero con el mod público que utiliza la ISO de GTA IV. Después se retomará el sustituto independiente y la reducción de otras dependencias donde sea viable. Esta revisión no autoriza publicar 0.6.2 ni declara terminada esa versión.

## Estado público comprobado el 26 de noviembre de 2026

- Repositorio: https://github.com/AdriiSuizo/sa-delirium (público).
- Última release oficial: v0.6.0, publicada el 25 de noviembre a las 04:20:47 UTC, sin marca de prerelease.
- Base main: f65fb8a4f47663feff24174dbccdd587513f799f.
- No se encontró release 0.6.1 ni 0.6.2 en el listado reciente, ni una rama con esos nombres en el inventario remoto.
- La 0.6.0 anuncia 15 perfiles de locomoción de NPC, poses de disparo en vehículos, mejoras de cobertura/sigilo, rifles, recuperación y audio. Son prestaciones documentadas; esta auditoría no las ha certificado en partida.
- El instalador mantiene los modos completo y Solo Delirium. Sigue requiriendo una ISO compatible de GTA IV Xbox 360 y una instalación clásica de SA admitida. No equivale a admitir todos los ejecutables clásicos.
- La PR #6 de diagnósticos de permisos sigue abierta. Sus cambios principales de Solo.cs ya aparecen en main: CheckWriteAccess, creación del staging dentro del try y diagnóstico de acceso denegado. No fusionarla a ciegas: también contiene un workflow histórico de publicación de prueba.

## Aislamiento

Se clonó main en sa-delirium-062-audit y se creó la rama local work/v0.6.2-baseline. El checkout sa-delirium-vehicle-lab conserva cambios locales de noviembre 13 sobre la etapa 0.4.0: no se sobrescribió ni se utilizó como base actual. El experimento D:\Delirium-Independent-Concept permanece separado. No se modificó ninguna instalación del juego ni se publicó nada.

## Comprobaciones ejecutadas

1. Compilación de las fuentes actuales del instalador con .NET Framework y PortablePackages.Tests.cs: correcta.
2. 22 pruebas sintéticas de paquetes y ediciones: PASS. Incluyen extracción, rutas peligrosas, cancelación, identificación, variantes de archivos copiables, instalación/restauración Solo y completa, conservación de partidas, loader/alias verificado, DirectX e ISO inválida. Son fixtures, no partidas ni una instalación completa con ISO real.
3. Test-UnifiedCandidateMetadata.py: PASS. Comprueba metadatos, preservación de bytes, AVX2 opcional y distinción estable/prerelease. Sus versiones de fixture siguen siendo 0.4.0/0.4.1: no acredita por sí solo un paquete 0.6.2.
4. La primera ejecución de paquetes se detuvo por el límite de longitud de ruta de SA. La repetición en una ruta más corta pasó. El límite debe conservarse y explicarse; no se considera un fallo de compatibilidad del motor.

Evidencia local: ../../logs/baseline-062-20261126 y ../../../logs/q62/results.json.

## Trabajo propuesto, en orden

1. Actualizar la documentación de compatibilidad y pruebas sin borrar la evidencia histórica. docs/TESTING.md y docs/DEPENDENCIES.md describen previas antiguas y no deben interpretarse como el estado actual de 0.6.0.
2. Preparar validación y empaquetado independientes de números antiguos. Los workflows de publicación observados siguen nombrados para 0.4.x y sus ejecuciones de la release 0.6.0 aparecen omitidas. Revisar el camino real del empaquetado actual antes de sustituirlos.
3. Auditar las novedades de 0.6.0 en su código y sus pruebas: propiedad de animaciones, transiciones de cobertura/recuperación, disparos desde vehículos y convivencia entre módulos. Corregir fallos reproducibles, con pruebas dirigidas y una base de comparación.
4. Verificar el instalador real completo y Solo en laboratorio, incluidas instalación, cancelación y restauración, preservando los archivos del usuario. No reutilizar aceptaciones de builds anteriores como evidencia de una nueva.
5. Reunir cambios, límites y evidencias en una candidata local. Elegir el número final y publicar únicamente cuando el usuario lo indique.
6. Después, retomar el sustituto independiente: inventario de cada recurso/runtime requerido, procedencia, licencia, función y reemplazo. Reutilizar las mejoras del adaptador público que sean compatibles. No sustituir dependencias por copias de contenido protegido ni prometer eliminar las necesarias para el juego anfitrión.

No se ha iniciado el juego, evaluado FPS ni certificado ausencia de crashes en misiones durante esta auditoría.
