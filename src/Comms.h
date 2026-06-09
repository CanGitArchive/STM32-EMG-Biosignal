#ifndef COMMS_H
#define COMMS_H
#include "stm32f4xx_hal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>     // strncpy, for copying the received text (no std::string / no heap)

// Comms : USB serial to the laptop (USART2). Streams samples; its interrupt catches "S<us>" commands.
class Comms
{
  public:
    void init()
    {
        __HAL_RCC_GPIOA_CLK_ENABLE();      // the serial pins PA2/PA3 live on port A
        __HAL_RCC_USART2_CLK_ENABLE();     // turn on power to serial port 2

        // PA2 = TX, PA3 = RX, both handed to USART2 ("alternate function 7")
        GPIO_InitTypeDef g = {0};
        g.Pin = GPIO_PIN_2 | GPIO_PIN_3; g.Mode = GPIO_MODE_AF_PP; g.Pull = GPIO_PULLUP;
        g.Speed = GPIO_SPEED_FREQ_VERY_HIGH; g.Alternate = GPIO_AF7_USART2;
        HAL_GPIO_Init(GPIOA, &g);

        huart.Instance          = USART2;
        huart.Init.BaudRate     = 115200;  // must match the laptop tool's baud rate
        huart.Init.WordLength   = UART_WORDLENGTH_8B;
        huart.Init.StopBits     = UART_STOPBITS_1;
        huart.Init.Parity       = UART_PARITY_NONE;
        huart.Init.Mode         = UART_MODE_TX_RX;
        huart.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
        huart.Init.OverSampling = UART_OVERSAMPLING_16;
        HAL_UART_Init(&huart);

        __HAL_UART_ENABLE_IT(&huart, UART_IT_RXNE);   // interrupt the instant a byte arrives
        HAL_NVIC_SetPriority(USART2_IRQn, 2, 0);
        HAL_NVIC_EnableIRQ(USART2_IRQn);
    }

    // Send one reading as a line of text, e.g. "1850\r\n" (the format the laptop tools expect).
    void sendDataCOM(uint16_t value)
    {
        char line[12];
        int length = snprintf(line, sizeof line, "%u\r\n", (unsigned)value);
        HAL_UART_Transmit(&huart, (uint8_t *)line, length, 10);   // 10 = give-up timeout (ms)
    }

    // Stream the raw sample, the on-chip centered value, and the signal-valid flag, as "raw,centered,valid".
    void sendStatus(uint16_t raw, int centered, bool valid)
    {
        char line[24];
        int length = snprintf(line, sizeof line, "%u,%d,%d\r\n", (unsigned)raw, centered, valid ? 1 : 0);
        HAL_UART_Transmit(&huart, (uint8_t *)line, length, 10);
    }

    // Called from the USART2 interrupt, one byte at a time. Builds up a line; on a newline it
    // parses the number, remembers it (debug), and if it was tagged 'S' uses it for the servo.
    void onByteReceived()
    {
        if (__HAL_UART_GET_FLAG(&huart, UART_FLAG_RXNE))
        {
            char c = (char)(huart.Instance->DR & 0xFF);   // reading the data register clears the flag
            if (c == '\n' || c == '\r')
            {
                receivedChars[receivedCount] = 0;          // end the string
                strncpy(lastReceivedText, receivedChars, sizeof(lastReceivedText) - 1);
                lastReceivedText[sizeof(lastReceivedText) - 1] = 0;   // remember the whole line (debug)
                int number = atoi(receivedChars + 1);      // the number after the leading tag
                lastReceivedNumber = number;               // remember it (any tag) - for debugging
                if (receivedChars[0] == 'S')
                    receivedServoValue = number;           // only 'S' commands move the servo
                receivedCount = 0;
            }
            else if (receivedCount < (int)sizeof(receivedChars) - 1)
            {
                receivedChars[receivedCount++] = c;
            }
        }
    }

    int receivedDataForServo() const { return receivedServoValue; }   // latest commanded servo position
    int lastReceived() const { return lastReceivedNumber; }   // last number that arrived (debug)
    const char *lastText() const { return lastReceivedText; }   // last full line that arrived (debug)

  private:
    UART_HandleTypeDef huart = {};
    char receivedChars[16] = {};        // we assemble one command line here, char by char
    int  receivedCount = 0;             // how many chars are in it so far
    volatile int receivedServoValue = 1300;   // = Servo's OPEN position; set by "S<n>" commands
    volatile int lastReceivedNumber = 0;       // last number received over serial (any tag)
    char lastReceivedText[16] = {};             // last full line received over serial (debug)
};
#endif
