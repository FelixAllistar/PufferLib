#ifndef SWAT_MATH_H
#define SWAT_MATH_H
#include <math.h>
#include <stdint.h>
#include "box3d/box3d.h"

#define SWAT_PI 3.14159265358979323846f
#define SWAT_DT (1.0f / 60.0f)
#define SWAT_RAD (SWAT_PI / 180.0f)
static inline float swat_clamp(float x, float lo, float hi) {
    return fmaxf(lo, fminf(hi, x));
}
static inline b3Vec3 swat_v(float x, float y, float z) { return (b3Vec3){x,y,z}; }
static inline b3Vec3 swat_add(b3Vec3 a, b3Vec3 b) {
    return swat_v(a.x+b.x, a.y+b.y, a.z+b.z);
}
static inline b3Vec3 swat_mul(b3Vec3 a, float k) { return swat_v(a.x*k,a.y*k,a.z*k); }
static inline b3Vec3 swat_normalize(b3Vec3 a) {
    float n = b3Length(a);
    return n > 1e-6f ? swat_mul(a,1.0f/n) : swat_v(1,0,0);
}
static inline b3Vec3 swat_direction(float yaw, float pitch) {
    return swat_v(cosf(yaw)*cosf(pitch), sinf(pitch), sinf(yaw)*cosf(pitch));
}
static inline float swat_angle(float radians) {
    return remainderf(radians, 2.0f*SWAT_PI);
}
static inline uint32_t swat_random(uint32_t* state) {
    uint32_t x = *state ? *state : 0x9e3779b9u;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    *state = x;
    return x;
}
static inline float swat_rand01(uint32_t* state) {
    return (float)(swat_random(state) >> 8) * (1.0f/16777216.0f);
}
#endif
