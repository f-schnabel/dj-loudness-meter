#include "app.h"
#include "resource.h"
#include <commctrl.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <objbase.h>
#include <UIAutomationClient.h>
#include <windowsx.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define ID_DEVICE 1001
#define ID_REFRESH 1002
#define ID_SIDE 1003
#define ID_MONITOR 1004
#define ID_ZERO 1005
#define ID_RATE 1006
#define ID_LOUDNESS 1007
#define ID_SYSTEM 1008
#define ID_RESET 1009
#define ID_CLOSE_APP 1010
#define ID_OPEN_SETTINGS 1011
#define TIMER_UI 1

static const int refresh_intervals[] = {10, 50, 125, 250, 500, 750, 1000, 1250, 1500, 1750, 2000};
static const COLORREF color_key = RGB(1, 2, 3), primary = RGB(243, 245, 246), secondary = RGB(143, 154, 163);
static const COLORREF warning = RGB(246, 195, 68), critical = RGB(255, 90, 95), connected = RGB(88, 214, 141), error_color = RGB(255, 123, 127);
static const GUID djlm_clsid_uiautomation = {0xff48dba4,0x60ef,0x4201,{0xaa,0x87,0x54,0x10,0x3e,0xef,0x59,0x4e}};
static const GUID djlm_iid_uiautomation = {0x30cbe57d,0xd9d0,0x452a,{0xab,0x13,0x7a,0xc5,0xac,0x48,0x25,0xee}};

static LRESULT CALLBACK settings_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
static LRESULT CALLBACK hit_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
static LRESULT CALLBACK overlay_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
static void save_and_close(App *app);

static int nearest_refresh(int value) {
    int best = 0, distance = abs(value - refresh_intervals[0]);
    for (int i = 1; i < (int)_countof(refresh_intervals); ++i) { int d = abs(value - refresh_intervals[i]); if (d < distance) { best = i; distance = d; } }
    return best;
}

static BOOL CALLBACK monitor_callback(HMONITOR monitor, HDC dc, LPRECT rect, LPARAM data) {
    (void)dc; (void)rect; App *app = (App *)data; if (app->monitor_count >= MAX_MONITORS) return FALSE;
    MONITORINFOEXW info = {sizeof(info)}; if (!GetMonitorInfoW(monitor, (MONITORINFO *)&info)) return TRUE;
    DisplayInfo *display = &app->monitors[app->monitor_count++];
    wcscpy_s(display->device, _countof(display->device), info.szDevice); display->bounds = info.rcMonitor; display->work = info.rcWork;
    display->primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0; return TRUE;
}

typedef struct { const wchar_t *monitor_device; int left; bool found; } TraySearch;

static BOOL CALLBACK tray_child_callback(HWND window, LPARAM data) {
    TraySearch *search = (TraySearch *)data; wchar_t class_name[128]; RECT rect;
    if (!IsWindowVisible(window) || !GetWindowRect(window, &rect) || rect.right <= rect.left) return TRUE;
    GetClassNameW(window, class_name, _countof(class_name));
    if (_wcsicmp(class_name, L"TrayNotifyWnd") == 0 || _wcsicmp(class_name, L"ClockButton") == 0 ||
        wcsstr(class_name, L"SystemTray") != NULL) {
        if (!search->found || rect.left < search->left) search->left = rect.left;
        search->found = true;
    }
    return TRUE;
}

static BOOL CALLBACK taskbar_callback(HWND window, LPARAM data) {
    TraySearch *search = (TraySearch *)data; wchar_t class_name[64];
    GetClassNameW(window, class_name, _countof(class_name));
    if (_wcsicmp(class_name, L"Shell_TrayWnd") != 0 && _wcsicmp(class_name, L"Shell_SecondaryTrayWnd") != 0) return TRUE;
    HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST); MONITORINFOEXW info = {sizeof(info)};
    if (!GetMonitorInfoW(monitor, (MONITORINFO *)&info) || _wcsicmp(info.szDevice, search->monitor_device) != 0) return TRUE;
    EnumChildWindows(window, tray_child_callback, data); return search->found ? FALSE : TRUE;
}

