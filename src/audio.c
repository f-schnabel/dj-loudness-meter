#include <initguid.h>
#include "audio.h"
#include <audioclient.h>
#include <avrt.h>
#include <propkeydef.h>
#include <functiondiscoverykeys_devpkey.h>
#include <ksmedia.h>
#include <mmdeviceapi.h>
#include <propvarutil.h>
#include <math.h>
#include <stdlib.h>
#include <wchar.h>

#pragma warning(disable: 4152) /* GetProcAddress is the Win32 function-loading API. */

typedef struct ebur128_state ebur128_state;
typedef ebur128_state *(__cdecl *ebur_init_fn)(unsigned, unsigned long, int);
typedef void (__cdecl *ebur_destroy_fn)(ebur128_state **);
typedef int (__cdecl *ebur_add_fn)(ebur128_state *, const float *, size_t);
typedef int (__cdecl *ebur_read_fn)(ebur128_state *, double *);
typedef struct { HMODULE module; ebur_init_fn init; ebur_destroy_fn destroy; ebur_add_fn add; ebur_read_fn momentary, short_term; } EburApi;

static const GUID djlm_clsid_device_enumerator = {0xbcde0395,0xe52f,0x467c,{0x8e,0x3d,0xc4,0x57,0x92,0x91,0x69,0x2e}};
static const GUID djlm_iid_device_enumerator = {0xa95664d2,0x9614,0x4f35,{0xa7,0x46,0xde,0x8d,0xb6,0x36,0x17,0xe6}};
static const GUID djlm_iid_audio_client = {0x1cb9ad4c,0xdbfa,0x4c32,{0xb1,0x78,0xc2,0xf5,0x68,0xa7,0x03,0xb2}};
static const GUID djlm_iid_capture_client = {0xc8adbd64,0xe71e,0x48a0,{0xa4,0xde,0x18,0x5c,0x39,0x5c,0xd3,0x17}};
static const GUID djlm_pcm = {1,0,0x0010,{0x80,0x00,0x00,0xaa,0x00,0x38,0x9b,0x71}};
static const GUID djlm_float = {3,0,0x0010,{0x80,0x00,0x00,0xaa,0x00,0x38,0x9b,0x71}};

static bool load_ebur(EburApi *api) {
    ZeroMemory(api, sizeof(*api)); api->module = LoadLibraryW(L"ebur128.dll"); if (!api->module) return false;
#define LOAD_EBUR(field, name) api->field = (void *)GetProcAddress(api->module, name); if (!api->field) goto failed
    LOAD_EBUR(init, "ebur128_init"); LOAD_EBUR(destroy, "ebur128_destroy"); LOAD_EBUR(add, "ebur128_add_frames_float");
    LOAD_EBUR(momentary, "ebur128_loudness_momentary"); LOAD_EBUR(short_term, "ebur128_loudness_shortterm");
#undef LOAD_EBUR
    return true;
failed: FreeLibrary(api->module); ZeroMemory(api, sizeof(*api)); return false;
}

static void set_error(AudioEngine *e, const wchar_t *message) {
    AcquireSRWLockExclusive(&e->lock); wcscpy_s(e->error, _countof(e->error), message); e->connected = false; ReleaseSRWLockExclusive(&e->lock);
}

void audio_init(AudioEngine *e, int hold_ms) {
    ZeroMemory(e, sizeof(*e)); InitializeSRWLock(&e->lock); e->hold_ms = hold_ms;
    LARGE_INTEGER f; QueryPerformanceFrequency(&f); e->frequency = f.QuadPart;
}

static wchar_t *device_friendly_name(IMMDevice *device, wchar_t *buffer, size_t count) {
    IPropertyStore *store = NULL; PROPVARIANT value; PropVariantInit(&value); buffer[0] = 0;
    if (SUCCEEDED(IMMDevice_OpenPropertyStore(device, STGM_READ, &store)) &&
        SUCCEEDED(IPropertyStore_GetValue(store, &PKEY_Device_FriendlyName, &value)) && value.vt == VT_LPWSTR)
        wcsncpy_s(buffer, count, value.pwszVal, _TRUNCATE);
    PropVariantClear(&value); if (store) IPropertyStore_Release(store); return buffer;
}

