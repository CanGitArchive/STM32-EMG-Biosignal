// main.cpp : FreeRTOS bring-up , one blink task proves the scheduler boots (A2). The EMG super-loop is at git 71af159; it returns as tasks in Step B.
#include "stm32f4xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"

extern "C" void xPortSysTickHandler(void);   // the kernel's tick, defined in the ARM_CM4F port

// The only task for now: blink the onboard LED (PA5 / LD2) at ~1 Hz. If LD2 blinks, the scheduler is alive.
static void blinkTask(void *params)
{
    (void)params;
    __HAL_RCC_GPIOA_CLK_ENABLE();
    GPIO_InitTypeDef led = {};
    led.Pin   = GPIO_PIN_5;
    led.Mode  = GPIO_MODE_OUTPUT_PP;
    led.Pull  = GPIO_NOPULL;
    led.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &led);

    while (1)
    {
        HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_5);
        vTaskDelay(pdMS_TO_TICKS(500));   // toggle every 500 ms -> ~1 Hz blink
    }
}

int main(void)
{
    HAL_Init();   // HAL + its 1 ms SysTick

    xTaskCreate(blinkTask, "blink", 128, nullptr, 1, nullptr);   // 128 words of stack, priority 1 (above idle's 0)
    vTaskStartScheduler();   // hands the CPU to the kernel; does NOT return while the scheduler runs

    while (1) { }   // only reached if the scheduler could not start (e.g. heap too small)
}

// SysTick now drives BOTH clocks: HAL's millisecond counter, and (once the scheduler is up) the kernel tick.
// The guard stops xPortSysTickHandler from firing before vTaskStartScheduler has set the kernel up.
extern "C" void SysTick_Handler(void)
{
    HAL_IncTick();
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
    {
        xPortSysTickHandler();
    }
}
