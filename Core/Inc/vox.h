#ifndef __VOX_H
#define __VOX_H

#include "stdbool.h"
#include "stdint.h"

void vox_init(void);
void vox_set_params(bool on, uint8_t gain, uint8_t anti_gain, uint16_t ms);
float vox_process_audio_sample(float sample);
float vox_update(float out_audio_sample);
void vox_compute(void);

void vox_restore_audio_input(uint8_t *use_internal_mic, uint8_t hmic, uint8_t line_in);

// For test
void vox_get_levels(int8_t *db);

void vox_stop(void);

#endif //__VOX_H