static bool taskbar_safe_right(const DisplayInfo *monitor, int *right) {
    TraySearch search = {monitor->device, 0, false}; EnumWindows(taskbar_callback, (LPARAM)&search);
    if (search.found) *right = search.left - 4; return search.found;
}

typedef struct { const wchar_t *monitor_device; HWND window; } TaskbarSearch;

static BOOL CALLBACK find_taskbar_callback(HWND window, LPARAM data) {
    TaskbarSearch *search = (TaskbarSearch *)data; wchar_t class_name[64];
    GetClassNameW(window, class_name, _countof(class_name));
    if (_wcsicmp(class_name, L"Shell_TrayWnd") != 0 && _wcsicmp(class_name, L"Shell_SecondaryTrayWnd") != 0) return TRUE;
    HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST); MONITORINFOEXW info = {sizeof(info)};
    if (GetMonitorInfoW(monitor, (MONITORINFO *)&info) && _wcsicmp(info.szDevice, search->monitor_device) == 0) { search->window = window; return FALSE; }
    return TRUE;
}

static HWND find_taskbar(const DisplayInfo *monitor) {
    TaskbarSearch search = {monitor->device, NULL}; EnumWindows(find_taskbar_callback, (LPARAM)&search); return search.window;
}

static bool widgets_enabled(void) {
    DWORD value = 0, size = sizeof(value);
    LSTATUS status = RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced",
        L"TaskbarDa", RRF_RT_REG_DWORD, NULL, &value, &size);
    return status == ERROR_SUCCESS && value != 0;
}

static bool query_widget_right(const DisplayInfo *monitor, int *right) {
    if (!widgets_enabled()) return false; HWND taskbar = find_taskbar(monitor); if (!taskbar) return false;
    IUIAutomation *automation = NULL; IUIAutomationElement *root = NULL, *button = NULL; IUIAutomationCondition *condition = NULL;
    VARIANT wanted; VariantInit(&wanted); wanted.vt = VT_BSTR; wanted.bstrVal = SysAllocString(L"WidgetsButton"); bool found = false;
    if (wanted.bstrVal && SUCCEEDED(CoCreateInstance(&djlm_clsid_uiautomation, NULL, CLSCTX_INPROC_SERVER, &djlm_iid_uiautomation, (void **)&automation)) &&
        SUCCEEDED(IUIAutomation_ElementFromHandle(automation, taskbar, &root)) &&
        SUCCEEDED(IUIAutomation_CreatePropertyCondition(automation, UIA_AutomationIdPropertyId, wanted, &condition)) &&
        SUCCEEDED(IUIAutomationElement_FindFirst(root, TreeScope_Descendants, condition, &button)) && button) {
        RECT bounds; if (SUCCEEDED(IUIAutomationElement_get_CurrentBoundingRectangle(button, &bounds)) && bounds.right > bounds.left) { *right = bounds.right + 2; found = true; }
    }
    if (button) IUIAutomationElement_Release(button); if (condition) IUIAutomationCondition_Release(condition);
    if (root) IUIAutomationElement_Release(root); if (automation) IUIAutomation_Release(automation); VariantClear(&wanted); return found;
}

static bool taskbar_safe_left(const DisplayInfo *monitor, int *left) {
    static ULONGLONG last_check; static wchar_t cached_monitor[32]; static int cached_left; static bool cached_found;
    ULONGLONG now = GetTickCount64();
    if (_wcsicmp(cached_monitor, monitor->device) == 0 && now - last_check < 5000) { if (cached_found) *left = cached_left; return cached_found; }
    last_check = now; wcscpy_s(cached_monitor, _countof(cached_monitor), monitor->device); cached_found = query_widget_right(monitor, &cached_left);
    if (cached_found) *left = cached_left; return cached_found;
}

