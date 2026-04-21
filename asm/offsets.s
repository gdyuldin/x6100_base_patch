.if BUILD_DATE == 221112001

.equ GET_DMA_CR_REGISTER_VALUE,   0x0802cf30
.equ ARM_BIQUAD_CASCADE_DF1_F32,  0x0803436c
.equ ARM_FIR_DECIMATE_F32,        0x08035ce0
.equ ORIG_INIT_DATA,              0x08032bf8
.equ ORIG_TX_AMP,                 0x080336d0
.equ TX_I_SIGNAL_OFFSET,          0x64
.equ RX_SP_OFFSET,                0x57
.equ RX_I_SIGNAL,                 0x20008138
.equ RX_Q_SIGNAL,                 0x2000813c
RX_I_REGISTER .req                s16
RX_Q_REGISTER .req                s17

.elseif BUILD_DATE == 230307001

.equ GET_DMA_CR_REGISTER_VALUE,   0x0802e190
.equ ARM_BIQUAD_CASCADE_DF1_F32,  0x08036024
.equ ARM_FIR_DECIMATE_F32,        0x08037998
.equ ORIG_INIT_DATA,              0x08034870
.equ ORIG_TX_AMP,                 0x08035388
.equ TX_I_SIGNAL_OFFSET,          0x114
.equ RX_SP_OFFSET,                0x107
.equ RX_I_SIGNAL,                 0x20008140
.equ RX_Q_SIGNAL,                 0x20008144
.equ GET_BATTERY_DATA_MAYBE,      0x0802d780
.equ AM_FM_DEMOD,                 0x20008148
.equ DEMOD_AUDIO,                 0x2000834c
.equ AFTER_OEM_NR_ADDR,           0x08024d88
.equ AFTER_OEM_AM_MULT_ADDR,      0x08024d2c
RX_I_REGISTER .req                s16
RX_Q_REGISTER .req                s22

.endif

.equ CCMRAM_STACK_VAL,     0x10010000
.equ CCMRAM_STACK_MIN,     0x10005000
