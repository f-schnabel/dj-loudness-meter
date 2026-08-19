#ifndef DJLM_APP_H
#define DJLM_APP_H

#include "audio.h"
#include "settings.h"
#include "system_metrics.h"
#include <windows.h>

#define MAX_MONITORS 16

typedef struct {
    wchar_t device[32], label[64];
    RECT bounds, work;
    bool primary;
} DisplayInfo;

typedef struct {
    HINSTANCE instance;
    HWND settings_window, hit_window, overlay_window;
    HWND device_combo, monitor_combo, side_combo, zero_edit, rate_slider, rate_label;
    HWND loudness_check, system_check, autostart_check, status_label, format_label, monitor_label;
    HFONT font, small_font, value_font, system_value_font;
    HBRUSH background_brush, overlay_brush;
    HPEN separator_pen;
    HDC overlay_buffer_dc;
    HBITMAP overlay_buffer_bitmap;
    HGDIOBJ overlay_buffer_original;
    int overlay_buffer_width, overlay_buffer_height;
    AppSettings settings;
    AudioEngine audio;
    SystemMetrics metrics;
    SystemSnapshot system_snapshot;
    MeterSnapshot meter_snapshot;
    AudioDevice devices[AUDIO_MAX_DEVICES];
    bool device_activity[AUDIO_MAX_DEVICES];
    int device_count, selected_device;
    HANDLE activity_thread, activity_stop_event;
    ULONGLONG last_activity_probe_tick;
    UINT activity_generation;
    DisplayInfo monitors[MAX_MONITORS];
    int monitor_count, selected_monitor;
    wchar_t status_text[512], format_text[128];
    ULONGLONG last_system_tick, last_status_tick;
    bool closing, menu_open, activity_sort_pending;
} App;

#ifdef __cplusplus
extern "C" {
#endif

int app_run(HINSTANCE instance, int show_command);
void app_set_status(App *app, const wchar_t *text);

#ifdef __cplusplus
}
#endif

#endif
