#include "meter.h"
#include "settings.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>

static void assert_near(double actual, double expected, double tolerance) {
    assert(fabs(actual - expected) <= tolerance);
}

static void test_peaks(void) {
    PeakMeter meter;
    peak_init(&meter, 10000000, 2000);
    float stereo[] = {0.1f, -0.25f, -0.5f, 0.125f};
    peak_update(&meter, stereo, 4, 2, 100);
    PeakReading reading = peak_read(&meter, 100);
    assert_near(reading.left, 0.5, 0.00001);
    assert_near(reading.right, 0.25, 0.00001);
    peak_reset(&meter);
    float clip[] = {1.0f, 0.0f};
    peak_update(&meter, clip, 2, 2, 100);
    assert(peak_read(&meter, 100).clipping);

    peak_init(&meter, 1000, 1000);
    float mono[] = {-0.5f};
    peak_update(&meter, mono, 1, 1, 0);
    reading = peak_read(&meter, 1000);
    assert_near(reading.left, 0.5, 0.00001);
    assert_near(reading.right, 0.5, 0.00001);
    reading = peak_read(&meter, 2000);
    assert_near(reading.hold, 0.062946, 0.00001);
    assert(!reading.clipping);
}

static void test_pcm(void) {
    float output[4];
    unsigned char pcm16[] = {0x00, 0x80, 0x00, 0xc0, 0x00, 0x00, 0xff, 0x7f};
    assert(pcm_to_float(pcm16, sizeof(pcm16), 16, output, 4) == 4);
    assert_near(output[0], -1, 0);
    assert_near(output[1], -0.5, 0);
    assert_near(output[2], 0, 0);
    assert_near(output[3], 0.999969, 0.00001);
    unsigned char pcm24[] = {0x00, 0x00, 0x80, 0xff, 0xff, 0x7f};
    assert(pcm_to_float(pcm24, sizeof(pcm24), 24, output, 4) == 2);
    assert_near(output[0], -1, 0);
    assert_near(output[1], 1, 0.00001);
    int32_t pcm32[] = {INT32_MIN, INT32_MAX};
    assert(pcm_to_float((unsigned char *)pcm32, sizeof(pcm32), 32, output, 4) == 2);
    assert_near(output[0], -1, 0);
    assert_near(output[1], 1, 0.00001);
    assert(pcm_to_float(pcm16, sizeof(pcm16), 8, output, 4) == 0);
    assert(pcm_to_float(pcm16, sizeof(pcm16), 16, output, 3) == 0);
}

static void test_display(void) {
    assert_near(display_adjust(-9, -9), 0, 0);
    assert_near(display_adjust(-6, -9), 3, 0);
    assert(isinf(display_adjust(-INFINITY, -9)));
    assert_near(display_normalize_reference(-50), -30, 0);
    assert_near(display_normalize_reference(5), 0, 0);
    assert(display_should_show(-99));
    assert(!display_should_show(-99.01));
    assert(!display_should_show(-INFINITY));
}

static void test_settings_normalization(void) {
    AppSettings settings;
    settings_defaults(&settings);
    assert(settings.window_width == SETTINGS_MIN_WIDTH && settings.window_height == SETTINGS_MIN_HEIGHT);

    settings.window_width = -1;
    settings.window_height = 99999;
    settings.refresh_ms = 0;
    settings.peak_hold_ms = 99999;
    settings.show_loudness = false;
    settings.show_system = false;
    settings.display_zero = -99.0;
    settings_normalize(&settings);

    assert(settings.window_width == SETTINGS_MIN_WIDTH && settings.window_height == 16384);
    assert(settings.refresh_ms == 10 && settings.peak_hold_ms == 60000);
    assert(settings.show_loudness && !settings.show_system);
    assert_near(settings.display_zero, -30.0, 0.0);

    settings.refresh_ms = 99999;
    settings.peak_hold_ms = -1;
    settings.display_zero = 5.0;
    settings_normalize(&settings);
    assert(settings.refresh_ms == 2000 && settings.peak_hold_ms == 0);
    assert_near(settings.display_zero, 0.0, 0.0);
}

int main(void) {
    test_peaks();
    test_pcm();
    test_display();
    test_settings_normalization();
    puts("native tests passed");
    return 0;
}
