#include "overlay.h"
#include "taskbar.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static const COLORREF primary = RGB(243, 245, 246), secondary = RGB(143, 154, 163);
static const COLORREF warning = RGB(246, 195, 68), critical = RGB(255, 90, 95);

bool overlay_init(App *app) {
    app->overlay_brush = CreateSolidBrush(DJLM_COLOR_KEY);
    app->separator_pen = CreatePen(PS_SOLID, 1, RGB(60, 70, 78));
    return app->overlay_brush && app->separator_pen;
}

void overlay_dispose(App *app) {
    if (app->overlay_buffer_bitmap) {
        SelectObject(app->overlay_buffer_dc, app->overlay_buffer_original);
        DeleteObject(app->overlay_buffer_bitmap);
    }
    if (app->overlay_buffer_dc) DeleteDC(app->overlay_buffer_dc);
    if (app->separator_pen) DeleteObject(app->separator_pen);
    if (app->overlay_brush) DeleteObject(app->overlay_brush);
}

static int overlay_width(App *app) {
    bool loudness_visible = app->settings.show_loudness && !app->meter_snapshot.hide_values;
    int width = loudness_visible ? 255 : 0;
    if (app->settings.show_system) width += 92 + (app->system_snapshot.has_temperature ? 46 : 0);
    if (loudness_visible && app->settings.show_system) width += 13;
    return width;
}

