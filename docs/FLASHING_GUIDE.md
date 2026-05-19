# Curie Flashing Guide

This repository currently ships two main firmware targets:

- robot firmware
- controller firmware

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

## Controller firmware

Primary file:

- `artifacts/release_bundle/controller/curie_controller_full_4mb.bin`

Windows command:

```bat
py -m esptool --chip esp32 -p COM16 -b 115200 --before default-reset --after hard-reset write-flash 0x0 "C:\path\to\curie_dual_esp32_flash_bundle\controller\curie_controller_full_4mb.bin"
```

After flashing the controller:

1. Power the robot first.
2. Power the controller second.
3. Reset the controller once if you need to see boot logs.
4. Open serial monitor at `115200` to verify:

```text
Curie controller boot
ESP-NOW controller ready on channel 1
```

## Controller self-test firmware

Use this if you want to validate the radio link without wiring the controller hardware.

File:

- `artifacts/controller_self_test_bundle/curie_controller_self_test_full_4mb.bin`

It automatically sends a scripted sequence:

- expressions
- blink
- shoulder up/down
- home
- forward
- stop
- estop
- clear estop

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
