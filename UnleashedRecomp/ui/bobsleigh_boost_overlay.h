#pragma once

// Speed-running / recording watermark for the AllowBobsleighBoost code.
//
// While the boost button is held during a bobsleigh ride, a small banner in the
// bottom-left corner shows the boost coefficient so that recorded footage makes
// clear the modified boost was in use. Boosting on foot does not trigger it:
// only the bobsleigh drive hook pings this overlay.
class BobsleighBoostOverlay
{
public:
    // Single source of truth for the sled boost multiplier: drives both the
    // gameplay speed/acceleration scaling in misc_patches.cpp and the value shown
    // on the watermark, so the two can never disagree.
    static constexpr double Coefficient = 1.75;

    static void Init();
    static void Draw();

    static void SetBoosting(bool boosting);

    // Called when external control (the ride) ends, for any reason.
    static void EndRide();
};
