# Calibración de presiones – hallazgos y contexto

Registro de las decisiones y razonamientos de calibración (v10.3). Los valores marcados *estimado* deben confirmarse midiendo en campo.

## Instalación (datos del usuario)

- Tanque elevado cilíndrico, ~1.5 m de alto (fondo = 0 %, borde superior = 100 %).
- Altura al sensor supuesta ~5 m → ~7.1 PSI con tanque lleno (a confirmar).
- 1 m de columna de agua ≈ 1.42 PSI.

## Flujo de trabajo en VS Code

1. Descomentar `#define MODO_CALIBRACION` en el `.ino`, subir con Arduino Community Edition o PlatformIO.
2. Monitor serie a 9600 bps: imprime cada segundo `P2_PSI | Estado | ArranquesHora`.
3. Estados: 0 REPOSO, 1 SONDEO, 2 BOMBEO, 3 VERIFICANDO, 4 ESPERA_SIN_AGUA, 5 ENFRIAMIENTO, 6 FALLA_SENSOR.
4. Al terminar, volver a comentar `MODO_CALIBRACION` y subir la versión de producción.
5. Aislamiento: no unir GND DC con neutro AC; con PC por USB usar aislamiento o calibrar sin la bomba en red.

## Comandos implementados (serie 9600, solo con `MODO_CALIBRACION`)

| Cmd | Medición | Enciende B1 | Preparación manual |
|---|---|---|---|
| `E` | P_ESTATICA_REAL (promedio 10 s) | No | Tanque a ~3 dedos del fondo; grifo abierto ~15 s y cerrado |
| `L` | P_LLENADO_REAL (15 s ajuste + 10 s promedio); sugiere `P_ARRANQUE` (prom − 1.0) y `P_TECHO_CEBADO_DINAMICO` (prom − 0.3) | Sí | Flotador abierto, grifos cerrados |
| `F` | CAIDA_MIN_FLUJO_ACTIVO: caída desde el pico a 1.5 s y 3.0 s tras apagar | Sí | Flotador abierto, grifos cerrados |
| `S` | P_SHUTOFF_REAL: máximo alcanzado (corta a 21 PSI, estancamiento 8 s o 60 s) | Sí | Flotador cerrado, sin consumo |
| `C` | T_CARGA_PULMON: 11→20 PSI; sugiere `T_VERIF_INICIAL` = 3 × t (tope 120 s) | Sí | Flotador cerrado, sin consumo, P2 ≤ 11 PSI |
| `X` | Apaga B1 y vuelve a REPOSO | — | — |

Seguridad de los comandos con bomba: exigen REPOSO_PRESURIZADO, sensor válido, presupuesto horario y 180 s desde el último arranque (cuentan como arranques); abortan por sensor inválido, límite de presión (19.5 PSI en L/F, 22 PSI en S/C), `X` o tope de tiempo; el watchdog sigue activo. Cada comando termina con una línea `RES,<cmd>,k=v,...` que lee el script.

## Script de PC: `tools/calibrar.py`

```
pip install -r tools/requirements.txt
python tools/calibrar.py --listar
python tools/calibrar.py --puerto COM3 E --nota "tanque a 3 dedos del fondo"
python tools/calibrar.py --puerto COM3 secuencia      # E, L, F, S, C con confirmaciones
```
Guarda cada medición en `data/calibraciones.csv` (fecha, comando, medición, nota, datos). Pide escribir `SI` antes de encender la bomba. Ctrl+C envía `X`. Anota en `--nota` el nivel del tanque y la presión de la calle.
*El firmware compila sin errores en ambos modos con PlatformIO (`python -m platformio run`). Aún no se ha probado en la placa ni el script contra hardware. downloads.arduino.cc devuelve 403 desde esta red, por eso `arduino-cli` no pudo instalar el núcleo AVR.*

## Detalle del comando `E` (P_ESTATICA_REAL)

