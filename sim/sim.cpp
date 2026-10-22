// Simulador en lazo cerrado: firmware REAL (.ino compilado en el PC) + modelo hidraulico simple.
// Uso:  sim <escenario> [--quiet] [--csv archivo] [--p-street psi] [--p-pre psi] [--v-pulmon L] [--h0 psi]
//       sim            (lista los escenarios)
// Los parametros hidraulicos son SUPUESTOS (ver sim/README.md): sirven para probar la LOGICA
// de control, no para calibrar valores. Un resultado FALLA indica un caso a verificar en campo.
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <random>
#include <string>
#include <vector>

#include <Arduino.h>
#include <avr/io.h>
#include <avr/wdt.h>

// ---------------------------------------------------------------- ganchos de hardware
unsigned long sim_millis = 0;
static int    g_pin_triac   = HIGH;     // HIGH = bomba apagada (logica inversa del MOC3052)
static int    g_adc_force   = -1;       // >=0 fuerza la lectura del ADC (falla de sensor)
static double g_psi_sensor  = 0.0;      // presion real en el nodo del sensor
static double g_adc_noise   = 1.0;      // ruido del ADC, cuentas (1 sigma, por muestra)
static std::mt19937 g_rng(12345);
static std::normal_distribution<double> g_norm(0.0, 1.0);

int sim_adc_read(int) {
  if (g_adc_force >= 0) return g_adc_force;
  double counts = 102.3 + g_psi_sensor * (818.4 / 200.0) + g_norm(g_rng) * g_adc_noise;
  long v = std::lround(counts);
  return (int)std::min(1023L, std::max(0L, v));
}
void sim_digital_write(int pin, int val) {
  if (pin == 7) g_pin_triac = val;
}

// ---------------------------------------------------------------- firmware real
#define __attribute__(x)
#include "../control_bomba_b1_v10_3/control_bomba_b1_v10_3.ino"
#undef __attribute__

// ---------------------------------------------------------------- modelo hidraulico
static const double PI_       = 3.14159265358979;
static const double PSI_PER_M = 1.4223;   // psi por metro de columna de agua
static const double P_ATM     = 14.7;

static double orif(double dP, double R) {          // caudal L/s por una resistencia cuadratica
  if (dP <= 0.0) return 0.0;
  return dP / std::sqrt(R * (dP + 0.05));          // regularizado cerca de dP = 0
}

struct Hyd {
  // --- SUPUESTOS (ajustables) ---
  double P_street = 6.0;    // psi, presion de la calle en la succion
  double H0       = 25.0;   // psi, altura de cierre (shutoff) que suma la bomba
  double a_pump   = 83.0;   // psi/(L/s)^2, caida de altura de la bomba con el caudal
  double R_pass   = 150.0;  // resistencia al paso por la bomba apagada (calle -> pulmon)
  double P_base   = 4.97;   // psi en el sensor con el tanque a 0 m (tanque lleno = 7.1 psi)
  double tank_H   = 1.5;    // m
  double tank_D   = 0.8;    // m
  double R_fill   = 12.4;   // psi/(L/s)^2, friccion de la subida al tanque
  double R_chk2   = 5.0;    // psi/(L/s)^2, Check #2
  double V_tot    = 6.0;    // L, volumen del pulmon (SUPUESTO; probar --v-pulmon)
  double P_pre    = 5.4;    // psi, precarga de aire (manometrica)
  // --- estado ---
  double h = 0.75, Vw = 0.0;
  bool   street_ok = true;
  double q_house_cmd = 0.0;
  // --- salidas del ultimo paso ---
  double P = 0.0, q_pump = 0.0, q_fill = 0.0, q_chk2 = 0.0, q_house = 0.0;