static void enumerate_monitors(App *app) {
    app->monitor_count = 0; EnumDisplayMonitors(NULL, NULL, monitor_callback, (LPARAM)app); app->selected_monitor = 0;
    for (int i = 0; i < app->monitor_count; ++i) {
        swprintf_s(app->monitors[i].label, _countof(app->monitors[i].label), L"Display %d%s", i + 1, app->monitors[i].primary ? L" (Main)" : L"");
        if ((app->settings.monitor_name[0] && _wcsicmp(app->settings.monitor_name, app->monitors[i].device) == 0) ||
            (!app->settings.monitor_name[0] && app->monitors[i].primary)) app->selected_monitor = i;
    }
}

static void set_font(HWND control, HFONT font) { SendMessageW(control, WM_SETFONT, (WPARAM)font, TRUE); }
static HWND control(App *app, const wchar_t *class_name, const wchar_t *text, DWORD style, int x, int y, int w, int h, int id) {
    HWND result = CreateWindowExW(0, class_name, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, app->settings_window, (HMENU)(INT_PTR)id, app->instance, NULL);
    set_font(result, app->font); return result;
}

static void apply_zero(App *app) {
    wchar_t text[64]; GetWindowTextW(app->zero_edit, text, _countof(text)); wchar_t *end = NULL; double value = wcstod(text, &end);
    if (end != text && isfinite(value) && value >= -30.0 && value <= 0.0) app->settings.display_zero = round(value * 10.0) / 10.0;
    swprintf_s(text, _countof(text), L"%.1f", app->settings.display_zero); SetWindowTextW(app->zero_edit, text);
}

static void update_rate(App *app) {
    int index = (int)SendMessageW(app->rate_slider, TBM_GETPOS, 0, 0); app->settings.refresh_ms = refresh_intervals[index];
    wchar_t text[32]; swprintf_s(text, _countof(text), L"%d ms", app->settings.refresh_ms); SetWindowTextW(app->rate_label, text);
    if (app->overlay_window) SetTimer(app->overlay_window, TIMER_UI, (UINT)app->settings.refresh_ms, NULL);
}

static void populate_devices(App *app) {
    audio_stop(&app->audio); SendMessageW(app->device_combo, CB_RESETCONTENT, 0, 0); app->device_count = audio_enumerate(app->devices, AUDIO_MAX_DEVICES);
    app->selected_device = -1;
    for (int i = 0; i < app->device_count; ++i) {
        SendMessageW(app->device_combo, CB_ADDSTRING, 0, (LPARAM)app->devices[i].name);
        if ((app->settings.endpoint_id[0] && wcscmp(app->settings.endpoint_id, app->devices[i].id) == 0) ||
            (!app->settings.endpoint_id[0] && app->devices[i].is_default)) app->selected_device = i;
    }
    if (app->selected_device < 0 && app->device_count) app->selected_device = 0;
    if (app->selected_device >= 0) {
        SendMessageW(app->device_combo, CB_SETCURSEL, app->selected_device, 0);
        wcscpy_s(app->settings.endpoint_id, _countof(app->settings.endpoint_id), app->devices[app->selected_device].id);
        audio_start(&app->audio, app->settings.endpoint_id); SetWindowTextW(app->status_label, L"Starting WASAPI loopback capture...");
    } else SetWindowTextW(app->status_label, L"No active Windows playback devices are available.");
}

static void populate_monitors(App *app) {
    SendMessageW(app->monitor_combo, CB_RESETCONTENT, 0, 0);
    for (int i = 0; i < app->monitor_count; ++i) SendMessageW(app->monitor_combo, CB_ADDSTRING, 0, (LPARAM)app->monitors[i].label);
    SendMessageW(app->monitor_combo, CB_SETCURSEL, app->selected_monitor, 0);
    ShowWindow(app->monitor_combo, app->monitor_count > 1 ? SW_SHOW : SW_HIDE); ShowWindow(app->monitor_label, app->monitor_count > 1 ? SW_SHOW : SW_HIDE);
}

static int overlay_width(App *app) {
    int width = app->settings.show_loudness ? 255 : 0;
    if (app->settings.show_system) width += 92 + (app->system_snapshot.has_temperature ? 46 : 0);
    if (app->settings.show_loudness && app->settings.show_system) width += 13; return width;
}

