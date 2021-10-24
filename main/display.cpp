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

#include "preset.h"
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
#define WIDTH_ENVELOPE          (200)
#define HEIGHT_ENVELOPE         (70)

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
static synth_params_t synth_params;

static oscillator_params_t osc1_params_cached;
static oscillator_params_t osc2_params_cached;
static oscillator_params_t lfo_params_cached;
static synth_params_t synth_params_cached = {
    .lfo_enabled = true     // as a workaround, we set a different value here from what is in main.c
                            // TODO: check if cache was initialized in the functions below
};

static envelope_params_t envelope_params;
static envelope_params_t envelope_params_cached;
static uint8_t envelope_buffer[WIDTH_ENVELOPE];

static int preset_index_cached;

static void sketch_waveform(waveform_t waveform, int x, int y, int width, int amplitude, lcd_type::pixel_type color)
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

static bool compare_osc_params(oscillator_params_t *params1, oscillator_params_t *params2)
{
    return (params1->amplitude == params2->amplitude) \
        && (params1->frequency == params2->frequency) \
        && (params1->waveform == params2->waveform);
}

static bool compare_synth_params(synth_params_t *params1, synth_params_t *params2)
{
    return (params1->lfo_enabled == params2->lfo_enabled) \
        && (params1->osc2_sync_enabled == params2->osc2_sync_enabled);
}

static bool compare_envelope_params(envelope_params_t *params1, envelope_params_t *params2)
{
    // NOTE: we do not check the amplitude here, because the displayed curve is
    //       normalized to the plot height
    return (params1->attack == params2->attack) \
        && (params1->decay == params2->decay) \
        && (params1->release == params2->release) \
        && (params1->sustain == params2->sustain);
}

static void display_oscillator_params(const char *oscillator_name, oscillator_params_t *params, oscillator_params_t *params_cached, int x, int y)
{
    char *freq_str;
    char *amp_str;
    int offset_x = x;

    /* check if params have changed */
    if(compare_osc_params(params, params_cached) == true)
        return;

    /* clear the row */
    draw::filled_rectangle(lcd, srect16(offset_x, y - 10, lcd.dimensions().width - WIDTH_PADDING, y + 10), lcd_color::black);

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

    /* update cache */
    memcpy(params_cached, params, sizeof(oscillator_params_t));
}

static void display_synth_params(synth_params_t *params, synth_params_t *params_cached, int x, int y)
{
    char *synth_params_str;

    /* check if parameters have changed */
    if(compare_synth_params(params, params_cached) == true)
        return;

    /* clear the row */
    draw::filled_rectangle(lcd, srect16(x, y - 10, lcd.dimensions().width - WIDTH_PADDING, y + 10), lcd_color::black);

    /* draw string */
    asprintf(&synth_params_str, "OSC2 sync: %s LFO: %s", // total length 23 characters
        params->osc2_sync_enabled ? "ON " : "OFF",
        params->lfo_enabled ? "ON ": "OFF"
    );
    draw::text(lcd, srect16(x, y - TEXT_HEIGHT / 2, x + 23 * FONT_DELTA_X, y + TEXT_HEIGHT / 2), (const char *) synth_params_str, FONT, lcd_color::white);
    free(synth_params_str);

    /* update cache */
    memcpy(params_cached, params, sizeof(synth_params_t));
}

static void display_envelope(envelope_params_t *params, envelope_params_t *params_cached, int x, int y)
{
    float time_window;
    char *time_window_str;

    /* check if parameters have changed */
    if(compare_envelope_params(params, params_cached) == true)
        return;

    synth_map_envelope(envelope_buffer, WIDTH_ENVELOPE, HEIGHT_ENVELOPE, &time_window);
    draw::filled_rectangle(lcd, srect16(x, y, x + WIDTH_ENVELOPE, y + HEIGHT_ENVELOPE), lcd_color::black);
    for(int i = 0; i < WIDTH_ENVELOPE - 1; i++) {
        draw::line(
            lcd,
            srect16(
                WIDTH_PADDING + i,
                y + HEIGHT_ENVELOPE - envelope_buffer[i],
                WIDTH_PADDING + i + 1,
                y + HEIGHT_ENVELOPE - envelope_buffer[i + 1]
            ),
            lcd_color::white
        );
    }

    draw::filled_rectangle(
        lcd,
        srect16(
            x + WIDTH_ENVELOPE - 2 * WIDTH_PADDING - 5 * FONT_DELTA_X,  // total length 5 characters: x.x s (see below)
            y + HEIGHT_ENVELOPE + 10,
            x + WIDTH_ENVELOPE,
            y + HEIGHT_ENVELOPE + 30
        ),
        lcd_color::black
    );
    asprintf(&time_window_str, "%.1f s", time_window);
    draw::text(
        lcd,
        srect16(
            x + WIDTH_ENVELOPE - WIDTH_PADDING - 5 * FONT_DELTA_X,
            y + HEIGHT_ENVELOPE + 20 - TEXT_HEIGHT / 2,
            x + WIDTH_ENVELOPE,
            y + HEIGHT_ENVELOPE + 20 + TEXT_HEIGHT / 2
        ),
        (const char *) time_window_str,
        FONT,
        lcd_color::white
    );
    free(time_window_str);

    /* update cache */
    memcpy(params_cached, params, sizeof(envelope_params_t));
}

static void display_preset(int *index, int *index_cached, int x, int y)
{
    char *preset_str;

    /* check if parameters have changed */
    if(*index == *index_cached)
        return;


    draw::filled_rectangle(
        lcd,
        srect16(
            x,
            y + 10,
            x + 2 * FONT_DELTA_X + WIDTH_PADDING,
            y + 30
        ),
        lcd_color::black
    );
    asprintf(&preset_str, "P%d", *index);
    draw::text(
        lcd,
        srect16(
            x,
            y + 20 - TEXT_HEIGHT / 2,
            x + WIDTH_ENVELOPE,
            y + 20 + TEXT_HEIGHT / 2
        ),
        (const char *) preset_str,
        FONT,
        lcd_color::white
    );
    free(preset_str);

    *index_cached = *index;
}

static void display_task(void *pvParameters)
{
    int preset_index;

    for(;;) {
        /* get oscillator params */
        synth_get_params(&osc1_params, &osc2_params, &lfo_params, &envelope_params, &synth_params);
        preset_index = preset_get_current_index();
        
        display_oscillator_params("OSC1", &osc1_params, &osc1_params_cached, WIDTH_PADDING, 20);
        display_oscillator_params("OSC2", &osc2_params, &osc2_params_cached, WIDTH_PADDING, 50);
        display_oscillator_params("LFO", &lfo_params, &lfo_params_cached, WIDTH_PADDING, 80);
        display_synth_params(&synth_params, &synth_params_cached, WIDTH_PADDING, 110);
        display_envelope(&envelope_params, &envelope_params_cached, WIDTH_PADDING, 130);
        display_preset(&preset_index, &preset_index_cached, WIDTH_PADDING + WIDTH_ENVELOPE + WIDTH_PADDING, 130);

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