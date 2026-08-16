#include "app.h"
#include <objbase.h>
#include <shellscalingapi.h>
#include <wil/resource.h>

static BOOL WINAPI console_handler(DWORD control) {
    if (control != CTRL_C_EVENT && control != CTRL_BREAK_EVENT) return FALSE;
    HWND overlay = FindWindowW(L"DjLoudnessMeterOverlay", NULL);
    if (overlay) PostMessageW(overlay, WM_CLOSE, 0, 0);
    return overlay != NULL;
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR command_line, int show_command) {
    (void)previous; (void)command_line;
    wil::unique_handle singleton(CreateMutexW(NULL, FALSE, L"Local\\DjLoudnessMeter.Singleton"));
    if (!singleton || GetLastError() == ERROR_ALREADY_EXISTS) return 0;

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    AttachConsole(ATTACH_PARENT_PROCESS);
    SetConsoleCtrlHandler(console_handler, TRUE);
    auto console_cleanup = wil::scope_exit([] { SetConsoleCtrlHandler(console_handler, FALSE); });

    HRESULT result = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    auto com_cleanup = wil::scope_exit([result] { if (SUCCEEDED(result)) CoUninitialize(); });
    return app_run(instance, show_command);
}
