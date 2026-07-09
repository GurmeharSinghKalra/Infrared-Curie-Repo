# Curie V4 Custom 38-pin 4-driver Firmware

ESP-IDF firmware target for the custom robot controller PCB using a 38-pin ESP32 DevKit layout.

## Hardware Pinout

### Motor Drivers

Each BTS7960 has `R_EN` and `L_EN` tied high on the PCB, so firmware only drives `RPWM` and `LPWM`.

| Driver | Signal | ESP32 GPIO |
| --- | --- | --- |
| M1 | RPWM | 25 |
| M1 | LPWM | 26 |
| M2 | RPWM | 27 |
| M2 | LPWM | 14 |
| M3 | RPWM | 13 |
| M3 | LPWM | 16 |
| M4 | RPWM | 17 |
| M4 | LPWM | 4 |

### Servos

| Servo | ESP32 GPIO |
| --- | --- |
| Left Shoulder | 12 |
| Right Shoulder | 2 |

Elbow servos were removed from the final robot direction. Firmware keeps the old elbow fields in telemetry only for dashboard compatibility; the motion task drives shoulders only.

### Displays

| Device | Signal | ESP32 GPIO |
| --- | --- | --- |
| Left OLED | SDA | 21 |
| Left OLED | SCL | 22 |
| Right OLED | SDA | 32 |
| Right OLED | SCL | 33 |
| MAX7219 Mouth | DIN | 23 |
| MAX7219 Mouth | CLK | 18 |
| MAX7219 Mouth | CS | 5 |

## Control Inputs

This build supports both control paths at the same time:

| Source | Transport | Behavior |
| --- | --- | --- |
| Web dashboard | WiFi + WebSocket JSON | Existing command surface remains supported |
| Physical controller | ESP-NOW packet v1 | Controller motion/arm commands win while packets are active |

ESP-NOW controller timeout is 500 ms. On timeout the robot stops drive output, holds shoulders, and shows `EXP_LOST`.

### ESP-NOW Controller Packet v1

Packet size is 8 bytes. Checksum is XOR of bytes 0 through 6.

| Byte | Field | Value |
| --- | --- | --- |
| 0 | Magic | `0xC4` |
| 1 | Throttle | `0` forward, `128` center, `255` backward |
| 2 | Steering | `0` left, `128` center, `255` right |
| 3 | Buttons | Bitfield below |
| 4 | Expression | `0` no change, `1-12` expression slots |
| 5 | Speed mode | `0` 40%, `1` 70%, `2` 100% |
| 6 | Sequence | Wraparound counter |
| 7 | Checksum | XOR bytes `0-6` |

Button bitfield:

| Bit | Meaning |
| --- | --- |
| 0 | E-STOP latch |
| 1 | Home: stop, shoulders home, neutral face |
| 2 | Shoulder up |
| 3 | Shoulder down |
| 4 | Clear E-STOP |

Current assumed tank-side mapping is M1 + M3 as left side and M2 + M4 as right side.

## Build

```bash
idf.py build
```

## Flash

```bash
idf.py -p <PORT> flash monitor
```

## Boot-sensitive Pins

This layout uses several ESP32 strapping pins because the direct-connected hardware is pin-heavy: `GPIO2`, `GPIO4`, `GPIO5`, `GPIO12`, and `GPIO15`.
Make sure the PCB does not force invalid boot levels on these pins during reset.
