/*
    Timeline - ticks to points and back, for anything drawn along time. The
    arrangement's ruler and lanes share one, so bars line up; the piano roll
    and the pattern editors have their own zoom.
*/

#pragma once

#include "TempoMap.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace kal
{

struct Timeline
{
    double pixelsPerTick = 0.03;   // about 115 points a bar of 4/4
    double startTick = 0;          // at x = 0

    float x (Tick t) const { return (float) ((double (t) - startTick) * pixelsPerTick); }
    Tick tick (float px) const { return (Tick) std::llround (startTick + px / pixelsPerTick); }
    Tick ticksFor (float points) const { return (Tick) std::llround (points / pixelsPerTick); }

    // A step for grid lines that keeps them at least `minPoints` apart.
    Tick lineStep (const Meter& m, float minPoints = 12.0f) const
    {
        Tick step = PPQ / 8;
        const Tick bar = m.barTicks();
        while ((float) (step * pixelsPerTick) < minPoints && step < bar) step *= 2;
        if (step >= bar) { step = bar; while ((float) (step * pixelsPerTick) < minPoints) step += bar; }
        return step;
    }

    void zoomAround (float px, double factor)
    {
        const double at = startTick + px / pixelsPerTick;
        pixelsPerTick = juce::jlimit (0.0005, 2.0, pixelsPerTick * factor);
        startTick = std::max (0.0, at - px / pixelsPerTick);
    }
};

} // namespace kal
