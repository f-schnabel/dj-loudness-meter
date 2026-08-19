#include "settings.h"
#include "meter.h"
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void settings_path(wchar_t *path, size_t count) {
    DWORD n = GetEnvironmentVariableW(L"APPDATA", path, (DWORD)count);
    if (!n || n >= count) {
        path[0] = L'.';
        path[1] = 0;
    }
    wcsncat_s(path, count, L"\\DjLoudnessMeter", _TRUNCATE);
    CreateDirectoryW(path, NULL);
    wcsncat_s(path, count, L"\\settings.json", _TRUNCATE);
}

void settings_defaults(AppSettings *s) {
    ZeroMemory(s, sizeof(*s));
    s->window_width = SETTINGS_MIN_WIDTH;
    s->window_height = SETTINGS_MIN_HEIGHT;
    s->show_loudness = true;
    s->show_system = true;
    s->refresh_ms = 500;
    s->peak_hold_ms = 5000;
    s->display_zero = -9.0;
}

void settings_normalize(AppSettings *s) {
    if (s->window_width < SETTINGS_MIN_WIDTH) s->window_width = SETTINGS_MIN_WIDTH;
    if (s->window_height < SETTINGS_MIN_HEIGHT) s->window_height = SETTINGS_MIN_HEIGHT;
    if (s->window_width > 16384) s->window_width = 16384;
    if (s->window_height > 16384) s->window_height = 16384;
    if (s->refresh_ms < 10) s->refresh_ms = 10;
    if (s->refresh_ms > 2000) s->refresh_ms = 2000;
    if (s->peak_hold_ms < 0) s->peak_hold_ms = 0;
    if (s->peak_hold_ms > 60000) s->peak_hold_ms = 60000;
    if (!s->show_loudness && !s->show_system) s->show_loudness = true;
    s->display_zero = display_normalize_reference(s->display_zero);
}

static const char *value_start(const char *json, const char *key) {
    char pattern[80];
    sprintf_s(pattern, sizeof(pattern), "\"%s\"", key);
    const char *p = strstr(json, pattern);
    if (!p) return NULL;
    p = strchr(p + strlen(pattern), ':');
    if (!p) return NULL;
    do {
        ++p;
    } while (*p == ' ' || *p == '\t');
    return p;
}

static int get_int(const char *json, const char *key, int fallback) {
    const char *p = value_start(json, key);
    if (!p) return fallback;
    char *end = NULL;
    errno = 0;
    long value = strtol(p, &end, 10);
    return end != p && !errno && value >= INT_MIN && value <= INT_MAX ? (int)value : fallback;
}

static double get_double(const char *json, const char *key, double fallback) {
    const char *p = value_start(json, key);
    if (!p) return fallback;
    char *end = NULL;
    errno = 0;
    double value = strtod(p, &end);
    return end != p && !errno && isfinite(value) ? value : fallback;
}

static bool get_bool(const char *json, const char *key, bool fallback) {
    const char *p = value_start(json, key);
    if (!p) return fallback;
    if (strncmp(p, "true", 4) == 0 && strchr(" \t\r\n,}", p[4])) return true;
    if (strncmp(p, "false", 5) == 0 && strchr(" \t\r\n,}", p[5])) return false;
    return fallback;
}

static void get_string(const char *json, const char *key, wchar_t *out, int count) {
    const char *p = value_start(json, key);
    if (!p || *p != '"') return;
    ++p;
    char utf8[1024];
    size_t n = 0;
    while (*p && *p != '"' && n + 1 < sizeof(utf8)) {
        if (*p == '\\' && p[1]) {
            ++p;
            if (*p == 'n') utf8[n++] = '\n';
            else utf8[n++] = *p;
            ++p;
        } else utf8[n++] = *p++;
    }
    utf8[n] = 0;
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, out, count);
    out[count - 1] = 0;
}

