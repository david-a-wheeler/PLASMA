-- a2cmd.lua: MAME autoboot script for tools/a2cmd. It waits for
-- PLASMA's prompt (its "MEM FREE:" line on the text screen), types
-- each of the commands in A2CMD_CMDS (separated by A2CMD_SEP, "|" if
-- unset) with a Return, each when CMD's prompt is back (the screen's
-- last line ends in ":" and has stayed so for 2 seconds of emulated
-- time) or A2CMD_GAP seconds after the one before (3600 if unset),
-- whichever comes first, and prints how long each took; then, when
-- the prompt is back after the last (or that long after it), it prints
-- the text page 1 (in 40 or 80 columns), one line per row, and makes
-- MAME exit. Subscriptions are globals: a local one would be garbage-
-- collected once this chunk ends, which cancels it.
local cmds = {}
local sep = os.getenv("A2CMD_SEP") or "|"
for c in string.gmatch(os.getenv("A2CMD_CMDS") or "", "([^" .. sep .. "]+)") do
  cmds[#cmds + 1] = c
end
local gap = tonumber(os.getenv("A2CMD_GAP") or "3600")
local boot = tonumber(os.getenv("A2CMD_BOOT") or "60")

local machine = manager.machine
local mem = machine.devices[":maincpu"].spaces["program"]

-- The extended 80-column card's aux memory, on a 128K IIe (nil on a
-- 64K one): in 80 columns, each row's even columns are there, at the
-- same addresses as the odd ones in main memory.
local aux = nil
local ext80 = machine.devices[":aux:ext80"]
if ext80 then
  for name, id in pairs(ext80.items) do
    if name:find("m_ram") then
      aux = emu.item(id)
    end
  end
end

-- The screen's width: 80 when the 80-column display is on (RD80VID,
-- $C01F, bit 7), else 40.
local function width()
  if aux and mem:read_u8(0xC01F) >= 0x80 then
    return 80
  end
  return 40
end

-- The byte in text row r, column c of page 1, for a screen w wide.
local function cell(r, c, w)
  local base = 0x400 + (r % 8) * 128 + math.floor(r / 8) * 40
  if w == 80 then
    if c % 2 == 0 then
      return aux:read(base + c // 2)
    end
    return mem:read_u8(base + c // 2)
  end
  return mem:read_u8(base + c)
end

-- Text row r of page 1, as a string.
local function row(r)
  local w = width()
  local line = ""
  for c = 0, w - 1 do
    local ch = cell(r, c, w) & 0x7f
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

local next_at = nil   -- when to type the next command at the latest
local done_at = nil   -- when to print the screen at the latest
local typed = 0
local typed_at = 0    -- when the last command was typed
local prompt_since = nil  -- since when the prompt has been back

-- The screen's last nonblank line, without the cursor: trailing
-- spaces and inverse or flashing bytes (below $80; the Monitor's input
-- cursor flashes the byte under it) don't count.
local function last_line()
  local w = width()
  for r = 23, 0, -1 do
    local n = w
    while n > 0 do
      local b = cell(r, n - 1, w)
      if b >= 0x80 and b ~= 0xA0 then
        break
      end
      n = n - 1
    end
    if n > 0 then
      return row(r):sub(1, n)
    end
  end
  return ""
end

-- Is CMD's prompt back, and has it been for 2 seconds (from t)? The
-- last line ends in ":", or in ":" and the cursor, which CMD blinks by
-- rewriting the byte after the prompt.
local function prompt_back(t)
  local l = last_line()
  if t < typed_at + 1 or (l:sub(-1) ~= ":" and l:sub(-2, -2) ~= ":") then
    prompt_since = nil
    return false
  end
  prompt_since = prompt_since or t
  return t >= prompt_since + 2
end

-- With A2CMD_DEBUG set, every 5 seconds: the last line, as the
-- prompt test sees it.
local debug_at = 0
local debug = os.getenv("A2CMD_DEBUG") ~= nil
a2cmd_sub = emu.add_machine_frame_notifier(function()
  local t = machine.time:as_double()
  if debug and t >= debug_at then
    debug_at = t + 5
    local l = last_line()
    print(string.format("a2cmd: %.0f s: last line [%s] (%d) typed %d at %.1f since %s", t, l, #l, typed, typed_at, tostring(prompt_since)))
  end
  if next_at == nil then
    if on_screen("MEM FREE:") then
      next_at = t + 1
    elseif t >= boot then
      print(string.format("no PLASMA prompt after %d s", boot))
      for r = 0, 23 do
        print(row(r))
      end
      machine:exit()
    end
  elseif typed < #cmds then
    if t >= next_at or (typed > 0 and prompt_back(t)) then
      if typed > 0 then
        print(string.format("a2cmd: %s: %.1f s", cmds[typed], t - typed_at))
      end
      typed = typed + 1
      machine.natkeyboard:post(cmds[typed] .. "\r")
      typed_at = t
      prompt_since = nil
      next_at = t + gap
      if typed == #cmds then
        done_at = t + gap
      end
    end
  elseif done_at == nil or t >= done_at or prompt_back(t) then
    print(string.format("a2cmd: %s: %.1f s", cmds[typed], t - typed_at))
    for r = 0, 23 do
      print(row(r))
    end
    machine:exit()
  end
end)
