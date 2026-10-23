#include "settings_window.h"
#include "autostart.h"
#include "overlay.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <commctrl.h>
#include <crtdbg.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

static AudioDevice available[2];
static int available_count, starts, saves;
static bool fail_start;

int audio_enumerate(AudioDevice *devices, int capacity) {
    assert(capacity >= available_count);
    CopyMemory(devices, available, sizeof(AudioDevice) * available_count);
    return available_count;
}

bool audio_probe_activity(const AudioDevice *devices, int count, bool *results, HANDLE stop_event) {
    (void)devices;
    (void)count;
    (void)results;
    (void)stop_event;
    return false;
}

void audio_stop(AudioEngine *engine) {
    if (engine->thread) CloseHandle(engine->thread);
    engine->thread = NULL;
}

bool audio_start(AudioEngine *engine, const wchar_t *endpoint_id) {
    audio_stop(engine);
    wcscpy_s(engine->endpoint_id, _countof(engine->endpoint_id), endpoint_id);
    // A signaled handle represents an exited capture worker.
    engine->thread = CreateEventW(NULL, TRUE, fail_start, NULL);
    assert(engine->thread);
    ++starts;
    return true;
}

void audio_reset(AudioEngine *engine) {
    (void)engine;
}

void audio_status(AudioEngine *engine, wchar_t *device, int dc, wchar_t *format, int fc, wchar_t *error, int ec) {
    (void)engine;
    (void)dc;
    (void)fc;
    (void)ec;
    device[0] = format[0] = error[0] = 0;
}

void app_set_status(App *app, const wchar_t *text) {
    wcscpy_s(app->status_text, _countof(app->status_text), text);
}

void app_save_settings(App *app) {
    (void)app;
    ++saves;
}

void overlay_position(App *app) {
    (void)app;
}

bool autostart_is_enabled(void) {
    return false;
}

bool autostart_set_enabled(bool enabled) {
    (void)enabled;
    return true;
}

static void recover(HWND window) {
    SendMessageW(window, WM_TIMER, 2, 0);
}

int main(void) {
    _set_error_mode(_OUT_TO_STDERR);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    INITCOMMONCONTROLSEX common = {sizeof(common), ICC_STANDARD_CLASSES | ICC_BAR_CLASSES};
    assert(InitCommonControlsEx(&common));
    WNDCLASSW window_class = {0};
    window_class.hInstance = GetModuleHandleW(NULL);
    window_class.lpfnWndProc = settings_window_proc;
    window_class.lpszClassName = L"DjLoudnessMeterRecoveryTest";
    assert(RegisterClassW(&window_class));

    // At sign-in, only the headset is ready; the saved speakers appear later.
    wcscpy_s(available[0].id, _countof(available[0].id), L"headset");
    wcscpy_s(available[0].name, _countof(available[0].name), L"Headset");
    available[0].is_default = true;
    available_count = 1;
    App app = {0};
    app.instance = window_class.hInstance;
    settings_defaults(&app.settings);
    wcscpy_s(app.settings.endpoint_id, _countof(app.settings.endpoint_id), L"speakers");
    HWND window = CreateWindowW(window_class.lpszClassName, L"", WS_OVERLAPPED, 0, 0, 620, 480, NULL, NULL, app.instance, &app);
    assert(window);
    assert(starts == 0 && app.selected_device == -1);
    assert(SendMessageW(app.device_combo, CB_GETCURSEL, 0, 0) == CB_ERR);
    recover(window);
    assert(starts == 0 && wcscmp(app.settings.endpoint_id, L"speakers") == 0);

    wcscpy_s(available[1].id, _countof(available[1].id), L"speakers");
    wcscpy_s(available[1].name, _countof(available[1].name), L"Speakers");
    available_count = 2;
    fail_start = true;
    recover(window);
    assert(starts == 1 && app.selected_device == 1);
    assert(wcscmp(app.audio.endpoint_id, L"speakers") == 0);

    // Failed initialization retries; a running worker remains untouched.
    fail_start = false;
    recover(window);
    assert(starts == 2 && wcscmp(app.audio.endpoint_id, L"speakers") == 0);
    recover(window);
    assert(starts == 2);

    // An invalidated/exited stream restarts on the same output.
    assert(SetEvent(app.audio.thread));
    recover(window);
    assert(starts == 3 && wcscmp(app.audio.endpoint_id, L"speakers") == 0);

    // Explicit selection persists immediately, using the combo's device mapping.
    assert(SendMessageW(app.device_combo, CB_SETCURSEL, 0, 0) == 0);
    SendMessageW(window, WM_COMMAND, MAKEWPARAM(1001, CBN_SELCHANGE), (LPARAM)app.device_combo);
    assert(starts == 4 && saves == 1);
    assert(wcscmp(app.settings.endpoint_id, L"headset") == 0);

    // Refresh during a disconnect clears stale rows without losing the saved source.
    available_count = 0;
    SendMessageW(window, WM_COMMAND, MAKEWPARAM(1002, BN_CLICKED), 0);
    assert(app.selected_device == -1 && !app.audio.thread);
    assert(SendMessageW(app.device_combo, CB_GETCOUNT, 0, 0) == 0);
    assert(wcscmp(app.settings.endpoint_id, L"headset") == 0);
    available_count = 2;
    recover(window);
    assert(starts == 5 && wcscmp(app.audio.endpoint_id, L"headset") == 0);

    app.closing = true;
    assert(SetEvent(app.audio.thread));
    recover(window);
    assert(starts == 5);
    settings_window_dispose(&app);
    audio_stop(&app.audio);
    assert(DestroyWindow(window));
    puts("audio recovery tests passed");
    return 0;
}
