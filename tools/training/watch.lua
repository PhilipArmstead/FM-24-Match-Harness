-- Log writers of player attribute bytes. Player pointers come from sub_1438F1890(rcx=mgr, rdx=player).
LOG = os.getenv("TEMP") .. [[\fm_attr_watch.log]]
ENTRY = 0x1438F1890
LO, HI = 0x210, 0x260
MAXP = 3

local function log(s)
  local f = io.open(LOG, "a"); f:write(s .. "\n"); f:close()
end

local function hexblock(a, n)
  local b = readBytes(a, n, true) or {}
  local t = {}
  for i = 1, #b do t[i] = string.format("%02x", b[i]) end
  return table.concat(t)
end

local function stack()
  local t = {}
  for i = 0, 0x600, 8 do
    local q = readQword(RSP + i)
    if q and q > 0x140001000 and q < 0x1466F4000 then t[#t + 1] = string.format("%x", q) end
    if #t >= 12 then break end
  end
  return table.concat(t, ",")
end

players = players or {}
nplayers = nplayers or 0
ripcount = ripcount or {}

function onWrite()
  local rip = RIP
  ripcount[rip] = (ripcount[rip] or 0) + 1
  if ripcount[rip] <= 40 then
    log(string.format("W rip=%x n=%d rcx=%x rdx=%x r8=%x stack=%s", rip, ripcount[rip], RCX, RDX, R8, stack()))
    for p, _ in pairs(players) do log(string.format("  %x %s", p, hexblock(p + LO, HI - LO))) end
  end
  return 1
end

function onEntry()
  local p = RDX
  if nplayers < MAXP and not players[p] then
    players[p] = true
    nplayers = nplayers + 1
    log(string.format("P %x %s", p, hexblock(p + LO, HI - LO)))
    debug_setBreakpoint(p + LO, HI - LO, bptWrite, bpmException, onWrite)
    if nplayers >= MAXP then debug_removeBreakpoint(ENTRY) end
  end
  return 1
end

log("start")
debugProcess()
debug_setBreakpoint(ENTRY, 1, bptExecute, bpmDebugRegister, onEntry)
log("armed")
