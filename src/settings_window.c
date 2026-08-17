#include "settings_window.h"
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
#define TIMER_UI 1

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

static void populate_devices(App *app) {
    audio_stop(&app->audio);
    SendMessageW(app->device_combo, CB_RESETCONTENT, 0, 0);
    app->device_count = audio_enumerate(app->devices, AUDIO_MAX_DEVICES);
    app->selected_device = -1;
    for (int i = 0; i < app->device_count; ++i) {
        SendMessageW(app->device_combo, CB_ADDSTRING, 0, (LPARAM)app->devices[i].name);
        if ((app->settings.endpoint_id[0] && wcscmp(app->settings.endpoint_id, app->devices[i].id) == 0) ||
            (!app->settings.endpoint_id[0] && app->devices[i].is_default))
            app->selected_device = i;
    }
    if (app->selected_device < 0 && app->device_count) app->selected_device = 0;
    if (app->selected_device >= 0) {
        SendMessageW(app->device_combo, CB_SETCURSEL, app->selected_device, 0);
        select_device(app, app->selected_device);
    } else app_set_status(app, L"No supported audio sources are available.");
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
    app->status_label = control(app, L"STATIC", L"Selecting an audio device...", SS_LEFT | SS_NOPREFIX, 16, 66, 412, 24, 0);
    control(app, L"STATIC", L"Audio source", SS_LEFT, 16, 100, 412, 20, 0);
    app->device_combo = control(app, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL, 28, 122, 308, 200, ID_DEVICE);
    control(app, L"BUTTON", L"Refresh", BS_PUSHBUTTON, 344, 121, 72, 27, ID_REFRESH);
    app->format_label = control(app, L"STATIC", L"", SS_LEFT | SS_NOPREFIX, 28, 153, 380, 18, 0);
    control(app, L"STATIC", L"Taskbar overlay", SS_LEFT, 16, 193, 412, 20, 0);
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
    populate_monitors(app);
    populate_devices(app);
}

void settings_window_show(App *app) {
    ShowWindow(app->settings_window, SW_SHOW);
    SetForegroundWindow(app->settings_window);
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
    else if (id == ID_DEVICE && notification == CBN_SELCHANGE) select_device(app, (int)SendMessageW(app->device_combo, CB_GETCURSEL, 0, 0));
    else if (id == ID_SIDE && notification == CBN_SELCHANGE) {
        app->settings.right_aligned = SendMessageW(app->side_combo, CB_GETCURSEL, 0, 0) == 1;
        overlay_position(app);
    } else if (id == ID_MONITOR && notification == CBN_SELCHANGE) {
        int selected = (int)SendMessageW(app->monitor_combo, CB_GETCURSEL, 0, 0);
        if (selected >= 0 && selected < app->monitor_count) app->selected_monitor = selected;
        overlay_position(app);
    } else if (id == ID_ZERO && notification == EN_KILLFOCUS) settings_window_apply_zero(app);
    else if ((id == ID_LOUDNESS || id == ID_SYSTEM) && notification == BN_CLICKED) update_visible_values(app, id);
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
        ((MINMAXINFO *)lparam)->ptMinTrackSize.x = 460;
        ((MINMAXINFO *)lparam)->ptMinTrackSize.y = 430;
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
