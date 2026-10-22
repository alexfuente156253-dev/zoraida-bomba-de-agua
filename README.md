# Zoraida – Control de Bomba de Agua (B1) v10.3

Sistema de control y protección hidráulica (triple check + pulmón) para una bomba monofásica.

- **MCU:** Arduino Nano (ATmega328P)
- **Conmutación:** BTA24-600BW + MOC3052 (activo en LOW, pin D7)
- **Sensor:** P2, 0–200 PSI ratiométrico (10%–90% de AVCC) en A1
- **Firmware:** [control_bomba_b1_v10_3/control_bomba_b1_v10_3.ino](control_bomba_b1_v10_3/control_bomba_b1_v10_3.ino)

## Recursos (medidos con PlatformIO, nanoatmega328, 2026-10-21)

| Modo | Flash | SRAM |
|---|---|---|
| Producción | 6.118 B (19,9 %) | 81 B (4,0 %) |
| `MODO_CALIBRACION` (Serial 9600, comandos E L F S C X R) | 13.676 B (44,5 %) | 290 B (14,2 %) |
| `MODO_DEPURACION` (eventos, telemetría, comandos D B) | 8.902 B (29,0 %) | 276 B (13,5 %) |

(Las cifras de la versión anterior, ~3.146 B y ~5.128 B de flash, no se reprodujeron al compilar.)
Compilar: `python -m platformio run` (entornos `nano_produccion` y `nano_calibracion`, ver [platformio.ini](platformio.ini)).

## Cambios en v10.3

1. El filtro EMA se siembra con la primera lectura válida (`filtroInicializado`) en lugar de una constante de 15 PSI. Con el sensor desconectado desde el arranque, `presionFiltrada` queda en 0.0 y el supervisor global pasa a `FALLA_SENSOR` a los 1,5 s.
2. Se documenta el ciclo de vida de `intentosFallidos`: conserva su nivel (≥ 3 → 20 min) mientras persista la sequía y vuelve a 0 solo cuando se confirma agua (cebado en `SONDEO_CALLE`, llegar a `P_MAX_OFF` = 20 PSI, o presión de red por encima de `P_ARRANQUE`).

## Diagrama hidráulico

```
                                   TANQUE ELEVADO (~5 m = 7.1 PSI)
                                  +----------------------------------------+
      Tubería de Subida           |    [Flotador Mecánico]                 |
   +------------------------------+---> (O)  <-- cierra al llenarse        |
   |                              |      Nivel de Agua                     |
   |                              +-------------------+--------------------+
   |                                                  | Toma inferior (gravedad)
   |                                                  v
   |                                             +---------+
   |                                             |CHECK #2 |  B1 apagada: abre por gravedad (~7.1 PSI)
   |                                             |  [ v ]  |  B1 encendida (>8 PSI): cierra
   |                                             +----+----+
   ^                                                  v
   |                   PULMÓN                      Nodo "T"2
   |               HIDRONEUMÁTICO                     |
   |                 +--------+                       |
   |                 |  Aire  |   TRANSDUCTOR         |
   |                 | ~7.5psi|   PRESIÓN (P2)        |
   ^                 |  Agua  |   (0-200 PSI)         |
   |                 +---+----+       +--+            |
   |                     |            |P2|            |
   +---[Llave Paso]------+------------+--+------------+-----> HACIA LOS GRIFOS DE LA CASA
   |
Nodo "T"1
   ^
   |          +---------+      +------------+      +---------+
RED CALLE ===>|CHECK #0 |=====>|  BOMBA B1  |=====>|CHECK #1 |=====> (Nodo "T"1)
(Suministro)  |  [ > ]  |      | (Monofás.) |      |  [ > ]  |
              +---------+      +------------+      +---------+
               (Succión)        Triac BTA24-600BW    (Descarga)
```

- **Check #0:** mantiene la carcasa de B1 llena y cebada cuando falta agua en la calle.
- **Check #1:** retiene los 20 PSI del pulmón con la bomba apagada.
- **Check #2:** abastece la casa por gravedad desde el tanque durante `ESPERA_SIN_FLUJO` y `PAUSA_ENFRIAMIENTO_MOTOR`.

## Diagrama eléctrico

```
 LADO DE CONTROL (5V DC AISLADO)                         |  LADO POTENCIA AC
---------------------------------------------------------+---------------------------------
 +5V ---+-------------------+----------------------+
        |                   |                      +--> Sensor P2 cable rojo (+5V)
    [C_desac]            [R_LED 220-330 Ω]
   100uF+100nF              |                      +--> Sensor P2 cable amarillo (señal)
        |                   v Pin 1 (ánodo)        |
       GND              +-------+                  |
                        |MOC3052| D7 (LOW = ON)    |
   ARDUINO NANO         +-------+                  |
   A1 <---[R_filtro 1kΩ]---------------------------+
   D13 -> LED               [C_f 100nF]   [R_pulldown 47k-100k] (detecta cable suelto)
   GND ---------------------+------------------------> Sensor P2 cable negro (GND)
   (NO UNIR GND DC CON NEUTRO AC)

 ---------- Aislamiento galvánico (mín. 7 mm) ----------

 L1 --[Disyuntor]--[Relé térmico]--+------------------+
                                   |               [R_Gate 180-360 Ω 1W]
                                   |                  |
                              [MOV 275V]          MOC3052 pin 6 / pin 4
                                   |                  |
                              BTA24-600BW: MT2, G, MT1 (+ R_GK 1 kΩ 1/2W)
                              disipador <= 3 °C/W, tab aislado
                                   |
                              BOMBA B1 (monofásica) ---- N
```

## Mediciones de campo

Ver la tabla de 5 mediciones en la cabecera del `.ino` (P_ESTATICA_REAL, P_LLENADO_REAL, CAIDA_MIN_FLUJO_ACTIVO, P_SHUTOFF_REAL, T_CARGA_PULMON).

## Uso para operadores

- **Esperar a que la bomba apague antes de abrir válvulas o grifos** cuando se acaba de energizar. Durante los primeros segundos el control verifica que haya flujo midiendo la subida de presión; si la casa consume en ese momento, puede interpretar falta de agua y esperar 5 minutos antes de reintentar. Mientras tanto la casa recibe agua del tanque por gravedad.

## Depuración

- **Registro de eventos en la EEPROM (siempre activo):** cada cambio de estado se guarda con su motivo, la presión y los intentos fallidos. Se conservan los últimos 46 eventos y sobreviven a cortes de luz.
- **Modo de depuración por USB:** compilar y subir el entorno `nano_depuracion` (`python -m platformio run -e nano_depuracion -t upload --upload-port COMx`). Envía cada evento y una telemetría por segundo, y acepta los comandos `D` (descargar registro) y `B` (borrar registro). Subir el firmware no borra la EEPROM, así que el registro hecho en producción se puede leer después.
- **Script de PC:** [tools/depurar.py](tools/depurar.py) guarda todo en `data/depuracion/` (eventos, telemetría, registro descargado y la salida serie cruda). Esa carpeta no se sube a git.
