"""
Antenna Tracker — Seguimiento en VIVO desde el receiver Featherweight
======================================================================
Lee los paquetes GPS del receiver Featherweight en tiempo real, convierte
lat/lon/alt a coordenadas locales ENU con la Ground Station como origen,
calcula el yaw y lo manda al Arduino que mueve los servos.

Usa SOLO la parte de procesamiento de señal GPS del script de captura original
(regex GPS_STAT + lector serial). Se descarta CSV, GS/TX/BATT, etc.

DOS PUERTOS SERIAL:
  - RECEIVER (Featherweight)  -> de aca LEE los paquetes GPS
  - ARDUINO  (ground station) -> a aca ESCRIBE los comandos Y<yaw>

CERADO EN VIVO:
  El PRIMER paquete GPS valido recibido define el frente (yaw = 0).
  => Apuntar la antena al cohete en la rampa ANTES de arrancar el script.

Como es seguimiento horizontal (o prueba en tierra), el TILT se mantiene FIJO.

Uso:
    python tracker_vivo.py --rx COM7 --arduino COM6
    python tracker_vivo.py --list

Requisitos:  pip install pyserial numpy
"""

import re
import sys
import time
import argparse
import numpy as np
import serial
import serial.tools.list_ports


# ============================================================
# CONFIGURACIÓN  — EDITAR ACÁ
# ============================================================
# >>> GPS DE LA GROUND STATION (cargar valores reales) <<<
GS_LAT = 31.95610
GS_LON = -102.40344
GS_ALT = 915.0

# >>> TILT FIJO (grados) — prueba en tierra / seguimiento horizontal <<<
TILT_FIJO = 0.0

# Puertos por defecto (se pueden sobreescribir por linea de comandos)
RX_PORT_DEFAULT      = "COM7"   # receiver Featherweight
ARDUINO_PORT_DEFAULT = "COM6"   # Arduino ground station
BAUD = 115200

# Filtro: solo mover si el yaw cambio mas que este umbral (grados).
# Evita micro-movimientos por el jitter del GPS.
UMBRAL_YAW = 1.0


# ============================================================
# 1. PARSING GPS  (tomado del script de captura, solo lo necesario)
# ============================================================
GPS_STAT_RE = re.compile(
    r'@\s+GPS_STAT\s+\d+\s+\d+\s+\d+\s+\d+\s+[\d:\.]+\s+'
    r'(CRC_OK|CRC_ERR)\s+'
    r'(TRK|GS|FND)\s+(\S+)\s+'
    r'Alt\s+(\d+)\s+'
    r'lt\s+([+-]?[\d\.]+)\s+'
    r'ln\s+([+-]?[\d\.]+)\s+'
    r'Vel\s+([+-]?\d+)\s+([+-]?\d+)\s+([+-]?\d+)\s+'
    r'Fix\s+(\d)\s+'
    r'#\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)',
    re.IGNORECASE,
)

FT_TO_M = 0.3048


def parsear_gps(line):
    """
    Devuelve (lat, lon, alt_m, fix) si la linea es un paquete GPS valido,
    o None si no lo es. fix: 0/1=sin fix, 2=2D, 3=3D.
    """
    if 'GPS_STAT' not in line:
        return None
    m = GPS_STAT_RE.search(line)
    if not m:
        return None
    crc, unit, lid, alt, lat, lon, hv, hdg, vv, fix, ns, s24, s32, s40 = m.groups()
    return (float(lat), float(lon), int(alt) * FT_TO_M, int(fix))


def _serial_lines(ser):
    """Genera lineas completas '@...' del receiver (tomado del original)."""
    buf = b''
    while True:
        chunk = ser.read(512)
        if not chunk:
            continue
        buf += chunk
        text  = buf.decode('ascii', errors='replace')
        parts = text.split('@')
        buf   = (('@' + parts[-1]).encode('ascii', errors='replace')
                 if len(parts) > 1 else buf)
        for fragment in parts[:-1]:
            yield '@' + fragment.rstrip()


# ============================================================
# 2. CONVERSIÓN GPS -> ENU
# ============================================================
def gps_a_enu(lat, lon, alt, lat0, lon0, alt0):
    R = 6378137.0
    lat0r = np.radians(lat0)
    norte = np.radians(lat - lat0) * R
    este  = np.radians(lon - lon0) * R * np.cos(lat0r)
    up    = alt - alt0
    return np.array([este, norte, up])


# ============================================================
# 3. MARCO DE CALIBRACIÓN + YAW
# ============================================================
def construir_marco_calibracion(base, cohete_inicial):
    v0 = cohete_inicial - base
    fh = np.array([v0[0], v0[1], 0.0])
    n = np.linalg.norm(fh)
    if n < 1e-9:
        fh = np.array([1.0, 0.0, 0.0]); n = 1.0
    f_hat = fh / n
    u_hat = np.array([0.0, 0.0, 1.0])
    l_hat = np.cross(u_hat, f_hat)
    l_hat = l_hat / np.linalg.norm(l_hat)
    return f_hat, l_hat


