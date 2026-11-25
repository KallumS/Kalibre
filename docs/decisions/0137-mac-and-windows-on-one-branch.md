# 0137. Mac and Windows on one branch

## Context

Kalibre for Windows was made on a branch of its own, `Windows` (0129), and
every change to the Mac version was merged across to it by hand. On 25
November the `Windows` branch was merged into `main` by accident. Nothing
broke: `Windows` already held every Mac change, its Windows code is behind
`JUCE_WINDOWS`, and its CI had been building and testing the Mac app as well.
Asked whether that's usual, the answer was that it is: programs for several
systems keep one main branch, the parts that differ marked for each, and
build every system from it. The musician chose to keep it.

## Decision

- `main` holds Kalibre for the Mac and for Windows. What's one system's alone
  is behind `JUCE_MAC` or `JUCE_WINDOWS` (or `platform::windows` in the
  core), as it already was on the `Windows` branch.
- CI builds and tests both from every push: the Mac's .dmg (Kalibre-macOS)
  and Windows' .zip (Kalibre-Windows).
- One version for both (0136); this is 1.1.0.
- The `Windows` branch isn't needed any more: wholly in `main`, the musician
  deletes it (25 November).
- A release is a tag `v<version>` on `main`; CI attaches both apps to it.

## Consequences

- A change is made once, for both. Words and keys on screen go through
  `ui::words()` (0129), and a change that only one system can show is tested
  on CI there.
- A failing Windows build stops a change as a failing Mac one does.
