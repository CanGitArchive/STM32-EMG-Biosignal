/*
 * STM32-EMG-Biosignal : RAW EMG streamer (data source for the Python tools).
 *
 * Streams the raw ADC value on PA0 (A0) over USART2 at ~200 Hz, one integer per line.
 * Deliberately dumb: ALL filtering, plotting, recording, and DTW happen on the laptop
 * (tools/emg_studio/), so the algorithm iterates in seconds without reflashing.
 *
 * Once the Python side nails the pipeline (notch + envelope + DTW templates incl. the
 * relaxed/cooldown class), we port the chosen pipeline back onto the STM32 for the
 * standalone build.
 *
 * Output: plain "<raw>\r\n" lines (0..4095). Non-numeric lines: none, so the parser is trivial.
 */
#include "stm32f4xx_hal.h"
#include <stdio.h>
#include <string.h>

static ADC_HandleTypeDef  hadc1;
static UART_HandleTypeDef huart2;

static uint32_t emg_read_raw(void)
{
    HAL_ADC_Start(&hadc1);
    HAL_ADC_PollForConversion(&hadc1, 2);
    uint32_t r = HAL_ADC_GetValue(&hadc1);
    HAL_ADC_Stop(&hadc1);
    return r;
}

static void adc_init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_ADC1_CLK_ENABLE();
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = GPIO_PIN_0; gpio.Mode = GPIO_MODE_ANALOG; gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &gpio);
    hadc1.Instance = ADC1;
    hadc1.Init.ClockPrescaler        = ADC_CLOCK_SYNC_PCLK_DIV4;
    hadc1.Init.Resolution            = ADC_RESOLUTION_12B;
    hadc1.Init.ScanConvMode          = DISABLE;
    hadc1.Init.ContinuousConvMode    = DISABLE;
    hadc1.Init.DiscontinuousConvMode = DISABLE;
    hadc1.Init.ExternalTrigConv      = ADC_SOFTWARE_START;
    hadc1.Init.DataAlign             = ADC_DATAALIGN_RIGHT;
    hadc1.Init.NbrOfConversion       = 1;
    hadc1.Init.DMAContinuousRequests = DISABLE;
    hadc1.Init.EOCSelection          = ADC_EOC_SINGLE_CONV;
    HAL_ADC_Init(&hadc1);
    ADC_ChannelConfTypeDef ch = {0};
    ch.Channel = ADC_CHANNEL_0; ch.Rank = 1; ch.SamplingTime = ADC_SAMPLETIME_84CYCLES;
    HAL_ADC_ConfigChannel(&hadc1, &ch);
}

static void uart_init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_USART2_CLK_ENABLE();
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = GPIO_PIN_2 | GPIO_PIN_3; gpio.Mode = GPIO_MODE_AF_PP; gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH; gpio.Alternate = GPIO_AF7_USART2;
    HAL_GPIO_Init(GPIOA, &gpio);
    huart2.Instance = USART2;
    huart2.Init.BaudRate = 115200; huart2.Init.WordLength = UART_WORDLENGTH_8B;
    huart2.Init.StopBits = UART_STOPBITS_1; huart2.Init.Parity = UART_PARITY_NONE;
    huart2.Init.Mode = UART_MODE_TX_RX; huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart2.Init.OverSampling = UART_OVERSAMPLING_16;
    HAL_UART_Init(&huart2);
}

int main(void)
{
    HAL_Init();
    adc_init();
    uart_init();

    char line[12];
    uint32_t next = HAL_GetTick();
    while (1)
    {
        /* fixed ~5 ms cadence (200 Hz) using the tick, steadier than HAL_Delay alone */
        while ((int32_t)(HAL_GetTick() - next) < 0) { }
        next += 5;

        uint32_t raw = emg_read_raw();
        int n = snprintf(line, sizeof line, "%lu\r\n", (unsigned long)raw);
        HAL_UART_Transmit(&huart2, (uint8_t *)line, n, 10);
    }
}

void SysTick_Handler(void) { HAL_IncTick(); }
