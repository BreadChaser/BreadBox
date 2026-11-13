#pragma once
// Shared, portable menu motion. No allocation, overshoot, or frame-count clock.
#include <math.h>
#include "ui_style.h"
static inline float ui_motion_ease(float t)
{
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    return t * t * (3 - 2 * t);
}
// Exact critically damped spring, stable at either 30 or 60 Hz and after a stall.
static inline void ui_motion_settle(float *x, float *v, float target, float rate, float dt)
{
    if (dt <= 0) return;
    float d = *x - target, c = *v + rate * d, e = expf(-rate * dt);
    *x = target + (d + c * dt) * e;
    *v = (*v - rate * c * dt) * e;
}
