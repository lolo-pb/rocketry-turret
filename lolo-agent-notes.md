# Rocketry Turret — Lolo Agent Notes

## Current flow

Live tracker:

```text
Featherweight receiver -> tracker.py -> Arduino -> servos
```

`tracker.py` parses `GPS_STAT`, rejects large position jumps, calculates turret yaw/elevation, logs CSV, and sends `Y...` / `T...` commands. `antenna_controller.ino` applies servo offsets, direction, speed, and mechanical limits.

Dashboard test:

```text
dashboard.py simulation -> WebSocket -> web/index.html
```

Run with `python dashboard.py`. It plays a repeating TeleMega-based flight at 10 updates per second. It does not run `tracker.py`, open serial ports, or command the turret.

## Dashboard data rule

All changing measurements must come from data available to `tracker.py`, or be calculated from that data. Fields already parsed and discarded by `tracker.py` may be retained for the dashboard.

Direct Featherweight values:

- Altitude, latitude, longitude, and GPS fix.
- Rocket heading.
- Horizontal and vertical speed.
- Packet CRC/integrity.
- Packet type and tracker/vehicle ID.
- Satellite count.
- Satellite signal-band counts: 24, 32, and 40 dB.

Calculated values:

- Relative altitude: raw altitude minus configured ground-station altitude.
- Total rocket speed: horizontal and vertical speed combined.
- Mach: total speed divided by an assumed speed of sound.
- Vertical acceleration: change in vertical speed divided by elapsed time.
- G counter: vertical acceleration divided by standard gravity (`9.80665 m/s²`).
- Flight time: time since the first accepted tracker packet.
- Turret azimuth/yaw: calculated from rocket and ground-station positions.
- Turret elevation/tilt: calculated from vertical and horizontal separation.
- Target range: tracker horizontal distance, or an easily calculated 3D/slant distance.

Important caveats:

- The G counter uses vertical acceleration only. It is not true accelerometer-measured load force: **agarrarlo con pinzas**. Its real meaning is “vertical acceleration expressed in G.”
- Decide whether “Target range” means horizontal ground distance or 3D/slant distance before live integration.
- Current `KEYFRAMES` independently hardcode altitude, speeds, yaw, elevation, and range. They are not guaranteed to obey the tracker equations or agree with one another.
- Simulation/source labels and browser data-link status are dashboard state, not rocket telemetry.
- Radar mechanical sweep and limits come from `antenna_controller.ino`, not `tracker.py`.
- The elevation gauge's 0–90° range is a UI choice, not a tracker-supplied limit.

Live integration must publish one consistent telemetry object from `tracker.py`. Its parser currently returns only latitude, longitude, altitude, and fix. Heading, velocities, CRC, identity, and satellite fields are already matched but discarded.

## Tracker selection and packet integrity

`GPS_STAT` may describe a rocket tracker (`TRK`), ground station (`GS`), or found/lost rocket (`FND`), and includes the device ID. The parser currently accepts all types and discards type and ID. Live tracking should require `TRK` and the expected rocket ID.

The parser captures `CRC_OK` / `CRC_ERR` but does not check it. It also does not parse or calculate the final CRC-16 checksum. A packet marked `CRC_ERR` can currently move the turret.

The 20 km outlier filter is not a replacement for CRC validation. Reject `CRC_ERR` at minimum; ideally verify CRC-16 too.

## Proposed standalone turret direction

Possible future flow:

```text
Featherweight receiver -> Arduino -> servos
                              |
                              -> Python dashboard and logger
```

This would let the turret keep tracking without the computer. The Arduino would parse GPS, calculate pointing, reject bad data, and drive the servos. Python would provide the dashboard and logs.

Current firmware blockers:

- Servo movement uses blocking loops and a 40 ms delay per degree.
- Azimuth and elevation move sequentially, delaying new serial input.
- Long movement can overflow the serial buffer or act on stale packets.
- A direct Featherweight connection needs compatible voltage and a suitable serial port.
- Reported servo position is commanded position, not measured position; there is no encoder feedback.

A non-blocking `millis()` control loop is likely better than threads: continuously parse input, store the newest targets, and step both servos independently.

## Open questions

- Which Arduino model is being used?
- How does the Featherweight expose serial data, and at what voltage?
- How frequently do GPS packets arrive?
- How fast may the turret move safely?
