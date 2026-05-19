# Curie V6 Release Updates

This note summarizes the fixes, firmware packaging changes, and remaining hardware checks made on branch `codex/curie-v6-release`.

## Context

- Target repo: `Infrared-Curie-Repo`
- Branch: `codex/curie-v6-release`
- ESP-IDF used for verification builds: `v5.5.3`
- Hardware note carried into firmware: servo line previously on `GPIO12` was rewired to `GPIO19`

## Controller Firmware Updates

File: `remote_controller/main/main.c`

- Removed the D-pad drive override that was suppressing joystick drive after BLE connect.
- Corrected D-pad GPIO mapping in firmware:
  - `UP -> GPIO26`
  - `RIGHT -> GPIO27`
  - `DOWN -> GPIO21`
  - `LEFT -> GPIO22`
- Kept joystick drive as the primary motion source.
- Improved joystick handling and calibration behavior for BLE control.
- Fixed BLE recovery behavior:
  - increased discovery/connect timeout
  - prevented duplicate recovery loops
  - handled disconnect-during-discovery cases cleanly
  - reduced reconnect storms that were causing `service discovery error: 7` and repeated `failed to initiate connection: 6`

File: `remote_controller/main/keypad.c`

- Added keypad debounce to stop repeated or missed expression triggers.

## Robot Firmware Updates

File: `firmware/v6_doit30_clean_rewrite/main/board/board_config.h`

- Preserved shoulder servo mapping for the rewired line:
  - `GPIO19 = left shoulder`
  - `GPIO2 = right shoulder`
- Raised tank-turn tuning to make left/right turn commands physically usable:
  - `CURIE_TANK_TURN_SCALE_PCT = 100`
  - `CURIE_TANK_TURN_MIN_PCT = 75`
- Added per-motor inversion configuration to match real vehicle wiring:
  - `CURIE_M1_INVERT = 1`
  - `CURIE_M2_INVERT = 0`
  - `CURIE_M3_INVERT = 1`
  - `CURIE_M4_INVERT = 0`

File: `firmware/v6_doit30_clean_rewrite/main/motion/motion_ctrl.c`

- Replaced the asymmetric per-wheel turn behavior with grouped tank-side drive logic.
- Applied per-motor inversion in the tank-side path to match field-reported polarity.
- Fixed the rear-right runaway/spin regression caused by the earlier asymmetric motion path.

File: `firmware/v6_doit30_clean_rewrite/main/state/robot_state.c`

- Added motion profile bounds protection so invalid profile values do not propagate into motion tables.

## Documentation Updates

- Updated `docs/CONTROLLER_PINOUT.md` to match actual controller GPIO mappings.
- Updated `docs/FLASHING_GUIDE.md` to remove outdated D-pad drive-override behavior.
- Updated release-bundle copies of controller documentation to match the firmware.

## Release Artifacts Regenerated

- `artifacts/release_bundle/robot/curie_robot_full_4mb.bin`
- `artifacts/release_bundle/controller/curie_controller_ble_full_4mb.bin`
- `artifacts/curie_dual_esp32_flash_bundle.zip`

## Remaining Hardware Checks

These items were not conclusively closed in firmware and should be treated as hardware-path issues if they persist after flashing the latest builds:

- Servo behavior on `GPIO19` only moving to endpoint-like positions:
  - inspect the servo on the `GPIO19` channel
  - inspect signal integrity and common ground
  - inspect servo power under load
- Any single-wheel missing-direction failure after the latest motor polarity update:
  - inspect the corresponding ESP32 GPIO line to motor-driver input path
  - inspect the affected H-bridge input and wiring

## Recommended Validation Order

1. Flash both latest robot and controller images.
2. Test robot motion from the Wi-Fi dashboard first: `forward`, `right`, `back`, `left`.
3. Test BLE connection stability from the controller.
4. Test joystick drive.
5. Test `JOY_SW + D-pad` combos for arms/home/e-stop.
6. Test keypad expression triggers.
