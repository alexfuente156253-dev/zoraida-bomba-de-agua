/*
 * ======================================================================================
 * SISTEMA DE CONTROL Y PROTECCIÓN HIDRÁULICA (TRIPLE CHECK + PULMÓN) - VERSIÓN v10.3
 * VERSIÓN CONSOLIDADA PARA PUESTA EN MARCHA EN CAMPO.
 * MCU: Arduino Nano (ATmega328P) | Conmutación: BTA24-600BW + MOC3052 (Activo en LOW)
 * Sensor único: P2 (0-200 PSI, ratiométrico 10%-90% de AVCC) en ADC 10 bits (0.244 PSI/cuenta)
 * CERO HARDWARE ADICIONAL. PROCESAMIENTO RATIOMÉTRICO DIRECTO EN CUENTAS DEL ADC.
 * ======================================================================================
 *
 * ESQUEMA HIDRÁULICO DE LA INSTALACIÓN:
 *
 *                                     TANQUE ELEVADO (~5m = 7.1 PSI estáticos)
 *                                    +------------------------------+
 *                                    |  [O]<-- Flotador Mecánico    |
 *                                    |   |     (Cierra al llenarse) |
 *                   Línea de Llenado +---+                          |
 *                           ^        |                              |
 *                           |        +--------------+---------------+
 *                           |                       | Toma Inferior (Gravedad)
 *                           |                       v
 *                           |                  [CHECK #2] (Abre hacia la casa si P2 < 7.1 PSI.
 *                           |                       |      Impide retorno al subir a 20 PSI)
 *                           |                       |
 * Calle ---> [CHECK #0] ---> (B1) ---> [CHECK #1] ->+---> [PULMÓN + Sensor P2] ---> RED CASA
 *            (Succión)                 (Descarga)
 *
 * GARANTÍA DE SUMINISTRO CONTINUO A LA VIVIENDA:
 *   - Durante cualquier pausa del motor (`PAUSA_ENFRIAMIENTO_MOTOR` de 20 min por tiempo máximo
 *     o por alcanzar el límite de 18 arranques/hora, o durante `ESPERA_SIN_AGUA`), la vivienda
 *     NO se queda sin agua: al abrir un grifo y bajar la presión de la línea a ~7.1 PSI, la
 *     válvula `CHECK #2` se abre automáticamente por gravedad y abastece la casa desde el tanque.
 *
 * TABLA DE 5 MEDICIONES EN CAMPO (SIN AGREGAR HARDWARE):
 *   1. P_ESTATICA_REAL: Bomba apagada, grifo abierto 15s y cerrado. Leer presión sostenida por
 *      el tanque (~7.1 PSI). Ajustar `P_CAIDA_SECO` = P_ESTATICA_REAL - 1.2 PSI (Actual: 6.0 PSI)
 *      y precargar el aire del pulmón (sin agua) a P_ESTATICA_REAL + 0.4 PSI (~7.5 PSI).
 *   2. P_LLENADO_REAL: Flotador del tanque abierto y grifos de casa cerrados. Encender B1 y leer
 *      presión estable. Ajustar `P_ARRANQUE` >= 1.0 PSI por debajo de P_LLENADO_REAL y ajustar
 *      `P_TECHO_CEBADO_DINAMICO` = P_LLENADO_REAL - 0.3 PSI.
 *   3. CAIDA_MIN_FLUJO_ACTIVO: Al apagar B1 con el flotador del tanque abierto, observar cuántos
 *      PSI cae P2 en 3.0 segundos.
 *      --> Si la tubería tiene mucha fricción (P_LLENADO_REAL >= 10.5 PSI), al apagar B1 caerá
 *          >= 0.80 PSI (`CAIDA_MIN_FLUJO_ACTIVO`). Si tiene muy poca fricción (P_LLENADO_REAL
 *          entre 9.6 y 10.2 PSI), al apagar B1 por 1.5s cruzará `P2 <= P_ARRANQUE` (9.5 PSI)
 *          confirmando igualmente el flujo sin falsos cortes.
 *   4. P_SHUTOFF_REAL: Con flotador cerrado y casa sin consumo, verificar que B1 supere 20.0 PSI.
 *   5. T_CARGA_PULMON: Con flotador cerrado y sin consumo, medir cuántos segundos tarda B1 en
 *      subir de 11.0 a 20.0 PSI. Ajustar `T_VERIF_INICIAL` >= 3 * T_CARGA_PULMON (Actual: 90s).
 */

#include <Arduino.h>
#include <avr/wdt.h>
#include <avr/io.h>

// Descomentar SOLO durante las pruebas de calibración con PC conectada.
// Uso de recursos en ATmega328P:
//   - Desactivado (Producción): Flash = ~3146 bytes (10.2%), SRAM = ~82 bytes (4.0%)
//   - Activado (Calibración):   Flash = ~5128 bytes (16.7%), SRAM = ~266 bytes (13.0%)
// #define MODO_CALIBRACION

// ================= PROTECCIÓN BOOTLOADER ANTIGUO (WDT BUG) =================
// Se ejecuta en la sección .init3 antes de setup() para limpiar el flag WDRF
// y evitar un bucle infinito de reinicios en Arduino Nano con Old Bootloader.
void desactivarWdtAlArranque(void) __attribute__((naked, used, section(".init3")));
void desactivarWdtAlArranque(void) {
  MCUSR = 0;
  wdt_disable();
}

// ================= PINES DEL HARDWARE =================
const int PIN_TRIAC_MOC  = 7;  // Salida al MOC3052 (Lógica inversa: LOW=ON, HIGH=OFF)
const int PIN_SENSOR_P2  = A1; // Entrada del sensor de presión ratiométrico (10% a 90% de AVCC)
const int PIN_LED_ESTADO = 13; // LED integrado para diagnóstico visual

