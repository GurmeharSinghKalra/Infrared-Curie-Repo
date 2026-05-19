# Curie Robot Pinout

Current robot firmware target:

- `firmware/v6_doit30_clean_rewrite`
- Board: `DOIT ESP32 DevKit V1 (30-pin)`
- Chip: `ESP32-D0WD-V3`

## Drive orientation

Physical motor orientation is fixed as:

- `M1 = front-left`
- `M2 = front-right`
- `M3 = rear-left`
- `M4 = rear-right`

The tank-drive logic groups:

- `M1 + M3` as the left side
- `M2 + M4` as the right side

## ESP32 GPIO map

### Motor driver outputs

| Motor | RPWM | LPWM | Physical position |
|---|---:|---:|---|
| `M1` | `GPIO25` | `GPIO26` | front-left |
| `M2` | `GPIO27` | `GPIO14` | front-right |
| `M3` | `GPIO13` | `GPIO16` | rear-left |
| `M4` | `GPIO17` | `GPIO4` | rear-right |

### Shoulder servos

| Function | GPIO |
|---|---:|
| Left shoulder servo | `GPIO12` |
| Right shoulder servo | `GPIO2` |

### OLED eyes

The firmware uses two independent I2C buses.

| Display | I2C port | SDA | SCL | Address |
|---|---:|---:|---:|---:|
| Left eye OLED | `0` | `GPIO21` | `GPIO22` | `0x78` |
| Right eye OLED | `1` | `GPIO32` | `GPIO33` | `0x78` |

### MAX7219 mouth

| Signal | GPIO |
|---|---:|
| `MOSI / DIN` | `GPIO23` |
| `CLK` | `GPIO18` |
| `CS` | `GPIO5` |
| `MISO` | not used |

The current mouth hardware is a dual 8x8 daisy-chained MAX7219 assembly.

## Network defaults

| Setting | Value |
|---|---|
| AP SSID | `Infrared Curie Setup` |
| AP password | open |
| AP IP | `192.168.4.1` |
| AP channel | `1` |

## Important hardware notes

- `GPIO12` and `GPIO2` are ESP32 strapping pins.
- `GPIO4` and `GPIO5` are also boot-sensitive on many ESP32 boards.
- If boot becomes unreliable, first suspect external circuitry on:
  - `GPIO12`
  - `GPIO2`
  - `GPIO4`
  - `GPIO5`

## Minimal wiring view

```text
ESP32 ROBOT

MOTOR DRIVER INPUTS
  M1_RPWM -> GPIO25
  M1_LPWM -> GPIO26
  M2_RPWM -> GPIO27
  M2_LPWM -> GPIO14
  M3_RPWM -> GPIO13
  M3_LPWM -> GPIO16
  M4_RPWM -> GPIO17
  M4_LPWM -> GPIO4

SERVOS
  Left shoulder  -> GPIO12
  Right shoulder -> GPIO2

LEFT EYE OLED
  SDA -> GPIO21
  SCL -> GPIO22

RIGHT EYE OLED
  SDA -> GPIO32
  SCL -> GPIO33

MAX7219 MOUTH
  DIN -> GPIO23
  CLK -> GPIO18
  CS  -> GPIO5
```