Enviar `E` por serie. No enciende la bomba. Requiere estado REPOSO con bomba apagada. Promedia 10 s y reporta promedio, mín, máx, dispersión, y sugiere `P_CAIDA_SECO` (= prom − 1.2) y precarga (= prom + 0.4). Avisos: dispersión > 0.5 PSI (grifo abierto/consumo) y promedio < 3 PSI.
*Compilado; sin probar en placa todavía.*

## Hallazgos

### La estática depende del nivel del tanque
- Lleno ≈ 7.1 PSI; a ~3 dedos del fondo (~5–6 cm, ~0.08 PSI) ≈ **5.0 PSI** *(estimado)*; diferencia ≈ 2.1 PSI.

### Usar la estática MÍNIMA
- `P_CAIDA_SECO` y la precarga del pulmón se calculan con la estática mínima. Si la real es mayor, solo hay más margen; usar la máxima daría falsas detecciones de descebado y el Check #2 no abriría con tanque bajo.
- Se mide **solo con el tanque casi vacío (a ~3 dedos del fondo)**, no totalmente vacío (daría ≈ 0 PSI).
- Un flotador mecánico abre cerca del nivel lleno, por lo que ese no es el mínimo; el mínimo es el nivel al que baja el tanque en un corte largo con B1 apagada.

### Valores esperados con estática mínima ≈ 5.0 PSI *(estimado)*

| Parámetro | Valor actual | Valor estimado |
|---|---|---|
| `P_CAIDA_SECO` (mín − 1.2) | 6.0 PSI | ≈ 3.8 PSI |
| Precarga del pulmón (mín + 0.4) | ~7.5 PSI | ≈ 5.4 PSI |
| `P_ARRANQUE` | 9.5 PSI | sin cambio |

- `P_CAIDA_SECO` solo se evalúa con la bomba en marcha, por lo que 6.0 no causa falsos disparos por nivel, pero el valor correcto para este tanque es ≈ 3.8.
- Con tanque casi vacío y B1 apagada la casa recibe solo ~5 PSI por gravedad.

### T_CARGA_PULMON
- Depende de la presión de la calle (B1 suma su altura de presión a la de entrada), del caudal y del volumen/precarga del pulmón.
- Medir en el peor caso (presión de calle más baja del día), quedarse con el tiempo más largo y fijar `T_VERIF_INICIAL` ≥ 3 × ese tiempo (hoy 90 s; subir si el peor caso supera 30 s).
- `P_SHUTOFF_REAL` también depende de la calle; medir con calle baja.

## Mediciones y automatización

| # | Medición | Comando propuesto |
|---|---|---|
| 1 | P_ESTATICA_REAL | `E` |
| 2 | P_LLENADO_REAL | `L` |
| 3 | CAIDA_MIN_FLUJO_ACTIVO | `F` |
| 4 | P_SHUTOFF_REAL | `S` |
| 5 | T_CARGA_PULMON | `C` (cronometra 11→20 PSI) |

(Implementados; ver sección anterior.)

Extras: `X` apaga la bomba de inmediato, `?` lista comandos. Seguridad: solo con `MODO_CALIBRACION`, tope de tiempo (~120 s), apagado si P2 > 20 PSI o falla el sensor, respetar límites de arranques/hora. Un script en Python (pyserial) guardaría un CSV con fecha/hora. Preparar flotador, grifos y precarga del pulmón es manual. Pruebas con bomba solo con el usuario presente.

## Pendiente
- Medir P_ESTATICA_REAL (tanque a ~3 dedos del fondo) y confirmar la estimación de 5.0 PSI / altura del sensor.
- Aplicar `P_CAIDA_SECO` y precarga calibrados.
- Probar en la placa los comandos `E L F S C` y el script (compilan, pero no se han ejecutado en hardware).
- Medir con la calle en su presión más baja (L, S, C) y registrar la presión de calle en `--nota`.

## Registro: cómo se compiló (PlatformIO)

Fecha: 2026-10-21.

