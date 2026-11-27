# Pruebas de tráfico de SA Delirium 0.6.3

Se usó GTA San Andreas clásico PC 1.0 US con instalación completa, modelos originales suavizados y recursos obtenidos de una ISO de GTA IV para Xbox 360. Una escena de prueba colocó a CJ en ragdoll en el carril y empezó cada pasada con 100 de vida. Los vehículos ya circulaban con sus propios conductores y rutas. El juego resolvió frenadas, trayectorias, colisiones y daño.

La candidata inicial se probó con dos coches del tráfico simplificado. Ninguno frenó y CJ murió después del segundo atropello. La corrección aplica el límite de velocidad a la ruta simplificada de SA, mantiene la detección durante la transición a levantarse y calcula el daño de un contacto continuo con cada coche a partir de su pico. Una colisión con otro coche o tras una separación sigue causando daño.

| Modelo de SA | Entrada (m/s) | Mínima (m/s) | Avisos de frenado | Vida al final |
| --- | ---: | ---: | ---: | ---: |
| 439 | 14.10 | 0.00 | 10 | 100.0 |
| 542 | 8.52 | 0.01 | 16 | 97.8 |
| 439 | 10.03 | 0.00 | 13 | 100.0 |
| 517 | 13.34 | 0.00 | 10 | 100.0 |
| 547 | 12.01 | 0.00 | 11 | 100.0 |
| 517 | 15.00 | 0.01 | 19 | 77.3 |
| 517 | 11.66 | 0.00 | 6 | 98.4 |

También se comprobó el arranque con el binario de distribución. Las pruebas de la candidata cubrieron cuatro direcciones con el spray, apuntado con pistola, contraataques, reposo y un cambio de dirección con el Admiral. Quedan fuera del alcance la campaña completa y la matriz de mods de terceros.
