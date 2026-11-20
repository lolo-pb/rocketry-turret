# Rocketry Turret

<img width="1901" height="967" alt="image" src="https://github.com/user-attachments/assets/538fa72a-4b73-420b-9990-5ebd686a5817" />


Ground-station antenna tracker for receiving live Featherweight GPS telemetry
and commanding Arduino-controlled azimuth and elevation servos.

## Files

- `tracker.py` reads GPS packets, calculates yaw and elevation, sends serial
  commands to the Arduino, logs telemetry to CSV, and rejects large GPS jumps.
- `antenna_controller.ino` receives `Y<yaw>` and `T<tilt>` commands and moves
  the two servos.
- `requirements.txt` lists the Python dependencies.

## Python setup

On Linux, create and activate a virtual environment, then install the
dependencies:

```shell
python3 -m venv .venv
source .venv/bin/activate
python -m pip install -r requirements.txt
```

On Windows PowerShell, use `py -m venv .venv` and then
`.venv\Scripts\Activate.ps1` instead.

Configure the ground-station coordinates, serial ports, thresholds, and other
hardware-specific constants near the top of `tracker.py` before operation.

List available serial ports:

```shell
python tracker.py --list
```

Start tracking:

```shell
python tracker.py --rx COM7 --arduino COM6
```

Point the antenna at the rocket on the launch rail before starting. The first
valid GPS packet establishes the zero-yaw direction.

## Linux C controller scaffold

Build with `make -C controller` and run `make -C controller test` for the
state and Unix socket integration tests. To start with two mock receivers:

```sh
mkdir -p run
mkfifo run/rx1 run/rx2
./controller/build/controller run/dashboard.sock run/rx1 run/rx2
```

Create the FIFOs once. Each command-line receiver gets a numeric ID starting
at 1. Write Featherweight records into either FIFO. A new `@` finishes the
preceding record, so append a final `@` when replaying a finite input file.

One local dashboard client can connect to the socket. It receives a JSON
snapshot on connection and on updates. Send one quoted command per line
(`"START_MANUAL"`, `"START_AUTOMATIC"`, or `"ARM"`). Each reply includes
`accepted` and the resulting state. Acquisition, tracking, and landing are
scaffolded for future implementation. The Python web dashboard currently
runs its own simulation.

## Dashboard simulation

Install the requirements, then start the local dashboard:

```shell
python -m pip install -r requirements.txt
python dashboard.py
```

The dashboard opens automatically at `http://127.0.0.1:8000` and runs a short,
repeating simulated flight based on the TeleMega workbook data. It shows live
telemetry without opening serial ports or sending commands to the turret. Stop
the server with `Ctrl+C`.

## Arduino

Open `antenna_controller.ino` in the Arduino IDE, select the target board and
port, and upload it. Review the pin assignments, offsets, directions, movement
speed, and mechanical limits before powering the servos.

## Current limitation

The tracker currently writes CSV logs to the Windows path configured directly
in `tracker.py`. That behavior is preserved from the latest source snapshot.

## History

The Git history was reconstructed from four source snapshots. They represent,
in order: yaw-only tracking, live elevation tracking, CSV telemetry logging,
and GPS outlier rejection.
