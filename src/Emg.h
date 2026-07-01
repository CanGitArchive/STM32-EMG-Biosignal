#ifndef EMG_H
#define EMG_H
#include "stm32f4xx_hal.h"

// Emg : the muscle sensor on PA0; TIM2 triggers the ADC at 1 kHz and DMA parks each result in 'sample'.
class Emg
{
  public:
    void init()
    {
        __HAL_RCC_GPIOA_CLK_ENABLE();
        __HAL_RCC_ADC1_CLK_ENABLE();
        __HAL_RCC_DMA2_CLK_ENABLE();     // ADC1's DMA lives on DMA2
        __HAL_RCC_TIM2_CLK_ENABLE();

        GPIO_InitTypeDef g = {0};        // PA0 analog (unchanged from before)
        g.Pin = GPIO_PIN_0; g.Mode = GPIO_MODE_ANALOG; g.Pull = GPIO_NOPULL;
        HAL_GPIO_Init(GPIOA, &g);

        // DMA2 Stream0 (ADC1): copy each conversion into 'sample', same spot every time = always latest
        hdma.Instance                 = DMA2_Stream0;
        hdma.Init.Channel             = DMA_CHANNEL_0;
        hdma.Init.Direction           = DMA_PERIPH_TO_MEMORY;
        hdma.Init.PeriphInc           = DMA_PINC_DISABLE;
        hdma.Init.MemInc              = DMA_MINC_DISABLE;     // don't walk an array, keep one spot
        hdma.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
        hdma.Init.MemDataAlignment    = DMA_MDATAALIGN_HALFWORD;
        hdma.Init.Mode                = DMA_CIRCULAR;          // never stops
        hdma.Init.Priority            = DMA_PRIORITY_HIGH;
        hdma.Init.FIFOMode            = DMA_FIFOMODE_DISABLE;
        HAL_DMA_Init(&hdma);
        __HAL_LINKDMA(&hadc, DMA_Handle, hdma);

        // ADC1: 12-bit, started by TIM2's trigger (not software), result handed to DMA
        hadc.Instance                   = ADC1;
        hadc.Init.ClockPrescaler        = ADC_CLOCK_SYNC_PCLK_DIV4;
        hadc.Init.Resolution            = ADC_RESOLUTION_12B;
        hadc.Init.ScanConvMode          = DISABLE;
        hadc.Init.ContinuousConvMode    = DISABLE;
        hadc.Init.DiscontinuousConvMode = DISABLE;
        hadc.Init.ExternalTrigConvEdge  = ADC_EXTERNALTRIGCONVEDGE_RISING;
        hadc.Init.ExternalTrigConv      = ADC_EXTERNALTRIGCONV_T2_TRGO;   // TIM2 fires the conversion
        hadc.Init.DataAlign             = ADC_DATAALIGN_RIGHT;
        hadc.Init.NbrOfConversion       = 1;
        hadc.Init.DMAContinuousRequests = ENABLE;             // keep feeding DMA forever
        hadc.Init.EOCSelection          = ADC_EOC_SINGLE_CONV;
        HAL_ADC_Init(&hadc);

        ADC_ChannelConfTypeDef ch = {0};
        ch.Channel = ADC_CHANNEL_0; ch.Rank = 1; ch.SamplingTime = ADC_SAMPLETIME_84CYCLES;
        HAL_ADC_ConfigChannel(&hadc, &ch);

        // TIM2: roll over at 1 kHz and route that tick out as TRGO, which is what triggers the ADC
        htim.Instance           = TIM2;
        htim.Init.Prescaler     = 16 - 1;        // 16 MHz / 16 = 1 MHz tick
        htim.Init.Period        = 1000 - 1;      // 1 MHz / 1000 = 1 kHz
        htim.Init.CounterMode   = TIM_COUNTERMODE_UP;
        htim.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
        HAL_TIM_Base_Init(&htim);
        TIM_MasterConfigTypeDef mc = {0};
        mc.MasterOutputTrigger = TIM_TRGO_UPDATE;             // the 1 kHz roll-over becomes the trigger
        mc.MasterSlaveMode     = TIM_MASTERSLAVEMODE_DISABLE;
        HAL_TIMEx_MasterConfigSynchronization(&htim, &mc);

        HAL_NVIC_SetPriority(DMA2_Stream0_IRQn, 5, 0);   // let the DMA-complete interrupt reach the CPU
        HAL_NVIC_EnableIRQ(DMA2_Stream0_IRQn);

        // go: arm DMA first, then start the 1 kHz triggers
        HAL_ADC_Start_DMA(&hadc, (uint32_t *)&sample, 1);
        HAL_TIM_Base_Start(&htim);
    }

    uint16_t read() { return sample; }   // the freshest sample DMA wrote, no start/poll/stop

    void handleDmaIrq() { HAL_DMA_IRQHandler(&hdma); }   // DMA ISR routes here, which fires ConvCpltCallback

  private:
    volatile uint16_t sample = 0;   // DMA keeps this filled at 1 kHz
    ADC_HandleTypeDef hadc = {};
    DMA_HandleTypeDef  hdma = {};
    TIM_HandleTypeDef  htim = {};
};
#endif
