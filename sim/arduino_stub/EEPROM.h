// Sustituto de la EEPROM de Arduino para el simulador (memoria en RAM, inicia borrada en 0xFF).
#pragma once
#include <stdint.h>
#include <string.h>

struct SimEEPROM {
  uint8_t mem[1024];
  SimEEPROM() { memset(mem, 0xFF, sizeof mem); }
  template <typename T> T& get(int addr, T& t) { memcpy(&t, mem + addr, sizeof(T)); return t; }
  template <typename T> const T& put(int addr, const T& t) { memcpy(mem + addr, &t, sizeof(T)); return t; }
  uint16_t length() const { return sizeof mem; }
};
inline SimEEPROM EEPROM;
