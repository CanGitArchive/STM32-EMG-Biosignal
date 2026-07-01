// main.cpp : the standalone myoelectric gripper under FreeRTOS (servo, comms, CAN, watchdog tasks + a 1 kHz brain in the DMA ISR).
#include "stm32f4xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
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

// The brain (DMA ISR) drops one token per flex; servoTask waits on it. A queue is the safe ISR-to-task hand-off.
static QueueHandle_t mailBox;
// A second copy of the same flex signal, for canTask to announce the gesture over CAN immediately.
static QueueHandle_t canMailBox;

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
        comms.sendStatus(raw, centered, trigger.isElectrodeAttached());
        vTaskDelay(pdMS_TO_TICKS(20));   // ~50 Hz
    }
}

// canTask : a queued flex token sends a gesture frame (0x100); a 200 ms timeout sends a status heartbeat (0x101).
static void canTask(void *params)
{
    (void)params;
    uint8_t token;
    while (1)
    {
        // Sleep here until either a flex token arrives OR 200 ms passes (then it's a heartbeat).
        bool gesture = (xQueueReceive(canMailBox, &token, pdMS_TO_TICKS(200)) == pdTRUE);

        uint8_t status[2] = { (uint8_t)servo.isClosed(),                 // bool -> 1 (closed) / 0 (open)
                              (uint8_t)trigger.isElectrodeAttached() };  // bool -> 1 (attached) / 0 (detached)

        uint16_t frameId;
        if (gesture)
            frameId = 0x100;   // a flex just happened -> immediate gesture event
        else
            frameId = 0x101;   // nothing new -> periodic status heartbeat
        canBus.sendFrame(frameId, status, 2);   // both frames carry the same 2 status bytes; only the ID differs
    }
}

// watchdogTask : lowest priority, pets the IWDG every 500 ms; if a higher task hangs it starves and the chip reboots.
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
    watchdog.init();     // arm the IWDG; watchdogTask feeds it

    // CAN: bring up the MCP2515 over SPI2 and go live on the bus (Normal mode). canTask broadcasts status.
    canBus.init();
    canBus.reset();
    canBus.setBitTiming8MHz500k();   // 8 MHz crystal @ 500 kbps; must match the other node(s)
    canBus.enterNormalMode();

    canMailBox = xQueueCreate(4, sizeof(uint8_t));   // brain -> canTask : announce each flex over CAN
    mailBox    = xQueueCreate(4, sizeof(uint8_t));   // brain -> servoTask : act on each flex
    configASSERT(mailBox != NULL && canMailBox != NULL);   // out of heap -> stop here so it's obvious
    // (canMailBox is created first, so when the ISR sees mailBox != NULL, canMailBox is ready too.)

    // priorities (higher = more urgent; idle is 0): servo preempts comms/can preempts watchdog.
    xTaskCreate(servoTask,    "servo",    256, nullptr, 3, nullptr);   // highest: actuation must not be delayed
    xTaskCreate(commsTask,    "comms",    512, nullptr, 2, nullptr);   // serial telemetry
    xTaskCreate(canTask,      "can",      256, nullptr, 2, nullptr);   // CAN status heartbeat
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

// The 1 kHz brain: on a valid flex, queue a token to servoTask and canTask, then yield to the woken task.
extern "C" void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *h)
{
    if (mailBox != NULL && trigger.update(emg.read()))   // queues are NULL until the scheduler is set up
    {
        uint8_t token = 1;
        BaseType_t higherPriorityTaskWoken = pdFALSE;
        xQueueSendFromISR(mailBox,    &token, &higherPriorityTaskWoken);   // wake servoTask (toggle)
        xQueueSendFromISR(canMailBox, &token, &higherPriorityTaskWoken);   // wake canTask (announce over CAN)
        portYIELD_FROM_ISR(higherPriorityTaskWoken);
    }
}
extern "C" void DMA2_Stream0_IRQHandler(void) { emg.handleDmaIrq(); }