int audio_enumerate(AudioDevice *devices, int capacity) {
    IMMDeviceEnumerator *enumerator = NULL; IMMDeviceCollection *collection = NULL; IMMDevice *default_device = NULL;
    LPWSTR default_id = NULL; int result = 0;
    if (FAILED(CoCreateInstance(&djlm_clsid_device_enumerator, NULL, CLSCTX_ALL, &djlm_iid_device_enumerator, (void **)&enumerator))) return 0;
    if (SUCCEEDED(IMMDeviceEnumerator_GetDefaultAudioEndpoint(enumerator, eRender, eMultimedia, &default_device)))
        IMMDevice_GetId(default_device, &default_id);
    if (SUCCEEDED(IMMDeviceEnumerator_EnumAudioEndpoints(enumerator, eRender, DEVICE_STATE_ACTIVE, &collection))) {
        UINT count = 0; IMMDeviceCollection_GetCount(collection, &count);
        for (UINT i = 0; i < count && result < capacity; ++i) {
            IMMDevice *device = NULL; LPWSTR id = NULL;
            if (SUCCEEDED(IMMDeviceCollection_Item(collection, i, &device)) && SUCCEEDED(IMMDevice_GetId(device, &id))) {
                wcsncpy_s(devices[result].id, _countof(devices[result].id), id, _TRUNCATE);
                device_friendly_name(device, devices[result].name, _countof(devices[result].name));
                devices[result].is_default = default_id && wcscmp(default_id, id) == 0; ++result;
            }
            CoTaskMemFree(id); if (device) IMMDevice_Release(device);
        }
    }
    if (default_id) CoTaskMemFree(default_id); if (default_device) IMMDevice_Release(default_device);
    if (collection) IMMDeviceCollection_Release(collection); IMMDeviceEnumerator_Release(enumerator);
    for (int i = 0; i < result; ++i) for (int j = i + 1; j < result; ++j) {
        bool swap = (!devices[i].is_default && devices[j].is_default) ||
            (devices[i].is_default == devices[j].is_default && _wcsicmp(devices[i].name, devices[j].name) > 0);
        if (swap) { AudioDevice temp = devices[i]; devices[i] = devices[j]; devices[j] = temp; }
    }
    return result;
}

static bool wave_format(WAVEFORMATEX *wave, int *channels, int *rate, int *bits, bool *is_float) {
    *channels = wave->nChannels; *rate = (int)wave->nSamplesPerSec; *bits = wave->wBitsPerSample; *is_float = false;
    WORD tag = wave->wFormatTag;
    if (tag == WAVE_FORMAT_EXTENSIBLE && wave->cbSize >= 22) {
        WAVEFORMATEXTENSIBLE *ext = (WAVEFORMATEXTENSIBLE *)wave;
        if (IsEqualGUID(&ext->SubFormat, &djlm_float)) *is_float = true;
        else if (!IsEqualGUID(&ext->SubFormat, &djlm_pcm)) return false;
    } else if (tag == WAVE_FORMAT_IEEE_FLOAT) *is_float = true;
    else if (tag != WAVE_FORMAT_PCM) return false;
    if (*channels < 1 || *channels > 2) return false;
    if ((*is_float && *bits != 32) || (!*is_float && *bits != 16 && *bits != 24 && *bits != 32)) return false;
    return wave->nBlockAlign == *channels * (*bits / 8);
}

static bool reset_loudness(AudioEngine *e, EburApi *api) {
    if (e->loudness) api->destroy((ebur128_state **)&e->loudness);
    e->loudness = api->init((unsigned)e->channels, (unsigned long)e->sample_rate, 3);
    return e->loudness != NULL;
}

