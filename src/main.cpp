// main.cpp : wires the parts together and runs the ~200 Hz loop. File layout: FIRMWARE_MAP.md
#include "stm32f4xx_hal.h"
#include "Emg.h"
#include "Servo.h"
#include "Comms.h"
#include "Timer.h"
#include "MuscleTrigger.h"
#include "Watchdog.h"

// the parts, as global objects (built cheaply here; real setup happens in init()/the starter)
static Emg           emg;
static Servo         servo;
static Comms         comms;
static Timer         timer;
static MuscleTrigger trigger;
static Watchdog      watchdog;

static uint16_t raw;        // latest muscle sample (0..4095)
static int      centered;   // latest centered value (raw minus the live baseline)
static volatile bool toggleRequested = false;   // the 1 kHz brain sets this; the loop acts on it

int main(void)
{
    HAL_Init();          // wake up the HAL (the 1 ms tick, flash settings)
    emg.init();
    servo.init();
    comms.init();
    timer.initialLoopTickStarter();   // start the loop timer now that HAL's clock is running
    watchdog.init();                  // arm the watchdog LAST, just before the loop starts petting it

    while (1)
    {
        raw = emg.read();                              // latest sample, for telemetry
        centered = trigger.centered();                 // latest centered value from the 1 kHz brain
        if (toggleRequested) { servo.toggle(); toggleRequested = false; }   // the brain flagged a flex
        servo.ease();                                  // glide toward the gripper's target every loop

        comms.sendStatus(raw, centered, trigger.isValid());      // raw, centered, signal-valid flag

        timer.waitForNextTick(5);                      // do the work, then wait out the rest of the 5 ms
        watchdog.pet();                                // a healthy iteration finished: reset the ~2 s countdown
    }
}

// Hardware interrupt handlers: the chip calls these by their exact names. extern "C" keeps the
// names un-mangled so the chip's jump-table finds them; each just pokes an object.
extern "C" void SysTick_Handler(void)   { HAL_IncTick(); }           // every 1 ms: keep HAL's clock ticking
extern "C" void USART2_IRQHandler(void) { comms.onByteReceived(); }  // a serial byte just arrived

// The brain now runs HERE, at 1 kHz: the DMA fires this each time it has a fresh sample (like SysTick,
// a handler the hardware calls, not a loop). It updates the detector and flags a flex for the loop.
extern "C" void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *h) { if (trigger.update(emg.read())) toggleRequested = true; }
extern "C" void DMA2_Stream0_IRQHandler(void)                  { emg.handleDmaIrq(); }
