#include "app.h"
#include "overlay.h"
#include "resource.h"
#include "settings_window.h"
#include <commctrl.h>
#include <stdio.h>
#include <stdlib.h>
#include <windowsx.h>

#define ID_CLOSE_APP 1010
#define ID_OPEN_SETTINGS 1011
#define TIMER_UI 1

static LRESULT CALLBACK hit_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
static LRESULT CALLBACK overlay_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);

static WNDCLASSEXW make_window_class(HINSTANCE instance, HICON icon, const wchar_t *name, UINT style, WNDPROC procedure) {
    WNDCLASSEXW result = {sizeof(result)};
    result.style = style;
    result.lpfnWndProc = procedure;
    result.hInstance = instance;
    result.hIcon = icon;
    result.hCursor = LoadCursorW(NULL, IDC_ARROW);
    result.lpszClassName = name;
    result.hIconSm = icon;
    return result;
}

static BOOL CALLBACK monitor_callback(HMONITOR monitor, HDC dc, LPRECT rect, LPARAM data) {
    (void)dc;
    (void)rect;
    App *app = (App *)data;
    if (app->monitor_count >= MAX_MONITORS) return FALSE;

    MONITORINFOEXW info = {sizeof(info)};
    if (!GetMonitorInfoW(monitor, (MONITORINFO *)&info)) return TRUE;

    DisplayInfo *display = &app->monitors[app->monitor_count++];
    wcscpy_s(display->device, _countof(display->device), info.szDevice);
    display->bounds = info.rcMonitor;
    display->work = info.rcWork;
    display->primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0;
    return TRUE;
}

static void enumerate_monitors(App *app) {
    app->monitor_count = 0;
    EnumDisplayMonitors(NULL, NULL, monitor_callback, (LPARAM)app);
    app->selected_monitor = 0;
    for (int i = 0; i < app->monitor_count; ++i) {
        swprintf_s(
            app->monitors[i].label,
            _countof(app->monitors[i].label),
            L"Display %d%s",
            i + 1,
            app->monitors[i].primary ? L" (Main)" : L""
        );
        if ((app->settings.monitor_name[0] && _wcsicmp(app->settings.monitor_name, app->monitors[i].device) == 0) ||
            (!app->settings.monitor_name[0] && app->monitors[i].primary)) {
            app->selected_monitor = i;
        }
    }
}

static void set_text_if_changed(HWND window, wchar_t *current, size_t count, const wchar_t *next) {
    if (wcscmp(current, next) == 0) return;
    wcscpy_s(current, count, next);
    SetWindowTextW(window, next);
}

void app_set_status(App *app, const wchar_t *text) {
    set_text_if_changed(app->status_label, app->status_text, _countof(app->status_text), text);
}

static void update_status(App *app) {
    wchar_t device[256], format[128], failure[256], status[512];
    audio_status(&app->audio, device, _countof(device), format, _countof(format), failure, _countof(failure));
    set_text_if_changed(app->format_label, app->format_text, _countof(app->format_text), format);
    if (failure[0]) {
        app_set_status(app, failure);
    } else if (app->meter_snapshot.connected) {
        swprintf_s(status, _countof(status), L"Monitoring %s%s", device, app->meter_snapshot.recent_audio ? L"" : L" - waiting for audio");
        app_set_status(app, status);
    }
}

static void tick(App *app) {
    app->meter_snapshot = audio_snapshot(&app->audio);
    ULONGLONG now = GetTickCount64();
    if (!app->last_system_tick || now - app->last_system_tick >= 500) {
        app->last_system_tick = now;
        if (app->settings.show_system) app->system_snapshot = system_metrics_read(&app->metrics);
        if (!app->menu_open) overlay_position(app);
    }
    if (!app->last_status_tick || now - app->last_status_tick >= 500) {
        app->last_status_tick = now;
        update_status(app);
    }
    InvalidateRect(app->overlay_window, NULL, FALSE);
}

