#include "audio.h"
#include <audioclient.h>
#include <avrt.h>
#include <propkeydef.h>
#include <functiondiscoverykeys_devpkey.h>
#include <ksmedia.h>
#include <math.h>
#include <memory>
#include <mmdeviceapi.h>
#include <propvarutil.h>
#include <stdlib.h>
#include <wchar.h>
#include <wil/com.h>
#include <wil/resource.h>

typedef struct ebur128_state ebur128_state;
extern "C" {
ebur128_state *__cdecl ebur128_init(unsigned channels, unsigned long sample_rate, int mode);
void __cdecl ebur128_destroy(ebur128_state **state);
int __cdecl ebur128_add_frames_float(ebur128_state *state, const float *source, size_t frames);
int __cdecl ebur128_loudness_momentary(ebur128_state *state, double *out);
int __cdecl ebur128_loudness_shortterm(ebur128_state *state, double *out);
}

static const GUID djlm_pcm = {1, 0, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};
static const GUID djlm_float = {3, 0, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};

using unique_mmcss_handle = wil::unique_any<HANDLE, decltype(&AvRevertMmThreadCharacteristics), AvRevertMmThreadCharacteristics>;

static void set_error(AudioEngine *e, const wchar_t *message) {
    auto lock = wil::AcquireSRWLockExclusive(&e->lock);
    wcscpy_s(e->error, _countof(e->error), message);
    e->connected = false;
}

static DWORD capture_fail(AudioEngine *e, const wchar_t *message) {
    set_error(e, message);
    return 0;
}

void audio_init(AudioEngine *e, int hold_ms) {
    ZeroMemory(e, sizeof(*e));
    InitializeSRWLock(&e->lock);
    e->hold_ms = hold_ms;
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    e->frequency = f.QuadPart;
}

static wchar_t *device_friendly_name(IMMDevice *device, wchar_t *buffer, size_t count) {
    wil::com_ptr_nothrow<IPropertyStore> store;
    wil::unique_prop_variant value;
    buffer[0] = 0;
    if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, store.put())) &&
        SUCCEEDED(store->GetValue(PKEY_Device_FriendlyName, value.addressof())) && value.vt == VT_LPWSTR)
        wcsncpy_s(buffer, count, value.pwszVal, _TRUNCATE);
    return buffer;
}

int audio_enumerate(AudioDevice *devices, int capacity) {
    auto enumerator = wil::CoCreateInstanceNoThrow<IMMDeviceEnumerator>(__uuidof(MMDeviceEnumerator), CLSCTX_ALL);
    wil::com_ptr_nothrow<IMMDeviceCollection> collection;
    wil::com_ptr_nothrow<IMMDevice> default_device;
    wil::unique_cotaskmem_string default_id;
    int result = 0;
    if (!enumerator) return 0;
    if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, default_device.put())))
        default_device->GetId(wil::out_param(default_id));
    if (SUCCEEDED(enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, collection.put()))) {
        UINT count = 0;
        collection->GetCount(&count);
        for (UINT i = 0; i < count && result < capacity; ++i) {
            wil::com_ptr_nothrow<IMMDevice> device;
            wil::unique_cotaskmem_string id;
            if (SUCCEEDED(collection->Item(i, device.put())) && SUCCEEDED(device->GetId(wil::out_param(id)))) {
                wcsncpy_s(devices[result].id, _countof(devices[result].id), id.get(), _TRUNCATE);
                device_friendly_name(device.get(), devices[result].name, _countof(devices[result].name));
                devices[result].is_default = default_id && wcscmp(default_id.get(), id.get()) == 0;
                ++result;
            }
        }
    }
    for (int i = 0; i < result; ++i)
        for (int j = i + 1; j < result; ++j) {
            bool swap = (!devices[i].is_default && devices[j].is_default) ||
                        (devices[i].is_default == devices[j].is_default && _wcsicmp(devices[i].name, devices[j].name) > 0);
            if (swap) {
                AudioDevice temp = devices[i];
                devices[i] = devices[j];
                devices[j] = temp;
            }
        }
    return result;
}

