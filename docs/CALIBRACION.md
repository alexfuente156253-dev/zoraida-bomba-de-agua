# Calibración de presiones – hallazgos y contexto

Registro de las decisiones y razonamientos de calibración (v10.3). Los valores marcados *estimado* deben confirmarse midiendo en campo.

## Instalación (datos del usuario)

- Tanque elevado cilíndrico, ~1.5 m de alto (fondo = 0 %, borde superior = 100 %).
- Altura al sensor supuesta ~5 m → ~7.1 PSI con tanque lleno (a confirmar).
- 1 m de columna de agua ≈ 1.42 PSI.

## Flujo de trabajo en VS Code

1. Descomentar `#define MODO_CALIBRACION` en el `.ino`, subir con Arduino Community Edition o PlatformIO.
2. Monitor serie a 9600 bps: imprime cada segundo `P2_PSI | Estado | ArranquesHora`.
3. Estados: 0 REPOSO, 1 SONDEO, 2 BOMBEO, 3 VERIFICANDO, 4 ESPERA_SIN_FLUJO, 5 FALLA_SENSOR.
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

Enviar `E` por serie. No enciende la bomba. Requiere estado REPOSO con bomba apagada. Promedia 10 s y reporta promedio, mín, máx, dispersión, y sugiere la precarga (= prom + 0.4); con el tanque lleno el promedio es `P_ESTATICA_MAX`. Avisos: dispersión > 0.5 PSI (grifo abierto/consumo) y promedio < 3 PSI.
*Compilado; sin probar en placa todavía.*

## Hallazgos

> Nota: `P_CAIDA_SECO` ya no existe en el firmware (ver *Unificación* al final); lo que sigue sobre ese umbral es el razonamiento histórico.

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
- Medir `P_ESTATICA_MAX` (tanque lleno) y aplicar la precarga calibrada.
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

## Unificación: "bomba en vacío" = "sin flujo"

Decisión del usuario: los dos escenarios son el mismo estado y el segundo detector era código redundante. Se eliminaron `P_CAIDA_SECO`, `T_CAVITACION_RAPIDA` y `t_caidaSeco`; el único criterio es `P2 <= P_SIN_FLUJO` (= `P_ESTATICA_MAX` + 0.5 PSI) durante `T_SIN_FLUJO` (10 s) con B1 encendida → `ESPERA_SIN_FLUJO` (antes `ESPERA_SIN_AGUA`). Cambio de comportamiento: un colapso total de presión ahora se declara a los 10 s y no a los 3 s. El comando `E` ya no sugiere `P_CAIDA_SECO`.

## Modos de bombeo y aceleración de presión (implementado, **sin compilar ni probar**)

Según las notas del usuario (docs/NOTAS_DEL_USUARIO.md, secciones 2, 2b y 2c):

- **Arranque = verificación de flujo** (`SONDEO_CALLE`): subida sostenida de P2 con una **pendiente mínima única** `PENDIENTE_MIN_SUBIDA` (0.3 PSI/s), la misma que en hidroneumático.
- **Modo LLENANDO TANQUE**: P2 en la presión de llenado `P_LLENADO` (10.2 PSI, SUPUESTO: calibrar con `L`) con histéresis ±0.5 PSI.
- **Modo HIDRONEUMÁTICO**: P2 > `P_LLENADO + 0.5` (flotador cerrado). P2 debe seguir acelerando (≥ `PENDIENTE_MIN_SUBIDA` = 0.3 PSI/s en ventanas de 1 s). Pendiente medida en ventanas de 3 s (`T_VENTANA_PENDIENTE_HIDRO`). Si se estanca 10 s (`T_SIN_FLUJO`): **micro-pausa de prueba de modo**. Si P2 cae (≥ 0.8 PSI desde el pico, o cruza `P_ARRANQUE` hacia abajo) el agua sigue yendo al tanque: se aprende esa presión como `pLlenadoActual`, vuelve a LLENANDO y sigue bombeando. Si no cae (flotador cerrado): P2 ≥ `PISO_FLUJO_TOTAL` (= `P_ARRANQUE` + 1 = 10.5 PSI) → **presión de estancamiento** (`pEstancamiento`), REPOSO y re-arranca al bajar a `P_ARRANQUE`; por debajo → **sin flujo total** → `ESPERA_SIN_FLUJO`. `pLlenadoActual` vuelve al `P_LLENADO` calibrado en cada arranque. Vuelve a LLENANDO cuando P2 ≤ `pLlenadoActual`.
- Al llegar a 20 PSI se corta y deja de monitorear hasta el siguiente arranque. Se mantienen la micro-pausa de verificación y el criterio único de sin flujo por nivel (`P_UMBRAL_FLUJO`).
- El comando `L` ahora también sugiere `P_LLENADO`; el reporte serie de calibración muestra el modo.
- Simulador: registra los cambios de modo y agrega el escenario `bomba_pierde_fuerza` (bomba débil con agua de calle cada vez más escasa).

