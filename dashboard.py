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


HOST = "127.0.0.1"
PORT = 8000
UPDATE_HZ = 10
PLAYBACK_SPEED = 8.0
WEB_DIR = Path(__file__).parent / "web"

# Smoothed key points based on the TeleMega flight in the comparison workbook.
# The final 16 simulated seconds keep the rocket landed for a two-second pause.
KEYFRAMES = [
    # time, altitude, horizontal speed, heading, vertical speed, yaw, tilt,
    # distance, GPS fix, satellites, satellites >=24/32/40 dB
    (   0.0,    0.0,  0.0,   0.0,   0.0,   0.0,  0.0,    0.0, 3, 14, 12, 6, 2),
    (   3.5,  748.0,  4.3,  -8.0, 622.0,  -8.0, 18.0,   15.0, 3, 10,  8, 4, 2),
    (  3.64,  822.0,  0.0,  -8.0, 620.0,  -8.0, 18.0,   15.0, 3, 10,  8, 4, 2),
    (   16., 5388.0, 67.6, -25.0, 215.0, -25.0, 55.0,  850.0, 3,  8,  6, 3, 1),
    (   36., 7480.0, 46.2, -47.0,   0.0, -47.0, 80.0, 1806.0, 3, 12, 10, 5, 2),
    (  40.5, 7538.0,  0.0, -47.0, -14.0, -47.0, 80.0, 1806.0, 3, 12, 10, 5, 2),
    (  180., 1800.0,  1.5, 144.0, -61.0, -36.0, 48.0, 1600.0, 3, 14, 12, 7, 3),
    (241.02,  254.0,  2.5, -20.0, -27.0, -20.0, 18.0, 1750.0, 3, 15, 13, 8, 4),
    (247.78,   29.0,  4.4, -18.0, -12.0, -18.0,  5.0, 1780.0, 3, 15, 13, 8, 4),
    (250.0 ,    0.0,  0.0, -18.0,   0.0, -18.0,  0.0, 1780.0, 3, 15, 13, 8, 4),
    (266.4 ,    0.0,  0.0, -18.0,   0.0, -18.0,  0.0, 1780.0, 3, 15, 13, 8, 4),
]

latest_telemetry = {}


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
        "yaw_deg": round(between(5), 1),
        "tilt_deg": round(between(6), 1),
        "distance_m": round(between(7), 1),
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
    step = PLAYBACK_SPEED / UPDATE_HZ
    cycle_steps = round(KEYFRAMES[-1][0] / step)

    while True:
        sim_time = (sequence % cycle_steps) * step
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
