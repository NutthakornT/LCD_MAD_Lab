# Lab 08 – RGB Colour Mixer on ILI9341 LCD (Page 1)

Code report for `Core/Src/main.c` · STM32F767ZI (NUCLEO-F767ZI) · STM32CubeIDE / HAL

## 1. Objective

Show the following on the ILI9341 LCD, following Figure 1.1 of the lab sheet:

- White background with black text.
- Temperature and relative humidity from the AM2320 sensor. The sensor is not connected yet, so both values are placeholders set to 0.0.
- The level of red, green and blue from 0% to 100%, each shown as a bar with a percentage.
- A circle filled with the colour made by mixing the three levels.
- Touching the red, green or blue circle raises that level by 10%.

Screen layout (landscape 320 × 240; the values are only an example):

```
+--------------------------------------------+
|  27.1 C        ( mix )        55.6%RH      |
|                                            |
|  (R)   [##########======]       80 %       |
|  (G)   [#####===========]       40 %       |
|  (B)   [########========]       70 %       |
+--------------------------------------------+
   ##### = full colour     ===== = pale tint
```

## 2. Overview of the changes

All new code is inside the CubeMX `/* USER CODE BEGIN … */` blocks, so it stays in place if the project is regenerated.

| USER CODE block | What was added |
|---|---|
| `Includes` | The ILI9341 driver, GFX and touchscreen headers, and `<stdio.h>` for `snprintf` |
| `PD` (defines) | Layout positions and sizes, text size, the 10% step, and pale tint colours |
| `PV` (variables) | The `Channel` enum, colour tables, the RGB percentages, and the temperature and humidity variables |
| `PFP` (prototypes) | Prototypes for the Page 1 functions |
| `0` (functions) | Helper, drawing and touch functions |
| `2` (after init) | Start the LCD, rotate it to landscape, and draw the first screen |
| `WHILE` | Main loop that checks for touches, adding 10% once per tap |

## 3. Code explanation

### 3.1 Includes

```c
#include <stdio.h>
#include "ILI9341/ILI9341_STM32_Driver.h"
#include "ILI9341/ILI9341_GFX.h"
#include "ILI9341/ILI9341_Touchscreen.h"
```

The library files are in `Core/Src/ILI9341/`. The include paths are relative to `main.c`, so no extra compiler include path is needed.
The driver handles SPI5 and the CS/DC/RST pins, the GFX file draws text and shapes, and the touchscreen file reads the XPT2046 touch chip by toggling GPIO pins in software.

### 3.2 Layout and colour defines

```c
#define BG_COLOUR       WHITE
#define TEXT_COLOUR     BLACK

#define FONT_H(size)    (8 * (size))    /* CHAR_HEIGHT of 5x5_font.h */
#define TOP_TEXT_SIZE   3               /* 18x24 px per char */
#define TOP_TEXT_Y      28
#define TEMP_X          8
#define HUMID_X         188
#define MIX_X           150
#define MIX_Y           40
#define MIX_R           26

#define ROW_Y0          105             /* centre Y of the R/G/B rows */
#define ROW_PITCH       55
#define DOT_X           30
#define DOT_R           20
#define BAR_X           60
#define BAR_W           140
#define BAR_H           30
#define PCT_X           222
#define PCT_TEXT_SIZE   3

#define TOUCH_MARGIN    10
#define STEP_PERCENT    10

#define LIGHT_RED       0xFE38
#define LIGHT_GREEN     0xBFF7
#define LIGHT_BLUE      0xC63F
```

- All positions are for the **landscape 320 × 240** orientation (`SCREEN_HORIZONTAL_1`). Keeping every number here makes the layout easy to adjust.
- The library font is 6 × 8 pixels per character. Size 3 makes each character 18 × 24 pixels, which is large and easy to read, like the lab sheet.
- The three colour rows are centred at Y = 105, 160 and 215, 55 pixels apart.
- The `LIGHT_*` values are pale RGB565 colours for the unfilled part of each bar, as in Figure 1.1. For example, 0xFE38 is about (255, 200, 200).

### 3.3 Variables

```c
typedef enum { CH_RED = 0, CH_GREEN, CH_BLUE, CH_COUNT } Channel;

static const uint16_t channel_colour[CH_COUNT] = { RED, GREEN, BLUE };
static const uint16_t channel_light[CH_COUNT]  = { LIGHT_RED, LIGHT_GREEN, LIGHT_BLUE };

static uint8_t rgb_percent[CH_COUNT] = { 0, 0, 0 };

/* TODO: fill these from the AM2320 later */
static float temperature = 0.0f;
static float humidity    = 0.0f;
```

