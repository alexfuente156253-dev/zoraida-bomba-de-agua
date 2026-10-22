// Sustituto minimo de la API de Arduino para compilar el firmware real en el PC.
// Solo para el simulador (sim/sim.cpp). No usar en el firmware.
#pragma once
#include <stdint.h>
#include <stddef.h>

#define HIGH 1
#define LOW 0
#define OUTPUT 1
#define INPUT 0
#define DEFAULT 1
#define A1 15
#define F(x) (x)

// Implementados por el simulador (sim.cpp)
extern unsigned long sim_millis;
int  sim_adc_read(int pin);
void sim_digital_write(int pin, int val);

inline unsigned long millis() { return sim_millis; }
inline int  analogRead(int pin) { return sim_adc_read(pin); }
inline void digitalWrite(int pin, int val) { sim_digital_write(pin, val); }
inline void pinMode(int, int) {}
inline void analogReference(int) {}
inline void delay(unsigned long ms) { sim_millis += ms; }