def calcular_yaw_rel(base, cohete, f_hat, l_hat):
    v = cohete - base
    cf = np.dot(v, f_hat)
    ci = np.dot(v, l_hat)
    dist_h = np.sqrt(cf**2 + ci**2)
    yaw = np.degrees(np.arctan2(ci, cf))
    return yaw, dist_h


# ============================================================
# 4. ARDUINO  (sin handshake)
# ============================================================
def mandar(ser, comando):
    ser.write((comando + "\n").encode())
    ser.flush()


def list_ports():
    ports = list(serial.tools.list_ports.comports())
    if ports:
        print("\nPuertos COM disponibles:")
        for p in ports:
            print(f"  {p.device:10}  {p.description}")
    else:
        print("\nNo se encontraron puertos COM.")


# ============================================================
# MAIN
# ============================================================
def main():
    ap = argparse.ArgumentParser(description="Tracker en vivo Featherweight -> Arduino")
    ap.add_argument('--rx',      default=RX_PORT_DEFAULT,      help='puerto del receiver Featherweight')
    ap.add_argument('--arduino', default=ARDUINO_PORT_DEFAULT, help='puerto del Arduino')
    ap.add_argument('--baud',    default=BAUD, type=int)
    ap.add_argument('--list',    action='store_true', help='listar puertos y salir')
    args = ap.parse_args()

    if args.list:
        list_ports()
        return

    gs = np.array([0.0, 0.0, 0.0])  # GS = origen ENU

    print("=" * 60)
    print("TRACKER EN VIVO — Featherweight -> Arduino")
    print("=" * 60)
    print(f"Receiver : {args.rx}")
    print(f"Arduino  : {args.arduino}")
    print(f"GS origen: lat={GS_LAT}  lon={GS_LON}")
    print(f"Tilt fijo: {TILT_FIJO:.1f}°")
    print("=" * 60)
    print("IMPORTANTE: apunta la antena al cohete en la rampa.")
    print("El PRIMER paquete GPS define el frente (yaw=0).")
    print("=" * 60)

    # Abrir Arduino
    try:
        ard = serial.Serial(args.arduino, args.baud, timeout=2)
    except serial.SerialException as e:
        print(f"[ERROR] No se pudo abrir el Arduino en {args.arduino}: {e}")
        list_ports()
        sys.exit(1)
    time.sleep(2.0)            # reset del Arduino
    ard.reset_input_buffer()

    # Fijar tilt una vez
    print(f"\nFijando tilt en {TILT_FIJO:.1f}°...")
    mandar(ard, f"T{TILT_FIJO:.1f}")
    time.sleep(0.5)

    # Abrir receiver Featherweight
    try:
        rx = serial.Serial(
            port=args.rx, baudrate=args.baud,
            bytesize=serial.EIGHTBITS, parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE, timeout=0.1,
        )
    except serial.SerialException as e:
        print(f"[ERROR] No se pudo abrir el receiver en {args.rx}: {e}")
        ard.close()
        list_ports()
        sys.exit(1)

    print(f"Escuchando paquetes GPS en {args.rx}... (Ctrl+C para parar)\n")

    marco = None          # se setea con el primer GPS valido
    ultimo_yaw = None     # para el filtro de umbral
    n_gps = 0

    try:
        for line in _serial_lines(rx):
            res = parsear_gps(line)
            if res is None:
                continue
            lat, lon, alt, fix = res

            # Ignorar paquetes sin fix 3D
            if fix < 2:
                print(f"  [GPS sin fix valido, ignorado]")
                continue

            punto = gps_a_enu(lat, lon, alt, GS_LAT, GS_LON, GS_ALT)
            n_gps += 1

            # Cerado: primer paquete define el frente
            if marco is None:
                marco = construir_marco_calibracion(gs, punto)
                print(f"  [CERADO] Frente fijado con primer GPS "
                      f"(lat={lat:.5f}, lon={lon:.5f}). yaw=0 aca.\n")
                mandar(ard, "Y0.0")
                ultimo_yaw = 0.0
                continue

            f_hat, l_hat = marco
            yaw, dist_h = calcular_yaw_rel(gs, punto, f_hat, l_hat)

            # Filtro de umbral: solo mover si cambio lo suficiente
            if ultimo_yaw is not None and abs(yaw - ultimo_yaw) < UMBRAL_YAW:
                print(f"  GPS #{n_gps}  yaw={yaw:6.2f}  dist={dist_h:6.1f}m  "
                      f"[sin cambio, no se mueve]")
                continue

            print(f"  GPS #{n_gps}  yaw={yaw:6.2f}  dist={dist_h:6.1f}m  -> Y{yaw:.1f}")
            mandar(ard, f"Y{yaw:.1f}")
            ultimo_yaw = yaw

    except KeyboardInterrupt:
        print("\n\n[Detenido]")
    finally:
        if rx.is_open:
            rx.close()
        if ard.is_open:
            ard.close()
        print(f"Total paquetes GPS procesados: {n_gps}")


if __name__ == '__main__':
    main()
