#include "ui.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "ui_theme.h"

#define PI 3.14159265358979323846f

typedef struct { int x, y, w, h; } box_t;
typedef struct { uint16_t *pixels; int top, bottom; } canvas_t;
typedef struct {
    uint16_t code;
    uint32_t offset;
    uint8_t width, height;
    int8_t left, top;
    uint8_t advance;
} glyph_t;
typedef struct {
    const uint8_t *pixels;
    const glyph_t *glyphs;
    size_t count;
    int cap_height;
} font_t;

#include "ui_glyphs.h"

static int min_int(int a, int b) { return a < b ? a : b; }
static int max_int(int a, int b) { return a > b ? a : b; }

static bool visible(const canvas_t *c, box_t b)
{
    return b.y < c->bottom && b.y + b.h > c->top;
}

static uint16_t blend(uint16_t a, uint16_t b, unsigned weight, unsigned max_weight)
{
    const unsigned base = max_weight - weight;
    const unsigned red = ((a >> 11) * base + (b >> 11) * weight) / max_weight;
    const unsigned green = (((a >> 5) & 63) * base + ((b >> 5) & 63) * weight) / max_weight;
    const unsigned blue = ((a & 31) * base + (b & 31) * weight) / max_weight;
    return (uint16_t)((red << 11) | (green << 5) | blue);
}

static void span(canvas_t *c, int x, int end, int y, uint16_t color)
{
    if (y < c->top || y >= c->bottom) {
        return;
    }
    uint16_t *row = c->pixels + (y - c->top) * UI_WIDTH;
    end = min_int(end, UI_WIDTH);
    for (int i = max_int(x, 0); i < end; ++i) {
        row[i] = color;
    }
}

static void rectangle(canvas_t *c, box_t b, uint16_t color)
{
    const int stop = min_int(b.y + b.h, c->bottom);
    for (int y = max_int(b.y, c->top); y < stop; ++y) {
        span(c, b.x, b.x + b.w, y, color);
    }
}

static void rounded(canvas_t *c, box_t b, int radius, uint16_t top, uint16_t bottom)
{
    const int stop = min_int(b.y + b.h, c->bottom);
    radius = min_int(radius, min_int(b.w, b.h) / 2);
    for (int y = max_int(b.y, c->top); y < stop; ++y) {
        const int fy = y - b.y;
        const int edge = min_int(fy, b.h - 1 - fy);
        const int d = max_int(0, radius - 1 - edge);
        const int inset = d ? radius - (int)sqrtf((float)(radius * radius - d * d)) : 0;
        const uint16_t color = top == bottom ? top : blend(top, bottom, fy, b.h - 1);
        span(c, b.x + inset, b.x + b.w - inset, y, color);
    }
}

static void panel(canvas_t *c, box_t b, int radius, uint16_t edge, uint16_t top, uint16_t bottom)
{
    rounded(c, b, radius, edge, blend(edge, 0, 2, 5));
    rounded(c, (box_t){b.x + 2, b.y + 2, b.w - 4, b.h - 4}, radius - 2, top, bottom);
}

static void line(canvas_t *c, int x0, int y0, int x1, int y1, int width, uint16_t color)
{
    if (min_int(y0, y1) - width >= c->bottom || max_int(y0, y1) + width < c->top) {
        return;
    }
    const int dx = abs(x1 - x0), dy = -abs(y1 - y0);
    const int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    for (;;) {
        rectangle(c, (box_t){x0 - width / 2, y0 - width / 2, width, width}, color);
        if (x0 == x1 && y0 == y1) {
            return;
        }
        const int twice = 2 * error;
        if (twice >= dy) { error += dy; x0 += sx; }
        if (twice <= dx) { error += dx; y0 += sy; }
    }
}

