@echo off
set PORT=%1
if "%PORT%"=="" set PORT=COM5
py -m esptool --chip esp32 -p %PORT% -b 115200 --before default_reset --after hard_reset write_flash 0x0 robot\curie_robot_full_4mb.bin