static void save_and_close(App *app) {
    app->closing = true;
    RECT rect;
    if (!IsIconic(app->settings_window) && GetWindowRect(app->settings_window, &rect)) {
        app->settings.window_x = rect.left;
        app->settings.window_y = rect.top;
        app->settings.window_width = rect.right - rect.left;
        app->settings.window_height = rect.bottom - rect.top;
    }
    if (app->monitor_count) {
        wcscpy_s(app->settings.monitor_name, _countof(app->settings.monitor_name), app->monitors[app->selected_monitor].device);
    }
    settings_window_apply_zero(app);
    settings_save(&app->settings);
    PostQuitMessage(0);
}

static int overlay_menu(App *app, HWND window, LPARAM position) {
    POINT point;
    if ((LONG_PTR)position == -1) {
        GetCursorPos(&point);
    } else {
        point.x = GET_X_LPARAM(position);
        point.y = GET_Y_LPARAM(position);
    }

    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, ID_OPEN_SETTINGS, L"Open settings");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, ID_CLOSE_APP, L"Close");
    app->menu_open = true;
    int command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, 0, window, NULL);
    app->menu_open = false;
    DestroyMenu(menu);
    overlay_position(app);
    RedrawWindow(app->overlay_window, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW);
    return command;
}

static void handle_overlay_command(App *app, int command) {
    if (command == ID_OPEN_SETTINGS) settings_window_show(app);
    else if (command == ID_CLOSE_APP) save_and_close(app);
}

static LRESULT CALLBACK hit_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    App *app = (App *)GetWindowLongPtrW(window, GWLP_USERDATA);
    if (message == WM_NCCREATE) {
        app = ((CREATESTRUCTW *)lparam)->lpCreateParams;
        SetWindowLongPtrW(window, GWLP_USERDATA, (LONG_PTR)app);
        app->hit_window = window;
    }
    if (!app) return DefWindowProcW(window, message, wparam, lparam);

    switch (message) {
    case WM_LBUTTONDBLCLK:
        settings_window_show(app);
        return 0;
    case WM_CONTEXTMENU:
        handle_overlay_command(app, overlay_menu(app, window, lparam));
        return 0;
    case WM_CLOSE:
        save_and_close(app);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    default:
        return DefWindowProcW(window, message, wparam, lparam);
    }
}

static LRESULT CALLBACK overlay_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    App *app = (App *)GetWindowLongPtrW(window, GWLP_USERDATA);
    if (message == WM_NCCREATE) {
        app = ((CREATESTRUCTW *)lparam)->lpCreateParams;
        SetWindowLongPtrW(window, GWLP_USERDATA, (LONG_PTR)app);
        app->overlay_window = window;
    }
    if (!app) return DefWindowProcW(window, message, wparam, lparam);

    switch (message) {
    case WM_TIMER:
        tick(app);
        return 0;
    case WM_CLOSE:
        save_and_close(app);
        return 0;
    case WM_LBUTTONDBLCLK:
        settings_window_show(app);
        return 0;
    case WM_CONTEXTMENU:
        handle_overlay_command(app, overlay_menu(app, window, lparam));
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT paint;
        HDC dc = BeginPaint(window, &paint);
        RECT rect;
        GetClientRect(window, &rect);
        overlay_render(app, dc, rect);
        EndPaint(window, &paint);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    default:
        return DefWindowProcW(window, message, wparam, lparam);
    }
}

static void dispose_app(App *app) {
    settings_window_dispose(app);
    audio_dispose(&app->audio);
    system_metrics_dispose(&app->metrics);
    if (app->overlay_window) DestroyWindow(app->overlay_window);
    if (app->hit_window) DestroyWindow(app->hit_window);
    if (app->settings_window) DestroyWindow(app->settings_window);
    overlay_dispose(app);
    if (app->font) DeleteObject(app->font);
    if (app->small_font) DeleteObject(app->small_font);
    if (app->value_font) DeleteObject(app->value_font);
    if (app->system_value_font) DeleteObject(app->system_value_font);
    if (app->background_brush) DeleteObject(app->background_brush);
}

