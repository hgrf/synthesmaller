#include "synth.h"

#include <math.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"

#include "driver/i2s.h"

#include "esp_dsp.h"

static const char *TAG = "SYNTH";

#define BUFFER_TIME             (0.01)      // buffer size of 10 ms
#define BUFFER_TIME_US          ((uint32_t) (BUFFER_TIME * 1000000))
#define I2S_TIMEOUT_MS          (100)
#define I2S_NUM                 (0)
#define CHANNEL_COUNT           (2)
#define SAMPLING_FREQ           (44100)
#define GPIO_NUM_BCLK           (4)
#define GPIO_NUM_WCLK           (5)
#define GPIO_NUM_DOUT           (18)

#define TIME_STEP                   (1.0 / SAMPLING_FREQ)
#define BUFFER_SAMPLES_PER_CHANNEL  ((uint32_t) (BUFFER_TIME / TIME_STEP))
#define BUFFER_SAMPLE_COUNT         (BUFFER_SAMPLES_PER_CHANNEL * CHANNEL_COUNT)

#define ENVELOPE_DOWNSAMPLING   (100)

/* see https://en.wikipedia.org/wiki/Root_mean_square#In_common_waveforms */
#define RMS_SINUS               (0.7071)       // 1/sqrt(2)
#define RMS_SQUARE              (1.0)
#define RMS_SAWTOOTH            (0.5774)       // 1/sqrt(3)

typedef struct {
    oscillator_params_t params;
    /* this is a buffer for one oscillation; it is particularly important
     * for sine wave calculation, since it will be too slow to do in real-time;
     * the lowest frequency it supports is 10 Hz, i.e. an oscillation period of
     * 100 ms
     */
    float buffer[SAMPLING_FREQ / 10];
    uint32_t buffer_size;
    /* since the LFO will typically operate at frequencies smaller than 20 Hz (see also
     * https://en.wikipedia.org/wiki/Low-frequency_oscillation), we use down-sampling,
     * so that the effective sampling rate is 100 times smaller than the actual sampling rate.
     * This way, we can buffer oscillations down to 0.1 Hz using the same space.
     */
    uint32_t downsampling_factor;
} oscillator_t;

typedef struct {
    envelope_params_t params;
    /* For the envelope, we use a fixed downsampling factor of 100. That means we
     * can store up to 10 s in this buffer, with a resolution of one sample per
     * 2 ms (if the original sampling rate is 44.1 kHz).
     */
    float attack_decay_buffer[SAMPLING_FREQ / 10];
    float release_buffer[SAMPLING_FREQ / 10];
    uint32_t attack_decay_buffer_size;
    uint32_t release_buffer_size;
    uint32_t trigger_offset;
    uint32_t release_offset;
} envelope_t;

static SemaphoreHandle_t m_osc_sem;
static oscillator_t m_osc1;
static oscillator_t m_osc2;
static oscillator_t m_lfo;
static envelope_t m_envelope;
static uint8_t m_last_key_pressed;
static synth_params_t m_synth_params;

struct {
    int16_t buffer[BUFFER_SAMPLE_COUNT];
    // at a sampling rate of 44.1 kHz, this offset value will overflow every
    // 2**32 / (44.1 kHz) = 27h
    uint32_t offset;
} m_buf;

