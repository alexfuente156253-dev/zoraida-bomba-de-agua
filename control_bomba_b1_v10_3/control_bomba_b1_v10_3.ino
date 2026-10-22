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
 *   - Con la bomba apagada (reposo o `ESPERA_SIN_FLUJO`), la vivienda NO se queda sin agua: al abrir un grifo y bajar la presión de la línea a ~7.1 PSI, la
 *     válvula `CHECK #2` se abre automáticamente por gravedad y abastece la casa desde el tanque.
 *
 * TABLA DE 5 MEDICIONES EN CAMPO (SIN AGREGAR HARDWARE):
 *   1. P_ESTATICA_REAL: Bomba apagada, grifo abierto 15s y cerrado. Leer presión sostenida por
 *      el tanque (~7.1 PSI lleno). Con el tanque LLENO el promedio es `P_ESTATICA_MAX`; con el tanque
 *      casi vacío se obtiene el mínimo y se precarga el aire del pulmón (sin agua) a mín + 0.4 PSI.
 *   2. P_LLENADO_REAL: Flotador del tanque abierto y grifos de casa cerrados. Encender B1 y leer
 *      presión estable (comando 'L'). Ajustar `P_ARRANQUE` >= 1.0 PSI por debajo de P_LLENADO_REAL.
 *      (El control no usa un valor fijo de llenado: aprende la meseta real en cada ciclo.)
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
#include <EEPROM.h>

// Registro de persistencia en EEPROM (ver "PERSISTENCIA EN EEPROM"). Se declara aquí, antes de cualquier
// función, porque el preprocesador de Arduino genera los prototipos al inicio del archivo.
struct RegistroPersistente {
  uint8_t  marca;         // MARCA_EEPROM si el registro es válido
  uint8_t  intentos;
  uint16_t secuencia;     // Crece en cada escritura: la mayor es la más reciente
  uint8_t  reservado[3];
  uint8_t  verificacion;  // Suma de control
};

// Evento del registro de depuración en EEPROM (ver "REGISTRO DE EVENTOS").
struct EventoLog {
  uint32_t t_s;         // Segundos desde el encendido
  uint16_t secuencia;   // Crece en cada evento; 0xFFFF = posición vacía
  int16_t  p_x10;       // P2 en décimas de PSI
  uint8_t  estado;      // Estado nuevo (EstadoSistema)
  uint8_t  motivo;      // Motivo del cambio (MotivoEvento)
  uint8_t  intentos;    // intentosFallidos en ese momento
};

// Depuración por USB: con MODO_DEPURACION (entorno PlatformIO nano_depuracion) cada evento y una
// telemetría por segundo salen por el puerto serie a 9600, y los comandos D (descargar registro) y
// B (borrar registro) quedan disponibles. El registro en EEPROM se escribe siempre, en todos los modos.
// #define MODO_DEPURACION

// Descomentar SOLO durante las pruebas de calibración con PC conectada.
// Uso de recursos en ATmega328P (medido con PlatformIO):
//   - Desactivado (Producción): Flash = 6482 bytes (21.1%), SRAM = 95 bytes (4.6%)
//   - Activado (Calibración):   Flash = 13878 bytes (45.2%), SRAM = 304 bytes (14.8%)
//   - MODO_DEPURACION:          Flash = 9048 bytes (29.5%), SRAM = 290 bytes (14.2%)
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
// casi vacío a ~3 dedos del fondo (~5.0 PSI estimado). La precarga del pulmón se calcula con la estática
// MÍNIMA medida (tanque casi vacío) + 0.4 PSI. "Bomba en vacío" y "sin flujo" son el mismo estado: se
// detecta con P_UMBRAL_FLUJO (ver abajo). Ver docs/CALIBRACION.md.
const float P_ARRANQUE              = 9.5;  // Cut-in: arranca cuando el pulmón baja a <= 9.5 PSI (+3.5 PSI)
const float P_MAX_OFF               = 20.0; // Cut-off: corta cuando el pulmón llega a 20.0 PSI

// Incremento relativo exigido en SONDEO_CALLE para demostrar que B1 abrió el Check #1
const float DELTA_P_CEBADO          = 1.5;  // Exige +1.5 PSI (> 6 cuentas del ADC) sobre la base de arranque

// Detección de flujo por SUBIDA SOSTENIDA de presión + referencia a la estática (criterio del usuario):
//   - Sin agua la bomba no puede subir P2; si P2 queda plana o cerca de la estática de la columna de
//     llenado, no hay flujo, aunque esté por encima de la estática (pulmón retenido por el Check #1).
//   - P_ESTATICA_MAX = estática con tanque LLENO (columna de la subida, entra por arriba). Medir con 'E'.
const float P_ESTATICA_MAX          = 7.1;  // SUPUESTO ~7.1 PSI: medir con tanque lleno y reemplazar
const float DELTA_SOBRE_ESTATICA    = 1.0;  // Hay flujo solo si P2 supera la estática en 1.0 PSI
// Umbral ÚNICO de flujo (criterio del usuario: estática 7.5 -> hay flujo por encima de 8.5 PSI). Lo usan
// la verificación al arrancar (exigir P2 por encima) y el bombeo (P2 <= umbral 10 s = sin flujo).
const float P_UMBRAL_FLUJO          = P_ESTATICA_MAX + DELTA_SOBRE_ESTATICA;
const float PENDIENTE_MIN_SUBIDA    = 0.3;  // PSI/s mínimos para confirmar flujo al arrancar (sondeo)
const unsigned long T_VENTANA_PENDIENTE = 1000UL; // Ventana de pendiente en el arranque (sondeo de 4 s)
const unsigned long T_SIN_FLUJO         = 1000UL;  // Ya con flujo: P2 toca el umbral 1 s -> sin flujo (criterio del usuario)
const unsigned long T_SIN_FLUJO_REARRANQUE = 10000UL; // Tras arrancar o reanudar, plazo para que P2 supere el umbral
// En bombeo, "estancada" = la presión NO SUBE: en T_ESTANCAMIENTO no supera en SUBIDA_MIN_ESTANCAMIENTO el
// máximo ya alcanzado. Una subida lenta (p. ej. el flotador cerrando poco a poco) sigue siendo subida.
const unsigned long T_ESTANCAMIENTO     = 10000UL; // 10 s sin subir = aceleración 0
const float SUBIDA_MIN_ESTANCAMIENTO    = 0.25;    // PSI (~1 cuenta del ADC) para contar como subida

