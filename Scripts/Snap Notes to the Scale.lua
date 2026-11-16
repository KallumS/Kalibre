-- Moves every note outside the project's scale to the nearest note inside it.
local sel = kalibre.selection()
if sel.clip == 0 then kalibre.message("Open a MIDI clip first.") return end
local moved = 0
for _, n in ipairs(kalibre.notes(sel.clip)) do
  if not kalibre.inScale(n.pitch) then
    local up, down = n.pitch + 1, n.pitch - 1
    while not kalibre.inScale(down) and down > 0 do down = down - 1 end
    while not kalibre.inScale(up) and up < 127 do up = up + 1 end
    local to = (n.pitch - down <= up - n.pitch) and down or up
    kalibre.setNote(sel.clip, n.id, { pitch = to })
    moved = moved + 1
  end
end
kalibre.message(moved == 0 and "Every note was already in " .. kalibre.scale().name .. "." or ("Moved " .. moved .. " notes into " .. kalibre.scale().name .. "."))