static bool wave_format(WAVEFORMATEX *wave, int *channels, int *rate, int *bits, bool *is_float) {
    *channels = wave->nChannels;
    *rate = (int)wave->nSamplesPerSec;
    *bits = wave->wBitsPerSample;
    *is_float = false;
    WORD tag = wave->wFormatTag;
    if (tag == WAVE_FORMAT_EXTENSIBLE && wave->cbSize >= 22) {
        WAVEFORMATEXTENSIBLE *ext = (WAVEFORMATEXTENSIBLE *)wave;
        if (IsEqualGUID(ext->SubFormat, djlm_float)) *is_float = true;
        else if (!IsEqualGUID(ext->SubFormat, djlm_pcm)) return false;
    } else if (tag == WAVE_FORMAT_IEEE_FLOAT) *is_float = true;
    else if (tag != WAVE_FORMAT_PCM) return false;
    if (*channels < 1 || *channels > 2) return false;
    if ((*is_float && *bits != 32) || (!*is_float && *bits != 16 && *bits != 24 && *bits != 32)) return false;
    return wave->nBlockAlign == *channels * (*bits / 8);
}

static bool reset_loudness(AudioEngine *e) {
    if (e->loudness) ebur128_destroy((ebur128_state **)&e->loudness);
    e->loudness = ebur128_init((unsigned)e->channels, (unsigned long)e->sample_rate, 3);
    return e->loudness != NULL;
}

struct CaptureSession {
    wil::com_ptr_nothrow<IMMDevice> device;
    wil::com_ptr_nothrow<IAudioClient> client;
    wil::com_ptr_nothrow<IAudioCaptureClient> capture;
    wil::unique_cotaskmem_ptr<WAVEFORMATEX> wave;
    wil::unique_handle packet_event;
    std::unique_ptr<float, decltype(&free)> converted{nullptr, free};
    size_t capacity = 0;
    int channels = 0;
    int sample_rate = 0;
    int bits = 0;
    bool is_float = false;
};

static const wchar_t *open_capture_device(AudioEngine *e, CaptureSession *session) {
    auto enumerator = wil::CoCreateInstanceNoThrow<IMMDeviceEnumerator>(__uuidof(MMDeviceEnumerator), CLSCTX_ALL);
    if (!enumerator || FAILED(enumerator->GetDevice(e->endpoint_id, session->device.put())) ||
        FAILED(session->device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, session->client.put_void())) ||
        FAILED(session->client->GetMixFormat(wil::out_param(session->wave))))
        return L"The playback device could not be opened.";
    return NULL;
}

static const wchar_t *configure_capture(CaptureSession *session) {
    if (!wave_format(session->wave.get(), &session->channels, &session->sample_rate, &session->bits, &session->is_float))
        return L"Only mono/stereo float or PCM 16/24/32-bit audio is supported.";

    session->packet_event.reset(CreateEventW(NULL, FALSE, FALSE, NULL));
    if (!session->packet_event) return L"Audio event creation failed.";

    HRESULT hr = session->client->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
        1000000,
        0,
        session->wave.get(),
        NULL
    );
    if (FAILED(hr) || FAILED(session->client->SetEventHandle(session->packet_event.get())) ||
        FAILED(session->client->GetService(__uuidof(IAudioCaptureClient), session->capture.put_void())))
        return L"WASAPI loopback capture could not start.";

    UINT32 buffer_frames = 0;
    if (FAILED(session->client->GetBufferSize(&buffer_frames)) || !buffer_frames) return L"WASAPI buffer size could not be read.";
    session->capacity = (size_t)buffer_frames * session->channels;
    session->converted.reset((float *)calloc(session->capacity, sizeof(float)));
    return session->converted ? NULL : L"Audio buffer allocation failed.";
}

