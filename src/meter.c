#include "meter.h"
#include <math.h>
#include <string.h>

static float held_value(PeakMeter *m, int64_t now) {
    if (m->held <= 0.0f || now <= m->hold_until) return m->held;
    float db = linear_to_db(m->held) - (float)(((double)(now - m->hold_until) / (double)m->frequency) * 18.0);
    float value = powf(10.0f, db / 20.0f);
    return m->latest > value ? m->latest : value;
}

void peak_init(PeakMeter *m, int64_t frequency, int hold_ms) {
    memset(m, 0, sizeof(*m));
    m->frequency = frequency;
    m->hold_duration = frequency * hold_ms / 1000;
    m->clip_duration = frequency * 3;
}

float peak_update(PeakMeter *m, const float *samples, size_t count, unsigned channels, int64_t now) {
    float left = 0.0f, right = 0.0f;
    if (channels == 1) {
        for (size_t i = 0; i < count; ++i) {
            float v = fabsf(samples[i]);
            if (v > left) left = v;
        }
        right = left;
    } else if (channels == 2) {
        for (size_t i = 0; i + 1 < count; i += 2) {
            float l = fabsf(samples[i]), r = fabsf(samples[i + 1]);
            if (l > left) left = l;
            if (r > right) right = r;
        }
    }
    if (left > m->display_left) m->display_left = left;
    if (right > m->display_right) m->display_right = right;
    m->latest = left > right ? left : right;
    if (m->latest >= held_value(m, now)) {
        m->held = m->latest;
        m->hold_until = now + m->hold_duration;
    }
    if (m->latest >= 1.0f) m->clip_until = now + m->clip_duration;
    return m->latest;
}

PeakReading peak_read(PeakMeter *m, int64_t now) {
    PeakReading r = {m->display_left, m->display_right, held_value(m, now), now < m->clip_until};
    m->display_left = m->display_right = m->latest = 0.0f;
    return r;
}

void peak_reset(PeakMeter *m) {
    int64_t frequency = m->frequency, hold = m->hold_duration, clip = m->clip_duration;
    memset(m, 0, sizeof(*m));
    m->frequency = frequency;
    m->hold_duration = hold;
    m->clip_duration = clip;
}

float linear_to_db(float value) {
    return value > 0.0f ? 20.0f * log10f(value) : -INFINITY;
}

double display_normalize_reference(double value) {
    if (!isfinite(value)) return 0.0;
    if (value < -30.0) return -30.0;
    if (value > 0.0) return 0.0;
    return value;
}

double display_adjust(double value, double reference) {
    return isfinite(value) ? value - display_normalize_reference(reference) : value;
}
bool display_should_show(double value) {
    return isfinite(value) && value >= -99.0;
}

size_t pcm_to_float(const unsigned char *source, size_t bytes, unsigned bits, float *out, size_t capacity) {
    size_t width = bits / 8, count = width ? bytes / width : 0;
    if (count > capacity || (bits != 16 && bits != 24 && bits != 32)) return 0;
    for (size_t i = 0, p = 0; i < count; ++i, p += width) {
        if (bits == 16) {
            int16_t v = (int16_t)((uint16_t)source[p] | ((uint16_t)source[p + 1] << 8));
            out[i] = (float)v / 32768.0f;
        } else if (bits == 24) {
            int32_t v = (int32_t)((uint32_t)source[p] | ((uint32_t)source[p + 1] << 8) | ((uint32_t)source[p + 2] << 16));
            if (v & 0x00800000) v |= (int32_t)0xff000000;
            out[i] = (float)v / 8388608.0f;
        } else {
            int32_t v;
            memcpy(&v, source + p, sizeof(v));
            out[i] = (float)((double)v / 2147483648.0);
        }
    }
    return count;
}
