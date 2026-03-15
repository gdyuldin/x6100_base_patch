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