// ================= CALIBRACIÓN RATIOMÉTRICA DIRECTA EN CUENTAS DEL ADC =================
// El sensor entrega 10% de AVCC a 0 PSI y 90% de AVCC a 200 PSI (span útil = 80% de AVCC).
// Como el ADC de 10 bits (0..1023) usa AVCC como referencia (DEFAULT), AVCC se cancela:
//   - 0 PSI (10% de 1023)     = 102.3 cuentas
//   - 200 PSI (90% de 1023)   = 920.7 cuentas
//   - Span útil (80% de 1023) = 818.4 cuentas -> Resolución = 200.0 / 818.4 = 0.24438 PSI/cuenta.
const float RANGO_MAX_SENSOR_PSI  = 200.0;
const float ADC_CERO_PSI          = 102.3; // 10.0% de 1023 cuentas
const float ADC_SPAN_UTIL         = 818.4; // 80.0% de 1023 cuentas (de 10% a 90% de AVCC)
const float ADC_MIN_SENSOR_VALIDO = 45.0;  // < 4.4% de AVCC indica cable cortado (con pull-down)
const float ADC_MAX_SENSOR_VALIDO = 982.0; // > 96.0% de AVCC indica cortocircuito a +AVCC

// ================= UMBRALES DE PRESIÓN CON SEPARACIÓN >= 1.5 PSI =================
// NOTA DE CALIBRACIÓN (tanque cilíndrico de ~1.5 m): la estática varía ~2.1 PSI entre lleno (~7.1) y
// casi vacío a ~3 dedos del fondo (~5.0 PSI estimado). P_CAIDA_SECO y la precarga del pulmón se calculan
// con la estática MÍNIMA medida (P_ESTATICA_REAL = tanque casi vacío): P_CAIDA_SECO = mín - 1.2 (~3.8 PSI
// estimado; 6.0 es el valor inicial sin calibrar). Ver docs/CALIBRACION.md.
const float P_CAIDA_SECO            = 6.0;  // Descebado con tanque vacío / pérdida total (< estática)
const float P_ARRANQUE              = 9.5;  // Cut-in: arranca cuando el pulmón baja a <= 9.5 PSI (+3.5 PSI)
const float P_TECHO_CEBADO_DINAMICO = 11.0; // Máximo exigible en SONDEO_CALLE (+1.5 PSI sobre P_ARRANQUE)
const float P_CASI_LLENO_PAUSA      = 18.5; // Si en la micro-pausa queda estanca >= 18.5 PSI, el tanque ya cerró
const float P_MAX_OFF               = 20.0; // Cut-off: corta cuando el pulmón llega a 20.0 PSI

// Incremento relativo exigido en SONDEO_CALLE para demostrar que B1 abrió el Check #1
const float DELTA_P_CEBADO          = 1.5;  // Exige +1.5 PSI (> 6 cuentas del ADC) sobre la base de arranque

// Parámetros de la Micro-Pausa de Verificación Hidrodinámica
const float AVANCE_OMITE_PAUSA      = 0.75; // Si P2 subió >= 0.75 PSI (>3 cuentas) en el intervalo, omite apagar B1
const float CAIDA_MIN_FLUJO_ACTIVO  = 0.80; // Caída mínima desde el pico durante la pausa que confirma flujo abierto

// ================= TIEMPOS DE CONTROL Y PROTECCIÓN (ms) =================
const unsigned long T_PRUEBA_CEBADO      = 4000UL;   // 4.0 seg máx en SONDEO_CALLE (Check #0 mantiene B1 cebada)
const unsigned long T_MAX_SONDEO_ABS     = 6500UL;   // 6.5 seg techo absoluto en SONDEO_CALLE ante oscilaciones
const unsigned long T_CONFIRMA_CEBADO    = 1000UL;   // 1.0 seg sosteniendo el Delta P confirma flujo hacia pulmón
const unsigned long T_CAVITACION_RAPIDA  = 3000UL;   // 3.0 seg exactos bajo P_CAIDA_SECO (6.0 PSI)
const unsigned long T_CORTE_20PSI        = 2000UL;   // 2.0 seg sosteniendo >= 20 PSI (carga completa del pulmón)
const unsigned long T_DEMANDA            = 1200UL;   // 1.2 seg bajo 9.5 PSI para confirmar descarga del pulmón

// Intervalos Adaptativos de Micro-Pausa (cubre toda la franja de 9.5 a 19.9 PSI)
const unsigned long T_VERIF_INICIAL      = 90000UL;  // 1ra verificación a los 90 seg
const unsigned long T_VERIF_MEDIA        = 180000UL; // 2da verificación a los 180 seg (3 min)
const unsigned long T_VERIF_ESTABLE      = 300000UL; // 3ra verificación en adelante cada 300 seg (5 min)
const unsigned long T_MIN_PAUSA_EFECTIVA = 1500UL;   // 1.5 seg mínimos con motor apagado antes de permitir re-encendido
const unsigned long T_PAUSA_VERIFICACION = 3000UL;   // 3.0 seg ventana total de micro-pausa para evaluar caída

// Protección Anti-Ciclado y Presupuesto Térmico de Arranques por Hora
const unsigned long T_MIN_ENTRE_ARRANQUES = 180000UL;  // 180 seg (3 min) entre arranques desde reposo
const unsigned long T_RETARDO_ENCENDIDO   = 15000UL;   // 15 seg de espera obligatoria tras corte eléctrico
const unsigned long T_VENTANA_UNA_HORA    = 3600000UL; // Ventana de 60 minutos para conteo de arranques reales
const uint8_t MAX_ARRANQUES_POR_HORA      = 18;        // Límite térmico estricto (incluye reanudaciones de micro-pausa)
const uint8_t CREDITO_ENFRIAMIENTO_20MIN  = 6;         // Al enfriar 20 min (1/3 de hora), descuenta solo 6 arranques (1/3 de 18)

// Tiempo máximo de marcha continua (llenado de tanque) y pausa de enfriamiento
const unsigned long T_MAX_BOMBEO          = 2700000UL; // 45 minutos máx de marcha continua llenando tanque
const unsigned long T_ENFRIAMIENTO_MAX    = 1200000UL; // 20 minutos de descanso antes de retomar

// Tiempos de espera escalonada cuando NO hay agua en la calle (con Check #0)
const unsigned long T_ESPERA_1 = 300000UL;  // 5 minutos  (1er intento sin agua en calle)
const unsigned long T_ESPERA_2 = 600000UL;  // 10 minutos (2do intento sin agua en calle)
const unsigned long T_ESPERA_3 = 1200000UL; // 20 minutos (3er intento en adelante)

