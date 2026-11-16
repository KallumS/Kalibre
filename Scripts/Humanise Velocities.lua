-- Gives every chosen note (or every note in the clip) a slightly different velocity.
local sel = kalibre.selection()
if sel.clip == 0 then kalibre.message("Open a MIDI clip first.") return end
local chosen = {}
for _, id in ipairs(sel.notes) do chosen[id] = true end
local all = #sel.notes == 0
for _, n in ipairs(kalibre.notes(sel.clip)) do
  if all or chosen[n.id] then
    local v = math.max(1, math.min(127, n.velocity + math.random(-10, 10)))
    kalibre.setNote(sel.clip, n.id, { velocity = v })
  end
end
kalibre.message("Velocities humanised.")
