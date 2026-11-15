-- Names the chord the chosen notes make, in the project's scale.
local sel = kalibre.selection()
if sel.clip == 0 then kalibre.message("Open a MIDI clip and choose some notes first.") return end
local chosen = {}
for _, id in ipairs(sel.notes) do chosen[id] = true end
local lowest
for _, n in ipairs(kalibre.notes(sel.clip)) do
  if chosen[n.id] and (lowest == nil or n.start < lowest) then lowest = n.start end
end
if lowest == nil then kalibre.message("Choose some notes first.") return end
local clip
for _, t in ipairs(kalibre.tracks()) do
  for _, c in ipairs(kalibre.clips(t.id)) do if c.id == sel.clip then clip = c end end
end
local name = kalibre.chordAt(clip.start + lowest)
kalibre.message(name ~= "" and ("That's " .. name .. ".") or "That isn't a chord.")
