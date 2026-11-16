# 0046. Video

## Decision

File > Import Video puts a film on the timeline at the cursor (`Project::video`), and its sound on a track of its own when the Mac can read it. The lower zone's Video tab (there when the song has a video) shows it with AVFoundation through JUCE's VideoComponent, following the playhead: playing with the song, corrected when it drifts by more than 80 ms, still on the cursor's frame when stopped. Its own sound is muted; the track plays it. A marker in the Sections lane shows where it starts.

## Consequences

- One video a song. The picture is only on the Mac (JUCE has no video on Linux).
