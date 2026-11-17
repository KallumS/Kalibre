--[[ Starting Blocks - getting a block into the song, in Kalibre.

     Kalibre's stand-in for reascripts/sb_place.lua (decision 0068), shared by
     Starting Blocks and Starting Blocks Notation: the same functions, so the
     window scripts run unchanged, talking to the device (the `host` table,
     Source/Engine/LuaTool.cpp) instead of REAPER.

     Adapted from the plugin branch's plugin/lua/sb_place.lua. What differs:
     Kalibre has a playhead and the device has a track, so Insert at Cursor
     puts the block on the device's track at the playhead, as a MIDI clip, in
     one undo step; there is no dragging out. Audition and Play with the Song
     play through the device's MIDI output, to the instrument after it.
]]

local M = {}

M.OK, M.NO_TRACK, M.WRITE_FAILED, M.NOTHING = 0, 1, 2, 3
-- As the plug-in: the window belongs to its host, and plays through MIDI out.
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

function M.cursor() return host.cursor() end

function M.barBeats()
  local num, den = M.timeSig()
  return num * 4 / den
end

------------------------------------------------------------------------------
-- The block the device plays
------------------------------------------------------------------------------

function M.follow(block)
  host.setBlock(block and block.notes or {}, block and block.beats or 0)
end

function M.playingWithSong() return host.withSong() end
function M.setPlayingWithSong(on) host.setWithSong(on and true or false) end

------------------------------------------------------------------------------
-- Into the song, and out to a file
------------------------------------------------------------------------------

-- On the device's own track, at the playhead.
function M.insert(block)
  if #block.notes == 0 then return M.NOTHING end
  local ok = host.insert({ { name = block.name, notes = block.notes } }, block.beats, block.name)
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
  f:write(M.midi.build(block.notes, block.beats, block.name, M.tempo(), num, den))
  f:close()
  host.reveal(path)
  return M.OK, path
end

------------------------------------------------------------------------------
-- Hearing one
------------------------------------------------------------------------------

function M.previewStart(block)
  if #block.notes == 0 then return false end
  M.follow(block)
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
