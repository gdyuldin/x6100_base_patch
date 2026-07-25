#include "utils.h"


float soft_limiter(float val, float max_val) {
    const float th = max_val * 0.5f;
    float x;
    if (val > th) {
        x = th  / val;
        val = (1.0f - x) * (max_val - th) + th;
    } else if (val < -th) {
        x = -th  / val;
        val = -((1.0f - x) * (max_val - th) + th);
    }
    return val;
}


inline float dc_blocker(float val, float k, struct dc_blocker_t *dc) {
    float tmp = val - dc->xm1 + k * dc->ym1;
    dc->xm1 = val;
    dc->ym1 = tmp;
    return tmp;
}

__attribute__((optimize("O1")))
void fill_zero(uint8_t *data, uint32_t size) {
  uint8_t *stop;

  stop = data + size;
  for (; data != stop; data++) {
    *data = 0;
  }
  return;
}


uint8_t bat_cap_by_voltage(float bat_mv) {
    // Voltage to percentage lookup table (voltage, percentage)
    // Based on voltage during discharge
    static const struct
    {
        uint16_t mv;
        uint8_t percent;
    } lut[] = {
        {8400, 100}, // Fully charged
        {8100, 94},
        {7838, 77},
        {7609, 62},
        {7508, 53},
        {7414, 41},
        {7326, 27},
        {7243, 15},
        {7166, 8},
        {7020, 3},
        {6000, 0}, // Safe cutoff
    };

    const uint8_t LUT_SIZE = ARRAY_SIZE(lut);

    // Clamp voltage
    if (bat_mv >= lut[0].mv) {
        return 100;
    }
    if (bat_mv <= lut[LUT_SIZE-1].mv) {
        return 0;
    }

    // Find interpolation interval
    for (uint8_t i = 0; i < LUT_SIZE - 1; i++) {
        if ((bat_mv >= lut[i+1].mv) && (bat_mv < lut[i].mv)) {
            // Linear interpolation between points
            float t = (float)(bat_mv - lut[i+1].mv) / (lut[i].mv - lut[i+1].mv);
            float percent = lut[i+1].percent + t * (lut[i].percent - lut[i+1].percent);
            return (uint8_t)(percent + 0.5f);
        }
    }

    return 0;
}
