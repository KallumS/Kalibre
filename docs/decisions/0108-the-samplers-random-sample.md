# 0108. The Sampler's Random Sample: any sound from the scanned folders

## Context

Asked (21 November): Kalibre Drums has Random Samples (0105), which fills its
pads with drums from the musician's scanned folders. The same for Kalibre
Sampler, but taking any kind of sample, not only drums.

## Decision

- **Random Sample**, under Load Sample in the Sampler's window: one sound
  from the Sounds tab's folders (the scan's index, `library::randomSound`),
  any kind: a loop, a one-shot, a note, a texture. Left out: MIDI files,
  what the scan couldn't read, empty files, longer than ten minutes (the
  Sampler refuses those), and the one it has, so each click is a new sound.
  A file gone since the scan is passed over (eight tries).
- Played in tune: when the scan heard one note in it (0095), the **Root
  Note** is set to that note, so the key it's played from sounds as written.
  A drum or a loop with no one note leaves the Root Note as it is. Tune,
  and every other knob, are left alone.
- **One undo step** each, as Kalibre Drums' edits are (`KalibreSampler::edit`,
  set by the Controller); **Load Sample** and a sound dropped on it are now
  one undo step too, and Undo back to no sample empties it (it kept the
  sample before).
- The message line names the sound, and its note when it set the Root Note.
  With no folders scanned, it says where to choose them.
- The choice is one pass over the index, keeping one path (reservoir
  sampling): nothing copied, so half a million sounds take no time.

## Consequences

Tested: in Core, only sounds that fit, every one of them in time, the same
for the same seed, never the one it has, nothing when there's nothing. In
the app: nothing scanned said and nothing changed; a pad heard as A3 loaded
with its Root Note A3; a loop next, the Root Note kept; each an undo step;
undone to the pad, then to none (Root back to C4); a dropped sound one undo
step. Hear it on the Mac.

- Amended 21 November: the macOS tests crashed (segfault, then a bus error
  a test earlier) though Linux passed. A plug-in's "my settings changed"
  (`updateHostDisplay`), which every sample loaded now sends, reached the
  engine as a message holding a raw `this`; when the test's engine, or its
  controller, was gone before it arrived, it ran on freed memory. The
  engine's messages now check its alive token, and the Controller clears the
  engine's callbacks when it goes. A test failed first (the callbacks left
  set; the message delivered to a gone engine) and passes now.
