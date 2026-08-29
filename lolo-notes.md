# Rocketry Turret Notes

## Current flow

```text
Featherweight receiver -> tracker.py -> Arduino -> servos
```

`tracker.py` currently reads and parses GPS packets, calculates azimuth and
elevation, filters bad data, logs telemetry, and sends `Y...` / `T...` commands.

`antenna_controller.ino` receives those commands and moves the azimuth and
elevation servos.

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