// Mesetas y micro-pausa en cada estancamiento (criterio del usuario). Al energizar B1 siempre se verifica
// flujo (SONDEO_CALLE). Con flujo confirmado se mide la aceleración en ventanas de 3 s; cada vez que llega
// a 0 (T_ESTANCAMIENTO) en un NIVEL NUEVO (primera vez en el ciclo, o más de MARGEN_NUEVO_NIVEL sobre la
// última meseta confirmada) se hace una micro-pausa de prueba (VERIFICANDO_FLUJO):
//   - P2 cae   -> sigue llenando: ese nivel es la nueva meseta (modo LLENANDO TANQUE) y sigue bombeando.
//   - no cae   -> terminó de llenar (flotador cerrado): sobre PISO_FLUJO_TOTAL apaga en esa presión de
//                 estancamiento (bomba con poca potencia; REPOSO, re-arranca al bajar a P_ARRANQUE);
//                 bajo el piso se fue el flujo total (ESPERA_SIN_FLUJO).
//   - llega a P_MAX_OFF -> corte normal, sin prueba.
//   - se estanca entre P_LLENO_ESTANCADO y P_MAX_OFF -> tanque lleno (corte, sin prueba).
// Estancarse en la misma meseta (± margen) es llenado normal: no se prueba (sigue la micro-pausa periódica).
const float MARGEN_NUEVO_NIVEL      = 0.5;  // PSI sobre la meseta confirmada para considerar un nivel nuevo
const float P_LLENO_ESTANCADO       = 18.0; // Estancada entre 18 y 20 PSI = tanque lleno (criterio del usuario)
const float PISO_FLUJO_TOTAL        = P_ARRANQUE + 1.0; // Estancada por debajo de esto: sin flujo total

// Parámetros de la Micro-Pausa de Verificación Hidrodinámica
const float AVANCE_OMITE_PAUSA      = 0.75; // Si P2 subió >= 0.75 PSI (>3 cuentas) en el intervalo, omite apagar B1
const float CAIDA_MIN_FLUJO_ACTIVO  = 0.80; // Caída mínima, desde la presión al apagar, que confirma flujo abierto
const unsigned long T_IGNORAR_INICIO_PAUSA = 500UL; // Primeros 0.5 s de la pausa sin evaluar (golpe de ariete)

// ================= TIEMPOS DE CONTROL Y PROTECCIÓN (ms) =================
const unsigned long T_PRUEBA_CEBADO      = 4000UL;   // 4.0 seg máx en SONDEO_CALLE (Check #0 mantiene B1 cebada)
const unsigned long T_MAX_SONDEO_ABS     = 6500UL;   // 6.5 seg techo absoluto en SONDEO_CALLE ante oscilaciones
const unsigned long T_CONFIRMA_CEBADO    = 1000UL;   // 1.0 seg sosteniendo el Delta P confirma flujo hacia pulmón
const unsigned long T_DEMANDA            = 1200UL;   // 1.2 seg bajo 9.5 PSI para confirmar descarga del pulmón

// Intervalos Adaptativos de Micro-Pausa (cubre toda la franja de 9.5 a 19.9 PSI)
const unsigned long T_VERIF_INICIAL      = 90000UL;  // 1ra verificación a los 90 seg
const unsigned long T_VERIF_MEDIA        = 180000UL; // 2da verificación a los 180 seg (3 min)
const unsigned long T_VERIF_ESTABLE      = 300000UL; // 3ra verificación en adelante cada 300 seg (5 min)
const unsigned long T_MIN_PAUSA_EFECTIVA = 1500UL;   // 1.5 seg mínimos con motor apagado antes de permitir re-encendido
const unsigned long T_PAUSA_VERIFICACION = 3000UL;   // 3.0 seg ventana total de micro-pausa para evaluar caída

// Protección Anti-Ciclado (la protección térmica del motor la da el relé térmico, por hardware)
const unsigned long T_MIN_ENTRE_ARRANQUES = 180000UL;  // 180 seg (3 min) entre arranques desde reposo
const unsigned long T_RETARDO_ENCENDIDO   = 15000UL;   // 15 seg de espera obligatoria tras corte eléctrico
// Tiempo máximo de bombeo continuo sin llegar a tanque lleno (red de seguridad ante fugas, grifos
// abiertos o casos no previstos). Cuenta desde que se confirma flujo, incluidas las micro-pausas.
const unsigned long T_MAX_BOMBEO          = 2700000UL; // 45 minutos

// Tiempos de espera escalonada cuando NO hay agua en la calle (con Check #0)
const unsigned long T_ESPERA_1 = 300000UL;  // 5 minutos  (1er intento sin agua en calle)
const unsigned long T_ESPERA_2 = 600000UL;  // 10 minutos (2do intento sin agua en calle)
const unsigned long T_ESPERA_3 = 1200000UL; // 20 minutos (3er intento en adelante)

// ================= MÁQUINA DE ESTADOS =================
enum EstadoSistema {
  REPOSO_PRESURIZADO,       // Pulmón cargado entre 9.5 y 20 PSI, bomba apagada
  SONDEO_CALLE,             // Modo verificación de flujo al energizar B1: subida sostenida (4 s máx)
  BOMBEO_ACTIVO,            // Flujo confirmado: modo LLENANDO TANQUE o HIDRONEUMÁTICO (hasta 20 PSI)
  VERIFICANDO_FLUJO,        // Micro-pausa activa (1.5s a 3.0s) cuando P2 está en meseta sin subir
  ESPERA_SIN_FLUJO,          // Sin flujo (calle seca, bomba sin agua o pérdida de cebado): espera escalonada 5/10/20 min (casa con agua por Check #2)
  FALLA_SENSOR              // Sensor desconectado (<45 cuentas) o en corto (>982 cuentas)
};

EstadoSistema estado = REPOSO_PRESURIZADO;

