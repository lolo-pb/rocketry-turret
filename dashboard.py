"""Local dashboard server with a deterministic simulated rocket flight."""

import asyncio
import contextlib
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
    # time, state, altitude, vertical speed, acceleration, yaw, tilt, distance, satellites
    (0.0, "boost", 0.0, 0.0, 138.0, 0.0, 0.0, 0.0, 14),
    (3.5, "boost", 748.0, 622.0, 0.0, -8.0, 18.0, 15.0, 10),
    (3.64, "fast", 822.0, 620.0, -23.0, -8.0, 18.0, 15.0, 10),
    (16.29, "coast", 5388.0, 215.0, -14.0, -25.0, 55.0, 850.0, 8),
    (36.97, "drogue", 7480.0, 0.0, -10.0, -47.0, 80.0, 1806.0, 12),
    (40.5, "drogue", 7538.0, -14.0, -10.0, -47.0, 80.0, 1806.0, 12),
    (180.0, "drogue", 1800.0, -61.0, 0.0, -36.0, 48.0, 1600.0, 14),
    (241.02, "main", 254.0, -27.0, 0.0, -20.0, 18.0, 1750.0, 15),
    (247.78, "main", 29.0, -12.0, 0.0, -18.0, 5.0, 1780.0, 15),
    (250.0, "landed", 0.0, 0.0, 0.0, -18.0, 0.0, 1780.0, 15),
    (266.4, "landed", 0.0, 0.0, 0.0, -18.0, 0.0, 1780.0, 15),
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

    return {
        "source": "simulation",
        "sequence": sequence,
        "elapsed_s": round(sim_time, 2),
        "flight_state": left[1],
        "altitude_m": round(between(2), 1),
        "vertical_speed_m_s": round(between(3), 1),
        "acceleration_m_s2": round(between(4), 1),
        "yaw_deg": round(between(5), 1),
        "tilt_deg": round(between(6), 1),
        "distance_m": round(between(7), 1),
        "satellites": round(between(8)),
        "gps_fix": 3,
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