Ajustes posteriores (decisiones del usuario, sin compilar): pendiente única `PENDIENTE_MIN_SUBIDA` (sin tolerancia distinta en hidroneumático); el umbral de la verificación de arranque se limita al piso (se eliminó `P_TECHO_CEBADO_DINAMICO`); la micro-pausa usa el mismo piso que el estancamiento (se eliminó `P_CASI_LLENO_PAUSA`); el comando `L` sugiere `P_LLENADO` y `P_ARRANQUE`; el reporte serie muestra modo, `pLlenadoActual` y la presión de estancamiento. Nuevo escenario del simulador `llenado_alto` (calle a 12 psi, llenado ~11 psi).

## Correcciones de incongruencias (pedido 30, sin compilar)

1. **La espera sin flujo ya no reinicia `intentosFallidos`** solo porque P2 > `P_ARRANQUE` al terminar: puede ser presión retenida en el pulmón. Pasa a REPOSO conservando los intentos; si la siguiente verificación falla, la escalada 5/10/20 min continúa.
2. **Umbral único de flujo** `P_UMBRAL_FLUJO` = `P_ESTATICA_MAX` + 1.0 PSI (regla del usuario: estática 7.5 → flujo sobre 8.5). Lo usan la verificación al arrancar y el criterio de sin flujo en bombeo (10 s). Se eliminó `P_SIN_FLUJO` (estática + 0.5), que dejaba una zona muerta.
3. **`P_LLENADO` supuesto = 10.5 PSI** para cumplir la regla `P_ARRANQUE` ≤ `P_LLENADO` − 1.0 (banda 10.0–11.0; el piso 10.5 coincide con `P_LLENADO`). Se reemplaza al calibrar con `L`.
4. **"Tope por software" → "presión de estancamiento"** (`pEstancamiento`): corrección de concepto del usuario. Es la presión intermedia donde la bomba pierde fuerza y su aceleración llega a 0 (ej. 15 PSI); ahí apaga y re-arranca al bajar a `P_ARRANQUE`. No es un corte aprendido. El tope máximo sigue siendo `P_MAX_OFF` (20 PSI).
5. Textos "calle seca" → "sin flujo" en comentarios y LED.

## Mesetas: micro-pausa en cada estancamiento en un nivel nuevo (pedido 35, sin compilar)

Reemplaza los modos por banda fija (`P_LLENADO` ± `HISTERESIS_LLENADO`) y la prueba exclusiva de hidroneumático. Regla del usuario: **cada vez que la aceleración llega a 0** (10 s sin subir 0.3 PSI/s, ventanas de 3 s, `T_ESTANCAMIENTO`) **en un nivel nuevo** —primera vez en el ciclo o más de `MARGEN_NUEVO_NIVEL` (0.5 PSI) sobre la última meseta confirmada— se hace una micro-pausa de prueba:

- **P2 cae** (≥ 0.8 PSI o cruza `P_ARRANQUE` hacia abajo): sigue llenando; ese nivel pasa a ser la meseta (`pMeseta`).
- **No cae y P2 ≥ `PISO_FLUJO_TOTAL`**: terminó de llenar con bomba de poca potencia; apaga en esa presión de estancamiento y re-arranca en `P_ARRANQUE`.
- **No cae y P2 < piso**: sin flujo total → `ESPERA_SIN_FLUJO`.
- **Llega a 20 PSI**: corte normal, sin prueba.

