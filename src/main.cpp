// main.cpp : wires the parts together and runs the ~200 Hz loop. File layout: FIRMWARE_MAP.md
#include "stm32f4xx_hal.h"
#include "Emg.h"
#include "Servo.h"
#include "Comms.h"
#include "Timer.h"
#include "MuscleTrigger.h"

// the parts, as global objects (built cheaply here; real setup happens in init()/the starter)
static Emg           emg;
static Servo         servo;
static Comms         comms;
static Timer         timer;
static MuscleTrigger trigger;

static uint16_t raw;        // latest muscle sample (0..4095)
static int      centered;   // latest centered value (raw minus the live baseline)

int main(void)
{
    HAL_Init();          // wake up the HAL (the 1 ms tick, flash settings)
    emg.init();
    servo.init();
    comms.init();
    timer.initialLoopTickStarter();   // start the loop timer now that HAL's clock is running

    while (1)
    {
        raw = emg.read();                              // one muscle sample (0..4095)
        if (trigger.update(raw)) servo.toggle();       // a valid flex flips the gripper
        centered = trigger.centered();                 // latest centered value, for the telemetry stream
        servo.ease();                                  // glide toward the gripper's target every loop

        comms.sendStatus(raw, centered, trigger.isValid());      // raw, centered, signal-valid flag

        timer.waitForNextTick(5);                      // do the work, then wait out the rest of the 5 ms
    }
}

// Hardware interrupt handlers: the chip calls these by their exact names. extern "C" keeps the
// names un-mangled so the chip's jump-table finds them; each just pokes an object.
extern "C" void SysTick_Handler(void)   { HAL_IncTick(); }           // every 1 ms: keep HAL's clock ticking
extern "C" void USART2_IRQHandler(void) { comms.onByteReceived(); }  // a serial byte just arrived
