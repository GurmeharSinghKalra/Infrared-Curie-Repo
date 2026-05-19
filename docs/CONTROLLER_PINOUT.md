# Curie Controller Pinout

Current controller firmware sources:

- `remote_controller/remote_controller.ino`
- `remote_controller_self_test/remote_controller_self_test.ino`

Controller target:

- `ESP32 DevKit V1`
- `30-pin board`
- transport: `ESP-NOW`
- radio channel: `1`

The controller does not join the robot AP. It sends radio packets directly to the robot.

## Controller layout

- `4x4 membrane keypad` = expression selection
- `left joystick` = drive
- `diamond buttons` = shoulders + safety
- `left joystick press` = speed mode / clear estop

## Left joystick

| Function | ESP32 GPIO | Notes |
|---|---:|---|
| `JOY_X` | `32` | ADC input |
| `JOY_Y` | `33` | ADC input |
| `JOY_SW` | `25` | `INPUT_PULLUP` |

### Left joystick behavior

- joystick drives the robot continuously
- short press on `JOY_SW` cycles speed mode
- long press on `JOY_SW` clears estop

## Diamond buttons

Physical arrangement:

```text
      UP
   LEFT RIGHT
     DOWN
```

| Position | GPIO | Action |
|---|---:|---|
| `UP` | `26` | shoulder up while held |
| `RIGHT` | `27` | emergency stop |
| `DOWN` | `21` | shoulder down while held |
| `LEFT` | `22` | arm home |

Wire each button from GPIO to `GND` and use the internal pull-up.

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

### Page 1: special expressions

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
- This keypad mapping is acceptable for a passive membrane keypad, but if boot becomes unreliable, these are the first pins to revisit.
- Keep all grounds common.
- Use `3.3V` for the joystick.

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
```
