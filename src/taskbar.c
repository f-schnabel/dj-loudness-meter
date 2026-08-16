#include "taskbar.h"
#include "automation.h"
#include <stdlib.h>
#include <wchar.h>

typedef struct {
    const wchar_t *monitor_device;
    int left;
    bool found;
} TraySearch;

static BOOL CALLBACK tray_child_callback(HWND window, LPARAM data) {
    TraySearch *search = (TraySearch *)data;
    wchar_t class_name[128];
    RECT rect;
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
    TraySearch *search = (TraySearch *)data;
    wchar_t class_name[64];
    GetClassNameW(window, class_name, _countof(class_name));
    if (_wcsicmp(class_name, L"Shell_TrayWnd") != 0 && _wcsicmp(class_name, L"Shell_SecondaryTrayWnd") != 0) return TRUE;
    HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
    MONITORINFOEXW info = {sizeof(info)};
    if (!GetMonitorInfoW(monitor, (MONITORINFO *)&info) || _wcsicmp(info.szDevice, search->monitor_device) != 0) return TRUE;
    EnumChildWindows(window, tray_child_callback, data);
    return search->found ? FALSE : TRUE;
}

static bool query_taskbar_safe_right(const DisplayInfo *monitor, int *right) {
    TraySearch search = {monitor->device, 0, false};
    EnumWindows(taskbar_callback, (LPARAM)&search);
    if (search.found) *right = search.left - 4;
    return search.found;
}

bool taskbar_safe_right(const DisplayInfo *monitor, int *right) {
    static ULONGLONG last_check;
    static wchar_t cached_monitor[32];
    static int cached_right;
    static bool cached_found;
    ULONGLONG now = GetTickCount64();
    if (_wcsicmp(cached_monitor, monitor->device) == 0 && now - last_check < 5000) {
        if (cached_found) *right = cached_right;
        return cached_found;
    }
    last_check = now;
    wcscpy_s(cached_monitor, _countof(cached_monitor), monitor->device);
    cached_found = query_taskbar_safe_right(monitor, &cached_right);
    if (cached_found) *right = cached_right;
    return cached_found;
}

typedef struct {
    const wchar_t *monitor_device;
    HWND window;
} TaskbarSearch;

static BOOL CALLBACK find_taskbar_callback(HWND window, LPARAM data) {
    TaskbarSearch *search = (TaskbarSearch *)data;
    wchar_t class_name[64];
    GetClassNameW(window, class_name, _countof(class_name));
    if (_wcsicmp(class_name, L"Shell_TrayWnd") != 0 && _wcsicmp(class_name, L"Shell_SecondaryTrayWnd") != 0) return TRUE;
    HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
    MONITORINFOEXW info = {sizeof(info)};
    if (GetMonitorInfoW(monitor, (MONITORINFO *)&info) && _wcsicmp(info.szDevice, search->monitor_device) == 0) {
        search->window = window;
        return FALSE;
    }
    return TRUE;
}

static HWND find_taskbar(const DisplayInfo *monitor) {
    TaskbarSearch search = {monitor->device, NULL};
    EnumWindows(find_taskbar_callback, (LPARAM)&search);
    return search.window;
}

static bool widgets_enabled(void) {
    DWORD value = 0, size = sizeof(value);
    LSTATUS status = RegGetValueW(
        HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced",
        L"TaskbarDa",
        RRF_RT_REG_DWORD,
        NULL,
        &value,
        &size
    );
    return status == ERROR_SUCCESS && value != 0;
}

static bool query_widget_right(const DisplayInfo *monitor, int *right) {
    if (!widgets_enabled()) return false;
    HWND taskbar = find_taskbar(monitor);
    if (!taskbar) return false;
    return automation_widget_right(taskbar, right);
}

bool taskbar_safe_left(const DisplayInfo *monitor, int *left) {
    static ULONGLONG last_check;
    static wchar_t cached_monitor[32];
    static int cached_left;
    static bool cached_found;
    ULONGLONG now = GetTickCount64();
    if (_wcsicmp(cached_monitor, monitor->device) == 0 && now - last_check < 5000) {
        if (cached_found) *left = cached_left;
        return cached_found;
    }
    last_check = now;
    wcscpy_s(cached_monitor, _countof(cached_monitor), monitor->device);
    cached_found = query_widget_right(monitor, &cached_left);
    if (cached_found) *left = cached_left;
    return cached_found;
}