  double ptank(double hh) const { return P_base + PSI_PER_M * hh; }
  double pressureFromVw(double v) const {
    if (v >= 0.0) return (P_pre + P_ATM) * V_tot / (V_tot - v) - P_ATM;
    return P_pre + 10.0 * v;                       // pulmon sin agua: linea rigida
  }
  void setPressure(double P0) {
    Vw = (P0 >= P_pre) ? V_tot * (1.0 - (P_pre + P_ATM) / (P0 + P_ATM)) : (P0 - P_pre) / 10.0;
    P = pressureFromVw(Vw);
  }
  void step(double dt, bool pump_on) {
    P = pressureFromVw(Vw);
    double Ptop = ptank(tank_H);
    bool float_open = h < tank_H - 0.01;
    q_fill = float_open ? orif(P - Ptop, R_fill) : 0.0;
    q_chk2 = (h > 0.03) ? orif(ptank(h) - P, R_chk2) : 0.0;
    q_pump = 0.0;
    if (street_ok) {
      if (pump_on) {
        double avail = P_street + H0 - P;
        q_pump = avail > 0.0 ? std::sqrt(avail / a_pump) : 0.0;
      } else {
        q_pump = orif(P_street - P, R_pass);
      }
    }
    q_house = q_house_cmd * std::min(1.0, std::max(0.0, P) / 2.0);
    Vw += (q_pump + q_chk2 - q_fill - q_house) * dt;
    Vw = std::min(V_tot - 0.5, std::max(-0.8, Vw));
    double area_L_per_m = PI_ * (tank_D / 2) * (tank_D / 2) * 1000.0;
    h += (q_fill - q_chk2) * dt / area_L_per_m;
    h = std::min(tank_H, std::max(0.0, h));
    P = pressureFromVw(Vw);
  }
};

// ---------------------------------------------------------------- escenarios y resultados
static const char* NOMBRE_ESTADO[] = {"REPOSO", "SONDEO", "BOMBEO", "VERIFICANDO",
                                      "ESPERA_SIN_FLUJO", "ENFRIAMIENTO", "FALLA_SENSOR"};
enum { S_REPOSO, S_SONDEO, S_BOMBEO, S_VERIF, S_ESPERA, S_ENFRIA, S_FALLA };

struct Ev { double t; int from, to; double P, h, dry; int intentos, arr; };
struct Res {
  std::vector<Ev> ev;
  std::vector<std::pair<double, bool>> pump;   // (t, encendida)
  double pump_on_s = 0, dry_s = 0, maxP = 0, minP = 1e9, t_full = -1, end = 0;
  int final_state = 0, final_int = 0, final_arr = 0;
  double final_h = 0, final_P = 0;
};

struct Scn {
  const char* name;
  const char* desc;
  double duration, h0, P0;
  std::function<bool(double)>   street;
  std::function<double(double)> house;
  std::function<int(double)>    adc;
  double tankD = 0.8;       // m, diametro del tanque
  double H0psi = -1;        // >0 sustituye la altura de cierre de la bomba
};

static bool g_ok = true;
static void check(bool c, const char* fmt, ...) {
  char buf[256];
  va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
  printf("  [%s] %s\n", c ? "OK   " : "FALLA", buf);
  if (!c) g_ok = false;
}
static void info(const char* fmt, ...) {
  char buf[256];
  va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
  printf("  [INFO ] %s\n", buf);
}
static std::string hms(double t) {
  char b[32]; int s = (int)t;
  snprintf(b, sizeof b, "%02d:%02d:%02d", s / 3600, (s / 60) % 60, s % 60);
  return b;
}
static std::vector<size_t> idxTo(const Res& r, int s) {
  std::vector<size_t> v;
  for (size_t i = 0; i < r.ev.size(); i++) if (r.ev[i].to == s) v.push_back(i);
  return v;
}
static double durAfter(const Res& r, size_t i) {
  return (i + 1 < r.ev.size() ? r.ev[i + 1].t : r.end) - r.ev[i].t;
}
static int startsInWindow(const Res& r, double w) {       // maximo de arranques en cualquier ventana w
  std::vector<double> st;
  for (auto& p : r.pump) if (p.second) st.push_back(p.first);
  int best = 0;
  for (size_t i = 0; i < st.size(); i++) {
    int n = 0;
    for (size_t j = i; j < st.size() && st[j] < st[i] + w; j++) n++;
    best = std::max(best, n);
  }
  return best;
}