Estancarse en la misma meseta es llenado normal y no se prueba (la micro-pausa periódica sigue). Cualquier micro-pausa que confirma flujo fija la meseta. `pMeseta` se reinicia en cada arranque. El modo (LLENANDO / HIDRONEUMÁTICO) queda como etiqueta informativa: hidroneumático = P2 por encima de la meseta. Se eliminaron `P_LLENADO`, `HISTERESIS_LLENADO`, `pLlenadoActual` y `pausaPorEstancamiento`; `T_ESTANCAMIENTO` separa el tiempo de estancamiento de `T_SIN_FLUJO`. El comando `L` solo sugiere `P_ARRANQUE` (llenado − 1.0 PSI). Costo: una micro-pausa (y un arranque contado) al inicio de cada llenado y en cada nivel nuevo.

## Estancamiento = la presión no sube; 18–20 PSI = lleno; bajar a la estática = sin flujo (pedido 38, sin compilar)

- En bombeo, **estancada** significa que P2 no supera en 0.25 PSI (`SUBIDA_MIN_ESTANCAMIENTO`, ~1 cuenta) el máximo ya alcanzado durante 10 s (`T_ESTANCAMIENTO`). Una subida lenta (flotador que cierra poco a poco) sigue siendo subida y llega a 20 PSI sin micro-pausas. La pendiente de 0.3 PSI/s queda solo para la verificación al arrancar; se eliminó la ventana de 3 s en bombeo.
- **Estancada entre 18 y 20 PSI** (`P_LLENO_ESTANCADO`): tanque lleno, corta sin micro-pausa.
- **Estancada bajo 18 PSI en un nivel nuevo** (ej. 17): micro-pausa de prueba (llenando / bomba de poca potencia / sin flujo).
- **La presión baja durante el llenado hasta la estática** (válvula abierta o calle agotada): sin flujo, en el momento en que ocurra (criterio único `P_UMBRAL_FLUJO` con 10 s de confirmación).
- Falsas alarmas al arrancar con consumo: decisión del usuario = **instruir a los operadores** (README, "Uso para operadores"); no se cambia el código.

## Micro-pausas periódicas solo durante el llenado (pedido 40, sin compilar)

La verificación periódica (90/180/300 s) solo corre en modo LLENANDO TANQUE: meseta confirmada y P2 dentro de ella. En la subida inicial y en hidroneumático el intervalo no avanza; esos tramos los cubren el estancamiento (prueba en nivel nuevo, 18-20 PSI = lleno) y el corte a 20 PSI. El chequeo del presupuesto de arranques que estaba dentro de esta verificación también queda limitado al llenado (ver punto C pendiente).

## Persistencia en EEPROM (pedido 43, sin compilar)

Se guardan `intentosFallidos`, `arranquesEnLaHora` y si estaba en `PAUSA_ENFRIAMIENTO_MOTOR`, para que un corte o parpadeo de la luz no reinicie la escalada de esperas ni salte el límite de 18 arranques/h. Al volver la luz: si estaba en enfriamiento, lo repite completo; si había intentos fallidos, entra en `ESPERA_SIN_FLUJO` con la espera de ese nivel; el presupuesto de arranques continúa (sin reloj no se sabe cuánto duró el corte, se asume lo más conservador). Registro de 8 bytes con secuencia y suma de control, rotando en 64 posiciones (512 bytes); solo se escribe cuando algo cambia. Comando de calibración `R`: pone intentos y arranques en 0. El simulador usa un sustituto de EEPROM en RAM (`sim/arduino_stub/EEPROM.h`).

## Eliminado: enfriamiento, límite de 45 min y presupuesto de arranques (pedido 45, sin compilar)

Por decisión del usuario se eliminó `PAUSA_ENFRIAMIENTO_MOTOR` (descanso de 20 min) junto con lo que lo disparaba: el límite de 45 min de marcha continua (`T_MAX_BOMBEO`) y el presupuesto de 18 arranques/h (`arranquesEnLaHora`, `MAX_ARRANQUES_POR_HORA`, ventana horaria, crédito de 6). Se mantienen el anti-ciclado de 3 min entre arranques desde reposo y el retardo de 15 s al encender; la protección térmica queda a cargo del **relé térmico** (hardware). Estados: 0 REPOSO, 1 SONDEO, 2 BOMBEO, 3 VERIFICANDO, 4 ESPERA_SIN_FLUJO, 5 FALLA_SENSOR. La EEPROM guarda solo `intentosFallidos`. Se eliminó el escenario `limite_45min` del simulador y los conteos de arranques pasan a ser informativos. Quedan sin objeto los pendientes C (presupuesto al reanudar), ventana horaria fija y enfriamiento sin demanda.

