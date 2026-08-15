#ifndef DJLM_SYSTEM_METRICS_H
#define DJLM_SYSTEM_METRICS_H

#include <stdbool.h>
#include <windows.h>
#include <pdh.h>

typedef struct {
    ULONGLONG idle, kernel, user;
    bool has_cpu_sample, temperature_available;
    PDH_HQUERY temperature_query;
    PDH_HCOUNTER temperature_counter;
    void *temperature_buffer;
    DWORD temperature_buffer_size;
} SystemMetrics;

typedef struct { double cpu, memory, temperature; bool has_cpu, has_memory, has_temperature; } SystemSnapshot;

void system_metrics_init(SystemMetrics *metrics);
SystemSnapshot system_metrics_read(SystemMetrics *metrics);
void system_metrics_dispose(SystemMetrics *metrics);

#endif

