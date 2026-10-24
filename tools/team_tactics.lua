-- Cheat Engine Lua: dump a team's stored tactics from a running fm.exe (FM24 24.2, Steam or Epic).
-- Paste into Table > Show Cheat Table Lua Script, or run in a {$lua} auto-assemble block, after
-- setting TEAM_UID and OUT_PATH below (or set them as globals and dofile() this). Layout and how it
-- was found: docs/FIXTURE.md, "Where a team's tactic is stored". Only human-managed teams have
-- stored tactics; for any other team the script says so.
-- shortcut: RVAs are the 24.2 builds'; the March 2024 build (1710868029) needs its own.

local TEAM_UID = TEAM_UID or 603 -- the team's unique ID (Aston Villa)
local OUT_PATH = OUT_PATH or os.getenv("TEMP") .. "\\team_tactics.txt"

local base = getAddress("fm.exe")
local RVA_TACTICS_MANAGER = 0x6374BD8 -- TACTICS_MANAGER *
local RVA_TEAM_VTABLE = 0x5A78848 -- db::TEAM
local RVA_PREPARED_TACTICS_VTABLE = 0x5A1AF38 -- simatch::PREPARED_TACTICS_CREATOR_ARRAY

local out = assert(io.open(OUT_PATH, "w"))
local function say(...) out:write(string.format(...), "\n") end

local function vtableBytes(rva)
	local v, t = base + rva, {}
	for _ = 1, 8 do t[#t + 1] = string.format("%02X", v % 256); v = v // 256 end
	return table.concat(t, " ")
end

-- The team: a db::TEAM whose unique ID (+0x0C) is TEAM_UID. The tactics are keyed by its +0x08.
local team
local hits = AOBScan(vtableBytes(RVA_TEAM_VTABLE), "+W-C", fsmAligned, "8")
if hits then
	for i = 0, hits.Count - 1 do
		local a = tonumber(hits[i], 16)
		if readInteger(a + 0x0C) == TEAM_UID then team = a; break end
	end
	hits.destroy()
end

-- TACTICS_MANAGER +0x18/+0x20: one record per human manager. Each record starts with a
-- std::map<int, TEAM_TACTICS> {head, size}; MSVC nodes are left, parent, right, colour, is-nil,
-- then the key (+0x20) and the value (+0x28).
local function findNode(node, head, key, depth)
	if node == head or depth > 64 or readBytes(node + 0x19, 1) ~= 0 then return nil end
	local k = readInteger(node + 0x20)
	if k == key then return node end
	return findNode(readQword(node + (key < k and 0 or 0x10)), head, key, depth + 1)
end

local value
if team then
	local key = readInteger(team + 8)
	local manager = readQword(base + RVA_TACTICS_MANAGER)
	for r = readQword(manager + 0x18), readQword(manager + 0x20) - 8, 8 do
		local head = readQword(readQword(r))
		local node = findNode(readQword(head + 8), head, key, 0)
		if node then value = node + 0x28; break end
	end
end

if not team then
	say("no team with unique ID %d", TEAM_UID)
elseif not value then
	say("team %d (TEAM %X) has no stored tactics: it isn't human-managed", TEAM_UID, team)
else
	-- TEAM_TACTICS +0x1008: PREPARED_TACTICS_CREATOR_ARRAY {vtable, vector<TACTIC_PLAN *>}, one
	-- plan per tactic slot.
	local array = value + 0x1008
	assert(readQword(array) == base + RVA_PREPARED_TACTICS_VTABLE, "unexpected TEAM_TACTICS layout")
	local first, last = readQword(array + 8), readQword(array + 0x10)
	say("team %d: TEAM %X, TEAM_TACTICS %X, %d tactics", TEAM_UID, team, value, (last - first) // 8)
	for i = 0, (last - first) // 8 - 1 do
		local plan = readQword(first + 8 * i)
		say("tactic[%d] plan %X", i, plan)
		say("  mentality %d", readBytes(plan + 0x19, 1))
		for w = 0, 2 do say("  team_instructions[%d] %d", w, readInteger(plan + 4 * w) & 0xFFFFFFFF) end
		for s = 0, 10 do
			local slot = plan + 0x30 + 0x48 * s
			say("  slot[%d] position %d role %d", s, readInteger(slot + 8) & 0xFFFFFFFF, readQword(slot))
			for set = readQword(slot + 0x10), readQword(slot + 0x18) - 0x18, 0x18 do
				-- +0x10 is probably the marked position (0 in every set seen), +0x14 the marked player (255: none)
				say("    player_instructions[%d] bits %d %d +10 %d mark_player %d", readQword(set),
					readInteger(set + 8) & 0xFFFFFFFF, readInteger(set + 0xC) & 0xFFFFFFFF,
					readInteger(set + 0x10) & 0xFFFFFFFF, readBytes(set + 0x14, 1))
			end
		end
	end
end
out:close()