// ================= MÁQUINA DE ESTADOS =================
enum EstadoSistema {
  REPOSO_PRESURIZADO,       // Pulmón cargado entre 9.5 y 20 PSI, bomba apagada
  SONDEO_CALLE,             // Prueba rápida de 4s evaluando Delta P (+1.5 PSI)
  BOMBEO_ACTIVO,            // Llenando tanque o cargando pulmón hasta 20 PSI
  VERIFICANDO_FLUJO,        // Micro-pausa activa (1.5s a 3.0s) cuando P2 está en meseta sin subir
  ESPERA_SIN_AGUA,          // Calle seca: espera escalonada 5/10/20 min (casa con agua por Check #2)
  PAUSA_ENFRIAMIENTO_MOTOR, // Descanso de 20 min tras 45 min o 18 arranques/h (casa con agua por Check #2)
  FALLA_SENSOR              // Sensor desconectado (<45 cuentas) o en corto (>982 cuentas)
};

EstadoSistema estado = REPOSO_PRESURIZADO;

// Temporizadores dedicados e independientes
unsigned long t_inicioEstado        = 0;
unsigned long t_condicion           = 0;
unsigned long t_corteAlta           = 0;
unsigned long t_caidaSeco           = 0;
unsigned long t_ultimaVerificacion  = 0;
unsigned long t_inicioPausaVerif    = 0;
unsigned long t_ultimoArranque      = 0;
unsigned long t_inicioVentanaHora   = 0;
unsigned long t_fallaSensor         = 0;

#ifdef MODO_CALIBRACION
unsigned long t_reporteSerial       = 0;
#endif

bool bombaEncendida             = false;
bool flujoConfirmadoEnPausa     = false; // Registra caída válida esperando cumplir T_MIN_PAUSA_EFECTIVA (1.5s)
bool filtroInicializado         = false; // Siembra presionFiltrada con la 1ra lectura real sin sesgo inicial

// Contador de intentos fallidos por calle seca:
// Escala la espera (1->5m, 2->10m, >=3->20m). Por diseño se mantiene en >=3 durante cortes
// prolongados de suministro y solo vuelve a 0 cuando se confirma agua (en SONDEO_CALLE, al
// llegar a 20 PSI, o si la presión de red sube sola por encima de P_ARRANQUE).
uint8_t intentosFallidos        = 0;
uint8_t verificacionesExitosas  = 0;     // Escala el intervalo adaptativo: 0->90s, 1->180s, >=2->300s
uint8_t arranquesEnLaHora       = 0;     // Cuenta TODOS los arranques físicos del motor

float presionFiltrada      = 0.0;  // Se inicializa directamente con la primera lectura válida en setup()
float presionBaseArranque  = 9.0;  // Captura P2 del pulmón justo antes de encender B1
float pRefVerificacion     = 10.0; // Presión al inicio del intervalo para evaluar si está subiendo sola
float pPicoDurantePausa    = 12.0; // Rastrea el valor máximo en VERIFICANDO_FLUJO (absorbe golpe de ariete)
bool sensorValido          = true;

// ================= FUNCIONES DE HARDWARE Y LECTURA =================
void apagarBomba() {
  digitalWrite(PIN_TRIAC_MOC, HIGH); // HIGH apaga el MOC3052
  bombaEncendida = false;
}

// Todo encendido físico desde estado apagado registra el arranque y suma al presupuesto horario
void encenderBomba(unsigned long ahora) {
  if (!bombaEncendida) {
    t_ultimoArranque = ahora;
    if (arranquesEnLaHora < 250) {
      arranquesEnLaHora++;
    }
    bombaEncendida = true;
  }
  digitalWrite(PIN_TRIAC_MOC, LOW);  // LOW enciende el MOC3052
}

// Conversión ratiométrica directa desde cuentas del ADC (sin convertir a voltios absolutos).
// Como tanto el sensor como el ADC están referenciados a AVCC, las variaciones de tensión
// de alimentación se cancelan tanto en la lectura de PSI como en los umbrales de diagnóstico.
float actualizarLecturaSensor() {
  uint32_t sumaADC = 0;
  for (uint8_t i = 0; i < 64; i++) {
    sumaADC += analogRead(PIN_SENSOR_P2);
  }
  float adcPromedio = sumaADC / 64.0;

  // Diagnóstico eléctrico ratiométrico directo en cuentas (45.0 a 982.0 cuentas)
  if (adcPromedio < ADC_MIN_SENSOR_VALIDO || adcPromedio > ADC_MAX_SENSOR_VALIDO) {
    sensorValido = false;
    return presionFiltrada;
  }
  sensorValido = true;

  float psiActual = 0.0;
  if (adcPromedio > ADC_CERO_PSI) {
    psiActual = (adcPromedio - ADC_CERO_PSI) * (RANGO_MAX_SENSOR_PSI / ADC_SPAN_UTIL);
  }

  // En la primera lectura válida tras encender el MCU, siembra el filtro directamente con el
  // valor real medido para arrancar ya centrado sin transitorio de convergencia.
  if (!filtroInicializado) {
    presionFiltrada    = psiActual;
    filtroInicializado = true;
  } else {
    presionFiltrada = (0.25 * psiActual) + (0.75 * presionFiltrada);
  }
  return presionFiltrada;
}

unsigned long obtenerIntervaloVerificacionAdaptativo() {
  if (verificacionesExitosas == 0) {
    return T_VERIF_INICIAL; // 90 segundos en el primer tramo tras arrancar
  } else if (verificacionesExitosas == 1) {
    return T_VERIF_MEDIA;   // 180 segundos en el segundo tramo
  }
  return T_VERIF_ESTABLE;   // 300 segundos una vez confirmado el llenado estable
}

void iniciarSondeoCalle(unsigned long ahora, float presionActual) {
  // Captura la presión retenida en el pulmón (aguas abajo del Check #1) ANTES de energizar B1
  presionBaseArranque = presionActual;
  t_inicioEstado      = ahora;
  t_condicion         = ahora;
  t_corteAlta         = ahora;
  estado              = SONDEO_CALLE;
}

