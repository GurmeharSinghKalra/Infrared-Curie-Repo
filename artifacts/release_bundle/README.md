# Curie ESP32 Firmware Bundle

This bundle contains two production firmware images.

## Included binaries

### Robot

- `robot\curie_robot_full_4mb.bin`

### BLE Controller

- `controller\curie_controller_ble_full_4mb.bin`
- `controller\curie_controller_full_4mb.bin`

Both controller files currently contain the same BLE controller image. The `ble` filename is the explicit one.

## Target hardware

Both production images target:

- `ESP32 DevKit V1`
- `30-pin board`
- `4 MB flash`

## Robot behavior

- boots AP-first
- creates Wi-Fi SSID `Infrared Curie Setup`
- AP IP `192.168.4.1`
- serves dashboard at `http://192.168.4.1`
- exposes WebSocket control and OTA update path
- exposes a BLE control service for the handheld controller

## Controller behavior

- does not join the robot AP
- scans for the robot over BLE
- joystick drives the robot
- hold joystick press + D-pad for shoulders/home/estop
- keypad selects expression pages
- optional OLED on `GPIO13/14` shows connection and actions

## Fastest Windows flash flow

Install esptool:

```bat
py -m pip install esptool
```

Use the provided scripts:

```bat
flash_robot_windows.bat COM5
flash_controller_windows.bat COM6
```

## Direct Windows commands

### Robot

```bat
py -m esptool --chip esp32 -p COM5 -b 115200 --before default-reset --after hard-reset write-flash 0x0 robot\curie_robot_full_4mb.bin
```

### BLE Controller

```bat
py -m esptool --chip esp32 -p COM6 -b 115200 --before default-reset --after hard-reset write-flash 0x0 controller\curie_controller_ble_full_4mb.bin
```

## After flashing the robot

1. Power the robot.
2. Connect a phone or laptop to `Infrared Curie Setup`.
3. Open `http://192.168.4.1`.

## After flashing the controller

1. Power the robot and controller in any order.
2. Open serial monitor at `115200` if you want diagnostics.
3. If an OLED is wired, it should show:
   - `Finding robot`
   - `Connecting`
   - `Robot ready`

## Manual component binaries

### Robot

- `robot\bootloader.bin`
- `robot\partition-table.bin`
- `robot\ota_data_initial.bin`
- `robot\curie_robot_app.bin`

### Controller

- `controller\bootloader.bin`
- `controller\partition-table.bin`
- `controller\curie_controller_app.bin`
