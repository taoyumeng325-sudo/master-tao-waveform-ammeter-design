#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "dmm.h"

enum { UI_WIDTH = 854, UI_HEIGHT = 480, UI_STRIP_ROWS = 20, UI_HISTORY = 301 };
typedef enum {
    UI_DIGITAL, UI_NEEDLE, UI_TREND, UI_COMBINED, UI_MODE_COUNT,
    UI_DMM_COVER = UI_MODE_COUNT, UI_DMM_FIRST,
    UI_PAGE_COUNT = UI_DMM_FIRST + DMM_MODE_COUNT
} ui_mode_t;

typedef struct {
    float voltage;
    float current;
    bool valid;
    bool demo;
} reading_t;

typedef struct {
    ui_mode_t mode;
    reading_t latest;
    reading_t history[UI_HISTORY];
    size_t write_index;
    size_t count;
} ui_state_t;

void ui_push(ui_state_t *state, reading_t reading);
/* top and rows specify a valid slice within the display. */
void ui_render(const ui_state_t *state, uint16_t *pixels, int top, int rows);
void ui_render_cover(uint16_t *pixels, int top, int rows);
void ui_label_reference(uint16_t *pixels, int top, int rows);
