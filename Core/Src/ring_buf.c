#include "ring_buf.h"


/**
 * Operations with a ring buffer
 */

void ring_buf_put(struct ring_buf *buf, float val) {
    buf->data[buf->w] = val;
    if (buf->w == buf->size - 1) {
        buf->w = 0;
    } else {
        buf->w++;
    }
}

float ring_buf_get(struct ring_buf *buf) {
    float val = buf->data[buf->r];
    if (buf->r == buf->size - 1) {
        buf->r = 0;
    } else {
        buf->r++;
    }
    return val;
}

uint8_t ring_buf_full(struct ring_buf *buf) {
    return buf->w == buf->r;
}

void ring_buf_reset(struct ring_buf *buf) {
    buf->w = 0;
    buf->r = 0;
}
