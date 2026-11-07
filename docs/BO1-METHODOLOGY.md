# Metodología consultada en BO1 y aplicación a SA

Consulta de solo lectura solicitada por Adrii el 4 de noviembre de 2026.

Referencia: el proyecto local de BO1 con físicas de GTA IV.
Documentos consultados: `HANDOFF_RAGDOLL_RUST.md`,
`HANDOFF_CODEX_GTA_SHOT_20261010.md`, `HANDOFF_CODEX_RAFAGAS_20261010.md`
y `euphoria-resultados-windows.md`. No se modificó ni compiló ese proyecto.

## Lo transferible

- Separar motor, ABI, propietario del esqueleto, hitboxes y lógica nativa. Una
  caída correcta en el banco de pruebas no demuestra que el personaje del juego
  tenga esas colisiones o que la IA respete su estado físico.
- Capturar cada impacto con punto, dirección, cuerpo y tiempo antes de evaluar
  la pose. Sumar impactos en una única fuerza puede cancelar fuerzas opuestas y
  perder el momento que tendrían aplicado en lugares diferentes.
- Mantener memoria temporal y sincronización por actor. Los resultados de BO1
  documentan fallos por compartir scratch del esqueleto y por cambiar poses
  mientras otras consultas las leen. Antes de corregir SA hay que demostrar que
  ese mismo problema existe aquí; esta consulta no demuestra una regresión.
- Verificar que navegación, dolor, recuperación y movimiento nativo no recuperen
  autoridad mientras el cuerpo siga simulado. Tampoco suprimir una respuesta
  nativa si no existe un cuerpo físico válido que la sustituya.
- Conservar el estado previo, comparar la misma escena y registrar binarios,
  fuentes y límites. Coste de una DLL aislada y FPS del juego son mediciones
  distintas.

No se importan calibraciones de impulsos de BO1 como si fueran valores originales
de GTA IV. Su propio handoff las identifica como experimentales. Tampoco se
mezclan sus líneas clean-room y de investigación del runtime.

## Aplicación concreta en esta continuación

El trabajo actual está en `release-work/evidence/legacy-iso-abi` de GTA SA.
Se conservaron las fuentes y binarios de desarrollo y de Steam. Se crearon dos
copias locales del SDK adaptado para comparar el lector de constantes anterior
con uno que lee el programa preparado desde la ISO del propietario.

La comparación bit a bit de las 992 entradas pasa. Las ocho entradas sin un slot
directamente reutilizable en el SDK nuevo proceden del código GPL anterior de
Delirium, no de valores calibrados para disimular un fallo: RNG, centro del cuerpo,
fase de disparo, tolerancia de contacto e identificadores de tareas. Véase el
informe del laboratorio para las procedencias concretas.

También se probó un importador aislado enlazado con nuestro SDK adaptado. Importó
la ISO, generó el cuerpo con su propio constructor, abrió de nuevo la caché tras
retirar la ISO y simuló 120 pasos de caída con poses finitas. El constructor del
SDK 0.6 no produce el mismo fichero que el del SDK adaptado; no se debe sustituir
uno por otro basándose únicamente en que ambos declaran 21 segmentos.

Esta evidencia no acredita el port del adaptador al ejecutable original de Steam.
Tampoco acredita las interacciones del cuerpo generado con la pose de CJ en ese
ejecutable. Las pruebas de esqueleto, colisiones y autoridad dentro del juego
siguen siendo necesarias antes de incluir este motor en el RAR.
