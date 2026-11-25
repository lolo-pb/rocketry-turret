"""Ground-station web server and controller launcher."""

import argparse
import asyncio
import contextlib
import ipaddress
import json
import math
import os
import subprocess
import tempfile
import threading
from contextlib import asynccontextmanager
from pathlib import Path

import uvicorn
from fastapi import FastAPI, HTTPException, WebSocket, WebSocketDisconnect
from fastapi.responses import FileResponse
from fastapi.staticfiles import StaticFiles


HOST = "0.0.0.0"
PORT = 8000
UPDATE_HZ = 10
WEB_DIR = Path(__file__).parent / "web"
latest_telemetry = {}
controller_socket = None
controller_connected = False
controller_writer = None
pending_command = None
command_lock = asyncio.Lock()


def dashboard_telemetry(snapshot, sequence):
    rocket = snapshot["rocket"]
    station = snapshot["ground_station"]
    turret = snapshot["turret"]
    east = north = up = 0
    if rocket["fix"] >= 2:
        earth_radius_m = 6378137.0
        east = math.radians(rocket["longitude"] - station["longitude"]) * earth_radius_m * math.cos(math.radians(station["latitude"]))
        north = math.radians(rocket["latitude"] - station["latitude"]) * earth_radius_m
        up = rocket["altitude_m"] - station["altitude_m"]
    return {
        "source": "controller",
        "sequence": sequence,
        "state": snapshot["state"],
        "receivers": snapshot["receivers"],
        "elapsed_s": 0,
        "packet_type": "0",
        "tracker_id": "0",
        "altitude_m": up,
        "horizontal_speed_m_s": 0,
        "heading_deg": 0,
        "vertical_speed_m_s": 0,
        "speed_m_s": 0,
        "acceleration_m_s2": 0,
        "yaw_deg": turret["azimuth_deg"],
        "tilt_deg": turret["elevation_deg"],
        "distance_m": math.hypot(east, north),
        "rocket_latitude_deg": rocket["latitude"],
        "rocket_longitude_deg": rocket["longitude"],
        "rocket_altitude_asl_m": rocket["altitude_m"],
        "rocket_east_m": east,
        "rocket_north_m": north,
        "rocket_up_m": up,
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
    global latest_telemetry, controller_connected, controller_writer, pending_command
    sequence = 0
    while True:
        try:
            reader, writer = await asyncio.open_unix_connection(controller_socket)
            controller_connected = True
            controller_writer = writer
            try:
                while line := await reader.readline():
                    snapshot = json.loads(line)
                    if snapshot.get("type") == "snapshot":
                        latest_telemetry = dashboard_telemetry(snapshot, sequence)
                        sequence += 1
                    elif snapshot.get("type") == "reply" and pending_command and not pending_command.done():
                        pending_command.set_result(snapshot)
            finally:
                controller_connected = False
                controller_writer = None
                if pending_command and not pending_command.done():
                    pending_command.set_exception(ConnectionError("Controller disconnected"))
                writer.close()
                await writer.wait_closed()
        except (OSError, ValueError):
            controller_connected = False
        await asyncio.sleep(1)


@asynccontextmanager
async def lifespan(_app):
    task = asyncio.create_task(read_controller())
    try:
        try:
            addresses = subprocess.check_output(["hostname", "-I"], text=True).split()
            found = False
            for address in addresses:
                ip = ipaddress.ip_address(address)
                if ip.version == 4 and not ip.is_loopback and not ip.is_link_local:
                    print(f"Dashboard on your network: https://{ip}:{PORT}", flush=True)
                    found = True
            if not found:
                print("No network IPv4 address found.", flush=True)
        except (OSError, subprocess.CalledProcessError, ValueError):
            print("Could not determine a network address. Run hostname -I to find it.", flush=True)
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


async def send_controller_command(command):
    global pending_command
    async with command_lock:
        if not controller_connected or controller_writer is None:
            raise HTTPException(status_code=503, detail="Controller disconnected")
        pending_command = asyncio.get_running_loop().create_future()
        try:
            controller_writer.write((json.dumps(command) + "\n").encode())
            await controller_writer.drain()
            reply = await asyncio.wait_for(pending_command, timeout=3)
        except (OSError, ConnectionError, asyncio.TimeoutError):
            raise HTTPException(status_code=503, detail="Controller did not respond")
        finally:
            pending_command = None
        if not reply["accepted"]:
            raise HTTPException(status_code=409, detail="Controller rejected the command")
        return reply


@app.post("/api/start-tracking")
async def start_tracking():
    return await send_controller_command("START_TRACKING")


@app.post("/api/start-manual")
async def start_manual():
    return await send_controller_command("START_MANUAL")


@app.post("/api/joystick")
async def joystick(x: int, y: int):
    if not (0 <= x <= 1023 and 0 <= y <= 1023):
        raise HTTPException(status_code=422, detail="Joystick axes must be between 0 and 1023")
    return await send_controller_command(f"JOYSTICK {x} {y}")


@app.post("/api/arm")
async def arm():
    return await send_controller_command("ARM")


@app.websocket("/ws/telemetry")
async def telemetry_socket(websocket: WebSocket):
    await websocket.accept()
    last_sequence = -1
    last_connection = None
    try:
        while True:
            sample = latest_telemetry
            if sample and (sample["sequence"] != last_sequence or controller_connected != last_connection):
                await websocket.send_json({**sample, "controller_connected": controller_connected})
                last_sequence = sample["sequence"]
                last_connection = controller_connected
            await asyncio.sleep(1 / UPDATE_HZ / 2)
    except WebSocketDisconnect:
        pass


def main():
    parser = argparse.ArgumentParser(description="Run the ground station and controller")
    parser.add_argument("--tracking", action="store_true", help="start directly in tracking mode")
    parser.add_argument("--log-dir", default="logs", help="directory for telemetry logs")
    parser.add_argument("receivers", nargs="*", help="receiver paths; prefix TeleDongle paths with teledongle:")
    args = parser.parse_args()

    tls_dir = Path.home() / ".config" / "ground-station" / "tls"
    cert = tls_dir / "dashboard.pem"
    key = tls_dir / "dashboard-key.pem"
    tls_dir.mkdir(mode=0o700, parents=True, exist_ok=True)
    if not cert.exists() and not key.exists():
        with tempfile.TemporaryDirectory(dir=tls_dir) as directory:
            temp_cert = Path(directory) / "dashboard.pem"
            temp_key = Path(directory) / "dashboard-key.pem"
            subprocess.run([
                "openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes",
                "-keyout", str(temp_key), "-out", str(temp_cert), "-days", "3650",
                "-subj", "/CN=ground-station", "-addext", "subjectAltName=DNS:gs.local",
            ], check=True)
            temp_key.chmod(0o600)
            temp_key.replace(key)
            temp_cert.replace(cert)
    if not cert.is_file() or not key.is_file():
        parser.error(f"HTTPS requires both {cert} and {key}")
    config = uvicorn.Config(app, host=HOST, port=PORT, timeout_graceful_shutdown=2,
                            ssl_certfile=str(cert), ssl_keyfile=str(key))
    config.load()

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
        controller = subprocess.Popen(command, env={**os.environ, "TURRET_LOG_DIR": args.log_dir})
        server = uvicorn.Server(config)

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
