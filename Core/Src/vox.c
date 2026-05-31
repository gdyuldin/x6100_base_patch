#include "vox.h"
#include "external.h"
#include "utils.h"
#include "comm.h"

#include <arm_math.h>

#define VOX_DECIM_K 32
#define VOX_DLINE 64
#define VOX_TX_OFF_HADOVER 500

// AIC in channels
#define LEFT_CH 1
#define RIGHT_CH 2

// AIC inputs
#define IN_2 2
#define IN_3 4


static CCMRAM struct {
    bool on;
    float gain_th;
    float anti_gain_th;
    uint16_t on_delay;
    uint16_t on_counter;
    uint16_t off_counter;
    bool running;

    float in_mic;
    float in_line;
    float out;

    // Delay line
    struct {
        float data[VOX_DLINE];
        uint8_t w;
        uint8_t r;
    } dline;

    // for debugging/reporting to linux
    float in_db;
    float out_db;
} vox;

static void vox_start(void);

void vox_init(void) {
    vox.on = false;
    vox.gain_th = 0.0f;
    vox.anti_gain_th = 0.0f;
    vox.on_delay = 200;

    vox.on_counter = 0;
    vox.off_counter = VOX_TX_OFF_HADOVER;
    vox.running = false;

    vox.in_mic = 0.0f;
    vox.in_line = 0.0f;
    vox.out = 0.0f;

    vox.dline.r = 0;
    vox.dline.w = 0;

    vox.in_db = 0.0f;
    vox.out_db = 0.0f;
}

void vox_set_params(bool on, uint8_t gain, uint8_t anti_gain, uint16_t ms) {
    vox.on = on;
    ms = CLIP(ms, 100, 2000);
    vox.on_delay = (ms * 25) / 32;
    // vox gain th range - 0 ... -100
    gain = MIN(gain, 100);
    vox.gain_th = -(float)gain;
    // vox anti-gain range -0 ... -100
    anti_gain = MIN(anti_gain, 100);
    vox.anti_gain_th = -(float)anti_gain;
    if (!vox.on) {
        vox.dline.r = 0;
        vox.dline.w = 0;
        vox.off_counter = VOX_TX_OFF_HADOVER;
        if (vox.running) {
            vox_stop();
        }
    }
}

/**
 * Add a delay for TX audio, if vox is enabled
 */
float vox_process_audio_sample(float sample) {
    if (!vox.on) {
        return sample;
    }
    vox.dline.data[vox.dline.w++] = sample;
    vox.dline.w &= (VOX_DLINE - 1);
    if (vox.dline.w == vox.dline.r) {
        sample = vox.dline.data[vox.dline.r++];
        vox.dline.r &= (VOX_DLINE - 1);
    } else {
        sample = 0.0f;
    }
    return sample;
}

float vox_update(float out_audio_sample) {
    float *in_audio = (float*)TX_AUDIO_FLOAT_IN;
    register uint32_t loop_counter __asm("r4");

    if (loop_counter & (VOX_DECIM_K - 1)) {
        return out_audio_sample;
    }

    float in_sample_mic = in_audio[loop_counter];
    float in_sample_line = in_audio[loop_counter + 0x80];

    vox.in_mic += in_sample_mic * in_sample_mic;
    vox.in_line += in_sample_line * in_sample_line;
    vox.out += out_audio_sample * out_audio_sample;

    return out_audio_sample;
}

void vox_compute(void)
{
    USE_OEM_MODULATION_AS(pMode);

    float avg_k = (float)VOX_DECIM_K / 128;

    float in;
    switch (*pMode) {
        case MOD_LSB_D:
        case MOD_USB_D:
            in = vox.in_line * avg_k;
            break;
        default:
            in = (vox.in_line + vox.in_mic) * avg_k;
            break;
    }

    if (in > 0) {
        in = pow2db(in);
    } else {
        in = -100.0f;
    }

    float out = -100.0f;
    if (vox.out > 0) {
        out = pow2db(vox.out * avg_k);
        out += get_rx_vol();
        out -= 60.0f;
    }

    vox.in_db = in;
    vox.out_db = out;

    // Reset accum
    vox.in_mic = 0.0f;
    vox.in_line = 0.0f;
    vox.out = 0.0f;

    if (!vox.on) {
        return;
    }

    USE_OEM_TX_STATE_FLAGS_AS(txState);
    if (*txState & ~(TX_STATE_VOX)) {
        // Non-vox TX
        *txState &= ~TX_STATE_VOX;
        vox.running = false;
        vox.off_counter = VOX_TX_OFF_HADOVER;
        vox.on_counter = 0;
        return;
    }

    switch (*pMode) {
        case MOD_CW:
        case MOD_CWR:
            return;
    }

    if (vox.off_counter) {
        vox.off_counter--;
    }

    if (!vox.off_counter && (in > vox.gain_th) && (out < vox.anti_gain_th)) {
        if (!vox.running) {
            // Turn on TX
            vox_start();
        }
        vox.on_counter = vox.on_delay;
    }

    if (vox.on_counter) {
        vox.on_counter--;
        if (!vox.on_counter && vox.running) {
            // Turn off TX
            vox_stop();
        }
    }
}

void vox_restore_audio_input(uint8_t *use_internal_mic, uint8_t hmic, uint8_t line_in)
{
    if (*use_internal_mic != 0) {
        *use_internal_mic = 0;
        ext_setup_internal_mic_power(0);
    }
    ext_set_mic_level(LEFT_CH, hmic);
    ext_set_audio_codec_input(LEFT_CH, IN_2);

    ext_set_mic_level(RIGHT_CH, line_in);
    ext_set_audio_codec_input(RIGHT_CH, IN_2 | IN_3);
}

float vox_get_audio_in_lvl() {
    return vox.in_db;
}

void vox_stop(void)
{
    USE_OEM_TX_STATE_FLAGS_AS(txState);
    *txState &= ~TX_STATE_VOX;

    void *struct_data = (void*)0x2000008c;
    uint32_t *tx_state_flags = (uint32_t*)0x200001b8;
    ext_setup_tx(struct_data, *tx_state_flags, 0);
    vox.running = false;
    vox.off_counter = VOX_TX_OFF_HADOVER;
    vox.on_counter = 0;
}


static void vox_start(void) {
    USE_OEM_TX_STATE_FLAGS_AS(txState);
    *txState |= TX_STATE_VOX;
    // Call setup tx
    void *struct_data = (void*)0x2000008c;
    uint32_t *tx_state_flags = (uint32_t*)0x200001b8;
    // Force external mic
    // *tx_state_flags |= 2;
    ext_setup_tx(struct_data, *tx_state_flags | 2, 1);
    vox.running = true;
}
