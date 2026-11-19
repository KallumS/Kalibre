# 0091. The top bar's timer: time spent making music in the song

## Context

Asked (18 November, night): a timer beside the chord in the top bar showing
how long the user has been making music in the song, using the clock from
their DAW Battle Pass (KallumS/DAW-Battle-Pass; `G.tick` in
reascripts/bp_game.lua, its decisions 0002 "active time, not wall time" and
0003 "the time is always passed in"; its `ableton` branch runs the same Lua).

## Decision

- **`Core/WorkClock`**, the battle pass's clock: each step counts the time
  since the last (a second at most, so a sleep or a long render never
  counts); the user is away after 3 minutes with nothing happening, 8 while
  the song plays back; recording always counts. As in the battle pass, up to
  3 minutes after walking away are counted rather than taken back.
- **What counts as something happening**: any edit (`Controller::apply`), the
  mouse moving, clicking, dragging or scrolling in Kalibre's windows (a global
  mouse listener, so the mouse over another app doesn't count, as the
  battle pass does with js_ReaScriptAPI), a key pressed, a note played on a
  MIDI keyboard, the transport starting or stopping.
- **Per song**: kept in the `.kalibre` file (`workSeconds`), written whenever
  it is saved (and in autosaves, backups and Collect All Files), carried on
  when it is opened. A new song, one from a template, or an opened
  DAWproject starts at 0:00. Undo and Redo don't move it.
- **On the top bar**, right of Chord, as **Making Music 1:02:09** (a
  stopwatch, hours shown once there are any); its label says **Paused**
  while away. Right-click the bar to hide it, as every item there (0070).

## Consequences

Tested: ten minutes of work, then twenty away, counts ten and three; an edit
starts it again; a song's time comes back when it is opened and is saved;
the timer sits just right of the chord; a new song starts at 0:00. The
clock's own tests run hours of it in a moment, as the battle pass's do.

Plug-in windows on the Mac are the plug-ins' own views: moving the mouse in
one may not reach Kalibre's listener. Changing a setting there still counts
when it's automated, and playing back counts for 8 minutes.
