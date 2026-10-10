#include "meter.h"
#include "monitors.h"
#include "settings.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

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

static void test_monitor_selection(void) {
    AppSettings settings;
    settings_defaults(&settings);
    DisplayInfo monitors[3] = {0};
    wcscpy_s(monitors[0].device, _countof(monitors[0].device), L"\\\\.\\DISPLAY3");
    wcscpy_s(monitors[0].identity, _countof(monitors[0].identity), L"headset");
    wcscpy_s(monitors[1].device, _countof(monitors[1].device), L"\\\\.\\DISPLAY1");
    wcscpy_s(monitors[1].identity, _countof(monitors[1].identity), L"main");
    monitors[1].primary = true;
    wcscpy_s(monitors[2].device, _countof(monitors[2].device), L"\\\\.\\DISPLAY2");
    wcscpy_s(monitors[2].identity, _countof(monitors[2].identity), L"second");
    assert(monitors_select(monitors, 3, &settings) == 1);
    assert(monitors_select(monitors, 0, &settings) == -1);

    // Existing settings still resolve by GDI name, then gain an identity.
    wcscpy_s(settings.monitor_name, _countof(settings.monitor_name), monitors[2].device);
    assert(monitors_select(monitors, 3, &settings) == 2);
    monitors_remember(&settings, &monitors[2]);
    assert(wcscmp(settings.monitor_id, L"second") == 0);

    // The headset takes the old GDI name; the intended monitor gets a new one.
    wcscpy_s(monitors[0].device, _countof(monitors[0].device), L"\\\\.\\DISPLAY2");
    wcscpy_s(monitors[2].device, _countof(monitors[2].device), L"\\\\.\\DISPLAY4");
    assert(monitors_select(monitors, 3, &settings) == 2);
    DisplayInfo swapped = monitors[0];
    monitors[0] = monitors[2];
    monitors[2] = swapped;
    assert(monitors_select(monitors, 3, &settings) == 0);

    // Missing preferred monitor uses the main display without forgetting the preference.
    assert(monitors_select(monitors + 1, 2, &settings) == 0);
    assert(wcscmp(settings.monitor_id, L"second") == 0);
    assert(monitors_select(monitors, 3, &settings) == 0);
    wcscpy_s(settings.monitor_id, _countof(settings.monitor_id), L"SECOND");
    assert(monitors_select(monitors, 3, &settings) == 0);

    // An unavailable legacy display also falls back to primary, regardless of enumeration order.
    settings.monitor_id[0] = 0;
    wcscpy_s(settings.monitor_name, _countof(settings.monitor_name), L"missing");
    assert(monitors_select(monitors, 3, &settings) == 1);
}

static void test_monitor_settings_roundtrip(void) {
    wchar_t original_appdata[32768], temporary[MAX_PATH], directory[MAX_PATH], path[MAX_PATH];
    DWORD original_length = GetEnvironmentVariableW(L"APPDATA", original_appdata, _countof(original_appdata));
    assert(original_length < _countof(original_appdata));
    assert(GetTempPathW(_countof(temporary), temporary));
    assert(GetTempFileNameW(temporary, L"djl", 0, directory));
    assert(DeleteFileW(directory));
    assert(CreateDirectoryW(directory, NULL));
    assert(SetEnvironmentVariableW(L"APPDATA", directory));

    AppSettings saved, loaded;
    settings_defaults(&saved);
    wcscpy_s(saved.monitor_name, _countof(saved.monitor_name), L"\\\\.\\DISPLAY2");
    wcscpy_s(saved.monitor_id, _countof(saved.monitor_id), L"\\\\?\\DISPLAY#SECOND#instance#{monitor-guid}");
    settings_save(&saved);
    settings_load(&loaded);
    assert(wcscmp(loaded.monitor_name, saved.monitor_name) == 0);
    assert(wcscmp(loaded.monitor_id, saved.monitor_id) == 0);

    wcscpy_s(path, _countof(path), directory);
    wcscat_s(path, _countof(path), L"\\DjLoudnessMeter\\settings.json");
    FILE *legacy = NULL;
    assert(_wfopen_s(&legacy, path, L"wb") == 0 && legacy);
    assert(fputs("{\"TaskbarMonitorDeviceName\":\"\\\\\\\\.\\\\DISPLAY2\"}", legacy) >= 0);
    assert(fclose(legacy) == 0);
    settings_load(&loaded);
    assert(wcscmp(loaded.monitor_name, saved.monitor_name) == 0);
    assert(!loaded.monitor_id[0]);

    assert(SetEnvironmentVariableW(L"APPDATA", original_length ? original_appdata : NULL));
    assert(DeleteFileW(path));
    wcscpy_s(path, _countof(path), directory);
    wcscat_s(path, _countof(path), L"\\DjLoudnessMeter");
    assert(RemoveDirectoryW(path));
    assert(RemoveDirectoryW(directory));
}

int main(void) {
    test_peaks();
    test_pcm();
    test_display();
    test_settings_normalization();
    test_monitor_selection();
    test_monitor_settings_roundtrip();
    puts("native tests passed");
    return 0;
}
