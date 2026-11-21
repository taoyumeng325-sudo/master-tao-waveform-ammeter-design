#include <stdlib.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "board.h"

#ifdef UI_HAS_PICTURES
extern const uint8_t ui_pictures_start[] asm("_binary_ui_pictures_bin_start");
extern const uint8_t ui_pictures_end[] asm("_binary_ui_pictures_bin_end");

static void show_reference_images(uint16_t *strip)
{
    const size_t palette_bytes = 512;
    const size_t frame_bytes = palette_bytes + (size_t)UI_WIDTH * UI_HEIGHT;
    const size_t size = (uintptr_t)ui_pictures_end - (uintptr_t)ui_pictures_start;
    if (size % frame_bytes != 0) {
        ESP_LOGE("ammeter", "Invalid reference image data");
        return;
    }

    for (size_t offset = 0; offset < size; offset += frame_bytes) {
        const uint8_t *picture = ui_pictures_start + offset;
        uint16_t palette[256];
        for (size_t i = 0; i < 256; ++i) {
            palette[i] = picture[2 * i] | (uint16_t)picture[2 * i + 1] << 8;
        }
        const uint8_t *pixels = picture + palette_bytes;
        for (int top = 0; top < UI_HEIGHT; top += UI_STRIP_ROWS) {
            const int rows = UI_HEIGHT - top < UI_STRIP_ROWS ? UI_HEIGHT - top : UI_STRIP_ROWS;
            for (int i = 0; i < rows * UI_WIDTH; ++i) {
                strip[i] = palette[pixels[top * UI_WIDTH + i]];
            }
            ui_label_reference(strip, top, rows);
            board_present(strip, UI_WIDTH, top, rows);
        }
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}
#endif

void app_main(void)
{
    static ui_state_t ui;
    static dmm_history_t dmm[DMM_MODE_COUNT];
    ui_mode_t page = UI_DIGITAL;
    if (!board_begin()) {
        ESP_LOGW("ammeter", "Board driver unavailable; running without display or measurements");
        return;
    }

    uint16_t *frame = malloc((size_t)UI_WIDTH * UI_STRIP_ROWS * sizeof(*frame));
    if (!frame) {
        ESP_LOGE("ammeter", "Unable to allocate RGB565 framebuffer");
        return;
    }

#ifdef UI_HAS_PICTURES
    show_reference_images(frame);
#endif

    for (int top = 0; top < UI_HEIGHT; top += UI_STRIP_ROWS) {
        const int rows = UI_HEIGHT - top < UI_STRIP_ROWS ? UI_HEIGHT - top : UI_STRIP_ROWS;
        ui_render_cover(frame, top, rows);
        board_present(frame, UI_WIDTH, top, rows);
    }
    vTaskDelay(pdMS_TO_TICKS(1800));

    int64_t next_power_sample = 0;
    int64_t next_dmm_sample = 0;
    for (;;) {
        const int selected = board_next_mode();
        const bool changed = selected >= 0 && selected < UI_PAGE_COUNT && selected != (int)page;
        if (changed) {
            page = (ui_mode_t)selected;
        }

        const int64_t now = esp_timer_get_time();
        const bool is_dmm = page >= UI_DMM_FIRST;
        const dmm_mode_t mode = is_dmm ? (dmm_mode_t)(page - UI_DMM_FIRST) : DMM_AC;
        const bool power_due = now >= next_power_sample;
        if (power_due) {
            ui_push(&ui, board_read());
            next_power_sample = now + 100000;
        }

        const bool dmm_due = is_dmm && (changed || now >= next_dmm_sample);
        if (dmm_due) {
            dmm_push(&dmm[mode], mode, board_read_dmm(mode));
            next_dmm_sample = now + 200000;
        }

        if (page < UI_MODE_COUNT) {
            ui.mode = page;
        }
        if (changed || dmm_due || (page < UI_MODE_COUNT && power_due)) {
            for (int top = 0; top < UI_HEIGHT; top += UI_STRIP_ROWS) {
                const int rows = UI_HEIGHT - top < UI_STRIP_ROWS ? UI_HEIGHT - top : UI_STRIP_ROWS;
                if (is_dmm) {
                    dmm_render(&dmm[mode], mode, frame, top, rows);
                } else if (page == UI_DMM_COVER) {
                    dmm_render_cover(frame, top, rows);
                } else {
                    ui_render(&ui, frame, top, rows);
                }
                board_present(frame, UI_WIDTH, top, rows);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
