// Notch : 50 Hz biquad notch on the centered signal, kills mains hum. filter() once per sample.
#ifndef NOTCH_H
#define NOTCH_H

class Notch
{
  public:
    float filter(float x)
    {
        float y = B0*x + B1*x1 + B2*x2 - A1*y1 - A2*y2;
        x2 = x1; x1 = x;
        y2 = y1; y1 = y;
        return y;
    }

  private:
    // A 50 Hz notch at the 1 kHz sample rate, Q = 2.5; recompute for a different rate or mains frequency.
    const float B0 =  0.9417923f;
    const float B1 = -1.7913934f;
    const float B2 =  0.9417923f;
    const float A1 = -1.7913934f;
    const float A2 =  0.8835919f;
    float x1 = 0, x2 = 0, y1 = 0, y2 = 0;   // filter memory: last two inputs and outputs
};
#endif