#ifdef MODO_CALIBRACION
// Comando serie 'E': mide P_ESTATICA_REAL durante 10 s con la bomba apagada (no la enciende).
// Preparación: tanque a ~3 dedos del fondo, grifo abierto ~15 s y cerrado, B1 apagada.
void calibrarEstatica() {
  if (estado != REPOSO_PRESURIZADO || bombaEncendida) {
    Serial.println(F("RECHAZADO: requiere REPOSO_PRESURIZADO con bomba apagada"));
    return;
  }
  float suma = 0.0, pMin = 1000.0, pMax = 0.0;
  uint16_t n = 0;
  unsigned long t0 = millis();
  while (millis() - t0 < 10000UL) {
    wdt_reset();
    float p = actualizarLecturaSensor();
    if (!sensorValido) {
      Serial.println(F("ABORTADO: sensor invalido"));
      return;
    }
    suma += p;
    if (p < pMin) pMin = p;
    if (p > pMax) pMax = p;
    n++;
    delay(100);
  }
  float prom = suma / n;
  Serial.print(F("P_ESTATICA_REAL(prom):")); Serial.println(prom, 2);
  Serial.print(F("min:")); Serial.print(pMin, 2);
  Serial.print(F(" max:")); Serial.print(pMax, 2);
  Serial.print(F(" dispersion:")); Serial.println(pMax - pMin, 2);
  if (pMax - pMin > 0.5) Serial.println(F("AVISO: lectura inestable (grifo abierto o consumo?)"));
  if (prom < 3.0) Serial.println(F("AVISO: estatica < 3 PSI, nivel demasiado bajo o sensor/altura incorrectos"));
  Serial.print(F("Sugerido P_CAIDA_SECO = ")); Serial.println(prom - 1.2, 2);
  Serial.print(F("Sugerida precarga pulmon = ")); Serial.println(prom + 0.4, 2);
  Serial.print(F("RES,E,prom=")); Serial.print(prom, 2);
  Serial.print(F(",min=")); Serial.print(pMin, 2);
  Serial.print(F(",max=")); Serial.print(pMax, 2);
  Serial.print(F(",caida_seco=")); Serial.print(prom - 1.2, 2);
  Serial.print(F(",precarga=")); Serial.println(prom + 0.4, 2);
}

// ---------------------------------------------------------------------------
// Comandos de calibración con bomba (L, F, S, C). Solo con MODO_CALIBRACION.
// Seguridad: exigen REPOSO_PRESURIZADO, sensor válido, presupuesto horario y
// T_MIN_ENTRE_ARRANQUES; cuentan como arranques; abortan por sensor inválido,
// límite de presión, comando 'X' o tope de tiempo; mantienen el watchdog activo.
// Las líneas "RES,<cmd>,k=v,..." son para el script tools/calibrar.py.
// ---------------------------------------------------------------------------
const float CAL_P_ABORT                = 22.0;     // Límite absoluto de presión en pruebas
const float CAL_P_CARGA_INI            = 11.0;     // Inicio del cronómetro de T_CARGA_PULMON
const unsigned long CAL_T_AJUSTE       = 15000UL;  // Tiempo de estabilización con B1 encendida
const unsigned long CAL_T_MEDICION     = 10000UL;  // Ventana de promedio
const unsigned long CAL_T_MAX_BOMBA    = 120000UL; // Tope de marcha en C
const unsigned long CAL_T_MAX_SHUTOFF  = 60000UL;  // Tope de marcha en S

float calPLimite = CAL_P_ABORT;
const __FlashStringHelper* calMotivo = nullptr;

bool calLeer(float &p) {
  static unsigned long tImp = 0;
  wdt_reset();
  p = actualizarLecturaSensor();
  if (!sensorValido) { calMotivo = F("sensor invalido"); return false; }
  if (p >= calPLimite) { calMotivo = F("limite de presion alcanzado"); return false; }
  while (Serial.available()) {
    char c = Serial.read();
    if (c == 'X' || c == 'x') { calMotivo = F("abortado por X"); return false; }
  }
  unsigned long ahora = millis();
  if (ahora - tImp >= 1000UL) {
    tImp = ahora;
    Serial.print(F("P2=")); Serial.println(p, 2);
  }
  return true;
}

void calFin() {
  apagarBomba();
  calPLimite  = CAL_P_ABORT;
  t_condicion = millis();
  estado      = REPOSO_PRESURIZADO;
}

void calAbortar() {
  Serial.print(F("ABORTADO: "));
  Serial.println(calMotivo);
  calFin();
}

bool calPrecondiciones() {
  unsigned long ahora = millis();
  if (estado != REPOSO_PRESURIZADO || bombaEncendida) {
    Serial.println(F("RECHAZADO: requiere REPOSO_PRESURIZADO con bomba apagada"));
    return false;
  }
  if (!sensorValido) {
    Serial.println(F("RECHAZADO: sensor invalido"));
    return false;
  }
  if (arranquesEnLaHora >= MAX_ARRANQUES_POR_HORA) {
    Serial.println(F("RECHAZADO: presupuesto de arranques por hora agotado"));
    return false;
  }
  if (ahora - t_ultimoArranque < T_MIN_ENTRE_ARRANQUES) {
    Serial.print(F("RECHAZADO: esperar "));
    Serial.print((T_MIN_ENTRE_ARRANQUES - (ahora - t_ultimoArranque)) / 1000UL);
    Serial.println(F(" s desde el ultimo arranque"));
    return false;
  }
  return true;
}

bool calEsperar(unsigned long dur) {
  unsigned long t0 = millis();
  float p;
  while (millis() - t0 < dur) {
    if (!calLeer(p)) return false;
    delay(50);
  }
  return true;
}

bool calPromedio(unsigned long dur, float &prom, float &pMin, float &pMax) {
  float suma = 0.0, p;
  uint16_t n = 0;
  pMin = 1000.0;
  pMax = 0.0;
  unsigned long t0 = millis();
  while (millis() - t0 < dur) {
    if (!calLeer(p)) return false;
    suma += p;
    n++;
    if (p < pMin) pMin = p;
    if (p > pMax) pMax = p;
    delay(50);
  }
  prom = suma / n;
  return true;
}

