# Curie ESP32 Firmware Bundle

This bundle contains two production firmware images and one controller test image.

## Included binaries

### Robot

- `robot\curie_robot_full_4mb.bin`

### Controller

- `controller\curie_controller_full_4mb.bin`

### Controller self-test

This file is kept outside the release bundle in the repository:

- `artifacts/controller_self_test_bundle/curie_controller_self_test_full_4mb.bin`

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
- keeps AP active for direct phone/laptop control
- exposes WebSocket control and OTA update path
- enables ESP-NOW controller reception after startup

## Controller behavior

- does not join the robot AP
- uses ESP-NOW on channel `1`
- keypad selects expression pages
- joystick drives the robot
- diamond buttons control shoulders, home, and estop

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

### Controller

```bat
py -m esptool --chip esp32 -p COM6 -b 115200 --before default-reset --after hard-reset write-flash 0x0 controller\curie_controller_full_4mb.bin
```

## After flashing the robot

1. Power the robot.
2. Connect a phone or laptop to `Infrared Curie Setup`.
3. Open `http://192.168.4.1`.

## After flashing the controller

1. Power the robot first.
2. Power the controller second.
3. Reset the controller once if you need to see boot logs.

Expected controller boot log:

```text
Curie controller boot
ESP-NOW controller ready on channel 1
```

## Manual component binaries

### Robot

- `robot\bootloader.bin`
- `robot\partition-table.bin`
- `robot\ota_data_initial.bin`
- `robot\curie_robot_app.bin`

### Controller

- `controller\bootloader.bin`
- `controller\partitions.bin`
- `controller\curie_controller_app.bin`
