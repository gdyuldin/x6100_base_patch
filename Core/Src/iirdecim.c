
#include "iirdecim.h"
#include "utils.h"
#include <dsp/filtering_functions.h>


#define IIR_NUM_STAGES 5

struct iir_filter_t {
    float state[IIR_NUM_STAGES * 4];
    arm_biquad_casd_df1_inst_f32 S;
};

typedef struct iir_filter_t iq_iir_filter_t[2];

__attribute__((section(".rodata.patch_data"))) const float iir_coeffs_2[IIR_NUM_STAGES * 5] = {
0.006772198619321254f, 0.012443804843624128f, 0.006772198619321254f, 1.0486425989169934f, -0.33895965074971035f,
1.0f, 1.051072412148685f, 1.0f, 0.7371461853443527f, -0.5479789762973442f,
1.0f, 0.4734490259549523f, 1.0f, 0.4281439684027536f, -0.7586984763245501f,
1.0f, 0.19924074909479517f, 0.9999999999999998f, 0.24722444954782677f, -0.8914186624539844f,
1.0f, 0.09591248891177054f, 1.0f, 0.17299438529114564f, -0.9693321469017971f,
};

__attribute__((section(".rodata.patch_data"))) const float iir_coeffs_4[IIR_NUM_STAGES * 5] = {
0.0005984754661682862f, 0.0007338701594314901f, 0.0005984754661682863f, 1.5604621337659632f, -0.6289408353171592f,
1.0f, -0.5514317651270004f, 1.0f, 1.525503046397621f, -0.7294421716535121f,
1.0f, -1.110163156718519f, 0.9999999999999999f, 1.4873949458219786f, -0.8454648109942025f,
1.0f, -1.2904164427894729f, 1.0f, 1.466785828578274f, -0.92758248579901f,
1.0f, -1.3490552728777523f, 1.0f, 1.4687469193141043f, -0.9792588197155409f,
};

__attribute__((section(".rodata.patch_data"))) const float iir_coeffs_8[IIR_NUM_STAGES * 5] = {
0.00018257731643477713f, -5.090910074284355e-06f, 0.0001825773164347771f, 1.780934888230156f, -0.7988619347117181f,
1.0f, -1.5322529669457468f, 0.9999999999999998f, 1.7999544443790456f, -0.8533427471914157f,
1.0f, -1.7497717775937058f, 0.9999999999999999f, 1.8225854177734075f, -0.9162756475526946f,
1.0f, -1.8084403319531133f, 1.0000000000000004f, 1.8404716614921386f, -0.9608330767329305f,
1.0f, -1.8265274070855686f, 1.0000000000000002f, 1.8560523476647752f, -0.988830064366675f,
};


static iq_iir_filter_t filter_2 __attribute((section(".ccmram")));
static iq_iir_filter_t filter_4 __attribute((section(".ccmram")));
static iq_iir_filter_t filter_8 __attribute((section(".ccmram")));

#define BLOCK_SIZE 16

static void iir_decim_iq(iq_iir_filter_t flt, float *pSrc, float *pDst, uint32_t countSrc, uint8_t N) {
    static float srcI[BLOCK_SIZE] __attribute((section(".ccmram")));
    static float srcQ[BLOCK_SIZE] __attribute((section(".ccmram")));
    static float dstI[BLOCK_SIZE] __attribute((section(".ccmram")));
    static float dstQ[BLOCK_SIZE] __attribute((section(".ccmram")));

    while (countSrc)
    {
        for (uint8_t i = 0; i < BLOCK_SIZE; i+=2)
        {
            srcI[i] = *pSrc++;
            srcQ[i] = *pSrc++;
            srcI[i+1] = *pSrc++;
            srcQ[i+1] = *pSrc++;
        }
        ext_arm_biquad_cascade_df1_f32(&flt[0].S, srcI, dstI, BLOCK_SIZE);
        ext_arm_biquad_cascade_df1_f32(&flt[1].S, srcQ, dstQ, BLOCK_SIZE);
        for (uint8_t i = 0; i < BLOCK_SIZE; i+=N)
        {
            *pDst++ = dstI[i];
            *pDst++ = dstQ[i];
        }
        countSrc -= BLOCK_SIZE;
    }
}

void iirdecim_init()
{
    for (uint8_t j = 0; j < 2; j++)
    {
        filter_2[j].S.numStages = IIR_NUM_STAGES;
        filter_2[j].S.pCoeffs = iir_coeffs_2;
        filter_2[j].S.pState = filter_2[j].state;
        filter_4[j].S.numStages = IIR_NUM_STAGES;
        filter_4[j].S.pCoeffs = iir_coeffs_4;
        filter_4[j].S.pState = filter_4[j].state;
        filter_8[j].S.numStages = IIR_NUM_STAGES;
        filter_8[j].S.pCoeffs = iir_coeffs_8;
        filter_8[j].S.pState = filter_8[j].state;
    }
    iirdecim_reset();
}

void iirdecim_reset()
{
    for (uint8_t j = 0; j < 2; j++)
    {
        ext_arm_fill_f32(0.0f, filter_2[j].state, ARRAY_SIZE(filter_2[j].state));
        ext_arm_fill_f32(0.0f, filter_4[j].state, ARRAY_SIZE(filter_4[j].state));
        ext_arm_fill_f32(0.0f, filter_8[j].state, ARRAY_SIZE(filter_8[j].state));
    }
}

void iir_decim_iq_n(uint8_t N, float *pSrc, float *pDst, uint32_t countSrc) {
    switch (N)
    {
    case 1:
        iir_decim_iq(filter_2, pSrc, pDst, countSrc, 2);
        break;
    case 2:
        iir_decim_iq(filter_4, pSrc, pDst, countSrc, 4);
        break;
    case 3:
        iir_decim_iq(filter_8, pSrc, pDst, countSrc, 8);
        break;

    default:
        if (pSrc != pDst) {
            ext_arm_copy_f32(pSrc, pDst, countSrc << 1);
        }
        break;
    }
}
