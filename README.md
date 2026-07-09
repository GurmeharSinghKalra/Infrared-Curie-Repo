# Infrared Curie

Infrared Curie is an ESP32-based expressive robot platform with:

- dual OLED eyes
- dual 8x8 MAX7219 mouth
- four DC drive motors
- shoulder servos
- AP-first local dashboard
- handheld ESP32 controller over BLE

Legacy code in this repository is from Google Antigravity; the current V6 robot/controller targets and release bundle were added by Codex.

## Current primary firmware targets

### Robot

- source: `firmware/v6_doit30_clean_rewrite`
- board: `DOIT ESP32 DevKit V1 (30-pin)`
- framework: `ESP-IDF 5.5.x`

### Controller

- source: `remote_controller`
- board: `ESP32 DevKit V1 (30-pin)`
- framework: `ESP-IDF 5.5.x`
- transport: BLE

## Current release artifacts

Prebuilt binaries and flashing scripts live in:

- `artifacts/release_bundle`
- `artifacts/curie_dual_esp32_flash_bundle.zip`

## Main features

- AP-first robot networking
- local dashboard at `http://192.168.4.1`
- WebSocket command channel
- OTA update endpoint
- BLE controller receiver
- dual-eye + eyebrow + mouth face engine
- paged controller expression input
- kid-facing controller OLED UI with auto-detected SSD1306 address support

## Repository structure

- `firmware/`
  - robot firmware targets
- `remote_controller/`
  - final handheld BLE controller firmware
- `artifacts/`
  - release binaries and flash bundles
- `docs/`
  - pinouts, flashing guide, and technical notes

## Start here

- robot pinout: `docs/ROBOT_PINOUT.md`
- controller pinout: `docs/CONTROLLER_PINOUT.md`
- flashing guide: `docs/FLASHING_GUIDE.md`
- current robot firmware notes: `firmware/v6_doit30_clean_rewrite/README.md`

## Controller overview

- joystick = analog drive
- D-pad = digital drive override
- hold joystick press + D-pad = arm/safety actions
- keypad = expression pages
- OLED = connection, expression, speed, and action feedback

## Robot build

```powershell
$env:IDF_TOOLS_PATH='C:\Espressif'
$env:IDF_PYTHON_ENV_PATH='C:\Espressif\python_env\idf5.5_py3.11_env'
& 'C:\Users\ameri\esp\v5.5.2\esp-idf\export.ps1'
cd firmware/v6_doit30_clean_rewrite
idf.py build
```

## Robot flash

```powershell
$env:IDF_TOOLS_PATH='C:\Espressif'
$env:IDF_PYTHON_ENV_PATH='C:\Espressif\python_env\idf5.5_py3.11_env'
& 'C:\Users\ameri\esp\v5.5.2\esp-idf\export.ps1'
cd firmware/v6_doit30_clean_rewrite
idf.py -p COM5 flash monitor
```
