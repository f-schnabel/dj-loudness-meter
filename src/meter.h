#ifndef DJLM_METER_H
#define DJLM_METER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    int64_t frequency, hold_duration, clip_duration;
    float display_left, display_right, latest, held;
    int64_t hold_until, clip_until;
} PeakMeter;

typedef struct { float left, right, hold; bool clipping; } PeakReading;

void peak_init(PeakMeter *meter, int64_t frequency, int hold_ms);
float peak_update(PeakMeter *meter, const float *samples, size_t count, unsigned channels, int64_t now);
PeakReading peak_read(PeakMeter *meter, int64_t now);
void peak_reset(PeakMeter *meter);
float linear_to_db(float amplitude);
double display_adjust(double value, double reference_dbfs);
double display_normalize_reference(double reference_dbfs);
bool display_should_show(double value);
size_t pcm_to_float(const unsigned char *source, size_t bytes, unsigned bits, float *output, size_t capacity);

#endif