// Motivo de cada cambio de estado, para el registro de eventos. Los valores no cambian entre versiones
// (el script tools/depurar.py los traduce): agregar nuevos al final.
enum MotivoEvento : uint8_t {
  M_NINGUNO = 0,
  M_ENCENDIDO,            // 1  Encendido del Arduino
  M_DEMANDA,              // 2  P2 bajó a P_ARRANQUE: arranca la verificación de flujo
  M_FLUJO_CONFIRMADO,     // 3  La verificación confirmó flujo: bombeo
  M_SIN_SUBIDA,           // 4  La verificación no vio subida sostenida: sin flujo
  M_CORTE_20,             // 5  P2 llegó a P_MAX_OFF: corte
  M_LLENO_ESTANCADO,      // 6  Estancada entre 18 y 20 PSI: tanque lleno
  M_PRUEBA_NIVEL_NUEVO,   // 7  Estancada en un nivel nuevo: micro-pausa de prueba
  M_PAUSA_PERIODICA,      // 8  Micro-pausa periódica del llenado
  M_PAUSA_CON_CAIDA,      // 9  En la micro-pausa la presión cayó: sigue llenando
  M_ESTANCAMIENTO,        // 10 No cayó, sobre el piso: bomba de poca potencia, reposo
  M_SIN_FLUJO_PAUSA,      // 11 No cayó, bajo el piso: sin flujo total
  M_SIN_FLUJO_NIVEL,      // 12 Con la bomba encendida P2 bajó al umbral de flujo: sin flujo
  M_TIEMPO_MAXIMO,        // 13 45 min de bombeo sin llegar a lleno
  M_FIN_ESPERA_REPOSO,    // 14 Terminó la espera con presión en el pulmón: reposo
  M_REINTENTO,            // 15 Terminó la espera: nueva verificación de flujo
  M_FALLA_SENSOR,         // 16 Sensor desconectado o en corto
  M_SENSOR_RECUPERADO,    // 17 El sensor volvió a leer bien
  M_CALIBRACION           // 18 Fin de una prueba de calibración
};
uint8_t motivo = M_NINGUNO;               // Se fija junto a cada cambio de estado
EstadoSistema estadoRegistrado = REPOSO_PRESURIZADO;

// Temporizadores dedicados e independientes
unsigned long t_inicioEstado        = 0;
unsigned long t_condicion           = 0;
unsigned long t_ultimaVerificacion  = 0;
unsigned long t_inicioPausaVerif    = 0;
unsigned long t_ultimoArranque      = 0;
unsigned long t_fallaSensor         = 0;
unsigned long t_ventanaPendiente    = 0;
unsigned long t_sinFlujo            = 0;
unsigned long t_estancamiento       = 0; // Última vez que P2 aceleró en modo hidroneumático

#ifdef MODO_CALIBRACION
unsigned long t_reporteSerial       = 0;
#endif
#ifdef MODO_DEPURACION
unsigned long t_telemetria          = 0;
#endif

bool bombaEncendida             = false;
bool flujoConfirmadoEnPausa     = false; // Registra caída válida esperando cumplir T_MIN_PAUSA_EFECTIVA (1.5s)
bool filtroInicializado         = false; // Siembra presionFiltrada con la 1ra lectura real sin sesgo inicial

// Contador de intentos fallidos por falta de flujo:
// Escala la espera (1->5m, 2->10m, >=3->20m). Por diseño se mantiene en >=3 durante cortes
// prolongados de suministro y solo vuelve a 0 cuando la bomba demuestra flujo (verificación al
// arrancar confirmada, corte a 20 PSI o apagado por estancamiento sobre el piso). Una P2 alta en
// la espera NO lo reinicia: puede ser presión retenida en el pulmón y no agua de la calle.
uint8_t intentosFallidos        = 0;
uint8_t verificacionesExitosas  = 0;     // Escala el intervalo adaptativo: 0->90s, 1->180s, >=2->300s

float presionFiltrada      = 0.0;  // Se inicializa directamente con la primera lectura válida en setup()
float presionBaseArranque  = 9.0;  // Captura P2 del pulmón justo antes de encender B1
float pRefVerificacion     = 10.0; // Presión al inicio del intervalo para evaluar si está subiendo sola
float pInicioPausa         = 12.0; // P2 en el instante de apagar para la micro-pausa (referencia de la caída)
bool sensorValido          = true;
float pVentanaPendiente    = 0.0;  // P2 al inicio de la ventana de pendiente (SONDEO_CALLE)
bool subidaCebado          = false; // true si en alguna ventana de 1 s P2 subió >= la pendiente mínima
// Modo de bombeo (informativo: reporte serie y depuración; el control no depende de él):
//   SUBIENDO       bomba encendida, todavía sin meseta de llenado confirmada
//   LLENANDO       meseta confirmada y P2 en ella
//   HIDRONEUMATICO P2 sobre la meseta, o el ciclo terminó arriba (corte, lleno o estancamiento)
enum ModoBombeo : uint8_t { MODO_SUBIENDO = 0, MODO_LLENANDO = 1, MODO_HIDRONEUMATICO = 2 };
uint8_t modoBombeo         = MODO_SUBIENDO;
bool flujoSobreUmbral      = false; // true cuando P2 ya superó P_UMBRAL_FLUJO desde el (re)arranque
// Presión de estancamiento: donde la bomba perdió fuerza y su aceleración llegó a 0 sin alcanzar P_MAX_OFF
// (ej. 15 PSI). Ahí se apaga y re-arranca al bajar a P_ARRANQUE. Informativo (reporte serie de calibración).
float pEstancamiento       = 0.0;
// Meseta de llenado confirmada en el ciclo actual (< 0 = aún desconocida). Se fija cada vez que una
// micro-pausa demuestra flujo al tanque; se reinicia en cada arranque (no se arrastra entre ciclos).
float pMeseta              = -1.0;
float pMaxEstanc           = 0.0;   // Máximo de P2 desde la última subida (detección de estancamiento)

