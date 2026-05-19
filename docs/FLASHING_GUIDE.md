# Curie Flashing Guide

This repository currently ships two main firmware targets:

- robot firmware
- BLE controller firmware

Release-ready binaries are packaged in:

- `artifacts/release_bundle`
- `artifacts/curie_dual_esp32_flash_bundle.zip`

## Board assumptions

Both images target:

- `ESP32 DevKit V1`
- `30-pin board`
- `4 MB flash`

## Robot firmware

Primary file:

- `artifacts/release_bundle/robot/curie_robot_full_4mb.bin`

Windows command:

```bat
py -m esptool --chip esp32 -p COM16 -b 115200 --before default-reset --after hard-reset write-flash 0x0 "C:\path\to\curie_dual_esp32_flash_bundle\robot\curie_robot_full_4mb.bin"
```

After flashing the robot:

1. Power the robot.
2. Connect a phone or laptop to `Infrared Curie Setup`.
3. Open `http://192.168.4.1`.

## BLE controller firmware

Primary file:

- `artifacts/release_bundle/controller/curie_controller_ble_full_4mb.bin`

Windows command:

```bat
py -m esptool --chip esp32 -p COM16 -b 115200 --before default-reset --after hard-reset write-flash 0x0 "C:\path\to\curie_dual_esp32_flash_bundle\controller\curie_controller_ble_full_4mb.bin"
```

After flashing the controller:

1. Power the robot first or second; the BLE controller now rescans and recovers either way.
2. Power the controller.
3. Open serial monitor at `115200` if you want diagnostics.
4. Expected boot logs include:

```text
Starting Curie BLE Controller (Native C ESP-IDF)...
NimBLE host task started
```

5. If an OLED is wired on `GPIO13/14`, the controller should show:
   - `Finding robot`
   - `Connecting`
   - `Robot ready`

## Controller controls

- joystick = analog drive
- short joystick press = speed mode
- long joystick press = clear estop
- hold joystick press + D-pad:
  - up = arms up
  - down = arms down
  - left = home
  - right = estop
- keypad = expressions

## If flashing fails

Check:

- correct COM port
- `py -m pip install esptool`
- USB cable supports data
- board is an ESP32 DevKit V1 class board

Quick port test:

```bat
py -m esptool --chip esp32 -p COM16 chip-id
```
