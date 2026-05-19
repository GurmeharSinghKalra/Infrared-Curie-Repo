#include "max7219_bsp.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "MAX7219";
static spi_device_handle_t spi;

// Register Addresses
#define MAX7219_REG_NOOP         0x00
#define MAX7219_REG_DIGIT0       0x01
#define MAX7219_REG_DECODEMODE   0x09
#define MAX7219_REG_INTENSITY    0x0A
#define MAX7219_REG_SCANLIMIT    0x0B
#define MAX7219_REG_SHUTDOWN     0x0C
#define MAX7219_REG_DISPLAYTEST  0x0F

static void max7219_write_raw(uint8_t *data, size_t len) {
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length = len * 8; // length in bits
    t.tx_buffer = data;
    spi_device_polling_transmit(spi, &t);
}

// Write to a specific register on all devices
static void max7219_write_all(uint8_t reg, uint8_t data) {
    uint8_t buf[NUM_MAX_DEVICES * 2];
    for (int i = 0; i < NUM_MAX_DEVICES; i++) {
        buf[i * 2] = reg;
        buf[i * 2 + 1] = data;
    }
    max7219_write_raw(buf, sizeof(buf));
}

void max7219_init(void) {
    esp_err_t ret;
    spi_bus_config_t buscfg = {
        .miso_io_num = PIN_NUM_MISO,
        .mosi_io_num = PIN_NUM_MOSI,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 32
    };

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 1000000, // 1 MHz is safe for MAX7219
        .mode = 0,                 // SPI mode 0
        .spics_io_num = PIN_NUM_CS,
        .queue_size = 1,
    };

    ret = spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO);
    ESP_ERROR_CHECK(ret);
    ret = spi_bus_add_device(SPI3_HOST, &devcfg, &spi);
    ESP_ERROR_CHECK(ret);

    max7219_write_all(MAX7219_REG_DISPLAYTEST, 0x00); // Disable test mode
    max7219_write_all(MAX7219_REG_SCANLIMIT, 0x07);   // Scan all digits
    max7219_write_all(MAX7219_REG_DECODEMODE, 0x00);  // No decode
    
    max7219_set_intensity(8);                         // Medium brightness
    max7219_clear();                                  // Clear displays before wake up
    max7219_shutdown(false);                          // Wake up
    
    ESP_LOGI(TAG, "MAX7219 initialized");
}

void max7219_set_row(uint8_t device, uint8_t row, uint8_t data) {
    if (device >= NUM_MAX_DEVICES || row > 7) return;
    
    uint8_t buf[NUM_MAX_DEVICES * 2];
    memset(buf, 0, sizeof(buf)); // Fill with NOOP (0x00, 0x00)
    
    // Modules are cascaded: Last module in chain receives data first.
    // Indexing: we want device 0 to be the first logic module (closest to ESP).
    // So device N is placed at position (NUM_DEVICES - 1 - device).
    int pos = (NUM_MAX_DEVICES - 1 - device) * 2;
    buf[pos] = MAX7219_REG_DIGIT0 + row;
    buf[pos + 1] = data;
    
    max7219_write_raw(buf, sizeof(buf));
}

void max7219_clear(void) {
    for (int i = 0; i < 8; i++) {
        max7219_write_all(MAX7219_REG_DIGIT0 + i, 0x00);
    }
}

void max7219_set_intensity(uint8_t intensity) {
    if(intensity > 15) intensity = 15;
    max7219_write_all(MAX7219_REG_INTENSITY, intensity);
}

void max7219_shutdown(bool shutdown) {
    max7219_write_all(MAX7219_REG_SHUTDOWN, shutdown ? 0x00 : 0x01);
}
