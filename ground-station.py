"""Local dashboard server with a deterministic simulated rocket flight."""

import argparse
import asyncio
import contextlib
import subprocess
import tempfile
import threading
from contextlib import asynccontextmanager
from pathlib import Path

import uvicorn
import mock_receiver
from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from fastapi.responses import FileResponse
from fastapi.staticfiles import StaticFiles


HOST = "0.0.0.0"
PORT = 8000
UPDATE_HZ = mock_receiver.UPDATE_HZ
PLAYBACK_SPEED = mock_receiver.PLAYBACK_SPEED
PRELAUNCH_HOLD_S = mock_receiver.PRELAUNCH_HOLD_S
EARTH_RADIUS_M = mock_receiver.EARTH_RADIUS_M
GROUND_STATION_LATITUDE_DEG = mock_receiver.GROUND_STATION_LATITUDE_DEG
GROUND_STATION_LONGITUDE_DEG = mock_receiver.GROUND_STATION_LONGITUDE_DEG
GROUND_STATION_ALTITUDE_ASL_M = mock_receiver.GROUND_STATION_ALTITUDE_ASL_M
simulation_time_at = mock_receiver.simulation_time_at
telemetry_at = mock_receiver.telemetry_at
WEB_DIR = Path(__file__).parent / "web"
latest_telemetry = {}


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


def main():
    parser = argparse.ArgumentParser(description="Run the ground station and controller")
    parser.add_argument("--tracking", action="store_true", help="start directly in tracking mode")
    parser.add_argument("receivers", nargs="*", help="receiver device or FIFO paths")
    args = parser.parse_args()

    root = Path(__file__).resolve().parent
    subprocess.run(["make", "-C", str(root / "controller")], check=True)
    with tempfile.TemporaryDirectory(prefix="ground-station-") as directory:
        command = [str(root / "controller" / "build" / "controller")]
        if args.tracking:
            command.append("--tracking")
        command.append(str(Path(directory) / "controller.sock"))
        command.extend(args.receivers)
        controller = subprocess.Popen(command)
        server = uvicorn.Server(uvicorn.Config(app, host=HOST, port=PORT))

        def monitor_controller():
            code = controller.wait()
            if not server.should_exit:
                print(f"Controller exited with status {code}", flush=True)
                server.should_exit = True

        monitor = threading.Thread(target=monitor_controller, daemon=True)
        monitor.start()
        try:
            server.run()
        finally:
            if controller.poll() is None:
                controller.terminate()
            try:
                controller.wait(timeout=5)
            except subprocess.TimeoutExpired:
                controller.kill()
                controller.wait()
            monitor.join()


if __name__ == "__main__":
    main()
