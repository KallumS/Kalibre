# 0122. Sync changes the sound playing

## Context

Asked (22 November): in the Sounds tab, clicking Sync only took effect when
the sound was clicked again; it should change the sound playing, even half
way through.

## Decision

Sync switched while a sound plays makes it again (stretched to the song's
tempo, or back to its own) on the preview's own thread, and when it's ready
carries on from the same place in it, at once: not from its start, and not
waiting for the next beat (`PreviewPlayer::remake`). Until then the old one
plays on, so nothing stops.

## Consequences

Tested: a loop at its own tempo, a quarter through, Sync on at half its
tempo: twice as long, carrying on a quarter through, each block going half
as far.
