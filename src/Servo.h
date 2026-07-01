// Servo : the SG90 gripper (PWM on PB6); open/close/toggle pick a position, ease() glides toward it each loop.
#ifndef SERVO_H
#define SERVO_H
#include "stm32f4xx_hal.h"

class Servo
{
  public:
    void init()
    {
        __HAL_RCC_GPIOB_CLK_ENABLE();      // turn on power to port B (PB6 lives here)
        __HAL_RCC_TIM4_CLK_ENABLE();       // turn on power to timer 4 (it makes the PWM)

        // hand PB6 over to timer 4 ("alternate function 2") so the timer can drive the pin
        GPIO_InitTypeDef g = {0};
        g.Pin = GPIO_PIN_6; g.Mode = GPIO_MODE_AF_PP; g.Pull = GPIO_NOPULL;
        g.Speed = GPIO_SPEED_FREQ_LOW; g.Alternate = GPIO_AF2_TIM4;
        HAL_GPIO_Init(GPIOB, &g);

        // tick once per microsecond (16 MHz / 16), roll over every 20 ms = 50 Hz servo frame
        htim.Instance           = TIM4;
        htim.Init.Prescaler     = 16 - 1;
        htim.Init.CounterMode   = TIM_COUNTERMODE_UP;
        htim.Init.Period        = 20000 - 1;
        htim.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
        HAL_TIM_PWM_Init(&htim);

        TIM_OC_InitTypeDef oc = {0};
        oc.OCMode = TIM_OCMODE_PWM1; oc.Pulse = OPEN; oc.OCPolarity = TIM_OCPOLARITY_HIGH;
        HAL_TIM_PWM_ConfigChannel(&htim, &oc, TIM_CHANNEL_1);
        HAL_TIM_PWM_Start(&htim, TIM_CHANNEL_1);

        targetPosition = currentPosition = OPEN;
        closed = false;
    }

    void open()   { closed = false; targetPosition = OPEN;  }   // choose the open position
    void close()  { closed = true;  targetPosition = CLOSE; }   // choose the closed position
    void toggle() { if (closed) open(); else close(); }         // flip between them

    bool isClosed() const { return closed; }                    // current state (for CAN/serial telemetry)

    // Call every loop: move at most 'slew' us toward the target, then write it to the PWM.
    void ease()
    {
        int step = targetPosition - currentPosition;   // how far we still have to go
        if (step >  slew) step =  slew;                // ...but never more than 'slew' per call,
        if (step < -slew) step = -slew;                //    in either direction
        currentPosition += step;                       // (step is 0 when we're already there)
        __HAL_TIM_SET_COMPARE(&htim, TIM_CHANNEL_1, currentPosition);
    }

    // Adjust how fast the gripper moves: microseconds of travel per step (bigger = faster).
    void setRotationSpeed(int speed) { slew = clampToRange(speed, 1, 20); }

  private:
    // Pulse widths in us, measured for OUR gripper (within the SG90's safe ~1270-1620 band).
    static const int OPEN = 1300, CLOSE = 1600;

    static int clampToRange(int value, int low, int high)
    {
        if (value < low)  return low;
        if (value > high) return high;
        return value;
    }

    TIM_HandleTypeDef htim = {};
    int  slew = 4;                  // microseconds moved per step = rotation speed
    int  targetPosition  = OPEN;    // where we want to be
    int  currentPosition = OPEN;    // where we are now (eased toward the target)
    bool closed = false;            // current gripper state
};
#endif