## Golpe de ariete, sin flujo inmediato, falla de sensor y tiempo máximo (pedido 50, sin compilar)

1. **Micro-pausa:** la caída se mide desde la presión **en el instante de apagar** (`pInicioPausa`), ya no desde un pico, y se ignoran los primeros 0.5 s (`T_IGNORAR_INICIO_PAUSA`). Un repunte por golpe de ariete que luego se disipa ya no se confunde con flujo. El comando `F` informa la caída con el mismo criterio y muestra el pico solo como dato.
2. **Sin flujo apenas toca la estática:** con flujo ya establecido, P2 ≤ `P_UMBRAL_FLUJO` durante 1 s (`T_SIN_FLUJO`) → sin flujo. Tras arrancar o reanudar (la micro-pausa deja P2 cerca de la estática) hay hasta 10 s (`T_SIN_FLUJO_REARRANQUE`) para que P2 supere el umbral; si no lo supera, también es sin flujo.
3. **Falla de sensor:** al recuperarse, si había intentos fallidos pendientes vuelve a `ESPERA_SIN_FLUJO` (no se salta la escalada 5/10/20 min).
4. **Tiempo máximo de bombeo: 45 min** (`T_MAX_BOMBEO`) desde que se confirma flujo sin llegar a lleno (red de seguridad ante fugas o grifos abiertos). Apaga y espera 20 min (intentos ≥ 3) antes de volver a verificar flujo. Escenario del simulador: `limite_45min`.

## Compilación y simulación (pedido 55)

Todo lo anterior marcado "sin compilar" quedó **compilado** (PlatformIO, ambos modos: producción 5.840 B flash / 92 B SRAM; calibración 13.274 B / 302 B) y **simulado** (10 de 14 escenarios pasan; las 4 fallas son la falsa alarma por consumo durante la verificación al arrancar, resuelta con instrucción a operadores). Error encontrado al compilar y corregido: la estructura `RegistroPersistente` debía declararse antes de cualquier función por los prototipos que genera el preprocesador de Arduino. Pendiente: probar en la placa real.

## Corte instantáneo a 20 PSI (pedido 58; compilado y simulado en el pedido 59)

El corte ya no exige 2 s sostenidos en `P_MAX_OFF` (`T_CORTE_20PSI`): apaga en cuanto P2 llega a 20 PSI, en la verificación al arrancar y en bombeo. Con la espera de 2 s la bomba seguía empujando contra el flotador cerrado y en el simulador la presión llegaba a ~27 PSI (sobrepico). Se eliminaron `T_CORTE_20PSI` y `t_corteAlta`. El ruido lo filtran el promedio de 64 lecturas y el filtro EMA del sensor.

Resultado (pedido 59): presión máxima en el simulador **20.1–20.2 PSI** (antes 27.2 PSI). Firmware: producción 5.656 B flash / 88 B SRAM; calibración 13.076 B / 298 B. Escenarios: siguen 10 de 14 (las 4 fallas son el consumo durante la verificación al arrancar).

## Depuración: registro de eventos y modo `MODO_DEPURACION` (pedido 62; compilado en el pedido 66)

- **Registro en EEPROM (todos los modos):** bytes 512–1023 (la mitad baja es la persistencia de intentos). Evento de 11 bytes: segundos desde el encendido, secuencia, P2 (décimas de PSI), estado nuevo, motivo e intentos; 46 posiciones rotativas. Se escribe en cada cambio de estado y en cada encendido. Desgaste estimado: ~10 años a 60 eventos/h.
- **Motivos** (`MotivoEvento`, valores fijos): 1 encendido, 2 demanda, 3 flujo confirmado, 4 verificación sin subida, 5 corte a 20 PSI, 6 lleno por estancamiento 18–20, 7 prueba en nivel nuevo, 8 micro-pausa periódica, 9 la presión cayó (sigue llenando), 10 estancamiento sobre el piso, 11 sin flujo bajo el piso, 12 sin flujo en la estática, 13 tiempo máximo, 14 fin de espera a reposo, 15 reintento, 16 falla de sensor, 17 sensor recuperado, 18 fin de calibración.
- **`MODO_DEPURACION`** (entorno `nano_depuracion`): línea `EV,secuencia,t_s,estado,motivo,p2,intentos` en cada evento; `TL,t_s,estado,p2,bomba,modo,meseta,intentos` (modo 0 subiendo, 1 llenando, 2 hidroneumático) cada segundo; comandos `D` (`LOG_INICIO` … `LOG,…` … `LOG_FIN`) y `B` (`LOG_BORRADO`).
- **`tools/depurar.py`:** `monitor`, `descargar` y `borrar`; archivos en `data/depuracion/` (ignorada por git).