1. **Intento con `arduino-cli`.** Se instaló con `winget install --id ArduinoSA.CLI -e` (v1.5.1) y se intentó `arduino-cli core update-index` y `arduino-cli core install arduino:avr`. Falló con **403 Forbidden** al descargar `https://downloads.arduino.cc/packages/package_index.tar.bz2`.
2. **Descartar el túnel.** El entorno tenía `HTTP_PROXY`, `HTTPS_PROXY` y `ALL_PROXY` (127.0.0.1:25344/25345). Se quitaron solo para el comando y también se probó con `curl --noproxy "*"` y con User-Agent de navegador. `downloads.arduino.cc` siguió devolviendo 403 (bloqueo del servidor hacia esta conexión), mientras que github.com, PyPI y el registro de PlatformIO respondían 200.
3. **Cambio a PlatformIO.** Se instaló con `python -m pip install --user platformio` (v6.2.0) y se creó [../platformio.ini](../platformio.ini) con `src_dir = control_bomba_b1_v10_3`, `platform = atmelavr`, `board = nanoatmega328`, `framework = arduino`. El entorno `nano_calibracion` añade `build_flags = -DMODO_CALIBRACION` en lugar de editar el `.ino`.
4. **Compilación.** `python -m platformio run` descargó la toolchain AVR y el framework y compiló ambos entornos sin errores:
   - `nano_produccion`: Flash 4.622 B (15,0 %), SRAM 71 B (3,5 %).
   - `nano_calibracion`: Flash 12.240 B (39,8 %), SRAM 276 B (13,5 %).
   Un aviso de "Package Mirror" (no resolvió `usc1.contabostorage.com`) no impidió la compilación.
5. **Subir a la placa.** `python -m platformio run -e nano_calibracion -t upload --upload-port COMx` (si es clon con bootloader antiguo, usar `board = nanoatmega328` con `upload_speed = 57600`). Monitor: `python -m platformio device monitor -b 9600`.

Limitación: solo se verificó que compila. Falta probar en la placa.

## Simulador en lazo cerrado (sim/)

Se agregó [../sim/](../sim/README.md): el firmware real compilado en el PC contra un modelo hidráulico simple (supuestos en el README del simulador). Se corrigió la detección de bomba en vacío (el cruce de `P_ARRANQUE` en la micro-pausa solo cuenta si P2 partía por encima; antes la bomba podía trabajar en vacío hasta 45 min, ahora se detecta en la siguiente micro-pausa). Resultado actual: 9 de 12 escenarios pasan. Fallan `consumo_normal`, `consumo_alto` y `ciclado_corto` (falsas alarmas de calle seca cuando la casa consume durante el sondeo), y `base_baja` con pulmón ≥ 8 L (el sondeo de 4 s no alcanza 9.5 psi). Datos de entrada que faltan para afinar el modelo: **volumen del pulmón**, **presión real de la calle**, **altura de cierre y caudal de la bomba**. Detalle y limitaciones en [../sim/README.md](../sim/README.md).

## Detección por subida sostenida de presión (implementada)

Criterio del usuario: sin agua la bomba no puede acelerar la presión; una P2 plana, aunque esté por encima de la estática (pulmón retenido por el Check #1), no es flujo. En el firmware: `SONDEO_CALLE` exige pendiente ≥ 0.3 psi/s en una ventana de 1 s y P2 ≥ `max(base + 1.5, P_ESTATICA_MAX + 1.0)` sostenida 1 s; `BOMBEO_ACTIVO` declara sin flujo si P2 ≤ `P_ESTATICA_MAX + 0.5` durante 10 s. **Calibrar**: `P_ESTATICA_MAX` (hoy 7.1, supuesto) con `E` con el tanque lleno; la pendiente mínima y δ con las mediciones reales. Resultados del simulador en [../sim/README.md](../sim/README.md). Compila: producción 5.208 B flash (17,0 %) / 84 B SRAM; calibración 12.666 B (41,2 %) / 289 B.