void settings_load(AppSettings *s) {
    settings_defaults(s);
    wchar_t path[MAX_PATH];
    settings_path(path, _countof(path));
    FILE *file = NULL;
    if (_wfopen_s(&file, path, L"rb") || !file) return;
    char json[16384];
    size_t n = fread(json, 1, sizeof(json) - 1, file);
    fclose(file);
    json[n] = 0;
    get_string(json, "SelectedAudioEndpointId", s->endpoint_id, _countof(s->endpoint_id));
    get_string(json, "TaskbarMonitorDeviceName", s->monitor_name, _countof(s->monitor_name));
    s->window_x = get_int(json, "WindowLeft", 0);
    s->window_y = get_int(json, "WindowTop", 0);
    s->has_window_position = value_start(json, "WindowLeft") != NULL && value_start(json, "WindowTop") != NULL;
    s->window_width = get_int(json, "WindowWidth", SETTINGS_MIN_WIDTH);
    s->window_height = get_int(json, "WindowHeight", SETTINGS_MIN_HEIGHT);
    s->right_aligned = get_bool(json, "TaskbarRightAligned", false);
    s->show_loudness = get_bool(json, "ShowLoudnessValues", true);
    s->show_system = get_bool(json, "ShowSystemValues", true);
    s->refresh_ms = get_int(json, "UiRefreshMilliseconds", 500);
    s->peak_hold_ms = get_int(json, "PeakHoldMilliseconds", 5000);
    s->display_zero = get_double(json, "DisplayZeroDbfs", -9.0);
    settings_normalize(s);
}

static void escaped_utf8(const wchar_t *wide, char *out, size_t count) {
    char raw[1024] = {0};
    if (!WideCharToMultiByte(CP_UTF8, 0, wide, -1, raw, sizeof(raw), NULL, NULL)) {
        out[0] = 0;
        return;
    }
    size_t n = 0;
    for (size_t i = 0; raw[i] && n + 2 < count; ++i) {
        if (raw[i] == '\\' || raw[i] == '"') out[n++] = '\\';
        out[n++] = raw[i];
    }
    out[n] = 0;
}

void settings_save(const AppSettings *s) {
    AppSettings normalized = *s;
    settings_normalize(&normalized);
    s = &normalized;
    wchar_t path[MAX_PATH];
    settings_path(path, _countof(path));
    wchar_t temporary[MAX_PATH];
    wcscpy_s(temporary, _countof(temporary), path);
    wcscat_s(temporary, _countof(temporary), L".tmp");
    FILE *file = NULL;
    if (_wfopen_s(&file, temporary, L"wb") || !file) return;
    char endpoint[1024], monitor[128];
    escaped_utf8(s->endpoint_id, endpoint, sizeof(endpoint));
    escaped_utf8(s->monitor_name, monitor, sizeof(monitor));
    int written = fprintf(
        file,
        "{\n  \"SelectedAudioEndpointId\": \"%s\",\n  \"WindowLeft\": %d,\n  \"WindowTop\": %d,\n"
        "  \"WindowWidth\": %d,\n  \"WindowHeight\": %d,\n  \"AlwaysOnTop\": false,\n"
        "  \"TaskbarMonitorDeviceName\": \"%s\",\n  \"TaskbarRightAligned\": %s,\n"
        "  \"ShowLoudnessValues\": %s,\n  \"ShowSystemValues\": %s,\n  \"UiRefreshMilliseconds\": %d,\n"
        "  \"PeakHoldMilliseconds\": %d,\n  \"DisplayZeroDbfs\": %.1f\n}\n",
        endpoint,
        s->window_x,
        s->window_y,
        s->window_width,
        s->window_height,
        monitor,
        s->right_aligned ? "true" : "false",
        s->show_loudness ? "true" : "false",
        s->show_system ? "true" : "false",
        s->refresh_ms,
        s->peak_hold_ms,
        s->display_zero
    );
    bool saved = written >= 0 && fflush(file) == 0;
    if (fclose(file) != 0) saved = false;
    if (!saved || !MoveFileExW(temporary, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) DeleteFileW(temporary);
}
