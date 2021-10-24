#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "driver/uart.h"

#include "midi_input.h"
#include "synth.h"
#include "pinout.h"
#include "preset.h"

#define MIDI_SB_CONTROL_CHANGE  (0b1011 << 4)
#define MIDI_SB_NOTE_ON         (0b1001 << 4)
#define MIDI_SB_NOTE_OFF        (0b1000 << 4)

#define MIDI_CC_LFO_FREQ            (0x4a)
#define MIDI_CC_LFO_ON_OFF          (0x4d)
#define MIDI_CC_OSC2_FREQ           (0x4c)
#define MIDI_CC_OSC2_AMP            (0x49)
#define MIDI_CC_OSC2_SYNC_ON_OFF    (0x47)
#define MIDI_CC_WF_OSC1             (0x4e)
#define MIDI_CC_WF_OSC2             (0x4f)
#define MIDI_CC_WF_LFO              (0x5b)
#define MIDI_CC_ENV_ATTACK          (0x5d)
#define MIDI_CC_ENV_DECAY           (0x5e)
#define MIDI_CC_ENV_SUSTAIN         (0x0a)
#define MIDI_CC_ENV_RELEASE         (0x5c)
#define MIDI_CC_SELECT_PRESET       (0x07)
#define MIDI_CC_SAVE_PRESET         (0x46)

#define MIDI_UART_BAUDRATE      (31250)
#define UART_BUFFER_SIZE        (1024 * 2)

const uart_port_t uart_num = UART_NUM_2;

static QueueHandle_t uart_queue;

static void midi_process_cc(uint8_t *midi_frame)
{
    printf("Control change: %02X = %02X\n", midi_frame[1], midi_frame[2]);
    switch(midi_frame[1]) {
    case MIDI_CC_OSC2_FREQ:
        /* map the MIDI value (0...127) to a frequency between 100 and 2000 Hz */
        synth_update_osc2_freq(100.0 + (float) midi_frame[2] * 1900.0 / 127.0);
        break;
    case MIDI_CC_OSC2_AMP:
        /* map the MIDI value (0...127) to an amplitude between 0 and 15000 */
        synth_update_osc2_amp((float) midi_frame[2] * 15000.0 / 127.0);
        break;
    case MIDI_CC_LFO_FREQ:
        /* map the MIDI value (0...127) to an LFO frequency between 0.1 and 20 Hz */
        synth_update_lfo_freq(0.1 + (float) midi_frame[2] * 19.9 / 127.0);
        break;
    case MIDI_CC_LFO_ON_OFF:
        synth_enable_lfo(midi_frame[2]);
        break;
    case MIDI_CC_OSC2_SYNC_ON_OFF:
        synth_enable_osc2_sync(midi_frame[2]);
        break;
    case MIDI_CC_WF_OSC1:
        synth_update_osc1_waveform((midi_frame[2] / 16) % 3);
        break;
    case MIDI_CC_WF_OSC2:
        synth_update_osc2_waveform((midi_frame[2] / 16) % 3);
        break;
    case MIDI_CC_WF_LFO:
        synth_update_lfo_waveform((midi_frame[2] / 16) % 3);
        break;
    case MIDI_CC_ENV_ATTACK:
        /* map the MIDI value (0...127) to an attack time between 0.01 and 1 s */
        synth_update_env_attack(0.01 + (float) midi_frame[2] * 0.99 / 127.0);
        break;
    case MIDI_CC_ENV_DECAY:
        /* map the MIDI value (0...127) to a decay time between 0.01 and 1 s */
        synth_update_env_decay(0.01 + (float) midi_frame[2] * 0.99 / 127.0);
        break;
    case MIDI_CC_ENV_SUSTAIN:
        /* map the MIDI value (0...127) to a sustain value between 0 and 100 % */
        synth_update_env_sustain((float) midi_frame[2] / 127.0);
        break;
    case MIDI_CC_ENV_RELEASE:
        /* map the MIDI value (0...127) to a release time between 0.01 and 1 s */
        synth_update_env_release(0.01 + (float) midi_frame[2] * 0.99 / 127.0);
        break;
    case MIDI_CC_SELECT_PRESET:
        /* map the MIDI value (0...127) to a preset value (0...6) */
        preset_select(midi_frame[2] / 20);
        break;
    case MIDI_CC_SAVE_PRESET:
        preset_save();
        break;
    }
}

static void midi_process_frame(uint8_t *midi_frame)
{
    switch(midi_frame[0] & 0xf0) {
    case MIDI_SB_CONTROL_CHANGE:
        midi_process_cc(midi_frame);
        break;
    case MIDI_SB_NOTE_ON:
        synth_key_press(midi_frame[1], midi_frame[2]);
        break;
    case MIDI_SB_NOTE_OFF:
        synth_key_release(midi_frame[1]);
        break;
    }
}

// #define BPM                 (120)
// #define NOTE_DURATION_MS    (1000 * 60 / BPM)

void midi_loop(void)
{
    int length;
    uint8_t midi_frame[3];

    for(;;) {
        ESP_ERROR_CHECK(uart_get_buffered_data_len(uart_num, (size_t*)&length));
        if(length >= 3) {
            uart_read_bytes(uart_num, midi_frame, 3, 100);
            for(int i = 0; i < 3; i++) {
                printf("%02X ", midi_frame[i]);
            }
            printf("\n");
            midi_process_frame(midi_frame);
        }
        // TODO: can we wait for queue events instead?
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }

    // for(;;) {
    //     synth_key_press(0x45, 0x7f);
    //     vTaskDelay(NOTE_DURATION_MS / 2 / portTICK_PERIOD_MS);
    //     synth_key_release(0x45);
    //     vTaskDelay(NOTE_DURATION_MS / 2 / portTICK_PERIOD_MS);
    //     synth_key_press(0x47, 0x7f);
    //     vTaskDelay(NOTE_DURATION_MS / 2 / portTICK_PERIOD_MS);
    //     synth_key_release(0x47);
    //     vTaskDelay(NOTE_DURATION_MS / 2 / portTICK_PERIOD_MS);
    // }
}

void midi_init(void)
{
    /* set up UART */
    uart_config_t uart_config = {
        .baud_rate = MIDI_UART_BAUDRATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    };
    ESP_ERROR_CHECK(uart_param_config(uart_num, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(uart_num, UART_PIN_NO_CHANGE, MIDI_UART_RX_GPIO, \
                                    UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    /* install UART driver using an event queue */
    ESP_ERROR_CHECK(uart_driver_install(uart_num, UART_BUFFER_SIZE, \
                                            UART_BUFFER_SIZE, 10, &uart_queue, 0));
}