static void synth_calculate_buffer(void)
{
    float envelope_val = 0.0;
    float lfo_val = 1.0;
    float osc2_val;

    xSemaphoreTake(m_osc_sem, portMAX_DELAY);

    for(int i = 0; i < BUFFER_SAMPLES_PER_CHANNEL; i++) {
        /* calculate envelope */
        if((m_buf.offset + i) > m_envelope.release_offset) {
            /* are we still within the release_buffer window? */
            if((m_buf.offset + i) < (m_envelope.release_offset + m_envelope.release_buffer_size * ENVELOPE_DOWNSAMPLING)) {
                envelope_val = m_envelope.release_buffer[(m_buf.offset + i - m_envelope.release_offset) / ENVELOPE_DOWNSAMPLING];
            }
            /* otherwise leave at zero */
        } else if((m_buf.offset + i) > m_envelope.trigger_offset) {
            /* are we still within the attack_decay_buffer window? */
            if((m_buf.offset + i) < (m_envelope.trigger_offset + m_envelope.attack_decay_buffer_size * ENVELOPE_DOWNSAMPLING)) {
                envelope_val = m_envelope.attack_decay_buffer[(m_buf.offset + i - m_envelope.trigger_offset) / ENVELOPE_DOWNSAMPLING];
            } else {
                /* otherwise just take the last value from that buffer (which is the sustain value) */
                envelope_val = m_envelope.attack_decay_buffer[m_envelope.attack_decay_buffer_size - 1];
            }
        }

        if(m_synth_params.lfo_enabled) {
            lfo_val = m_lfo.buffer[((m_buf.offset + i) / m_lfo.downsampling_factor) % m_lfo.buffer_size];
        }

        if(m_synth_params.osc2_sync_enabled) {
            // TODO: 
            // - check the sync logic
            // - this only works correctly if the downsampling factors are equal
            osc2_val = m_osc2.buffer[(((m_buf.offset + i) / m_osc1.downsampling_factor) % m_osc1.buffer_size) % m_osc2.buffer_size];
        } else {
            osc2_val = m_osc2.buffer[((m_buf.offset + i) / m_osc2.downsampling_factor) % m_osc2.buffer_size];
        }

        /* calculate sample */
        m_buf.buffer[CHANNEL_COUNT * i] = (int16_t) (
            envelope_val
            * lfo_val
            * (
                m_osc1.buffer[((m_buf.offset + i) / m_osc1.downsampling_factor) % m_osc1.buffer_size] +
                osc2_val
            )
        );
    }

    /* copy signal to other channel(s) */
    for(int i = 0; i < BUFFER_SAMPLES_PER_CHANNEL; i++) {
        for(int j = 1; j < CHANNEL_COUNT; j++) {
            m_buf.buffer[CHANNEL_COUNT * i + j] = m_buf.buffer[CHANNEL_COUNT * i];
        }
    }

    xSemaphoreGive(m_osc_sem);

    m_buf.offset += BUFFER_SAMPLES_PER_CHANNEL;
}

static void i2s_init(void)
{
    ESP_LOGI(TAG, "Initializing I2S bus...");

    /* set up I2S bus */
    i2s_config_t i2s_config = {
        .mode = I2S_MODE_MASTER | I2S_MODE_TX,
        .sample_rate = SAMPLING_FREQ,
        .bits_per_sample = 16,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_I2S | I2S_COMM_FORMAT_I2S_MSB,
        .intr_alloc_flags = 0,  // default interrupt priority
        .dma_buf_count = 4,     // num of dma buff
        .dma_buf_len = 512,     // size of every dma buff, all dma buffs size = dma_buf_count*dma_buf_len;
        .use_apll = false
    };
    i2s_pin_config_t pin_config = {       
        .bck_io_num = GPIO_NUM_BCLK,
        .ws_io_num = GPIO_NUM_WCLK,
        .data_out_num = GPIO_NUM_DOUT,
        .data_in_num = I2S_PIN_NO_CHANGE
    };
    i2s_driver_install(I2S_NUM, &i2s_config, 0, NULL);
    i2s_set_pin(I2S_NUM, &pin_config);

    ESP_LOGI(TAG, "I2S bus ready");
}

static void synth_task(void *pvParameters)
{
    i2s_init();

    uint64_t t_us;
    uint64_t calc_time_us;
    // int64_t wait_time_us;
    size_t i2s_bytes_written;
    uint32_t load;
    uint64_t load_last_displayed = 0;

    for(;;) {
        t_us = esp_timer_get_time();

        /* calculate buffer */
        synth_calculate_buffer();

        /* calculate and show load */
        // NOTE: we could also average the load
        if(esp_timer_get_time() - load_last_displayed > 1000000) {
            calc_time_us = esp_timer_get_time() - t_us;
            load = 100 * calc_time_us / BUFFER_TIME_US;
            printf("Calculation load: %u %%\n", load);
            load_last_displayed = esp_timer_get_time();
        }

        /* send it to I2S bus */
        i2s_write(I2S_NUM, m_buf.buffer, BUFFER_SAMPLE_COUNT * sizeof(int16_t), &i2s_bytes_written, I2S_TIMEOUT_MS / portTICK_PERIOD_MS);

        if(i2s_bytes_written < BUFFER_SAMPLE_COUNT * sizeof(int16_t)) {
            ESP_LOGW(TAG, "I2S timeout\n");
        }

        /* how long until we need to calculate next buffer? */
        // NOTE: if we are late, we will never catch up with this logic, since the wait time is over one cycle;
        //       besides we do not really need this wait logic, since i2s_write takes care of buffering out correctly
        // wait_time_us = t_us + BUFFER_TIME_US - esp_timer_get_time();
        // if(wait_time_us > 1000) {
        //     vTaskDelay(wait_time_us / 1000 / portTICK_PERIOD_MS);
        // }
    }
}

