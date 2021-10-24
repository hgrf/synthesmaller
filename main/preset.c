#include "preset.h"
#include "synth.h"

#include "esp_log.h"
#include "esp_vfs_fat.h"

static const char *TAG = "PRESET";

static int current_preset_index;

static oscillator_params_t osc1_params;
static oscillator_params_t osc2_params;
static oscillator_params_t lfo_params;
static synth_params_t synth_params;
static envelope_params_t envelope_params;

int preset_get_current_index(void)
{
    return current_preset_index;
}

void preset_select(int index)
{
    char *filename;
    FILE *f;
    size_t bytes_read = 0;

    if(index == current_preset_index)
       return;

    current_preset_index = index;

    /* try to load preset from storage */
    asprintf(&filename, "/spiffs/PRESET%d", index);
    f = fopen(filename, "r");
    free(filename);
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file for reading");
        return;
    }

    bytes_read += fread(&osc1_params, 1, sizeof(oscillator_params_t), f);
    bytes_read += fread(&osc2_params, 1, sizeof(oscillator_params_t), f);
    bytes_read += fread(&lfo_params, 1, sizeof(oscillator_params_t), f);
    bytes_read += fread(&envelope_params, 1, sizeof(envelope_params_t), f);
    bytes_read += fread(&synth_params, 1, sizeof(synth_params_t), f);

    ESP_LOGI(TAG, "%u bytes read from PRESET%d", bytes_read, index);

    synth_update(&osc1_params, &osc2_params, &lfo_params, &envelope_params, &synth_params);

    fclose(f);
}

void preset_save(void)
{
    char *filename;
    FILE *f;
    size_t bytes_written = 0;

    asprintf(&filename, "/spiffs/PRESET%d", current_preset_index);
    f = fopen(filename, "w");
    free(filename);
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file for writing");
        return;
    }

    synth_get_params(&osc1_params, &osc2_params, &lfo_params, &envelope_params, &synth_params);

    bytes_written += fwrite(&osc1_params, 1, sizeof(oscillator_params_t), f);
    bytes_written += fwrite(&osc2_params, 1, sizeof(oscillator_params_t), f);
    bytes_written += fwrite(&lfo_params, 1, sizeof(oscillator_params_t), f);
    bytes_written += fwrite(&envelope_params, 1, sizeof(envelope_params_t), f);
    bytes_written += fwrite(&synth_params, 1, sizeof(synth_params_t), f);

    ESP_LOGI(TAG, "%u bytes written to PRESET%d", bytes_written, current_preset_index);

    fclose(f);
}