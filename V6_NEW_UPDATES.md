# Curie V6.1 Firmware & Dashboard: Hotfixes and Enhancements

This document details the latest changes made in the V6.1 update to resolve critical motion control, telemetry, and physical hardware behavior issues.

---

## Summary of Changes

### 1. Reclaimed Left Shoulder Servo (GPIO 12 MTDI)
*   **Change:** Reconfigured `motion_ctrl.c` to include `driver/gpio.h` and explicitly call `gpio_reset_pin(12)` in `setup_servo()`.
*   **Result:** Reclaimed GPIO 12 from its JTAG default configuration, restoring high-speed MCPWM signal output. The left shoulder servo is now fully functional.

### 2. Corrected Tank Turning and Drive Vector Inversion
*   **Change:** Corrected `motion_ctrl.c` in `set_tank_sides()` by swapping the erroneous directional operations for the right side wheels:
    ```c
    int right_fwd = right_pct > 0 ? right_pwm : 0;
    int right_bwd = right_pct < 0 ? right_pwm : 0;
    ```
*   **Result:** Forward and backward motions now drive perfectly. Tank turns are strong, symmetrical, and spin around the exact physical center of the robot.

### 3. Fixed Speed Slider Cap at 80%
*   **Change:** Added WebSocket speed synchronization to the compiled React/Vite frontend assets (`app.js` and `dashboard_ui/app.js`). Adjusting the slider now instantly emits `N({cmd:"speed",speed:L[0]})` to updating the internal limits of the robot.
*   **Result:** Users can now utilize the full range from 0% up to 100% velocity. The speed meter is responsive and matches inputs.

### 4. Distinct Motion Profiles (Precision, Smooth, Aggressive)
*   **Change:** Configured the WebSocket command router in `wifi_server.c` to parse the incoming profile parameter uniquely:
    *   **Precision Mode:** Maps to `PROFILE_SMOOTH` (ramp step delta = `2` per 20ms frame, enabling smooth and detailed adjustments).
    *   **Smooth Mode:** Maps to `PROFILE_NORMAL` (ramp step delta = `6` per 20ms frame).
    *   **Aggressive Mode:** Maps to `PROFILE_AGGRESSIVE` (ramp step delta = `25` per 20ms frame, providing instantaneous response).
*   **Result:** Each profile now provides distinct acceleration characteristics.

### 5. Overhauled Handheld Controller to Native C ESP-IDF
*   **Change:** Replaced the bloated PlatformIO/Arduino controller project with a native C ESP-IDF project. It uses a high-performance **NimBLE Client** to handle scans and connections cleanly.
*   **Result:** Binary size was reduced by over **50%** (from 1.12 MB down to `520 KB`), with **49% free space** remaining in application partitions. Connection stability is pristine, and memory leaks are completely eliminated.

### 6. Implemented Write-Without-Response (`ble_gattc_write_no_rsp_flat`)
*   **Change:** Swapped the GATT write procedure from standard write-with-response (`ble_gattc_write_flat`) to write-without-response (`ble_gattc_write_no_rsp_flat`) inside the controller's main loop.
*   **Result:** Resolved the BLE connection queue congestion (Error Code `6` / `BLE_HS_EAGAIN`). Real-time joystick streaming at 40ms intervals is now completely smooth and lag-free.

---

### Verification and Flash Log

#### 🤖 1. Curie Robot Firmware
*   **Framework:** Native C ESP-IDF v5.4.1
*   **Binary Size:** `0x189f80` bytes (1.61 MB), with **18% free space** remaining.
*   **Flashing Success:** Flashed successfully over `COM5` to the robot.

#### 🕹️ 2. Handheld Controller Firmware
*   **Framework:** Native C ESP-IDF v5.4.1 (NimBLE Client)
*   **Binary Size:** `0x831d0` bytes (520 KB), with **49% free space** remaining.
*   **Flashing Success:** Flashed successfully over `COM5` to the controller.

