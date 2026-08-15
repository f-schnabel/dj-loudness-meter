#ifndef DJLM_APP_H
#define DJLM_APP_H

#include "audio.h"
#include "settings.h"
#include "system_metrics.h"
#include <windows.h>

#define MAX_MONITORS 16

typedef struct { wchar_t device[32], label[64]; RECT bounds, work; bool primary; } DisplayInfo;

typedef struct {
    HINSTANCE instance;
    HWND settings_window, hit_window, overlay_window;
    HWND device_combo, monitor_combo, side_combo, zero_edit, rate_slider, rate_label;
    HWND loudness_check, system_check, status_label, format_label, monitor_label;
    HFONT font, small_font, value_font, system_value_font;
    HBRUSH background_brush;
    HDC overlay_buffer_dc;
    HBITMAP overlay_buffer_bitmap;
    HGDIOBJ overlay_buffer_original;
    int overlay_buffer_width, overlay_buffer_height;
    AppSettings settings;
    AudioEngine audio;
    SystemMetrics metrics;
    SystemSnapshot system_snapshot;
    MeterSnapshot meter_snapshot;
    AudioDevice devices[AUDIO_MAX_DEVICES]; int device_count, selected_device;
    DisplayInfo monitors[MAX_MONITORS]; int monitor_count, selected_monitor;
    ULONGLONG last_system_tick;
    bool closing, menu_open;
} App;

int app_run(HINSTANCE instance, int show_command);

#endif
