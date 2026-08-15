#include "settings.h"
#include "meter.h"
#include <direct.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void settings_path(wchar_t *path, size_t count) {
    DWORD n = GetEnvironmentVariableW(L"APPDATA", path, (DWORD)count);
    if (!n || n >= count) { path[0] = L'.'; path[1] = 0; }
    wcsncat_s(path, count, L"\\DjLoudnessMeter", _TRUNCATE);
    CreateDirectoryW(path, NULL);
    wcsncat_s(path, count, L"\\settings.json", _TRUNCATE);
}

void settings_defaults(AppSettings *s) {
    ZeroMemory(s, sizeof(*s));
    s->window_width = 460; s->window_height = 410;
    s->show_loudness = true; s->show_system = true;
    s->refresh_ms = 500; s->peak_hold_ms = 5000; s->display_zero = -9.0;
}

static const char *value_start(const char *json, const char *key) {
    char pattern[80]; sprintf_s(pattern, sizeof(pattern), "\"%s\"", key);
    const char *p = strstr(json, pattern); if (!p) return NULL;
    p = strchr(p + strlen(pattern), ':');
    if (!p) return NULL; do { ++p; } while (*p == ' ' || *p == '\t'); return p;
}

static int get_int(const char *json, const char *key, int fallback) {
    const char *p = value_start(json, key); return p ? (int)strtol(p, NULL, 10) : fallback;
}

static double get_double(const char *json, const char *key, double fallback) {
    const char *p = value_start(json, key); return p ? strtod(p, NULL) : fallback;
}

static bool get_bool(const char *json, const char *key, bool fallback) {
    const char *p = value_start(json, key); if (!p) return fallback;
    return strncmp(p, "true", 4) == 0 ? true : strncmp(p, "false", 5) == 0 ? false : fallback;
}

static void get_string(const char *json, const char *key, wchar_t *out, int count) {
    const char *p = value_start(json, key); if (!p || *p != '"') return; ++p;
    char utf8[1024]; size_t n = 0;
    while (*p && *p != '"' && n + 1 < sizeof(utf8)) {
        if (*p == '\\' && p[1]) { ++p; if (*p == 'n') utf8[n++] = '\n'; else utf8[n++] = *p; ++p; }
        else utf8[n++] = *p++;
    }
    utf8[n] = 0; MultiByteToWideChar(CP_UTF8, 0, utf8, -1, out, count); out[count - 1] = 0;
}

void settings_load(AppSettings *s) {
    settings_defaults(s);
    wchar_t path[MAX_PATH]; settings_path(path, _countof(path));
    FILE *file = NULL; if (_wfopen_s(&file, path, L"rb") || !file) return;
    char json[16384]; size_t n = fread(json, 1, sizeof(json) - 1, file); fclose(file); json[n] = 0;
    get_string(json, "SelectedAudioEndpointId", s->endpoint_id, _countof(s->endpoint_id));
    get_string(json, "TaskbarMonitorDeviceName", s->monitor_name, _countof(s->monitor_name));
    s->window_x = get_int(json, "WindowLeft", 0); s->window_y = get_int(json, "WindowTop", 0);
    s->has_window_position = value_start(json, "WindowLeft") != NULL && value_start(json, "WindowTop") != NULL;
    s->window_width = get_int(json, "WindowWidth", 460); s->window_height = get_int(json, "WindowHeight", 410);
    s->right_aligned = get_bool(json, "TaskbarRightAligned", false);
    s->show_loudness = get_bool(json, "ShowLoudnessValues", true);
    s->show_system = get_bool(json, "ShowSystemValues", true);
    if (!s->show_loudness && !s->show_system) s->show_loudness = true;
    s->refresh_ms = get_int(json, "UiRefreshMilliseconds", 500);
    s->peak_hold_ms = get_int(json, "PeakHoldMilliseconds", 5000);
    s->display_zero = display_normalize_reference(get_double(json, "DisplayZeroDbfs", -9.0));
}

static void escaped_utf8(const wchar_t *wide, char *out, size_t count) {
    char raw[1024]; WideCharToMultiByte(CP_UTF8, 0, wide, -1, raw, sizeof(raw), NULL, NULL);
    size_t n = 0; for (size_t i = 0; raw[i] && n + 2 < count; ++i) { if (raw[i] == '\\' || raw[i] == '"') out[n++] = '\\'; out[n++] = raw[i]; } out[n] = 0;
}

void settings_save(const AppSettings *s) {
    wchar_t path[MAX_PATH]; settings_path(path, _countof(path));
    wchar_t temporary[MAX_PATH]; wcscpy_s(temporary, _countof(temporary), path); wcscat_s(temporary, _countof(temporary), L".tmp");
    FILE *file = NULL; if (_wfopen_s(&file, temporary, L"wb") || !file) return;
    char endpoint[1024], monitor[128]; escaped_utf8(s->endpoint_id, endpoint, sizeof(endpoint)); escaped_utf8(s->monitor_name, monitor, sizeof(monitor));
    fprintf(file,
        "{\n  \"SelectedAudioEndpointId\": \"%s\",\n  \"WindowLeft\": %d,\n  \"WindowTop\": %d,\n"
        "  \"WindowWidth\": %d,\n  \"WindowHeight\": %d,\n  \"AlwaysOnTop\": false,\n"
        "  \"TaskbarMonitorDeviceName\": \"%s\",\n  \"TaskbarRightAligned\": %s,\n"
        "  \"ShowLoudnessValues\": %s,\n  \"ShowSystemValues\": %s,\n  \"UiRefreshMilliseconds\": %d,\n"
        "  \"PeakHoldMilliseconds\": %d,\n  \"DisplayZeroDbfs\": %.1f\n}\n",
        endpoint, s->window_x, s->window_y, s->window_width, s->window_height, monitor,
        s->right_aligned ? "true" : "false", s->show_loudness ? "true" : "false", s->show_system ? "true" : "false",
        s->refresh_ms, s->peak_hold_ms, s->display_zero);
    fclose(file); MoveFileExW(temporary, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
}
