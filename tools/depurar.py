#!/usr/bin/env python3
"""Depuracion de control_bomba_b1_v10_3 (firmware compilado con MODO_DEPURACION, entorno nano_depuracion).

Ejemplos:
  python tools/depurar.py --listar
  python tools/depurar.py --puerto COM3 monitor              # guarda eventos y telemetria en vivo
  python tools/depurar.py --puerto COM3 monitor --minutos 60
  python tools/depurar.py --puerto COM3 descargar            # registro guardado en la EEPROM
  python tools/depurar.py --puerto COM3 borrar               # borra el registro de la EEPROM

Los archivos se guardan en data/depuracion/ con fecha y hora en el nombre:
  eventos_*.csv     cada cambio de estado (en vivo)
  telemetria_*.csv  una fila por segundo
  registro_*.csv    eventos descargados de la EEPROM (los ultimos 46, sobreviven a cortes de luz)
  serie_*.txt       todo lo recibido por el puerto, sin procesar
Ctrl+C termina el monitor.
"""
import argparse
import csv
import sys
import time
from datetime import datetime
from pathlib import Path

import serial
from serial.tools import list_ports

BAUD = 9600
DIR = Path(__file__).resolve().parent.parent / "data" / "depuracion"

# Deben coincidir con EstadoSistema y MotivoEvento del firmware
ESTADOS = ["Reposo", "Verificacion de flujo", "Bombeo", "Micro-pausa", "Espera sin flujo", "Falla de sensor"]
MODOS = ["Subiendo", "Llenando", "Hidroneumatico"]
MOTIVOS = [
    "-", "Encendido", "Demanda (P2 <= arranque)", "Flujo confirmado", "Verificacion: se estanco sin flujo",
    "Corte a 20 psi", "Lleno (estancada 18-20 psi)", "Prueba: estancada en nivel nuevo",
    "Micro-pausa periodica", "Presion cayo: sigue llenando", "Estancamiento sobre el piso",
    "Sin flujo (pausa bajo el piso)", "Sin flujo (estancada en la estatica)", "Tiempo maximo de bombeo",
    "Fin de espera: reposo", "Fin de espera: reintento", "Falla de sensor", "Sensor recuperado",
    "Fin de prueba de calibracion", "Flujo debil: estancada sobre la estatica, a bombeo",
]


def nombre(lista, i):
    try:
        return lista[int(i)]
    except (ValueError, IndexError):
        return f"? ({i})"


def abrir(puerto):
    ser = serial.Serial(puerto, BAUD, timeout=0.5)
    time.sleep(2.5)  # el Nano se reinicia al abrir el puerto
    ser.reset_input_buffer()
    return ser


def archivo(prefijo, ext="csv"):
    DIR.mkdir(parents=True, exist_ok=True)
    return DIR / f"{prefijo}_{datetime.now().strftime('%Y%m%d_%H%M%S')}.{ext}"


def fila_evento(partes):
    # secuencia, t_s, estado, motivo, p2, intentos
    sec, t_s, est, mot, p2, intentos = partes[:6]
    return [sec, t_s, est, nombre(ESTADOS, est), mot, nombre(MOTIVOS, mot), p2, intentos]


CAB_EVENTO = ["secuencia", "t_s", "estado", "estado_nombre", "motivo", "motivo_nombre", "p2_psi", "intentos"]
CAB_TELEM = ["hora_pc", "t_s", "estado", "estado_nombre", "p2_psi", "bomba", "modo", "modo_nombre", "meseta_psi", "intentos"]


def monitor(ser, minutos):
    f_ev, f_tl, f_raw = archivo("eventos"), archivo("telemetria"), archivo("serie", "txt")
    fin = time.time() + minutos * 60 if minutos else None
    with f_ev.open("w", newline="", encoding="utf-8") as fe, \
         f_tl.open("w", newline="", encoding="utf-8") as ft, \
         f_raw.open("w", encoding="utf-8") as fr:
        we, wt = csv.writer(fe), csv.writer(ft)
        we.writerow(["hora_pc"] + CAB_EVENTO)
        wt.writerow(CAB_TELEM)
        print(f"Guardando en {DIR} (Ctrl+C para terminar)")
        try:
            while fin is None or time.time() < fin:
                linea = ser.readline().decode(errors="replace").strip()
                if not linea:
                    continue
                hora = datetime.now().isoformat(timespec="seconds")
                fr.write(f"{hora} {linea}\n")
                partes = linea.split(",")
                if partes[0] == "EV" and len(partes) >= 7:
                    fila = fila_evento(partes[1:])
                    we.writerow([hora] + fila)
                    fe.flush()
                    print(f"{hora}  {fila[3]:<22} {fila[5]:<36} P2={fila[6]} psi  intentos={fila[7]}")
                elif partes[0] == "TL" and len(partes) >= 8:
                    t_s, est, p2, bomba, modo, meseta, intentos = partes[1:8]
                    wt.writerow([hora, t_s, est, nombre(ESTADOS, est), p2, bomba, modo, nombre(MODOS, modo),
                                 meseta, intentos])
                    ft.flush()
        except KeyboardInterrupt:
            pass
    print(f"Eventos: {f_ev}\nTelemetria: {f_tl}\nSerie: {f_raw}")


def descargar(ser):
    ser.write(b"D")
    destino = archivo("registro")
    filas, fin = [], time.time() + 20
    while time.time() < fin:
        linea = ser.readline().decode(errors="replace").strip()
        if linea.startswith("LOG,"):
            filas.append(fila_evento(linea.split(",")[1:]))
        elif linea == "LOG_FIN":
            break
    else:
        print("Aviso: no llego LOG_FIN; el registro puede estar incompleto.")
    with destino.open("w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(CAB_EVENTO)
        w.writerows(filas)
    for fila in filas:
        print(f"#{fila[0]:>5}  t={fila[1]:>7} s  {fila[3]:<22} {fila[5]:<36} P2={fila[6]} psi  intentos={fila[7]}")
    print(f"{len(filas)} eventos guardados en {destino}")


def borrar(ser, confirmar):
    if confirmar and input("Esto BORRA el registro de eventos del Arduino. Escribe SI: ").strip() != "SI":
        print("Cancelado.")
        return
    ser.write(b"B")
    fin = time.time() + 10
    while time.time() < fin:
        if ser.readline().decode(errors="replace").strip() == "LOG_BORRADO":
            print("Registro borrado.")
            return
    print("Sin confirmacion del Arduino: el firmware tiene MODO_DEPURACION activo?")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--puerto", help="Puerto serie, p. ej. COM3")
    ap.add_argument("--listar", action="store_true", help="Lista los puertos serie")
    ap.add_argument("--minutos", type=float, default=0, help="Duracion del monitor (0 = hasta Ctrl+C)")
    ap.add_argument("--si", action="store_true", help="No pedir confirmacion al borrar")
    ap.add_argument("accion", nargs="?", choices=["monitor", "descargar", "borrar"])
    a = ap.parse_args()
    if a.listar:
        for p in list_ports.comports():
            print(f"{p.device}\t{p.description}")
        return
    if not a.puerto or not a.accion:
        ap.error("indica --puerto y una accion (monitor, descargar o borrar)")
    with abrir(a.puerto) as ser:
        if a.accion == "monitor":
            monitor(ser, a.minutos)
        elif a.accion == "descargar":
            descargar(ser)
        else:
            borrar(ser, not a.si)


if __name__ == "__main__":
    try:
        main()
    except serial.SerialException as e:
        sys.exit(f"Error de puerto serie: {e}")
