#ifndef DJLM_TASKBAR_H
#define DJLM_TASKBAR_H

#include "app.h"

bool taskbar_safe_left(const DisplayInfo *monitor, int *left);
bool taskbar_safe_right(const DisplayInfo *monitor, int *right);

#endif
