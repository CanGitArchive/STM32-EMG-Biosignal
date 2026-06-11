// FreeRTOSConfig.h : tunes the kernel for THIS board (Nucleo-F446RE, HSI 16 MHz). FreeRTOS.h includes
// this by name. Settings only, no logic. Lives in include/ so both our code AND the kernel sources see it.
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

// --- the two numbers that MUST match the hardware ---
#define configCPU_CLOCK_HZ        ( 16000000UL )  // real core clock: HSI 16 MHz (NOT the 180 MHz max). Wrong value = every delay is wrong.
#define configTICK_RATE_HZ        ( ( TickType_t ) 1000 )  // scheduler heartbeat: 1000/sec = 1 ms per tick (matches HAL's 1 ms)

// --- scheduler behaviour ---
#define configUSE_PREEMPTION      1   // 1 = preemptive (kernel can yank a task for a higher-priority one). The whole point.
#define configUSE_TIME_SLICING    1   // equal-priority ready tasks share time, one tick each, round-robin
#define configMAX_PRIORITIES      5   // priority levels 0 (lowest) .. 4 (highest); we need ~4 (idle/watchdog/comms/servo)
#define configIDLE_SHOULD_YIELD   1   // the idle task steps aside at once when a real task becomes ready

// --- memory ---
#define configMINIMAL_STACK_SIZE  ( ( uint16_t ) 128 )   // words (x4 = 512 bytes): the idle task's stack, and our floor for task stacks
#define configTOTAL_HEAP_SIZE     ( ( size_t ) ( 10 * 1024 ) )  // 10 KB pool that task/queue creation carves from (heap_4.c). F446 has 128 KB RAM, so modest.
#define configMAX_TASK_NAME_LEN   16
#define configUSE_16_BIT_TICKS    0   // 0 = 32-bit tick counter (this is a 32-bit chip; 16 bits would overflow in ~65 s)

// --- features (each one pulls in code; off = smaller binary) ---
#define configUSE_MUTEXES               1
#define configUSE_COUNTING_SEMAPHORES   1
#define configUSE_TIMERS                0   // software timers OFF (we did not copy timers.c). Flip on later only if needed.
#define configUSE_CO_ROUTINES           0   // co-routines OFF (did not copy croutine.c); legacy, unrelated to tasks
#define configUSE_IDLE_HOOK             0   // no custom idle callback
#define configUSE_TICK_HOOK             0   // no custom per-tick callback
#define configCHECK_FOR_STACK_OVERFLOW  0   // off for now; can set to 2 later (needs a hook) during bring-up

// --- allocation model ---
#define configSUPPORT_DYNAMIC_ALLOCATION 1  // xTaskCreate / xQueueCreate allocate from the heap above
#define configSUPPORT_STATIC_ALLOCATION  0  // not hand-placing task memory; keeps this config simple

// --- which API functions to compile in (1 = include) ---
#define INCLUDE_vTaskDelay              1   // the sleep; we need it
#define INCLUDE_vTaskDelayUntil         1   // drift-free periodic delay (good for fixed-rate tasks)
#define INCLUDE_vTaskPrioritySet        1
#define INCLUDE_uxTaskPriorityGet       1
#define INCLUDE_vTaskSuspend            1
#define INCLUDE_vTaskDelete             1
#define INCLUDE_xTaskGetSchedulerState  1   // used to guard the FreeRTOS tick before the scheduler starts (SysTick wiring)

// --- catch config/usage mistakes during bring-up: freeze here so the debugger lands on the bad line ---
#define configASSERT( x ) if ( ( x ) == 0 ) { taskDISABLE_INTERRUPTS(); while(1); }

// --- Cortex-M NVIC interrupt-priority wiring (REQUIRED by the ARM_CM4F port) ---
// STM32F4 implements 4 priority bits. Reminder: LOWER number = MORE urgent.
#ifdef __NVIC_PRIO_BITS
    #define configPRIO_BITS  __NVIC_PRIO_BITS   // pull the real value from CMSIS (4 on STM32F4)
#else
    #define configPRIO_BITS  4
#endif

#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY        15  // 0xF: least-urgent level (kernel tick + PendSV sit here)
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY    5  // ISRs at this level or LESS urgent may call FreeRTOS "...FromISR" APIs

// the port wants these as raw 8-bit register values (the priority shifted into the high bits)
#define configKERNEL_INTERRUPT_PRIORITY \
        ( configLIBRARY_LOWEST_INTERRUPT_PRIORITY << ( 8 - configPRIO_BITS ) )
#define configMAX_SYSCALL_INTERRUPT_PRIORITY \
        ( configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << ( 8 - configPRIO_BITS ) )

// --- hand the CPU's exception vectors to the kernel ---
// The vector table calls SVC_Handler / PendSV_Handler by those exact names; point them at the port's code.
// NOTE: we deliberately do NOT remap SysTick here. Our own SysTick_Handler (main.cpp) will call BOTH
// HAL_IncTick() and the kernel's xPortSysTickHandler(), so HAL keeps its 1 ms clock. (Wired in sub-step 4.)
#define vPortSVCHandler     SVC_Handler
#define xPortPendSVHandler  PendSV_Handler

#endif // FREERTOS_CONFIG_H
