#pragma once

/* RGB values for the display theme. */
#define RGB565(r, g, b) ((uint16_t)(((r) & 0xf8) << 8 | ((g) & 0xfc) << 3 | (b) >> 3))
#define INK RGB565(239, 251, 255)
#define CYAN RGB565(0, 229, 238)
#define GREEN RGB565(53, 244, 75)
#define RED RGB565(255, 68, 68)
#define BLUE RGB565(53, 124, 255)
#define PANEL RGB565(4, 19, 28)
#define GRID RGB565(39, 64, 77)
#define VOLTAGE_FILL RGB565(3, 102, 21)
#define CURRENT_FILL RGB565(157, 18, 20)
#define POWER_FILL RGB565(11, 51, 174)