static bool initialize_meter(AudioEngine *e, const CaptureSession *session) {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    auto lock = wil::AcquireSRWLockExclusive(&e->lock);
    e->channels = session->channels;
    e->sample_rate = session->sample_rate;
    e->started_at = now.QuadPart;
    e->last_packet = e->last_signal = 0;
    e->loudness_reset_for_silence = true;
    peak_init(&e->peak, e->frequency, e->hold_ms);
    if (!reset_loudness(e)) return false;
    device_friendly_name(session->device.get(), e->device_name, _countof(e->device_name));
    swprintf_s(
        e->format,
        _countof(e->format),
        L"%.1f kHz - %d-bit %s - %d ch",
        session->sample_rate / 1000.0,
        session->bits,
        session->is_float ? L"float" : L"PCM",
        session->channels
    );
    e->error[0] = 0;
    e->connected = true;
    return true;
}

static const wchar_t *process_packets(AudioEngine *e, CaptureSession *session) {
    DWORD mmcss_index = 0;
    LARGE_INTEGER now;
    unique_mmcss_handle mmcss(AvSetMmThreadCharacteristicsW(L"Audio", &mmcss_index));
    if (FAILED(session->client->Start())) return L"WASAPI loopback capture could not start.";
    auto stop_client = wil::scope_exit([session] { session->client->Stop(); });

    HANDLE waits[2] = {e->stop_event, session->packet_event.get()};
    for (;;) {
        DWORD wait = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
        if (wait == WAIT_OBJECT_0) break;
        if (wait != WAIT_OBJECT_0 + 1) return L"Audio capture wait failed.";

        UINT32 packet = 0;
        for (;;) {
            HRESULT next_result = session->capture->GetNextPacketSize(&packet);
            if (FAILED(next_result)) return L"The playback device disconnected or WASAPI capture failed.";
            if (!packet) break;

            BYTE *data = NULL;
            UINT32 frames = 0;
            DWORD flags = 0;
            if (FAILED(session->capture->GetBuffer(&data, &frames, &flags, NULL, NULL)))
                return L"The playback device disconnected or WASAPI capture failed.";
            auto release_packet = wil::scope_exit([session, frames] { session->capture->ReleaseBuffer(frames); });

            size_t samples = (size_t)frames * session->channels;
            const float *values = NULL;
            if (samples > session->capacity) return L"WASAPI returned an oversized packet.";
            if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
                ZeroMemory(session->converted.get(), samples * sizeof(float));
                values = session->converted.get();
            } else if (session->is_float) values = (const float *)data;
            else {
                if (!pcm_to_float(
                        data,
                        samples * (session->bits / 8),
                        (unsigned)session->bits,
                        session->converted.get(),
                        session->capacity
                    ))
                    return L"Audio sample conversion failed.";
                values = session->converted.get();
            }

            QueryPerformanceCounter(&now);
            bool reset_failed = false;
            {
                auto lock = wil::AcquireSRWLockExclusive(&e->lock);
                if (e->reset_requested) {
                    peak_reset(&e->peak);
                    e->loudness_reset_for_silence = true;
                    e->reset_requested = false;
                    reset_failed = !reset_loudness(e);
                }
                if (!reset_failed) {
                    float packet_peak = peak_update(&e->peak, values, samples, (unsigned)session->channels, now.QuadPart);
                    if (e->loudness) ebur128_add_frames_float((ebur128_state *)e->loudness, values, frames);
                    e->last_packet = now.QuadPart;
                    if (packet_peak > 0.000001f) {
                        e->last_signal = now.QuadPart;
                        e->loudness_reset_for_silence = false;
                    }
                }
            }
            if (reset_failed) return L"libebur128 reset failed.";
        }
    }
    return NULL;
}

static DWORD WINAPI capture_thread(void *parameter) {
    AudioEngine *e = (AudioEngine *)parameter;
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    auto com_cleanup = wil::scope_exit([hr] {
        if (SUCCEEDED(hr)) CoUninitialize();
    });
    auto engine_cleanup = wil::scope_exit([e] {
        auto lock = wil::AcquireSRWLockExclusive(&e->lock);
        e->connected = false;
        if (e->loudness) ebur128_destroy((ebur128_state **)&e->loudness);
    });
    if (FAILED(hr)) return capture_fail(e, L"COM initialization failed.");

    CaptureSession session;
    if (const wchar_t *error = open_capture_device(e, &session)) return capture_fail(e, error);
    if (const wchar_t *error = configure_capture(&session)) return capture_fail(e, error);
    if (!initialize_meter(e, &session)) return capture_fail(e, L"libebur128 initialization failed.");
    if (const wchar_t *error = process_packets(e, &session)) return capture_fail(e, error);
    return 0;
}