## Modo de bombeo con tres valores (pedido 65; compilado en el pedido 66)

`modoBombeo` reemplaza a `modoHidroneumatico`: **SUBIENDO** (bomba encendida sin meseta confirmada), **LLENANDO** (meseta confirmada y P2 en ella) e **HIDRONEUMÁTICO** (P2 sobre la meseta, o el ciclo terminó arriba: corte a 20 PSI, lleno por estancamiento o presión de estancamiento, aunque nunca se haya confirmado una meseta). Antes, con el tanque lleno y sin meseta confirmada la etiqueta decía "llenando". Es informativo (reporte serie, telemetría y simulador); la micro-pausa periódica sigue corriendo solo en LLENANDO.

## Verificación al arrancar por aceleración de presión, sin plazo fijo (pedido 70; compilado en el pedido 76)

Por indicación del usuario, la verificación al arrancar (`SONDEO_CALLE`) ya no tiene plazo fijo de 4 s (ni el techo de 6.5 s) ni pendiente mínima de 0.3 PSI/s: usa **la misma función de aceleración de presión que el bombeo**. Mientras P2 supere su máximo en 0.25 PSI (`SUBIDA_MIN_ESTANCAMIENTO`), sigue; si pasa 10 s (`T_ESTANCAMIENTO`) sin subir por debajo del umbral, es sin flujo. El flujo se confirma cuando P2 supera el umbral (presión de partida + 1.5, mínimo estática + 1, máximo el piso) durante 1 s: llegar a él desde abajo ya demuestra la subida. Se eliminaron `PENDIENTE_MIN_SUBIDA`, `T_VENTANA_PENDIENTE`, `T_PRUEBA_CEBADO`, `T_MAX_SONDEO_ABS` y su ventana de pendiente. Efecto: con la bomba débil o con consumo ya no se corta a los 4 s mientras la presión sube; una bomba sin agua se declara sin flujo a los ~10 s en lugar de 4.

## Sin flujo tras arrancar o reanudar, por aceleración (pedido 73; compilado en el pedido 76)

Se eliminó el plazo fijo de 10 s (`T_SIN_FLUJO_REARRANQUE`) para que P2 supere el umbral de flujo después de arrancar o reanudar una micro-pausa. Ahora usa la función de aceleración: mientras P2 siga subiendo hacia el umbral, el bombeo continúa; si se estanca (10 s sin superar su máximo en 0.25 PSI) por debajo del umbral, es sin flujo (motivo 4). Con el flujo ya establecido sigue la regla del usuario: tocar la estática + 1 durante 1 s es sin flujo (motivo 12). Con esto, toda decisión sobre si hay flujo depende de la aceleración; los tiempos que quedan son protecciones del motor (3 min entre arranques, 15 s al encender, 1.5 s mínimo de pausa), la espera sin flujo (el sensor no ve la calle con la bomba apagada), el tiempo máximo de 45 min (fugas), la ventana de la propia aceleración (10 s) y filtros de ruido cortos.

## Arranque inmediato al tocar P_ARRANQUE (pedido 75; compilado en el pedido 76)

Se eliminó la confirmación de demanda de 1.2 s (T_DEMANDA): la bomba enciende en cuanto P2 llega a P_ARRANQUE (si ya pasaron los 3 min entre arranques). Con la espera, el consumo seguía bajando la presión ~1 PSI (arrancaba en ~8.4 PSI). El ruido lo filtran el promedio de 64 lecturas y el filtro EMA.
