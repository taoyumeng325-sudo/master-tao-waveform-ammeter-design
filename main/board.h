#pragma once
#include "ui.h"

/* Run on the app task. Return true only after verified hardware initialization. */
bool board_begin(void);
/* Present an RGB565 strip at y=top and consume it before returning. */
void board_present(const uint16_t *frame, int width, int top, int rows);
/* Return -1 when no user input is queued, or a ui_mode_t value. */
int board_next_mode(void);
/* Return an invalid reading when the measurement is unavailable. */
reading_t board_read(void);
dmm_reading_t board_read_dmm(dmm_mode_t mode);
