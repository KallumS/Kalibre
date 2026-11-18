# 0074. Alias clips; the I/O button on track headers

## Context

Asked (18 November): right-click a MIDI clip > Create Alias Clip, so that
its duplicates are edited together, with a small symbol on alias clips. And
the routing (MIDI in, audio in and out, and the rest) out of the track's
right-click menu, next to the M, S and R buttons.

Linked clips already existed (0037: Duplicate Linked): clips sharing a
`link` id have their notes and audio kept the same after every edit
(`song::propagateLinks`), and a duplicate (Command-D, Option-drag) keeps the
id it was copied with.

## Decision

- "Alias" is the user's word for linked clips, and the menu now says it:
  **Create Alias Clip(s)** gives each chosen clip a link of its own, so
  every later duplicate of it is an alias; **Duplicate as Alias** (was
  Duplicate Linked); **Make Independent** (was Unlink).
- An alias clip has two links of a chain drawn at the right of its title.
- **I/O**, a button on every track header but the master's, beside M, S and
  R: one menu, each choice ticked: Audio In (stereo pairs, Mono…), MIDI In
  (every keyboard, none, each), MIDI Channel, MPE, Monitor Input, then Out
  (the master or its folder, folders and returns) and Send To. Its tooltip
  says where the track listens and plays to now. These left the track's
  right-click menu.

## Consequences

Tested: an alias's duplicates stay aliases and share an edit; Make
Independent lets one go; the I/O button's words. Rendered.