static const glyph_t *next_glyph(const font_t *font, const char **p)
{
    const unsigned char *s = (const unsigned char *)*p;
    unsigned code = *s++;
    if ((code & 0xe0) == 0xc0 && (s[0] & 0xc0) == 0x80) {
        code = ((code & 31) << 6) | (s[0] & 63);
        s += 1;
    } else if ((code & 0xf0) == 0xe0 && (s[0] & 0xc0) == 0x80 && (s[1] & 0xc0) == 0x80) {
        code = ((code & 15) << 12) | ((s[0] & 63) << 6) | (s[1] & 63);
        s += 2;
    }
    *p = (const char *)s;
    for (size_t i = 0; i < font->count; ++i) {
        if (font->glyphs[i].code == code) {
            return &font->glyphs[i];
        }
    }
    return NULL;
}

static int text_width(const font_t *font, const char *s)
{
    int width = 0;
    while (*s) {
        const glyph_t *g = next_glyph(font, &s);
        if (g) {
            width += g->advance;
        }
    }
    return width;
}

/* y is the center of the capital-letter height. align is left, center, or right. */
static void text(canvas_t *c, const font_t *font, int x, int y, int align,
                 uint16_t color, const char *s)
{
    if (y + font->cap_height < c->top || y - font->cap_height >= c->bottom) {
        return;
    }
    if (align) {
        x -= text_width(font, s) / (align == 1 ? 2 : 1);
    }
    const int baseline = y + font->cap_height / 2;
    while (*s) {
        const glyph_t *g = next_glyph(font, &s);
        if (!g) {
            continue;
        }
        const int left = x + g->left;
        const int top = baseline + g->top;
        for (int row = max_int(top, c->top); row < min_int(top + g->height, c->bottom); ++row) {
            uint16_t *dst = c->pixels + (row - c->top) * UI_WIDTH;
            for (int col = max_int(left, 0); col < min_int(left + g->width, UI_WIDTH); ++col) {
                const size_t pos = (row - top) * g->width + col - left;
                const unsigned alpha = (font->pixels[g->offset + pos / 2] >> (4 * (pos % 2))) & 15;
                if (alpha) {
                    dst[col] = alpha == 15 ? color : blend(dst[col], color, alpha, 15);
                }
            }
        }
        x += g->advance;
    }
}

static const char *const mode_names[] = {"数显", "指针", "曲线", "指针/曲线"};
static const char *const reading_names[] = {"电压", "电流", "功率"};
static const char *const units[] = {"V", "A", "W"};
static const uint16_t reading_colors[] = {GREEN, RED, BLUE};
static const uint16_t reading_fills[] = {VOLTAGE_FILL, CURRENT_FILL, POWER_FILL};

static void pulse(canvas_t *c, int x, int y)
{
    const int points[][2] = {{0,0}, {7,0}, {10,-5}, {14,6}, {19,-10}, {24,7}, {28,0}, {38,0}};
    for (size_t i = 1; i < sizeof(points) / sizeof(points[0]); ++i) {
        line(c, x + points[i-1][0], y + points[i-1][1],
             x + points[i][0], y + points[i][1], 2, GREEN);
    }
}

static void header(canvas_t *c, int mode, reading_t reading)
{
    if (!visible(c, (box_t){0,0,UI_WIDTH,64})) {
        return;
    }
    rectangle(c, (box_t){0,0,UI_WIDTH,61}, RGB565(2,9,13));
    pulse(c, 12, 29);
    text(c, &title_font, 60, 28, 0, INK, "电流监测");
    line(c, 181, 60, 215, 0, 3, CYAN);
    const int starts[] = {221, 313, 405, 497};
    const int widths[] = {84, 84, 84, 137};
    for (int i = 0; i < UI_MODE_COUNT; ++i) {
        const bool active = mode == i;
        panel(c, (box_t){starts[i], 9, widths[i], 43}, 10, active ? CYAN : GRID,
              active ? RGB565(0,148,255) : PANEL, active ? RGB565(0,50,210) : RGB565(1,8,12));
        text(c, &label_font, starts[i] + widths[i]/2, 31, 1, INK, mode_names[i]);
    }
    panel(c, (box_t){644, 8, 199, 44}, 10, CYAN, PANEL, 0);
    text(c, &label_font, 743, 22, 1, INK, "DC 80V / 16A IN");
    text(c, &axis_font, 743, 41, 1, CYAN, reading.demo ? "DEMO" : reading.valid ? "LIVE" : "NO DATA");
    rectangle(c, (box_t){0,58,UI_WIDTH,3}, CYAN);
}

