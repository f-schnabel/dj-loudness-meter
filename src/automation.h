#ifndef DJLM_AUTOMATION_H
#define DJLM_AUTOMATION_H

#include <stdbool.h>
#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

bool automation_widget_right(HWND taskbar, int *right);

#ifdef __cplusplus
}
#endif

#endif
