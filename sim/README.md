# Simulador en lazo cerrado

Compila el **firmware real** (`control_bomba_b1_v10_3.ino`) en el PC con un reemplazo mínimo de la API de Arduino ([arduino_stub/](arduino_stub/)) y lo conecta a un **modelo hidráulico simple** (bomba, tres checks, pulmón de aire, tanque con flotador, subida con fricción, casa). Sirve para probar la **lógica de control**; no sustituye las mediciones de campo.

## Uso

Requiere `g++` (se instaló con `winget install BrechtSanders.WinLibs.POSIX.UCRT`).

```powershell
powershell -ExecutionPolicy Bypass -File sim\build.ps1      # compila sim\build\sim.exe
sim\build\sim.exe                                           # lista escenarios
sim\build\sim.exe bomba_vacio                               # un escenario con la bitácora de estados
sim\build\sim.exe normal --csv log.csv                      # P, nivel y caudales cada 1 s
powershell -ExecutionPolicy Bypass -File sim\run_all.ps1    # todos; solo muestra FALLA/INFO
.\sim\sensibilidad.ps1 -Escenarios normal,base_baja         # barrido de --v-pulmon (o -Parametro p-street / p-pre)
```
Opciones: `--quiet`, `--csv archivo`, `--v-pulmon L`, `--p-street psi`, `--p-pre psi`.

## Supuestos del modelo (NO medidos; editar en `Hyd` de sim.cpp)

| Parámetro | Valor | Origen |
|---|---|---|
| Tanque | 1.5 m de alto, Ø 0.8 m, estática 4.97 psi (vacío) a 7.10 psi (lleno) | Datos del usuario + estimación |
| Pulmón | 6 L, precarga 5.4 psi | Supuesto |
| Presión de la calle | 6 psi en la succión | Supuesto |
| Bomba | altura de cierre 25 psi sobre la calle, caída 83 psi/(L/s)² (≈0.5 L/s a 10 psi) | Supuesto |
| Subida al tanque | fricción 12.4 psi/(L/s)², entrada por arriba | Calibrado para ~10 psi de llenado |
| Check #2 | 5 psi/(L/s)² | Supuesto |
| Sensor | ruido ADC 1 cuenta por muestra | Supuesto |

## Escenarios y resultado con el firmware actual

| Escenario | Qué prueba | Resultado |
|---|---|---|
| `normal` | Llenado normal, micro-pausas | PASA |
| `calle_seca` | Escalada de esperas 5/10/20/20 min | PASA |
| `sensor_falla` | Cable suelto: falla en 1.5 s, bomba apagada, recuperación | PASA |
| `limite_45min` | Corte a 45 min y enfriamiento de 20 min | PASA |
| `base_baja` | Arranque desde la estática del tanque casi vacío | PASA con pulmón ≤ 6 L, **FALLA con ≥ 8 L** |
| `calle_vuelve` | Reanudación al volver el agua | PASA |
| `bomba_vacio` | La calle se corta en pleno llenado | **FALLA**: no se detecta; 2.409 s de bomba en vacío hasta el límite de 45 min |
| `consumo_normal` | Consumo de la casa al momento de arrancar | **FALLA**: falsa alarma de "calle seca" (espera de 5 min) |
| `consumo_alto` | Demanda continua 0.4 L/s | **FALLA**: falsas alarmas y esperas de 5/10/20 min mientras el tanque se vacía |
| `ciclado_corto` | Ráfagas de consumo cada 200 s | **FALLA**: falsas alarmas; el presupuesto de 18 arranques/h se consume casi entero |

## Hallazgos

1. **Bomba en vacío no detectada** (confirma la revisión de lógica). Al quedar sin agua, P2 cae a ~7.1 psi (columna de la subida), por encima de `P_CAIDA_SECO`, y en la micro-pausa `P2 <= P_ARRANQUE` ya se cumple, así que se confirma flujo falso y se reanuda. Corrección propuesta: exigir que P2 *cruce* los 9.5 psi durante la pausa. Pendiente de aplicar.
2. **El sondeo de 4 s depende del volumen del pulmón.** Debe llevar P2 de la base a `max(base+1.5, 9.5)` en ~3 s. Con un pulmón grande, o con la base en la estática del tanque casi vacío, no lo logra aunque haya agua: con 12 L o más el escenario `normal` nunca llena el tanque (esperas de 5/10/20 min). Hay que conocer el volumen real del pulmón.
3. **Falsa alarma de "calle seca" con consumo.** Si la casa consume al arrancar, el caudal de la bomba se va a la casa y el sondeo falla; entra a espera 5 min (la casa sigue con agua por el Check #2 a baja presión). Con demanda alta sostenida el tanque puede vaciarse mientras la bomba espera.
4. **Sobrepresión al cortar.** El corte a 20 psi exige 2 s sostenidos; con 6 L y una bomba de cierre alto el modelo llega a ~27 psi. Depende de la altura de cierre real (comando `S`) y de la tubería.
5. **Presupuesto de arranques.** Cada reanudación tras micro-pausa y cada sondeo fallido cuentan; en `ciclado_corto` se llega a 18/h.

## Límites

Resultados cualitativos. Con parámetros aproximados un FALLA indica un caso a **verificar en campo**, no un error confirmado; los PASA no garantizan nada fuera del modelo (sin golpe de ariete, sin arranque de motor, sin aire atrapado, sin variación de la calle). Re-ejecutar tras cada cambio de lógica o con los valores medidos (`--v-pulmon`, `--p-street`).
