#ifndef SYNTH_H
#define SYNTH_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WAVEFORM_SINUS,
    WAVEFORM_SAWTOOTH,
    WAVEFORM_SQUARE,
} waveform_t;

typedef struct {
    float amplitude;
    float frequency;
    waveform_t waveform;
} oscillator_params_t;

typedef struct {
    float attack;
    float decay;
    float sustain;        // as a fraction of amplitude
    float release;
    float amplitude;
} envelope_params_t;

typedef struct {
    uint8_t lfo_enabled;
    uint8_t osc2_sync_enabled;
} synth_params_t;

int synth_init(oscillator_params_t *osc1_params, oscillator_params_t *osc2_params,
                oscillator_params_t *lfo_params, envelope_params_t *envelope_params,
                synth_params_t *synth_params);
void synth_key_press(uint8_t key, uint8_t velocity);
void synth_key_release(uint8_t key);

void synth_get_params(oscillator_params_t *osc1_params, oscillator_params_t *osc2_params,
                        oscillator_params_t *lfo_params);

void synth_enable_lfo(uint8_t enabled);
void synth_enable_osc2_sync(uint8_t enabled);

void synth_update_osc1_freq(float freq);
void synth_update_osc1_waveform(waveform_t wf);
void synth_update_osc2_freq(float freq);
void synth_update_osc2_amp(float amp);
void synth_update_osc2_waveform(waveform_t wf);
void synth_update_lfo_freq(float freq);
void synth_update_lfo_waveform(waveform_t wf);

#ifdef __cplusplus
}
#endif

#endif // SYNTH_H
