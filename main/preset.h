#ifndef PRESET_H
#define PRESET_H

#ifdef __cplusplus
extern "C" {
#endif

void preset_select(int index);
void preset_save(void);
int preset_get_current_index(void);

#ifdef __cplusplus
}
#endif

#endif // PRESET_H