static void position_overlay(App *app) {
    if (!app->monitor_count) return; DisplayInfo *m = &app->monitors[app->selected_monitor];
    int bottom = m->bounds.bottom - m->work.bottom, top = m->work.top - m->bounds.top;
    int height = bottom > 0 ? bottom : top > 0 ? top : 42; if (height < 32) height = 32;
    int y = bottom > 0 ? m->work.bottom : top > 0 ? m->bounds.top : m->bounds.bottom - height;
    int width = overlay_width(app); int x;
    if (app->settings.right_aligned) {
        int safe_right; if (!taskbar_safe_right(m, &safe_right)) { int reserve = m->primary ? 240 : 120; safe_right = m->bounds.right - reserve; }
        x = safe_right - width; if (x < m->bounds.left) x = m->bounds.left;
    }
    else { int safe_left; x = taskbar_safe_left(m, &safe_left) ? safe_left : m->bounds.left; }
    if (app->hit_window) SetWindowPos(app->hit_window, HWND_TOPMOST, x, y, width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    SetWindowPos(app->overlay_window, HWND_TOPMOST, x, y, width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

static void create_settings_controls(App *app) {
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
    SendMessageW(app->side_combo, CB_ADDSTRING, 0, (LPARAM)L"Left"); SendMessageW(app->side_combo, CB_ADDSTRING, 0, (LPARAM)L"Right");
    SendMessageW(app->side_combo, CB_SETCURSEL, app->settings.right_aligned ? 1 : 0, 0);
    app->monitor_label = control(app, L"STATIC", L"Monitor", SS_LEFT, 28, 251, 120, 22, 0);
    app->monitor_combo = control(app, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST, 151, 247, 160, 180, ID_MONITOR);
    control(app, L"STATIC", L"Display zero", SS_LEFT, 28, 283, 120, 22, 0);
    app->zero_edit = control(app, L"EDIT", L"", WS_BORDER | ES_RIGHT | ES_AUTOHSCROLL, 151, 279, 54, 24, ID_ZERO);
    control(app, L"STATIC", L"dBFS", SS_LEFT, 213, 283, 45, 22, 0); apply_zero(app);
    control(app, L"STATIC", L"Update rate", SS_LEFT, 28, 315, 120, 22, 0);
    app->rate_slider = control(app, TRACKBAR_CLASSW, L"", TBS_HORZ | TBS_AUTOTICKS, 145, 308, 190, 30, ID_RATE);
    SendMessageW(app->rate_slider, TBM_SETRANGE, TRUE, MAKELONG(0, _countof(refresh_intervals) - 1));
    SendMessageW(app->rate_slider, TBM_SETPOS, TRUE, nearest_refresh(app->settings.refresh_ms));
    app->rate_label = control(app, L"STATIC", L"", SS_RIGHT, 345, 315, 68, 22, 0); update_rate(app);
    control(app, L"STATIC", L"Values", SS_LEFT, 28, 347, 120, 22, 0);
    app->loudness_check = control(app, L"BUTTON", L"Loudness", BS_AUTOCHECKBOX, 151, 343, 90, 24, ID_LOUDNESS);
    app->system_check = control(app, L"BUTTON", L"System", BS_AUTOCHECKBOX, 252, 343, 75, 24, ID_SYSTEM);
    SetWindowTheme(app->loudness_check, L"", L""); SetWindowTheme(app->system_check, L"", L"");
    SendMessageW(app->loudness_check, BM_SETCHECK, app->settings.show_loudness ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(app->system_check, BM_SETCHECK, app->settings.show_system ? BST_CHECKED : BST_UNCHECKED, 0);
    control(app, L"BUTTON", L"Reset meter", BS_PUSHBUTTON, 330, 343, 86, 27, ID_RESET);
    populate_monitors(app); populate_devices(app);
}

static void show_settings(App *app) {
    ShowWindow(app->settings_window, SW_SHOW); SetForegroundWindow(app->settings_window);
}

static void format_reading(double value, wchar_t *buffer, size_t count) {
    if (!display_should_show(value)) wcscpy_s(buffer, count, L"\x2212\x221e");
    else swprintf_s(buffer, count, value > 0.05 ? L"+%.1f" : L"%.1f", value);
}

static COLORREF threshold_color(double value, double amber, double red) {
    return !isfinite(value) ? primary : value >= red ? critical : value >= amber ? warning : primary;
}

static void draw_cell(HDC dc, App *app, RECT rect, const wchar_t *label, double raw, double warn, double red) {
    wchar_t value[32]; double adjusted = display_adjust(raw, app->settings.display_zero); format_reading(adjusted, value, _countof(value));
    SetTextColor(dc, secondary); SelectObject(dc, app->small_font); RECT top = rect; top.bottom = top.top + (rect.bottom - rect.top) / 2;
    DrawTextW(dc, label, -1, &top, DT_CENTER | DT_BOTTOM | DT_SINGLELINE | DT_NOPREFIX);
    SetTextColor(dc, threshold_color(adjusted, display_adjust(warn, app->settings.display_zero), display_adjust(red, app->settings.display_zero)));
    SelectObject(dc, app->value_font); RECT bottom = rect; bottom.top = top.bottom - 4; DrawTextW(dc, value, -1, &bottom, DT_CENTER | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
}

static void draw_system_cell(HDC dc, App *app, RECT rect, const wchar_t *label, double value, bool valid, const wchar_t *suffix) {
    wchar_t text[32]; if (valid) swprintf_s(text, _countof(text), L"%.0f%s", value, suffix); else wcscpy_s(text, _countof(text), L"N/A");
    SetTextColor(dc, secondary); SelectObject(dc, app->small_font); RECT top = rect; top.bottom = top.top + (rect.bottom - rect.top) / 2; DrawTextW(dc, label, -1, &top, DT_CENTER | DT_BOTTOM | DT_SINGLELINE);
    SetTextColor(dc, primary); SelectObject(dc, app->value_font); RECT bottom = rect; bottom.top = top.bottom - 4; DrawTextW(dc, text, -1, &bottom, DT_CENTER | DT_TOP | DT_SINGLELINE);
}

static void paint_overlay(App *app, HDC dc, RECT bounds) {
    HBRUSH key_brush = CreateSolidBrush(color_key); FillRect(dc, &bounds, key_brush); DeleteObject(key_brush); SetBkMode(dc, TRANSPARENT);
    int x = 0, height = bounds.bottom; if (app->settings.show_loudness) {
        int cell = 255 / 4; wchar_t peak_label[32];
        if (app->settings.refresh_ms < 1000) swprintf_s(peak_label, _countof(peak_label), L"P (%dms)", app->settings.refresh_ms);
        else swprintf_s(peak_label, _countof(peak_label), L"P (%.2gs)", app->settings.refresh_ms / 1000.0);
        RECT r = {x, 0, x + cell, height}; draw_cell(dc, app, r, peak_label, app->meter_snapshot.peak_db, -6, -1); x += cell;
        r = (RECT){x, 0, x + cell, height}; draw_cell(dc, app, r, L"P (5s)", app->meter_snapshot.hold_db, -6, -1); x += cell;
        r = (RECT){x, 0, x + cell, height}; draw_cell(dc, app, r, L"LUFS (0.4s)", app->meter_snapshot.momentary, -12, -9); x += cell;
        r = (RECT){x, 0, x + 255 - cell * 3, height}; draw_cell(dc, app, r, L"LUFS (3s)", app->meter_snapshot.short_term, -12, -9); x += 255 - cell * 3;
    }
    if (app->settings.show_loudness && app->settings.show_system) { HPEN pen = CreatePen(PS_SOLID, 1, RGB(60,70,78)); SelectObject(dc, pen); MoveToEx(dc, x + 6, 10, NULL); LineTo(dc, x + 6, height - 10); DeleteObject(pen); x += 13; }
    if (app->settings.show_system) {
        if (app->system_snapshot.has_temperature) { RECT r = {x, 0, x + 46, height}; draw_system_cell(dc, app, r, L"Temp", app->system_snapshot.temperature, true, L"\x00b0"); x += 46; }
        RECT r = {x, 0, x + 46, height}; draw_system_cell(dc, app, r, L"CPU", app->system_snapshot.cpu, app->system_snapshot.has_cpu, L"%"); x += 46;
        r = (RECT){x, 0, x + 46, height}; draw_system_cell(dc, app, r, L"RAM", app->system_snapshot.memory, app->system_snapshot.has_memory, L"%");
    }
}

static void tick(App *app) {
    app->meter_snapshot = audio_snapshot(&app->audio); ULONGLONG now = GetTickCount64();
    if (!app->last_system_tick || now - app->last_system_tick >= 500) { app->last_system_tick = now; if (app->settings.show_system) app->system_snapshot = system_metrics_read(&app->metrics); if (!app->menu_open) position_overlay(app); }
    wchar_t device[256], format[128], failure[256], status[512]; audio_status(&app->audio, device, _countof(device), format, _countof(format), failure, _countof(failure));
    SetWindowTextW(app->format_label, format);
    if (failure[0]) SetWindowTextW(app->status_label, failure);
    else if (app->meter_snapshot.connected) { swprintf_s(status, _countof(status), L"Monitoring %s%s", device, app->meter_snapshot.recent_audio ? L"" : L" - waiting for audio"); SetWindowTextW(app->status_label, status); }
    InvalidateRect(app->overlay_window, NULL, FALSE);
    if (!app->menu_open) SetWindowPos(app->overlay_window, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

static void save_and_close(App *app) {
    app->closing = true; RECT rect; if (GetWindowRect(app->settings_window, &rect)) { app->settings.window_x = rect.left; app->settings.window_y = rect.top; app->settings.window_width = rect.right - rect.left; app->settings.window_height = rect.bottom - rect.top; }
    if (app->monitor_count) wcscpy_s(app->settings.monitor_name, _countof(app->settings.monitor_name), app->monitors[app->selected_monitor].device);
    apply_zero(app); settings_save(&app->settings); PostQuitMessage(0);
}

static LRESULT CALLBACK settings_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    App *app = (App *)GetWindowLongPtrW(window, GWLP_USERDATA);
    if (message == WM_NCCREATE) { app = ((CREATESTRUCTW *)lparam)->lpCreateParams; SetWindowLongPtrW(window, GWLP_USERDATA, (LONG_PTR)app); app->settings_window = window; }
    if (!app) return DefWindowProcW(window, message, wparam, lparam);
    switch (message) {
    case WM_CREATE: create_settings_controls(app); return 0;
    case WM_CLOSE: if (!app->closing) { ShowWindow(window, SW_HIDE); return 0; } break;
    case WM_GETMINMAXINFO: ((MINMAXINFO *)lparam)->ptMinTrackSize.x = 460; ((MINMAXINFO *)lparam)->ptMinTrackSize.y = 430; return 0;
    case WM_COMMAND: {
        int id = LOWORD(wparam), notification = HIWORD(wparam);
        if (id == ID_REFRESH) populate_devices(app);
        else if (id == ID_RESET) audio_reset(&app->audio);
        else if (id == ID_DEVICE && notification == CBN_SELCHANGE) { int i = (int)SendMessageW(app->device_combo, CB_GETCURSEL, 0, 0); if (i >= 0 && i < app->device_count) { app->selected_device = i; wcscpy_s(app->settings.endpoint_id, _countof(app->settings.endpoint_id), app->devices[i].id); audio_start(&app->audio, app->settings.endpoint_id); } }
        else if (id == ID_SIDE && notification == CBN_SELCHANGE) { app->settings.right_aligned = SendMessageW(app->side_combo, CB_GETCURSEL, 0, 0) == 1; position_overlay(app); }
        else if (id == ID_MONITOR && notification == CBN_SELCHANGE) { app->selected_monitor = (int)SendMessageW(app->monitor_combo, CB_GETCURSEL, 0, 0); position_overlay(app); }
        else if (id == ID_ZERO && notification == EN_KILLFOCUS) apply_zero(app);
        else if ((id == ID_LOUDNESS || id == ID_SYSTEM) && notification == BN_CLICKED) {
            bool loud = SendMessageW(app->loudness_check, BM_GETCHECK, 0, 0) == BST_CHECKED, sys = SendMessageW(app->system_check, BM_GETCHECK, 0, 0) == BST_CHECKED;
            if (!loud && !sys) { SendMessageW(id == ID_LOUDNESS ? app->loudness_check : app->system_check, BM_SETCHECK, BST_CHECKED, 0); }
            app->settings.show_loudness = SendMessageW(app->loudness_check, BM_GETCHECK, 0, 0) == BST_CHECKED;
            app->settings.show_system = SendMessageW(app->system_check, BM_GETCHECK, 0, 0) == BST_CHECKED; position_overlay(app);
        }
        return 0; }
    case WM_HSCROLL: if ((HWND)lparam == app->rate_slider) update_rate(app); return 0;
    case WM_CTLCOLORSTATIC: SetBkMode((HDC)wparam, TRANSPARENT); SetTextColor((HDC)wparam, primary); return (LRESULT)app->background_brush;
    case WM_CTLCOLORBTN: SetBkMode((HDC)wparam, TRANSPARENT); SetTextColor((HDC)wparam, primary); return (LRESULT)app->background_brush;
    case WM_ERASEBKGND: { RECT r; GetClientRect(window, &r); FillRect((HDC)wparam, &r, app->background_brush); return 1; }
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

static int overlay_menu(App *app, HWND window, LPARAM position) {
    POINT point;
    if ((LONG_PTR)position == -1) GetCursorPos(&point); else { point.x = GET_X_LPARAM(position); point.y = GET_Y_LPARAM(position); }
    HMENU menu = CreatePopupMenu(); AppendMenuW(menu, MF_STRING, ID_OPEN_SETTINGS, L"Open settings"); AppendMenuW(menu, MF_SEPARATOR, 0, NULL); AppendMenuW(menu, MF_STRING, ID_CLOSE_APP, L"Close");
    app->menu_open = true;
    int command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, 0, window, NULL);
    app->menu_open = false; DestroyMenu(menu); position_overlay(app); RedrawWindow(app->overlay_window, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW); return command;
}

static LRESULT CALLBACK hit_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    App *app = (App *)GetWindowLongPtrW(window, GWLP_USERDATA);
    if (message == WM_NCCREATE) { app = ((CREATESTRUCTW *)lparam)->lpCreateParams; SetWindowLongPtrW(window, GWLP_USERDATA, (LONG_PTR)app); app->hit_window = window; }
    if (!app) return DefWindowProcW(window, message, wparam, lparam);
    switch (message) {
    case WM_LBUTTONDBLCLK: show_settings(app); return 0;
    case WM_CONTEXTMENU: { int command = overlay_menu(app, window, lparam); if (command == ID_OPEN_SETTINGS) show_settings(app); else if (command == ID_CLOSE_APP) save_and_close(app); return 0; }
    case WM_CLOSE: save_and_close(app); return 0;
    case WM_ERASEBKGND: return 1;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

static LRESULT CALLBACK overlay_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    App *app = (App *)GetWindowLongPtrW(window, GWLP_USERDATA);
    if (message == WM_NCCREATE) { app = ((CREATESTRUCTW *)lparam)->lpCreateParams; SetWindowLongPtrW(window, GWLP_USERDATA, (LONG_PTR)app); app->overlay_window = window; }
    if (!app) return DefWindowProcW(window, message, wparam, lparam);
    switch (message) {
    case WM_TIMER: tick(app); return 0;
    case WM_CLOSE: save_and_close(app); return 0;
    case WM_LBUTTONDBLCLK: show_settings(app); return 0;
    case WM_CONTEXTMENU: {
        int command = overlay_menu(app, window, lparam);
        if (command == ID_OPEN_SETTINGS) show_settings(app); else if (command == ID_CLOSE_APP) save_and_close(app); return 0; }
    case WM_PAINT: { PAINTSTRUCT paint; HDC dc = BeginPaint(window, &paint); RECT rect; GetClientRect(window, &rect); paint_overlay(app, dc, rect); EndPaint(window, &paint); return 0; }
    case WM_ERASEBKGND: return 1;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

int app_run(HINSTANCE instance, int show_command) {
    (void)show_command; INITCOMMONCONTROLSEX common = {sizeof(common), ICC_STANDARD_CLASSES | ICC_BAR_CLASSES}; InitCommonControlsEx(&common);
    App app; ZeroMemory(&app, sizeof(app)); app.instance = instance; settings_load(&app.settings); audio_init(&app.audio, app.settings.peak_hold_ms); system_metrics_init(&app.metrics); enumerate_monitors(&app);
    app.background_brush = CreateSolidBrush(RGB(17, 20, 23)); app.font = CreateFontW(-14, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    app.small_font = CreateFontW(-11, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    app.value_font = CreateFontW(-16, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    HICON icon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_APP));
    WNDCLASSEXW settings_class = {sizeof(settings_class), CS_HREDRAW | CS_VREDRAW, settings_proc, 0, 0, instance, icon, LoadCursorW(NULL, IDC_ARROW), NULL, NULL, L"DjLoudnessMeterSettings", icon};
    WNDCLASSEXW hit_class = {sizeof(hit_class), CS_DBLCLKS, hit_proc, 0, 0, instance, icon, LoadCursorW(NULL, IDC_ARROW), NULL, NULL, L"DjLoudnessMeterHitArea", icon};
    WNDCLASSEXW overlay_class = {sizeof(overlay_class), CS_DBLCLKS, overlay_proc, 0, 0, instance, icon, LoadCursorW(NULL, IDC_ARROW), NULL, NULL, L"DjLoudnessMeterOverlay", icon};
    RegisterClassExW(&settings_class); RegisterClassExW(&hit_class); RegisterClassExW(&overlay_class);
    int x = app.settings.has_window_position ? app.settings.window_x : CW_USEDEFAULT, y = app.settings.has_window_position ? app.settings.window_y : CW_USEDEFAULT;
    int settings_width = app.settings.window_width < 460 ? 460 : app.settings.window_width;
    int settings_height = app.settings.window_height < 430 ? 430 : app.settings.window_height;
    HWND settings = CreateWindowExW(0, settings_class.lpszClassName, L"DJ Loudness Meter Settings", WS_OVERLAPPEDWINDOW,
        x, y, settings_width, settings_height, NULL, NULL, instance, &app);
    HWND hit = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_LAYERED, hit_class.lpszClassName, L"", WS_POPUP,
        0, 0, 360, 42, NULL, NULL, instance, &app);
    HWND overlay = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_LAYERED, overlay_class.lpszClassName, L"DJ Loudness Meter", WS_POPUP,
        0, 0, 360, 42, NULL, NULL, instance, &app);
    if (!settings || !hit || !overlay) return 1; SetLayeredWindowAttributes(hit, 0, 1, LWA_ALPHA); SetLayeredWindowAttributes(overlay, color_key, 255, LWA_COLORKEY); position_overlay(&app); ShowWindow(hit, SW_SHOWNOACTIVATE); ShowWindow(overlay, SW_SHOWNOACTIVATE);
    SetTimer(overlay, TIMER_UI, (UINT)app.settings.refresh_ms, NULL); tick(&app);
    MSG message; while (GetMessageW(&message, NULL, 0, 0) > 0) { TranslateMessage(&message); DispatchMessageW(&message); }
    audio_dispose(&app.audio); system_metrics_dispose(&app.metrics);
    DestroyWindow(app.overlay_window); DestroyWindow(app.hit_window); DestroyWindow(app.settings_window);
    DeleteObject(app.font); DeleteObject(app.small_font); DeleteObject(app.value_font); DeleteObject(app.background_brush); return (int)message.wParam;
}
