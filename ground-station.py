"""Ground-station web server and controller launcher."""

import argparse
import asyncio
import contextlib
import json
import subprocess
import tempfile
import threading
from contextlib import asynccontextmanager
from pathlib import Path

import uvicorn
from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from fastapi.responses import FileResponse
from fastapi.staticfiles import StaticFiles


HOST = "0.0.0.0"
PORT = 8000
UPDATE_HZ = 10
WEB_DIR = Path(__file__).parent / "web"
latest_telemetry = {}
controller_socket = None
controller_connected = False


def dashboard_telemetry(snapshot, sequence):
    rocket = snapshot["rocket"]
    station = snapshot["ground_station"]
    return {
        "source": "controller",
        "sequence": sequence,
        "state": snapshot["state"],
        "receivers": snapshot["receivers"],
        "elapsed_s": 0,
        "packet_type": "0",
        "tracker_id": "0",
        "altitude_m": 0,
        "horizontal_speed_m_s": 0,
        "heading_deg": 0,
        "vertical_speed_m_s": 0,
        "speed_m_s": 0,
        "acceleration_m_s2": 0,
        "yaw_deg": 0,
        "tilt_deg": 0,
        "distance_m": 0,
        "rocket_latitude_deg": rocket["latitude"],
        "rocket_longitude_deg": rocket["longitude"],
        "rocket_altitude_asl_m": rocket["altitude_m"],
        "rocket_east_m": 0,
        "rocket_north_m": 0,
        "rocket_up_m": 0,
        "ground_station_latitude_deg": station["latitude"],
        "ground_station_longitude_deg": station["longitude"],
        "ground_station_altitude_asl_m": station["altitude_m"],
        "gps_fix": rocket["fix"],
        "satellites": 0,
        "satellites_24db": 0,
        "satellites_32db": 0,
        "satellites_40db": 0,
        "crc_ok": False,
    }


async def read_controller():
    global latest_telemetry, controller_connected
    sequence = 0
    while True:
        try:
            reader, writer = await asyncio.open_unix_connection(controller_socket)
            controller_connected = True
            try:
                while line := await reader.readline():
                    snapshot = json.loads(line)
                    if snapshot.get("type") == "snapshot":
                        latest_telemetry = dashboard_telemetry(snapshot, sequence)
                        sequence += 1
            finally:
                controller_connected = False
                writer.close()
                await writer.wait_closed()
        except (OSError, ValueError):
            controller_connected = False
        await asyncio.sleep(1)


@asynccontextmanager
async def lifespan(_app):
    task = asyncio.create_task(read_controller())
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
    return {"status": "ok", "source": "controller", "controller_connected": controller_connected}


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
    global controller_socket
    with tempfile.TemporaryDirectory(prefix="ground-station-") as directory:
        controller_socket = str(Path(directory) / "controller.sock")
        command = [str(root / "controller" / "build" / "controller")]
        if args.tracking:
            command.append("--tracking")
        command.append(controller_socket)
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
