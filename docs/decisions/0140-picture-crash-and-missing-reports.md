# 0140. Take Pictures' crash, and crashes with no report

## Context

The musician (26 November, 1.3.0 on a MacBook Air): Kalibre quit while Take
Pictures was going through the plug-ins, and wrote no crash report. macOS's
own report said: the main thread, a bad memory access at address 0, in
`objc_msgSend` from `objc_autoreleasePoolPop`, at the end of a message.
Hundreds of plug-ins' bundles were loaded, among them Softube's (PACE copy
protection) and u-he's.

- **The crash.** macOS releases the objects a message made only when the
  message ends (its autorelease pool). Take Pictures closed a plug-in's
  window and deleted the plug-in in the same message, so the window's last
  pieces were released after their plug-in had gone. The engine never does
  this: it keeps a removed plug-in at least a second and a half after its
  window (0110, 0115). The picture-taker didn't.
- **No report.** Kalibre's crash handler (0073) is a signal handler. A
  plug-in loaded into Kalibre can set its own for the same signals, and many
  copy-protected ones do: the last to set it gets the crash, and Kalibre's
  handler never ran.

## Decision

- **Take Pictures lets go of each plug-in two seconds after its window has
  closed**, in a message of its own (`Taker::letGo`), as the engine does.
  Its window closes in one message, the plug-in is put aside in the next,
  and deleted on the picture-taker's timer once its time has come. The
  picture-taker is kept between runs, so what it's still to let go of goes
  in its time. A test checks the plug-in is deleted at least 1.5 s after its
  window, in a later message.
- **A crash is noticed even without Kalibre's report.** Each start writes
  `Running.txt` in the Crash Reports folder, and quitting deletes it. A start
  that finds it knows the last run ended unexpectedly. If Kalibre's own
  report of it is missing, the newest of macOS's (`Kalibre*.ips` in
  ~/Library/Logs/DiagnosticReports, newer than that start) is copied into
  the Crash Reports folder, with a report of Kalibre's beside it: what went
  wrong in Kalibre's words, the crashed thread's calls, the version that
  crashed. The message line tells of it as of any crash. With none found,
  it says Kalibre quit unexpectedly and no report was found.
- **The plug-in being photographed** (`Taking.txt`, 0138) is named in that
  message, and left out of Take Pictures from then on.
- Kalibre doesn't take the signals back from plug-ins: copy protection may
  rely on its own handler, and a plug-in that stopped working for it would
  be worse than a report brought in later.

## Consequences

- Windows and Linux have no system report to bring in: there, a run that
  ended unexpectedly with no report is still told of.
- Force Quit, or the Mac losing power, is told of as an unexpected end too.
- Tested: the plug-in let go of later than its window; a run that ended
  without quitting found; macOS's report (one shaped like the musician's)
  brought in and summed up as "a bad memory access", an older one left; the
  photographed plug-in named once. Untried on the Mac until the musician
  runs Take Pictures again.