static DWORD WINAPI capture_thread(void *parameter) {
    AudioEngine *e = parameter; HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED); bool com = SUCCEEDED(hr);
    IMMDeviceEnumerator *enumerator = NULL; IMMDevice *device = NULL; IAudioClient *client = NULL; IAudioCaptureClient *capture = NULL;
    WAVEFORMATEX *wave = NULL; HANDLE packet_event = NULL; float *converted = NULL; EburApi api = {0}; bool api_loaded = false;
    DWORD mmcss_index = 0; HANDLE mmcss = NULL;
    if (!com) { set_error(e, L"COM initialization failed."); goto cleanup; }
    if (!load_ebur(&api)) { set_error(e, L"ebur128.dll is missing or incompatible."); goto cleanup; } api_loaded = true;
    if (FAILED(CoCreateInstance(&djlm_clsid_device_enumerator, NULL, CLSCTX_ALL, &djlm_iid_device_enumerator, (void **)&enumerator)) ||
        FAILED(IMMDeviceEnumerator_GetDevice(enumerator, e->endpoint_id, &device)) ||
        FAILED(IMMDevice_Activate(device, &djlm_iid_audio_client, CLSCTX_ALL, NULL, (void **)&client)) ||
        FAILED(IAudioClient_GetMixFormat(client, &wave))) { set_error(e, L"The playback device could not be opened."); goto cleanup; }
    int channels, rate, bits; bool is_float;
    if (!wave_format(wave, &channels, &rate, &bits, &is_float)) { set_error(e, L"Only mono/stereo float or PCM 16/24/32-bit audio is supported."); goto cleanup; }
    packet_event = CreateEventW(NULL, FALSE, FALSE, NULL); if (!packet_event) { set_error(e, L"Audio event creation failed."); goto cleanup; }
    hr = IAudioClient_Initialize(client, AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK, 1000000, 0, wave, NULL);
    if (FAILED(hr) || FAILED(IAudioClient_SetEventHandle(client, packet_event)) ||
        FAILED(IAudioClient_GetService(client, &djlm_iid_capture_client, (void **)&capture))) {
        set_error(e, L"WASAPI loopback capture could not start."); goto cleanup;
    }
    UINT32 buffer_frames = 0; IAudioClient_GetBufferSize(client, &buffer_frames);
    size_t capacity = (size_t)buffer_frames * channels; converted = calloc(capacity, sizeof(float));
    if (!converted) { set_error(e, L"Audio buffer allocation failed."); goto cleanup; }
    LARGE_INTEGER now; QueryPerformanceCounter(&now);
    AcquireSRWLockExclusive(&e->lock);
    e->channels = channels; e->sample_rate = rate; e->started_at = now.QuadPart; e->last_packet = e->last_signal = 0; e->loudness_reset_for_silence = true;
    peak_init(&e->peak, e->frequency, e->hold_ms);
    if (!reset_loudness(e, &api)) { ReleaseSRWLockExclusive(&e->lock); set_error(e, L"libebur128 initialization failed."); goto cleanup; }
    device_friendly_name(device, e->device_name, _countof(e->device_name));
    swprintf_s(e->format, _countof(e->format), L"%.1f kHz - %d-bit %s - %d ch", rate / 1000.0, bits, is_float ? L"float" : L"PCM", channels);
    e->error[0] = 0; e->connected = true; ReleaseSRWLockExclusive(&e->lock);
    mmcss = AvSetMmThreadCharacteristicsW(L"Audio", &mmcss_index);
    if (FAILED(IAudioClient_Start(client))) { set_error(e, L"WASAPI loopback capture could not start."); goto cleanup; }
    HANDLE waits[2] = {e->stop_event, packet_event}; bool running = true;
    while (running) {
        DWORD wait = WaitForMultipleObjects(2, waits, FALSE, INFINITE); if (wait == WAIT_OBJECT_0) break; if (wait != WAIT_OBJECT_0 + 1) break;
        UINT32 packet = 0;
        for (;;) {
            HRESULT next_result = IAudioCaptureClient_GetNextPacketSize(capture, &packet);
            if (FAILED(next_result)) { set_error(e, L"The playback device disconnected or WASAPI capture failed."); running = false; break; }
            if (!packet) break;
            BYTE *data = NULL; UINT32 frames = 0; DWORD flags = 0;
            if (FAILED(IAudioCaptureClient_GetBuffer(capture, &data, &frames, &flags, NULL, NULL))) { set_error(e, L"The playback device disconnected or WASAPI capture failed."); running = false; break; }
            size_t samples = (size_t)frames * channels; const float *values = NULL;
            if (samples > capacity) { IAudioCaptureClient_ReleaseBuffer(capture, frames); set_error(e, L"WASAPI returned an oversized packet."); running = false; break; }
            if (flags & AUDCLNT_BUFFERFLAGS_SILENT) { ZeroMemory(converted, samples * sizeof(float)); values = converted; }
            else if (is_float) values = (const float *)data;
            else { if (!pcm_to_float(data, samples * (bits / 8), (unsigned)bits, converted, capacity)) running = false; values = converted; }
            QueryPerformanceCounter(&now); AcquireSRWLockExclusive(&e->lock);
            if (e->reset_requested) { peak_reset(&e->peak); reset_loudness(e, &api); e->loudness_reset_for_silence = true; e->reset_requested = false; }
            float packet_peak = peak_update(&e->peak, values, samples, (unsigned)channels, now.QuadPart);
            if (e->loudness) api.add(e->loudness, values, frames); e->last_packet = now.QuadPart;
            if (packet_peak > 0.000001f) { e->last_signal = now.QuadPart; e->loudness_reset_for_silence = false; }
            ReleaseSRWLockExclusive(&e->lock); IAudioCaptureClient_ReleaseBuffer(capture, frames);
        }
    }
    IAudioClient_Stop(client);
