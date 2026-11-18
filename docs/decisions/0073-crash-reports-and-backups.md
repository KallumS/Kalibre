# 0073. Crash reports and backups every 15 minutes

## Context

Asked (17 November): crash reports when Kalibre crashes, and backups of the
project every 15 minutes. Also asked for plug-in sandboxing, so a crashing
plug-in doesn't take Kalibre down: that is a larger change, not made here
(see the session log of 18 November).

## Decision

- **Crash reports**: on starting, handlers are set for the signals a crash
  raises (bad memory access, abort, illegal instruction, arithmetic, trap),
  on a stack of their own. The handler, using only calls safe in a signal
  handler, writes `Kalibre Crash <time>.txt` in Application Support/Kalibre/
  Crash Reports: what went wrong, the plug-in the audio thread was inside
  (each plug-in's wrapper notes its name as it plays, `crashcontext`), and
  the stack. Then the signal goes on as before: the Mac writes its own report
  and the program ends.
- On the next start the message line says Kalibre quit unexpectedly, and
  what (for example "a bad memory access, in Riffer"), with where the report
  and the backups are. Help > Show Crash Reports opens the folder.
- **Backups**: every 15 minutes, while the song has changed since the last
  one (and not while recording), a dated copy in the song's folder, in
  Backups (a song not yet saved: Application Support/Kalibre/Backups/
  Untitled). Named to the millisecond; the newest 48 kept (12 hours of work).
  File > Open a Backup lists the latest 12 and opens one as the song,
  unsaved, so saving puts it where the song is. The 3-minute autosave stays.

## Consequences

A crash on the main thread inside a plug-in's window isn't named (only the
stack shows it). Tested: a child process that crashes writes the report and
still ends with the signal; backups made, skipped when nothing changed,
pruned, opened as the song.
