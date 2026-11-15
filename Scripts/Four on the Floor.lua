-- Makes a drums track with a four-bar house beat at the cursor.
local t = kalibre.addTrack("Drums")
local start = kalibre.selection().cursor
local bar = kalibre.ppq * 4
local c = kalibre.addMidiClip(t, start, bar * 4)
for b = 0, 3 do
  for beat = 0, 3 do
    local at = b * bar + beat * kalibre.ppq
    kalibre.addNote(c, { start = at, length = kalibre.ppq / 4, pitch = 36, velocity = 110 })
    kalibre.addNote(c, { start = at + kalibre.ppq / 2, length = kalibre.ppq / 4, pitch = 42, velocity = 80 })
    if beat % 2 == 1 then kalibre.addNote(c, { start = at, length = kalibre.ppq / 4, pitch = 39, velocity = 100 }) end
  end
end
kalibre.select({ clip = c })
kalibre.message("Four bars of drums on a new track.")