/* not threadsafe, should be called after obtaining semaphore */
static void oscillator_calculate_buffer(oscillator_t *osc)
{
    /* calculate buffer size */
    // NOTE: this method will falsiy the frequency for frequencies that do not "fit" into the
    //       sampling rate
    osc->buffer_size = SAMPLING_FREQ / osc->downsampling_factor / osc->params.frequency;

    /* calculate buffer for oscillator */
    switch(osc->params.waveform) {
    case WAVEFORM_SINUS:
        // NOTE: this is quite slow, can we find a fast implementation of sin() somewhere?
        // NOTE: the DSP library does not seem to do this any faster than the sin() function in a loop, do we need the library?
        // NOTE: if we generate this in a different buffer before taking the semaphore and then use memcpy, we risk less to affect the audio
        dsps_tone_gen_f32(osc->buffer, osc->buffer_size, osc->params.amplitude, osc->params.frequency / SAMPLING_FREQ * osc->downsampling_factor, 0.0);
        break;
    case WAVEFORM_SAWTOOTH:
        for(int i = 0; i < osc->buffer_size; i++)
            /* normalize with respect to sinus */
            osc->buffer[i] = i * osc->params.amplitude * RMS_SINUS / RMS_SAWTOOTH / osc->buffer_size;
        break;
    case WAVEFORM_SQUARE:
        for(int i = 0; i < osc->buffer_size / 2; i++)
            /* normalize with respect to sinus */
            osc->buffer[i] = osc->params.amplitude * RMS_SINUS / RMS_SQUARE;
        for(int i = osc->buffer_size / 2; i < osc->buffer_size; i++)
            osc->buffer[i] = -osc->params.amplitude * RMS_SINUS / RMS_SQUARE;
        break;
    }

    /* calculate RMS value */
    // float rms = 0.0;
    // for(int i = 0; i < osc->buffer_size; i++) {
    //     rms += osc->buffer[i] * osc->buffer[i];
    // }
    // rms /= osc->buffer_size;
    // printf("RMS: %.2f\n", sqrt(rms));
}

static void oscillator_update(oscillator_t *osc, oscillator_params_t *params)
{
    if((params->frequency * osc->downsampling_factor < 10.0) || (params->frequency * osc->downsampling_factor > SAMPLING_FREQ / 2.0)) {
        printf("Invalid frequency\n");
        return;
    }

    xSemaphoreTake(m_osc_sem, portMAX_DELAY);

    memcpy(&osc->params, params, sizeof(oscillator_params_t));

    oscillator_calculate_buffer(osc);

    xSemaphoreGive(m_osc_sem);
}

