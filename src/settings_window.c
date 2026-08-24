#include "settings_window.h"
#include "autostart.h"
#include "overlay.h"
#include <commctrl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <uxtheme.h>
#include <wchar.h>

#define ID_DEVICE 1001
#define ID_REFRESH 1002
#define ID_SIDE 1003
#define ID_MONITOR 1004
#define ID_ZERO 1005
#define ID_RATE 1006
#define ID_LOUDNESS 1007
#define ID_SYSTEM 1008
#define ID_RESET 1009
#define ID_AUTOSTART 1012
#define TIMER_UI 1
#define WM_ACTIVITY_READY (WM_APP + 1)

static const int refresh_intervals[] = {10, 50, 125, 250, 500, 750, 1000, 1250, 1500, 1750, 2000};
static const COLORREF primary = RGB(243, 245, 246);

static int nearest_refresh(int value) {
    int best = 0, distance = abs(value - refresh_intervals[0]);
    for (int i = 1; i < (int)_countof(refresh_intervals); ++i) {
        int d = abs(value - refresh_intervals[i]);
        if (d < distance) {
            best = i;
            distance = d;
        }
    }
    return best;
}

static void set_font(HWND control, HFONT font) {
    SendMessageW(control, WM_SETFONT, (WPARAM)font, TRUE);
}

static HWND control(App *app, const wchar_t *class_name, const wchar_t *text, DWORD style, int x, int y, int w, int h, int id) {
    HWND result = CreateWindowExW(
        0,
        class_name,
        text,
        WS_CHILD | WS_VISIBLE | style,
        x,
        y,
        w,
        h,
        app->settings_window,
        (HMENU)(INT_PTR)id,
        app->instance,
        NULL
    );
    set_font(result, app->font);
    return result;
}

void settings_window_apply_zero(App *app) {
    wchar_t text[64];
    GetWindowTextW(app->zero_edit, text, _countof(text));
    wchar_t *end = NULL;
    double value = wcstod(text, &end);
    if (end != text && isfinite(value) && value >= -30.0 && value <= 0.0) app->settings.display_zero = round(value * 10.0) / 10.0;
    swprintf_s(text, _countof(text), L"%.1f", app->settings.display_zero);
    SetWindowTextW(app->zero_edit, text);
}

static void update_rate(App *app) {
    int index = (int)SendMessageW(app->rate_slider, TBM_GETPOS, 0, 0);
    app->settings.refresh_ms = refresh_intervals[index];
    wchar_t text[32];
    swprintf_s(text, _countof(text), L"%d ms", app->settings.refresh_ms);
    SetWindowTextW(app->rate_label, text);
    if (app->overlay_window) SetTimer(app->overlay_window, TIMER_UI, (UINT)app->settings.refresh_ms, NULL);
}

static void report_start_failure(App *app) {
    wchar_t device[1], format[1], failure[256];
    audio_status(&app->audio, device, _countof(device), format, _countof(format), failure, _countof(failure));
    if (failure[0]) app_set_status(app, failure);
}

static void select_device(App *app, int index) {
    if (index < 0 || index >= app->device_count) return;
    app->selected_device = index;
    wcscpy_s(app->settings.endpoint_id, _countof(app->settings.endpoint_id), app->devices[index].id);
    app_set_status(app, app->devices[index].is_direct ? L"Starting direct Voicemeeter capture..." : L"Starting WASAPI loopback capture...");
    if (!audio_start(&app->audio, app->settings.endpoint_id)) report_start_failure(app);
}

static bool device_should_swap(const AudioDevice *left, const AudioDevice *right) {
    if (left->has_audio != right->has_audio) return !left->has_audio;
    if (left->is_direct != right->is_direct) return !left->is_direct;
    if (left->is_default != right->is_default) return !left->is_default;
    return _wcsicmp(left->name, right->name) > 0;
}

static int find_device(const App *app, const wchar_t *id) {
    for (int i = 0; i < app->device_count; ++i)
        if (wcscmp(id, app->devices[i].id) == 0) return i;
    return -1;
}

static int combo_device(const App *app) {
    LRESULT row = SendMessageW(app->device_combo, CB_GETCURSEL, 0, 0);
    if (row == CB_ERR) return -1;
    LRESULT device = SendMessageW(app->device_combo, CB_GETITEMDATA, (WPARAM)row, 0);
    return device == CB_ERR ? -1 : (int)device;
}

