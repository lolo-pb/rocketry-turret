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
import csv
import time
import argparse
from datetime import datetime
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

# >>> OFFSET DE PITCH (grados) — apuntando recto al frente, tilt=0 <<<
# El Arduino ya suma OFFSET_EL=105; este es un ajuste fino adicional si la
# antena no quedo perfectamente horizontal al apuntar al frente. Normalmente 0.
PITCH_OFFSET = 0.0

# Umbral de ALTURA: solo ajustar el pitch si la altitud cambio mas que esto (m).
# Evita micro-movimientos del eje de elevacion por el ruido del GPS.
UMBRAL_ALTURA_M = 10.0

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
    return f_hat, l_hat, u_hat


def calcular_yaw_tilt_rel(base, cohete, f_hat, l_hat, u_hat):
    v = cohete - base
    cf = np.dot(v, f_hat)
    ci = np.dot(v, l_hat)
    ca = np.dot(v, u_hat)
    dist_h = np.sqrt(cf**2 + ci**2)
    yaw  = np.degrees(np.arctan2(ci, cf))
    tilt = np.degrees(np.arctan2(ca, dist_h))
    return yaw, tilt, dist_h


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
    print(f"Pitch    : offset={PITCH_OFFSET:.1f}°  umbral altura={UMBRAL_ALTURA_M:.0f}m")
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

    # --- Abrir CSV de log (nombre automatico con fecha/hora) ---
    csv_path = rf"C:\Users\felip\Documents\Ground_Station\csv_vuelos\vuelo_{datetime.now():%Y%m%d_%H%M%S}.csv"
  
    CSV_FIELDS = [
        "timestamp", "n_gps", "evento",
        "lat", "lon", "alt_m", "fix",
        "enu_este", "enu_norte", "enu_up",
        "yaw", "tilt", "dist_h",
        "yaw_cmd", "pitch_cmd",
        "mover_yaw", "mover_pitch",
    ]
    csv_fh = open(csv_path, "w", newline="", encoding="utf-8")
    csv_writer = csv.DictWriter(csv_fh, fieldnames=CSV_FIELDS, restval="")
    csv_writer.writeheader()
    csv_fh.flush()
    print(f"Log CSV: {csv_path}\n")

    marco = None          # se setea con el primer GPS valido
    ultimo_yaw = None     # ultimo yaw que disparo movimiento (filtro angular)
    ultima_alt = None     # ultima altitud que disparo ajuste de pitch (filtro 10m)
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
            ts = datetime.now().isoformat(timespec="milliseconds")

            # Cerado: primer paquete define el frente (yaw=0, pitch al offset)
            if marco is None:
                marco = construir_marco_calibracion(gs, punto)
                f_hat, l_hat, u_hat = marco
                _, tilt0, _ = calcular_yaw_tilt_rel(gs, punto, f_hat, l_hat, u_hat)
                print(f"  [CERADO] Frente fijado con primer GPS "
                      f"(lat={lat:.5f}, lon={lon:.5f}, alt={alt:.1f}m).")
                print(f"           yaw=0 aca.  tilt inicial del cohete={tilt0:.1f}°\n")
                mandar(ard, "Y0.0")
                mandar(ard, f"T{PITCH_OFFSET:.1f}")
                ultimo_yaw = 0.0
                ultima_alt = alt
                # Log de la fila de cerado
                csv_writer.writerow({
                    "timestamp": ts, "n_gps": n_gps, "evento": "CERADO",
                    "lat": f"{lat:.6f}", "lon": f"{lon:.6f}",
                    "alt_m": f"{alt:.2f}", "fix": fix,
                    "enu_este": f"{punto[0]:.2f}", "enu_norte": f"{punto[1]:.2f}",
                    "enu_up": f"{punto[2]:.2f}",
                    "yaw": "0.00", "tilt": f"{tilt0:.2f}", "dist_h": "0.00",
                    "yaw_cmd": "0.0", "pitch_cmd": f"{PITCH_OFFSET:.1f}",
                    "mover_yaw": 1, "mover_pitch": 1,
                })
                csv_fh.flush()
                continue

            f_hat, l_hat, u_hat = marco
            yaw, tilt, dist_h = calcular_yaw_tilt_rel(gs, punto, f_hat, l_hat, u_hat)

            # --- EJE YAW: filtro angular ---
            mover_yaw = (ultimo_yaw is None or abs(yaw - ultimo_yaw) >= UMBRAL_YAW)
            yaw_cmd = ""
            if mover_yaw:
                yaw_cmd = f"{yaw:.1f}"
                mandar(ard, f"Y{yaw:.1f}")
                ultimo_yaw = yaw

            # --- EJE PITCH: filtro por cambio de ALTURA (>= 10m) ---
            mover_pitch = (ultima_alt is None or abs(alt - ultima_alt) >= UMBRAL_ALTURA_M)
            pitch_cmd = ""
            if mover_pitch:
                tilt_cmd = tilt + PITCH_OFFSET
                pitch_cmd = f"{tilt_cmd:.1f}"
                mandar(ard, f"T{tilt_cmd:.1f}")
                ultima_alt = alt

            # Log de la fila de seguimiento
            csv_writer.writerow({
                "timestamp": ts, "n_gps": n_gps, "evento": "TRACK",
                "lat": f"{lat:.6f}", "lon": f"{lon:.6f}",
                "alt_m": f"{alt:.2f}", "fix": fix,
                "enu_este": f"{punto[0]:.2f}", "enu_norte": f"{punto[1]:.2f}",
                "enu_up": f"{punto[2]:.2f}",
                "yaw": f"{yaw:.2f}", "tilt": f"{tilt:.2f}", "dist_h": f"{dist_h:.2f}",
                "yaw_cmd": yaw_cmd, "pitch_cmd": pitch_cmd,
                "mover_yaw": 1 if mover_yaw else 0,
                "mover_pitch": 1 if mover_pitch else 0,
            })
            csv_fh.flush()

            # Reporte
            etiquetas = []
            etiquetas.append(f"Y{yaw:.1f}" if mover_yaw else "yaw-")
            etiquetas.append(f"T{tilt:.1f}" if mover_pitch else "pitch-")
            print(f"  GPS #{n_gps}  yaw={yaw:6.2f}  tilt={tilt:6.2f}  "
                  f"alt={alt:7.1f}m  dist={dist_h:6.1f}m  -> {' '.join(etiquetas)}")

    except KeyboardInterrupt:
        print("\n\n[Detenido]")
    finally:
        if rx.is_open:
            rx.close()
        if ard.is_open:
            ard.close()
        csv_fh.close()
        print(f"Total paquetes GPS procesados: {n_gps}")
        print(f"Log guardado en: {csv_path}")


if __name__ == '__main__':
    main()
