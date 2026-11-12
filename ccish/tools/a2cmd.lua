-- a2cmd.lua: MAME autoboot script for tools/a2cmd. It waits for
-- PLASMA's prompt (its "MEM FREE:" line on the text screen), types
-- each of the commands in A2CMD_CMDS (separated by A2CMD_SEP, "|" if
-- unset) with a Return, A2CMD_GAP seconds of emulated time apart (5
-- if unset), then A2CMD_AFTER seconds after the last (20 if unset)
-- prints the 40-column text page 1, one line per row, and makes MAME
-- exit. Subscriptions are globals: a local one would be garbage-
-- collected once this chunk ends, which cancels it.
local cmds = {}
local sep = os.getenv("A2CMD_SEP") or "|"
for c in string.gmatch(os.getenv("A2CMD_CMDS") or "", "([^" .. sep .. "]+)") do
  cmds[#cmds + 1] = c
end
local gap = tonumber(os.getenv("A2CMD_GAP") or "5")
local after = tonumber(os.getenv("A2CMD_AFTER") or "20")
local boot = tonumber(os.getenv("A2CMD_BOOT") or "60")

local machine = manager.machine
local mem = machine.devices[":maincpu"].spaces["program"]

-- Text row r of page 1, as a string.
local function row(r)
  local base = 0x400 + (r % 8) * 128 + math.floor(r / 8) * 40
  local line = ""
  for c = 0, 39 do
    local ch = mem:read_u8(base + c) & 0x7f
    if ch < 32 then
      ch = ch + 64               -- inverse and flashing letters
    end
    line = line .. string.char(ch)
  end
  return line
end

-- Is TEXT anywhere on the screen?
local function on_screen(text)
  for r = 0, 23 do
    if row(r):find(text, 1, true) then
      return true
    end
  end
  return false
end

local next_at = nil   -- when to type the next command
local done_at = nil   -- when to print the screen
local typed = 0

a2cmd_sub = emu.add_machine_frame_notifier(function()
  local t = machine.time:as_double()
  if next_at == nil then
    if on_screen("MEM FREE:") then
      next_at = t + 1
    elseif t >= boot then
      print(string.format("no PLASMA prompt after %d s", boot))
      machine:exit()
    end
  elseif typed < #cmds then
    if t >= next_at then
      typed = typed + 1
      machine.natkeyboard:post(cmds[typed] .. "\r")
      next_at = t + gap
      if typed == #cmds then
        done_at = t + after
      end
    end
  elseif done_at == nil or t >= done_at then
    for r = 0, 23 do
      print(row(r))
    end
    machine:exit()
  end
end)
