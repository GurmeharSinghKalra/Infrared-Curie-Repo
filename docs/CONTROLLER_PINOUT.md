# Curie BLE Controller Pinout

Current controller firmware source:

- `remote_controller/main/main.c`
- `remote_controller/main/keypad.c`
- `remote_controller/main/oled_ui.c`

Controller target:

- `ESP32 DevKit V1`
- `30-pin board`
- transport: `BLE`
- framework: `ESP-IDF 5.5.x`

This controller does not join the robot Wi-Fi. It scans for the robot's BLE service and sends 8-byte control packets continuously after it connects.

## Controller layout

- `4x4 membrane keypad` = expressions and face actions
- `left joystick` = analog drive
- `left joystick press` = speed / combo modifier / clear estop
- `diamond buttons` = shoulder/home/estop combo inputs
- `OLED` = connection state, selected expression, speed mode, and action feedback

## Left joystick

| Function | ESP32 GPIO | Notes |
|---|---:|---|
| `JOY_X` | `32` | ADC input |
| `JOY_Y` | `33` | ADC input |
| `JOY_SW` | `25` | `INPUT_PULLUP` |

### Joystick behavior

- move joystick = proportional drive
- short press on `JOY_SW` = cycle speed mode
- long press on `JOY_SW` = clear estop
- hold `JOY_SW` and use the D-pad = arm/safety combo actions

## Diamond buttons

Physical arrangement:

```text
      UP
   LEFT RIGHT
     DOWN
```

| Position | GPIO | Normal action |
|---|---:|---|
| `UP` | `26` | combo input |
| `RIGHT` | `27` | combo input |
| `DOWN` | `21` | combo input |
| `LEFT` | `22` | combo input |

### Joystick-button combo actions

Hold `JOY_SW`, then press a D-pad button:

| Combo | Action |
|---|---|
| `JOY_SW + UP` | shoulders up |
| `JOY_SW + DOWN` | shoulders down |
| `JOY_SW + LEFT` | arm home |
| `JOY_SW + RIGHT` | estop |

Wire each D-pad button from GPIO to `GND` and use the internal pull-up.

## OLED display

| Function | GPIO |
|---|---:|
| `SDA` | `13` |
| `SCL` | `14` |
| `VCC` | `3V3` |
| `GND` | `GND` |

OLED notes:

- firmware auto-detects SSD1306 at `0x3C` or `0x3D`
- screen shows `Finding robot`, `Connecting`, `Robot ready`
- shows current face label and speed label
- shows action overlays like `GO!`, `BLINK`, `HOME`, `ARMS UP`

## Battery sense

| Function | GPIO | Status |
|---|---:|---|
| `Battery ADC` | `34` | reserved, disabled until divider hardware is finalized |

## 4x4 membrane keypad wiring

### Key layout

| Row/Col | C1 | C2 | C3 | C4 |
|---|---|---|---|---|
| R1 | `1` | `2` | `3` | `A` |
| R2 | `4` | `5` | `6` | `B` |
| R3 | `7` | `8` | `9` | `C` |
| R4 | `*` | `0` | `#` | `D` |

### ESP32 connections

| Keypad line | GPIO |
|---|---:|
| `R1` | `19` |
| `R2` | `18` |
| `R3` | `5` |
| `R4` | `17` |
| `C1` | `16` |
| `C2` | `4` |
| `C3` | `2` |
| `C4` | `15` |

## Expression pages

The controller uses a paged keypad model.

### Page 0: core expressions

| Key | Expression |
|---|---|
| `1` | happiness |
| `2` | sadness |
| `3` | anger |
| `4` | fear |
| `5` | disgust |
| `6` | confused |
| `7` | contempt |
| `8` | thoughtful |
| `9` | shy |
| `A` | funny |
| `B` | surprised |
| `C` | excited |

### Page 1: special faces

| Key | Expression |
|---|---|
| `1` | sleep |
| `2` | scan |
| `3` | love |
| `4` | wink |
| `5` | thoughtful |
| `6` | funny |
| `7` | surprised |
| `8` | excited |
| `9` | fear |
| `A` | disgust |
| `B` | confused |
| `C` | contempt |

### Utility keys

| Key | Action |
|---|---|
| `0` | neutral face |
| `*` | blink |
| `#` | random core expression |
| `D` | toggle expression page |

## Practical notes

- `GPIO2`, `GPIO4`, `GPIO5`, and `GPIO15` are boot-sensitive on ESP32.
- this keypad mapping works for a passive membrane keypad, but if boot becomes unreliable these pins are the first thing to revisit
- keep all grounds common
- use `3.3V` for joystick and OLED

## Minimal wiring view

```text
ESP32 CONTROLLER

4x4 MEMBRANE KEYPAD
  R1 -> GPIO19
  R2 -> GPIO18
  R3 -> GPIO5
  R4 -> GPIO17
  C1 -> GPIO16
  C2 -> GPIO4
  C3 -> GPIO2
  C4 -> GPIO15

LEFT JOYSTICK
  VRx -> GPIO32
  VRy -> GPIO33
  SW  -> GPIO25
  VCC -> 3V3
  GND -> GND

DIAMOND BUTTONS
  UP    -> GPIO26 -> button -> GND
  RIGHT -> GPIO27 -> button -> GND
  DOWN  -> GPIO21 -> button -> GND
  LEFT  -> GPIO22 -> button -> GND

OLED
  SDA -> GPIO13
  SCL -> GPIO14
  VCC -> 3V3
  GND -> GND

BATTERY ADC
  ADC -> GPIO34
```