// 'L': P_LLENADO_REAL. Flotador abierto, grifos cerrados. Sugiere P_ARRANQUE y P_TECHO_CEBADO_DINAMICO.
void calLlenado() {
  if (!calPrecondiciones()) return;
  Serial.println(F("L: B1 ON (flotador abierto, grifos cerrados)"));
  calPLimite = 19.5;
  float prom, pMin, pMax;
  encenderBomba(millis());
  if (!calEsperar(CAL_T_AJUSTE) || !calPromedio(CAL_T_MEDICION, prom, pMin, pMax)) {
    calAbortar();
    return;
  }
  calFin();
  Serial.print(F("P_LLENADO_REAL(prom):")); Serial.println(prom, 2);
  if (pMax - pMin > 0.5) Serial.println(F("AVISO: lectura inestable"));
  Serial.print(F("Sugerido P_ARRANQUE <= ")); Serial.println(prom - 1.0, 2);
  Serial.print(F("Sugerido P_TECHO_CEBADO_DINAMICO = ")); Serial.println(prom - 0.3, 2);
  Serial.print(F("RES,L,prom=")); Serial.print(prom, 2);
  Serial.print(F(",min=")); Serial.print(pMin, 2);
  Serial.print(F(",max=")); Serial.print(pMax, 2);
  Serial.print(F(",p_arranque=")); Serial.print(prom - 1.0, 2);
  Serial.print(F(",p_techo=")); Serial.println(prom - 0.3, 2);
}

// 'F': CAIDA_MIN_FLUJO_ACTIVO. Con B1 estable y flotador abierto, apaga y registra la caída en 3 s.
void calCaidaFlujo() {
  if (!calPrecondiciones()) return;
  Serial.println(F("F: B1 ON, luego micro-pausa de 3 s (flotador abierto)"));
  calPLimite = 19.5;
  float base, pMin, pMax, p = 0.0;
  encenderBomba(millis());
  if (!calEsperar(CAL_T_AJUSTE) || !calPromedio(5000UL, base, pMin, pMax)) {
    calAbortar();
    return;
  }
  apagarBomba();
  float pico = base, p15 = base;
  bool tomado15 = false, cruzo = false;
  unsigned long t0 = millis();
  while (millis() - t0 < T_PAUSA_VERIFICACION) {
    if (!calLeer(p)) { calAbortar(); return; }
    unsigned long dt = millis() - t0;
    if (p > pico) pico = p;
    if (!tomado15 && dt >= T_MIN_PAUSA_EFECTIVA) { p15 = p; tomado15 = true; }
    if (p <= P_ARRANQUE) cruzo = true;
    delay(20);
  }
  calFin();
  float caida15 = pico - p15;
  float caida30 = pico - p;
  bool confirma = (caida15 >= CAIDA_MIN_FLUJO_ACTIVO) || (p15 <= P_ARRANQUE);
  Serial.print(F("base=")); Serial.print(base, 2);
  Serial.print(F(" pico=")); Serial.print(pico, 2);
  Serial.print(F(" caida a 1.5s=")); Serial.print(caida15, 2);
  Serial.print(F(" caida a 3.0s=")); Serial.println(caida30, 2);
  Serial.println(confirma ? F("Flujo CONFIRMADO a 1.5 s") : F("Flujo NO confirmado a 1.5 s: revisar CAIDA_MIN_FLUJO_ACTIVO"));
  Serial.print(F("RES,F,base=")); Serial.print(base, 2);
  Serial.print(F(",pico=")); Serial.print(pico, 2);
  Serial.print(F(",caida_1_5=")); Serial.print(caida15, 2);
  Serial.print(F(",caida_3_0=")); Serial.print(caida30, 2);
  Serial.print(F(",cruzo_p_arranque=")); Serial.print(cruzo ? 1 : 0);
  Serial.print(F(",confirma=")); Serial.println(confirma ? 1 : 0);
}

// 'S': P_SHUTOFF_REAL. Flotador cerrado y casa sin consumo: sube hasta superar P_MAX_OFF o estancarse.
void calShutoff() {
  if (!calPrecondiciones()) return;
  Serial.println(F("S: B1 ON (flotador cerrado, sin consumo)"));
  float p = 0.0, pMax = 0.0, pRef = 0.0;
  bool estanco = false;
  unsigned long t0 = millis(), tRef = t0;
  encenderBomba(t0);
  while (millis() - t0 < CAL_T_MAX_SHUTOFF) {
    if (!calLeer(p)) { calAbortar(); return; }
    if (p > pMax) pMax = p;
    if (p >= P_MAX_OFF + 1.0) break;
    if (p > pRef + 0.2) { pRef = p; tRef = millis(); }
    else if (millis() - tRef >= 8000UL) { estanco = true; break; }
    delay(50);
  }
  calFin();
  bool supera = (pMax >= P_MAX_OFF);
  Serial.print(F("P_SHUTOFF max=")); Serial.println(pMax, 2);
  Serial.println(supera ? F("B1 SUPERA P_MAX_OFF") : F("B1 NO alcanza P_MAX_OFF: revisar bomba/pulmon/entrada"));
  Serial.print(F("RES,S,max=")); Serial.print(pMax, 2);
  Serial.print(F(",estanco=")); Serial.print(estanco ? 1 : 0);
  Serial.print(F(",supera_p_max_off=")); Serial.println(supera ? 1 : 0);
}

