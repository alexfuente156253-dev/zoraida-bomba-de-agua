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

## Comando implementado: `E` (P_ESTATICA_REAL)

Enviar `E` por serie. No enciende la bomba. Requiere estado REPOSO con bomba apagada. Promedia 10 s y reporta promedio, mín, máx, dispersión, y sugiere `P_CAIDA_SECO` (= prom − 1.2) y precarga (= prom + 0.4). Avisos: dispersión > 0.5 PSI (grifo abierto/consumo) y promedio < 3 PSI.
*Firmware modificado sin compilar ni probar en placa todavía.*

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

## Mediciones y automatización propuesta (pendiente salvo `E`)

| # | Medición | Comando propuesto |
|---|---|---|
| 1 | P_ESTATICA_REAL | `E` (hecho) |
| 2 | P_LLENADO_REAL | `L` |
| 3 | CAIDA_MIN_FLUJO_ACTIVO | `F` |
| 4 | P_SHUTOFF_REAL | `S` |
| 5 | T_CARGA_PULMON | `C` (cronometra 11→20 PSI) |

Extras: `X` apaga la bomba de inmediato, `?` lista comandos. Seguridad: solo con `MODO_CALIBRACION`, tope de tiempo (~120 s), apagado si P2 > 20 PSI o falla el sensor, respetar límites de arranques/hora. Un script en Python (pyserial) guardaría un CSV con fecha/hora. Preparar flotador, grifos y precarga del pulmón es manual. Pruebas con bomba solo con el usuario presente.

## Pendiente
- Medir P_ESTATICA_REAL (tanque a ~3 dedos del fondo) y confirmar la estimación de 5.0 PSI / altura del sensor.
- Aplicar `P_CAIDA_SECO` y precarga calibrados.
- Implementar `L`, `F`, `S`, `C` y el script/CSV.
