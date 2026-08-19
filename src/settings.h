#ifndef DJLM_SETTINGS_H
#define DJLM_SETTINGS_H

#include <stdbool.h>
#include <windows.h>

#define SETTINGS_MIN_WIDTH 620
#define SETTINGS_MIN_HEIGHT 480

typedef struct {
    wchar_t endpoint_id[512];
    wchar_t monitor_name[32];
    int window_x, window_y, window_width, window_height;
    bool has_window_position, right_aligned, show_loudness, show_system;
    int refresh_ms, peak_hold_ms;
    double display_zero;
} AppSettings;

void settings_defaults(AppSettings *settings);
void settings_normalize(AppSettings *settings);
void settings_load(AppSettings *settings);
void settings_save(const AppSettings *settings);

#endif
