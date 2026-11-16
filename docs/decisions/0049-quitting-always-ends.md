# 0049. Quitting always ends the program

## Context

On the Mac, Kalibre seemed to keep running after it was closed, and opened
again at login (16 November). Nothing in Kalibre starts it at login: the Mac
reopens apps that were still running when it shut down. Quitting unloads every
plug-in and closes the audio driver, and one that won't let go would leave a
Kalibre with no window running. On Linux it quits in under a second.

## Decision

When the app shuts down (after the project was saved or let go), a watchdog
thread ends the process after five seconds, whatever is still unwinding.

## Consequences

A plug-in that takes longer than five seconds to unload is cut off; what it
had was saved with the project before. Not yet confirmed on the Mac: if
Kalibre still reopens at login, it was open when the Mac shut down, and
System Settings > General > Login Items, or untick "Reopen windows when
logging back in" when shutting down, are the places to look.
