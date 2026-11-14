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

-- With A2CMD_WATCH set to "LO-HI ARM" (hex addresses, and screen
-- text): once ARM is on the screen, each write to main memory from LO
-- to HI is printed (the first 40), with the 6502's PC when MAME gives
-- it, and zero page $EF-$F4 (the 64K VM's fetch loop and IP). For
-- finding what writes over a program's bytecode on the 64K VM.
local watch_tap = nil
local watch_arm = nil
local watch_n = 0
do
  local w = os.getenv("A2CMD_WATCH")
  if w then
    local lo, hi, arm = w:match("^(%x+)-(%x+) (.+)$")
    lo, hi = tonumber(lo, 16), tonumber(hi, 16)
    watch_arm = function()
      local cpu = machine.devices[":maincpu"]
      watch_tap = mem:install_write_tap(lo, hi, "a2cmd_watch",
        function(offset, data, mask)
          watch_n = watch_n + 1
          if watch_n <= 40 then
            local ok, pc = pcall(function() return cpu.state["PC"].value end)
            local zp = ""
            for a = 0xEF, 0xF4 do
              zp = zp .. string.format(" %02X", mem:read_u8(a))
            end
            print(string.format("a2cmd: write $%04X = $%02X, PC %s, zp $EF:%s",
              offset, data & 0xFF, ok and string.format("$%04X", pc) or "?", zp))
          end
          return data
        end)
    end
    a2cmd_watch_text = arm
  end
end

-- With A2CMD_FETCHES set to "LO-HI ARM": once ARM is on the screen,
-- each read of main memory from LO to HI is kept (the last 400), with
-- the 6502's PC and stack pointer, and the last 151 are printed when
-- MAME exits: on the 64K VM, a read at PC $F1 is a bytecode fetch,
-- and one at $F6 the dispatch's JMP (OPTBL), so this shows the last
-- bytecode run before a trap. (It found init's SAW to TMP writing
-- over the dispatch's page: doc/linking.md, 5.5.)
local fetch_arm = nil
local fetches = {}
-- With A2CMD_FETCH_FIRST set too, the first 400 instead.
local fetch_first = os.getenv("A2CMD_FETCH_FIRST") ~= nil
do
  local f = os.getenv("A2CMD_FETCHES")
  if f then
    local lo, hi, arm = f:match("^(%x+)-(%x+) (.+)$")
    lo, hi = tonumber(lo, 16), tonumber(hi, 16)
    fetch_arm = function()
      local cpu = machine.devices[":maincpu"]
      a2cmd_fetch_tap = mem:install_read_tap(lo, hi, "a2cmd_fetches",
        function(offset, data, mask)
          local pc = cpu.state["PC"].value
          fetches[#fetches + 1] = string.format("$%04X:%02X pc $%04X sp $%02X",
            offset, data & 0xFF, pc, cpu.state["SP"].value & 0xFF)
          if #fetches > 400 and not fetch_first then
            table.remove(fetches, 1)
          end
          return data
        end)
    end
    a2cmd_fetch_text = arm
  end
end
local function print_fetches()
  local from = math.max(1, #fetches - 150)
  if fetch_first then
    from = 1
  end
  for i = from, math.min(#fetches, from + 399) do
    print("a2cmd: fetch " .. fetches[i])
  end
end

-- With A2CMD_DEBUG set, every 5 seconds: the last line, as the
-- prompt test sees it.
local debug_at = 0
local debug = os.getenv("A2CMD_DEBUG") ~= nil
a2cmd_sub = emu.add_machine_frame_notifier(function()
  local t = machine.time:as_double()
  if watch_arm and on_screen(a2cmd_watch_text) then
    watch_arm()
    watch_arm = nil
  end
  if fetch_arm and on_screen(a2cmd_fetch_text) then
    fetch_arm()
    fetch_arm = nil
  end
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
    print_fetches()
    machine:exit()
  end
end)