void overlay_position(App *app) {
    if (!app->monitor_count) return;
    DisplayInfo *m = &app->monitors[app->selected_monitor];
    int bottom = m->bounds.bottom - m->work.bottom, top = m->work.top - m->bounds.top;
    int height = bottom > 0 ? bottom : top > 0 ? top : 42;
    if (height < 32) height = 32;
    int y = bottom > 0 ? m->work.bottom : top > 0 ? m->bounds.top : m->bounds.bottom - height;
    int width = overlay_width(app);
    int x;
    if (app->settings.right_aligned) {
        int safe_right;
        if (!taskbar_safe_right(m, &safe_right)) {
            int reserve = m->primary ? 240 : 120;
            safe_right = m->bounds.right - reserve;
        }
        x = safe_right - width;
        if (x < m->bounds.left) x = m->bounds.left;
    } else {
        int safe_left;
        x = taskbar_safe_left(m, &safe_left) ? safe_left : m->bounds.left;
    }
    if (app->hit_window) SetWindowPos(app->hit_window, HWND_TOPMOST, x, y, width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    SetWindowPos(app->overlay_window, HWND_TOPMOST, x, y, width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

static void format_reading(double value, wchar_t *buffer, size_t count) {
    if (!display_should_show(value)) wcscpy_s(buffer, count, L"\x2212\x221e");
    else swprintf_s(buffer, count, value > 0.05 ? L"+%.1f" : L"%.1f", value);
}

static COLORREF threshold_color(double value, double amber, double red) {
    return !isfinite(value) ? primary : value >= red ? critical : value >= amber ? warning : primary;
}

static void draw_cell(HDC dc, App *app, RECT rect, const wchar_t *label, double raw, double warn, double red) {
    wchar_t value[32];
    double adjusted = display_adjust(raw, app->settings.display_zero);
    format_reading(adjusted, value, _countof(value));
    rect.right -= 5;
    int midpoint = rect.top + (rect.bottom - rect.top) / 2;
    SetTextColor(dc, secondary);
    SelectObject(dc, app->small_font);
    RECT top = rect;
    top.bottom = midpoint - 4;
    DrawTextW(dc, label, -1, &top, DT_RIGHT | DT_BOTTOM | DT_SINGLELINE | DT_NOPREFIX);
    SetTextColor(
        dc,
        threshold_color(adjusted, display_adjust(warn, app->settings.display_zero), display_adjust(red, app->settings.display_zero))
    );
    SelectObject(dc, app->value_font);
    RECT bottom = rect;
    bottom.top = midpoint - 4;
    DrawTextW(dc, value, -1, &bottom, DT_RIGHT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
}

static void draw_system_cell(HDC dc, App *app, RECT rect, const wchar_t *label, double value, bool valid, const wchar_t *suffix) {
    wchar_t text[32];
    if (valid) swprintf_s(text, _countof(text), L"%.0f%s", value, suffix);
    else wcscpy_s(text, _countof(text), L"N/A");
    int midpoint = rect.top + (rect.bottom - rect.top) / 2;
    SetTextColor(dc, secondary);
    SelectObject(dc, app->small_font);
    RECT top = rect;
    top.bottom = midpoint - 4;
    DrawTextW(dc, label, -1, &top, DT_CENTER | DT_BOTTOM | DT_SINGLELINE);
    SetTextColor(dc, primary);
    SelectObject(dc, app->system_value_font);
    RECT bottom = rect;
    bottom.top = midpoint - 4;
    DrawTextW(dc, text, -1, &bottom, DT_CENTER | DT_TOP | DT_SINGLELINE);
}

static void paint_overlay(App *app, HDC dc, RECT bounds) {
    FillRect(dc, &bounds, app->overlay_brush);
    SetBkMode(dc, TRANSPARENT);
    bool loudness_visible = app->settings.show_loudness && !app->meter_snapshot.hide_values;
    int x = 0, height = bounds.bottom;
    if (loudness_visible) {
        int cell = 255 / 4;
        wchar_t peak_label[32];
        if (app->settings.refresh_ms < 1000) swprintf_s(peak_label, _countof(peak_label), L"P \x2009(%dms)", app->settings.refresh_ms);
        else swprintf_s(peak_label, _countof(peak_label), L"P \x2009(%.2gs)", app->settings.refresh_ms / 1000.0);
        RECT r = {x, 0, x + cell, height};
        draw_cell(dc, app, r, peak_label, app->meter_snapshot.peak_db, -6, -1);
        x += cell;
        r = (RECT){x, 0, x + cell, height};
        draw_cell(dc, app, r, L"P \x2009(5s)", app->meter_snapshot.hold_db, -6, -1);
        x += cell;
        r = (RECT){x, 0, x + cell, height};
        draw_cell(dc, app, r, L"LUFS \x2009(0.4s)", app->meter_snapshot.momentary, -12, -9);
        x += cell;
        r = (RECT){x, 0, x + 255 - cell * 3, height};
        draw_cell(dc, app, r, L"LUFS \x2009(3s)", app->meter_snapshot.short_term, -12, -9);
        x += 255 - cell * 3;
    }
    if (loudness_visible && app->settings.show_system) {
        if (app->separator_pen) {
            HGDIOBJ previous = SelectObject(dc, app->separator_pen);
            MoveToEx(dc, x + 6, 10, NULL);
            LineTo(dc, x + 6, height - 10);
            SelectObject(dc, previous);
        }
        x += 13;
    }
    if (app->settings.show_system) {
        if (app->system_snapshot.has_temperature) {
            RECT r = {x, 0, x + 46, height};
            draw_system_cell(dc, app, r, L"Temp", app->system_snapshot.temperature, true, L"\x00b0");
            x += 46;
        }
        RECT r = {x, 0, x + 46, height};
        draw_system_cell(dc, app, r, L"CPU", app->system_snapshot.cpu, app->system_snapshot.has_cpu, L"%");
        x += 46;
        r = (RECT){x, 0, x + 46, height};
        draw_system_cell(dc, app, r, L"RAM", app->system_snapshot.memory, app->system_snapshot.has_memory, L"%");
    }
}

static bool ensure_overlay_buffer(App *app, HDC target, int width, int height) {
    if (width < 1 || height < 1) return false;
    if (!app->overlay_buffer_dc) app->overlay_buffer_dc = CreateCompatibleDC(target);
    if (!app->overlay_buffer_dc) return false;
    if (app->overlay_buffer_bitmap && app->overlay_buffer_width == width && app->overlay_buffer_height == height) return true;
    HBITMAP bitmap = CreateCompatibleBitmap(target, width, height);
    if (!bitmap) return false;
    if (app->overlay_buffer_bitmap) {
        SelectObject(app->overlay_buffer_dc, app->overlay_buffer_original);
        DeleteObject(app->overlay_buffer_bitmap);
    }
    app->overlay_buffer_original = SelectObject(app->overlay_buffer_dc, bitmap);
    app->overlay_buffer_bitmap = bitmap;
    app->overlay_buffer_width = width;
    app->overlay_buffer_height = height;
    return true;
}

void overlay_render(App *app, HDC target, RECT bounds) {
    int width = bounds.right - bounds.left, height = bounds.bottom - bounds.top;
    if (ensure_overlay_buffer(app, target, width, height)) {
        paint_overlay(app, app->overlay_buffer_dc, bounds);
        BitBlt(target, 0, 0, width, height, app->overlay_buffer_dc, 0, 0, SRCCOPY);
    } else paint_overlay(app, target, bounds);
}
