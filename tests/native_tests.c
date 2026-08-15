#include "meter.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>

static void near(double actual, double expected, double tolerance) { assert(fabs(actual - expected) <= tolerance); }

static void test_peaks(void) {
    PeakMeter meter; peak_init(&meter, 10000000, 2000);
    float stereo[] = {0.1f, -0.25f, -0.5f, 0.125f}; peak_update(&meter, stereo, 4, 2, 100);
    PeakReading reading = peak_read(&meter, 100); near(reading.left, 0.5, 0.00001); near(reading.right, 0.25, 0.00001);
    peak_reset(&meter); float clip[] = {1.0f, 0.0f}; peak_update(&meter, clip, 2, 2, 100); assert(peak_read(&meter, 100).clipping);
}

static void test_pcm(void) {
    float output[4]; unsigned char pcm16[] = {0x00,0x80, 0x00,0xc0, 0x00,0x00, 0xff,0x7f};
    assert(pcm_to_float(pcm16, sizeof(pcm16), 16, output, 4) == 4); near(output[0], -1, 0); near(output[1], -0.5, 0); near(output[2], 0, 0); near(output[3], 0.999969, 0.00001);
    unsigned char pcm24[] = {0x00,0x00,0x80, 0xff,0xff,0x7f}; assert(pcm_to_float(pcm24, sizeof(pcm24), 24, output, 4) == 2); near(output[0], -1, 0); near(output[1], 1, 0.00001);
    int32_t pcm32[] = {INT32_MIN, INT32_MAX}; assert(pcm_to_float((unsigned char *)pcm32, sizeof(pcm32), 32, output, 4) == 2); near(output[0], -1, 0); near(output[1], 1, 0.00001);
}

static void test_display(void) {
    near(display_adjust(-9, -9), 0, 0); near(display_adjust(-6, -9), 3, 0); assert(isinf(display_adjust(-INFINITY, -9)));
    near(display_normalize_reference(-50), -30, 0); near(display_normalize_reference(5), 0, 0);
    assert(display_should_show(-99)); assert(!display_should_show(-99.01)); assert(!display_should_show(-INFINITY));
}

int main(void) { test_peaks(); test_pcm(); test_display(); puts("native tests passed"); return 0; }
