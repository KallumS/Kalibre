# 0135. The activity log

## Context

Record labels have begun asking for evidence that a song wasn't made by an
AI song generator (Suno and the like). The musician's idea: Kalibre already
says what's done in the message line, so keep a log of every action in the
song, saved as text beside it and brought up to date on each save, with a
way in the File menu to turn it off.

## Decision

- **What's a line.** Every undo step, by the name Undo gives it (the one
  place every change to the song goes through: `edit::History::onStep`), and
  every Undo and Redo; playing (from which bar), stopping (where), recording
  (on which tracks, from which bar); a new song and a song opened (with
  Kalibre's version); each save (with the top bar's music-making time so
  far); and what the message line said. A message said again straight after
  isn't written twice, and an export's running percentage is never written.
- **An AI agent's work is kept apart.** While an agent's tool runs
  (`Controller::agentActing`), its undo steps are "AI agent", not "Edit", and
  each tool that changes something is a line of its own ("Used
  set_plugin_parameter"), as some change the song without an undo step.
- **The file.** "<Song> Activity Log.txt", beside "<Song>.kalibre". Plain text
  anyone can read: a heading (the song, the Kalibre that wrote it, the time
  zone), then a line each: date and time, a word for what kind, what. Lines
  wait in memory (`activity::Log`, in the core) and are added to the file
  when the song is saved; nothing in the file is ever rewritten. Save As to a
  new name copies the log so far to the new name first.
- **The switch.** File > Keep an Activity Log, ticked when on. On unless the
  musician turns it off; kept between songs (Editing.json, with the
  transients' sensitivity). Off, nothing is kept or written; switched on
  again, a "Log" line says nothing was kept in between.

Not done: the log isn't sealed against editing (a checksum chain could show
that a line was changed or taken out, and Kalibre could check it); autosaves
and backups don't write it; what's done after the last save is lost if
Kalibre quits without saving, as the song's changes are.

## Consequences

- A change to the song that doesn't go through `History::push` isn't in
  the log: anything new that changes the song is an undo step, as every
  gesture already is (0008).
- Something that isn't an undo step but is worth having on record (as play
  and record are) calls `Controller::logActivity`.
- A message that's only progress is said with `logged = false`.