static std::vector<Scn> escenarios() {
  auto siempre = [](double) { return true; };
  auto nada    = [](double) { return 0.0; };
  auto sinFalla = [](double) { return -1; };
  std::vector<Scn> v;
  v.push_back({"normal", "Llenado normal: tanque al 50%, calle OK, casa sin consumo (1 h)",
               3600, 0.75, 7.10, siempre, nada, sinFalla});
  v.push_back({"calle_seca", "Calle seca desde el inicio: escalada de esperas 5/10/20 min (3 h)",
               10800, 0.75, 7.10, [](double) { return false; }, nada, sinFalla});
  v.push_back({"sin_flujo", "La calle se corta a los 300 s en pleno llenado: la bomba queda sin flujo (50 min)",
               3000, 0.40, 7.10, [](double t) { return t < 300.0; }, nada, sinFalla});
  v.push_back({"consumo_normal", "Tanque lleno, pulmon a 20 psi, la casa consume 0.25 L/s entre 60 y 300 s",
               900, 1.5, 20.0, siempre, [](double t) { return (t >= 60 && t < 300) ? 0.25 : 0.0; }, sinFalla});
  v.push_back({"sensor_falla", "Cable del sensor suelto entre 200 y 230 s durante el llenado (1 h)",
               3600, 0.50, 7.10, siempre, nada, [](double t) { return (t >= 200 && t < 230) ? 5 : -1; }});
  v.push_back({"consumo_alto", "Demanda alta continua (0.40 L/s = 24 L/min) con la calle OK y el tanque al 33% (70 min)",
               4200, 0.50, 7.10, siempre, [](double) { return 0.40; }, sinFalla});
  v.push_back({"limite_45min", "Tanque grande (D=1.6 m) que no alcanza a llenarse en 45 min: enfriamiento de 20 min (80 min)",
               4800, 0.05, 7.10, siempre, nada, sinFalla, 1.6});
  v.push_back({"ciclado_corto", "Tanque lleno, rafagas de consumo cada 200 s: limite de arranques (2 h)",
               7200, 1.5, 20.0, siempre,
               [](double t) { return (t >= 30 && fmod(t - 30, 200.0) < 40.0) ? 0.30 : 0.0; }, sinFalla});
  v.push_back({"base_baja", "Arranque con el pulmon en la estatica del tanque casi vacio (5.5 psi), calle OK (10 min)",
               600, 0.40, 5.54, siempre, nada, sinFalla});
  v.push_back({"pulmon_retenido", "Calle seca con el pulmon retenido a 9 psi por el Check #1 y tanque lleno: P2 plana, sin subida (15 min)",
               900, 1.5, 9.0, [](double) { return false; }, nada, sinFalla});
  v.push_back({"bomba_debil", "Llenado normal con la bomba degradada (cierre 25 psi con calle de 6 psi) (1 h)",
               3600, 0.75, 7.10, siempre, nada, sinFalla, 0.8, 19.0});
  v.push_back({"calle_vuelve", "Calle seca los primeros 30 min y luego regresa (2 h)",
               7200, 0.50, 7.10, [](double t) { return t >= 1800.0; }, nada, sinFalla});
  return v;
}

