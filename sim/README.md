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
Opciones: `--quiet`, `--csv archivo`, `--v-pulmon L`, `--p-street psi`, `--p-pre psi`, `--h0 psi` (altura de cierre de la bomba sobre la calle).

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

## Escenarios y resultado con el firmware actual (9 de 12 pasan; fallan los tres de falsa alarma por consumo)

| Escenario | Qué prueba | Resultado |
|---|---|---|
| `normal` | Llenado normal, micro-pausas | PASA |
| `calle_seca` | Escalada de esperas 5/10/20/20 min | PASA |
| `sensor_falla` | Cable suelto: falla en 1.5 s, bomba apagada, recuperación | PASA |
| `limite_45min` | Corte a 45 min y enfriamiento de 20 min | PASA |
| `base_baja` | Arranque desde la estática del tanque casi vacío | PASA con pulmón ≤ 12 L, FALLA con 20 L |
| `pulmon_retenido` | Pulmón retenido a 9 psi por el Check #1, P2 plana, sin agua | PASA: nunca pasa a BOMBEO, declara sin agua |
| `bomba_debil` | Llenado con la bomba degradada (cierre 25 psi) | PASA con pulmón ≤ 12 L (antes de la nueva detección fallaba con cualquier volumen) |
| `calle_vuelve` | Reanudación al volver el agua | PASA |
| `bomba_vacio` | La calle se corta en pleno llenado | PASA: se detecta a los ~11 s del corte (originalmente no se detectaba: 2.409 s en vacío; tras la corrección de la micro-pausa: 184 s) |
| `consumo_normal` | Consumo de la casa al momento de arrancar | **FALLA**: falsa alarma de "calle seca" (espera de 5 min) |
| `consumo_alto` | Demanda continua 0.4 L/s | **FALLA**: falsas alarmas y esperas de 5/10/20 min mientras el tanque se vacía |
| `ciclado_corto` | Ráfagas de consumo cada 200 s | **FALLA**: falsas alarmas; el presupuesto de 18 arranques/h se consume casi entero |

## Hallazgos

1. **Bomba en vacío no detectada** (confirma la revisión de lógica). Al quedar sin agua, P2 cae a ~7.1 psi (columna de la subida), por encima de `P_CAIDA_SECO`, y en la micro-pausa `P2 <= P_ARRANQUE` ya se cumple, así que se confirma flujo falso y se reanuda. **Corregido**: en `VERIFICANDO_FLUJO` el cruce de `P_ARRANQUE` solo cuenta si P2 estaba por encima al iniciar la pausa (`cruzoArranque`); mismo criterio en el comando `F`. Verificado con el simulador (antes/después) y compilado con PlatformIO.
2. **El sondeo de 4 s depende del volumen del pulmón.** Debe llevar P2 de la base a `max(base+1.5, 9.5)` en ~3 s. Con un pulmón grande, o con la base en la estática del tanque casi vacío, no lo logra aunque haya agua: con 12 L o más el escenario `normal` nunca llena el tanque (esperas de 5/10/20 min). Hay que conocer el volumen real del pulmón.
3. **Falsa alarma de "calle seca" con consumo.** Si la casa consume al arrancar, el caudal de la bomba se va a la casa y el sondeo falla; entra a espera 5 min (la casa sigue con agua por el Check #2 a baja presión). Con demanda alta sostenida el tanque puede vaciarse mientras la bomba espera.
4. **Sobrepresión al cortar.** El corte a 20 psi exige 2 s sostenidos; con 6 L y una bomba de cierre alto el modelo llega a ~27 psi. Depende de la altura de cierre real (comando `S`) y de la tubería.
5. **Presupuesto de arranques.** Cada reanudación tras micro-pausa y cada sondeo fallido cuentan; en `ciclado_corto` se llega a 18/h.

## Límites

Resultados cualitativos. Con parámetros aproximados un FALLA indica un caso a **verificar en campo**, no un error confirmado; los PASA no garantizan nada fuera del modelo (sin golpe de ariete, sin arranque de motor, sin aire atrapado, sin variación de la calle). Re-ejecutar tras cada cambio de lógica o con los valores medidos (`--v-pulmon`, `--p-street`).

## Bomba debilitada (dato del usuario)

La bomba antes apenas cortaba el presostato a 30 psi y ahora solo llega a 25 psi (desgaste: impulsor, condensador, fugas de aire en la succión o motor). Con la calle a 6 psi equivale a `--h0 25` (antes) y `--h0 19` (ahora). Con `--h0 19` el modelo se estanca en ~9.4 psi mientras llena el tanque, por debajo de `P_ARRANQUE` (9.5): el sondeo siempre falla, el sistema cae en espera de 20 min y el tanque no se llena (independiente del volumen del pulmón). Solución en el firmware: recalibrar con el comando `L` (`P_ARRANQUE` ≥ 1 psi por debajo de `P_LLENADO_REAL`, siempre por encima de la estática máxima ~7.1 psi). Con la bomba debilitada esa ventana es de solo ~2 psi; conviene revisar la bomba.

## Detección por subida sostenida de presión (criterio del usuario)

Implementada en el firmware: en `SONDEO_CALLE` el cebado se confirma solo si P2 sube de forma sostenida (pendiente ≥ `PENDIENTE_MIN_CEBADO` = 0.3 psi/s en alguna ventana de 1 s) y supera `max(base + 1.5, P_ESTATICA_MAX + 1.0)` durante 1 s; ya no se exige llegar a `P_ARRANQUE`. En `BOMBEO_ACTIVO`, si P2 ≤ `P_ESTATICA_MAX + 0.5` durante 10 s con B1 encendida, se declara sin flujo. `P_ESTATICA_MAX` (7.1 psi) es un supuesto: medirlo con `E` con el tanque lleno.

Efecto en el simulador: bomba en vacío detectada en ~11 s; bomba debilitada ya llena el tanque; el pulmón retenido (P2 plana por encima de la estática) no se toma por flujo. **Sigue sin resolverse** la falsa alarma cuando la casa consume durante el sondeo (`consumo_normal`, `consumo_alto`, `ciclado_corto`): bajar el margen sobre la estática a 0.5 o 0.7 psi no lo arregla, así que el límite está en cuánto caudal se lleva la casa; hace falta el volumen real del pulmón y la curva de la bomba.
