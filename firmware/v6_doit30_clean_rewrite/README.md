# Curie V6 DOIT DevKit V1 Rewrite

ESP-IDF 5.5.x firmware target for the Curie robot on a DOIT ESP32 DevKit V1 (30-pin).

## Board profile

- Board: DOIT ESP32 DevKit V1 (30-pin)
- Chip: ESP32-D0WD-V3
- Flash: 4 MB
- Console UART: GPIO1 TX0, GPIO3 RX0

## Pin map

- Motor M1: GPIO25 / GPIO26
- Motor M2: GPIO27 / GPIO14
- Motor M3: GPIO13 / GPIO16
- Motor M4: GPIO17 / GPIO4
- Left shoulder servo: GPIO19
- Right shoulder servo: GPIO2
- Left OLED: I2C0 SDA GPIO21, SCL GPIO22
- Right OLED: I2C1 SDA GPIO32, SCL GPIO33
- MAX7219 mouth: MOSI GPIO23, CLK GPIO18, CS GPIO5

## Important hardware assumptions

- GPIO2 is still a strapping pin.
- GPIO4 and GPIO5 are also sensitive at boot on many ESP32 designs.
- This target preserves the existing robot wiring except for the left shoulder servo, which was moved off GPIO12 to GPIO19.
- If boot behavior is flaky, first suspect GPIO2, GPIO4, and GPIO5.

## Network behavior

- AP-first boot
- AP SSID: `Infrared Curie Setup`
- AP IP: `192.168.4.1`
- Open AP for compatibility-first provisioning
- Dashboard served locally over HTTP
- WebSocket control channel at `/ws`
- OTA endpoint at `/update`
- If STA credentials are saved via `/api/wifi/connect`, the board keeps the local AP alive and starts a STA uplink without leaving AP mode
- BLE controller receiver starts after robot boot and re-advertises on stale-link recovery

## Build

```powershell
$env:IDF_TOOLS_PATH='C:\Espressif'
$env:IDF_PYTHON_ENV_PATH='C:\Espressif\python_env\idf5.5_py3.11_env'
& 'C:\Users\ameri\esp\v5.5.2\esp-idf\export.ps1'
idf.py build
```

## Flash

```powershell
$env:IDF_TOOLS_PATH='C:\Espressif'
$env:IDF_PYTHON_ENV_PATH='C:\Espressif\python_env\idf5.5_py3.11_env'
& 'C:\Users\ameri\esp\v5.5.2\esp-idf\export.ps1'
idf.py -p COM5 flash monitor
```
