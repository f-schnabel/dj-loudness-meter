#ifndef DJLM_SETTINGS_WINDOW_H
#define DJLM_SETTINGS_WINDOW_H

#include "app.h"

LRESULT CALLBACK settings_window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
void settings_window_apply_zero(App *app);
void settings_window_dispose(App *app);
void settings_window_show(App *app);

#endif
