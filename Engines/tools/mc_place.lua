--[[ Midi Catalogue - getting a block into the song, in Kalibre.

     Kalibre's stand-in for reascripts/mc_place.lua (decision 0068): the same
     functions, so the window script runs unchanged, talking to the device
     (the `host` table, Source/Engine/LuaTool.cpp) instead of REAPER.

     A single part goes on the device's own track at the playhead; a block
     spread across a section arrives as one new track per instrument, named
     for it, under the device's track, all at the playhead, as one undo step.
     Audition plays through the device's MIDI output, timed by the audio
     thread, to the instrument after it.
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

function M.barBeats()
  local num, den = M.timeSig()
  return num * 4 / den
end

------------------------------------------------------------------------------
-- Into the song, and out to a file
------------------------------------------------------------------------------

function M.insert(block)
  if #block.notes == 0 then return M.NOTHING end
  local parts = {}
  for i, part in ipairs(block.parts) do parts[i] = { name = part.name, notes = part.notes } end
  local ok = host.insert(parts, block.beats, block.name)
  return ok and M.OK or M.NO_TRACK
end

function M.binPath() return host.exportDir() end

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

function M.export(block)
  if #block.notes == 0 then return M.NOTHING end
  local dir = M.binPath()
  host.makeDir(dir)
  local path = M.uniquePath(dir, M.midi.sanitise(block.name))
  if not path then return M.WRITE_FAILED end
  local num, den = M.timeSig()
  local f = io.open(path, "wb")
  if not f then return M.WRITE_FAILED end
  f:write(M.midi.buildParts(block.parts, block.beats, block.name, M.tempo(), num, den))
  f:close()
  host.reveal(path)
  return M.OK, path
end

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

function M.previewTick(_, loop)
  host.setLoop(loop and true or false)
  return host.auditionPosition()
end

return M
