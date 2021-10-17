#include "display.h"
#include "pinout.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "common/spi_master.hpp"
#include "ili9341.hpp"
#include "gfx_drawing.hpp"
#include "gfx_image.hpp"
#include "gfx_drawing.hpp"
#include "stream.hpp"
#include "gfx_color_cpp14.hpp"
#include "../fonts/Bm437_ToshibaSat_9x14.h"

#include "synth.h"

using namespace espidf;
using namespace io;
using namespace gfx;

#define LCD_ILI9341
#define PARALLEL_LINES 16
#define DMA_CHAN    2

#define FONT                    Bm437_ToshibaSat_9x14_FON
#define FONT_DELTA_X            (9)
#define TEXT_HEIGHT             (14)
#define MAX_TEXT_WIDTH_AMP      (9 * FONT_DELTA_X)      // A=99999.9
#define MAX_TEXT_WIDTH_FREQ     (9 * FONT_DELTA_X)      // 9999.9 Hz
#define MAX_TEXT_WIDTH_NAME     (4 * FONT_DELTA_X)      // OSC1, OSC2 or LFO
#define WIDTH_WAVEFORM          (28)
#define WIDTH_PADDING           (10)

// To speed up transfers, every SPI transfer sends as much data as possible. 

// configure the spi bus. Must be done before the driver
spi_master spi_host(nullptr,
                    LCD_HOST,
                    PIN_NUM_CLK,
                    PIN_NUM_MISO,
                    PIN_NUM_MOSI,
                    GPIO_NUM_NC,
                    GPIO_NUM_NC,
                    PARALLEL_LINES*320*2+8,
                    DMA_CHAN);

// we use the default, modest buffer - it makes things slower but uses less
// memory. it usually works fine at default but you can change it for performance 
// tuning. It's the final parameter: Note that it shouldn't be any bigger than 
// the DMA size
using lcd_type = ili9341<LCD_HOST,
                        PIN_NUM_CS,
                        PIN_NUM_DC,
                        PIN_NUM_RST,
                        PIN_NUM_BCKL>;

lcd_type lcd;

using lcd_color = color<typename lcd_type::pixel_type>;

static oscillator_params_t osc1_params;
static oscillator_params_t osc2_params;
static oscillator_params_t lfo_params;

static oscillator_params_t osc1_params_cached;
static oscillator_params_t osc2_params_cached;
static oscillator_params_t lfo_params_cached;

void sketch_waveform(waveform_t waveform, int x, int y, int width, int amplitude, lcd_type::pixel_type color)
{
    switch(waveform) {
    case WAVEFORM_SINUS:
        /* not really a sinusoidal, but oh well */
        draw::arc(lcd, srect16(x, y, x + width/4, y - amplitude).flip_vertical(), color);
        draw::arc(lcd, srect16(x + width/4, y - amplitude, x + width/2, y).flip_horizontal(), color);
        draw::arc(lcd, srect16(x + width/2, y, x + 3*width/4, y + amplitude).flip_vertical(), color);
        draw::arc(lcd, srect16(x + 3*width/4, y + amplitude, x + width, y).flip_horizontal(), color);
        break;
    case WAVEFORM_SAWTOOTH:
        draw::line(lcd, srect16(x, y, x + width/2, y - amplitude), color);
        draw::line(lcd, srect16(x + width/2, y - amplitude, x + width/2, y + amplitude), color);
        draw::line(lcd, srect16(x + width/2, y + amplitude, x + width, y), color);
        break;
    case WAVEFORM_SQUARE:
        draw::line(lcd, srect16(x, y, x, y - amplitude), color);
        draw::line(lcd, srect16(x, y - amplitude, x + width/2, y - amplitude), color);
        draw::line(lcd, srect16(x + width/2, y - amplitude, x + width/2, y + amplitude), color);
        draw::line(lcd, srect16(x + width/2, y + amplitude, x + width, y + amplitude), color);
        draw::line(lcd, srect16(x + width, y + amplitude, x + width, y), color);
        break;
    }
}

void display_oscillator_params(const char *oscillator_name, oscillator_params_t *params, int x, int y)
{
    char *freq_str;
    char *amp_str;
    int offset_x = x;

    /* draw oscillator name */
    draw::text(lcd, srect16(offset_x, y - TEXT_HEIGHT / 2, offset_x + MAX_TEXT_WIDTH_NAME, y + TEXT_HEIGHT / 2), oscillator_name, FONT, lcd_color::white);
    offset_x += MAX_TEXT_WIDTH_NAME + WIDTH_PADDING;

    /* sketch waveform */
    sketch_waveform(params->waveform, offset_x, y, WIDTH_WAVEFORM, TEXT_HEIGHT / 2, lcd_color::white);
    offset_x += WIDTH_WAVEFORM + WIDTH_PADDING;

    /* draw frequency */
    asprintf(&freq_str, "%.1f Hz", params->frequency);
    draw::text(lcd, srect16(offset_x, y - TEXT_HEIGHT / 2, offset_x + MAX_TEXT_WIDTH_FREQ, y + TEXT_HEIGHT / 2), (const char *) freq_str, FONT, lcd_color::white);
    free(freq_str);
    offset_x += MAX_TEXT_WIDTH_FREQ + WIDTH_PADDING;

    /* draw amplitude */
    asprintf(&amp_str, "A=%.1f", params->amplitude);
    draw::text(lcd, srect16(offset_x, y - TEXT_HEIGHT / 2, offset_x + MAX_TEXT_WIDTH_AMP, y + TEXT_HEIGHT / 2), (const char *) amp_str, FONT, lcd_color::white);
    free(amp_str);
}

bool compare_osc_params(oscillator_params_t *params1, oscillator_params_t *params2)
{
    return (params1->amplitude == params2->amplitude) \
        && (params1->frequency == params2->frequency) \
        && (params1->waveform == params2->waveform);
}

void display_task(void *pvParameters)
{
    for(;;) {
        /* get oscillator params */
        synth_get_params(&osc1_params, &osc2_params, &lfo_params);

        if(compare_osc_params(&osc1_params, &osc1_params_cached) == false) {
            draw::filled_rectangle(lcd, srect16(10, 10, 310, 30), lcd_color::black);
            display_oscillator_params("OSC1", &osc1_params, 10, 20);
            memcpy(&osc1_params_cached, &osc1_params, sizeof(oscillator_params_t));
        }

        if(compare_osc_params(&osc2_params, &osc2_params_cached) == false) {
            draw::filled_rectangle(lcd, srect16(10, 40, 310, 60), lcd_color::black);
            display_oscillator_params("OSC2", &osc2_params, 10, 50);
            memcpy(&osc2_params_cached, &osc2_params, sizeof(oscillator_params_t));
        }

        if(compare_osc_params(&lfo_params, &lfo_params_cached) == false) {
            draw::filled_rectangle(lcd, srect16(10, 70, 310, 90), lcd_color::black);
            display_oscillator_params("LFO ", &lfo_params, 10, 80);
            memcpy(&lfo_params_cached, &lfo_params, sizeof(oscillator_params_t));
        }

        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}

void display_init(void)
{
    // check to make sure SPI was initialized successfully
    if(!spi_host.initialized()) {
        printf("SPI host initialization error.\r\n");
        abort();
    }

    /* draw gray background */
    draw::filled_rectangle(lcd, (srect16) lcd.bounds(), lcd_color::gray);

    xTaskCreatePinnedToCore(display_task, "display_task", 4096, NULL, 1, NULL, 0);
}