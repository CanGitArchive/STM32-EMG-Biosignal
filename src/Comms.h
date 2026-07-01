#ifndef COMMS_H
#define COMMS_H
#include "stm32f4xx_hal.h"
#include <stdio.h>     // snprintf

// Comms : one-way USB serial telemetry to the laptop (USART2 TX); the board reports state, takes no commands.
class Comms
{
  public:
    void init()
    {
        __HAL_RCC_GPIOA_CLK_ENABLE();      // the serial TX pin PA2 lives on port A
        __HAL_RCC_USART2_CLK_ENABLE();     // turn on power to serial port 2

        // PA2 = TX, handed to USART2 ("alternate function 7"). RX is unused; we only transmit.
        GPIO_InitTypeDef g = {0};
        g.Pin = GPIO_PIN_2; g.Mode = GPIO_MODE_AF_PP; g.Pull = GPIO_PULLUP;
        g.Speed = GPIO_SPEED_FREQ_VERY_HIGH; g.Alternate = GPIO_AF7_USART2;
        HAL_GPIO_Init(GPIOA, &g);

        huart.Instance          = USART2;
        huart.Init.BaudRate     = 115200;  // must match the laptop tool's baud rate
        huart.Init.WordLength   = UART_WORDLENGTH_8B;
        huart.Init.StopBits     = UART_STOPBITS_1;
        huart.Init.Parity       = UART_PARITY_NONE;
        huart.Init.Mode         = UART_MODE_TX;   // transmit only
        huart.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
        huart.Init.OverSampling = UART_OVERSAMPLING_16;
        HAL_UART_Init(&huart);
    }

    // Stream the raw sample, the on-chip centered value, and the signal-valid flag, as "raw,centered,valid".
    void sendStatus(uint16_t raw, int centered, bool valid)
    {
        char line[24];
        int length = snprintf(line, sizeof line, "%u,%d,%d\r\n", (unsigned)raw, centered, valid ? 1 : 0);
        HAL_UART_Transmit(&huart, (uint8_t *)line, length, 10);   // 10 = give-up timeout (ms)
    }

  private:
    UART_HandleTypeDef huart = {};
};
#endif