void ui_push(ui_state_t *state, reading_t reading)
{
    if (!isfinite(reading.voltage) || !isfinite(reading.current) ||
        !isfinite(reading.voltage * reading.current)) {
        reading.valid = false;
    }
    state->latest = reading;
    state->history[state->write_index] = reading;
    state->write_index = (state->write_index + 1) % UI_HISTORY;
    if (state->count < UI_HISTORY) {
        ++state->count;
    }
}

static float fraction(float value, float max)
{
    return fminf(1.f, fmaxf(0.f, value / max));
}

static void reading_text(char *buf, size_t size, float value, bool valid)
{
    if (!valid) {
        snprintf(buf, size, "--");
    } else if (fabsf(value) >= 10000.f) {
        snprintf(buf, size, "O.L");
    } else {
        snprintf(buf, size, "%.4f", (double)value);
    }
}

static void meter(canvas_t *c, box_t b, reading_t r, int i, bool large)
{
    if (!visible(c, b)) {
        return;
    }
    const float values[] = {r.voltage, r.current, r.voltage * r.current};
    const uint16_t accent = reading_colors[i];
    panel(c, b, large ? 23 : 12, accent, reading_fills[i], blend(reading_fills[i],0,1,3));
    char value[20];
    reading_text(value, sizeof(value), values[i], r.valid);
    if (large) {
        const int center_y = b.y + b.h / 2;
        panel(c, (box_t){b.x + 30, center_y - 29, 67, 58}, 13, INK, reading_fills[i], reading_fills[i]);
        text(c, &meter_font, b.x + 64, center_y, 1, INK, units[i]);
        text(c, &title_font, b.x + 125, center_y, 0, INK, reading_names[i]);
        rectangle(c, (box_t){b.x + 237, center_y - 39, 2, 78}, INK);
        const font_t *font = text_width(&large_font, value) < b.w - 345 ? &large_font : &meter_font;
        text(c, font, b.x + b.w - 108, center_y, 2, INK, value);
        text(c, &meter_font, b.x + b.w - 60, center_y, 1, INK, units[i]);
    } else {
        const font_t *font = b.w >= 248 ? &meter_font : &title_font;
        const bool compact = b.h < 58;
        const int label_y = b.y + (compact ? 10 : 18);
        const int value_y = b.y + (compact ? 31 : 51);
        text(c, &axis_font, b.x + b.w - 14, value_y, 2, INK, units[i]);
        text(c, font, b.x + b.w - 35, value_y, 2, INK, value);
        text(c, compact ? &axis_font : &label_font, b.x + 14, label_y, 0, INK, reading_names[i]);
    }
}

static void meters_bottom(canvas_t *c, reading_t r, int y, int h)
{
    for (int i = 0; i < 3; ++i) {
        meter(c, (box_t){12 + 278 * i, y, 266, h}, r, i, false);
    }
}

static const float ranges[] = {.25f, 2.5f, 5.f, 10.f, 16.f};
static const char *const range_names[] = {"250 mA", "2.5 A", "5 A", "10 A", "16 A"};

static int select_range(float current)
{
    for (int i = 0; i < 4; ++i) {
        if (fabsf(current) <= ranges[i]) {
            return i;
        }
    }
    return 4;
}

static void range_buttons(canvas_t *c, box_t b, int selected)
{
    text(c, &label_font, b.x + b.w / 2, b.y + 12, 1, INK, "量程");
    const int step = (b.h - 31) / 5;
    const int height = min_int(39, step - 4);
    for (int i = 0; i < 5; ++i) {
        const bool active = i == selected;
        const box_t button = {b.x, b.y + 30 + i * step, b.w, height};
        panel(c, button, 8, active ? CYAN : GRID,
              active ? RGB565(0,130,248) : PANEL, active ? BLUE : 0);
        text(c, &axis_font, b.x + b.w/2, button.y + height/2, 1, INK, range_names[i]);
    }
}