// 'C': T_CARGA_PULMON. Cronometra de 11.0 a 20.0 PSI con flotador cerrado y sin consumo.
void calCargaPulmon() {
  if (!calPrecondiciones()) return;
  float p;
  if (!calLeer(p)) { calAbortar(); return; }
  if (p > CAL_P_CARGA_INI) {
    Serial.println(F("RECHAZADO: P2 > 11 PSI; baja la presion abriendo un grifo"));
    return;
  }
  Serial.println(F("C: B1 ON (flotador cerrado, sin consumo)"));
  bool paso11 = false, completo = false;
  unsigned long t0 = millis(), t11 = 0, tFin = 0;
  encenderBomba(t0);
  while (millis() - t0 < CAL_T_MAX_BOMBA) {
    if (!calLeer(p)) { calAbortar(); return; }
    if (!paso11 && p >= CAL_P_CARGA_INI) { paso11 = true; t11 = millis(); }
    if (paso11 && p >= P_MAX_OFF) { completo = true; tFin = millis(); break; }
    delay(20);
  }
  calFin();
  if (!completo) {
    Serial.println(F("INCOMPLETO: no se llego de 11 a 20 PSI en 120 s"));
    Serial.println(F("RES,C,completo=0"));
    return;
  }
  float t = (tFin - t11) / 1000.0;
  Serial.print(F("T_CARGA_PULMON(s):")); Serial.println(t, 1);
  Serial.print(F("Sugerido T_VERIF_INICIAL >= ")); Serial.print(t * 3.0, 0); Serial.println(F(" s"));
  Serial.print(F("RES,C,completo=1,t_carga_s=")); Serial.print(t, 1);
  Serial.print(F(",t_verif_inicial_s=")); Serial.println(t * 3.0, 0);
}

void procesarComandoCal(char c) {
  switch (c) {
    case 'E': case 'e': calibrarEstatica(); break;
    case 'L': case 'l': calLlenado();       break;
    case 'F': case 'f': calCaidaFlujo();    break;
    case 'S': case 's': calShutoff();       break;
    case 'C': case 'c': calCargaPulmon();   break;
    case 'X': case 'x': calFin(); Serial.println(F("X: bomba apagada")); break;
    case '?': Serial.println(F("Comandos: E L F S C X ?")); break;
    default: break;
  }
}
#endif

void setup() {
  // 1. Apagado inmediato en frío antes de habilitar salida
  digitalWrite(PIN_TRIAC_MOC, HIGH);
  pinMode(PIN_TRIAC_MOC, OUTPUT);
  pinMode(PIN_LED_ESTADO, OUTPUT);

  // Configurar explícitamente AVCC como referencia ratiométrica del ADC
  analogReference(DEFAULT);

#ifdef MODO_CALIBRACION
  Serial.begin(9600);
#endif

  // 2. Sembrar y estabilizar el filtro EMA con lecturas reales del transductor
  for (uint8_t i = 0; i < 16; i++) {
    actualizarLecturaSensor();
  }

  unsigned long ahora = millis();
  t_inicioEstado       = ahora;
  t_condicion          = ahora;
  t_corteAlta          = ahora;
  t_caidaSeco          = ahora;
  t_ultimaVerificacion = ahora;
  t_inicioVentanaHora  = ahora;
  t_fallaSensor        = ahora;

  // 3. Configurar t_ultimoArranque para forzar un retardo de seguridad de 15 segundos
  // tras cualquier encendido o microcorte de energía eléctrica.
  t_ultimoArranque = ahora - (T_MIN_ENTRE_ARRANQUES - T_RETARDO_ENCENDIDO);

  // 4. Habilitar Watchdog a 2 segundos
  wdt_enable(WDTO_2S);
}

