#include "autostart.h"
#include <stdlib.h>
#include <windows.h>
#include <wchar.h>

static const wchar_t run_key[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const wchar_t value_name[] = L"DjLoudnessMeter";

static bool command_line(wchar_t *command, DWORD count) {
    if (count < 3) return false;
    command[0] = L'"';
    DWORD length = GetModuleFileNameW(NULL, command + 1, count - 2);
    if (!length || length >= count - 2) return false;
    command[length + 1] = L'"';
    command[length + 2] = 0;
    return true;
}

bool autostart_is_enabled(void) {
    HKEY key = NULL;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, run_key, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) return false;
    wchar_t registered[32768];
    DWORD type = 0, bytes = sizeof(registered);
    LSTATUS status = RegQueryValueExW(key, value_name, NULL, &type, (BYTE *)registered, &bytes);
    RegCloseKey(key);
    if (status != ERROR_SUCCESS || type != REG_SZ || bytes < sizeof(wchar_t)) return false;
    registered[_countof(registered) - 1] = 0;
    wchar_t expected[32768];
    return command_line(expected, _countof(expected)) && _wcsicmp(registered, expected) == 0;
}

bool autostart_set_enabled(bool enabled) {
    HKEY key = NULL;
    if (enabled) {
        if (RegCreateKeyExW(HKEY_CURRENT_USER, run_key, 0, NULL, 0, KEY_SET_VALUE, NULL, &key, NULL) != ERROR_SUCCESS) return false;
        wchar_t command[32768];
        bool valid = command_line(command, _countof(command));
        LSTATUS status =
            valid ? RegSetValueExW(key, value_name, 0, REG_SZ, (const BYTE *)command, (DWORD)((wcslen(command) + 1) * sizeof(wchar_t)))
                  : ERROR_BAD_PATHNAME;
        RegCloseKey(key);
        return status == ERROR_SUCCESS;
    }
    LSTATUS status = RegOpenKeyExW(HKEY_CURRENT_USER, run_key, 0, KEY_SET_VALUE, &key);
    if (status == ERROR_FILE_NOT_FOUND) return true;
    if (status != ERROR_SUCCESS) return false;
    status = RegDeleteValueW(key, value_name);
    RegCloseKey(key);
    return status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND;
}