static void dial(canvas_t *c, box_t b, reading_t reading, int range)
{
    if (!visible(c, b)) {
        return;
    }
    const int x = b.x + b.w / 2, y = b.y + b.h - 36;
    const int rx = b.w / 2 - 32, ry = b.h - 64;
    int lx = x - rx, ly = y;
    for (int i = 1; i <= 100; ++i) {
        const float angle = PI * (1.f - i / 100.f);
        const int px = x + (int)(cosf(angle) * rx), py = y - (int)(sinf(angle) * ry);
        const uint16_t color = i <= 60 ? GREEN : i <= 78 ? RGB565(255,223,18) : RED;
        line(c, lx, ly, px, py, 7, color);
        lx = px;
        ly = py;
    }
    const int steps = range == 4 ? 8 : 10;
    for (int i = 0; i <= steps * 4; ++i) {
        const float angle = PI * (1.f - i / (float)(steps * 4));
        const float co = cosf(angle), si = sinf(angle);
        const bool major = i % 4 == 0;
        const int px = x + (int)(co * (rx - 5)), py = y - (int)(si * (ry - 5));
        line(c, px, py, x + (int)(co * (rx - (major ? 17 : 10))),
             y - (int)(si * (ry - (major ? 17 : 10))), major ? 2 : 1, INK);
        if (major && (b.w >= 250 || i % (steps * 2) == 0)) {
            char label[12];
            const float scale = range == 0 ? 250.f : ranges[range];
            snprintf(label, sizeof(label), "%.3g", (double)(scale * i / (steps * 4)));
            text(c, &axis_font, x + (int)(co * (rx + 16)), y - (int)(si * (ry + 18)),
                 1, INK, label);
        }
    }
    text(c, &title_font, x, y - ry / 2 - 2, 1, INK, range == 0 ? "mA" : "A");
    if (reading.valid) {
        const float angle = PI * (1.f - fraction(reading.current, ranges[range]));
        const int end_x = x + (int)(cosf(angle) * (rx - 14));
        const int end_y = y - (int)(sinf(angle) * (ry - 14));
        line(c, x, y, end_x, end_y, 5, RED);
        line(c, x, y - 1, end_x, end_y - 1, 1, RGB565(255,187,163));
    }
    panel(c, (box_t){x-13,y-13,26,26}, 13, INK, RGB565(191,208,221), RGB565(66,89,109));
    char value[24];
    reading_text(value, sizeof(value) - 2, reading.current, reading.valid);
    const size_t end = text_width(&label_font, value);
    text(c, &label_font, x - 8, y + 23, 1, CYAN, value);
    text(c, &axis_font, x + (int)end / 2 + 2, y + 23, 0, INK, "A");
}