The `Channel` enum is used as an array index. This lets one function handle red, green and blue instead of repeating the code three times.
`rgb_percent[]` stores each level (0–100). `temperature` and `humidity` are placeholders until the AM2320 is added.

### 3.4 Helper functions

```c
static uint16_t Row_Y(Channel ch)
{
  return ROW_Y0 + ch * ROW_PITCH;
}
```

Returns the centre Y position of a colour row.

```c
static void Format_1dp(char *buf, size_t len, float v, const char *unit)
{
  int x10 = (int)(v * 10.0f + (v >= 0 ? 0.5f : -0.5f));
  const char *sign = (x10 < 0) ? "-" : "";
  if (x10 < 0) x10 = -x10;
  snprintf(buf, len, "%s%d.%d%s", sign, x10 / 10, x10 % 10, unit);
}
```

Turns a float into text with one decimal place, such as `27.1 C`. By default, STM32CubeIDE's newlib-nano `printf` cannot print floats (`%f`) unless the `-u _printf_float` linker flag is added.
To avoid that, the value is multiplied by 10, rounded, and printed as a whole part and a decimal digit.

```c
static uint16_t Mix_Colour(void)
{
  uint16_t r = rgb_percent[CH_RED]   * 31 / 100;
  uint16_t g = rgb_percent[CH_GREEN] * 63 / 100;
  uint16_t b = rgb_percent[CH_BLUE]  * 31 / 100;
  return (r << 11) | (g << 5) | b;
}
```

Mixes the three levels into one colour. The ILI9341 uses **RGB565**: 5 bits for red (0–31), 6 bits for green (0–63) and 5 bits for blue (0–31).
Each percentage is scaled to its bit range, and the parts are shifted into place: `RRRRRGGGGGGBBBBB`.

### 3.5 Drawing functions

```c
static void Page1_DrawSensor(void)
{
  char buf[16];

  Format_1dp(buf, sizeof(buf), temperature, " C ");
  ILI9341_Draw_Text(buf, TEMP_X, TOP_TEXT_Y, TEXT_COLOUR, TOP_TEXT_SIZE, BG_COLOUR);

  Format_1dp(buf, sizeof(buf), humidity, "%RH");
  ILI9341_Draw_Text(buf, HUMID_X, TOP_TEXT_Y, TEXT_COLOUR, TOP_TEXT_SIZE, BG_COLOUR);
}
```

Draws the temperature on the left and the humidity on the right of the top row. `ILI9341_Draw_Text` paints the background behind each character, so new text covers the old text without clearing the screen.
The extra space after `" C "` clears a leftover character if the number gets shorter.

```c
static void Page1_DrawMix(void)
{
  ILI9341_Draw_Filled_Circle(MIX_X, MIX_Y, MIX_R, Mix_Colour());
  ILI9341_Draw_Hollow_Circle(MIX_X, MIX_Y, MIX_R + 1, LIGHTGREY);
}
```

Draws the circle with the mixed colour. A light grey outline is added so the circle can still be seen on the white background when all three levels are 100% (white).

```c
static void Page1_DrawChannel(Channel ch)
{
  char buf[8];
  uint16_t y = Row_Y(ch);
  uint16_t bar_top = y - BAR_H / 2;
  uint16_t fill_w = rgb_percent[ch] * BAR_W / 100;

  if (fill_w > 0)
    ILI9341_Draw_Rectangle(BAR_X, bar_top, fill_w, BAR_H, channel_colour[ch]);
  if (fill_w < BAR_W)
    ILI9341_Draw_Rectangle(BAR_X + fill_w, bar_top, BAR_W - fill_w, BAR_H, channel_light[ch]);

  snprintf(buf, sizeof(buf), "%3d %%", rgb_percent[ch]);
  ILI9341_Draw_Text(buf, PCT_X, y - FONT_H(PCT_TEXT_SIZE) / 2,
                    TEXT_COLOUR, PCT_TEXT_SIZE, BG_COLOUR);
}
```

- The filled width is `percent × BAR_W / 100`. The bar is drawn in two parts: the solid colour, then the pale colour for the rest. Nothing has to be erased first, so the bar does not flicker.
- The `if` checks skip rectangles with zero width, because the library would compute `X + Width − 1` with Width = 0 and get a bad address.
- `"%3d %%"` always gives 5 characters (`"  0 %"`, `" 80 %"`, `"100 %"`), so a shorter number always covers the longer one before it.