// ================= PERSISTENCIA EN EEPROM (sobrevive a cortes de luz) =================
// Se guarda intentosFallidos, para que un corte o un parpadeo de la luz no reinicie la escalada de
// esperas sin flujo. Sin reloj no se sabe cuánto duró el corte: al volver se asume lo más conservador
// (la espera del nivel de intentos alcanzado se repite completa).
// Desgaste: ~100.000 escrituras por celda. Un registro de 8 bytes rota en 64 posiciones (512 bytes)
// y solo se escribe cuando algo cambia: ~6,4 millones de escrituras, décadas de uso.
const uint8_t MARCA_EEPROM = 0xA5;
const uint8_t SLOTS_EEPROM = 64;

uint8_t  slotEeprom           = 0;
uint16_t secuenciaEeprom      = 0;
uint8_t  intentosGuardados    = 0;

uint8_t sumaControl(const RegistroPersistente &r) {
  return (uint8_t)(r.marca + r.intentos + (r.secuencia & 0xFF) + (r.secuencia >> 8) +
                   r.reservado[0] + r.reservado[1] + r.reservado[2] + 0x5A);
}

// Busca el registro válido más reciente. false si no hay ninguno (EEPROM nueva o borrada).
bool leerPersistencia(RegistroPersistente &mejor) {
  bool encontrado = false;
  for (uint8_t i = 0; i < SLOTS_EEPROM; i++) {
    RegistroPersistente r;
    EEPROM.get(i * sizeof(RegistroPersistente), r);
    if (r.marca != MARCA_EEPROM || r.verificacion != sumaControl(r)) continue;
    if (!encontrado || (int16_t)(r.secuencia - mejor.secuencia) > 0) {
      mejor      = r;
      slotEeprom = i;
      encontrado = true;
    }
  }
  return encontrado;
}

// Escribe un registro nuevo (en la posición siguiente) solo si cambiaron los intentos.
void guardarPersistenciaSiCambio() {
  if (intentosFallidos == intentosGuardados) {
    return;
  }
  RegistroPersistente r;
  r.marca        = MARCA_EEPROM;
  r.intentos     = intentosFallidos;
  r.secuencia    = ++secuenciaEeprom;
  r.reservado[0] = 0;
  r.reservado[1] = 0;
  r.reservado[2] = 0;
  r.verificacion = sumaControl(r);
  slotEeprom = (slotEeprom + 1) % SLOTS_EEPROM;
  EEPROM.put(slotEeprom * sizeof(RegistroPersistente), r);
  intentosGuardados = intentosFallidos;
}

// ================= REGISTRO DE EVENTOS (DEPURACIÓN, SOBREVIVE A CORTES DE LUZ) =================
// Cada cambio de estado se guarda con su motivo, la presión y los intentos, en la mitad alta de la EEPROM
// (bytes 512-1023; la baja es de la persistencia). Rota entre las posiciones disponibles (46 eventos de
// 11 bytes): se conservan siempre los últimos. Desgaste: cada posición se reescribe una vez cada 46
// eventos; con ~60 eventos por hora son ~10 años hasta las 100.000 escrituras por celda.
const uint16_t LOG_BASE  = 512;
const uint8_t  LOG_SLOTS = (1024 - LOG_BASE) / sizeof(EventoLog);

uint16_t secuenciaLog = 0;
uint8_t  slotLog      = 0;

// Al encender: continúa la numeración desde el último evento guardado.
void iniciarRegistroEventos() {
  bool hay = false;
  for (uint8_t i = 0; i < LOG_SLOTS; i++) {
    EventoLog e;
    EEPROM.get(LOG_BASE + i * sizeof(EventoLog), e);
    if (e.secuencia == 0xFFFF) continue;
    if (!hay || (int16_t)(e.secuencia - secuenciaLog) > 0) {
      secuenciaLog = e.secuencia;
      slotLog      = i;
      hay          = true;
    }
  }
  if (!hay) slotLog = LOG_SLOTS - 1;   // El primer evento irá a la posición 0
}

#ifdef MODO_DEPURACION
// Una línea CSV por evento: "EV,..." en vivo o "LOG,..." al descargar el registro.
void imprimirEvento(const EventoLog &e, bool enVivo) {
  Serial.print(enVivo ? F("EV,") : F("LOG,"));
  Serial.print(e.secuencia); Serial.print(',');
  Serial.print(e.t_s);       Serial.print(',');
  Serial.print(e.estado);    Serial.print(',');
  Serial.print(e.motivo);    Serial.print(',');
  Serial.print(e.p_x10 / 10.0, 1); Serial.print(',');
  Serial.println(e.intentos);
}

// 'D': envía el registro completo, del evento más antiguo al más reciente.
void descargarRegistro() {
  Serial.println(F("LOG_INICIO,secuencia,t_s,estado,motivo,p2,intentos"));
  for (uint8_t k = 1; k <= LOG_SLOTS; k++) {
    uint8_t i = (slotLog + k) % LOG_SLOTS;
    EventoLog e;
    EEPROM.get(LOG_BASE + i * sizeof(EventoLog), e);
    wdt_reset();
    if (e.secuencia == 0xFFFF) continue;
    imprimirEvento(e, false);
  }
  Serial.println(F("LOG_FIN"));
}

// 'B': borra el registro (deja todas las posiciones vacías).
void borrarRegistro() {
  EventoLog vacio;
  memset(&vacio, 0xFF, sizeof(vacio));
  for (uint8_t i = 0; i < LOG_SLOTS; i++) {
    EEPROM.put(LOG_BASE + i * sizeof(EventoLog), vacio);
    wdt_reset();
  }
  secuenciaLog = 0;
  slotLog      = LOG_SLOTS - 1;
  Serial.println(F("LOG_BORRADO"));
}
#endif

void registrarEvento(uint8_t mot, float p) {
  EventoLog e;
  e.t_s = millis() / 1000UL;
  secuenciaLog++;
  if (secuenciaLog == 0xFFFF) secuenciaLog = 0;   // 0xFFFF queda reservado para "vacío"
  e.secuencia = secuenciaLog;
  e.p_x10     = (int16_t)(p * 10.0 + (p >= 0 ? 0.5 : -0.5));
  e.estado    = (uint8_t)estado;
  e.motivo    = mot;
  e.intentos  = intentosFallidos;
  slotLog = (slotLog + 1) % LOG_SLOTS;
  EEPROM.put(LOG_BASE + slotLog * sizeof(EventoLog), e);
#ifdef MODO_DEPURACION
  imprimirEvento(e, true);
#endif
}

