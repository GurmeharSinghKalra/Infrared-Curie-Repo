#pragma once

// DOIT ESP32 DevKit V1 (30-pin) board profile for Curie.
// These GPIO assignments preserve the existing robot wiring while making
// the risks visible in one place.

#define CURIE_BOARD_NAME "DOIT ESP32 DevKit V1 (30-pin)"

// UART console is on GPIO1/TX0 and GPIO3/RX0. Do not use for peripherals.

// Motors: 4 H-bridge PWM pairs.
// Physical orientation is fixed as:
//   M1 = front-left
//   M2 = front-right
//   M3 = rear-left
//   M4 = rear-right
// Motion code groups M1+M3 as the left tank side and M2+M4 as the right tank side.
#define CURIE_M1_RPWM_GPIO 25
#define CURIE_M1_LPWM_GPIO 26
#define CURIE_M2_RPWM_GPIO 27
#define CURIE_M2_LPWM_GPIO 14
#define CURIE_M3_RPWM_GPIO 13
#define CURIE_M3_LPWM_GPIO 16
#define CURIE_M4_RPWM_GPIO 17
#define CURIE_M4_LPWM_GPIO 4

// Shoulders: preserved from the existing robot harness.
// Warning: GPIO12 and GPIO2 are ESP32 strapping pins. They are safe only if the
// attached servo circuitry does not force invalid levels during reset.
#define CURIE_LEFT_SHOULDER_GPIO 12
#define CURIE_RIGHT_SHOULDER_GPIO 2

// Eyes: two SSD1306-style I2C OLED buses.
#define CURIE_LEFT_OLED_I2C_PORT  0
#define CURIE_LEFT_OLED_SDA_GPIO 21
#define CURIE_LEFT_OLED_SCL_GPIO 22
#define CURIE_LEFT_OLED_I2C_ADDR 0x78

#define CURIE_RIGHT_OLED_I2C_PORT  1
#define CURIE_RIGHT_OLED_SDA_GPIO 32
#define CURIE_RIGHT_OLED_SCL_GPIO 33
#define CURIE_RIGHT_OLED_I2C_ADDR 0x78
#define CURIE_OLED_I2C_SPEED_HZ 100000

// Mouth: dual MAX7219 chain on VSPI-class pins.
#define CURIE_MAX7219_MISO_GPIO -1
#define CURIE_MAX7219_MOSI_GPIO 23
#define CURIE_MAX7219_CLK_GPIO  18
#define CURIE_MAX7219_CS_GPIO    5

// Mouth display transform. These defaults match the current dual 8x8 daisy-chained
// mouth hardware with upright-facing matrices.
#define CURIE_MOUTH_SWAP_HALVES      0
#define CURIE_MOUTH_LEFT_FLIP_ROWS   1
#define CURIE_MOUTH_LEFT_FLIP_COLS   1
#define CURIE_MOUTH_RIGHT_FLIP_ROWS  1
#define CURIE_MOUTH_RIGHT_FLIP_COLS  1

// In-place turn scaling. Turning at reduced speed is more robust on the current
// drivetrain than commanding both sides to full opposing duty.
#define CURIE_TANK_TURN_SCALE_PCT 65

// Wi-Fi defaults tuned for compatibility-first provisioning.
#define CURIE_AP_SSID "Infrared Curie Setup"
#define CURIE_AP_PASSWORD ""
#define CURIE_AP_CHANNEL 1
#define CURIE_AP_MAX_CONNECTIONS 4