cleanup:
    AcquireSRWLockExclusive(&e->lock); e->connected = false; if (e->loudness && api_loaded) api.destroy((ebur128_state **)&e->loudness); ReleaseSRWLockExclusive(&e->lock);
    if (mmcss) AvRevertMmThreadCharacteristics(mmcss); free(converted); if (wave) CoTaskMemFree(wave); if (packet_event) CloseHandle(packet_event);
    if (capture) IAudioCaptureClient_Release(capture); if (client) IAudioClient_Release(client); if (device) IMMDevice_Release(device);
    if (enumerator) IMMDeviceEnumerator_Release(enumerator); if (api_loaded) FreeLibrary(api.module); if (com) CoUninitialize(); return 0;
}

bool audio_start(AudioEngine *e, const wchar_t *endpoint) {
    audio_stop(e); AcquireSRWLockExclusive(&e->lock); e->device_name[0] = e->format[0] = e->error[0] = 0; e->connected = false; ReleaseSRWLockExclusive(&e->lock);
    wcsncpy_s(e->endpoint_id, _countof(e->endpoint_id), endpoint, _TRUNCATE);
    e->stop_event = CreateEventW(NULL, TRUE, FALSE, NULL); if (!e->stop_event) return false;
    e->thread = CreateThread(NULL, 0, capture_thread, e, 0, NULL); if (!e->thread) { CloseHandle(e->stop_event); e->stop_event = NULL; return false; }
    return true;
}

void audio_stop(AudioEngine *e) {
    if (e->stop_event) SetEvent(e->stop_event); if (e->thread) { WaitForSingleObject(e->thread, INFINITE); CloseHandle(e->thread); }
    if (e->stop_event) CloseHandle(e->stop_event); e->thread = e->stop_event = NULL;
    AcquireSRWLockExclusive(&e->lock); e->connected = false; ReleaseSRWLockExclusive(&e->lock);
}

void audio_dispose(AudioEngine *e) { audio_stop(e); }
void audio_reset(AudioEngine *e) { AcquireSRWLockExclusive(&e->lock); e->reset_requested = true; ReleaseSRWLockExclusive(&e->lock); }

MeterSnapshot audio_snapshot(AudioEngine *e) {
    MeterSnapshot s = {-INFINITY, -INFINITY, -INFINITY, -INFINITY, false, false, false, false}; LARGE_INTEGER now; QueryPerformanceCounter(&now);
    AcquireSRWLockExclusive(&e->lock); s.connected = e->connected;
    if (e->connected && e->loudness) {
        int64_t signal_at = e->last_signal ? e->last_signal : e->started_at;
        int64_t silence_ticks = now.QuadPart - signal_at;
        bool silent = silence_ticks >= e->frequency * 5;
        s.hide_values = silence_ticks >= e->frequency * 10;
        PeakReading p = peak_read(&e->peak, now.QuadPart);
        if (silent) { peak_reset(&e->peak); if (!e->loudness_reset_for_silence) { e->reset_requested = true; e->loudness_reset_for_silence = true; } }
        else {
            s.peak_db = linear_to_db(p.left > p.right ? p.left : p.right); s.hold_db = linear_to_db(p.hold); s.clipping = p.clipping;
            double m = -INFINITY, st = -INFINITY; /* reads are cheap and serialized with frame submission */
            HMODULE module = GetModuleHandleW(L"ebur128.dll");
            if (module) { ebur_read_fn rm = (void *)GetProcAddress(module, "ebur128_loudness_momentary"); ebur_read_fn rs = (void *)GetProcAddress(module, "ebur128_loudness_shortterm"); if (rm && !rm(e->loudness, &m) && isfinite(m)) s.momentary = m; if (rs && !rs(e->loudness, &st) && isfinite(st)) s.short_term = st; }
            s.recent_audio = e->last_packet && now.QuadPart - e->last_packet <= e->frequency * 3 / 4;
        }
    }
    ReleaseSRWLockExclusive(&e->lock); return s;
}

void audio_status(AudioEngine *e, wchar_t *device, int dc, wchar_t *format, int fc, wchar_t *error, int ec) {
    AcquireSRWLockShared(&e->lock); wcsncpy_s(device, dc, e->device_name, _TRUNCATE); wcsncpy_s(format, fc, e->format, _TRUNCATE); wcsncpy_s(error, ec, e->error, _TRUNCATE); ReleaseSRWLockShared(&e->lock);
}
