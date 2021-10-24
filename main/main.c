#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_spiffs.h"
#include "esp_system.h"
#include "esp_log.h"

#include "driver/ledc.h"
#include "driver/i2c.h"

#include "sgtl5000.h"
#include "midi_input.h"
#include "synth.h"
#include "display.h"
#include "pinout.h"

#define MCLK_FREQ               (11289600)

static const char *TAG = "APP";

void app_main(void)
{
    /* set up SPIFFS */
    esp_err_t ret;
    esp_vfs_spiffs_conf_t spiffs_conf = {
        .base_path = "/spiffs",
        .format_if_mount_failed = true,
        .max_files = 5,
        .partition_label = "storage",
    };
    ret = esp_vfs_spiffs_register(&spiffs_conf);
    ESP_ERROR_CHECK(ret);   

    /* set up I2C bus */
    i2c_port_t i2c_master_port = 1;
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = GPIO_NUM_SDA,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,        /* From I2C driver datasheet; internal pull-up may not be sufficient */
        .scl_io_num = GPIO_NUM_SCL,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master = {
            .clk_speed = 100000
        }
    };
    ESP_ERROR_CHECK(i2c_param_config(i2c_master_port, &conf));
    ESP_ERROR_CHECK(i2c_driver_install(i2c_master_port, conf.mode, 0, 0, 0));

    /* set up MCLK signal */
    ledc_timer_config_t ledc_timer = {
        .duty_resolution = LEDC_TIMER_2_BIT,
        .freq_hz = MCLK_FREQ,
        .speed_mode = LEDC_HIGH_SPEED_MODE,
        .timer_num = LEDC_TIMER_0
    };
    ledc_timer_config(&ledc_timer);

    ledc_channel_config_t ledc_channel = {
        .channel = LEDC_CHANNEL_0,
        .duty = 2,
        .gpio_num = GPIO_NUM_MCLK,
        .speed_mode = LEDC_HIGH_SPEED_MODE,
        .hpoint = 0,
        .timer_sel = LEDC_TIMER_0
    };
    ledc_channel_config(&ledc_channel);

    vTaskDelay(500 / portTICK_PERIOD_MS);

    /* initialize audio codec */
    sgtl5000_init(i2c_master_port, 0b0001010);

    /* initialize signal generator (including I2S bus) */
    oscillator_params_t osc1_params = {
        .amplitude = 10000.0,
        .waveform = WAVEFORM_SINUS,
        .frequency = 440.0,     // A4
    };
    oscillator_params_t osc2_params = {
        .amplitude = 0.0,
        .waveform = WAVEFORM_SAWTOOTH,
        .frequency = 523.25,    // C5
    };
    oscillator_params_t lfo_params = {
        .amplitude = 1.0,
        .waveform = WAVEFORM_SAWTOOTH,
        .frequency = 10.0,
    };
    envelope_params_t envelope_params = {
        .amplitude = 1.0,
        .attack = 0.1,
        .decay = 0.1,
        .sustain = 0.5,
        .release = 1.0,
    };
    synth_params_t synth_params = {
        .lfo_enabled = 0,
        .osc2_sync_enabled = 0,
    };
    synth_init(&osc1_params, &osc2_params, &lfo_params, &envelope_params, &synth_params);

    display_init();

    midi_init();
    midi_loop();
}