# Curie Controller Pinout

This document describes the revised handheld controller firmware and wiring.

- Source: [remote_controller.ino](</C:/Users/ameri/Documents/New project/Infrared-Curie-Repo/remote_controller/remote_controller.ino>)
- Controller MCU: ESP32 DevKit V1, 30-pin
- Transport: ESP-NOW on channel `1`
- Robot dashboard remains on the robot AP:
  - SSID: `Infrared Curie Setup`
  - URL: `http://192.168.4.1`

## Controller layout

- `4x4 membrane keypad`: expressions
- `Left joystick`: robot drive
- `Diamond 4 buttons`: shoulders and safety actions
- `Left joystick press`: speed mode / clear estop

## Left joystick

| Function | ESP32 GPIO | Notes |
|---|---:|---|
| `JOY_X` | `32` | ADC input |
| `JOY_Y` | `33` | ADC input |
| `JOY_SW` | `25` | Digital input, `INPUT_PULLUP` |

### Left joystick behavior

- `X/Y` drive the robot as tank-mixed throttle + steering
- short press on `JOY_SW`: cycles speed cap
  - `40%`
  - `70%`
  - `100%`
- long press on `JOY_SW` for about `1.2 s`: clear estop

## Diamond buttons

Recommended physical arrangement:

```text
      UP
   LEFT RIGHT
     DOWN
```

| Position | ESP32 GPIO | Action |
|---|---:|---|
| `UP` | `26` | shoulders up while held |
| `RIGHT` | `27` | emergency stop while held |
| `DOWN` | `21` | shoulders down while held |
| `LEFT` | `22` | arm home / neutral reset |

Wire each button from GPIO to `GND` and use the internal pull-up.

## 4x4 membrane keypad

### Key layout

| Row/Col | C1 | C2 | C3 | C4 |
|---|---|---|---|---|
| R1 | `1` | `2` | `3` | `A` |
| R2 | `4` | `5` | `6` | `B` |
| R3 | `7` | `8` | `9` | `C` |
| R4 | `*` | `0` | `#` | `D` |

### ESP32 connections

| Keypad line | ESP32 GPIO |
|---|---:|
| `R1` | `19` |
| `R2` | `18` |
| `R3` | `5` |
| `R4` | `17` |
| `C1` | `16` |
| `C2` | `4` |
| `C3` | `2` |
| `C4` | `15` |

## Keypad expression mapping

| Key | Action |
|---|---|
| `1` | happy |
| `2` | neutral |
| `3` | sad |
| `4` | wink |
| `5` | love |
| `6` | angry |
| `7` | sleep |
| `8` | scan |
| `9` | surprise |
| `0` | curious |
| `A` | excited |
| `B` | confused |
| `C` | lost |
| `D` | custom |
| `*` | blink |
| `#` | neutral reset |

## Practical notes

- GPIO `2`, `4`, `5`, and `15` are boot-sensitive on ESP32. This keypad mapping is acceptable for a passive membrane keypad, but if boot becomes flaky, move the keypad to safer GPIOs first.
- Keep all grounds common.
- Use `3.3V` for the joystick module.
- `GPIO21` and `GPIO22` are used only on the controller for buttons, not I2C.

## Minimal schematic view

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
```
