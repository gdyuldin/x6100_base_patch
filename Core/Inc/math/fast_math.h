#include <arm_math.h>

__STATIC_FORCEINLINE void cmplx_mul(
    float real_src_1, float imag_src_1,
    float real_src_2, float imag_src_2,
    float *real_dst, float *imag_dst)
{
    float m1 = real_src_1 * real_src_2;
    float m2 = imag_src_1 * imag_src_2;
    float m3 = real_src_1 * imag_src_2;
    float m4 = imag_src_1 * real_src_2;
    *real_dst = m1 - m2;
    *imag_dst = m3 + m4;
}
