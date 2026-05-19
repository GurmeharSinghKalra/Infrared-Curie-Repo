#ifndef MAX7219_BSP_H
#define MAX7219_BSP_H

#include <stdint.h>
#include <stdbool.h>
#include "driver/spi_master.h"
#include "board/board_config.h"

#define PIN_NUM_MISO CURIE_MAX7219_MISO_GPIO
#define PIN_NUM_MOSI CURIE_MAX7219_MOSI_GPIO
#define PIN_NUM_CLK  CURIE_MAX7219_CLK_GPIO
#define PIN_NUM_CS   CURIE_MAX7219_CS_GPIO

#define NUM_MAX_DEVICES 2

void max7219_init(void);
void max7219_set_row(uint8_t device, uint8_t row, uint8_t data);
void max7219_clear(void);
void max7219_set_intensity(uint8_t intensity);
void max7219_shutdown(bool shutdown);

#endif // MAX7219_BSP_H