static void sort_device_order(const App *app, int *order) {
    for (int i = 0; i < app->device_count; ++i)
        order[i] = i;
    for (int i = 0; i < app->device_count; ++i)
        for (int j = i + 1; j < app->device_count; ++j)
            if (device_should_swap(&app->devices[order[i]], &app->devices[order[j]])) {
                int temporary = order[i];
                order[i] = order[j];
                order[j] = temporary;
            }
}

static void rebuild_device_combo(App *app) {
    int order[AUDIO_MAX_DEVICES] = {0};
    sort_device_order(app, order);
    SendMessageW(app->device_combo, CB_RESETCONTENT, 0, 0);
    int selected_row = -1;
    app->selected_device = find_device(app, app->settings.endpoint_id);
    if (app->selected_device < 0 && app->device_count) app->selected_device = order[0];
    for (int row = 0; row < app->device_count; ++row) {
        int device = order[row];
        LRESULT added = SendMessageW(app->device_combo, CB_ADDSTRING, 0, (LPARAM)app->devices[device].name);
        if (added == CB_ERR || added == CB_ERRSPACE) continue;
        SendMessageW(app->device_combo, CB_SETITEMDATA, (WPARAM)added, device);
        if (device == app->selected_device) selected_row = (int)added;
    }
    SendMessageW(app->device_combo, CB_SETCURSEL, selected_row, 0);
}

static void stop_activity_probe(App *app) {
    if (app->activity_stop_event) SetEvent(app->activity_stop_event);
    if (app->activity_thread) {
        WaitForSingleObject(app->activity_thread, INFINITE);
        CloseHandle(app->activity_thread);
    }
    if (app->activity_stop_event) CloseHandle(app->activity_stop_event);
    app->activity_thread = app->activity_stop_event = NULL;
}

static DWORD WINAPI activity_probe_thread(void *parameter) {
    App *app = (App *)parameter;
    UINT generation = app->activity_generation;
    if (audio_probe_activity(app->devices, app->device_count, app->device_activity, app->activity_stop_event))
        PostMessageW(app->settings_window, WM_ACTIVITY_READY, generation, 0);
    return 0;
}

static void start_activity_probe(App *app) {
    stop_activity_probe(app);
    if (!app->device_count) return;
    ++app->activity_generation;
    ZeroMemory(app->device_activity, sizeof(app->device_activity));
    app->activity_stop_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!app->activity_stop_event) return;
    app->activity_thread = CreateThread(NULL, 0, activity_probe_thread, app, 0, NULL);
    if (!app->activity_thread) {
        CloseHandle(app->activity_stop_event);
        app->activity_stop_event = NULL;
        return;
    }
    app->last_activity_probe_tick = GetTickCount64();
}

static void populate_devices(App *app) {
    stop_activity_probe(app);
    audio_stop(&app->audio);
    app->device_count = audio_enumerate(app->devices, AUDIO_MAX_DEVICES);
    app->selected_device = app->settings.endpoint_id[0] ? find_device(app, app->settings.endpoint_id) : -1;
    for (int i = 0; app->selected_device < 0 && i < app->device_count; ++i)
        if (app->devices[i].is_default) app->selected_device = i;
    if (app->selected_device < 0 && app->device_count) app->selected_device = 0;
    if (app->selected_device >= 0) {
        select_device(app, app->selected_device);
        rebuild_device_combo(app);
    } else app_set_status(app, L"No supported audio sources are available.");
    start_activity_probe(app);
}

static void populate_monitors(App *app) {
    SendMessageW(app->monitor_combo, CB_RESETCONTENT, 0, 0);
    for (int i = 0; i < app->monitor_count; ++i)
        SendMessageW(app->monitor_combo, CB_ADDSTRING, 0, (LPARAM)app->monitors[i].label);
    SendMessageW(app->monitor_combo, CB_SETCURSEL, app->selected_monitor, 0);
    ShowWindow(app->monitor_combo, app->monitor_count > 1 ? SW_SHOW : SW_HIDE);
    ShowWindow(app->monitor_label, app->monitor_count > 1 ? SW_SHOW : SW_HIDE);
}

