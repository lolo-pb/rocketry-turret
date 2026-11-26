# Rocketry Turret — Lolo Agent Notes

## TODO

- [/] `controller/manual.c` — manual acquisition
- [ ] `controller/automatic.c` — automatic acquisition
- [/] `controller/tracker.c` — locked tracking

- [ ] REview reciever logic. maybe a khalman filter

- [ ] Read live USB receivers on the Pi.
- [ ] Validate packet integrity and tracker ID.

- [X] Connect the dashboard to the controller socket.
- [X] Serve the dashboard headlessly on the private network.

- [ ] Add Commanding and Viewing roles for multiple laptops.
- [ ] Make the top nav actually show state, current mode, connection and potential errors, like are the servos initiated and that

- [ ] Define landing detection and show the estimated landing position.

- [/] Joystick with the arduino nano to usb to the laptop.
- [ ] Add a Sig-Strength thing to the manual calibration screen 

- [ ] Make the pi host its own network

## Things to keep in mind

How the controller connects to the usb recievers,
How the controller reads fro the recievers and not make that blocking,<Importan!/>
We need to identify different recievers in the different usb ports.<Importan!/>
Logging needs to be non blocking.
We have 2 loggings, the raw and parse. if logging proves slow or problematic we might want to remove one.
The Featherweight manual labels GPS_STAT altitude ASL in feet, and the parser converts it to meters


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
