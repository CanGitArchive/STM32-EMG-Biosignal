// main.cpp : B2 + queue , servoTask (~200 Hz) + commsTask (~50 Hz) + watchdogTask. The brain hands each flex to servoTask through a FreeRTOS queue. The 1 kHz brain stays in the DMA callback. File map: FIRMWARE_MAP.md
#include "stm32f4xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include <stdio.h>          // snprintf, for the step-1 CAN probe message (temporary)
#include "Emg.h"
#include "Servo.h"
#include "Comms.h"
#include "MuscleTrigger.h"
#include "Watchdog.h"
#include "Mcp2515CanBus.h"

extern "C" void xPortSysTickHandler(void);   // the kernel tick, defined in the ARM_CM4F port

// the parts (built cheaply here; real setup happens in init())
static Emg           emg;
static Servo         servo;
static Comms         comms;
static MuscleTrigger trigger;
static Watchdog      watchdog;
static Mcp2515CanBus canBus;

// The brain (DMA ISR) drops one token in here per valid flex; servoTask waits on it. This replaces the
// old volatile flag: a queue is the safe ISR->task hand-off, it does the locking, so no manual volatile.
static QueueHandle_t mailBox;

// servoTask : highest priority. Waits on the flex queue, toggles on a flex, eases every cycle.
static void servoTask(void *params)
{
    (void)params;
    uint8_t token;
    while (1)
    {
        // block up to 5 ms for a flex; the timeout doubles as the ~200 Hz pacing (replaces vTaskDelay)
        if (xQueueReceive(mailBox, &token, pdMS_TO_TICKS(5)) == pdTRUE)
        {
            servo.toggle();   // a flex arrived
        }
        servo.ease();         // glide toward the target every cycle
    }
}

// commsTask : telemetry to the laptop viewer. ~50 Hz is plenty (the viewer refreshes ~30/sec).
static void commsTask(void *params)
{
    (void)params;
    while (1)
    {
        uint16_t raw      = emg.read();
        int      centered = trigger.centered();
        comms.sendStatus(raw, centered, trigger.isValid());
        vTaskDelay(pdMS_TO_TICKS(20));   // ~50 Hz
    }
}

// watchdogTask : lowest priority. Pets the IWDG every 500 ms (well inside the ~2 s timeout). Being lowest
// IS the safety: if a higher task hangs and never yields, this never runs -> never pets -> the chip reboots.
static void watchdogTask(void *params)
{
    (void)params;
    while (1)
    {
        watchdog.pet();
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

int main(void)
{
    HAL_Init();          // HAL + its 1 ms SysTick
    emg.init();
    servo.init();
    comms.init();

    // --- STEP 3a PROBE (temporary scaffolding; delete once CAN bring-up is verified) ------------------
    // Loopback test: the chip routes its own transmit back into its own receiver (no bus wires, no second
    // node, no ACK). Build a frame, send it, read it back, print both: they should match. This is the
    // "send == read" bounce. Still SPI-only on the wire. Loop never returns (FreeRTOS firmware paused).
    canBus.init();
    canBus.reset();
    canBus.setBitTiming8MHz500k();      // bit timing is writable only in config mode (now, before we leave it)
    canBus.acceptAllOnRxBuffer0();
    canBus.enterLoopbackMode();
    uint8_t counter = 0;
    while (1)
    {
        uint8_t txData[4] = { 0x45, 0x4D, 0x47, counter };   // 'E' 'M' 'G' + a counter (matches the bench frame)
        canBus.sendFrame(0x100, txData, 4);
        HAL_Delay(5);                                        // let the loopback land in the RX buffer

        char line[96];
        if (canBus.messageWaiting())
        {
            uint16_t rxId;
            uint8_t  rxData[8];
            uint8_t  len = canBus.readFrame(&rxId, rxData);
            snprintf(line, sizeof line, "sent 0x100 [45 4D 47 %02X] -> got 0x%03X [%02X %02X %02X %02X] len=%u\r\n",
                     counter, rxId, rxData[0], rxData[1], rxData[2], rxData[3], len);
        }
        else
        {
            snprintf(line, sizeof line, "sent 0x100 counter=%02X -> NOTHING received\r\n", counter);
        }
        comms.sendLine(line);
        counter++;
        HAL_Delay(1000);
    }
    // --- end step-3a probe -------------------------------------------------------------------------

    watchdog.init();     // arm the IWDG; watchdogTask feeds it

    mailBox = xQueueCreate(4, sizeof(uint8_t));   // up to 4 pending flexes, 1 byte each
    configASSERT(mailBox != NULL);                // out of heap -> stop here so it's obvious

    // priorities (higher = more urgent; idle is 0): servo preempts comms preempts watchdog.
    xTaskCreate(servoTask,    "servo",    256, nullptr, 3, nullptr);   // highest: actuation must not be delayed
    xTaskCreate(commsTask,    "comms",    512, nullptr, 2, nullptr);   // middle: telemetry can wait for the servo
    xTaskCreate(watchdogTask, "watchdog", 128, nullptr, 1, nullptr);   // lowest: starved if anything above hangs
    vTaskStartScheduler();                                             // hand the CPU to the kernel; does NOT return

    while (1) { }        // only reached if the scheduler could not start
}

// Interrupt handlers. SysTick feeds BOTH HAL and the kernel. The 1 kHz brain runs in the ADC/DMA callback.
extern "C" void SysTick_Handler(void)
{
    HAL_IncTick();
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) { xPortSysTickHandler(); }
}
extern "C" void USART2_IRQHandler(void)  { comms.onByteReceived(); }

// The 1 kHz brain. On a valid flex, hand a token to servoTask via the queue, then yield so the higher-
// priority servoTask runs the instant this ISR exits. The DMA interrupt is priority 5 (the syscall
// ceiling), so calling xQueueSendFromISR from here is allowed.
extern "C" void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *h)
{
    if (mailBox != NULL && trigger.update(emg.read()))   // mailBox is NULL until the scheduler is set up
    {
        uint8_t token = 1;
        BaseType_t higherPriorityTaskWoken = pdFALSE;
        xQueueSendFromISR(mailBox, &token, &higherPriorityTaskWoken);
        portYIELD_FROM_ISR(higherPriorityTaskWoken);
    }
}
extern "C" void DMA2_Stream0_IRQHandler(void) { emg.handleDmaIrq(); }