static void create_controls(App *app) {
    control(app, L"STATIC", L"TASKBAR METER", SS_LEFT, 16, 13, 300, 25, 0);
    control(app, L"STATIC", L"Overlay settings", SS_LEFT, 16, 38, 300, 18, 0);
    app->status_label = control(app, L"STATIC", L"Selecting an audio device...", SS_LEFT | SS_NOPREFIX, 16, 66, 572, 24, 0);
    control(app, L"STATIC", L"Audio source", SS_LEFT, 16, 100, 572, 20, 0);
    app->device_combo = control(app, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL, 28, 122, 470, 200, ID_DEVICE);
    control(app, L"BUTTON", L"Refresh", BS_PUSHBUTTON, 506, 121, 82, 27, ID_REFRESH);
    app->format_label = control(app, L"STATIC", L"", SS_LEFT | SS_NOPREFIX, 28, 153, 560, 18, 0);
    control(app, L"STATIC", L"Taskbar overlay", SS_LEFT, 16, 193, 572, 20, 0);
    control(app, L"STATIC", L"Side", SS_LEFT, 28, 219, 120, 22, 0);
    app->side_combo = control(app, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST, 151, 215, 112, 100, ID_SIDE);
    SendMessageW(app->side_combo, CB_ADDSTRING, 0, (LPARAM)L"Left");
    SendMessageW(app->side_combo, CB_ADDSTRING, 0, (LPARAM)L"Right");
    SendMessageW(app->side_combo, CB_SETCURSEL, app->settings.right_aligned ? 1 : 0, 0);
    app->monitor_label = control(app, L"STATIC", L"Monitor", SS_LEFT, 28, 251, 120, 22, 0);
    app->monitor_combo = control(app, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST, 151, 247, 160, 180, ID_MONITOR);
    control(app, L"STATIC", L"Display zero", SS_LEFT, 28, 283, 120, 22, 0);
    app->zero_edit = control(app, L"EDIT", L"", WS_BORDER | ES_RIGHT | ES_AUTOHSCROLL, 151, 279, 54, 24, ID_ZERO);
    control(app, L"STATIC", L"dBFS", SS_LEFT, 213, 283, 45, 22, 0);
    settings_window_apply_zero(app);
    control(app, L"STATIC", L"Update rate", SS_LEFT, 28, 315, 120, 22, 0);
    app->rate_slider = control(app, TRACKBAR_CLASSW, L"", TBS_HORZ | TBS_AUTOTICKS, 145, 308, 190, 30, ID_RATE);
    SendMessageW(app->rate_slider, TBM_SETRANGE, TRUE, MAKELONG(0, _countof(refresh_intervals) - 1));
    SendMessageW(app->rate_slider, TBM_SETPOS, TRUE, nearest_refresh(app->settings.refresh_ms));
    app->rate_label = control(app, L"STATIC", L"", SS_RIGHT, 345, 315, 68, 22, 0);
    update_rate(app);
    control(app, L"STATIC", L"Values", SS_LEFT, 28, 347, 120, 22, 0);
    app->loudness_check = control(app, L"BUTTON", L"Loudness", BS_AUTOCHECKBOX, 151, 343, 90, 24, ID_LOUDNESS);
    app->system_check = control(app, L"BUTTON", L"System", BS_AUTOCHECKBOX, 252, 343, 75, 24, ID_SYSTEM);
    SetWindowTheme(app->loudness_check, L"", L"");
    SetWindowTheme(app->system_check, L"", L"");
    SendMessageW(app->loudness_check, BM_SETCHECK, app->settings.show_loudness ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(app->system_check, BM_SETCHECK, app->settings.show_system ? BST_CHECKED : BST_UNCHECKED, 0);
    control(app, L"BUTTON", L"Reset meter", BS_PUSHBUTTON, 330, 343, 86, 27, ID_RESET);
    control(app, L"STATIC", L"Startup", SS_LEFT, 16, 383, 572, 20, 0);
    app->autostart_check = control(app, L"BUTTON", L"Start automatically when I sign in", BS_AUTOCHECKBOX, 28, 407, 260, 24, ID_AUTOSTART);
    SetWindowTheme(app->autostart_check, L"", L"");
    SendMessageW(app->autostart_check, BM_SETCHECK, autostart_is_enabled() ? BST_CHECKED : BST_UNCHECKED, 0);
    populate_monitors(app);
    populate_devices(app);
}

void settings_window_show(App *app) {
    ShowWindow(app->settings_window, SW_RESTORE);
    SetForegroundWindow(app->settings_window);
    if (GetTickCount64() - app->last_activity_probe_tick >= 10000) start_activity_probe(app);
}

void settings_window_dispose(App *app) {
    stop_activity_probe(app);
}

static bool button_checked(HWND button) {
    return SendMessageW(button, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

static void update_visible_values(App *app, int changed_id) {
    bool show_loudness = button_checked(app->loudness_check);
    bool show_system = button_checked(app->system_check);
    if (!show_loudness && !show_system) {
        HWND changed = changed_id == ID_LOUDNESS ? app->loudness_check : app->system_check;
        SendMessageW(changed, BM_SETCHECK, BST_CHECKED, 0);
        show_loudness = button_checked(app->loudness_check);
        show_system = button_checked(app->system_check);
    }
    app->settings.show_loudness = show_loudness;
    app->settings.show_system = show_system;
    overlay_position(app);
}

static void handle_command(App *app, int id, int notification) {
    if (id == ID_REFRESH) populate_devices(app);
    else if (id == ID_RESET) audio_reset(&app->audio);
    else if (id == ID_DEVICE && notification == CBN_SELCHANGE) select_device(app, combo_device(app));
    else if (id == ID_DEVICE && notification == CBN_CLOSEUP && app->activity_sort_pending) {
        app->activity_sort_pending = false;
        rebuild_device_combo(app);
    } else if (id == ID_SIDE && notification == CBN_SELCHANGE) {
        app->settings.right_aligned = SendMessageW(app->side_combo, CB_GETCURSEL, 0, 0) == 1;
        overlay_position(app);
    } else if (id == ID_MONITOR && notification == CBN_SELCHANGE) {
        int selected = (int)SendMessageW(app->monitor_combo, CB_GETCURSEL, 0, 0);
        if (selected >= 0 && selected < app->monitor_count) app->selected_monitor = selected;
        overlay_position(app);
    } else if (id == ID_ZERO && notification == EN_KILLFOCUS) settings_window_apply_zero(app);
    else if (id == ID_AUTOSTART && notification == BN_CLICKED) {
        bool enabled = button_checked(app->autostart_check);
        if (!autostart_set_enabled(enabled)) {
            SendMessageW(app->autostart_check, BM_SETCHECK, enabled ? BST_UNCHECKED : BST_CHECKED, 0);
            app_set_status(app, L"Windows startup setting could not be updated.");
        }
    } else if ((id == ID_LOUDNESS || id == ID_SYSTEM) && notification == BN_CLICKED) update_visible_values(app, id);
}

LRESULT CALLBACK settings_window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    App *app = (App *)GetWindowLongPtrW(window, GWLP_USERDATA);
    if (message == WM_NCCREATE) {
        app = ((CREATESTRUCTW *)lparam)->lpCreateParams;
        SetWindowLongPtrW(window, GWLP_USERDATA, (LONG_PTR)app);
        app->settings_window = window;
    }
    if (!app) return DefWindowProcW(window, message, wparam, lparam);
    switch (message) {
    case WM_CREATE:
        create_controls(app);
        return 0;
    case WM_CLOSE:
        if (!app->closing) {
            ShowWindow(window, SW_HIDE);
            return 0;
        }
        break;
    case WM_GETMINMAXINFO:
        ((MINMAXINFO *)lparam)->ptMinTrackSize.x = SETTINGS_MIN_WIDTH;
        ((MINMAXINFO *)lparam)->ptMinTrackSize.y = SETTINGS_MIN_HEIGHT;
        return 0;
    case WM_ACTIVITY_READY:
        if ((UINT)wparam != app->activity_generation) return 0;
        stop_activity_probe(app);
        for (int i = 0; i < app->device_count; ++i)
            app->devices[i].has_audio = app->device_activity[i];
        if (app->selected_device >= 0 && app->selected_device < app->device_count && app->meter_snapshot.recent_audio)
            app->devices[app->selected_device].has_audio = true;
        if (SendMessageW(app->device_combo, CB_GETDROPPEDSTATE, 0, 0)) app->activity_sort_pending = true;
        else rebuild_device_combo(app);
        return 0;
    case WM_COMMAND:
        handle_command(app, LOWORD(wparam), HIWORD(wparam));
        return 0;
    case WM_HSCROLL:
        if ((HWND)lparam == app->rate_slider) update_rate(app);
        return 0;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
        SetBkMode((HDC)wparam, TRANSPARENT);
        SetTextColor((HDC)wparam, primary);
        return (LRESULT)app->background_brush;
    case WM_ERASEBKGND: {
        RECT r;
        GetClientRect(window, &r);
        FillRect((HDC)wparam, &r, app->background_brush);
        return 1;
    }
    default:
        break;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}
