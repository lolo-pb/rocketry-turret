"""Local dashboard server with a deterministic simulated rocket flight."""

import asyncio
import contextlib
import math
import threading
import time
import urllib.error
import urllib.request
import webbrowser
from contextlib import asynccontextmanager
from pathlib import Path

import uvicorn
from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from fastapi.responses import FileResponse
from fastapi.staticfiles import StaticFiles


HOST = "127.0.0.1"
PORT = 8000
UPDATE_HZ = 10
PLAYBACK_SPEED = 1.0
PRELAUNCH_HOLD_S = 10.0
WEB_DIR = Path(__file__).parent / "web"
EARTH_RADIUS_M = 6378137.0
GROUND_STATION_LATITUDE_DEG = 31.95610
GROUND_STATION_LONGITUDE_DEG = -102.40344
GROUND_STATION_ALTITUDE_ASL_M = 915.0
SIMULATED_LAUNCH_EAST_M = 0.0
SIMULATED_LAUNCH_NORTH_M = 100.0

# Smoothed key points sampled from the TeleMega flight in the comparison workbook.
# Horizontal speed and heading are estimated between recorded GPS fixes. The
# GPS path is translated to the tracker.py ground station instead of relocating it.
# The final simulated seconds keep the rocket landed before the cycle restarts.
KEYFRAMES = [
    # time, altitude, horizontal speed, heading, vertical speed, yaw, tilt,
    # distance, GPS fix, satellites, satellites >=24/32/40 dB
    (   0.0,    0.0,  0.0,   0.0,   0.0,   0.0,  0.0,    0.0, 3, 14,  2, 0, 0),
    (   3.5,  747.7, 19.1, -57.4, 621.9, -59.8, 18.0,   29.3, 3, 10,  0, 0, 0),
    (  3.64,  822.4, 19.1, -57.4, 619.6, -59.6, 18.0,   32.0, 3, 10,  0, 0, 0),
    (   16., 5325.0, 51.2, -17.7, 218.7, -22.6,  8.0,  591.3, 3,  4,  0, 0, 0),
    (   36., 7518.0, 36.5, -17.8,   0.0, -19.7, 79.0, 1547.3, 3, 11,  4, 0, 0),
    (  40.5, 7538.3, 23.8, -16.8, -13.6, -19.4, 78.0, 1687.2, 3, 12, 12, 0, 0),
    (  180., 2194.4,  8.7, 160.5, -31.7,  -9.0, 57.0, 1545.9, 3, 15, 11, 3, 0),
    (241.02,  254.2, 15.7,  85.7, -27.1,  10.5, 11.0, 1383.4, 3, 15, 12, 6, 0),
    (247.78,   28.8, 17.5,  84.9, -11.9,  14.7,  1.0, 1430.9, 3, 15, 10, 0, 0),
    ( 250.0,    0.0,  0.0,  84.9,   0.0,  15.5,  0.0, 1437.0, 3, 15, 10, 0, 0),
    ( 266.4,    0.0,  0.0,  84.9,   0.0,  15.5,  0.0, 1437.0, 3, 15, 10, 0, 0),
]

latest_telemetry = {}


def simulation_time_at(sequence):
    step = PLAYBACK_SPEED / UPDATE_HZ
    cycle_duration_s = PRELAUNCH_HOLD_S + KEYFRAMES[-1][0]
    cycle_steps = round(cycle_duration_s / step)
    cycle_time_s = (sequence % cycle_steps) * step
    return max(cycle_time_s - PRELAUNCH_HOLD_S, 0.0)


def enu_to_gps(east_m, north_m, up_m):
    """Convert local ENU meters back to GPS coordinates for simulation."""
    latitude = GROUND_STATION_LATITUDE_DEG + math.degrees(north_m / EARTH_RADIUS_M)
    longitude = GROUND_STATION_LONGITUDE_DEG + math.degrees(
        east_m
        / (
            EARTH_RADIUS_M
            * math.cos(math.radians(GROUND_STATION_LATITUDE_DEG))
        )
    )
    return latitude, longitude, GROUND_STATION_ALTITUDE_ASL_M + up_m


