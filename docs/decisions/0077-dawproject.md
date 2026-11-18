# 0077. DAWproject in and out

## Context

Asked (18 November): DAWproject import and export. DAWproject is the open
format Bitwig and PreSonus published (github.com/bitwig/dawproject): a zip
of project.xml, metadata.xml, the audio and plug-in states, read and
written by Bitwig Studio, Studio One, Cubase 14 and others.

## Decision

- `Core/DawProject` writes and reads the XML (with Kalibre's XML reader and
  writer, no JUCE); the window does the zip with JUCE. Times in beats
  (Kalibre's ticks over 960); volume linear (0 dB = 1), pan 0 to 1.
- **Carried**: tracks, folders (nested), returns and the master; volume,
  pan, mute, solo, colour, outputs, sends (pre or post, level, on);
  tempo and time signature, and their changes (tempo ramps as linear);
  markers; MIDI clips with notes (channel, velocity) and controllers, the
  offset and a loop from the start; audio clips (where in the file, fades),
  the files copied in (`audio/`); song details (metadata.xml).
- **Plug-ins**: VST3, AU and CLAP by kind, name and maker, matched on
  opening to the plug-ins on this Mac by name (the same format first).
  Kalibre's own devices are written as built-in devices with their state.
  Other plug-ins' states are JUCE's, not .vstpreset or CLAP presets, so they
  go in a side file only Kalibre reads (`kalibre/states/`), and a Kalibre
  file opened in Kalibre keeps them. Another program's built-in devices
  (Bitwig's EQ+) are left out, and the message line says so.
- **Not yet**: automation, warping (audio comes in unwarped), takes,
  versions, the clip launcher, other programs' plug-in settings.
- File > Open a DAWproject… opens one as a new, unsaved song (its audio
  extracted beside it, in "<name> Audio"); File > Export a DAWproject….
- Bitwig's layouts read too: lanes inside lanes, notes inside a clip's
  lanes, audio inside warps, clips inside a clip.

## Consequences

Tested: a song written and read back; Bitwig's layout; in the app, a song
exported with audio and Kalibre Synth's state and opened again. The written
XML validates against the format's own schema (Project.xsd, MetaData.xsd),
checked with xmllint while building this; not yet on CI. Not yet tried with
Bitwig, Studio One or Cubase themselves.
