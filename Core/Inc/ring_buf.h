#ifndef __RING_BUF_H
#define __RING_BUF_H

#include "stdint.h"

/**
 * Ring buffer struct
 */

struct ring_buf {
    float *data;
    uint32_t w;
    uint32_t r;
    uint32_t size;
};


void ring_buf_put(struct ring_buf *buf, float val);

float ring_buf_get(struct ring_buf *buf);

uint8_t ring_buf_full(struct ring_buf *buf);

void ring_buf_reset(struct ring_buf *buf);



#endif //__RING_BUF_H
