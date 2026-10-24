// Exercise the actual callback and watchdog without loading Voicemeeter or touching audio devices.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "../src/audio.cpp" // NOLINT(bugprone-suspicious-include): test private callback logic without exposing a public API.
#include <assert.h>
#include <crtdbg.h>
#include <stdio.h>

static void initialize_test_stream(AudioEngine *engine) {
    audio_init(engine, 5000);
    engine->voicemeeter_offset = 6;
    VoicemeeterAudioInfo info = {48000, 4};
    voicemeeter_callback(engine, 1, &info, 0);
    assert(engine->connected && engine->loudness);
}

static void dispose_test_stream(AudioEngine *engine) {
    if (engine->loudness) ebur128_destroy((ebur128_state **)&engine->loudness);
    audio_dispose(engine);
}

static void test_start_without_buffers(void) {
    AudioEngine engine;
    audio_init(&engine, 5000);
    int64_t registered_at = engine.frequency * 100;
    assert(!voicemeeter_stream_error(&engine, registered_at, registered_at + engine.frequency * 4));
    assert(voicemeeter_stream_error(&engine, registered_at, registered_at + engine.frequency * 5));

    // STARTING alone does not prove that the audio callback is delivering buffers.
    initialize_test_stream(&engine);
    registered_at = engine.started_at;
    assert(voicemeeter_stream_error(&engine, registered_at, registered_at + engine.frequency * 5));
    dispose_test_stream(&engine);
}

static void test_silence_and_stall(void) {
    AudioEngine engine;
    initialize_test_stream(&engine);
    float input[8][4] = {}, output[8][4] = {};
    VoicemeeterAudioBuffer buffer = {};
    buffer.sample_rate = 48000;
    buffer.samples = 4;
    buffer.inputs = buffer.outputs = 8;
    for (int channel = 0; channel < 8; ++channel) {
        buffer.read[channel] = input[channel];
        buffer.write[channel] = output[channel];
        output[channel][0] = 1.0f;
    }
    voicemeeter_callback(&engine, 10, &buffer, 0);
    assert(engine.last_packet && !engine.last_signal);
    for (int channel = 0; channel < 8; ++channel)
        assert(output[channel][0] == 0.0f);
    int64_t registered_at = engine.last_packet - engine.frequency * 60;
    assert(!voicemeeter_stream_error(&engine, registered_at, engine.last_packet + engine.frequency * 4));
    assert(voicemeeter_stream_error(&engine, registered_at, engine.last_packet + engine.frequency * 5));

    input[6][0] = 0.25f;
    voicemeeter_callback(&engine, 10, &buffer, 0);
    assert(engine.last_signal && engine.peak.latest == 0.25f);
    assert(output[6][0] == 0.25f);

    // Keep passing audio through while a changed engine waits for worker-side recovery.
    buffer.sample_rate = 44100;
    input[0][0] = 0.5f;
    voicemeeter_callback(&engine, 10, &buffer, 0);
    assert(output[0][0] == 0.5f);
    assert(voicemeeter_stream_error(&engine, registered_at, engine.last_packet));
    dispose_test_stream(&engine);
}

static void test_engine_notifications(void) {
    for (long command = 2; command <= 3; ++command) {
        AudioEngine engine;
        initialize_test_stream(&engine);
        voicemeeter_callback(&engine, command, NULL, 0);
        assert(!engine.connected);
        assert(voicemeeter_stream_error(&engine, engine.started_at, engine.started_at));
        dispose_test_stream(&engine);
    }
}

int main(void) {
    _set_error_mode(_OUT_TO_STDERR);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    test_start_without_buffers();
    test_silence_and_stall();
    test_engine_notifications();
    puts("Voicemeeter tests passed");
    return 0;
}
