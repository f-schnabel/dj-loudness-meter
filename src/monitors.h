#ifndef DJLM_MONITORS_H
#define DJLM_MONITORS_H

#include "settings.h"

#define DJLM_MAX_MONITORS 16

typedef struct {
    wchar_t device[32], identity[128], label[64];
    RECT bounds, work;
    bool primary;
} DisplayInfo;

int monitors_enumerate(DisplayInfo *monitors, int capacity);
int monitors_select(const DisplayInfo *monitors, int count, const AppSettings *settings);
void monitors_remember(AppSettings *settings, const DisplayInfo *monitor);

#endif
