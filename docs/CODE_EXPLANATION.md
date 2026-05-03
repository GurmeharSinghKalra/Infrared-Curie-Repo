# Infrared Curie: Code Explanation

This document breaks down the major architectural components of the ESP-IDF firmware and the physical remote controller.

## 1. Network & WiFi System (`wifi_server.c`)
This file is the backbone of the robot's connectivity.
- **Dual Mode Switching**: The robot uses `save_wifi_credentials()` and `start_ap_mode()` / `start_sta_mode()` to switch between broadcasting its own hotspot (`Infrared Curie Setup`) and connecting to a home network.
- **Graceful WiFi Scanning**: `api_wifi_scan_get_handler()` uses `esp_wifi_scan_start()` but specifically **avoids** `ESP_ERROR_CHECK`. This was a critical fix; calling a hard error check here during Station mode would cause a kernel panic.
- **WebSocket Handler (`ws_handler`)**: 
  - Receives incoming JSON frames from the UI or Remote.
  - Parses them using `cJSON`.
  - Dispatches commands (like `move`, `anim`, `arms`) to the `event_bus_send()`.
  - Includes a crucial dangling-pointer safeguard (`telemetry_deregister_ws()`) to ensure network swaps don't try to send data to dead WebSocket clients.
- **OTA Updates (`ota_update_post_handler`)**: Uses `esp_ota_ops.h` to receive `.bin` file streams sequentially and writes them directly to the next available boot partition.

## 2. Motion Controller (`motion_ctrl.c`)
This file handles hardware PWM generation for both DC motors and servos.
- **V3 Dual Driver Logic (Current)**:
  - Consolidates 8 PWM pins down to 4 (`LEFT_RPWM/LPWM` and `RIGHT_RPWM/LPWM`).
  - Uses `ledc` timers for high-frequency (20kHz) smooth motor control.
  - Implements **Differential Drive**: The `switch (dir)` block handles tank-steering by running the left and right drivers in opposite directions for `DIR_LEFT` and `DIR_RIGHT`.
- **Servo Easing (`step_toward`)**: Instead of jumping servos to their target immediately (which draws huge current spikes), the `task_motion` loop eases the `cur_ls` (current left shoulder) towards the `target` value using `SERVO_STEP_DEG`.

## 3. Display Controller (`display_ctrl.c`)
Controls the dual I2C OLED eyes and the SPI MAX7219 mouth.
- **Expression Enums (`str_to_exp`)**: The system maps simple strings ("happy", "angry") sent over WebSockets into a 9-value enum (`EXP_HAPPY`, `EXP_ANGRY`).
- **OLED Drawing (`draw_expression`)**: Uses the `u8g2` library. Each expression contains custom geometry (circles, boxes, triangles) designed to look expressive.
- **Blink Logic**: A non-blocking pseudo-random blink generator briefly overwrites the current expression with horizontal lines (`EXP_BLINK`) for 120ms to make the robot feel alive.
- **Custom Matrix Designer**: Listens for the `matrix` command via WebSocket. It receives an array of 16 integers (representing 16 columns of 8 pixels) and streams it directly over SPI to the MAX7219 driver.

## 4. Telemetry Engine (`telemetry.c`)
Runs asynchronously to gather robot state data.
- **JSON Formatting**: Packages uptime, speed profile, current expression, and battery level into a JSON string.
- **Transmission**: Sends this JSON string back to all connected WebSocket clients every 500ms using `httpd_ws_send_frame_async()`.

## 5. Physical Remote Controller (`remote_controller.ino`)
A standalone Arduino sketch designed for a secondary ESP32.
- **Hardware Integration**: Uses the `Keypad.h` library to map a 4x4 matrix keypad to 8 GPIO pins.
- **Analog Joysticks**: Uses standard `analogRead()` (0-4095).
- **Deadzone Logic**: The `processMovement()` function ignores inputs near the center (~2000) and triggers `FWD`/`BWD`/`LEFT`/`RIGHT` when the sticks hit extreme thresholds (<1000 or >3000).
- **Communication**: Acts as a WebSocket Client (`WebSocketsClient.h`) and sends standard Curie-compatible JSON packets, acting exactly like an invisible Web Dashboard.
