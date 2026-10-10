#include "monitors.h"
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    DisplayInfo *monitors;
    int count, capacity;
} MonitorList;

static BOOL CALLBACK monitor_callback(HMONITOR monitor, HDC dc, LPRECT rect, LPARAM data) {
    (void)dc;
    (void)rect;
    MonitorList *list = (MonitorList *)data;
    if (list->count >= list->capacity) return FALSE;

    MONITORINFOEXW info = {sizeof(info)};
    if (!GetMonitorInfoW(monitor, (MONITORINFO *)&info)) return TRUE;

    DisplayInfo *display = &list->monitors[list->count++];
    ZeroMemory(display, sizeof(*display));
    wcscpy_s(display->device, _countof(display->device), info.szDevice);
    display->bounds = info.rcMonitor;
    display->work = info.rcWork;
    display->primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0;
    DISPLAY_DEVICEW device = {sizeof(device)};
    for (DWORD i = 0; EnumDisplayDevicesW(info.szDevice, i, &device, EDD_GET_DEVICE_INTERFACE_NAME); ++i) {
        if (device.StateFlags & DISPLAY_DEVICE_ACTIVE) {
            wcscpy_s(display->identity, _countof(display->identity), device.DeviceID);
            break;
        }
    }
    swprintf_s(display->label, _countof(display->label), L"Display %d%s", list->count, display->primary ? L" (Main)" : L"");
    return TRUE;
}

int monitors_enumerate(DisplayInfo *monitors, int capacity) {
    MonitorList list = {monitors, 0, capacity};
    EnumDisplayMonitors(NULL, NULL, monitor_callback, (LPARAM)&list);
    return list.count;
}

int monitors_select(const DisplayInfo *monitors, int count, const AppSettings *settings) {
    int fallback = count ? 0 : -1;
    for (int i = 0; i < count; ++i) {
        if (monitors[i].primary) fallback = i;
        // A numbered GDI name can now belong to a different monitor. Only use it for legacy settings.
        if (settings->monitor_id[0]) {
            if (_wcsicmp(settings->monitor_id, monitors[i].identity) == 0) return i;
        } else if (settings->monitor_name[0] && _wcsicmp(settings->monitor_name, monitors[i].device) == 0) return i;
    }
    return fallback;
}

void monitors_remember(AppSettings *settings, const DisplayInfo *monitor) {
    wcscpy_s(settings->monitor_name, _countof(settings->monitor_name), monitor->device);
    wcscpy_s(settings->monitor_id, _countof(settings->monitor_id), monitor->identity);
}