static void evaluar(const std::string& n, const Res& r) {
  printf("\nVerificaciones:\n");
  auto esp = idxTo(r, S_ESPERA);
  if (n == "normal" || n == "bomba_debil") {
    check(r.final_h >= 1.49, "tanque lleno al final (h=%.2f m, lleno en %s)", r.final_h, r.t_full >= 0 ? hms(r.t_full).c_str() : "nunca");
    check(r.final_state == S_REPOSO, "termina en REPOSO (estado=%s)", NOMBRE_ESTADO[r.final_state]);
    check(idxTo(r, S_ENFRIA).empty() && idxTo(r, S_FALLA).empty(), "sin enfriamientos ni fallas espurias");
    check(startsInWindow(r, 3600) <= 18, "arranques en la hora: %d (<=18)", startsInWindow(r, 3600));
    info("esperas 'sin agua' con calle OK: %zu (el sondeo de 4 s exige llegar a P_ARRANQUE; depende del volumen del pulmon, ver --v-pulmon)", esp.size());
    info("presion maxima %.1f psi: el corte a 20 psi exige 2 s sostenidos; el sobrepico depende de la altura de cierre de la bomba y del pulmon", r.maxP);
  } else if (n == "calle_seca") {
    check(esp.size() >= 4, "al menos 4 esperas (hubo %zu)", esp.size());
    const double exp[4] = {300, 600, 1200, 1200};
    for (size_t i = 0; i < esp.size() && i < 4; i++) {
      double d = durAfter(r, esp[i]);
      check(std::fabs(d - exp[i]) <= 15.0, "espera %zu = %.0f s (esperado %.0f)", i + 1, d, exp[i]);
    }
    double mx = 0;
    for (size_t i : idxTo(r, S_SONDEO)) mx = std::max(mx, durAfter(r, i));
    check(mx <= 7.0, "sondeo mas largo %.1f s (<=7)", mx);
    check(r.minP >= 4.5, "la casa conserva presion por gravedad (minimo %.1f psi)", r.minP);
  } else if (n == "sin_flujo") {
    const double tFail = 300.0;
    double tDet = -1, dryDet = -1;
    for (size_t i : esp) if (r.ev[i].t > tFail) { tDet = r.ev[i].t; dryDet = r.ev[i].dry; break; }
    if (tDet < 0) info("la falta de flujo NO fue detectada en %.0f s", r.end - tFail);
    else info("deteccion %.0f s despues del corte; bomba sin flujo hasta entonces: %.0f s", tDet - tFail, dryDet);
    check(tDet > 0 && (tDet - tFail) <= 600.0, "detectada en <= 600 s tras el corte");
    check(dryDet >= 0 && dryDet <= 600.0, "bomba sin flujo acumulada <= 600 s antes de detectar");
  } else if (n == "consumo_normal") {
    check(!idxTo(r, S_SONDEO).empty(), "la bomba arranca por baja presion (sondeos: %zu)", idxTo(r, S_SONDEO).size());
    check(esp.empty() && idxTo(r, S_ENFRIA).empty() && idxTo(r, S_FALLA).empty(), "sin esperas, enfriamientos ni fallas espurias");
    check(r.final_state == S_REPOSO && r.final_P >= 9.5, "termina en REPOSO con pulmon recargado (P=%.1f psi)", r.final_P);
    check(startsInWindow(r, 3600) <= 18, "arranques: %d (<=18)", startsInWindow(r, 3600));
  } else if (n == "sensor_falla") {
    auto fa = idxTo(r, S_FALLA);
    double tF = fa.empty() ? -1 : r.ev[fa[0]].t;
    check(!fa.empty() && tF - 200.0 <= 2.5, "FALLA_SENSOR a los %.1f s de la falla (<=2.5)", tF - 200.0);
    double tOff = -1;
    for (auto& p : r.pump) if (p.first >= 200.0 && !p.second) { tOff = p.first; break; }
    check(tOff >= 0 && tOff - 200.0 <= 2.5, "bomba apagada a los %.1f s de la falla (<=2.5)", tOff - 200.0);
    double tRec = -1;
    for (auto& e : r.ev) if (e.from == S_FALLA) { tRec = e.t; break; }
    check(tRec >= 232.9 && tRec <= 240.0, "sale de FALLA a los %.1f s (restaurado a 230 s + 3 s)", tRec);
    check(r.final_h >= 1.49, "termina de llenar el tanque tras recuperarse (h=%.2f m)", r.final_h);
  } else if (n == "consumo_alto") {
    check(esp.empty(), "sin falsa alarma de 'calle seca' con la calle OK (esperas: %zu)", esp.size());
    check(r.final_h >= 0.10, "el tanque no se vacia (h final %.2f m)", r.final_h);
    info("presion minima en la casa %.1f psi; el sondeo de 4 s no distingue 'sin agua' de 'demanda que se lleva el caudal'", r.minP);
  } else if (n == "limite_45min") {
    auto pa = idxTo(r, S_ENFRIA);
    check(!pa.empty(), "entra a ENFRIAMIENTO por los 45 min de marcha (%zu veces)", pa.size());
    if (!pa.empty()) {
      double t0 = r.ev[pa[0]].t;
      check(t0 >= 2650 && t0 <= 2800, "primer enfriamiento en %s (esperado ~00:45 tras arrancar)", hms(t0).c_str());
      check(std::fabs(durAfter(r, pa[0]) - 1200.0) <= 5.0, "enfriamiento dura %.0f s (esperado 1200)", durAfter(r, pa[0]));
    }
  } else if (n == "ciclado_corto") {
    // solo arranques desde reposo: se descartan las reanudaciones tras la micro-pausa (apagada < 10 s)
    double minGap = 1e9, last = -1, tOff = -1e9;
    for (auto& p : r.pump) {
      if (!p.second) { tOff = p.first; continue; }
      if (p.first - tOff < 10.0) continue;
      if (last >= 0) minGap = std::min(minGap, p.first - last);
      last = p.first;
    }
    check(minGap >= 179.0, "separacion minima entre arranques desde reposo %.0f s (>=180)", minGap);
    check(startsInWindow(r, 3600) <= 18, "arranques en cualquier hora: %d (<=18)", startsInWindow(r, 3600));
    check(idxTo(r, S_FALLA).empty() && esp.empty(), "sin fallas ni esperas espurias");
  } else if (n == "pulmon_retenido") {
    double mx = 0;
    for (size_t i : idxTo(r, S_SONDEO)) mx = std::max(mx, durAfter(r, i));
    check(!esp.empty(), "declara 'sin agua' (esperas: %zu)", esp.size());
    check(idxTo(r, S_BOMBEO).empty(), "nunca pasa a BOMBEO_ACTIVO con P2 plana");
    check(mx <= 7.0, "sondeo mas largo %.1f s (<=7)", mx);
  } else if (n == "base_baja") {
    auto so = idxTo(r, S_SONDEO);
    bool ok = !so.empty() && r.ev[so[0] + 1 < r.ev.size() ? so[0] + 1 : so[0]].to == S_BOMBEO;
    check(ok, "el primer sondeo con calle OK confirma cebado (llegar de ~5.5 a %.1f psi en <=4 s)", (double)P_ARRANQUE);
    info("esperas 'sin agua' con calle OK: %zu; el sondeo exige subir hasta max(base+1.5, P_ARRANQUE)", esp.size());
  } else if (n == "calle_vuelve") {
    double tB = -1;
    for (size_t i : idxTo(r, S_BOMBEO)) if (r.ev[i].t >= 1800.0) { tB = r.ev[i].t; break; }
    check(tB >= 0 && tB - 1800.0 <= 1500.0, "reanuda el bombeo %.0f s despues de volver la calle (<=1500)", tB - 1800.0);
    check(r.final_h >= 1.49, "tanque lleno al final (h=%.2f m)", r.final_h);
    check(r.final_int == 0, "intentosFallidos vuelve a 0 (=%d)", r.final_int);
  }
}

