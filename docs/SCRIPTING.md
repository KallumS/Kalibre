# Scripting Kalibre

Scripts are Lua 5.4 files. Put them in
`~/Library/Application Support/Kalibre/Scripts` (Scripts > Show the Scripts
Folder) and they appear in the Scripts menu. A script run is one undo step; if
it fails, nothing it did is kept. One that runs for more than five seconds is
stopped.

Positions are in ticks: `kalibre.ppq` (960) to a quarter note. Tracks, clips
and notes are found by their id.

| Function | What it does |
| --- | --- |
| `kalibre.tracks()` | Every track: `{id, name, kind, parent, volume, pan, mute, solo, delay, colour, version, versions}` |
| `kalibre.track(id)` | One track |
| `kalibre.addTrack(name, kind, parent)` | `kind` is "track", "folder" or "return"; returns its id |
| `kalibre.setTrack(id, {name=, volume=, pan=, mute=, solo=, delay=, colour=})` | Change a track |
| `kalibre.removeTrack(id)` | |
| `kalibre.newVersion(id)` | A new version of the track, copied; returns its number |
| `kalibre.clips(trackId)` | `{id, name, kind, start, length, muted}` |
| `kalibre.addMidiClip(trackId, start, length)` | Returns its id |
| `kalibre.notes(clipId)` | `{id, start, length, pitch, velocity, channel, articulation}` (start from the clip's content) |
| `kalibre.addNote(clipId, {start, length, pitch, velocity, channel, articulation})` | Returns its id |
| `kalibre.setNote(clipId, noteId, {...})` | Change any of those fields |
| `kalibre.removeNotes(clipId, {ids})` | |
| `kalibre.quantise(clipId, grid, strength, {ids})` | |
| `kalibre.transpose(clipId, semitones, {ids})` | |
| `kalibre.tempo(tick)`, `kalibre.setTempo(tick, bpm, ramp)` | |
| `kalibre.setMeter(bar, numerator, denominator)`, `kalibre.barStart(bar)` | Bars from 1 |
| `kalibre.scale()`, `kalibre.setScale("F#", "Dorian")`, `kalibre.inScale(pitch)` | |
| `kalibre.chordAt(tick)` | The chord sounding there, named |
| `kalibre.selection()` | `{tracks, clip, notes, cursor, playhead}` |
| `kalibre.select({clip=, notes=})` | Choose a clip or notes |
| `kalibre.message(text)` | Shown in the window's message line |
| `kalibre.action("play" / "stop" / "record")` | |
| `print(...)` | Also shown in the message line |

The examples in `Scripts/` (Scripts > Add the Example Scripts) are a good start.
