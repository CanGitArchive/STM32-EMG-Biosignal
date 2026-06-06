/*
 * STM32-EMG-Biosignal : EMG health monitor + fault snapshot (diagnostic).
 *
 * Goal: reproduce and UNDERSTAND the bias~1200 / railing failure we saw once, and lay the
 * groundwork for a real connection-fault warning (Phase 7 failsafe).
 *
 * Per 50 ms window it classifies signal health and prints a status word:
 *   GOOD                            : amp small, no clipping, bias near center
 *   FAULT:CLIP                      : samples hitting a rail (signal railing)
 *   FAULT:NOISE                     : amplitude too large (mains leaking in)
 *   FAULT:BIASOFF                   : DC center wandered far from ~1850
 *
 * LD2 (PA5) is the WARNING light : ON = fault, OFF = good.
 *
 * AND : the first window of each fault episode arms a 1000-sample raw capture; the trace
 * is then dumped framed by FAULTCAP.../ENDFAULT so we can see the actual waveform of the
 * failure (is it real clipping? asymmetric? a DC shift?). Re-arms after returning to GOOD.
 *
 * Thresholds below are provisional : we tune them once we have reproduced faults to look at.
 * Pipeline (bias -> notch -> rectify -> smooth) is unchanged from the committed build a918064.
 */
#include "stm32f4xx_hal.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

#define SAMPLE_HZ   1000
#define MAINS_HZ    50
#define WIN         (SAMPLE_HZ/MAINS_HZ)
#define BIAS_ALPHA  0.001f

/* 50 Hz notch biquad (RBJ, f0/fs=0.05, Q=2.5) */
#define NOTCH_B0   0.9417923f
#define NOTCH_B1  -1.7913934f
#define NOTCH_B2   0.9417923f
#define NOTCH_A1  -1.7913934f
#define NOTCH_A2   0.8835919f

/* ---- fault thresholds (provisional, tune after reproducing) ---- */
#define AMP_FAULT   400      /* p2p above this = NOISE fault (clean is ~30..100)  */
#define BIAS_LO     1550     /* DC center expected ~1850; outside +-300 = BIASOFF */
#define BIAS_HI     2150
#define CLIP_FAULT  1        /* any clipped sample in the window = CLIP fault     */

#define CAPN        1000     /* fault snapshot length (1 s @ 1 kHz) */

static ADC_HandleTypeDef  hadc1;
static UART_HandleTypeDef huart2;
static TIM_HandleTypeDef  htim2;

static volatile float g_bias = 2048.0f;
static volatile float g_env  = 0.0f;

static float    ring[WIN];
static int      ridx = 0;
static float    rsum = 0.0f;

static volatile uint16_t win_min  = 4095, win_max = 0, clip_cnt = 0, samp_cnt = 0;

/* fault snapshot buffer */
static volatile uint16_t cap[CAPN];
static volatile int      cap_idx  = 0;
static volatile int      cap_arm  = 0;   /* ISR fills cap[] while this is set   */
static volatile int      cap_full = 0;

static float nx1=0, nx2=0, ny1=0, ny2=0;
static float notch(float x)
{
    float y = NOTCH_B0*x + NOTCH_B1*nx1 + NOTCH_B2*nx2 - NOTCH_A1*ny1 - NOTCH_A2*ny2;
    nx2=nx1; nx1=x; ny2=ny1; ny1=y;
    return y;
}