int main(int argc, char** argv) {
  auto lista = escenarios();
  if (argc < 2) {
    printf("Escenarios:\n");
    for (auto& s : lista) printf("  %-16s %s\n", s.name, s.desc);
    printf("  %-16s %s\n", "todos", "ejecuta todos");
    return 0;
  }
  std::string nombre = argv[1];
  bool quiet = false; std::string csvPath;
  Hyd proto;
  for (int i = 2; i < argc; i++) {
    std::string a = argv[i];
    if (a == "--quiet") quiet = true;
    else if (a == "--csv" && i + 1 < argc) csvPath = argv[++i];
    else if (a == "--p-street" && i + 1 < argc) proto.P_street = atof(argv[++i]);
    else if (a == "--p-pre" && i + 1 < argc) proto.P_pre = atof(argv[++i]);
    else if (a == "--v-pulmon" && i + 1 < argc) proto.V_tot = atof(argv[++i]);
    else if (a == "--h0" && i + 1 < argc) proto.H0 = atof(argv[++i]);
  }
  const Scn* S = nullptr;
  for (auto& s : lista) if (nombre == s.name) S = &s;
  if (!S) { fprintf(stderr, "escenario desconocido: %s\n", nombre.c_str()); return 2; }

  printf("== %s ==\n%s\n", S->name, S->desc);
  printf("Supuestos: calle %.1f psi, pulmon %.0f L precarga %.1f psi, bomba H0=%.0f psi\n",
         proto.P_street, proto.V_tot, proto.P_pre, proto.H0);

  Hyd hyd = proto; hyd.tank_D = S->tankD; if (S->H0psi > 0) hyd.H0 = S->H0psi; hyd.h = S->h0; hyd.setPressure(S->P0);
  FILE* csv = csvPath.empty() ? nullptr : fopen(csvPath.c_str(), "w");
  if (csv) fprintf(csv, "t,estado,bomba,P_real,P_fw,h,q_pump,q_fill,q_chk2,q_house,calle\n");

  g_psi_sensor = hyd.P;
  sim_millis = 0;
  setup();

  Res r; r.end = S->duration;
  const double dtc = 0.010;
  long steps = (long)(S->duration / dtc);
  int prev = (int)estado; bool prevPump = false; double nextCsv = 0.0;
  for (long i = 0; i < steps; i++) {
    double t = i * dtc;
    hyd.street_ok = S->street(t);
    hyd.q_house_cmd = S->house(t);
    g_adc_force = S->adc(t);
    bool pump_on = (g_pin_triac == LOW);
    hyd.step(dtc / 2, pump_on);
    hyd.step(dtc / 2, pump_on);
    g_psi_sensor = hyd.P;
    sim_millis = (unsigned long)std::llround(t * 1000.0) + 10;
    loop();
    pump_on = (g_pin_triac == LOW);

    if (pump_on) { r.pump_on_s += dtc; if (!hyd.street_ok) r.dry_s += dtc; }
    r.maxP = std::max(r.maxP, hyd.P); r.minP = std::min(r.minP, hyd.P);
    if (r.t_full < 0 && hyd.h >= 1.49) r.t_full = t;
    if (pump_on != prevPump) { r.pump.push_back({t, pump_on}); prevPump = pump_on; }
    if ((int)estado != prev) {
      Ev e{t, prev, (int)estado, hyd.P, hyd.h, r.dry_s, (int)intentosFallidos, (int)arranquesEnLaHora};
      r.ev.push_back(e);
      if (!quiet)
        printf("[%s] %-15s -> %-15s | P=%5.2f psi h=%.2f m intentos=%d arranques/h=%d\n", hms(t).c_str(),
               NOMBRE_ESTADO[prev], NOMBRE_ESTADO[(int)estado], hyd.P, hyd.h, e.intentos, e.arr);
      prev = (int)estado;
    }
    if (csv && t >= nextCsv) {
      nextCsv += 1.0;
      fprintf(csv, "%.0f,%s,%d,%.2f,%.2f,%.3f,%.3f,%.3f,%.3f,%.3f,%d\n", t, NOMBRE_ESTADO[(int)estado], pump_on,
              hyd.P, presionFiltrada, hyd.h, hyd.q_pump, hyd.q_fill, hyd.q_chk2, hyd.q_house, hyd.street_ok);
    }
  }
  if (csv) fclose(csv);
  r.final_state = (int)estado; r.final_h = hyd.h; r.final_P = hyd.P;
  r.final_int = (int)intentosFallidos; r.final_arr = (int)arranquesEnLaHora;

  printf("\nResumen: bomba encendida %.0f s (%.0f s sin flujo), P max %.1f psi, P min %.1f psi, h final %.2f m\n",
         r.pump_on_s, r.dry_s, r.maxP, r.minP, r.final_h);
  evaluar(S->name, r);
  printf("\nRESULTADO %s: %s\n", S->name, g_ok ? "PASA" : "FALLA");
  return g_ok ? 0 : 1;
}
