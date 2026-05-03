#ifndef MAX7219_BSP_H
#define MAX7219_BSP_H

#include <stdint.h>
#include <stdbool.h>
#include "driver/spi_master.h"

// Define the pins for MAX7219
#define PIN_NUM_MISO -1 // Not used
#define PIN_NUM_MOSI 15
#define PIN_NUM_CLK  2
#define PIN_NUM_CS   5

#define NUM_MAX_DEVICES 2

void max7219_init(void);
void max7219_set_row(uint8_t device, uint8_t row, uint8_t data);
void max7219_clear(void);
void max7219_set_intensity(uint8_t intensity);
void max7219_shutdown(bool shutdown);

#endif // MAX7219_BSP_H
