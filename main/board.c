#include "board.h"

#include <math.h>

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_ldo_regulator.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "st7701_init.h"

enum { POWER_1 = 31, POWER_2 = 32, BACKLIGHT = 33, RESET = 28 };
static const char *TAG = "gda004";
static esp_lcd_panel_handle_t panel;
static uint16_t *scanout;
static int last_mode = -1;
static int64_t demo_start;

static void delay_ms(unsigned ms)
{
    TickType_t ticks = pdMS_TO_TICKS(ms);
    vTaskDelay(ticks + 1);
}

/* Parameters reconstructed from the original TFT_Init function at 0x40022DAC. */
static esp_err_t begin_panel(void)
{
    const esp_ldo_channel_config_t ldo_cfg = {.chan_id = 3, .voltage_mv = 2500};
    esp_ldo_channel_handle_t ldo;
    ESP_RETURN_ON_ERROR(esp_ldo_acquire_channel(&ldo_cfg, &ldo), TAG, "DSI supply");

    const gpio_config_t pins = {
        .pin_bit_mask = (1ULL << POWER_1) | (1ULL << POWER_2) |
                        (1ULL << BACKLIGHT) | (1ULL << RESET),
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&pins), TAG, "panel GPIO");
    gpio_set_level(BACKLIGHT, 0);
    gpio_set_level(POWER_1, 0);
    gpio_set_level(POWER_2, 0);
    gpio_set_level(POWER_1, 1);
    delay_ms(10);
    gpio_set_level(POWER_2, 1);
    delay_ms(10);
    gpio_set_level(RESET, 0);
    delay_ms(10);
    gpio_set_level(RESET, 1);
    delay_ms(20);
    gpio_set_level(RESET, 0);
    delay_ms(300);

    const esp_lcd_dsi_bus_config_t dsi = {
        .bus_id = 0, .num_data_lanes = 2, .lane_bit_rate_mbps = 400.f,
    };
    esp_lcd_dsi_bus_handle_t bus;
    ESP_RETURN_ON_ERROR(esp_lcd_new_dsi_bus(&dsi, &bus), TAG, "DSI bus");

    const esp_lcd_dbi_io_config_t dbi = {
        .virtual_channel = 0, .lcd_cmd_bits = 8, .lcd_param_bits = 8,
    };
    esp_lcd_panel_io_handle_t io;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_dbi(bus, &dbi, &io), TAG, "DSI command channel");

    const esp_lcd_dpi_panel_config_t dpi = {
        .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = 30.f,
        .in_color_format = LCD_COLOR_FMT_RGB565,
        .num_fbs = 1,
        .video_timing = {
            .h_size = UI_HEIGHT, .v_size = UI_WIDTH,
            .hsync_pulse_width = 6, .hsync_back_porch = 40, .hsync_front_porch = 40,
            .vsync_pulse_width = 4, .vsync_back_porch = 8, .vsync_front_porch = 10,
        },
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_dpi(bus, &dpi, &panel), TAG, "DPI video channel");
    void *buffer = NULL;
    ESP_RETURN_ON_ERROR(esp_lcd_dpi_panel_get_frame_buffer(panel, 1, &buffer),
                        TAG, "get scanout buffer");
    scanout = buffer;

    for (size_t i = 0; i < sizeof(panel_init) / sizeof(panel_init[0]); ++i) {
        const st7701_command_t *cmd = &panel_init[i];
        ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, cmd->command,
                            cmd->length ? cmd->data : NULL, cmd->length), TAG, "ST7701 init");
        if (cmd->delay_ms) {
            delay_ms(cmd->delay_ms);
        }
    }

    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(panel), TAG, "start DPI");
    ESP_RETURN_ON_ERROR(gpio_set_level(BACKLIGHT, 1), TAG, "backlight");
    return ESP_OK;
}

bool board_begin(void)
{
    if (begin_panel() != ESP_OK) {
        return false;
    }
    demo_start = esp_timer_get_time();
    ESP_LOGI(TAG, "Display test; measurements are simulated");
    return true;
}

void board_present(const uint16_t *frame, int width, int top, int rows)
{
    for (int x = 0; x < width; ++x) {
        for (int y = 0; y < rows; ++y) {
            scanout[(width - 1 - x) * UI_HEIGHT + top + y] = frame[y * width + x];
        }
    }
    if (top + rows != UI_HEIGHT) {
        return;
    }
    /* Passing the DPI-owned buffer requests a cache writeback without a copy. */
    esp_err_t err = esp_lcd_panel_draw_bitmap(panel, 0, 0, UI_HEIGHT, width, scanout);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "DPI draw failed: %s", esp_err_to_name(err));
    }
}

int board_next_mode(void)
{
    static int64_t first_page_us;
    const int64_t now = esp_timer_get_time();
    if (last_mode < 0) {
        first_page_us = now;
    }
    const int mode = (int)((now - first_page_us) / 5000000 % UI_PAGE_COUNT);
    if (last_mode == mode) {
        return -1;
    }
    last_mode = mode;
    return mode;
}

reading_t board_read(void)
{
    float t = (float)(esp_timer_get_time() - demo_start) / 1000000.f;
    return (reading_t){
        .voltage = 12.f + 1.7f * sinf(t * 0.7f),
        .current = 3.f + 1.2f * sinf(t * 1.5f),
        .valid = true,
        .demo = true,
    };
}

dmm_reading_t board_read_dmm(dmm_mode_t mode)
{
    const float seconds = (float)(esp_timer_get_time() - demo_start) / 1000000.f;
    const float nominal[] = {220.5f, 12.6f, .523f, 1367.f};
    const float jitter[] = {.11f, .025f, .0012f, 1.4f};
    if ((unsigned)mode >= DMM_MODE_COUNT) {
        return (dmm_reading_t){.demo = true};
    }
    return (dmm_reading_t){
        .value = nominal[mode] + jitter[mode] * sinf(seconds * 4.f),
        .status = DMM_VALID,
        .demo = true,
    };
}