def telemetry_at(sim_time, sequence):
    """Interpolate one deterministic telemetry sample from the keyframes."""
    left = KEYFRAMES[0]
    right = KEYFRAMES[-1]
    for candidate in KEYFRAMES[1:]:
        right = candidate
        if sim_time < candidate[0]:
            break
        left = candidate

    span = right[0] - left[0]
    progress = 0.0 if span == 0 else (sim_time - left[0]) / span

    def between(index):
        return left[index] + (right[index] - left[index]) * progress

    horizontal_speed = between(2)
    vertical_speed = between(4)
    vertical_acceleration = 0.0 if span == 0 else (right[4] - left[4]) / span
    path_bearing_rad = math.radians(between(5))
    path_distance_m = between(7)
    east_m = SIMULATED_LAUNCH_EAST_M + math.sin(path_bearing_rad) * path_distance_m
    north_m = SIMULATED_LAUNCH_NORTH_M + math.cos(path_bearing_rad) * path_distance_m
    up_m = between(1)
    latitude, longitude, altitude_asl_m = enu_to_gps(east_m, north_m, up_m)
    ground_distance_m = math.hypot(east_m, north_m)
    yaw_deg = math.degrees(math.atan2(east_m, north_m))
    tilt_deg = math.degrees(math.atan2(up_m, ground_distance_m))

    return {
        "source": "simulation",
        "sequence": sequence,
        "elapsed_s": round(sim_time, 2),
        "packet_type": "TRK",
        "tracker_id": "SIMULATED",
        "altitude_m": round(between(1), 1),
        "horizontal_speed_m_s": round(horizontal_speed, 1),
        "heading_deg": round(between(3), 1),
        "vertical_speed_m_s": round(vertical_speed, 1),
        "speed_m_s": round(math.hypot(horizontal_speed, vertical_speed), 1),
        "acceleration_m_s2": round(vertical_acceleration, 1),
        "yaw_deg": round(yaw_deg, 1),
        "tilt_deg": round(tilt_deg, 1),
        "distance_m": round(ground_distance_m, 1),
        "rocket_latitude_deg": round(latitude, 7),
        "rocket_longitude_deg": round(longitude, 7),
        "rocket_altitude_asl_m": round(altitude_asl_m, 1),
        "rocket_east_m": round(east_m, 2),
        "rocket_north_m": round(north_m, 2),
        "rocket_up_m": round(up_m, 1),
        "ground_station_latitude_deg": GROUND_STATION_LATITUDE_DEG,
        "ground_station_longitude_deg": GROUND_STATION_LONGITUDE_DEG,
        "ground_station_altitude_asl_m": GROUND_STATION_ALTITUDE_ASL_M,
        "gps_fix": left[8],
        "satellites": round(between(9)),
        "satellites_24db": round(between(10)),
        "satellites_32db": round(between(11)),
        "satellites_40db": round(between(12)),
        "crc_ok": True,
    }


async def run_simulation():
    global latest_telemetry
    sequence = 0

    while True:
        sim_time = simulation_time_at(sequence)
        latest_telemetry = telemetry_at(sim_time, sequence)
        sequence += 1
        await asyncio.sleep(1 / UPDATE_HZ)


@asynccontextmanager
async def lifespan(_app):
    task = asyncio.create_task(run_simulation())
    try:
        yield
    finally:
        task.cancel()
        with contextlib.suppress(asyncio.CancelledError):
            await task


app = FastAPI(lifespan=lifespan)
app.mount("/static", StaticFiles(directory=WEB_DIR), name="static")


@app.get("/")
async def index():
    return FileResponse(WEB_DIR / "index.html")


@app.get("/health")
async def health():
    return {"status": "ok", "source": "simulation"}


@app.websocket("/ws/telemetry")
async def telemetry_socket(websocket: WebSocket):
    await websocket.accept()
    last_sequence = -1
    try:
        while True:
            sample = latest_telemetry
            if sample and sample["sequence"] != last_sequence:
                await websocket.send_json(sample)
                last_sequence = sample["sequence"]
            await asyncio.sleep(1 / UPDATE_HZ / 2)
    except WebSocketDisconnect:
        pass


def open_browser_when_ready():
    health_url = f"http://{HOST}:{PORT}/health"
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        try:
            with urllib.request.urlopen(health_url, timeout=0.25):
                webbrowser.open(f"http://{HOST}:{PORT}")
                return
        except (urllib.error.URLError, OSError):
            time.sleep(0.1)


if __name__ == "__main__":
    threading.Thread(target=open_browser_when_ready, daemon=True).start()
    uvicorn.run(app, host=HOST, port=PORT)
