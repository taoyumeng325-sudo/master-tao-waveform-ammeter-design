#include "dmm.h"

#include <math.h>

void dmm_push(dmm_history_t *history, dmm_mode_t mode, dmm_reading_t reading)
{
    if (reading.status == DMM_VALID &&
        (!isfinite(reading.value) || (mode != DMM_DC && reading.value < 0.f))) {
        reading.status = DMM_UNAVAILABLE;
    }
    history->readings[history->next] = reading;
    history->next = (history->next + 1) % DMM_HISTORY;
    if (history->count < DMM_HISTORY) {
        ++history->count;
    }
}
