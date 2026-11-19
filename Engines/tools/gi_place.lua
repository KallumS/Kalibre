--[[ Good Idea - getting an idea into the song, in Kalibre.

     Kalibre's stand-in for reascripts/gi_place.lua (decision 0093): the same
     functions, so the window script runs unchanged, talking to the device
     (the `host` table, Source/Engine/LuaTool/ToolSession.cpp) instead of
     REAPER, as mc_place.lua does for Midi Catalogue, whose placement Good
     Idea's grew from.

     An idea in one clip goes on the device's own track at the playhead,
     each part on its own MIDI channel (drums on 10); one laid out on tracks
     arrives as a new track per part, named for it, under the device's track,
     all as one undo step. Audition plays through the device's MIDI output,
     timed by the audio thread, to the instrument after it. Play with REAPER
     is the device's Play with the Song: it plays the idea over and over in
     time with the song, from its first bar.
]]

local M = {}

M.OK, M.NO_TRACK, M.WRITE_FAILED, M.NOTHING = 0, 1, 2, 3
M.HOST = "plugin"

local SEP = package.config:sub(1, 1)

function M.setMidi(midi) M.midi = midi end

------------------------------------------------------------------------------
-- What the song is doing
------------------------------------------------------------------------------

function M.tempo() return host.tempo() end

function M.timeSig()
  local num, den = host.timeSig()
  if not num or num <= 0 or not den or den <= 0 then return 4, 4 end
  return num, den
end

function M.timeSigNow() return M.timeSig() end

------------------------------------------------------------------------------
-- Into the song
------------------------------------------------------------------------------

function M.insert(block)
  if #block.notes == 0 then return M.NOTHING end
  local parts
  if block.layout ~= "tracks" or #block.parts == 1 then
    parts = { { name = block.name, notes = block.notes } }
  else
    parts = {}
    for i, part in ipairs(block.parts) do parts[i] = { name = part.name, notes = part.notes } end
  end
  local ok = host.insert(parts, block.beats, block.name)
  return ok and M.OK or M.NO_TRACK
end

------------------------------------------------------------------------------
-- Out to a file: Music/Good Idea, or a folder chosen
------------------------------------------------------------------------------

function M.binPath() return host.exportDir() end
function M.projectFolder() return nil end

function M.exportDir(where, chosen)
  if where == "Folder" and chosen and chosen ~= "" then return chosen, true end
  return M.binPath(), where ~= "Folder"
end

local function exists(path)
  local f = io.open(path, "rb")
  if f then f:close(); return true end
  return false
end

function M.uniquePath(dir, base)
  local path = dir .. SEP .. base .. ".mid"
  local n = 2
  while exists(path) do
    path = dir .. SEP .. base .. " " .. n .. ".mid"
    n = n + 1
    if n > 999 then return nil end
  end
  return path
end

function M.export(block, dir)
  if #block.notes == 0 then return M.NOTHING end
  dir = (dir and dir ~= "") and dir:gsub("[/\\]+$", "") or M.binPath()
  host.makeDir(dir)
  local path = M.uniquePath(dir, M.midi.sanitise(block.name))
  if not path then return M.WRITE_FAILED end
  local num, den = M.timeSigNow()
  local bytes
  if block.layout == "tracks" then
    bytes = M.midi.buildParts(block.parts, block.beats, block.name, M.tempo(), num, den)
  else
    bytes = M.midi.build(block.notes, block.beats, block.name, M.tempo(), num, den)
  end
  local f = io.open(path, "wb")
  if not f then return M.WRITE_FAILED end
  f:write(bytes)
  f:close()
  return M.OK, path
end

function M.openFolder(dir)
  host.makeDir(dir)
  host.reveal(dir)
  return true
end

-- Kalibre has no folder box to type into from a script: "Folder" keeps the
-- one chosen before, or Music/Good Idea.
function M.chooseFolder() return nil end

------------------------------------------------------------------------------
-- Hearing one
------------------------------------------------------------------------------

function M.previewStart(block)
  M.previewStop()
  if #block.notes == 0 then return false end
  host.setBlock(block.notes, block.beats)
  host.audition(true)
  return true
end

function M.previewStop() host.audition(false) end
function M.previewRunning() return host.auditioning() end

-- The device plays with the song itself: nothing for the window to follow.
function M.previewWithReaper() return false end
function M.reaperPlaying() return false end
function M.previewWithReaperStart(block) return M.previewStart(block) end

function M.previewSwap(block)
  if not host.auditioning() then return false end
  if #block.notes == 0 then M.previewStop(); return false end
  host.setBlock(block.notes, block.beats)
  return true
end

-- Play with REAPER: the device's Play with the Song, the idea given to it
-- (called every frame: a new idea only when there is one).
local followed
function M.follow(block, on)
  if block and block ~= followed then host.setBlock(block.notes, block.beats); followed = block end
  if (on and true or false) ~= host.withSong() then host.setWithSong(on and true or false) end
end

-- A click on the roll: Audition from the top (the device can't start mid-way).
function M.previewFrom(block) return M.previewStart(block) end

function M.previewTick(_, loop)
  host.setLoop(loop and true or false)
  return host.auditionPosition()
end

return M
