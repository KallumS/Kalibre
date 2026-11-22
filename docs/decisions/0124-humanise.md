# 0124. Humanise

## Context

Asked (22 November): a right-click option for MIDI clips, Humanise, that
randomises the velocity and timing of notes by 10%.

## Decision

Right-click a MIDI clip (or several chosen): **Humanise**. Each note's
velocity moves up to 10% of itself either way (100 becomes 90 to 110), and
its start up to 10% of the editors' grid either way (the lower zone's Grid:
at 1/16, 24 ticks, a fortieth of a beat), its length kept, never before the
clip's content begins. A grid's tenth, not a beat's, so a tight part stays
tight and a loose one can be made looser by a coarser grid. One undo step,
a new roll each time (`edit::humanise`, seeded).

## Consequences

Tested in the core: 64 notes, every velocity within 10%, every start within a
tenth of a sixteenth, lengths kept, most of them moved, the same seed the
same notes.