// ================= FUNCIONES DE HARDWARE Y LECTURA =================
void apagarBomba() {
  digitalWrite(PIN_TRIAC_MOC, HIGH); // HIGH apaga el MOC3052
  bombaEncendida = false;
}

// Todo encendido físico desde estado apagado registra la hora del arranque (anti-ciclado)
void encenderBomba(unsigned long ahora) {
  if (!bombaEncendida) {
    t_ultimoArranque = ahora;
    bombaEncendida   = true;
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
  modoBombeo          = MODO_SUBIENDO;
  pMeseta             = -1.0;           // Cada arranque parte sin meseta conocida
  pVentanaPendiente   = presionActual;
  t_ventanaPendiente  = ahora;
  subidaCebado        = false;
  t_inicioEstado      = ahora;
  t_condicion         = ahora;
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
  Serial.println(F("Tanque LLENO: usar este promedio como P_ESTATICA_MAX"));
  Serial.print(F("Sugerida precarga pulmon = ")); Serial.println(prom + 0.4, 2);
  Serial.print(F("RES,E,prom=")); Serial.print(prom, 2);
  Serial.print(F(",min=")); Serial.print(pMin, 2);
  Serial.print(F(",max=")); Serial.print(pMax, 2);
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
  motivo      = M_CALIBRACION;
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

// 'L': P_LLENADO_REAL. Flotador abierto, grifos cerrados. Sugiere P_ARRANQUE (llenado - 1.0 PSI).
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
  Serial.print(F("RES,L,prom=")); Serial.print(prom, 2);
  Serial.print(F(",min=")); Serial.print(pMin, 2);
  Serial.print(F(",max=")); Serial.print(pMax, 2);
  Serial.print(F(",p_arranque=")); Serial.println(prom - 1.0, 2);
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
    if (base > P_ARRANQUE && p <= P_ARRANQUE) cruzo = true;
    delay(20);
  }
  calFin();
  float caida15 = base - p15;   // Desde la presión al apagar (mismo criterio que el control)
  float caida30 = base - p;     // El pico solo se informa: muestra si hubo golpe de ariete
  bool confirma = (base - p15 >= CAIDA_MIN_FLUJO_ACTIVO) || (base > P_ARRANQUE && p15 <= P_ARRANQUE);
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
    case 'R': case 'r':
      intentosFallidos = 0;
      calFin();
      Serial.println(F("R: intentos fallidos en 0 (se guarda en EEPROM)"));
      break;
    case '?': Serial.println(F("Comandos: E L F S C X R ?")); break;
    default: break;
  }
}
#endif

#if defined(MODO_CALIBRACION) || defined(MODO_DEPURACION)
// Comandos por el puerto serie: D y B (depuración) y los de calibración.
void procesarComando(char c) {
#ifdef MODO_DEPURACION
  if (c == 'D' || c == 'd') { descargarRegistro(); return; }
  if (c == 'B' || c == 'b') { borrarRegistro(); return; }
#endif
#ifdef MODO_CALIBRACION
  procesarComandoCal(c);
#endif
}
#endif

void setup() {
  // 1. Apagado inmediato en frío antes de habilitar salida
  digitalWrite(PIN_TRIAC_MOC, HIGH);
  pinMode(PIN_TRIAC_MOC, OUTPUT);
  pinMode(PIN_LED_ESTADO, OUTPUT);

  // Configurar explícitamente AVCC como referencia ratiométrica del ADC
  analogReference(DEFAULT);

#if defined(MODO_CALIBRACION) || defined(MODO_DEPURACION)
  Serial.begin(9600);
#endif

  // 2. Sembrar y estabilizar el filtro EMA con lecturas reales del transductor
  for (uint8_t i = 0; i < 16; i++) {
    actualizarLecturaSensor();
  }

  unsigned long ahora = millis();
  t_inicioEstado       = ahora;
  t_condicion          = ahora;
  t_ultimaVerificacion = ahora;
  t_fallaSensor        = ahora;

  // 3. Configurar t_ultimoArranque para forzar un retardo de seguridad de 15 segundos
  // tras cualquier encendido o microcorte de energía eléctrica.
  t_ultimoArranque = ahora - (T_MIN_ENTRE_ARRANQUES - T_RETARDO_ENCENDIDO);

  // 4. Recuperar la escalada de esperas sin flujo guardada antes del corte de luz
  RegistroPersistente reg;
  if (leerPersistencia(reg)) {
    secuenciaEeprom   = reg.secuencia;
    intentosFallidos  = (reg.intentos > 10) ? 10 : reg.intentos;
    intentosGuardados = intentosFallidos;
    if (intentosFallidos > 0) {
      estado = ESPERA_SIN_FLUJO;   // Repite la espera del nivel de intentos alcanzado
    }
  }

  // 5. Registro de eventos: continúa la numeración y anota el encendido
  iniciarRegistroEventos();
  estadoRegistrado = estado;
  registrarEvento(M_ENCENDIDO, presionFiltrada);

  // 6. Habilitar Watchdog a 2 segundos
  wdt_enable(WDTO_2S);
}

