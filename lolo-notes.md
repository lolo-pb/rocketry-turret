# Rocketry Turret Notes

## Current flow

```text
Featherweight receiver -> tracker.py -> Arduino -> servos
```

`tracker.py` currently reads and parses GPS packets, calculates azimuth and
elevation, filters bad data, logs telemetry, and sends `Y...` / `T...` commands.

`antenna_controller.ino` receives those commands and moves the azimuth and
elevation servos.

## Dashboard test

```text
dashboard.py simulator -> WebSocket -> web/index.html
```

Run it with `python dashboard.py`. It starts a local FastAPI server, opens the
browser, and shows a deterministic simulated flight at 10 updates per second.
The flight is a short, smoothed version of the TeleMega data in the Excel
workbook and repeats automatically.

Simulation mode does not import `tracker.py`, open serial ports, or send Arduino
commands. The existing tracker and firmware are unchanged.

The dashboard receives structured values for flight state, altitude, speed,
acceleration, yaw, tilt, distance, satellites, GPS fix, and CRC status. Later,
`tracker.py` can publish the same structure while remaining the only code that
owns the serial ports.

## Proposed direction

Move the tracking work onto the Arduino:

```text
Featherweight receiver -> Arduino -> servos
                              |
                              -> Python dashboard and logger
```

The Arduino would parse GPS packets, calculate pointing angles, reject bad
data, and drive the servos. Python would become an optional dashboard for
status, graphs, events, and CSV logging.

This would let the turret continue tracking without the computer.

## Limitations

- The current Arduino movement functions use blocking `while` loops and a
  `delay(40)` for every degree. While a servo is moving, the main loop cannot
  return to `Serial.available()` to read more data.
- Azimuth and elevation move sequentially. A long azimuth movement delays the
  elevation movement and all new commands.
- Incoming serial bytes may wait in the hardware buffer while the turret moves.
  If movement takes too long, the buffer can overflow or leave the controller
  acting on stale packets. In practice, it mainly listens while stationary.
- A direct Featherweight connection requires compatible voltage levels and a
  spare hardware serial port, or another suitable serial interface.
- The reported servo position is commanded position, not measured physical
  position. There is no encoder feedback.(Maybe inpractical to implement)

## Tracker selection and packet integrity

`GPS_STAT` packets can describe a rocket tracker (`TRK`), the ground station
itself (`GS`), or a found/lost rocket (`FND`). They also contain the device ID.
The current parser accepts all three types, then discards both the type and ID.
Tracking should eventually require `TRK` and the expected rocket ID so the
turret cannot point at the ground station or another rocket.

Packets contain a `CRC_OK` / `CRC_ERR` result and end with a separate CRC-16
checksum. The parser currently stores the first result in a variable called
`crc`, but never checks it. It also stops parsing before the final checksum and
does not calculate CRC-16 itself. As a result, even a packet already marked
`CRC_ERR` is returned as valid GPS data, used to calculate angles, and may send
commands to the turret.

The 20 km outlier filter is not a replacement for CRC validation. It only
rejects a position when its geometric jump is enormous. Corrupted data can
still produce a believable position less than 20 km away, a bad altitude or
angle, or a bad first calibration point. At minimum, reject `CRC_ERR`; ideally,
also calculate and verify the final CRC-16 checksum.

## Unused telemetry

The current `GPS_STAT` parser already extracts but discards horizontal
velocity, heading, vertical velocity, total satellite count, satellite signal
quality counts, packet type, tracker ID, and CRC status.

Take into acount for dashboard.

## IDEA

Threads could be nice but may not exist on the selected Arduino.
Use a non-blocking control loop instead?:

- Continuously poll and parse serial input.
- Store target angles without moving to them in one blocking operation.
- Use `millis()` to step each servo when its movement interval expires.
- Update azimuth and elevation independently in the same loop.
- Keep processing new packets while both servos are moving.

maybe keep a target and aproach that pid style on several passes.



## Open questions

- Which Arduino model is being used?
- How does the Featherweight receiver expose serial data, and at what voltage?
- How frequently do GPS packets arrive?(Very important)
- How fast may the turret move safely?
