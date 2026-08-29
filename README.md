# Rocketry Turret

Ground-station antenna tracker for receiving live Featherweight GPS telemetry
and commanding Arduino-controlled azimuth and elevation servos.

## Files

- `tracker.py` reads GPS packets, calculates yaw and elevation, sends serial
  commands to the Arduino, logs telemetry to CSV, and rejects large GPS jumps.
- `antenna_controller.ino` receives `Y<yaw>` and `T<tilt>` commands and moves
  the two servos.
- `requirements.txt` lists the Python dependencies.

## Python setup

Create and activate a virtual environment, then install the dependencies:

```shell
python -m venv .venv
python -m pip install -r requirements.txt
```

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