static void envelope_update(envelope_t *envelope, envelope_params_t *envelope_params)
{
    if(envelope_params->attack <= 0.0) {
        printf("Invalid attack value: %.2f\n", envelope_params->attack);
        return;
    }
    if(envelope_params->decay <= 0.0) {
        printf("Invalid attack value: %.2f\n", envelope_params->decay);
        return;
    }
    if(envelope_params->release <= 0.0) {
        printf("Invalid release value: %.2f\n", envelope_params->release);
        return;
    }
    if((envelope_params->sustain < 0.0) || (envelope_params->sustain > 1.0)) {
        printf("Invalid sustain value: %.2f\n", envelope_params->sustain);
        return;
    }

    /* calculate required size for attack and decay and check if buffer is large enough */
    uint32_t attack_size = envelope_params->attack * SAMPLING_FREQ / ENVELOPE_DOWNSAMPLING;
    uint32_t decay_size = envelope_params->decay * SAMPLING_FREQ / ENVELOPE_DOWNSAMPLING;
    if(attack_size + decay_size > SAMPLING_FREQ / 10) {
        printf("Attack/decay buffer too small\n");
        return;
    }

    /* calculate required size for release and check if buffer is large enough */
    uint32_t release_size = envelope_params->release * SAMPLING_FREQ / ENVELOPE_DOWNSAMPLING;
    if(release_size > SAMPLING_FREQ / 10) {
        printf("Release buffer too small\n");
        return;
    }

    xSemaphoreTake(m_osc_sem, portMAX_DELAY);

    memcpy(&envelope->params, envelope_params, sizeof(envelope_params_t));
    
    /* calculate envelope attack_decay_buffer */
    envelope->attack_decay_buffer_size = attack_size + decay_size;
    for(int i = 0; i < attack_size; i++) {
        float t = (float) i * ENVELOPE_DOWNSAMPLING / SAMPLING_FREQ;
        /* NOTE: we will reach 95 % of the final amplitude */
        envelope->attack_decay_buffer[i] = envelope->params.amplitude * (1.0 - exp(-3.0 * t / envelope->params.attack));
    }
    for(int i = attack_size; i < envelope->attack_decay_buffer_size; i++) {
        float t = (float) (i - attack_size) * ENVELOPE_DOWNSAMPLING / SAMPLING_FREQ;
        /* NOTE: Similarly as above, we use an exponential evolution with a speedup of 3.
         *       We also correct the amplitude to 95% (see above)
         */
        envelope->attack_decay_buffer[i] = 0.95 * envelope->params.amplitude * (1.0 - (
            (1.0 - envelope->params.sustain) * (1.0 - exp(-3.0 * t / envelope->params.decay))
        ));
    }

    /* calculate envelope release_buffer */
    envelope->release_buffer_size = release_size;
    for(int i = 0; i < envelope->release_buffer_size; i++) {
        float t = (float) i * ENVELOPE_DOWNSAMPLING / SAMPLING_FREQ;
        envelope->release_buffer[i] = envelope->attack_decay_buffer[envelope->attack_decay_buffer_size - 1] * exp(-3.0 * t / envelope->params.release);
    }

    xSemaphoreGive(m_osc_sem);
}

// TODO: this is MIDI specific and should be in midi_input.c
static float frequency_from_key(uint8_t key)
{
    float m = (float) key;

    // see https://newt.phys.unsw.edu.au/jw/notes.html
    return 440.0 * pow(2.0, ((m - 69.0) / 12.0));
}

static void synth_update(oscillator_params_t *osc1_params, oscillator_params_t *osc2_params,
                            oscillator_params_t *lfo_params, envelope_params_t *envelope_params,
                            synth_params_t *synth_params)
{
    xSemaphoreTake(m_osc_sem, portMAX_DELAY);
    memcpy(&m_synth_params, synth_params, sizeof(synth_params_t));
    xSemaphoreGive(m_osc_sem);

    oscillator_update(&m_osc1, osc1_params);
    oscillator_update(&m_osc2, osc2_params);
    oscillator_update(&m_lfo, lfo_params);
    envelope_update(&m_envelope, envelope_params);
}

static void synth_update_freq(oscillator_t *osc, float freq)
{
    if((freq * osc->downsampling_factor < 10.0) || (freq * osc->downsampling_factor > SAMPLING_FREQ / 2.0)) {
        printf("Invalid frequency\n");
        return;
    }

    xSemaphoreTake(m_osc_sem, portMAX_DELAY);

    osc->params.frequency = freq;
    oscillator_calculate_buffer(osc);

    xSemaphoreGive(m_osc_sem);
}

static void synth_update_amp(oscillator_t *osc, float amp)
{
    xSemaphoreTake(m_osc_sem, portMAX_DELAY);

    osc->params.amplitude = amp;
    oscillator_calculate_buffer(osc);

    xSemaphoreGive(m_osc_sem);
}

