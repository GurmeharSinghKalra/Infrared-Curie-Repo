# Infrared Curie: Troubleshooting & Development History

This document tracks the major roadblocks faced during the development of Infrared Curie, how they were mitigated, features that were removed, and roadmap items to be added back.

## 🔴 Failed Features & Critical Bugs

### 1. Dual-Driver Hardware Overload
**The Problem**: The original V2 architecture supported 4 independent motor drivers for holonomic movement. Due to space/cost constraints, the hardware was modified to use only 2 drivers (Left side and Right side). Two motors were wired in parallel to a single BTS7960 driver.
**The Failure**: When the firmware sent mismatched or rapid PWM signals to the 2-driver setup, the motors drew massive stall currents, tripping power protection and rendering the robot immobile. Additionally, the left-side pins (25/26) were physically swapped, causing direction conflicts.
**The Fix**: Migrated to the **V3 Dual Driver** architecture. The code was rewritten to treat the robot strictly as a Differential Drive (Tank Steer) platform. Pins 25 and 26 were swapped in software, and PWM channels were safely consolidated.

### 2. The "Dangling WebSocket" Kernel Panic
**The Problem**: Switching the robot from Access Point mode to Station Mode (connecting to a home router) causes the ESP32 to restart its HTTP server.
**The Failure**: If the UI Dashboard was open, the telemetry engine would try to send JSON data to a WebSocket client that had just been forcefully disconnected by the network restart, causing a `StoreProhibited` fatal exception (Core Panic).
**The Fix**: Added safe deregistration logic. When `stop_http_server()` is called during a network transition, `telemetry_deregister_ws()` immediately nulls out the active client pointer, preventing the background task from accessing dead memory.

### 3. WiFi Scan Crash in STA Mode
**The Problem**: Users opening the WiFi Setup menu while the robot was already connected to their home network triggered a background WiFi scan.
**The Failure**: `esp_wifi_scan_start()` was wrapped in `ESP_ERROR_CHECK()`. Scanning while actively transmitting in STA mode sometimes throws transient errors, causing the ESP_ERROR_CHECK to forcefully abort and reboot the entire robot.
**The Fix**: Removed `ESP_ERROR_CHECK` from the scan handler. It now gracefully catches the error, logs a warning, and returns an empty JSON array `[]` to the UI without crashing.

## ❌ Removed Features

1. **Holonomic / Mecanum Movement**:
   - Because we downgraded from 4 independent drivers to 2 shared drivers, diagonal movement and strafing are no longer physically possible. The `DIR_FWD_LEFT` and `DIR_BWD_RIGHT` commands have been simplified to soft-turns in the V3 architecture.
   - *Note: The V2 firmware is archived in this repository if the 4-driver hardware is ever restored.*

2. **SPIFFS Web Hosting**:
   - Initially, the React UI was going to be hosted from the ESP32's SPIFFS filesystem. However, uploading large UI bundles over serial was slow and error-prone.
   - We removed SPIFFS dependency. The UI is now embedded directly into the C-binary via CMake (`EMBED_FILES`), ensuring the dashboard is always perfectly synchronized with the firmware version.

## 🟢 Features to be Added / Restored

1. **Camera Feed Integration**:
   - The UI has a placeholder for a video feed. Future hardware iterations should explore adding an ESP32-CAM module, streaming over a separate port to the dashboard.
2. **Restore 4-Driver Support**:
   - If a custom PCB (like the Moveo HAT) is designed for Curie, the 4 BTS7960 drivers should be restored to re-enable true omnidirectional movement.
3. **Firmware Checksums**:
   - The current OTA Update module accepts any `.bin` file. Future updates should implement MD5 checksums to prevent accidental corruption if a bad file is uploaded.
