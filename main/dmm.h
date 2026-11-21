#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { DMM_HISTORY = 20 };

typedef enum { DMM_AC, DMM_DC, DMM_DIODE, DMM_RESISTANCE, DMM_MODE_COUNT } dmm_mode_t;
typedef enum { DMM_UNAVAILABLE, DMM_VALID, DMM_OPEN, DMM_OVERRANGE } dmm_status_t;

typedef struct {
    float value; /* Volts, or ohms in resistance mode. */
    dmm_status_t status;
    bool demo;
} dmm_reading_t;

typedef struct {
    dmm_reading_t readings[DMM_HISTORY];
    size_t next;
    size_t count;
} dmm_history_t;

void dmm_push(dmm_history_t *history, dmm_mode_t mode, dmm_reading_t reading);
void dmm_render(const dmm_history_t *history, dmm_mode_t mode, uint16_t *pixels, int top, int rows);
void dmm_render_cover(uint16_t *pixels, int top, int rows);
