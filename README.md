# Lab 08 – RGB Colour Mixer + AM2320 on ILI9341 LCD (Page 1)

Code report for `Core/Src/main.c` · STM32F767ZI (NUCLEO-F767ZI) · STM32CubeIDE / HAL

## 1. Objective

Show the following on the ILI9341 LCD, following Figure 1.1 of the lab sheet:

- White background with black text.
- Temperature and relative humidity read from the **AM2320** sensor over I2C1.
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

## 2. Hardware and CubeMX setup

| Peripheral | Pins | Used for |
|---|---|---|
| SPI5 + PC8 (CS), PC9 (DC), PC10 (RST) | – | ILI9341 LCD |
| PE2–PE6 (GPIO) | T_IRQ, T_CLK, T_MISO, T_MOSI, T_CS | XPT2046 touch controller (software SPI) |
| **I2C1** | **PB8 = SCL, PB9 = SDA** | AM2320 temperature / humidity sensor (address `0x5C`) |

The AM2320 needs pull-up resistors on SDA and SCL. Most AM2320 modules have them built in.

## 3. Overview of the changes

All new code is inside the CubeMX `/* USER CODE BEGIN … */` blocks, so it stays in place if the project is regenerated.

| USER CODE block | What was added |
|---|---|
| `Includes` | The ILI9341 driver, GFX and touchscreen headers, and `<stdio.h>` for `snprintf` |
| `PD` (defines) | Layout positions and sizes, text sizes, the 10% step, pale tint colours, and the AM2320 address and read period |
| `PV` (variables) | The `Channel` enum, colour tables, the RGB percentages, `t` / `h`, and the AM2320 command and data buffers |
| `PFP` (prototypes) | Prototypes for the Page 1 functions and `CRC16_2` |
| `0` (functions) | Helper, drawing, touch and `AM2320_Read()` functions |
| `2` (after init) | Start the LCD, rotate it to landscape, draw the first screen, and start the sensor timer |
| `WHILE` | Main loop: reads the sensor every 3 s and checks for touches (10% once per tap) |
| `4` | `CRC16_2()` checksum function from the lab sheet |

## 4. Code explanation

### 4.1 Includes

```c
#include <stdio.h>
#include "ILI9341/ILI9341_STM32_Driver.h"
#include "ILI9341/ILI9341_GFX.h"
#include "ILI9341/ILI9341_Touchscreen.h"
```

The library files are in `Core/Src/ILI9341/`. The include paths are relative to `main.c`, so no extra compiler include path is needed.
The driver handles SPI5 and the CS/DC/RST pins, the GFX file draws text and shapes, and the touchscreen file reads the XPT2046 touch chip by toggling GPIO pins in software.
`i2c.h` (with `hi2c1`) is included by CubeMX in the generated include section.

### 4.2 Layout, colour and sensor defines

```c
#define BG_COLOUR       WHITE
#define TEXT_COLOUR     BLACK

#define FONT_H(size)    (8 * (size))    /* CHAR_HEIGHT of 5x5_font.h */
#define TOP_TEXT_SIZE   2               /* 12x16 px per char */
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

#define AM2320_ADDR     (0x5C << 1)
#define AM2320_PERIOD   3000            /* ms, sensor needs > 2 s between reads */

#define LIGHT_RED       0xFE38
#define LIGHT_GREEN     0xBFF7
#define LIGHT_BLUE      0xC63F
```

- All positions are for the **landscape 320 × 240** orientation (`SCREEN_HORIZONTAL_1`). Keeping every number here makes the layout easy to adjust.
- The library font is 6 × 8 pixels per character. The temperature and humidity use size 2 (12 × 16 pixels per character). The percentages use size 3 (18 × 24 pixels).
- The three colour rows are centred at Y = 105, 160 and 215, 55 pixels apart.
- The `LIGHT_*` values are pale RGB565 colours for the unfilled part of each bar, as in Figure 1.1. For example, 0xFE38 is about (255, 200, 200).
- `AM2320_ADDR` is the sensor's 7-bit I2C address `0x5C` shifted left by one, because HAL wants the 8-bit form.
- `AM2320_PERIOD` sets how often the sensor is read. The AM2320 needs at least 2 s between readings, so 3 s is used.