static void chart(canvas_t *c, const ui_state_t *s, box_t b)
{
    if (!visible(c, b)) {
        return;
    }
    const bool compact = b.h < 220;
    const int left = b.x + 57, right = b.x + b.w - 57;
    const int top = b.y + 30, bottom = b.y + b.h - 28;
    const int divisions = compact ? 4 : 8;
    int max_volts = 80;
    if (!compact) {
        float peak = 0.f;
        for (size_t i = 0; i < s->count; ++i) {
            if (s->history[i].valid) {
                peak = fmaxf(peak, s->history[i].voltage);
            }
        }
        max_volts = peak <= 20.f ? 20 : peak <= 40.f ? 40 : 80;
    }
    const uint16_t volts_color = compact ? CYAN : GREEN;
    panel(c, b, 15, CYAN, RGB565(3,12,20), RGB565(1,5,9));
    text(c, &axis_font, b.x + 15, b.y + 15, 0, volts_color, "V (V)");
    text(c, &axis_font, b.x + b.w - 15, b.y + 15, 2, RED, "I (A)");
    text(c, &axis_font, b.x + b.w / 2 - 62, b.y + 15, 1, volts_color, "电压");
    text(c, &axis_font, b.x + b.w / 2 + 62, b.y + 15, 1, RED, "电流");
    for (int i = 0; i <= 6; ++i) {
        const int x = left + (right - left) * i / 6;
        char value[8];
        snprintf(value, sizeof(value), "%d", i * 5);
        line(c, x, top, x, bottom, 1, GRID);
        text(c, &axis_font, x, bottom + 15, 1, INK, value);
    }
    for (int i = 0; i <= divisions; ++i) {
        const int y = top + (bottom - top) * i / divisions;
        char volts[16], amps[16];
        snprintf(volts, sizeof(volts), "%.1f", (double)max_volts * (divisions - i) / divisions);
        snprintf(amps, sizeof(amps), "%d", 16 - 16 * i / divisions);
        line(c, left, y, right, y, 1, GRID);
        text(c, &axis_font, left - 7, y, 2, volts_color, volts);
        text(c, &axis_font, right + 7, y, 0, RED, amps);
    }
    line(c, left, top, left, bottom, 2, volts_color);
    line(c, right, top, right, bottom, 2, RED);
    line(c, left, bottom, right, bottom, 2, INK);
    text(c, &axis_font, right + 16, bottom + 15, 0, INK, "S");
    const size_t oldest = (s->write_index + UI_HISTORY - s->count) % UI_HISTORY;
    for (size_t i = 1; i < s->count; ++i) {
        const reading_t a = s->history[(oldest + i - 1) % UI_HISTORY];
        const reading_t d = s->history[(oldest + i) % UI_HISTORY];
        if (a.valid && d.valid) {
            const int x1 = right - (int)((s->count - 1 - i) * (right - left) / (UI_HISTORY - 1));
            const int x0 = right - (int)((s->count - i) * (right - left) / (UI_HISTORY - 1));
            const int h = bottom - top;
            line(c, x0, bottom - (int)(fraction(a.voltage, (float)max_volts) * h),
                 x1, bottom - (int)(fraction(d.voltage, (float)max_volts) * h), 2, volts_color);
            line(c, x0, bottom - (int)(fraction(a.current, 16) * h),
                 x1, bottom - (int)(fraction(d.current, 16) * h), 2, RED);
        }
    }
}

void ui_render(const ui_state_t *s, uint16_t *pixels, int top, int rows)
{
    canvas_t c = {.pixels = pixels, .top = top, .bottom = top + rows};
    rectangle(&c, (box_t){0,0,UI_WIDTH,UI_HEIGHT}, 0);
    header(&c, s->mode, s->latest);
    if (s->mode == UI_DIGITAL) {
        for (int i = 0; i < 3; ++i) {
            meter(&c, (box_t){18,76 + i * 130,818,119}, s->latest, i, true);
        }
    } else if (s->mode == UI_NEEDLE) {
        const int range = select_range(s->latest.current);
        panel(&c, (box_t){12,73,830,303}, 17, CYAN, PANEL, RGB565(1,8,13));
        range_buttons(&c, (box_t){28,87,109,277}, range);
        line(&c, 149, 88, 149, 360, 1, CYAN);
        dial(&c, (box_t){165,83,657,280}, s->latest, range);
        meters_bottom(&c, s->latest, 387, 81);
    } else if (s->mode == UI_TREND) {
        chart(&c, s, (box_t){12,73,830,302});
        meters_bottom(&c, s->latest, 386, 82);
    } else if (s->mode == UI_COMBINED) {
        panel(&c, (box_t){12,73,830,193}, 16, CYAN, PANEL, RGB565(1,8,13));
        range_buttons(&c, (box_t){25,77,87,181}, 4);
        line(&c, 121, 83, 121, 254, 1, CYAN);
        line(&c, 625, 83, 625, 254, 1, CYAN);
        dial(&c, (box_t){140,80,468,179}, s->latest, 4);
        for (int i = 0; i < 3; ++i) {
            meter(&c, (box_t){638,86 + i * 56,190,47}, s->latest, i, false);
        }
        chart(&c, s, (box_t){12,276,830,190});
    }
}

