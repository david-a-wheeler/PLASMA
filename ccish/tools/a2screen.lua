-- a2screen.lua: MAME autoboot script for tools/a2screen. After
-- A2SCREEN_AFTER seconds of emulated time (the environment; 20 if
-- unset), print the Apple II's 40-column text page 1, one line per
-- row, and make MAME exit. The subscription is a global: a local one
-- would be garbage-collected once this chunk ends, which cancels it.
local after = tonumber(os.getenv("A2SCREEN_AFTER") or "20")

a2screen_sub = emu.add_machine_frame_notifier(function()
  if manager.machine.time.seconds < after then
    return
  end
  local mem = manager.machine.devices[":maincpu"].spaces["program"]
  for r = 0, 23 do
    -- Text row r is at $400 + 128 * (r % 8) + 40 * (r / 8).
    local base = 0x400 + (r % 8) * 128 + math.floor(r / 8) * 40
    local line = ""
    for c = 0, 39 do
      local ch = mem:read_u8(base + c) & 0x7f
      if ch < 32 then
        ch = ch + 64               -- inverse and flashing letters
      end
      line = line .. string.char(ch)
    end
    print(line)
  end
  manager.machine:exit()
end)