### 4.3 Variables

```c
typedef enum { CH_RED = 0, CH_GREEN, CH_BLUE, CH_COUNT } Channel;

static const uint16_t channel_colour[CH_COUNT] = { RED, GREEN, BLUE };
static const uint16_t channel_light[CH_COUNT]  = { LIGHT_RED, LIGHT_GREEN, LIGHT_BLUE };

static uint8_t rgb_percent[CH_COUNT] = { 0, 0, 0 };

/* latest AM2320 reading: t in degC, h in %RH */
static float t = 0.0f;
static float h = 0.0f;

/* AM2320: function 0x03 (read registers), start at 0x00, read 4 bytes */
static uint8_t cmdBuffer[3] = { 0x03, 0x00, 0x04 };
static uint8_t dataBuffer[8];
```

- The `Channel` enum is used as an array index. This lets one function handle red, green and blue instead of repeating the code three times.
- `rgb_percent[]` stores each level (0–100).
- `t` and `h` hold the latest temperature (°C) and humidity (%RH). They start at 0.0 until the first good reading.
- `cmdBuffer` is the AM2320 read command: function code `0x03` (read registers), start at register `0x00`, read `0x04` bytes (humidity high/low, temperature high/low).
- `dataBuffer` holds the 8-byte reply: `[0]` function code, `[1]` byte count, `[2..3]` humidity, `[4..5]` temperature, `[6..7]` CRC (low byte first).

### 4.4 Helper functions

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

### 4.5 Drawing functions

```c
static void Page1_DrawSensor(void)
{
  char buf[16];

  Format_1dp(buf, sizeof(buf), t, " C ");
  ILI9341_Draw_Text(buf, TEMP_X, TOP_TEXT_Y, TEXT_COLOUR, TOP_TEXT_SIZE, BG_COLOUR);

  Format_1dp(buf, sizeof(buf), h, "%RH");
  ILI9341_Draw_Text(buf, HUMID_X, TOP_TEXT_Y, TEXT_COLOUR, TOP_TEXT_SIZE, BG_COLOUR);
}
```

Draws `t` on the left and `h` on the right of the top row. `ILI9341_Draw_Text` paints the background behind each character, so new text covers the old text without clearing the screen.
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

### 4.6 Touch handling

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

### 4.7 AM2320 sensor reading

```c
static uint8_t AM2320_Read(void)
{
  //Wake up sensor (it NACKs this one while asleep, that is expected)
  HAL_I2C_Master_Transmit(&hi2c1, AM2320_ADDR, cmdBuffer, 3, 200);
  //Send reading command
  HAL_I2C_Master_Transmit(&hi2c1, AM2320_ADDR, cmdBuffer, 3, 200);

  HAL_Delay(1);

  //Receive sensor data
  HAL_I2C_Master_Receive(&hi2c1, AM2320_ADDR, dataBuffer, 8, 200);

  uint16_t Rcrc = dataBuffer[7] << 8;
  Rcrc += dataBuffer[6];
  if (Rcrc != CRC16_2(dataBuffer, 6))
    return 0;

  uint16_t temperature = ((dataBuffer[4] & 0x7F) << 8) + dataBuffer[5];
  t = temperature / 10.0;
  t = (((dataBuffer[4] & 0x80) >> 7) == 1) ? (t * (-1)) : t; // the temperature can be negative

  uint16_t humidity = (dataBuffer[2] << 8) + dataBuffer[3];
  h = humidity / 10.0;
  return 1;
}
```

This is the sensor code from the lab sheet, put into its own function:

1. **Wake up.** The AM2320 sleeps between readings to avoid warming itself up. The first transmit only wakes it, so it is expected to fail (NACK).
2. **Send the read command** `{0x03, 0x00, 0x04}`, wait 1 ms, then **read the 8-byte reply**.
3. **Check the CRC.** The last two bytes are a CRC-16 of the first six, sent low byte first. If it doesn't match, the reading is thrown away, the function returns 0, and `t` and `h` keep their old values.
4. **Temperature:** bytes 4–5 give the value × 10. Bit 7 of byte 4 is the sign bit, so it is masked off with `0x7F`, and the result is made negative if the bit is set.
5. **Humidity:** bytes 2–3 give the value × 10.