void ui_render_cover(uint16_t *pixels, int top, int rows)
{
    canvas_t c = {.pixels = pixels, .top = top, .bottom = top + rows};
    rectangle(&c, (box_t){0,0,UI_WIDTH,UI_HEIGHT}, 0);
    header(&c, -1, (reading_t){.demo = true});
    static const char *const titles[] = {"数字显示", "指针显示", "曲线显示", "指针+曲线"};
    static const char *const captions[] = {"电压 / 电流 / 功率", "模拟指针仪表", "趋势波形曲线", "指针/曲线组合"};
    for (int i = 0; i < 4; ++i) {
        const int x = 22 + i % 2 * 410;
        const int y = 78 + i / 2 * 191;
        const box_t area = {x,y,397,176};
        if (visible(&c, area)) {
            panel(&c, area, 18, i == 0 ? GREEN : CYAN, PANEL, RGB565(1,6,9));
            text(&c, &title_font, x + 99, y + 72, 1, INK, titles[i]);
            text(&c, &label_font, x + 99, y + 111, 1, CYAN, captions[i]);
            line(&c, x + 196, y + 23, x + 196, y + 153, 1, GRID);
            if (i == 1 || i == 3) {
                dial(&c, (box_t){x+201,y+11,180,154},
                     (reading_t){.current=3.f,.valid=true,.demo=true}, 4);
            } else if (i == 2) {
                for (int k = 0; k < 3; ++k) {
                    const int mid = y + 55 + k * 32;
                    for (int q = 1; q <= 145; ++q) {
                        const int x0 = x+220+q-1, x1 = x+220+q;
                        const int y0 = mid + (int)(6*sinf((q-1)/8.f));
                        const int y1 = mid + (int)(6*sinf(q/8.f));
                        line(&c,x0,y0,x1,y1,2,reading_colors[k]);
                    }
                }
            } else {
                text(&c,&meter_font,x+359,y+44,2,GREEN,"12.00 V");
                text(&c,&meter_font,x+359,y+89,2,RED,"3.00 A");
                text(&c,&meter_font,x+359,y+134,2,BLUE,"36.00 W");
            }
        }
    }
    rectangle(&c, (box_t){22,462,807,2}, CYAN);
}

void ui_label_reference(uint16_t *pixels, int top, int rows)
{
    canvas_t c = {.pixels = pixels, .top = top, .bottom = top + rows};
    rounded(&c, (box_t){12,446,137,25}, 6, PANEL, PANEL);
    text(&c, &axis_font, 24, 458, 0, INK, "UI PREVIEW");
}

static const char *const dmm_names[] = {"交流电压", "直流电压", "二极管", "电阻档"};
static const char *const dmm_tabs[] = {"AC", "DC", "二极管", "电阻"};
#define DMM_LIME RGB565(176, 255, 36)

static void dmm_symbol(canvas_t *c, dmm_mode_t mode, int x, int y)
{
    if (mode == DMM_DIODE) {
        line(c, x - 39, y, x + 39, y, 3, CYAN);
        rectangle(c, (box_t){x+17,y-25,4,51}, CYAN);
        for (int i = 0; i <= 29; ++i) {
            const int half = (29 - i) * 23 / 29;
            rectangle(c, (box_t){x-15+i,y-half,1,2*half+1}, CYAN);
        }
    } else if (mode == DMM_RESISTANCE) {
        text(c, &meter_font, x, y, 1, CYAN, "Ω");
    } else {
        text(c, &meter_font, x - 21, y, 1, CYAN, "V");
        if (mode == DMM_AC) {
            for (int i = 1; i <= 40; ++i) {
                line(c, x-1+i-1, y-(int)(6*sinf((i-1)*PI/20.f)),
                     x-1+i, y-(int)(6*sinf(i*PI/20.f)), 3, CYAN);
            }
        } else {
            rectangle(c, (box_t){x-1,y-6,40,3}, CYAN);
            for (int i = 0; i < 3; ++i) {
                rectangle(c, (box_t){x-1+i*15,y+4,10,3}, CYAN);
            }
        }
    }
}

