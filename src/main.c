/*
 * STM32-EMG-Biosignal : Milestone 1, EMG envelope -> servo open/close (threshold).
 *
 * Closes the full loop: sense -> process -> decide -> actuate, in a plain super-loop
 * (FreeRTOS comes later). Pipeline (TIM2 @ 1 kHz ISR): ADC -> adaptive bias -> 50 Hz
 * notch -> rectify -> envelope. Main loop thresholds the envelope with hysteresis and
 * drives the SG90 (TIM4_CH1 / PB6) to OPEN or CLOSE.
 *
 * Decision: contract (env > ENV_HIGH) -> CLOSE ; relax (env < ENV_LOW) -> OPEN.
 * The gap between the two thresholds is hysteresis, it stops chatter at the boundary.
 *
 * Failsafe: on a signal fault (clip / over-amplitude / bias out of range) the servo is
 * forced OPEN (safe) and the EMG is ignored until the signal is healthy again.
 *
 * Thresholds are PROVISIONAL : watch the printed env during relax vs contract and tune.
 *
 * Wiring: Grove EMG SIG->PA0(A0), VCC->3V3, GND->GND ; reference (white) on wrist.
 *         Servo signal->PB6(D10), V+->separate 6 V pack, GND->battery- AND Nucleo GND.
 */
#include "stm32f4xx_hal.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

#define SAMPLE_HZ   1000
#define MAINS_HZ    50
#define WIN         (SAMPLE_HZ/MAINS_HZ)
#define BIAS_ALPHA  0.001f

#define NOTCH_B0   0.9417923f
#define NOTCH_B1  -1.7913934f
#define NOTCH_B2   0.9417923f
#define NOTCH_A1  -1.7913934f
#define NOTCH_A2   0.8835919f

/* --- decision thresholds (PROVISIONAL, tune from the printed envS) --- */
#define ENV_HIGH    70       /* contract above this -> CLOSE (on the SLOW envelope) */
#define ENV_LOW     30       /* relax below this    -> OPEN                          */
#define ENV_SLOW_A  0.005f   /* ~200 ms smoothing : flattens held-contraction jitter */
#define WARMUP_MS   2500     /* hold OPEN until the adaptive bias settles after boot  */

/* --- servo endpoints --- */
#define OPEN_US     1000
#define CLOSE_US    2000

/* --- fault thresholds (from the health-monitor work) --- */
#define AMP_FAULT   400
#define BIAS_LO     1550
#define BIAS_HI     2150

static ADC_HandleTypeDef  hadc1;
static UART_HandleTypeDef huart2;
static TIM_HandleTypeDef  htim2;   /* 1 kHz EMG sampling */
static TIM_HandleTypeDef  htim4;   /* 50 Hz servo PWM    */

static volatile float g_bias = 2048.0f;
static volatile float g_env  = 0.0f;
static volatile float g_env_slow = 0.0f;   /* slow-smoothed envelope, used for decisions */
static volatile uint16_t win_min = 4095, win_max = 0, clip_cnt = 0;

static float ring[WIN]; static int ridx = 0; static float rsum = 0.0f;

static float nx1=0,nx2=0,ny1=0,ny2=0;
static float notch(float x){
    float y = NOTCH_B0*x + NOTCH_B1*nx1 + NOTCH_B2*nx2 - NOTCH_A1*ny1 - NOTCH_A2*ny2;
    nx2=nx1; nx1=x; ny2=ny1; ny1=y; return y;
}

static uint32_t emg_read_raw(void){
    HAL_ADC_Start(&hadc1);
    HAL_ADC_PollForConversion(&hadc1, 2);
    uint32_t r = HAL_ADC_GetValue(&hadc1);
    HAL_ADC_Stop(&hadc1);
    return r;
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim){
    if (htim->Instance != TIM2) return;   /* TIM4 is PWM only, no IRQ */
    uint32_t rawi = emg_read_raw();
    if (rawi < win_min) win_min = (uint16_t)rawi;
    if (rawi > win_max) win_max = (uint16_t)rawi;
    if (rawi <= 1 || rawi >= 4094) clip_cnt++;
    float raw = (float)rawi;
    g_bias += BIAS_ALPHA * (raw - g_bias);
    float acf = notch(raw - g_bias);
    float rect = fabsf(acf);
    rsum -= ring[ridx]; ring[ridx] = rect; rsum += rect;
    ridx = (ridx + 1) % WIN;
    g_env = rsum / (float)WIN;
    g_env_slow += ENV_SLOW_A * (g_env - g_env_slow);   /* ~200 ms decision envelope */
}

