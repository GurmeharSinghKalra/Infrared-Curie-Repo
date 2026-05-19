# Curie V6 Firmware Overhaul and Update

This document details the major updates and bug fixes implemented during the V6 firmware overhaul for the Curie Robot platform.

## 1. Tank Drive and Movement Logic
- **Issue:** The left and right side motors were not acting symmetrically in tank drive, causing forward and turning inputs to behave erratically.
- **Resolution (`motion_ctrl.c`):** The PWM output calculation for the right-side motors (`R_F_PIN`, `R_B_PIN`) was inverted. Pushing the joystick forward now accurately rotates both wheel sets forward, correctly mapping throttle and steering vector mixing for a smooth tank drive.

## 2. Mouth Display Matrix Mirroring and Masking
- **Issue:** The 8x8 LED matrix displays used for the mouth were rendering images offset, backwards, and bleeding light outside the robot's physical faceplate cutouts.
- **Resolution (`board_config.h` & `display_ctrl.c`):** 
  - Adjusted boolean definitions (`CURIE_MOUTH_LEFT_FLIP_COLS`, `CURIE_MOUTH_RIGHT_FLIP_COLS`, `CURIE_MOUTH_SWAP_HALVES`) to match the physical panel wiring layout so drawing on the dashboard is mirrored correctly.
  - Implemented a binary mask matrix overlay over all 18 hardcoded `MOUTH_` expression frames. The corner LEDs (top left/right, bottom left/right) are forced permanently off in software so the animation fits cleanly behind the circular faceplate without clipping or bleeding.

## 3. Cartoony Eyes Design
- **Issue:** The robot's eyes were using legacy box styles, lacking expression.
- **Resolution (`display_ctrl.c`):** Overhauled the `FACE_PROFILES` array to utilize `EYE_STYLE_CIRCLE`. The drawing function was updated to use `u8g2_DrawDisc` for rendering large, thick, circular eyes. Eyebrow offsets and thicknesses were increased to generate highly expressive, kid-friendly looks for emotions like "Happy", "Angry", "Sad", etc.

## 4. Servo Consolidation
- **Issue:** The dashboard had 4 separate joint sliders (Base, Shoulder, Elbow, Gripper), which was unnecessary for the robot's physical design and made control overly complex.
- **Resolution (`app.js` & `wifi_server.c`):** 
  - The React UI was simplified to display one single `Arms` slider ranging from 0° (hands down) to 180° (hands fully up).
  - The `wifi_server.c` WebSocket handler was updated to intercept the `arms` joint, symmetrically calculating and streaming coordinates for both servos (`left_shoulder = angle`, `right_shoulder = 180 - angle`) and ignoring legacy arm joints.

## 5. BLE Connectivity Architecture Migration
- **Issue:** The legacy ESP-NOW architecture was interfering with the Wi-Fi AP and causing connectivity drops on mobile devices connecting to the robot's dashboard.
- **Resolution:**
  - **Robot:** Completely removed the `espnow_controller.c/h` components. Developed a highly robust `ble_controller.c` built on the **NimBLE** stack. The robot now advertises a standard GATT service under the name **"Curie-Robot"**.
  - **Handheld Controller:** Entirely rewrote the `remote_controller.ino` and `remote_controller_self_test.ino` sketches to use the `BLEDevice` API. The controller now operates as a BLE central device, boots up, scans for "Curie-Robot", connects securely, and streams motor/button vectors 25 times a second without requiring manual MAC pairing.

## 6. Compiler and Memory Optimizations
- **Issue:** Compiling the ESP-IDF binary with both the Wi-Fi and Bluetooth stacks triggered a fatal linker error (`iram0_0_seg overflowed by 8,336 bytes`), and the resulting binary exceeded the size limit of the default OTA partition.
- **Resolution (`sdkconfig.defaults` & `partitions.csv`):**
  - **IRAM Optimization:** Disabled `CONFIG_ESP_WIFI_IRAM_OPT` and `CONFIG_ESP_WIFI_RX_IRAM_OPT`, relocating high-speed Wi-Fi optimizations out of Instruction RAM and freeing up over 16KB of IRAM space to allow NimBLE to compile cleanly.
  - **Memory Footprint:** Restricted `CONFIG_BT_NIMBLE_MAX_CONNECTIONS` to 1, saving critical runtime SRAM.
  - **Partition Resizing:** Expanded the physical `ota_0` and `ota_1` flash partitions in `partitions.csv` from 1500KB to 1920KB (`0x1E0000`), taking advantage of the unused 1MB flash space on the 4MB module to perfectly accommodate the unified BLE+Wi-Fi firmware.

*All modifications successfully compiled, flashed, and tested on the ESP32.*