void loop() {
  wdt_reset();

  float P2 = actualizarLecturaSensor();
  unsigned long ahora = millis();

#ifdef MODO_CALIBRACION
  if (ahora - t_reporteSerial >= 1000UL) {
    t_reporteSerial = ahora;
    Serial.print(F("P2_PSI:"));
    Serial.print(P2, 2);
    Serial.print(F(" | Estado:"));
    Serial.print((int)estado);
    Serial.print(F(" | Modo:"));
    Serial.print(modoBombeo == MODO_LLENANDO ? F("LLENANDO")
                 : modoBombeo == MODO_HIDRONEUMATICO ? F("HIDRONEUMATICO") : F("SUBIENDO"));
    Serial.print(F(" | Meseta:"));
    Serial.print(pMeseta, 1);
    Serial.print(F(" | Estanc:"));
    Serial.println(pEstancamiento, 1);
  }
#endif
#ifdef MODO_DEPURACION
  // Telemetría: una línea por segundo (tiempo, estado, P2, bomba, modo, meseta, intentos)
  if (ahora - t_telemetria >= 1000UL) {
    t_telemetria = ahora;
    Serial.print(F("TL,"));
    Serial.print(ahora / 1000UL);         Serial.print(',');
    Serial.print((int)estado);            Serial.print(',');
    Serial.print(P2, 2);                  Serial.print(',');
    Serial.print(bombaEncendida ? 1 : 0); Serial.print(',');
    Serial.print(modoBombeo);             Serial.print(',');
    Serial.print(pMeseta, 1);             Serial.print(',');
    Serial.println(intentosFallidos);
  }
#endif
#if defined(MODO_CALIBRACION) || defined(MODO_DEPURACION)
  if (Serial.available()) {
    char c = Serial.read();
    procesarComando(c);
  }
#endif

  // ================= SUPERVISIÓN GLOBAL DE CABLE SUELTO / SENSOR ROTO =================
  if (!sensorValido) {
    if (ahora - t_fallaSensor >= 1500UL && estado != FALLA_SENSOR) {
      apagarBomba();
      t_inicioEstado = ahora;
      motivo = M_FALLA_SENSOR;
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

      // Exige 180s desde el último arranque
      bool ventanaArranqueLista = (ahora - t_ultimoArranque >= T_MIN_ENTRE_ARRANQUES);

      if (P2 <= P_ARRANQUE && ventanaArranqueLista) {
        if (ahora - t_condicion >= T_DEMANDA) {
          motivo = M_DEMANDA;
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

      // Umbral: +DELTA_P_CEBADO sobre la base, y siempre por encima de la estática máxima + margen
      // (descarta que la subida sea el pulmón recargándose por gravedad desde el tanque).
      float umbralCebadoDinamico = presionBaseArranque + DELTA_P_CEBADO;
      if (umbralCebadoDinamico < P_UMBRAL_FLUJO) {
        umbralCebadoDinamico = P_UMBRAL_FLUJO;
      }
      if (umbralCebadoDinamico > PISO_FLUJO_TOTAL) {
        umbralCebadoDinamico = PISO_FLUJO_TOTAL;   // Nunca exigir más que el piso de flujo parcial
      }

      // 1. Si cargó directo el pulmón a 20 PSI (tanque lleno y sin consumo en casa): corta al instante
      if (P2 >= P_MAX_OFF) {
        apagarBomba();
        motivo           = M_CORTE_20;
        intentosFallidos = 0;
        t_condicion      = ahora;
        estado           = REPOSO_PRESURIZADO;
        break;
      }

      // 2. Cebado confirmado = P2 subió de forma sostenida (pendiente >= PENDIENTE_MIN_SUBIDA en alguna
      //    ventana de 1 s) Y superó el umbral dinámico durante 1.0 s. Una P2 plana, aunque esté alta
      //    (pulmón retenido por el Check #1), no confirma flujo.
      if (ahora - t_ventanaPendiente >= T_VENTANA_PENDIENTE) {
        float pendiente = (P2 - pVentanaPendiente) * 1000.0 / (float)(ahora - t_ventanaPendiente);
        if (pendiente >= PENDIENTE_MIN_SUBIDA) {
          subidaCebado = true;
        }
        pVentanaPendiente  = P2;
        t_ventanaPendiente = ahora;
      }
      bool cebadoOk = subidaCebado && (P2 >= umbralCebadoDinamico);
      if (cebadoOk) {
        if (ahora - t_condicion >= T_CONFIRMA_CEBADO) {
          intentosFallidos       = 0; // Éxito de cebado: reinicia el escalonamiento de espera sin agua
          verificacionesExitosas = 0; // Inicia el intervalo adaptativo en 90s
          pRefVerificacion       = P2;
          t_inicioEstado         = ahora;
          t_ultimaVerificacion   = ahora;
          t_sinFlujo             = ahora;
          flujoSobreUmbral       = false;
          t_estancamiento        = ahora;
          pMaxEstanc             = P2;
          motivo                 = M_FLUJO_CONFIRMADO;
          estado                 = BOMBEO_ACTIVO;
          break;
        }
      } else {
        t_condicion = ahora;
      }

      // 3. Guardián de tiempo independiente (4.0s sin Delta P, o 6.5s absolutos):
      bool sinIncrementoDeCebado  = !cebadoOk && (ahora - t_inicioEstado >= T_PRUEBA_CEBADO);
      bool tiempoAbsolutoExcedido = (ahora - t_inicioEstado >= T_MAX_SONDEO_ABS);

      if (sinIncrementoDeCebado || tiempoAbsolutoExcedido) {
        apagarBomba();
        motivo = M_SIN_SUBIDA;
        if (intentosFallidos < 10) intentosFallidos++;
        t_inicioEstado = ahora;
        estado         = ESPERA_SIN_FLUJO;
      }
      break;
    }

    // ------------------------------------------------------------------
    // ESTADO 3: BOMBEO NORMAL CON VERIFICACIÓN ADAPTATIVA EN TODA LA FRANJA
    // ------------------------------------------------------------------
    case BOMBEO_ACTIVO: {
      encenderBomba(ahora);
      digitalWrite(PIN_LED_ESTADO, HIGH);

      // --- A. Corte por pulmón cargado: apaga al instante en que P2 llega a P_MAX_OFF ---
      //     Sin espera de confirmación: con 2 s de espera la bomba seguía empujando contra el flotador
      //     cerrado y la presión pasaba de 20 PSI (sobrepico). El ruido ya lo filtran el promedio de 64
      //     lecturas y el filtro EMA del sensor.
      if (P2 >= P_MAX_OFF) {
        apagarBomba();
        motivo           = M_CORTE_20;
        intentosFallidos = 0;
        t_condicion      = ahora;
        estado           = REPOSO_PRESURIZADO;
        break;
      }

      // --- B. SIN FLUJO (incluye bomba en vacío, calle seca o pérdida de cebado) ---
      //     Criterio del usuario: si con la bomba encendida la presión desciende hasta la estática (alguien
      //     abrió una válvula o se agota la calle), es sin flujo apenas la toca (P2 <= P_UMBRAL_FLUJO 1 s).
      //     Tras arrancar o reanudar (la micro-pausa deja P2 cerca de la estática) se dan hasta 10 s para que
      //     P2 supere el umbral; si no lo supera en ese plazo, también es sin flujo.
      if (P2 > P_UMBRAL_FLUJO) {
        flujoSobreUmbral = true;
      }
      unsigned long plazoSinFlujo = flujoSobreUmbral ? T_SIN_FLUJO : T_SIN_FLUJO_REARRANQUE;
      if (P2 <= P_UMBRAL_FLUJO) {
        if (ahora - t_sinFlujo >= plazoSinFlujo) {
          apagarBomba();
          motivo = M_SIN_FLUJO_NIVEL;
          if (intentosFallidos < 10) intentosFallidos++;
          t_inicioEstado = ahora;
          estado         = ESPERA_SIN_FLUJO;
          break;
        }
      } else {
        t_sinFlujo = ahora;
      }

      // --- B2. ESTANCAMIENTO Y MESETAS: micro-pausa de prueba en cada estancamiento en un nivel nuevo ---
      if (P2 > pMaxEstanc + SUBIDA_MIN_ESTANCAMIENTO) {
        pMaxEstanc      = P2;         // Sigue subiendo (aunque sea lento)
        t_estancamiento = ahora;
      }
      bool nivelNuevo    = (pMeseta < 0.0) || (P2 > pMeseta + MARGEN_NUEVO_NIVEL);
      if (pMeseta < 0.0) {
        modoBombeo = MODO_SUBIENDO;
      } else {
        modoBombeo = (P2 > pMeseta + MARGEN_NUEVO_NIVEL) ? MODO_HIDRONEUMATICO : MODO_LLENANDO;
      }
      if (ahora - t_estancamiento >= T_ESTANCAMIENTO) {
        if (P2 >= P_LLENO_ESTANCADO) {
          // Estancada entre 18 y 20 PSI: tanque lleno (sin micro-pausa)
          apagarBomba();
          motivo           = M_LLENO_ESTANCADO;
          intentosFallidos = 0;
          t_condicion      = ahora;
          estado           = REPOSO_PRESURIZADO;
          break;
        }
        if (nivelNuevo) {
          // Aceleración 0 en un nivel nuevo: ¿sigue llenando, terminó de llenar o la bomba no da más?
          motivo                 = M_PRUEBA_NIVEL_NUEVO;
          pInicioPausa      = P2;
          flujoConfirmadoEnPausa = false;
          apagarBomba();
          t_inicioPausaVerif     = ahora;
          estado                 = VERIFICANDO_FLUJO;
          break;
        }
        t_estancamiento = ahora;      // Misma meseta: llenado normal (la micro-pausa periódica sigue vigente)
        pMaxEstanc      = P2;
      }

      // --- C. Verificación Hidrodinámica Adaptativa (90s -> 180s -> 300s): SOLO DURANTE EL LLENADO ---
      //     (criterio del usuario). Llenando = meseta confirmada y P2 en ella. Fuera del llenado
      //     (subida inicial o hidroneumático) el intervalo no corre: lo cubren el estancamiento y el corte.
      bool enLlenado = (modoBombeo == MODO_LLENANDO);
      if (!enLlenado) {
        t_ultimaVerificacion = ahora;
        pRefVerificacion     = P2;
      }
      unsigned long intervaloActual = obtenerIntervaloVerificacionAdaptativo();
      if (enLlenado && (ahora - t_ultimaVerificacion >= intervaloActual)) {
        // Solo omite la micro-pausa si la presión demostró estar subiendo activamente (+0.75 PSI)
        if (P2 - pRefVerificacion >= AVANCE_OMITE_PAUSA) {
          pRefVerificacion     = P2;
          t_ultimaVerificacion = ahora;
        }
        // Si la presión lleva todo el intervalo estacionada (en cualquier valor entre 9.5 y 19.9 PSI),
        // hace la micro-pausa: si estaba llenando el tanque caerá <= P_ARRANQUE (9.5 PSI) o caerá >= 0.8 PSI;
        // si estaba atrapada por el Check #1 sin agua en la calle, quedará fija y cortará en 3 segundos.
        else {
          motivo                 = M_PAUSA_PERIODICA;
          pInicioPausa      = P2;
          flujoConfirmadoEnPausa = false;
          apagarBomba();
          t_inicioPausaVerif     = ahora;
          estado                 = VERIFICANDO_FLUJO;
          break;
        }
      }

      // --- D. Tiempo máximo de bombeo (45 min) sin llegar a lleno: posible fuga o grifo abierto ---
      //     Apaga y espera 20 min (nivel de intentos >= 3) antes de volver a verificar flujo.
      if (ahora - t_inicioEstado >= T_MAX_BOMBEO) {
        apagarBomba();
        motivo = M_TIEMPO_MAXIMO;
        if (intentosFallidos < 3) intentosFallidos = 3;
        t_inicioEstado = ahora;
        estado         = ESPERA_SIN_FLUJO;
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

      // 2. Si durante la pausa la presión alcanza el corte de 20 PSI, el tanque ya se llenó
      if (P2 >= P_MAX_OFF) {
        motivo           = M_CORTE_20;
        intentosFallidos = 0;
        t_condicion      = ahora;
        estado           = REPOSO_PRESURIZADO;
        break;
      }

      // 3. Evaluar si ocurrió caída de presión que demuestra flujo hacia el tanque o la casa:
      //    - Cae >= 0.80 PSI desde la presión en el instante de apagar (pInicioPausa).
      //    - O cruza <= P_ARRANQUE rumbo a la estática (llenado de baja fricción).
      //    El cruce de P_ARRANQUE solo cuenta si P2 estaba POR ENCIMA al iniciar la pausa: si ya estaba
      //    en la estática (sin flujo, pulmón drenado hacia el tanque) no hay caída que confirme flujo.
      //    Se mide desde la presión al apagar (no desde un pico) y se ignoran los primeros 0.5 s, para que
      //    un repunte por golpe de ariete que luego se disipa no se confunda con flujo al tanque.
      if (tiempoEnPausa >= T_IGNORAR_INICIO_PAUSA) {
        float caida        = pInicioPausa - P2;
        bool cruzoArranque = (pInicioPausa > P_ARRANQUE) && (P2 <= P_ARRANQUE);
        if (caida >= CAIDA_MIN_FLUJO_ACTIVO || cruzoArranque) {
          flujoConfirmadoEnPausa = true;
        }
      }

      // 4. Si se confirmó el flujo PERO garantizando al menos 1.5s de pausa efectiva (T_MIN_PAUSA_EFECTIVA)
      //    para que el rotor no sufra reconexión instantánea ni agote los arranques por hora:
      if (flujoConfirmadoEnPausa && (tiempoEnPausa >= T_MIN_PAUSA_EFECTIVA)) {
        // P2 cayó: el agua sigue yendo al tanque. Ese nivel es la meseta de llenado confirmada.
        pMeseta            = pInicioPausa;
        modoBombeo         = MODO_LLENANDO;
        if (verificacionesExitosas < 10) verificacionesExitosas++;
        pRefVerificacion     = P2;
        t_ultimaVerificacion = ahora;
        t_sinFlujo           = ahora;
        flujoSobreUmbral     = false;
        t_estancamiento      = ahora;
        pMaxEstanc           = P2;
        motivo               = M_PAUSA_CON_CAIDA;
        encenderBomba(ahora); // Reanuda el bombeo
        estado               = BOMBEO_ACTIVO;
        break;
      }

      // 5. Al cumplirse los 3.0 segundos sin caída apreciable: la red está estanca (flotador cerrado).
      //    (Vale igual para la micro-pausa periódica y para la prueba por estancamiento en nivel nuevo.)
      //    Sobre PISO_FLUJO_TOTAL la presión alcanzada es la presión de estancamiento (bomba de poca
      //    potencia: REPOSO, re-arranca en P_ARRANQUE); bajo el piso, sin flujo.
      if (tiempoEnPausa >= T_PAUSA_VERIFICACION) {
        if (P2 >= PISO_FLUJO_TOTAL) {
          pEstancamiento   = P2;
          motivo           = M_ESTANCAMIENTO;
          intentosFallidos = 0;
          t_condicion      = ahora;
          estado           = REPOSO_PRESURIZADO;
        } else {
          if (intentosFallidos < 10) intentosFallidos++;
          t_inicioEstado = ahora;
          motivo         = M_SIN_FLUJO_PAUSA;
          estado         = ESPERA_SIN_FLUJO;
        }
      }
      break;
    }

    // ------------------------------------------------------------------
    // ESTADO 5: SIN FLUJO (ESPERA ESCALONADA: 5/10/20 MIN)
    // Nota de diseño: intentosFallidos NO se reinicia al salir de la espera; permanece en >=3
    // (esperas de 20 min) durante cortes largos de suministro hasta que un ciclo confirme flujo real.
    // ------------------------------------------------------------------
    case ESPERA_SIN_FLUJO: {
      apagarBomba();
      // Parpadeo rápido (2 Hz) indicando sin flujo
      digitalWrite(PIN_LED_ESTADO, (ahora / 250UL) % 2);

      unsigned long tiempoEsperaActual = T_ESPERA_3; // 20 min (para intentosFallidos >= 3)
      if (intentosFallidos == 1) {
        tiempoEsperaActual = T_ESPERA_1;             // 5 min
      } else if (intentosFallidos == 2) {
        tiempoEsperaActual = T_ESPERA_2;             // 10 min
      }

      bool ventanaArranqueLista = (ahora - t_ultimoArranque >= T_MIN_ENTRE_ARRANQUES);

      if ((ahora - t_inicioEstado >= tiempoEsperaActual) && ventanaArranqueLista) {
        if (P2 > P_ARRANQUE) {
          // Aún hay presión en el pulmón: REPOSO arrancará al bajar a P_ARRANQUE. Los intentos se
          // conservan; si la siguiente verificación falla, la escalada de esperas continúa.
          motivo           = M_FIN_ESPERA_REPOSO;
          t_condicion      = ahora;
          estado           = REPOSO_PRESURIZADO;
        } else {
          motivo = M_REINTENTO;
          iniciarSondeoCalle(ahora, P2);
        }
      }
      break;
    }

    // ------------------------------------------------------------------
    // ESTADO 6: FALLA ELÉCTRICA DEL SENSOR (CABLE SUELTO O CORTOCIRCUITO)
    // ------------------------------------------------------------------
    case FALLA_SENSOR: {
      apagarBomba();
      // Destello muy rápido (10 Hz) indicando cable de sensor suelto o en corto
      digitalWrite(PIN_LED_ESTADO, (ahora / 50UL) % 2);

      // Si el sensor vuelve a dar lectura válida (45 a 982 cuentas) por 3 segundos, se recupera solo.
      // Si había intentos fallidos pendientes, vuelve a la espera sin flujo (no se salta la escalada).
      if (sensorValido) {
        if (ahora - t_inicioEstado >= 3000UL) {
          t_condicion = ahora;
          motivo      = M_SENSOR_RECUPERADO;
          if (intentosFallidos > 0) {
            t_inicioEstado = ahora;
            estado         = ESPERA_SIN_FLUJO;
          } else {
            estado         = REPOSO_PRESURIZADO;
          }
        }
      } else {
        t_inicioEstado = ahora;
      }
      break;
    }
  }

  // Un ciclo que termina arriba (corte a 20 PSI, lleno por estancamiento o presión de estancamiento)
  // terminó en hidroneumático, aunque nunca se haya confirmado una meseta de llenado.
  if (estado == REPOSO_PRESURIZADO &&
      (motivo == M_CORTE_20 || motivo == M_LLENO_ESTANCADO || motivo == M_ESTANCAMIENTO)) {
    modoBombeo = MODO_HIDRONEUMATICO;
  }

  // Registro de eventos: cada cambio de estado con su motivo (EEPROM; por USB en MODO_DEPURACION)
  if (estado != estadoRegistrado) {
    registrarEvento(motivo, P2);
    estadoRegistrado = estado;
    motivo           = M_NINGUNO;
  }

  // Persistencia: guarda los intentos fallidos si cambiaron (sobrevive a cortes de luz)
  guardarPersistenciaSiCambio();
}
