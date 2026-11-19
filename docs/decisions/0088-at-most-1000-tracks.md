# 0088. At most 1000 tracks

## Context

Asked (18 November, late): what's the most tracks Kalibre takes? There was
no limit. The user asked for 1000, and the message "Unable to add more than
1000 tracks" when someone tries to make the 1001st.

## Decision

- 1000 tracks at most (`edit::maxTracks`): ordinary tracks, folders,
  returns and Summing tracks count; the master and the Cue track don't.
- Checked in one place, `Controller::apply`: any change that would leave more
  than 1000 tracks is undone as a whole and the message line says "Unable to
  add more than 1000 tracks" (as a warning). A command that makes several
  (Explode Chords, the Note Splitter's New Tracks, an import) adds none
  rather than some. Scripts, opening a song or a template, and opening a
  DAWproject are checked the same way.

## Consequences

Tested: the 1000th is added, the 1001st refused with the message and nothing
to undo; New Tracks and a script refused alike.