static void led_init(void){
    __HAL_RCC_GPIOA_CLK_ENABLE();
    GPIO_InitTypeDef g={0}; g.Pin=GPIO_PIN_5; g.Mode=GPIO_MODE_OUTPUT_PP; g.Pull=GPIO_NOPULL; g.Speed=GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA,&g);
}

static void adc_init(void){
    __HAL_RCC_GPIOA_CLK_ENABLE(); __HAL_RCC_ADC1_CLK_ENABLE();
    GPIO_InitTypeDef gpio={0}; gpio.Pin=GPIO_PIN_0; gpio.Mode=GPIO_MODE_ANALOG; gpio.Pull=GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA,&gpio);
    hadc1.Instance=ADC1;
    hadc1.Init.ClockPrescaler=ADC_CLOCK_SYNC_PCLK_DIV4; hadc1.Init.Resolution=ADC_RESOLUTION_12B;
    hadc1.Init.ScanConvMode=DISABLE; hadc1.Init.ContinuousConvMode=DISABLE; hadc1.Init.DiscontinuousConvMode=DISABLE;
    hadc1.Init.ExternalTrigConv=ADC_SOFTWARE_START; hadc1.Init.DataAlign=ADC_DATAALIGN_RIGHT;
    hadc1.Init.NbrOfConversion=1; hadc1.Init.DMAContinuousRequests=DISABLE; hadc1.Init.EOCSelection=ADC_EOC_SINGLE_CONV;
    HAL_ADC_Init(&hadc1);
    ADC_ChannelConfTypeDef ch={0}; ch.Channel=ADC_CHANNEL_0; ch.Rank=1; ch.SamplingTime=ADC_SAMPLETIME_84CYCLES;
    HAL_ADC_ConfigChannel(&hadc1,&ch);
}

static void uart_init(void){
    __HAL_RCC_GPIOA_CLK_ENABLE(); __HAL_RCC_USART2_CLK_ENABLE();
    GPIO_InitTypeDef gpio={0}; gpio.Pin=GPIO_PIN_2|GPIO_PIN_3; gpio.Mode=GPIO_MODE_AF_PP; gpio.Pull=GPIO_PULLUP;
    gpio.Speed=GPIO_SPEED_FREQ_VERY_HIGH; gpio.Alternate=GPIO_AF7_USART2; HAL_GPIO_Init(GPIOA,&gpio);
    huart2.Instance=USART2; huart2.Init.BaudRate=115200; huart2.Init.WordLength=UART_WORDLENGTH_8B;
    huart2.Init.StopBits=UART_STOPBITS_1; huart2.Init.Parity=UART_PARITY_NONE; huart2.Init.Mode=UART_MODE_TX_RX;
    huart2.Init.HwFlowCtl=UART_HWCONTROL_NONE; huart2.Init.OverSampling=UART_OVERSAMPLING_16; HAL_UART_Init(&huart2);
}

static void emg_timer_init(void){
    __HAL_RCC_TIM2_CLK_ENABLE();
    htim2.Instance=TIM2; htim2.Init.Prescaler=16-1; htim2.Init.CounterMode=TIM_COUNTERMODE_UP;
    htim2.Init.Period=(1000000/SAMPLE_HZ)-1; htim2.Init.ClockDivision=TIM_CLOCKDIVISION_DIV1;
    htim2.Init.AutoReloadPreload=TIM_AUTORELOAD_PRELOAD_DISABLE; HAL_TIM_Base_Init(&htim2);
    HAL_NVIC_SetPriority(TIM2_IRQn,1,0); HAL_NVIC_EnableIRQ(TIM2_IRQn); HAL_TIM_Base_Start_IT(&htim2);
}
void TIM2_IRQHandler(void){ HAL_TIM_IRQHandler(&htim2); }

