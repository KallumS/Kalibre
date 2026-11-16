# 0044. Spectrum, oscilloscope, phase scope and tuner

## Decision

The lower zone's Meters tab (Y). The master's fader and the chosen track's keep their last 8192 samples in a lock-free tap; the window reads them 30 times a second. Spectrum: a 4096-point FFT on a log scale with held peaks. Oscilloscope: 40 ms from a rising zero. Phase: mid against side, with the correlation. Tuner: YIN on the chosen track, note name and cents (`Core/AudioTools`, tested).