static void synth_update_waveform(oscillator_t *osc, waveform_t wf)
{
    xSemaphoreTake(m_osc_sem, portMAX_DELAY);

    osc->params.waveform = wf;
    oscillator_calculate_buffer(osc);

    xSemaphoreGive(m_osc_sem);
}

void synth_update_osc1_freq(float freq)
{
    printf("Upating OSC1 frequency: %.2f Hz\n", freq);
    synth_update_freq(&m_osc1, freq);
}

void synth_update_osc1_waveform(waveform_t wf)
{
    printf("Updating OSC1 waveform: %d\n", (int) wf);
    synth_update_waveform(&m_osc1, wf);
}

void synth_update_osc2_freq(float freq)
{
    printf("Upating OSC2 frequency: %.2f Hz\n", freq);
    synth_update_freq(&m_osc2, freq);
}

void synth_update_osc2_amp(float amp)
{
    printf("Updating OSC2 amplitude: %.2f\n", amp);
    synth_update_amp(&m_osc2, amp);
}

void synth_update_osc2_waveform(waveform_t wf)
{
    printf("Updating OSC2 waveform: %d\n", (int) wf);
    synth_update_waveform(&m_osc2, wf);
}

void synth_update_lfo_freq(float freq)
{
    printf("Upating LFO frequency: %.2f Hz\n", freq);
    synth_update_freq(&m_lfo, freq);
}

void synth_update_lfo_waveform(waveform_t wf)
{
    printf("Updating LFO waveform: %d\n", (int) wf);
    synth_update_waveform(&m_lfo, wf);
}

void synth_enable_lfo(uint8_t enabled)
{
    xSemaphoreTake(m_osc_sem, portMAX_DELAY);

    m_synth_params.lfo_enabled = enabled;

    xSemaphoreGive(m_osc_sem);
}

void synth_enable_osc2_sync(uint8_t enabled)
{
    xSemaphoreTake(m_osc_sem, portMAX_DELAY);

    m_synth_params.osc2_sync_enabled = enabled;

    xSemaphoreGive(m_osc_sem);
}

void synth_key_press(uint8_t key, uint8_t velocity)
{
    envelope_params_t params;

    m_last_key_pressed = key;

    synth_update_osc1_freq(frequency_from_key(key));

    // TODO: this is a very inefficient way of updating the envelope's
    //       amplitude; the amplitude could simply be multiplied in 
    //       synth_calculate_buffer()
    // TODO: implement a better model to map velocity to amplitude:
    //       https://www.cs.cmu.edu/~rbd/papers/velocity-icmc2006.pdf
    memcpy(&params, &m_envelope.params, sizeof(envelope_params_t));
    params.amplitude = (float) velocity / 127.0;
    envelope_update(&m_envelope, &params);

    xSemaphoreTake(m_osc_sem, portMAX_DELAY);

    m_envelope.trigger_offset = m_buf.offset;
    m_envelope.release_offset = 0xFFFFFFFF;   // far in the future

    xSemaphoreGive(m_osc_sem);
}

void synth_key_release(uint8_t key)
{
    if(key != m_last_key_pressed)
        return;

    xSemaphoreTake(m_osc_sem, portMAX_DELAY);

    m_envelope.release_offset = m_buf.offset;

    xSemaphoreGive(m_osc_sem);
}

int synth_init(oscillator_params_t *osc1_params, oscillator_params_t *osc2_params,
                oscillator_params_t *lfo_params, envelope_params_t *envelope_params,
                synth_params_t *synth_params)
{
    /* set up semaphore for parameter change */
    m_osc_sem = xSemaphoreCreateBinary();
    xSemaphoreGive(m_osc_sem);

    /* set parameters */
    m_osc1.downsampling_factor = 1;
    m_osc2.downsampling_factor = 1;
    m_lfo.downsampling_factor = 100;

    m_envelope.trigger_offset = 0xFFFFFFFF;
    m_envelope.release_offset = 0xFFFFFFFF;

    synth_update(osc1_params, osc2_params, lfo_params, envelope_params, synth_params);

    /* start synth_task */
    xTaskCreatePinnedToCore(synth_task, "synth_task", 4096, NULL, 1, NULL, 1);

    return 0;
}