static void servo_init(void){
    __HAL_RCC_GPIOB_CLK_ENABLE(); __HAL_RCC_TIM4_CLK_ENABLE();
    GPIO_InitTypeDef gpio={0}; gpio.Pin=GPIO_PIN_6; gpio.Mode=GPIO_MODE_AF_PP; gpio.Pull=GPIO_NOPULL;
    gpio.Speed=GPIO_SPEED_FREQ_LOW; gpio.Alternate=GPIO_AF2_TIM4; HAL_GPIO_Init(GPIOB,&gpio);
    htim4.Instance=TIM4; htim4.Init.Prescaler=16-1; htim4.Init.CounterMode=TIM_COUNTERMODE_UP;
    htim4.Init.Period=20000-1; htim4.Init.ClockDivision=TIM_CLOCKDIVISION_DIV1; HAL_TIM_PWM_Init(&htim4);
    TIM_OC_InitTypeDef oc={0}; oc.OCMode=TIM_OCMODE_PWM1; oc.Pulse=OPEN_US; oc.OCPolarity=TIM_OCPOLARITY_HIGH;
    HAL_TIM_PWM_ConfigChannel(&htim4,&oc,TIM_CHANNEL_1); HAL_TIM_PWM_Start(&htim4,TIM_CHANNEL_1);
}
static void servo_set(int us){ __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_1, us); }

int main(void){
    HAL_Init();
    for (int i=0;i<WIN;i++) ring[i]=0.0f;
    led_init(); adc_init(); uart_init(); emg_timer_init(); servo_init();

    const char *hello="STM32-EMG : envelope -> servo open/close (threshold)\r\n";
    HAL_UART_Transmit(&huart2,(uint8_t*)hello,strlen(hello),100);

    char line[64];
    int closed = 0;             /* 0 = OPEN, 1 = CLOSED */
    servo_set(OPEN_US);

    while (1){
        uint16_t mn=win_min, mx=win_max, cl=clip_cnt;
        win_min=4095; win_max=0; clip_cnt=0;
        uint16_t amp = (mx>=mn)?(mx-mn):0;
        uint32_t bias=(uint32_t)g_bias, env=(uint32_t)g_env;

        uint32_t envs = (uint32_t)g_env_slow;
        /* fault = real disconnection signatures ONLY (clip / bias-off). NOT amplitude :
         * a strong contraction legitimately has large amplitude. */
        int fault = (cl>0) || (bias<BIAS_LO) || (bias>BIAS_HI);
        const char *st;

        if (HAL_GetTick() < WARMUP_MS){
            /* let the adaptive bias settle before trusting the signal */
            closed = 0; servo_set(OPEN_US);
            HAL_GPIO_WritePin(GPIOA,GPIO_PIN_5,GPIO_PIN_RESET);
            st = "WARMUP";
        } else if (fault){
            /* FAILSAFE: signal not trustworthy -> open hand, ignore EMG */
            closed = 0; servo_set(OPEN_US);
            HAL_GPIO_WritePin(GPIOA,GPIO_PIN_5,GPIO_PIN_SET);   /* warning LED on */
            st = "FAULT->OPEN";
        } else {
            HAL_GPIO_WritePin(GPIOA,GPIO_PIN_5,GPIO_PIN_RESET);
            if (!closed && envs > ENV_HIGH){ closed=1; servo_set(CLOSE_US); }
            else if (closed && envs < ENV_LOW){ closed=0; servo_set(OPEN_US); }
            st = closed ? "CLOSED" : "OPEN";
        }

        int n=snprintf(line,sizeof line,"env=%4lu envS=%4lu amp=%4u bias=%4lu  %s\r\n",
                       (unsigned long)env, (unsigned long)envs, amp, (unsigned long)bias, st);
        if(n<0)n=0; if(n>(int)sizeof line)n=(int)sizeof line;
        HAL_UART_Transmit(&huart2,(uint8_t*)line,n,50);

        HAL_Delay(30);
    }
}

void SysTick_Handler(void){ HAL_IncTick(); }