static void dmm_value_text(char *out, size_t size, dmm_mode_t mode, dmm_reading_t reading)
{
    if (reading.status == DMM_OPEN || reading.status == DMM_OVERRANGE ||
        (reading.status == DMM_VALID && fabsf(reading.value) >= 999999999.f)) {
        snprintf(out, size, "O.L");
    } else if (reading.status != DMM_VALID) {
        snprintf(out, size, "--");
    } else if (mode == DMM_RESISTANCE) {
        if (reading.value >= 999999.5f) {
            snprintf(out, size, "%.3f MΩ", (double)reading.value / 1000000.0);
        } else if (reading.value >= 999.95f) {
            snprintf(out, size, "%.3f kΩ", (double)reading.value / 1000.0);
        } else {
            snprintf(out, size, "%.1f Ω", (double)reading.value);
        }
    } else {
        const int precision = mode == DMM_DIODE ? 3 : 1;
        snprintf(out, size, "%.*f V", precision, (double)reading.value);
    }
}

void dmm_render(const dmm_history_t *history, dmm_mode_t mode, uint16_t *pixels, int top, int rows)
{
    canvas_t c = {.pixels = pixels, .top = top, .bottom = top + rows};
    rectangle(&c, (box_t){0,0,UI_WIDTH,UI_HEIGHT}, 0);
    if ((unsigned)mode >= DMM_MODE_COUNT) {
        return;
    }
    const bool has_samples = history->count > 0;
    const size_t latest_index = (history->next + DMM_HISTORY - 1) % DMM_HISTORY;
    const dmm_reading_t latest = has_samples ? history->readings[latest_index] : (dmm_reading_t){0};
    panel(&c, (box_t){10,45,834,412}, 16, CYAN, RGB565(2,12,17), 0);
    text(&c, &label_font, 25, 22, 0, INK, "GD-A-004  数字万用表");
    line(&c, 282, 40, 319, 40, 2, CYAN);
    line(&c, 319, 40, 343, 7, 2, CYAN);
    line(&c, 343, 7, 833, 7, 2, CYAN);
    for (int i = 0; i < DMM_MODE_COUNT; ++i) {
        const int x = 355 + i * 94;
        panel(&c, (box_t){x,12,86,26}, 7, mode == (dmm_mode_t)i ? CYAN : GRID,
              mode == (dmm_mode_t)i ? RGB565(0,75,100) : PANEL, 0);
        text(&c, &label_font, x + 43, 25, 1, INK, dmm_tabs[i]);
    }
    text(&c, &axis_font, 818, 25, 2, CYAN, latest.demo ? "DEMO" : "DMM");

    const box_t hero = {21,54,812,140};
    panel(&c, hero, 13, CYAN, RGB565(2,12,17), 0);
    if (visible(&c, hero)) {
        line(&c, 247, 65, 247, 182, 1, CYAN);
        dmm_symbol(&c, mode, 134, 92);
        text(&c, &title_font, 134, 134, 1, INK, dmm_names[mode]);
        const char *reference = mode == DMM_AC ? "参考量程 1000 V" :
                                mode == DMM_DC ? "参考量程 600 V" : NULL;
        if (latest.demo && reference) {
            panel(&c, (box_t){48,157,172,26}, 8, GRID, PANEL, 0);
            text(&c, &label_font, 134, 170, 1, DMM_LIME, reference);
        }
        char value[28];
        dmm_value_text(value, sizeof(value), mode, latest);
        const font_t *font = text_width(&dmm_value_font, value) <= 554 ? &dmm_value_font : &meter_font;
        text(&c, font, 531, 124, 1, DMM_LIME, value);
    }

    const size_t oldest = (history->next + DMM_HISTORY - history->count) % DMM_HISTORY;
    for (size_t i = 0; i < DMM_HISTORY; ++i) {
        const box_t box = {22 + (int)(i % 5) * 164, 203 + (int)(i / 5) * 60, 154, 53};
        if (visible(&c, box)) {
            const bool filled = i < history->count;
            const bool latest_cell = filled && i + 1 == history->count;
            char slot[4], value[28];
            snprintf(slot, sizeof(slot), "%02u", (unsigned)i + 1);
            dmm_reading_t reading = filled ? history->readings[(oldest + i) % DMM_HISTORY] :
                                            (dmm_reading_t){0};
            dmm_value_text(value, sizeof(value), mode, reading);
            panel(&c, box, 9, latest_cell ? DMM_LIME : CYAN,
                  latest_cell ? RGB565(7,41,16) : PANEL, 0);
            text(&c, &axis_font, box.x + 8, box.y + 12, 0, INK, slot);
            const font_t *font = text_width(&dmm_history_font, value) <= 126 ? &dmm_history_font : &axis_font;
            text(&c, font, box.x + box.w - 10, box.y + 30, 2,
                 latest_cell ? DMM_LIME : INK, value);
        }
    }
}

