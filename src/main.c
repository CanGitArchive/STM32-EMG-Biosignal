/*
 * STM32-EMG-Biosignal : EMG streamer + servo command listener.
 *
 * Streams raw ADC (PA0) over USART2 at ~200 Hz (one int per line) AND listens on the same
 * UART for servo commands, so the laptop tools can both read EMG and drive the gripper.
 *
 * Command: a line "S<microseconds>\n" sets the servo TARGET pulse width. The firmware
 * SLEW-LIMITS toward the target (a few us per 5 ms loop), so the SG90 always eases between
 * positions and never slams the printed PLA gripper. Targets are clamped to a safe range.
 *
 * RX is interrupt-driven (the F4 USART has no RX FIFO; polling at 200 Hz would drop bytes
 * of a fast command burst). TX (the raw stream) stays polled.
 *
 * Servo on TIM4_CH1 / PB6 (Nucleo D10). Same wiring as the servo bring-up: signal->D10,
 * V+->separate 6 V supply, GND->battery- AND Nucleo GND.
 */
#include "stm32f4xx_hal.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define SMIN     800        /* clamp: conservative travel limits (widen once verified) */
#define SMAX     2200
#define SLEW     4          /* us per 5 ms loop -> ~800 us/s : gentle, never slams      */

static ADC_HandleTypeDef  hadc1;
static UART_HandleTypeDef huart2;
static TIM_HandleTypeDef  htim4;

static volatile int target_us  = 1500;     /* commanded position (set by RX ISR) */
static int          current_us = 1500;     /* slewed actual position             */

static char rxbuf[16];
static int  rxi = 0;

static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

static uint32_t emg_read_raw(void)
{
    HAL_ADC_Start(&hadc1);
    HAL_ADC_PollForConversion(&hadc1, 2);
    uint32_t r = HAL_ADC_GetValue(&hadc1);
    HAL_ADC_Stop(&hadc1);
    return r;
}

void USART2_IRQHandler(void)
{
    if (__HAL_UART_GET_FLAG(&huart2, UART_FLAG_RXNE))
    {
        char c = (char)(huart2.Instance->DR & 0xFF);   /* reading DR clears RXNE (+ ORE) */
        if (c == '\n' || c == '\r')
        {
            rxbuf[rxi] = 0;
            if (rxbuf[0] == 'S')
                target_us = clampi(atoi(rxbuf + 1), SMIN, SMAX);
            rxi = 0;
        }
        else if (rxi < (int)sizeof(rxbuf) - 1)
        {
            rxbuf[rxi++] = c;
        }
    }
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
    __HAL_UART_ENABLE_IT(&huart2, UART_IT_RXNE);
    HAL_NVIC_SetPriority(USART2_IRQn, 2, 0);
    HAL_NVIC_EnableIRQ(USART2_IRQn);
}

static void servo_init(void)
{
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_TIM4_CLK_ENABLE();
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = GPIO_PIN_6; gpio.Mode = GPIO_MODE_AF_PP; gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW; gpio.Alternate = GPIO_AF2_TIM4;
    HAL_GPIO_Init(GPIOB, &gpio);
    htim4.Instance = TIM4;
    htim4.Init.Prescaler = 16 - 1; htim4.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim4.Init.Period = 20000 - 1; htim4.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    HAL_TIM_PWM_Init(&htim4);
    TIM_OC_InitTypeDef oc = {0};
    oc.OCMode = TIM_OCMODE_PWM1; oc.Pulse = 1500; oc.OCPolarity = TIM_OCPOLARITY_HIGH;
    HAL_TIM_PWM_ConfigChannel(&htim4, &oc, TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_1);
}

int main(void)
{
    HAL_Init();
    adc_init();
    uart_init();
    servo_init();

    char line[12];
    uint32_t next = HAL_GetTick();
    while (1)
    {
        while ((int32_t)(HAL_GetTick() - next) < 0) { }
        next += 5;                              /* ~200 Hz */

        uint32_t raw = emg_read_raw();
        int n = snprintf(line, sizeof line, "%lu\r\n", (unsigned long)raw);
        HAL_UART_Transmit(&huart2, (uint8_t *)line, n, 10);

        /* slew the servo toward the commanded target (eases, never slams) */
        if (current_us < target_us)
            current_us += (target_us - current_us < SLEW) ? (target_us - current_us) : SLEW;
        else if (current_us > target_us)
            current_us -= (current_us - target_us < SLEW) ? (current_us - target_us) : SLEW;
        __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_1, current_us);
    }
}

void SysTick_Handler(void) { HAL_IncTick(); }
