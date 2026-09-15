/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "rng.h"
#include "spi.h"
#include "tim.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include "ILI9341/ILI9341_STM32_Driver.h"
#include "ILI9341/ILI9341_GFX.h"
#include "ILI9341/ILI9341_Touchscreen.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* Page 1 layout (landscape 320x240, SCREEN_HORIZONTAL_1) */
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

#define TOUCH_MARGIN    10              /* extra px around a dot that still counts as a hit */
#define STEP_PERCENT    10

/* light tints used for the empty part of each bar */
#define LIGHT_RED       0xFE38
#define LIGHT_GREEN     0xBFF7
#define LIGHT_BLUE      0xC63F
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
typedef enum { CH_RED = 0, CH_GREEN, CH_BLUE, CH_COUNT } Channel;

static const uint16_t channel_colour[CH_COUNT] = { RED, GREEN, BLUE };
static const uint16_t channel_light[CH_COUNT]  = { LIGHT_RED, LIGHT_GREEN, LIGHT_BLUE };

static uint8_t rgb_percent[CH_COUNT] = { 0, 0, 0 };

/* TODO: fill these from the AM2320 later */
static float temperature = 0.0f;
static float humidity    = 0.0f;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
static void Page1_Init(void);
static void Page1_DrawSensor(void);
static void Page1_DrawMix(void);
static void Page1_DrawChannel(Channel ch);
static void Page1_HandleTouch(uint16_t x, uint16_t y);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
static uint16_t Row_Y(Channel ch)
{
  return ROW_Y0 + ch * ROW_PITCH;
}

/* Formats v with one decimal without needing printf float support */
static void Format_1dp(char *buf, size_t len, float v, const char *unit)
{
  int x10 = (int)(v * 10.0f + (v >= 0 ? 0.5f : -0.5f));
  const char *sign = (x10 < 0) ? "-" : "";
  if (x10 < 0) x10 = -x10;
  snprintf(buf, len, "%s%d.%d%s", sign, x10 / 10, x10 % 10, unit);
}

/* Mix the three channels (0-100 %) into one RGB565 colour */
static uint16_t Mix_Colour(void)
{
  uint16_t r = rgb_percent[CH_RED]   * 31 / 100;
  uint16_t g = rgb_percent[CH_GREEN] * 63 / 100;
  uint16_t b = rgb_percent[CH_BLUE]  * 31 / 100;
  return (r << 11) | (g << 5) | b;
}

static void Page1_DrawSensor(void)
{
  char buf[16];

  Format_1dp(buf, sizeof(buf), temperature, " C ");
  ILI9341_Draw_Text(buf, TEMP_X, TOP_TEXT_Y, TEXT_COLOUR, TOP_TEXT_SIZE, BG_COLOUR);

  Format_1dp(buf, sizeof(buf), humidity, "%RH");
  ILI9341_Draw_Text(buf, HUMID_X, TOP_TEXT_Y, TEXT_COLOUR, TOP_TEXT_SIZE, BG_COLOUR);
}

static void Page1_DrawMix(void)
{
  ILI9341_Draw_Filled_Circle(MIX_X, MIX_Y, MIX_R, Mix_Colour());
  /* outline so the circle is still visible when the mix is white */
  ILI9341_Draw_Hollow_Circle(MIX_X, MIX_Y, MIX_R + 1, LIGHTGREY);
}

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

/* The touch library is calibrated for SCREEN_VERTICAL_1 (240x320).
   Convert its result into SCREEN_HORIZONTAL_1 (320x240) coordinates. */
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
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* Enable the CPU Cache */

  /* Enable I-Cache---------------------------------------------------------*/
  SCB_EnableICache();

  /* Enable D-Cache---------------------------------------------------------*/
  SCB_EnableDCache();

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_RNG_Init();
  MX_SPI5_Init();
  MX_TIM1_Init();
  /* USER CODE BEGIN 2 */
  ILI9341_Init();
  ILI9341_Set_Rotation(SCREEN_HORIZONTAL_1);
  Page1_Init();

  uint8_t touch_held = 0;
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
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
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 12;
  RCC_OscInitStruct.PLL.PLLN = 192;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 9;
  RCC_OscInitStruct.PLL.PLLR = 2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Activate the Over-Drive mode
  */
  if (HAL_PWREx_EnableOverDrive() != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_6) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