void dmm_render_cover(uint16_t *pixels, int top, int rows)
{
    canvas_t c = {.pixels = pixels, .top = top, .bottom = top + rows};
    rectangle(&c, (box_t){0,0,UI_WIDTH,UI_HEIGHT}, RGB565(1,8,12));
    panel(&c, (box_t){14,29,826,415}, 24, CYAN, RGB565(4,40,46), RGB565(1,15,21));
    line(&c, 281, 85, 281, 385, 2, GRID);

    const uint16_t jacket = RGB565(255,205,0);
    const uint16_t case_color = RGB565(56,53,29);
    panel(&c, (box_t){53,95,136,257}, 22, INK, jacket, RGB565(201,117,4));
    panel(&c, (box_t){69,117,103,56}, 9, case_color,
          RGB565(78,151,221), RGB565(176,219,253));
    text(&c, &label_font, 121, 145, 1, RGB565(12,47,75), "DMM");
    panel(&c, (box_t){76,184,90,90}, 45, case_color, RGB565(86,94,98), RGB565(20,27,33));
    line(&c, 108, 247, 143, 211, 7, INK);
    for (int i = 0; i < 3; ++i) {
        panel(&c, (box_t){76+i*34,302,19,19}, 9, i == 0 ? RED : case_color, 0, 0);
    }
    line(&c, 106, 351, 108, 381, 4, GRID);
    line(&c, 108, 381, 213, 381, 4, GRID);
    line(&c, 213, 381, 213, 219, 4, GRID);
    line(&c, 146, 351, 147, 394, 4, RED);
    line(&c, 147, 394, 248, 394, 4, RED);
    line(&c, 248, 394, 248, 219, 4, RED);
    rounded(&c, (box_t){205,145,17,83}, 6, RGB565(89,100,108), RGB565(17,23,29));
    rounded(&c, (box_t){240,145,17,83}, 6, RED, RGB565(109,7,8));
    line(&c, 213, 110, 213, 145, 3, INK);
    line(&c, 248, 110, 248, 145, 3, INK);

    text(&c, &meter_font, 324, 112, 0, INK, "万用表");
    line(&c, 321, 151, 804, 151, 2, CYAN);
    text(&c, &title_font, 323, 185, 0, INK, "AC / DC / 二极管 / 电阻");
    for (int i = 0; i < DMM_MODE_COUNT; ++i) {
        const int x = 320 + i * 124;
        panel(&c, (box_t){x,236,112,120}, 17, CYAN, RGB565(0,76,38), RGB565(7,32,33));
        dmm_symbol(&c, (dmm_mode_t)i, x + 56, 278);
        text(&c, &label_font, x + 56, 332, 1, INK, dmm_tabs[i]);
    }
    text(&c, &axis_font, 804, 412, 2, CYAN, "DEMO");
}