static uint32_t emg_read_raw(void)
{
    HAL_ADC_Start(&hadc1);
    HAL_ADC_PollForConversion(&hadc1, 2);
    uint32_t raw = HAL_ADC_GetValue(&hadc1);
    HAL_ADC_Stop(&hadc1);
    return raw;
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance != TIM2) return;

    uint32_t rawi = emg_read_raw();

    if (rawi < win_min) win_min = (uint16_t)rawi;
    if (rawi > win_max) win_max = (uint16_t)rawi;
    if (rawi <= 1 || rawi >= 4094) clip_cnt++;
    samp_cnt++;

    /* fault snapshot fill */
    if (cap_arm && !cap_full) {
        cap[cap_idx++] = (uint16_t)rawi;
        if (cap_idx >= CAPN) cap_full = 1;
    }

    float raw = (float)rawi;
    g_bias += BIAS_ALPHA * (raw - g_bias);
    float ac  = raw - g_bias;
    float acf = notch(ac);
    float rect = fabsf(acf);
    rsum -= ring[ridx]; ring[ridx] = rect; rsum += rect;
    ridx = (ridx + 1) % WIN;
    g_env = rsum / (float)WIN;
}

static void led_init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    GPIO_InitTypeDef g = {0};
    g.Pin = GPIO_PIN_5; g.Mode = GPIO_MODE_OUTPUT_PP; g.Pull = GPIO_NOPULL; g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &g);
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

static void timer_init(void)
{
    __HAL_RCC_TIM2_CLK_ENABLE();
    htim2.Instance = TIM2;
    htim2.Init.Prescaler = 16 - 1;
    htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim2.Init.Period = (1000000 / SAMPLE_HZ) - 1;
    htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    HAL_TIM_Base_Init(&htim2);
    HAL_NVIC_SetPriority(TIM2_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(TIM2_IRQn);
    HAL_TIM_Base_Start_IT(&htim2);
}

void TIM2_IRQHandler(void) { HAL_TIM_IRQHandler(&htim2); }

static void uart_puts(const char *s) { HAL_UART_Transmit(&huart2, (uint8_t*)s, strlen(s), 200); }

int main(void)
{
    HAL_Init();
    for (int i = 0; i < WIN; i++) ring[i] = 0.0f;
    led_init(); adc_init(); uart_init(); timer_init();

    char line[96];
    int episode_captured = 0;   /* one snapshot per fault episode */

    while (1)
    {
        uint16_t mn = win_min, mx = win_max, cl = clip_cnt;
        win_min = 4095; win_max = 0; clip_cnt = 0; samp_cnt = 0;
        uint16_t amp = (mx >= mn) ? (mx - mn) : 0;
        uint32_t bias = (uint32_t)g_bias;
        uint32_t env  = (uint32_t)g_env;

        const char *status = "GOOD";
        int fault = 1;
        if      (cl >= CLIP_FAULT)                 status = "FAULT:CLIP";
        else if (amp > AMP_FAULT)                  status = "FAULT:NOISE";
        else if (bias < BIAS_LO || bias > BIAS_HI) status = "FAULT:BIASOFF";
        else { status = "GOOD"; fault = 0; }

        /* warning light : ON when faulted */
        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, fault ? GPIO_PIN_SET : GPIO_PIN_RESET);

        int n = snprintf(line, sizeof line, "amp=%4u clip=%3u bias=%4lu env=%4lu %s\r\n",
                         amp, cl, (unsigned long)bias, (unsigned long)env, status);
        if (n < 0) n = 0; if (n > (int)sizeof line) n = (int)sizeof line;
        HAL_UART_Transmit(&huart2, (uint8_t*)line, n, 100);

        /* arm a raw snapshot on the first window of a fault episode */
        if (fault && !episode_captured && !cap_arm) {
            cap_idx = 0; cap_full = 0; cap_arm = 1;
        }
        if (cap_full) {
            uart_puts("FAULTCAP n=1000 fs=1000\r\n");
            for (int i = 0; i < CAPN; i++) {
                int m = snprintf(line, sizeof line, "%u\r\n", (unsigned)cap[i]);
                HAL_UART_Transmit(&huart2, (uint8_t*)line, m, 100);
            }
            uart_puts("ENDFAULT\r\n");
            cap_arm = 0; cap_full = 0; episode_captured = 1;
        }
        if (!fault) episode_captured = 0;   /* re-arm for the next episode */

        HAL_Delay(50);
    }
}

void SysTick_Handler(void) { HAL_IncTick(); }
