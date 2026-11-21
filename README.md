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
`accepted` and the resulting state. Manual and automatic acquisition and landing detection remain stubs. The web dashboard reads controller snapshots.

## Looping mock receiver

With the dashboard Python dependencies installed, open two terminals in the
project folder. In the first terminal, build the controller and start it in
disarmed with a mock receiver:

```sh
make -C controller
mkdir -p run
mkfifo run/rx1
python ground-station.py run/rx1
```

In the second terminal, activate the virtual environment and start the mock:

```sh
source .venv/bin/activate
python mock_receiver.py run/rx1
```

Open the dashboard and select **Start tracking**. The mock sends the simulated
flight as GPS packets and repeats it
until stopped with Ctrl+C. The first packet establishes the zero direction.
The controller prints commanded turret angles as it moves. Servo output is
simulated unless the PWM environment variables below are configured. The web dashboard receives the controller's GPS data. Other dashboard values
are zero until the controller supplies them.

## Raspberry Pi Zero W tracking output

Build on the Pi with `make -C controller`. Pass receiver device paths such as
`/dev/serial/by-id/...` in the same positions as the mock FIFOs. The controller
configures serial devices for 115200 baud, 8N1. In tracking mode it uses fresh
Featherweight `CRC_OK TRK` records with a 3D fix. If multiple Featherweight
inputs supply a fix on the same loop pass, the first takes priority.

Prefix TeleDongle device paths with `teledongle:`. Other paths, including
the mock FIFO, are Featherweight inputs. For example:

```sh
python ground-station.py /dev/serial/by-path/FEATHERWEIGHT teledongle:/dev/serial/by-path/TELEDONGLE
```

Replace the example paths with the paths shown on your Pi. Configure the
TeleDongle radio frequency and telemetry mode separately. The TeleMega v5
parser logs recognized telemetry but does not control the turret.

The controller creates `logs/raw-*.csv` and `logs/parsed-*.csv` for each
session. Raw entries contain exact USB read bytes encoded as hex, including
unrecognized packets. Parsed entries contain the recognized fields. The source
includes the receiver number and each entry has a Pi receive timestamp.
A logger thread writes buffered records independently of the control loop.
If the queue fills or is busy, entries are dropped and the total is reported
on shutdown. Use `--log-dir PATH` with `ground-station.py` to choose the
destination. The hardware stream and TeleMega decoding still need verification
against captures from the actual devices.

Set the station coordinates in `controller/main.c` for your site.
Elevation uses GPS altitude and station altitude in the same reference frame.
Point the calibrated zero direction toward the rocket before tracking. The
first valid tracking fix establishes the horizontal zero bearing. Tracking
updates move each commanded axis by up to one degree every 40 ms.

For hardware output, configure the Pi's boot settings with
`dtoverlay=pwm-2chan,pin=12,func=4,pin2=13,func2=4` and disable competing
PWM users such as onboard analog audio. Reboot and identify the corresponding
`/sys/class/pwm/pwmchipN`. Export channels 0 and 1 through its `export`
attribute and give the controller write access to both channels. Channel 0
drives azimuth on BCM GPIO12 (header pin 32). Channel 1 drives elevation on
BCM GPIO13 (header pin 33). Confirm these pins are available on your hub HAT.
Use a servo supply rated for both motors and connect its ground to Pi ground.

Set these environment variables before starting the controller:

| Variable | Value |
| --- | --- |
| `TURRET_PWM_CHIP` | Path to the chip with exported `pwm0` and `pwm1` |
| `TURRET_AZ_PULSE_US` | Azimuth pulse widths at 0° and 270°, as `min:max` in microseconds |
| `TURRET_EL_PULSE_US` | Elevation pulse widths in the same format |

The driver assumes positional servos with 270° calibrated travel and a 20 ms
period. Verify the pulse range, travel, period, and 3.3 V signal compatibility
for the actual servos. With `TURRET_PWM_CHIP` unset, the controller runs
with simulated servo output. During tracking, the driver prints PWM write errors to standard error and continues. Shutdown disables channels enabled by the controller.

## Ground station dashboard

Start the controller and web server:

```shell
python ground-station.py
```

Open `http://<pi-ip-address>:8000` on a device in the same network. Pass
receiver paths as arguments. Add `--tracking` to start directly in tracking
mode. The looping mock receiver instructions above provide simulated GPS.
Stop the ground station with `Ctrl+C`.

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
