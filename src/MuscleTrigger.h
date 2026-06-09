// MuscleTrigger : on-chip baseline tracking + dip detection. update() returns true on the one
// sample a valid muscle "dip" should toggle the gripper. Ignores garbage (failsafe) and double-fires.
#ifndef MUSCLE_TRIGGER_H
#define MUSCLE_TRIGGER_H
#include "stm32f4xx_hal.h"

class MuscleTrigger
{
  public:
    // Feed one raw sample per loop. Returns true exactly once per valid flex (a "dip").
    // While the signal is invalid (electrode unplugged / railing) it returns false and holds.
    bool update(uint16_t raw)
    {
        samplesSinceRearm++;

        // --- failsafe: is the signal trustworthy right now? ---
        bool badSample = (raw > RAIL_HIGH) || (raw < RAIL_LOW);   // a real EMG signal never goes here
        if (badSample) cleanSamples = 0;                          // any garbage -> distrust immediately
        else if (cleanSamples < VALID_AFTER) cleanSamples++;      // count clean samples back up
        electrodesConnected = (cleanSamples >= VALID_AFTER);      // trust only after ~1 s fully clean

        if (!electrodesConnected)
        {
            lastCentered = (int)((float)raw - baseline);   // still report it for telemetry, but...
            armed = false;                                 // ...require a fresh re-arm when it returns,
            return false;                                  // and DON'T touch baseline (no garbage in it)
        }

        // --- track the resting baseline and center the signal ---
        if (!baselineInitialized) { baseline = raw; baselineInitialized = true; }
        baseline += RATE * ((float)raw - baseline);
        lastCentered = (int)((float)raw - baseline);

        // --- dip detection: fire once per flex ---
        bool fired = false;
        if (armed && lastCentered <= -DIP_THRESHOLD && samplesSinceRearm >= LOCKOUT)
        {
            fired = true;
            armed = false;            // disarm: no fire until released
        }
        if (!armed && lastCentered > -REARM_LEVEL)   // the release AFTER a fire: re-arm AND start the lockout
        {
            armed = true;
            samplesSinceRearm = 0;    // count the bounce-guard window from the RELEASE, not the fire
        }
        return fired;
    }

    int  centered() const { return lastCentered; }            // latest centered value (telemetry)
    bool isValid()  const { return electrodesConnected; }     // is the signal trusted right now?

  private:
    const float RATE          = 0.001f;  // baseline tracker speed (~5 s to follow drift at 200 Hz)
    const int   DIP_THRESHOLD = 425;     // centered must dip this far below rest to count as a flex
    const int   REARM_LEVEL   = 150;     // ...and climb back above -this to re-arm for the next one
    const int   LOCKOUT       = 25;      // samples (~125 ms at 200 Hz) after a release before another fire counts
    const int   RAIL_HIGH     = 3000;    // raw above this = bad signal (a real flex peaks ~2346)
    const int   RAIL_LOW      = 25;      // raw below this = bad signal (a real flex bottoms ~89)
    const int   VALID_AFTER   = 200;     // need this many clean samples in a row (~1 s) to trust it

    float baseline = 0.0f;               // the live resting level
    bool  baselineInitialized = false;
    bool  electrodesConnected = false;   // is the signal currently trustworthy? (set each update)
    bool  armed = true;                  // ready to fire? (disarms after a fire, re-arms on release)
    int   samplesSinceRearm = 1000;      // loop counts since the last release/re-arm (starts past the lockout)
    int   cleanSamples = 0;              // consecutive non-bad samples (0 = distrust the signal)
    int   lastCentered = 0;              // most recent centered value
};
#endif