For example, a temperature of `0x01 0x0F` = 271, which is **27.1 °C**.

```c
uint16_t CRC16_2(uint8_t *ptr, uint8_t length)
{
      uint16_t  crc = 0xFFFF;
      uint8_t   s   = 0x00;

      while(length--) {
        crc ^= *ptr++;
        for(s = 0; s < 8; s++) {
          if((crc & 0x01) != 0) {
            crc >>= 1;
            crc ^= 0xA001;
          } else crc >>= 1;
        }
      }
      return crc;
}
```

The checksum function from the lab sheet (in `USER CODE BEGIN 4`). It is CRC-16/Modbus: start at `0xFFFF`, XOR in each byte, then for each of the 8 bits shift right and XOR with `0xA001` whenever the bit shifted out was 1.
Its prototype is in `USER CODE BEGIN PFP` because `AM2320_Read()` calls it before it appears in the file.

### 4.8 Initialisation (USER CODE 2)

```c
ILI9341_Init();
ILI9341_Set_Rotation(SCREEN_HORIZONTAL_1);
Page1_Init();

uint8_t touch_held = 0;
uint32_t last_sensor_tick = HAL_GetTick();
```

This runs after CubeMX has set up GPIO, RNG, SPI5, TIM1 and I2C1. `ILI9341_Init()` resets the LCD and sends its setup commands.
The screen is then rotated to landscape to match Figure 1.1, and the first screen is drawn. `last_sensor_tick` records the start time, so the first sensor reading happens 3 s after power-on, once the sensor has had time to start up.

### 4.9 Main loop

```c
while (1)
{
  /* read the AM2320 every AM2320_PERIOD ms without blocking the touch */
  if (HAL_GetTick() - last_sensor_tick >= AM2320_PERIOD)
  {
    last_sensor_tick = HAL_GetTick();
    if (AM2320_Read())
      Page1_DrawSensor();
  }

  /* one +10 % step per press: act on the press edge, wait for release */
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

- **Sensor timer.** The lab sheet's example waits with `HAL_Delay(5000)`, but that would freeze the board for 5 s at a time and miss touches.
  Instead, `HAL_GetTick()` (milliseconds since start-up) is compared with the time of the last reading. The sensor is read only when 3 s have passed, and the rest of the time the loop keeps running.
  The text is redrawn only after a good reading.
- **Touch.** `TP_Touchpad_Pressed()` checks the touch IRQ pin, which is low while the screen is pressed. The `touch_held` flag makes each tap count only once:
  the value goes up when the finger first touches, and nothing more happens until the finger is lifted. Without the flag, holding a finger down would keep adding 10%.
- The 20 ms delay slows the loop down and helps ignore contact bounce.

## 5. Change to the ILI9341 library

In `ILI9341_GFX.h` and `ILI9341_GFX.c`, the `X` and `Y` parameters of `ILI9341_Draw_Char` and `ILI9341_Draw_Text` were changed from `uint8_t` to `uint16_t`.
A `uint8_t` can only hold 0–255, but the landscape screen is 320 pixels wide. Text on the right side, such as the humidity and percentages starting at X = 188 and 222, would wrap back to the left edge.

## 6. Differences from the lab sheet example

- **No UART output.** The sheet sends the values over `huart3`, but UART is not turned on in this project. The values are shown on the LCD instead.
- **No `HAL_Delay(5000)`.** Replaced by the `HAL_GetTick()` timer so touch keeps working (see 4.9).
- **No `HAL_GPIO_TogglePin(GPIOB, GPIO_PIN_0)`.** PB0 is not set up as an output in this project.

## 7. Troubleshooting

- **Temperature and humidity stay at `0.0`:** every reading is failing the CRC check. Check the SDA/SCL wiring (PB9/PB8), the power, and the pull-up resistors.
- **Touch hits the wrong circle:** see the note in 4.6.
