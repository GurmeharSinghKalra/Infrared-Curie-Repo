Curie BLE Controller firmware

Flash command (Windows, example COM5):
py -m esptool --chip esp32 -p COM5 -b 115200 --before default-reset --after hard-reset write-flash 0x0 "controller\curie_controller_ble_full_4mb.bin"

This controller firmware is the native ESP-IDF BLE controller from the current GitHub branch codex/curie-v6-release.
