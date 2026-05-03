# Infrared Curie
**An Advanced ESP32-Based Robotic Controller Platform**

Infrared Curie is a highly expressive, web-controlled, and remotely operated robot built on the ESP-IDF framework. It features dual OLED eyes, a MAX7219 LED matrix mouth, a 4-servo robotic arm system, and a robust dual-mode WiFi stack (Access Point + Station mode) with real-time WebSocket telemetry.

## Features
- **Expressive Faces**: 8 built-in expressions (Happy, Sad, Angry, Wink, etc.) using dual I2C OLED screens.
- **Custom LED Matrix Designer**: A real-time WebSocket-synced 16x8 matrix designer in the Web UI.
- **Over-The-Air (OTA) Updates**: Flash new `.bin` firmware directly through the browser.
- **Dual Mode WiFi**: Operates as a standalone Access Point out of the box, with the ability to scan and connect to local WiFi networks.
- **Physical Remote Support**: Supports an external ESP32 physical remote controller via WebSockets.
- **Dual-Architecture Firmware**:
  - `v2_architecture_4_driver`: Legacy code supporting 4 independent motor drivers (Holonomic).
  - `v3_architecture_dual_driver`: Current stable code optimized for 2 BTS7960 drivers (Differential Tank Steer).

## Repository Structure
- `docs/`: Comprehensive technical documentation, code explanations, and development history.
- `firmware/`: The ESP-IDF C codebase (split by V2 and V3 architectures).
- `dashboard_ui/`: The built React/Vite source assets embedded into the ESP32.
- `remote_controller/`: The Arduino sketch for the standalone physical remote.

## Getting Started
To flash the firmware to an ESP32, you will need the ESP-IDF v5.2 environment:
```bash
cd firmware/v3_architecture_dual_driver
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

For full documentation, please refer to the `docs/` folder.
