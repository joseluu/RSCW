#ifndef SIM_ARM_MATH_H
#define SIM_ARM_MATH_H
#include <cmath>
typedef float float32_t;
inline void arm_scale_f32(const float *s, float k, float *d, int n) { for (int i = 0; i < n; i++) d[i] = s[i] * k; }
inline void arm_add_f32(const float *a, const float *b, float *d, int n) { for (int i = 0; i < n; i++) d[i] = a[i] + b[i]; }
inline float arm_sin_f32(float x) { return sinf(x); }
#endif
