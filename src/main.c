#include "app.h"
#include <objbase.h>
#include <shellscalingapi.h>

static BOOL WINAPI console_handler(DWORD control) {
    if (control != CTRL_C_EVENT && control != CTRL_BREAK_EVENT) return FALSE;
    HWND overlay = FindWindowW(L"DjLoudnessMeterOverlay", NULL);
    if (overlay) PostMessageW(overlay, WM_CLOSE, 0, 0);
    return overlay != NULL;
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR command_line, int show_command) {
    (void)previous; (void)command_line;
    HANDLE singleton = CreateMutexW(NULL, FALSE, L"Local\\DjLoudnessMeter.Singleton");
    if (!singleton || GetLastError() == ERROR_ALREADY_EXISTS) { if (singleton) CloseHandle(singleton); return 0; }
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    AttachConsole(ATTACH_PARENT_PROCESS); SetConsoleCtrlHandler(console_handler, TRUE);
    HRESULT result = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    int exit_code = app_run(instance, show_command);
    if (SUCCEEDED(result)) CoUninitialize();
    SetConsoleCtrlHandler(console_handler, FALSE);
    CloseHandle(singleton);
    return exit_code;
}
