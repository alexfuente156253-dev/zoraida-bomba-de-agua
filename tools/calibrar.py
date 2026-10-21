#!/usr/bin/env python3
"""Asistente de calibracion para control_bomba_b1_v10_3 (requiere MODO_CALIBRACION).

Ejemplos:
  python tools/calibrar.py --listar
  python tools/calibrar.py --puerto COM3 E --nota "tanque a 3 dedos del fondo"
  python tools/calibrar.py --puerto COM3 secuencia

Cada medicion se guarda en data/calibraciones.csv. Los comandos que encienden la
bomba (L, F, S, C) piden confirmacion escrita; Ctrl+C envia 'X' (apaga la bomba).
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
CSV_PATH = Path(__file__).resolve().parent.parent / "data" / "calibraciones.csv"

# comando -> (descripcion, enciende bomba, timeout en s, preparacion manual)
COMANDOS = {
    "E": ("P_ESTATICA_REAL", False, 25,
          "Bomba apagada, tanque a ~3 dedos del fondo, grifo abierto ~15 s y cerrado."),
    "L": ("P_LLENADO_REAL", True, 60,
          "Flotador del tanque ABIERTO y grifos de la casa CERRADOS."),
    "F": ("CAIDA_MIN_FLUJO_ACTIVO", True, 70,
          "Flotador del tanque ABIERTO y grifos de la casa CERRADOS."),
    "S": ("P_SHUTOFF_REAL", True, 90,
          "Flotador CERRADO y casa SIN consumo."),
    "C": ("T_CARGA_PULMON", True, 150,
          "Flotador CERRADO, casa SIN consumo y P2 <= 11 PSI (abre un grifo para bajarla)."),
}
SECUENCIA = ["E", "L", "F", "S", "C"]
CAMPOS = ["fecha", "comando", "medicion", "nota", "datos"]


def listar_puertos():
    for p in list_ports.comports():
        print(f"{p.device}\t{p.description}")


def abrir(puerto):
    ser = serial.Serial(puerto, BAUD, timeout=0.5)
    time.sleep(2.5)  # el Nano se reinicia al abrir el puerto
    ser.reset_input_buffer()
    return ser


def ejecutar(ser, cmd):
    """Envia el comando y devuelve (resultado, lineas). resultado: dict RES o None."""
    _, _, timeout, _ = COMANDOS[cmd]
    ser.write(cmd.encode())
    fin = time.time() + timeout
    lineas, res = [], None
    try:
        while time.time() < fin:
            linea = ser.readline().decode(errors="replace").strip()
            if not linea or linea.startswith("P2_PSI:"):
                continue
            print("  " + linea)
            lineas.append(linea)
            if linea.startswith("RES,"):
                partes = linea.split(",")
                res = dict(p.split("=", 1) for p in partes[2:] if "=" in p)
                break
            if linea.startswith(("ABORTADO", "RECHAZADO", "INCOMPLETO")):
                break
    except KeyboardInterrupt:
        ser.write(b"X")
        print("  Interrumpido: enviado X (bomba apagada)")
    else:
        if res is None and not lineas:
            print("  Sin respuesta: el firmware tiene MODO_CALIBRACION activo?")
    return res, lineas


def guardar(cmd, nota, res):
    CSV_PATH.parent.mkdir(exist_ok=True)
    nuevo = not CSV_PATH.exists()
    with CSV_PATH.open("a", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        if nuevo:
            w.writerow(CAMPOS)
        datos = ";".join(f"{k}={v}" for k, v in res.items())
        w.writerow([datetime.now().isoformat(timespec="seconds"), cmd, COMANDOS[cmd][0], nota, datos])
    print(f"  Guardado en {CSV_PATH}")


def correr(ser, cmd, nota, confirmar):
    nombre, bomba, _, prep = COMANDOS[cmd]
    print(f"\n== {cmd}: {nombre} ==\nPreparacion: {prep}")
    if bomba and confirmar:
        if input("Esto ENCIENDE la bomba. Estas presente y listo? Escribe SI: ").strip() != "SI":
            print("  Omitido.")
            return
    elif not bomba and confirmar:
        input("Enter cuando la instalacion este lista... ")
    res, _ = ejecutar(ser, cmd)
    if res:
        guardar(cmd, nota, res)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--puerto", help="Puerto serie, p. ej. COM3")
    ap.add_argument("--listar", action="store_true", help="Lista los puertos serie")
    ap.add_argument("--nota", default="", help="Nota para el CSV (nivel de tanque, presion de calle...)")
    ap.add_argument("--si", action="store_true", help="No pedir confirmaciones (no recomendado)")
    ap.add_argument("accion", nargs="?", help="E, L, F, S, C o 'secuencia'")
    a = ap.parse_args()

    if a.listar:
        listar_puertos()
        return
    if not a.puerto or not a.accion:
        ap.error("indica --puerto y una accion (E, L, F, S, C o secuencia)")
    accion = a.accion.upper() if a.accion.lower() != "secuencia" else "SECUENCIA"
    cmds = SECUENCIA if accion == "SECUENCIA" else [accion]
    if any(c not in COMANDOS for c in cmds):
        ap.error("accion invalida")

    with abrir(a.puerto) as ser:
        for c in cmds:
            correr(ser, c, a.nota, not a.si)


if __name__ == "__main__":
    try:
        main()
    except serial.SerialException as e:
        sys.exit(f"Error de puerto serie: {e}")
