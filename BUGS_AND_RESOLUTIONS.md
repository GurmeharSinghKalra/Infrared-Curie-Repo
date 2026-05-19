# Curie V6 Firmware & Dashboard: Bugs and Resolutions Log

This document lists all the critical hardware integration issues, firmware bugs, and dashboard communication problems identified during pair programming and physical hardware testing, along with the engineering solutions implemented to resolve them.

---

## 1. Mouth Row Inversion (Upside-down Mouth)
*   **The Problem:** The smile/frown animation patterns on the 8x8 LED mouth matrices were physically rendering upside down.
*   **The Root Cause:** Row and column scanning logic in the physical hardware wiring or SPI display drivers mapped row `0` to the bottom rather than the top, causing vertical mirroring.
*   **The Resolution:** 
    *   Modified `board_config.h`.
    *   Set `CURIE_MOUTH_LEFT_FLIP_ROWS` and `CURIE_MOUTH_RIGHT_FLIP_ROWS` parameters from `1` to `0`.
    *   This instantly corrected the vertical orientation of the smileys and frowned mouth designs.

---

## 2. LED Light-Bleed Behind Faceplate Masking
*   **The Problem:** Some outer corner LEDs of the 8x8 matrix were hidden behind the physical mouth faceplate on the robot, causing an ugly halo effect and truncated display patterns.
*   **The Root Cause:** Standard 8x8 arrays were used in code but the faceplate layout only leaves specific central pixels visible.
*   **The Resolution:** 
    *   Created a high-fidelity hardware bitmask array in `mouth_data.h`:
        *   `MOUTH_MASK_LEFT[8]`
        *   `MOUTH_MASK_RIGHT[8]`
    *   Updated the rendering loop in `display_ctrl.c` (`render_mouth()`) to run a bitwise-AND operation (`&`) between every generated frame row byte and the corresponding mask row byte.
    *   This permanently prevents any covered LEDs from lighting up, making the visible expression clean and bright with zero bleed.

---

## 3. OLED Eyes Crop & Alignment
*   **The Problem:** Eye animations were cut off at the top and bottom borders, and looked too large.
*   **The Root Cause:** Original render sizes were larger than standard 128x64 limits, causing scaling and clipping issues on the 0.96-inch OLED displays.
*   **The Resolution:**
    *   Redesigned all 18 unique eye renderers to strictly conform to a **max radius of 24px** (center `CX=64`, `CY=32`).
    *   Applied synchronized animation timing loops so left and right eyes look and blink together perfectly.

---

## 4. Tank Turning Logic Failure
*   **The Problem:** Tank turns did not work at all. Forward and backward movements behaved erratically.
*   **The Root Cause:** In `motion_ctrl.c` lines 123-124, the right motor forward and backward duty cycle assignments were inverted (`right_fwd` used `< 0` and `right_bwd` used `> 0`), causing right-side wheels to turn backward when commanded forward, and vice versa.
*   **The Resolution:**
    *   Corrected the direction mapping in `set_tank_sides()` in `motion_ctrl.c`:
        ```c
        int left_fwd = left_pct > 0 ? left_pwm : 0;
        int left_bwd = left_pct < 0 ? left_pwm : 0;
        int right_fwd = right_pct > 0 ? right_pwm : 0;
        int right_bwd = right_pct < 0 ? right_pwm : 0;
        ```
    *   Now, a left turn runs left wheels backward and right wheels forward, and a right turn runs left wheels forward and right wheels backward. Both sides spin correctly around the robot's physical center.

---

## 5. Capped Velocity (Speed Slider Stuck at 80%)
*   **The Problem:** The speed meter on the telemetry and speed slider would jump back to 80% and never allow the user to go higher.
*   **The Root Cause:** 
    1.  The robot state initialized speed to `80`.
    2.  The dashboard slider only updated the local React state but **never sent the `{cmd: "speed", speed: val}`** command to the WebSocket interface.
    3.  Every time the robot sent a telemetry update heartbeat, the dashboard received `speed = 80` and forced the slider back down to `80%`.
*   **The Resolution:**
    *   Binary patched the minified React/Vite dashboard build files (`app.js`) to explicitly trigger a WebSocket transmission `N({cmd:"speed",speed:L[0]})` during both slider changes (`onValueChange`) and commitments (`onValueCommit`).
    *   Now, sliding up to 100% updates the robot's internal motor duty cycle limits, and the telemetry happily reflects the new speed instantly!

---

## 6. Dead Speed Profiles (Smooth, Aggressive, Precision)
*   **The Problem:** Clicking "Smooth", "Aggressive", or "Precision" had no physical impact on the robot.
*   **The Root Cause:**
    *   In the HTTP handler in `wifi_server.c`, "Smooth" and "Precision" were both aliased to the same `PROFILE_SMOOTH` value.
    *   The ramp step delta in `motion_ctrl.c` was too narrow, making normal and smooth profiles feel almost identical.
*   **The Resolution:**
    *   Decoupled the mappings:
        *   **Precision Mode:** Uses `PROFILE_SMOOTH` with a very slow, stable ramp factor of `2` per 20ms step (for ultra-precise alignments).
        *   **Smooth Mode:** Uses `PROFILE_NORMAL` with a comfortable ramp factor of `6` per 20ms step.
        *   **Aggressive Mode:** Uses `PROFILE_AGGRESSIVE` with an instantaneous ramp factor of `25` per 20ms step (for instant skid-turns).

---

## 7. Reclaiming GPIO 12 Left Shoulder Servo
*   **The Problem:** The Left Shoulder Servo connected to GPIO 12 did not move, while the right servo worked perfectly.
*   **The Root Cause:** GPIO 12 on the ESP32 is the `MTDI` strapping pin and a JTAG signal (`TDI`). By default, the hardware boot configuration registers keep JTAG features mapped to this pin, which blocks standard MCPWM output pulses.
*   **The Resolution:**
    *   Imported `driver/gpio.h` into `motion_ctrl.c`.
    *   Added a call to `gpio_reset_pin(pin)` inside `setup_servo()` before any MCPWM registers are configured.
    *   This successfully detaches the internal boot strapping register mappings and reclaims GPIO 12 as a normal high-performance GPIO, restoring the left shoulder servo to full 0° to 180° motion!
