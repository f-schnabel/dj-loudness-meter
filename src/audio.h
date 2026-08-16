#ifndef DJLM_AUDIO_H
#define DJLM_AUDIO_H

#include "meter.h"
#include <windows.h>

#define AUDIO_MAX_DEVICES 64

typedef struct { wchar_t id[512]; wchar_t name[256]; bool is_default; } AudioDevice;
typedef struct {
    double peak_db, hold_db, momentary, short_term;
    bool clipping, connected, recent_audio, hide_values;
} MeterSnapshot;

typedef struct {
    SRWLOCK lock;
    HANDLE thread, stop_event;
    wchar_t endpoint_id[512], device_name[256], format[128], error[256];
    int hold_ms;
    bool connected, reset_requested, loudness_reset_for_silence;
    PeakMeter peak;
    void *loudness;
    int channels, sample_rate;
    int64_t frequency, started_at, last_packet, last_signal;
} AudioEngine;

#ifdef __cplusplus
extern "C" {
#endif

void audio_init(AudioEngine *engine, int hold_ms);
void audio_dispose(AudioEngine *engine);
int audio_enumerate(AudioDevice *devices, int capacity);
bool audio_start(AudioEngine *engine, const wchar_t *endpoint_id);
void audio_stop(AudioEngine *engine);
void audio_reset(AudioEngine *engine);
MeterSnapshot audio_snapshot(AudioEngine *engine);
void audio_status(AudioEngine *engine, wchar_t *device, int device_count, wchar_t *format, int format_count, wchar_t *error, int error_count);

#ifdef __cplusplus
}
#endif

#endif