bool audio_start(AudioEngine *e, const wchar_t *endpoint) {
    audio_stop(e);
    {
        auto lock = wil::AcquireSRWLockExclusive(&e->lock);
        e->device_name[0] = e->format[0] = e->error[0] = 0;
        e->connected = false;
    }
    wcsncpy_s(e->endpoint_id, _countof(e->endpoint_id), endpoint, _TRUNCATE);
    e->stop_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!e->stop_event) {
        set_error(e, L"Audio stop event creation failed.");
        return false;
    }
    e->thread = CreateThread(NULL, 0, capture_thread, e, 0, NULL);
    if (!e->thread) {
        CloseHandle(e->stop_event);
        e->stop_event = NULL;
        set_error(e, L"Audio capture thread creation failed.");
        return false;
    }
    return true;
}

void audio_stop(AudioEngine *e) {
    if (e->stop_event) SetEvent(e->stop_event);
    if (e->thread) {
        WaitForSingleObject(e->thread, INFINITE);
        CloseHandle(e->thread);
    }
    if (e->stop_event) CloseHandle(e->stop_event);
    e->thread = e->stop_event = NULL;
    auto lock = wil::AcquireSRWLockExclusive(&e->lock);
    e->connected = false;
}

void audio_dispose(AudioEngine *e) {
    audio_stop(e);
}
void audio_reset(AudioEngine *e) {
    auto lock = wil::AcquireSRWLockExclusive(&e->lock);
    e->reset_requested = true;
}

MeterSnapshot audio_snapshot(AudioEngine *e) {
    MeterSnapshot s = {-HUGE_VAL, -HUGE_VAL, -HUGE_VAL, -HUGE_VAL, false, false, false, false};
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    auto lock = wil::AcquireSRWLockExclusive(&e->lock);
    s.connected = e->connected;
    if (e->connected && e->loudness) {
        int64_t signal_at = e->last_signal ? e->last_signal : e->started_at;
        int64_t silence_ticks = now.QuadPart - signal_at;
        bool silent = silence_ticks >= e->frequency * 5;
        s.hide_values = silence_ticks >= e->frequency * 10;
        PeakReading p = peak_read(&e->peak, now.QuadPart);
        if (silent) {
            peak_reset(&e->peak);
            if (!e->loudness_reset_for_silence) {
                e->reset_requested = true;
                e->loudness_reset_for_silence = true;
            }
        } else {
            s.peak_db = linear_to_db(p.left > p.right ? p.left : p.right);
            s.hold_db = linear_to_db(p.hold);
            s.clipping = p.clipping;
            double m = -HUGE_VAL, st = -HUGE_VAL; /* reads are cheap and serialized with frame submission */
            if (!ebur128_loudness_momentary((ebur128_state *)e->loudness, &m) && isfinite(m)) s.momentary = m;
            if (!ebur128_loudness_shortterm((ebur128_state *)e->loudness, &st) && isfinite(st)) s.short_term = st;
            s.recent_audio = e->last_packet && now.QuadPart - e->last_packet <= e->frequency * 3 / 4;
        }
    }
    return s;
}

void audio_status(AudioEngine *e, wchar_t *device, int dc, wchar_t *format, int fc, wchar_t *error, int ec) {
    auto lock = wil::AcquireSRWLockShared(&e->lock);
    wcsncpy_s(device, dc, e->device_name, _TRUNCATE);
    wcsncpy_s(format, fc, e->format, _TRUNCATE);
    wcsncpy_s(error, ec, e->error, _TRUNCATE);
}
