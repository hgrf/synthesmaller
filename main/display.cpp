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

void display_oscillator_params(const char *oscillator_name, waveform_t waveform, float frequency, int x, int y)
{
    /* draw oscillator name */
    const font& f = Bm437_ToshibaSat_9x14_FON;
    srect16 text_rect = srect16(spoint16(0, 0), f.measure_text((ssize16) lcd.dimensions(), oscillator_name));
    text_rect = text_rect.offset(x, y - text_rect.height() / 2);
    draw::text(lcd, text_rect, oscillator_name, f, lcd_color::white);

    /* sketch waveform */
    sketch_waveform(waveform, x + text_rect.width() + 10, y, 28, text_rect.height() / 2, lcd_color::white);

    /* draw frequency */
    char *freq_str;
    asprintf(&freq_str, "%.1f Hz", frequency);
    text_rect = srect16(spoint16(0, 0), f.measure_text((ssize16) lcd.dimensions(), (const char *) freq_str));
    text_rect = text_rect.offset(x + 90, y - text_rect.height() / 2);
    draw::text(lcd, text_rect, (const char *) freq_str, f, lcd_color::white);
    free(freq_str);
}

void display_task(void *pvParameters)
{
    for(;;) {
        /* get oscillator params */
        synth_get_params(&osc1_params);

        /* clear OSC1 params */
        // TODO: here we could clear only the parameters that have been changed in order to
        //       increase performace
        draw::filled_rectangle(lcd, srect16(10, 10, 200, 30), lcd_color::black);

        display_oscillator_params("OSC1", osc1_params.waveform, osc1_params.frequency, 10, 20);

        vTaskDelay(1000 / portTICK_PERIOD_MS);
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