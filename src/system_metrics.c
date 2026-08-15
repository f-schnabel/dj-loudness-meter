#include "system_metrics.h"
#include <pdhmsg.h>
#include <math.h>
#include <stdlib.h>

static ULONGLONG file_time(FILETIME value) { return ((ULONGLONG)value.dwHighDateTime << 32) | value.dwLowDateTime; }

void system_metrics_init(SystemMetrics *m) {
    ZeroMemory(m, sizeof(*m));
    if (PdhOpenQueryW(NULL, 0, &m->temperature_query) == ERROR_SUCCESS &&
        PdhAddEnglishCounterW(m->temperature_query, L"\\Thermal Zone Information(*)\\Temperature", 0, &m->temperature_counter) == ERROR_SUCCESS)
        m->temperature_available = true;
    else {
        if (m->temperature_query) PdhCloseQuery(m->temperature_query);
        m->temperature_query = NULL; m->temperature_counter = NULL;
    }
}

static void disable_temperature(SystemMetrics *m) {
    m->temperature_available = false;
    if (m->temperature_query) PdhCloseQuery(m->temperature_query);
    m->temperature_query = NULL; m->temperature_counter = NULL;
    free(m->temperature_buffer); m->temperature_buffer = NULL; m->temperature_buffer_size = 0;
}

static void read_temperature(SystemMetrics *m, SystemSnapshot *s) {
    if (!m->temperature_available || PdhCollectQueryData(m->temperature_query) != ERROR_SUCCESS) { if (m->temperature_available) disable_temperature(m); return; }
    DWORD size = m->temperature_buffer_size, count = 0;
    PDH_STATUS status = PdhGetFormattedCounterArrayW(m->temperature_counter, PDH_FMT_DOUBLE, &size, &count, m->temperature_buffer);
    if (status == PDH_MORE_DATA) {
        void *buffer = realloc(m->temperature_buffer, size); if (!buffer) { disable_temperature(m); return; }
        m->temperature_buffer = buffer; m->temperature_buffer_size = size;
        status = PdhGetFormattedCounterArrayW(m->temperature_counter, PDH_FMT_DOUBLE, &size, &count, buffer);
    }
    if (status != ERROR_SUCCESS) { disable_temperature(m); return; }
    PDH_FMT_COUNTERVALUE_ITEM_W *items = m->temperature_buffer; double hottest = -INFINITY;
    for (DWORD i = 0; i < count; ++i) if ((items[i].FmtValue.CStatus == PDH_CSTATUS_VALID_DATA || items[i].FmtValue.CStatus == PDH_CSTATUS_NEW_DATA) &&
        isfinite(items[i].FmtValue.doubleValue) && items[i].FmtValue.doubleValue > hottest) hottest = items[i].FmtValue.doubleValue;
    if (isfinite(hottest)) { s->temperature = hottest - 273.15; s->has_temperature = true; }
}

SystemSnapshot system_metrics_read(SystemMetrics *m) {
    SystemSnapshot s = {0}; FILETIME idle_time, kernel_time, user_time;
    if (GetSystemTimes(&idle_time, &kernel_time, &user_time)) {
        ULONGLONG idle = file_time(idle_time), kernel = file_time(kernel_time), user = file_time(user_time);
        if (m->has_cpu_sample && idle >= m->idle && kernel >= m->kernel && user >= m->user) {
            ULONGLONG idle_delta = idle - m->idle, total = kernel - m->kernel + user - m->user;
            if (total && idle_delta <= total) { s.cpu = 100.0 * (double)(total - idle_delta) / total; s.has_cpu = true; }
        }
        m->idle = idle; m->kernel = kernel; m->user = user; m->has_cpu_sample = true;
    }
    MEMORYSTATUSEX memory = {sizeof(memory)};
    if (GlobalMemoryStatusEx(&memory) && memory.ullTotalPhys) { s.memory = 100.0 * (double)(memory.ullTotalPhys - memory.ullAvailPhys) / memory.ullTotalPhys; s.has_memory = true; }
    read_temperature(m, &s); return s;
}

void system_metrics_dispose(SystemMetrics *m) { disable_temperature(m); }