```c
static void Page1_Init(void)
{
  ILI9341_Fill_Screen(BG_COLOUR);

  Page1_DrawSensor();
  Page1_DrawMix();

  for (Channel ch = CH_RED; ch < CH_COUNT; ch++)
  {
    ILI9341_Draw_Filled_Circle(DOT_X, Row_Y(ch), DOT_R, channel_colour[ch]);
    Page1_DrawChannel(ch);
  }
}
```

Draws the whole page once: white background, sensor values, mix circle, and for each colour its circle, bar and percentage.
The red, green and blue circles never change, so they are drawn only here.

### 3.6 Touch handling

```c
static void Page1_HandleTouch(uint16_t x, uint16_t y)
{
  int32_t hit_r = DOT_R + TOUCH_MARGIN;

  for (Channel ch = CH_RED; ch < CH_COUNT; ch++)
  {
    int32_t dx = (int32_t)x - DOT_X;
    int32_t dy = (int32_t)y - Row_Y(ch);
    if (dx * dx + dy * dy <= hit_r * hit_r)
    {
      rgb_percent[ch] += STEP_PERCENT;
      if (rgb_percent[ch] > 100)
        rgb_percent[ch] = 0;

      Page1_DrawChannel(ch);
      Page1_DrawMix();
      return;
    }
  }
}
```

- To test whether a touch is inside a circle, the code checks `dx² + dy² ≤ r²`. This avoids a square root.
- The hit radius is 10 pixels larger than the drawn circle (`TOUCH_MARGIN`), because the resistive touch panel is not very precise.
- Each hit adds 10%. Going past 100% wraps back to 0%, so the level can be touched round and round.
- Only the changed bar and the mix circle are redrawn. The library draws circles one pixel at a time, which is slow, so redrawing the whole screen would be too slow.

```c
static uint8_t Touch_Read_Landscape(uint16_t *x, uint16_t *y)
{
  uint16_t pos[2];
  if (TP_Read_Coordinates(pos) != TOUCHPAD_DATA_OK)
    return 0;

  uint16_t tx = (pos[0] > 239) ? 239 : pos[0];
  uint16_t ty = (pos[1] > 319) ? 319 : pos[1];

  *x = ty;
  *y = 239 - tx;
  return 1;
}
```

The touchscreen library is calibrated for the upright screen, `SCREEN_VERTICAL_1` (240 × 320). This page uses the sideways screen, so the touch position is converted:
`X_land = Y_port` and `Y_land = 239 − X_port`. The values are first limited to the screen size. If the reading is noisy, for example the finger was lifted while sampling, the function returns 0 and the touch is ignored.

> **Note:** If touches land in mirrored places on the real board, change the conversion to `*x = 319 - ty; *y = tx;`.

### 3.7 Initialisation (USER CODE 2)

```c
ILI9341_Init();
ILI9341_Set_Rotation(SCREEN_HORIZONTAL_1);
Page1_Init();

uint8_t touch_held = 0;
```

This runs after CubeMX has set up GPIO, RNG, SPI5 and TIM1. `ILI9341_Init()` resets the LCD and sends its setup commands.
The screen is then rotated to landscape to match Figure 1.1, and the first screen is drawn.

### 3.8 Main loop

```c
while (1)
{
  if (TP_Touchpad_Pressed())
  {
    uint16_t x, y;
    if (!touch_held && Touch_Read_Landscape(&x, &y))
    {
      touch_held = 1;
      Page1_HandleTouch(x, y);
    }
  }
  else
  {
    touch_held = 0;
  }

  HAL_Delay(20);
}
```

`TP_Touchpad_Pressed()` checks the touch IRQ pin, which is low while the screen is pressed. The `touch_held` flag makes each tap count only once:
the value goes up when the finger first touches, and nothing more happens until the finger is lifted. Without the flag, holding a finger down would keep adding 10%.
The 20 ms delay slows the loop down and helps ignore contact bounce.

## 4. Change to the ILI9341 library

In `ILI9341_GFX.h` and `ILI9341_GFX.c`, the `X` and `Y` parameters of `ILI9341_Draw_Char` and `ILI9341_Draw_Text` were changed from `uint8_t` to `uint16_t`.
A `uint8_t` can only hold 0–255, but the landscape screen is 320 pixels wide. Text on the right side, such as the humidity and percentages starting at X = 188 and 222 with 18 pixels per character, would wrap back to the left edge.

## 5. Remaining work

- Read the AM2320 sensor, store the results in `temperature` and `humidity`, and call `Page1_DrawSensor()` every few seconds.
- Check the touch direction on the real board (see the note in 3.6).
