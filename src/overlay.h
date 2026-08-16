#ifndef DJLM_OVERLAY_H
#define DJLM_OVERLAY_H

#include "app.h"

#define DJLM_COLOR_KEY RGB(1, 2, 3)

bool overlay_init(App *app);
void overlay_dispose(App *app);
void overlay_position(App *app);
void overlay_render(App *app, HDC target, RECT bounds);

#endif
