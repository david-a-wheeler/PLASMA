-- a2time.lua: MAME autoboot script for tools/a2time. It waits for
-- PLASMA's prompt (its "MEM FREE:" line on the text screen), types
-- A2TIME_CMD and Return, and then, for A2TIME_FOR seconds of emulated
-- time, records each time the high nibble of the screen byte at
-- A2TIME_ADDR changes. Then it prints the times and the mean time
-- between changes, and makes MAME exit.
--
-- For Rod's Colors, $4BC (the default) is lores pixel (20, 19), which
-- only ring 19 plots, and whose color changes once a frame in each
-- version, so the time between changes is the time a frame takes.
--
-- Subscriptions and taps are globals: a local one would be
-- garbage-collected once this chunk ends, which cancels it.
local cmd = os.getenv("A2TIME_CMD") or ""
local run_for = tonumber(os.getenv("A2TIME_FOR") or "300")
local addr = tonumber(os.getenv("A2TIME_ADDR") or "0x4BC")
local boot = tonumber(os.getenv("A2TIME_BOOT") or "60") -- prompt's limit
local SKIP = 3        -- changes while starting up (mode switch, first frames)
local CYCLE = 48      -- frames in a cycle of Rod's Colors (w = 3 to 50)
local SETTLE = 1      -- seconds from seeing the prompt to typing
local sums_on = os.getenv("A2TIME_SUMS") ~= nil

local machine = manager.machine
local mem = machine.devices[":maincpu"].spaces["program"]

local function now()
  return machine.time:as_double()
end

-- Is TEXT anywhere on the 40-column text page 1? Text row r starts at
-- $400 + 128 * (r % 8) + 40 * (r / 8).
local function on_screen(text)
  for r = 0, 23 do
    local base = 0x400 + (r % 8) * 128 + math.floor(r / 8) * 40
    local line = ""
    for c = 0, 39 do
      line = line .. string.char(mem:read_u8(base + c) & 0x7f)
    end
    if line:find(text, 1, true) then
      return true
    end
  end
  return false
end

local prompt_at = nil  -- when the prompt was seen
local typed_at = nil   -- when the command was typed
local last = nil       -- the byte's high nibble at the last change
local changes = {}     -- times of the changes after typing
local sums = {}        -- page 1's checksums at the changes (sums_on)

-- A checksum of page 1, $400 to $7FF: h = h * 31 + each byte, mod 2^24.
local function page_sum()
  local h = 0
  for a = 0x400, 0x7ff do
    h = (h * 31 + mem:read_u8(a)) % 0x1000000
  end
  return h
end

a2time_tap = mem:install_write_tap(addr, addr, "a2time",
  function(offset, data, mask)
    if typed_at == nil then
      return
    end
    local hi = data & 0xf0
    if hi ~= last then
      last = hi
      changes[#changes + 1] = now()
      if sums_on then
        sums[#changes] = page_sum()
      end
    end
  end)

-- Print the results: each change's time from typing, then the frame
-- time, over a whole number of cycles when there's at least one (a
-- frame's time can depend on w), else over all the changes after SKIP.
local function report()
  print(string.format("command: %s, typed at %.3f s", cmd, typed_at))
  for i, t in ipairs(changes) do
    print(string.format("change %d: %.4f s%s", i, t - typed_at,
                        sums_on and string.format(" sum %06x", sums[i]) or ""))
  end
  local n = #changes - SKIP - 1     -- intervals after the start-up ones
  if n < 1 then
    print("too few changes to time")
    return
  end
  if n >= CYCLE then
    n = n - n % CYCLE
  end
  local first = changes[SKIP + 1]
  local span = changes[SKIP + 1 + n] - first
  print(string.format("first timed change: %.3f s after typing",
                      first - typed_at))
  print(string.format("frames: %d%s; seconds a frame: %.4f", n,
                      n % CYCLE == 0 and " (whole cycles)" or "", span / n))
  local lo, hi = math.huge, 0
  for i = SKIP + 1, SKIP + n do
    local d = changes[i + 1] - changes[i]
    lo = math.min(lo, d)
    hi = math.max(hi, d)
  end
  print(string.format("shortest frame: %.4f s, longest: %.4f s", lo, hi))
end

a2time_sub = emu.add_machine_frame_notifier(function()
  local t = now()
  if prompt_at == nil then
    if on_screen("MEM FREE:") then
      prompt_at = t
    elseif t >= boot then
      print(string.format("no PLASMA prompt after %d s", boot))
      machine:exit()
    end
  elseif typed_at == nil then
    if t >= prompt_at + SETTLE then
      last = mem:read_u8(addr) & 0xf0
      typed_at = t
      machine.natkeyboard:post(cmd .. "\r")
    end
  elseif t >= typed_at + run_for then
    report()
    machine:exit()
  end
end)
