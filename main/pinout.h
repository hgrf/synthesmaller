#ifndef PINOUT_H
#define PINOUT_H

/* on the WROVER-E module, GPIO 6-11 and 16-17 are used for
 * SPI flash and PSRAM
 * https://www.espressif.com/sites/default/files/documentation/esp32-wrover-e_esp32-wrover-ie_datasheet_en.pdf
 */

/* input only pins: (https://randomnerdtutorials.com/esp32-pinout-reference-gpios/)
 * IO34, IO35, IO36, IO39
 */

/* on the ESP-WROVER-KIT v4.1, an RGB LED is connected (via MOSFETs) to
 * IO0, IO2 and IO4
 *
 * and a crystal oscillator is connected between IO32 and IO33 via R11 and R23
 * 
 * the four debug lines (TCK, TDI, TDO, TMS) can be connected to
 * IO13, IO12, IO15 and IO14 respectively using jumpers
 *
 * https://dl.espressif.com/dl/schematics/ESP-WROVER-KIT_V4_1.pdf
 */

/* UART (MIDI in) pinout */
#define MIDI_UART_RX_GPIO       (2)

/* I2C pinout */
#define GPIO_NUM_SDA            (13)
#define GPIO_NUM_SCL            (4)

/* master clock for audio codec */
#define GPIO_NUM_MCLK           (15)

/* I2S pinout */
#define GPIO_NUM_BCLK           (26)
#define GPIO_NUM_WCLK           (27)
#define GPIO_NUM_DOUT           (14)

/* pinout for the ILI9431 LCD */
#define LCD_HOST    HSPI_HOST

#define PIN_NUM_MISO GPIO_NUM_25
#define PIN_NUM_MOSI GPIO_NUM_23
#define PIN_NUM_CLK  GPIO_NUM_19
#define PIN_NUM_CS   GPIO_NUM_22

#define PIN_NUM_DC   GPIO_NUM_21
#define PIN_NUM_RST  GPIO_NUM_18
#define PIN_NUM_BCKL GPIO_NUM_5

#endif // PINOUT_H
