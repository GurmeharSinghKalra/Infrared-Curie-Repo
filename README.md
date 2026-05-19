# Infrared Curie

Infrared Curie is an ESP32-based expressive robot platform with:

- dual OLED eyes
- dual 8x8 MAX7219 mouth
- four DC drive motors
- shoulder servos
- AP-first local dashboard
- handheld ESP32 controller over ESP-NOW

Legacy code in this repository is from Google Antigravity; the current V6 robot/controller targets and release bundle were added by Codex.

## Current primary firmware targets

### Robot

- source: `firmware/v6_doit30_clean_rewrite`
- board: `DOIT ESP32 DevKit V1 (30-pin)`
- framework: `ESP-IDF 5.5.x`

### Controller

- source: `remote_controller/remote_controller.ino`
- self-test source: `remote_controller_self_test/remote_controller_self_test.ino`
- board: `ESP32 DevKit V1 (30-pin)`
- framework: Arduino core for ESP32

## Current release artifacts

Prebuilt binaries and flashing scripts live in:

- `artifacts/release_bundle`
- `artifacts/curie_dual_esp32_flash_bundle.zip`

## Main features

- AP-first robot networking
- local dashboard at `http://192.168.4.1`
- WebSocket command channel
- OTA update endpoint
- ESP-NOW controller receiver
- dual-eye + eyebrow + mouth face engine
- paged controller expression input
- controller self-test image for radio validation without controller hardware

## Repository structure

- `firmware/`
  - robot firmware targets
- `remote_controller/`
  - final handheld controller firmware
- `remote_controller_self_test/`
  - controller radio self-test firmware
- `artifacts/`
  - release binaries and flash bundles
- `docs/`
  - pinouts, flashing guide, and technical notes

## Start here

- robot pinout: `docs/ROBOT_PINOUT.md`
- controller pinout: `docs/CONTROLLER_PINOUT.md`
- flashing guide: `docs/FLASHING_GUIDE.md`
- current robot firmware notes: `firmware/v6_doit30_clean_rewrite/README.md`

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