int app_run(HINSTANCE instance, int show_command) {
    (void)show_command;
    INITCOMMONCONTROLSEX common = {sizeof(common), ICC_STANDARD_CLASSES | ICC_BAR_CLASSES};
    InitCommonControlsEx(&common);

    App app;
    ZeroMemory(&app, sizeof(app));
    app.instance = instance;
    settings_load(&app.settings);
    audio_init(&app.audio, app.settings.peak_hold_ms);
    system_metrics_init(&app.metrics);
    enumerate_monitors(&app);

    app.background_brush = CreateSolidBrush(RGB(17, 20, 23));
    app.font = CreateFontW(-14, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    app.small_font = CreateFontW(
        -10,
        0,
        0,
        0,
        FW_NORMAL,
        0,
        0,
        0,
        DEFAULT_CHARSET,
        0,
        0,
        ANTIALIASED_QUALITY,
        DEFAULT_PITCH,
        L"Segoe UI Variable Small"
    );
    app.value_font =
        CreateFontW(-16, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Segoe UI Variable Text");
    app.system_value_font =
        CreateFontW(-14, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Segoe UI Variable Text");
    if (!app.background_brush || !overlay_init(&app)) {
        dispose_app(&app);
        return 1;
    }

    HICON icon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_APP));
    WNDCLASSEXW settings_class =
        make_window_class(instance, icon, L"DjLoudnessMeterSettings", CS_HREDRAW | CS_VREDRAW, settings_window_proc);
    WNDCLASSEXW hit_class = make_window_class(instance, icon, L"DjLoudnessMeterHitArea", CS_DBLCLKS, hit_proc);
    WNDCLASSEXW overlay_class = make_window_class(instance, icon, L"DjLoudnessMeterOverlay", CS_DBLCLKS, overlay_proc);
    RegisterClassExW(&settings_class);
    RegisterClassExW(&hit_class);
    RegisterClassExW(&overlay_class);

    int width = app.settings.window_width < SETTINGS_MIN_WIDTH ? SETTINGS_MIN_WIDTH : app.settings.window_width;
    int height = app.settings.window_height < SETTINGS_MIN_HEIGHT ? SETTINGS_MIN_HEIGHT : app.settings.window_height;
    RECT saved_rect = {
        app.settings.window_x,
        app.settings.window_y,
        app.settings.window_x + width,
        app.settings.window_y + height
    };
    bool saved_position_visible =
        app.settings.has_window_position && MonitorFromRect(&saved_rect, MONITOR_DEFAULTTONULL) != NULL;
    int x = saved_position_visible ? app.settings.window_x : CW_USEDEFAULT;
    int y = saved_position_visible ? app.settings.window_y : CW_USEDEFAULT;
    HWND settings = CreateWindowExW(
        0,
        settings_class.lpszClassName,
        L"DJ Loudness Meter Settings",
        WS_OVERLAPPEDWINDOW,
        x,
        y,
        width,
        height,
        NULL,
        NULL,
        instance,
        &app
    );
    HWND hit = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_LAYERED,
        hit_class.lpszClassName,
        L"",
        WS_POPUP,
        0,
        0,
        360,
        42,
        NULL,
        NULL,
        instance,
        &app
    );
    HWND overlay = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_LAYERED,
        overlay_class.lpszClassName,
        L"DJ Loudness Meter",
        WS_POPUP,
        0,
        0,
        360,
        42,
        NULL,
        NULL,
        instance,
        &app
    );
    if (!settings || !hit || !overlay) {
        dispose_app(&app);
        return 1;
    }

    SetLayeredWindowAttributes(hit, 0, 1, LWA_ALPHA);
    SetLayeredWindowAttributes(overlay, DJLM_COLOR_KEY, 255, LWA_COLORKEY);
    overlay_position(&app);
    ShowWindow(hit, SW_SHOWNOACTIVATE);
    ShowWindow(overlay, SW_SHOWNOACTIVATE);
    if (!SetTimer(overlay, TIMER_UI, (UINT)app.settings.refresh_ms, NULL)) {
        dispose_app(&app);
        return 1;
    }
    tick(&app);

    MSG message = {0};
    BOOL result;
    while ((result = GetMessageW(&message, NULL, 0, 0)) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    int exit_code = result < 0 ? 1 : (int)message.wParam;
    dispose_app(&app);
    return exit_code;
}