void loop() {
  wdt_reset();

  float P2 = actualizarLecturaSensor();
  unsigned long ahora = millis();

  // Reinicio estricto de la ventana horaria del presupuesto térmico únicamente cada 60 minutos reales
  if (ahora - t_inicioVentanaHora >= T_VENTANA_UNA_HORA) {
    t_inicioVentanaHora = ahora;
    arranquesEnLaHora   = 0;
  }

#ifdef MODO_CALIBRACION
  if (ahora - t_reporteSerial >= 1000UL) {
    t_reporteSerial = ahora;
    Serial.print(F("P2_PSI:"));
    Serial.print(P2, 2);
    Serial.print(F(" | Estado:"));
    Serial.print((int)estado);
    Serial.print(F(" | ArranquesHora:"));
    Serial.println(arranquesEnLaHora);
  }
  if (Serial.available()) {
    char c = Serial.read();
    procesarComandoCal(c);
  }
#endif

  // ================= SUPERVISIÓN GLOBAL DE CABLE SUELTO / SENSOR ROTO =================
  if (!sensorValido) {
    if (ahora - t_fallaSensor >= 1500UL && estado != FALLA_SENSOR) {
      apagarBomba();
      t_inicioEstado = ahora;
      estado = FALLA_SENSOR;
    }
  } else {
    t_fallaSensor = ahora;
  }

  // ================= MÁQUINA DE ESTADOS PRINCIPAL =================
  switch (estado) {

    // ------------------------------------------------------------------
    // ESTADO 1: PULMÓN PRESURIZADO / TANQUE LLENO (BOMBA APAGADA)
    // ------------------------------------------------------------------
    case REPOSO_PRESURIZADO: {
      apagarBomba();
      digitalWrite(PIN_LED_ESTADO, LOW);

      // Si se agotó el presupuesto de 18 arranques en la hora actual, pasa a enfriamiento
      if (arranquesEnLaHora >= MAX_ARRANQUES_POR_HORA) {
        t_inicioEstado = ahora;
        estado         = PAUSA_ENFRIAMIENTO_MOTOR;
        break;
      }

      // Exige 180s desde el último arranque
      bool ventanaArranqueLista = (ahora - t_ultimoArranque >= T_MIN_ENTRE_ARRANQUES);

      if (P2 <= P_ARRANQUE && ventanaArranqueLista) {
        if (ahora - t_condicion >= T_DEMANDA) {
          iniciarSondeoCalle(ahora, P2);
        }
      } else {
        t_condicion = ahora;
      }
      break;
    }

    // ------------------------------------------------------------------
    // ESTADO 2: ARRANQUE DE PRUEBA (4.0s CON CHECK #0 MANTENIENDO VOLUTA LLENA)
    // ------------------------------------------------------------------
    case SONDEO_CALLE: {
      encenderBomba(ahora);
      digitalWrite(PIN_LED_ESTADO, HIGH);

      float umbralCebadoDinamico = presionBaseArranque + DELTA_P_CEBADO;
      if (umbralCebadoDinamico < P_ARRANQUE) {
        umbralCebadoDinamico = P_ARRANQUE;
      }
      if (umbralCebadoDinamico > P_TECHO_CEBADO_DINAMICO) {
        umbralCebadoDinamico = P_TECHO_CEBADO_DINAMICO;
      }

      // 1. Verificar si cargó directo el pulmón a 20 PSI (tanque lleno y sin consumo en casa)
      if (P2 >= P_MAX_OFF) {
        if (ahora - t_corteAlta >= T_CORTE_20PSI) {
          apagarBomba();
          intentosFallidos = 0;
          t_condicion      = ahora;
          estado           = REPOSO_PRESURIZADO;
          break;
        }
      } else {
        t_corteAlta = ahora;
      }

      // 2. Verificar si superó el umbral dinámico de cebado durante 1.0s
      if (P2 >= umbralCebadoDinamico) {
        if (ahora - t_condicion >= T_CONFIRMA_CEBADO) {
          intentosFallidos       = 0; // Éxito de cebado: reinicia el escalonamiento de espera sin agua
          verificacionesExitosas = 0; // Inicia el intervalo adaptativo en 90s
          pRefVerificacion       = P2;
          t_inicioEstado         = ahora;
          t_corteAlta            = ahora;
          t_caidaSeco            = ahora;
          t_ultimaVerificacion   = ahora;
          estado                 = BOMBEO_ACTIVO;
          break;
        }
      } else {
        t_condicion = ahora;
      }

      // 3. Guardián de tiempo independiente (4.0s sin Delta P, o 6.5s absolutos):
      bool sinIncrementoDeCebado  = (P2 < umbralCebadoDinamico) && (ahora - t_inicioEstado >= T_PRUEBA_CEBADO);
      bool tiempoAbsolutoExcedido = (ahora - t_inicioEstado >= T_MAX_SONDEO_ABS);

      if (sinIncrementoDeCebado || tiempoAbsolutoExcedido) {
        apagarBomba();
        if (intentosFallidos < 10) intentosFallidos++;
        t_inicioEstado = ahora;
        estado         = ESPERA_SIN_AGUA;
      }
      break;
    }

    // ------------------------------------------------------------------
    // ESTADO 3: BOMBEO NORMAL CON VERIFICACIÓN ADAPTATIVA EN TODA LA FRANJA
    // ------------------------------------------------------------------
    case BOMBEO_ACTIVO: {
      encenderBomba(ahora);
      digitalWrite(PIN_LED_ESTADO, HIGH);

      // --- A. Temporizador dedicado de Corte por Pulmón Cargado (>= 20.0 PSI por 2.0s) ---
      if (P2 >= P_MAX_OFF) {
        if (ahora - t_corteAlta >= T_CORTE_20PSI) {
          apagarBomba();
          intentosFallidos = 0;
          t_condicion      = ahora;
          estado           = REPOSO_PRESURIZADO;
          break;
        }
      } else {
        t_corteAlta = ahora;
      }

      // --- B. Temporizador dedicado de Caída Crítica (< 6.0 PSI por 3.0s) ---
      if (P2 < P_CAIDA_SECO) {
        if (ahora - t_caidaSeco >= T_CAVITACION_RAPIDA) {
          apagarBomba();
          if (intentosFallidos < 10) intentosFallidos++;
          t_inicioEstado = ahora;
          estado         = ESPERA_SIN_AGUA;
          break;
        }
      } else {
        t_caidaSeco = ahora;
      }

      // --- C. Verificación Hidrodinámica Adaptativa (90s -> 180s -> 300s) ---
      unsigned long intervaloActual = obtenerIntervaloVerificacionAdaptativo();
      if (ahora - t_ultimaVerificacion >= intervaloActual) {
        // Solo omite la micro-pausa si la presión demostró estar subiendo activamente (+0.75 PSI)
        if (P2 - pRefVerificacion >= AVANCE_OMITE_PAUSA) {
          pRefVerificacion     = P2;
          t_ultimaVerificacion = ahora;
        }
        // Si alcanzó el tope de 18 arranques en la hora, pasa a pausa térmica (casa con agua por Check #2)
        else if (arranquesEnLaHora >= MAX_ARRANQUES_POR_HORA) {
          apagarBomba();
          t_inicioEstado = ahora;
          estado         = PAUSA_ENFRIAMIENTO_MOTOR;
          break;
        }
        // Si la presión lleva todo el intervalo estacionada (en cualquier valor entre 9.5 y 19.9 PSI),
        // hace la micro-pausa: si estaba llenando el tanque caerá <= P_ARRANQUE (9.5 PSI) o caerá >= 0.8 PSI;
        // si estaba atrapada por el Check #1 sin agua en la calle, quedará fija y cortará en 3 segundos.
        else {
          pPicoDurantePausa      = P2;
          flujoConfirmadoEnPausa = false;
          apagarBomba();
          t_inicioPausaVerif     = ahora;
          estado                 = VERIFICANDO_FLUJO;
          break;
        }
      }

      // --- D. Límite de marcha continua llenando tanque (45 min acumulados) ---
      if (ahora - t_inicioEstado >= T_MAX_BOMBEO) {
        apagarBomba();
        t_inicioEstado = ahora;
        estado         = PAUSA_ENFRIAMIENTO_MOTOR;
        break;
      }

      break;
    }

    // ------------------------------------------------------------------
    // ESTADO 4: MICRO-PAUSA ACTIVA (MÍNIMO 1.5s APAGADA, MÁXIMO 3.0s)
    // ------------------------------------------------------------------
    case VERIFICANDO_FLUJO: {
      apagarBomba();
      digitalWrite(PIN_LED_ESTADO, LOW);

      unsigned long tiempoEnPausa = ahora - t_inicioPausaVerif;

      // 1. Rastrear el pico máximo alcanzado tras el apagado (absorbe golpe de ariete o rebote)
      if (P2 > pPicoDurantePausa) {
        pPicoDurantePausa = P2;
      }

      // 2. Si durante la pausa la presión alcanza el corte de 20 PSI, el tanque ya se llenó
      if (P2 >= P_MAX_OFF) {
        intentosFallidos = 0;
        t_condicion      = ahora;
        estado           = REPOSO_PRESURIZADO;
        break;
      }

      // 3. Evaluar si ocurrió caída de presión que demuestra flujo hacia el tanque o la casa:
      //    - Alta fricción: cae >= 0.80 PSI desde el pico.
      //    - Baja fricción (llenado entre 9.6 y 10.2 PSI): cruza <= P_ARRANQUE (9.5 PSI) rumbo a estática.
      float caidaDesdePico = pPicoDurantePausa - P2;
      if (caidaDesdePico >= CAIDA_MIN_FLUJO_ACTIVO || P2 <= P_ARRANQUE) {
        flujoConfirmadoEnPausa = true;
      }

      // 4. Si se confirmó el flujo PERO garantizando al menos 1.5s de pausa efectiva (T_MIN_PAUSA_EFECTIVA)
      //    para que el rotor no sufra reconexión instantánea ni agote los arranques por hora:
      if (flujoConfirmadoEnPausa && (tiempoEnPausa >= T_MIN_PAUSA_EFECTIVA)) {
        if (verificacionesExitosas < 10) verificacionesExitosas++;
        pRefVerificacion     = P2;
        t_ultimaVerificacion = ahora;
        t_corteAlta          = ahora;
        t_caidaSeco          = ahora;
        encenderBomba(ahora); // Contabiliza el re-arranque en arranquesEnLaHora
        estado               = BOMBEO_ACTIVO;
        break;
      }

      // 5. Al cumplirse los 3.0 segundos sin caída apreciable (< 0.80 PSI y > 9.5 PSI): la red está estanca.
      if (tiempoEnPausa >= T_PAUSA_VERIFICACION) {
        if (P2 >= P_CASI_LLENO_PAUSA) {
          intentosFallidos = 0;
          t_condicion      = ahora;
          estado           = REPOSO_PRESURIZADO;
        } else {
          if (intentosFallidos < 10) intentosFallidos++;
          t_inicioEstado = ahora;
          estado         = ESPERA_SIN_AGUA;
        }
      }
      break;
    }

    // ------------------------------------------------------------------
    // ESTADO 5: PROTECCIÓN POR CALLE SECA (ESPERA ESCALONADA RÁPIDA: 5/10/20 MIN)
    // Nota de diseño: intentosFallidos NO se reinicia al salir hacia SONDEO_CALLE;
    // permanece en >=3 (esperas de 20 min) durante cortes largos de red pública hasta
    // que un ciclo confirme flujo de agua real o la presión supere P_ARRANQUE.
    // ------------------------------------------------------------------
    case ESPERA_SIN_AGUA: {
      apagarBomba();
      // Parpadeo rápido (2 Hz) indicando falta de agua en la calle
      digitalWrite(PIN_LED_ESTADO, (ahora / 250UL) % 2);

      unsigned long tiempoEsperaActual = T_ESPERA_3; // 20 min (para intentosFallidos >= 3)
      if (intentosFallidos == 1) {
        tiempoEsperaActual = T_ESPERA_1;             // 5 min
      } else if (intentosFallidos == 2) {
        tiempoEsperaActual = T_ESPERA_2;             // 10 min
      }

      bool ventanaArranqueLista = (ahora - t_ultimoArranque >= T_MIN_ENTRE_ARRANQUES);
      bool presupuestoHoraOk    = (arranquesEnLaHora < MAX_ARRANQUES_POR_HORA);

      if ((ahora - t_inicioEstado >= tiempoEsperaActual) && ventanaArranqueLista && presupuestoHoraOk) {
        if (P2 > P_ARRANQUE) {
          intentosFallidos = 0; // La red recuperó presión por sí sola
          t_condicion      = ahora;
          estado           = REPOSO_PRESURIZADO;
        } else {
          iniciarSondeoCalle(ahora, P2);
        }
      }
      break;
    }

    // ------------------------------------------------------------------
    // ESTADO 6: PAUSA DE ENFRIAMIENTO TRAS 45 MIN O TOPE DE ARRANQUES/HORA
    // Nota: Durante estos 20 min la casa sigue teniendo agua por gravedad vía Check #2.
    // ------------------------------------------------------------------
    case PAUSA_ENFRIAMIENTO_MOTOR: {
      apagarBomba();
      // Doble destello lento indicando pausa térmica de 20 minutos
      unsigned long cicloLed = ahora % 2000UL;
      bool encenderLed = (cicloLed < 150UL) || (cicloLed >= 300UL && cicloLed < 450UL);
      digitalWrite(PIN_LED_ESTADO, encenderLed ? HIGH : LOW);

      // Tras 20 minutos de enfriamiento (1/3 de hora):
      // NO se reinicia t_inicioVentanaHora ni se borra a cero el contador para no duplicar el
      // presupuesto térmico en la misma hora. Solo se descuenta el crédito proporcional a los
      // 20 minutos de descanso (6 arranques) si la ventana de 60 min aún no ha expirado sola.
      if (ahora - t_inicioEstado >= T_ENFRIAMIENTO_MAX) {
        if (arranquesEnLaHora >= CREDITO_ENFRIAMIENTO_20MIN) {
          arranquesEnLaHora -= CREDITO_ENFRIAMIENTO_20MIN;
        } else {
          arranquesEnLaHora = 0;
        }
        t_condicion = ahora;
        estado      = REPOSO_PRESURIZADO;
      }
      break;
    }

    // ------------------------------------------------------------------
    // ESTADO 7: FALLA ELÉCTRICA DEL SENSOR (CABLE SUELTO O CORTOCIRCUITO)
    // ------------------------------------------------------------------
    case FALLA_SENSOR: {
      apagarBomba();
      // Destello muy rápido (10 Hz) indicando cable de sensor suelto o en corto
      digitalWrite(PIN_LED_ESTADO, (ahora / 50UL) % 2);

      // Si el sensor vuelve a dar lectura válida (45 a 982 cuentas) por 3 segundos, se recupera solo
      if (sensorValido) {
        if (ahora - t_inicioEstado >= 3000UL) {
          t_condicion = ahora;
          estado      = REPOSO_PRESURIZADO;
        }
      } else {
        t_inicioEstado = ahora;
      }
      break;
    }
  }
}
