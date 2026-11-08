/*=====================================================================
MCPGameExamples.h
-----------------
Executable Luau game examples returned by the MCP reference tool.
=====================================================================*/
#pragma once

#include "Escaping.h"
#include <string>

namespace MCPHandlers
{
inline std::string getConnectFourGameExample()
{
	const std::string script = R"luau(--lua
-- Server-authoritative two-player Connect Four. Attach this to a visible game host object.
-- Players look at the host, press E, then choose a column from the HUD.
local WIDTH, HEIGHT = 7, 6
local SCHEMA = "connect_four_v1"

local function freshState()
	return {schema=SCHEMA, board={".......",".......",".......",".......",".......","......."},
		players={}, turn=1, status="waiting", winner=0, moves=0}
end

local function readState()
	local state = game.getState()
	if type(state) == "table" and state.schema == SCHEMA then return state end
	return freshState()
end

local function playerIndex(state, avatar_uid)
	for i, player in ipairs(state.players) do
		if player.uid == avatar_uid then return i end
	end
	return 0
end

local function countDirection(board, row, col, dr, dc, token)
	local count = 0
	local r, c = row + dr, col + dc
	while r >= 1 and r <= HEIGHT and c >= 1 and c <= WIDTH and string.sub(board[r], c, c) == token do
		count = count + 1
		r, c = r + dr, c + dc
	end
	return count
end

local function hasFour(board, row, col, token)
	local directions = {{0,1},{1,0},{1,1},{1,-1}}
	for _, d in ipairs(directions) do
		if 1 + countDirection(board,row,col,d[1],d[2],token) + countDirection(board,row,col,-d[1],-d[2],token) >= 4 then
			return true
		end
	end
	return false
end

local function boardText(state)
	local lines = {"1  2  3  4  5  6  7"}
	for row = 1, HEIGHT do
		local cells = {}
		for col = 1, WIDTH do
			local cell = string.sub(state.board[row], col, col)
			cells[col] = cell == "." and "·" or cell
		end
		lines[#lines + 1] = table.concat(cells, "  ")
	end
	return table.concat(lines, "\n")
end

local function statusText(state)
	if state.status == "waiting" then return "Waiting for a second player." end
	if state.status == "draw" then return "Draw. Start another match?" end
	if state.status == "won" then return (state.winner == 1 and "X" or "O") .. " wins! Start another match?" end
	if state.status == "forfeit" then return (state.winner == 1 and "X" or "O") .. " wins by forfeit." end
	return "Turn: " .. (state.turn == 1 and "X" or "O")
end

local function refreshHUD(state)
	for _, avatar in ipairs(game.getPlayers()) do
		local actions = {}
		local index = playerIndex(state, avatar.uid)
		if state.status == "playing" and index == state.turn then
			for col = 1, WIDTH do
				actions[#actions + 1] = {id="drop_" .. tostring(col), label="Drop in column " .. tostring(col)}
			end
		elseif index > 0 and (state.status == "won" or state.status == "draw" or state.status == "forfeit") then
			actions[1] = {id="restart", label="Start a new match"}
		else
			actions[1] = {id="wait", label="Waiting for players"}
		end
		game.setHUDMenu(avatar, "Four in a Row", actions, statusText(state) .. "\n" .. boardText(state))
	end
end

local function publish(state)
	local ok, reason = game.setState(state)
	if not ok then print("Could not update game state: " .. tostring(reason)); return end
	refreshHUD(state)
end

local function resetForActivePlayers()
	local state = freshState()
	for _, avatar in ipairs(game.getPlayers()) do
		if #state.players == 2 then break end
		state.players[#state.players + 1] = {uid=avatar.uid, name=avatar.name}
	end
	if #state.players == 2 then state.status = "playing" end
	return state
end

function onUserGameSessionChanged(avatar, object, active)
	if not IS_SERVER then return end
	local state = readState()
	local index = playerIndex(state, avatar.uid)
	if state.status == "waiting" then
		-- Rebuild from the authoritative participant list so a waiting player who
		-- disconnects does not leave the remaining player out of the roster.
		state = resetForActivePlayers()
	elseif not active and index > 0 then
		state.status = "forfeit"
		state.winner = 3 - index
	end
	publish(state)
end

function onUserGameAction(avatar, object, action_id, values)
	if not IS_SERVER then return end
	local state = readState()
	local index = playerIndex(state, avatar.uid)
	if index == 0 then return end
	if action_id == "restart" and (state.status == "won" or state.status == "draw" or state.status == "forfeit") then
		publish(resetForActivePlayers())
		return
	end
	if state.status ~= "playing" or index ~= state.turn then return end
	local col = tonumber(string.match(action_id, "^drop_(%d+)$"))
	if not col or col < 1 or col > WIDTH then return end
	local row = 0
	for r = HEIGHT, 1, -1 do
		if string.sub(state.board[r], col, col) == "." then row = r; break end
	end
	if row == 0 then return end
	local token = state.turn == 1 and "X" or "O"
	state.board[row] = string.sub(state.board[row],1,col-1) .. token .. string.sub(state.board[row],col+1)
	state.moves = state.moves + 1
	if hasFour(state.board, row, col, token) then
		state.status = "won"
		state.winner = state.turn
	elseif state.moves >= WIDTH * HEIGHT then
		state.status = "draw"
	else
		state.turn = 3 - state.turn
	end
	publish(state)
end

if IS_SERVER then
	local state = readState()
	local saved_state = game.getState()
	if type(saved_state) ~= "table" or saved_state.schema ~= SCHEMA or state.status == "waiting" then
		state = resetForActivePlayers()
		game.setState(state)
	end
	refreshHUD(state)
end)luau";
return "{\"purpose\":\"A complete event-driven multiplayer Luau game using server-validated HUD actions and late-join-safe shared state. The game host must be visible; players look at it and press E.\",\"release_boundary\":\"Requires the updated Linux server with server scripting enabled and Qt clients advertising the scripted-game HUD and state capabilities. Local avatar input additionally requires a Qt client with game.getLocalInput/game.setPlayerControl and focused game-session support.\",\"authoring_workflow\":\"Create a visible game host with build_mesh at an explicit writable-parcel position, then attach the returned UID with update_object_script. Prefix the complete script with --lua, read it back, and inspect get_object_script_log after a player focuses the host. Server scripting must be enabled for the shared game loop.\",\"script\":\"" + web::Escaping::JSONEscape(script) + "\",\"genre_patterns\":[{\"genre\":\"real-time arena\",\"pattern\":\"Keep game rules and score on the server. Read game.getInput(avatar) from a server game loop; move only game-spawned pieces with game.moveObjectWithSlide; use game.getAimTarget(avatar, distance, true) to select world switches; its ray direction is client-reported, so do not use it alone as proof of a competitive hit or anti-cheat line of sight. Local avatar movement is client-side and does not add force-based server physics or anti-cheat validation.\"},{\"genre\":\"platformer or obstacle course\",\"pattern\":\"Build level geometry with game.createVoxelObject or templates, and use game.getOverlaps/game.getPlayersNear for checkpoints. In a focused client loop, read game.getLocalInput() and pass its move_x, move_y, jump and run to game.setPlayerControl() for local collision-aware avatar movement. Keep checkpoints, progress and rewards on the server with validated game messages and game.setPlayerData; client movement is not authoritative.\"},{\"genre\":\"racing or vehicle-lite\",\"pattern\":\"Use game.getLocalInput/game.setPlayerControl for local on-foot movement, or game.getInput(avatar) for server-observed gamepad input; move server-owned kinematic vehicles with game.sweepAABB or game.moveObjectWithSlide and validate race results with game.setState. This is scripted movement, not wheel or suspension simulation or authoritative avatar physics.\"},{\"genre\":\"turn-based puzzle or board game\",\"pattern\":\"Validate every onUserGameAction against server state, update with game.setState, and render controls with game.setHUDMenu. The included Connect Four script is a complete example.\"},{\"genre\":\"quest or interactive story\",\"pattern\":\"Use named HUD panels and validated fields for choices, game.setState for current world progress, and game.getPlayerData/setPlayerData for authenticated per-account progress.\"}],\"controls\":\"Two active participants take turns dropping discs with HUD buttons. Other focused players spectate. Leaving during a match awards a forfeit; either player can start a new match after a result.\",\"extension_points\":[\"Use game.createLoop for timed rounds or moving pieces.\",\"Use game.getInput for server-observed pointer/gamepad state and game.isKeyDown for server-held buttons; use game.getLocalInput/game.setPlayerControl only for local client presentation.\",\"Use game.createVoxelObject for bounded voxel game pieces without a pre-placed template.\",\"Use game.spawnGroup and game.setObjectTransform for server-owned game pieces; use game.sweepAABB before fast scripted movement.\",\"Use game.getPlayerData/setPlayerData for per-account persistent scores, checking authentication and save errors.\"]}";
}

inline std::string getQuestGameExample()
{
	const std::string script = R"luau(--lua
-- Server-authoritative story quest with durable per-account progress.
local KEY = "quest_ruins_v1"
local RELIC_UIDS = {12011,12012,12013} -- replace with the three relic object UIDs
local function loadQuest(avatar)
  local data, version = game.getPlayerData(avatar, KEY)
  if type(data) ~= "table" then data = {stage="offered", relics=0, claimed=false} end
  return data, version
end
local function showQuest(avatar, data)
  local actions = {}
  if data.stage == "offered" then actions={{id="accept",label="Accept the quest"}}
  elseif data.stage == "active" and data.relics >= 3 then actions={{id="turn_in",label="Return the three relics"}}
  else actions={{id="status",label="Quest status"}} end
  local text = "Ancient Ruins\nRelics: " .. tostring(data.relics) .. "/3\nStage: " .. data.stage
  if data.claimed then text=text .. "\nReward claimed." end
  game.setHUDMenu(avatar,"The Lost Relics",actions,text)
end
if IS_SERVER then
  for _,uid in ipairs(RELIC_UIDS) do
    addEventListener("onUserUsedObject",uid,function(avatar,relic)
      local data, version=loadQuest(avatar)
      if data.stage~="active" or data.relics>=3 then return end
      data.relics=data.relics+1
      local ok, reason=game.setPlayerData(avatar,KEY,version,data)
      if not ok then print("Relic save failed: " .. tostring(reason)); return end
      showQuest(avatar,data)
    end)
  end
end
function onUserGameSessionChanged(avatar, object, active)
  if not IS_SERVER or not active then return end
  local data = loadQuest(avatar)
  showQuest(avatar,data)
end
function onUserGameAction(avatar, object, action_id, values)
  if not IS_SERVER then return end
  local data, version = loadQuest(avatar)
  if action_id == "accept" and data.stage == "offered" then data.stage="active"
  elseif action_id == "turn_in" and data.stage == "active" and data.relics >= 3 and not data.claimed then
    data.stage="complete"; data.claimed=true; data.reward="ruins_badge"
  elseif action_id ~= "status" then return end
  if action_id ~= "status" then
    local ok, reason = game.setPlayerData(avatar,KEY,version,data)
    if not ok then print("Quest save failed: " .. tostring(reason)); return end
  end
  showQuest(avatar,data)
end
)luau";
	return "{\"purpose\":\"Server-validated quest with three configured relic objects, persistent per-account progress, accept/turn-in actions and one-time reward claim.\",\"configuration\":\"Replace RELIC_UIDS with three existing resource/relic object UIDs. The player must accept the quest, focus this game host, and use each relic object.\",\"requirements\":\"Persistent player data requires an active focused game session and an authenticated account.\",\"script\":\"" + web::Escaping::JSONEscape(script) + "\"}";
}

inline std::string getResourceGameExample()
{
	const std::string script = R"luau(--lua
-- Resource nodes with per-player respawn and per-account inventory.
local NODE_UIDS = {12001,12002,12003} -- replace with actual resource object UIDs
local RESPAWN_SECONDS = 30
local function inventory(avatar)
  local data, version = game.getPlayerData(avatar,"inventory_v1")
  if type(data) ~= "table" then data={counts={}, node_ready_at={}} end
  if type(data.counts) ~= "table" then data.counts={} end
  if type(data.node_ready_at) ~= "table" then data.node_ready_at={} end
  return data, version
end
local function refresh(avatar, data)
  local actions={{id="bag",label="Check inventory"}}
  game.setHUDMenu(avatar,"Field Kit",actions,"Crystals: " .. tostring(data.counts.crystal or 0) ..
    "\nIron: " .. tostring(data.counts.iron or 0) .. "\nHerbs: " .. tostring(data.counts.herb or 0))
end
local function registerNode(uid)
  addEventListener("onUserUsedObject",uid,function(avatar,node)
    if not IS_SERVER then return end
    local bag, bag_version=inventory(avatar)
    local node_key=tostring(uid)
    local now=getCurrentTime()
    if (bag.node_ready_at[node_key] or 0)>now then return end
    local kind=(uid % 3 == 0) and "crystal" or ((uid % 3 == 1) and "iron" or "herb")
    bag.counts[kind]=(bag.counts[kind] or 0)+1
    bag.node_ready_at[node_key]=now+RESPAWN_SECONDS
    local saved, save_error=game.setPlayerData(avatar,"inventory_v1",bag_version,bag)
    if not saved then print("Inventory save failed: " .. tostring(save_error)); return end
    refresh(avatar,bag)
  end)
end
if IS_SERVER then for _,uid in ipairs(NODE_UIDS) do registerNode(uid) end end
function onUserGameSessionChanged(avatar, object, active)
  if IS_SERVER and active then local bag=inventory(avatar); refresh(avatar,bag) end
end
function onUserGameAction(avatar, object, action_id, values)
  if IS_SERVER and action_id=="bag" then local bag=inventory(avatar); refresh(avatar,bag) end
end
)luau";
	return "{\"purpose\":\"Configurable harvestable world nodes, per-player respawn timers, and persistent per-player inventories.\",\"configuration\":\"Replace NODE_UIDS with placed resource object UIDs. Give each node an onUserUsedObject event (the host registers listeners); players focus the game host first. Adjust RESPAWN_SECONDS and resource-kind mapping. Test permissions and cooldown behavior in-world.\",\"limits\":\"Inventory and per-player node cooldowns share one versioned player-data record, so each harvest saves atomically as one record. Saves require authenticated players in the focused game session. Nodes are independently harvestable by each account.\",\"script\":\"" + web::Escaping::JSONEscape(script) + "\"}";
}

inline std::string getStrategyGameExample()
{
	const std::string script = R"luau(--lua
-- Small turn-based tactical strategy prototype on a 6x6 grid.
local N=6
local function fresh()
  return {schema="tactics_v1",turn=1,players={},units={{x=1,y=1,hp=3},{x=6,y=6,hp=3}},status="waiting"}
end
local function stateNow()
  local s=game.getState()
  if type(s)=="table" and s.schema=="tactics_v1" then return s end
  return fresh()
end
local function playerIndex(s,uid)
  for i,p in ipairs(s.players) do if p.uid==uid then return i end end
  return 0
end
local function render(s)
  local grid={}
  for y=1,N do
    local row={}
    for x=1,N do
      local c="·"
      for i,u in ipairs(s.units) do if u.hp>0 and u.x==x and u.y==y then c=(i==1 and "A" or "B") end end
      row[#row+1]=c
    end
    grid[#grid+1]=table.concat(row," ")
  end
  for _,av in ipairs(game.getPlayers()) do
    local i=playerIndex(s,av.uid)
    local actions={}
    if s.status=="playing" and i==s.turn then
      actions={{id="up",label="Move up"},{id="down",label="Move down"},
        {id="left",label="Move left"},{id="right",label="Move right"},{id="attack",label="Attack adjacent"}}
    elseif s.status=="finished" then actions={{id="restart",label="Start another match"}}
    else actions={{id="wait",label="Waiting for your turn"}} end
    local status=s.status=="finished" and ("Winner: player " .. tostring(s.winner)) or ("Turn: " .. tostring(s.turn))
    game.setHUDMenu(av,"Tactical Grid",actions,status .. "\n" .. table.concat(grid,"\n"))
  end
end
local function start()
  local s=fresh()
  for _,av in ipairs(game.getPlayers()) do if #s.players<2 then s.players[#s.players+1]={uid=av.uid} end end
  if #s.players==2 then s.status="playing" end
  game.setState(s); render(s)
end
function onUserGameSessionChanged(avatar,object,active)
  if not IS_SERVER then return end
  local s=stateNow()
  if s.status=="waiting" then start()
	elseif not active and playerIndex(s,avatar.uid)>0 then s.status="finished"; s.winner=3-playerIndex(s,avatar.uid); game.setState(s); render(s)
	else render(s) end
end
function onUserGameAction(avatar,object,action_id,values)
  if not IS_SERVER then return end
  local s=stateNow(); local i=playerIndex(s,avatar.uid)
	if s.status=="finished" and action_id=="restart" then start(); return end
  if s.status~="playing" or i~=s.turn then return end
  local u=s.units[i]
  if action_id=="attack" then
    local target=s.units[3-i]
    if math.abs(u.x-target.x)+math.abs(u.y-target.y)==1 then
      target.hp=target.hp-1
      if target.hp<=0 then s.status="finished"; s.winner=i end
    end
  else
    local dx=(action_id=="left" and -1) or (action_id=="right" and 1) or 0
    local dy=(action_id=="up" and 1) or (action_id=="down" and -1) or 0
    if dx==0 and dy==0 then return end
    local nx,ny=u.x+dx,u.y+dy
    if nx<1 or nx>N or ny<1 or ny>N then return end
    local other=s.units[3-i]
    if other.hp>0 and other.x==nx and other.y==ny then return end
    u.x,u.y=nx,ny
  end
  s.turn=3-i
  game.setState(s); render(s)
end
if IS_SERVER then
  local s=stateNow()
  if s.status=="waiting" then start() else render(s) end
end
)luau";
	return "{\"purpose\":\"Server-validated turn-based tactics example with two participants, movement, adjacent attacks, health, turn ownership and HUD. Extend with terrain, unit classes, action points and resource costs.\",\"limitations\":\"The sample renders a symbolic grid in HUD; it does not automatically create 3D units or implement pathfinding. Add template UIDs and server-side movement validation for visual units.\",\"script\":\"" + web::Escaping::JSONEscape(script) + "\"}";
}

inline std::string getRPGProgressionExample()
{
	const std::string script = R"luau(--lua
-- Server-authoritative XP, levels, health, damage, gold and a three-second action cooldown.
local KEY="rpg_demo_v1"
local function load(avatar)
  local d,v=game.getPlayerData(avatar,KEY)
  if type(d)~="table" then d={level=1,xp=0,hp=10,max_hp=10,gold=0,enemy_hp=8,next_action=0} end
  return d,v
end
local function render(avatar,d)
  local actions={{id="attack",label="Attack training monster"},{id="rest",label="Restore health"},{id="train",label="Train (+5 XP)"}}
  local body="Level "..d.level.."  XP "..d.xp.."/"..(d.level*100).."\nHP "..d.hp.."/"..d.max_hp..
    "  Gold "..d.gold.."\nMonster HP "..d.enemy_hp
  game.setHUDMenu(avatar,"RPG Progress",actions,body)
end
local function save(avatar,d,v)
  local ok,err=game.setPlayerData(avatar,KEY,v,d)
  if not ok then print("RPG save rejected: "..tostring(err)); return false end
  render(avatar,d); return true
end
function onUserGameSessionChanged(avatar,object,active)
  if IS_SERVER and active then local d=load(avatar); render(avatar,d) end
end
function onUserGameAction(avatar,object,action_id,values)
  if not IS_SERVER then return end
  local d,v=load(avatar); local now=getCurrentTime()
  if action_id=="rest" then d.hp=d.max_hp
  elseif now<d.next_action then return
  elseif action_id=="train" then d.xp=d.xp+5
  elseif action_id=="attack" then
    if d.hp<=0 then return end
    local damage=game.randomInt(2,2+math.floor(d.level/2))
    d.enemy_hp=d.enemy_hp-damage
    if d.enemy_hp<=0 then d.enemy_hp=8; d.xp=d.xp+25; d.gold=d.gold+10
    else d.hp=math.max(0,d.hp-game.randomInt(1,3)) end
  else return end
  if action_id~="rest" then d.next_action=now+3 end
  while d.xp>=d.level*100 do
    d.xp=d.xp-d.level*100; d.level=d.level+1; d.max_hp=d.level*10; d.hp=d.max_hp
  end
  save(avatar,d,v)
end
)luau";
	return "{\"purpose\":\"Working RPG progression template: per-account XP, level thresholds, health, damage, gold, monster reset and server-side cooldown.\",\"requirements\":\"Authenticated account and active focused session; progress is saved in one versioned player-data record. Replace the training monster with configured enemy objects and validate each target.\",\"script\":\"" + web::Escaping::JSONEscape(script) + "\"}";
}

inline std::string getMonopolyExample()
{
	const std::string script = R"luau(--lua
-- Lightweight two-player property-board game inspired by Monopoly; not the trademark board/rules.
local PROPERTIES={2,4,6,8,10}
local function fresh() return {schema="property_board_v1",status="waiting",players={},pos={0,0},cash={1200,1200},owner={},turn=1,pending=0} end
local function stateNow() local s=game.getState(); if type(s)=="table" and s.schema=="property_board_v1" then return s end; return fresh() end
local function isProperty(i) for _,p in ipairs(PROPERTIES) do if p==i then return true end end; return false end
local function render(s)
  for _,av in ipairs(game.getPlayers()) do
    local actions={{id="wait",label="Wait"}}
    if s.status=="playing" and s.players[s.turn] and s.players[s.turn].uid==av.uid then
      if s.pending>0 then actions={{id="buy",label="Buy for 200"},{id="skip",label="Leave unowned"}}
      else actions={{id="roll",label="Roll and move"}} end
    end
    local body="Turn: player "..s.turn.."  Cash: "..s.cash[1].." / "..s.cash[2].."\nPlaces: "..s.pos[1].." / "..s.pos[2]
    if s.pending>0 then body=body.."\nUnowned property "..s.pending.." costs 200." end
    if s.status=="finished" then body=body.."\nWinner: player "..tostring(s.winner) end
    game.setHUDMenu(av,"Property Board",actions,body)
  end
end
local function start()
  local s=fresh(); for _,av in ipairs(game.getPlayers()) do if #s.players<2 then s.players[#s.players+1]={uid=av.uid} end end
  if #s.players==2 then s.status="playing" end; game.setState(s); render(s)
end
function onUserGameSessionChanged(avatar,object,active)
  if not IS_SERVER then return end; local s=stateNow()
  if s.status=="waiting" then start()
  elseif not active and s.players[1] and (s.players[1].uid==avatar.uid or (s.players[2] and s.players[2].uid==avatar.uid)) then
    local leaver=(s.players[1].uid==avatar.uid) and 1 or 2; s.status="finished"; s.winner=3-leaver; game.setState(s); render(s)
  else render(s) end
end
function onUserGameAction(avatar,object,action_id,values)
  if not IS_SERVER then return end; local s=stateNow(); local i=0
  for n,p in ipairs(s.players) do if p.uid==avatar.uid then i=n end end
  if s.status~="playing" or i~=s.turn then return end
  if action_id=="buy" and s.pending>0 and s.cash[i]>=200 then s.cash[i]=s.cash[i]-200; s.owner[s.pending]=i; s.pending=0; s.turn=3-i
  elseif action_id=="skip" and s.pending>0 then s.pending=0; s.turn=3-i
  elseif action_id=="roll" and s.pending==0 then
    local steps=game.randomInt(1,6)+game.randomInt(1,6); local old=s.pos[i]; local pos=(old+steps)%12
    if old+steps>=12 then s.cash[i]=s.cash[i]+100 end; s.pos[i]=pos
    if isProperty(pos) and not s.owner[pos] then s.pending=pos
    elseif s.owner[pos] and s.owner[pos]~=i then
      local rent=50; local other=s.owner[pos]
      s.cash[i]=s.cash[i]-rent; s.cash[other]=s.cash[other]+rent
      if s.cash[i]<0 then s.status="finished"; s.winner=other else s.turn=3-i end
    else s.turn=3-i end
  else return end
  game.setState(s); render(s)
end
if IS_SERVER then local s=stateNow(); if s.status=="waiting" then start() else render(s) end end
)luau";
	return "{\"purpose\":\"Playable two-player property-board example: server-secure dice, turn validation, cash, start bonus, property purchase, rent and bankruptcy.\",\"limitations\":\"This is a compact original prototype, not the full Monopoly rules. Add deeds, houses, cards, jail, trades and a larger board as separate validated state transitions.\",\"script\":\"" + web::Escaping::JSONEscape(script) + "\"}";
}

inline std::string getCardGameExample()
{
	const std::string script = R"luau(--lua
-- Two-player high-card duel with a server-generated deterministic shuffle.
local function fresh() return {schema="high_card_v1",status="waiting",players={},round=0,score={0,0},turn=1,last={0,0}} end
local function stateNow() local s=game.getState(); if type(s)=="table" and s.schema=="high_card_v1" then return s end; return fresh() end
local function start()
  local s=fresh(); for _,av in ipairs(game.getPlayers()) do if #s.players<2 then s.players[#s.players+1]={uid=av.uid} end end
  local deck={}; for i=1,52 do deck[i]=((i-1)%13)+2 end
  for i=52,2,-1 do local j=game.randomInt(1,i); deck[i],deck[j]=deck[j],deck[i] end
  local old_deck,deck_version=game.getData("private_deck_v1")
  local saved,reason=game.setData("private_deck_v1",deck_version,deck)
  if not saved then print("Could not save private deck: "..tostring(reason)); return end
  if #s.players==2 then s.status="playing" end; game.setState(s)
  for _,av in ipairs(game.getPlayers()) do
    local actions=(s.status=="playing" and s.players[1].uid==av.uid) and {{id="draw",label="Draw a card"}} or {{id="wait",label="Wait"}}
    game.setHUDMenu(av,"High Card",actions,"First to 5 points wins.")
  end
end
local function render(s)
  for _,av in ipairs(game.getPlayers()) do
    local i=(s.players[1] and s.players[1].uid==av.uid) and 1 or ((s.players[2] and s.players[2].uid==av.uid) and 2 or 0)
    local actions=(s.status=="playing" and i==s.turn) and {{id="draw",label="Draw a card"}} or {{id="wait",label="Wait"}}
    local body="Score "..s.score[1].." - "..s.score[2].."\nLast cards "..s.last[1].." - "..s.last[2]
    if s.status=="finished" then body=body..(s.winner==0 and "\nDraw" or ("\nWinner: player "..tostring(s.winner))) end
    game.setHUDMenu(av,"High Card",actions,body)
  end
end
function onUserGameSessionChanged(avatar,object,active)
  if not IS_SERVER then return end; local s=stateNow()
  if not active and s.players[1] and (s.players[1].uid==avatar.uid or (s.players[2] and s.players[2].uid==avatar.uid)) then
    s.status="finished"; s.winner=(s.players[1].uid==avatar.uid) and 2 or 1; game.setState(s); render(s)
  elseif s.status=="waiting" then start() else render(s) end
end
function onUserGameAction(avatar,object,action_id,values)
  if not IS_SERVER or action_id~="draw" then return end; local s=stateNow(); local i=0
  for n,p in ipairs(s.players) do if p.uid==avatar.uid then i=n end end
  if s.status~="playing" or i==0 or i~=s.turn or s.round>=26 then return end
  local deck=game.getData("private_deck_v1")
  if type(deck)~="table" then return end
  local card_index=s.round*2+1
  s.last[1]=deck[card_index]; s.last[2]=deck[card_index+1]; s.round=s.round+1
  if s.last[1]>s.last[2] then s.score[1]=s.score[1]+1 elseif s.last[2]>s.last[1] then s.score[2]=s.score[2]+1 end
  if s.score[1]>=5 or s.score[2]>=5 or s.round>=26 then
    s.status="finished"
    if s.score[1]==s.score[2] then s.winner=0 else s.winner=(s.score[1]>s.score[2]) and 1 or 2 end
  end
  s.turn=3-i
  game.setState(s); render(s)
end
if IS_SERVER then local s=stateNow(); if s.status=="waiting" then start() else render(s) end end
)luau";
	return "{\"purpose\":\"Server-shuffled two-player card-game starter with turn checks, private server-side deck, score and secure random shuffle. Extend rank/suit handling into poker, blackjack or collectible-card rules.\",\"limitations\":\"This sample uses high-card rounds and does not include betting, private hands or full deck-building rules. Keep hidden card identities server-side and send each player only their own hand if adding private cards.\",\"script\":\"" + web::Escaping::JSONEscape(script) + "\"}";
}

inline std::string getEconomyGameExample()
{
	const std::string script = R"luau(--lua
-- One-record RPG economy: rarity loot, crafting and a small NPC shop.
local KEY="economy_v1"
local function load(avatar)
  local d,v=game.getPlayerData(avatar,KEY)
  if type(d)~="table" then d={gold=100,items={wood=0,iron=0,crystal=0,sword=0,potion=0},last_chest=0} end
  if type(d.items)~="table" then d.items={} end
  return d,v
end
local function render(avatar,d)
  local a={{id="chest",label="Open loot chest"},{id="craft",label="Craft sword (3 wood + 1 iron)"},{id="potion",label="Buy potion (25 gold)"}}
  local i=d.items
  local body="Gold "..d.gold.."\\nWood "..(i.wood or 0).."  Iron "..(i.iron or 0).."  Crystal "..(i.crystal or 0)..
    "\\nSwords "..(i.sword or 0).."  Potions "..(i.potion or 0)
  game.setHUDMenu(avatar,"Camp Economy",a,body)
end
local function commit(avatar,d,v)
  local ok,err=game.setPlayerData(avatar,KEY,v,d)
  if not ok then print("Economy save failed: "..tostring(err)); return end
  render(avatar,d)
end
function onUserGameSessionChanged(avatar,object,active)
  if IS_SERVER and active then local d=load(avatar); render(avatar,d) end
end
function onUserGameAction(avatar,object,action_id,values)
  if not IS_SERVER then return end
  local d,v=load(avatar); local items=d.items
  if action_id=="chest" then
    local now=getCurrentTime(); if now-d.last_chest<10 then return end
    d.last_chest=now; local roll=game.randomInt(1,100)
    if roll<=60 then items.wood=(items.wood or 0)+2
    elseif roll<=90 then items.iron=(items.iron or 0)+1
    else items.crystal=(items.crystal or 0)+1 end
  elseif action_id=="craft" then
    if (items.wood or 0)<3 or (items.iron or 0)<1 then return end
    items.wood=items.wood-3; items.iron=items.iron-1; items.sword=(items.sword or 0)+1
  elseif action_id=="potion" then
    if d.gold<25 then return end
    d.gold=d.gold-25; items.potion=(items.potion or 0)+1
  else return end
  commit(avatar,d,v)
end
)luau";
	return "{\"purpose\":\"Persistent single-account economy starter with server-randomized common/rare loot, recipe validation, crafting, gold and shop purchases. Each action commits inventory and currency together in one versioned player-data record.\",\"limitations\":\"This is an NPC shop, not player-to-player trade. Valuable cross-account trades need an atomic multi-account transaction API; two independent player-data writes are not an atomic exchange.\",\"script\":\"" + web::Escaping::JSONEscape(script) + "\"}";
}

inline std::string getSurvivalGameExample()
{
	const std::string script = R"luau(--lua
-- Per-account survival loop: hunger, thirst, health, food and water.
local KEY="survival_v1"
local function load(avatar)
  local d,v=game.getPlayerData(avatar,KEY)
  if type(d)~="table" then d={hunger=100,thirst=100,hp=100,food=5,water=5} end
  return d,v
end
local function render(avatar,d)
  local actions={{id="eat",label="Eat food"},{id="drink",label="Drink water"}}
  game.setHUDMenu(avatar,"Survival",actions,"Hunger "..d.hunger.."  Thirst "..d.thirst.."\\nHealth "..d.hp.."  Food "..d.food.."  Water "..d.water)
end
local function tickPlayer(avatar)
  local d,v=load(avatar)
  d.hunger=math.max(0,d.hunger-1); d.thirst=math.max(0,d.thirst-2)
  if d.hunger==0 or d.thirst==0 then d.hp=math.max(0,d.hp-1) end
  local ok,err=game.setPlayerData(avatar,KEY,v,d)
  if ok then render(avatar,d) else print("Survival save unavailable: "..tostring(err)) end
end
function onUserGameSessionChanged(avatar,object,active)
  if IS_SERVER and active then local d=load(avatar); render(avatar,d) end
end
function onUserGameAction(avatar,object,action_id,values)
  if not IS_SERVER then return end; local d,v=load(avatar)
  if action_id=="eat" and d.food>0 then d.food=d.food-1; d.hunger=math.min(100,d.hunger+35)
  elseif action_id=="drink" and d.water>0 then d.water=d.water-1; d.thirst=math.min(100,d.thirst+45)
  else return end
  local ok,err=game.setPlayerData(avatar,KEY,v,d)
  if ok then render(avatar,d) else print("Survival save unavailable: "..tostring(err)) end
end
if IS_SERVER then game.createLoop(function(host,dt)
  for _,avatar in ipairs(game.getPlayers()) do tickPlayer(avatar) end
end,1) end
)luau";
	return "{\"purpose\":\"Survival-game starter with server-loop hunger/thirst, health loss, consumable food/water and persistent player stats.\",\"requirements\":\"Authenticated players must focus the host; player-data saves are versioned and quota-limited. Tune the loop interval and decay rates for the intended game pace.\",\"limitations\":\"This template has no world pickup objects, shelter, temperature or crafting recipes. Add resource-node listeners and update the same player record when granting consumables.\",\"script\":\"" + web::Escaping::JSONEscape(script) + "\"}";
}

inline std::string getTycoonGameExample()
{
	const std::string script = R"luau(--lua
-- Per-account production tycoon: gather ore, refine bars, sell goods and upgrade capacity.
local KEY="tycoon_factory_v1"
local function load(avatar)
  local d,v=game.getPlayerData(avatar,KEY)
  if type(d)~="table" then d={cash=200,level=1,ore=0,bars=0,last_collect=0} end
  return d,v
end
local function render(avatar,d)
  local a={{id="collect",label="Mine ore"},{id="refine",label="Refine 3 ore"},
    {id="sell",label="Sell all bars"},{id="upgrade",label="Upgrade factory"}}
  game.setHUDMenu(avatar,"Factory Tycoon",a,"Cash "..d.cash.."  Factory level "..d.level..
    "\\nOre "..d.ore.."  Metal bars "..d.bars)
end
local function commit(avatar,d,v)
  local ok,err=game.setPlayerData(avatar,KEY,v,d)
  if not ok then print("Factory save failed: "..tostring(err)); return end
  render(avatar,d)
end
function onUserGameSessionChanged(avatar,object,active)
  if IS_SERVER and active then local d=load(avatar); render(avatar,d) end
end
function onUserGameAction(avatar,object,action_id,values)
  if not IS_SERVER then return end; local d,v=load(avatar)
  if action_id=="collect" then
    local now=getCurrentTime(); if now-d.last_collect<2 then return end
    d.last_collect=now; d.ore=d.ore+game.randomInt(1,2+d.level)
  elseif action_id=="refine" then
    if d.ore<3 then return end; d.ore=d.ore-3; d.bars=d.bars+1
  elseif action_id=="sell" then
    if d.bars<1 then return end; d.cash=d.cash+d.bars*20; d.bars=0
  elseif action_id=="upgrade" then
    local price=d.level*150; if d.cash<price or d.level>=20 then return end
    d.cash=d.cash-price; d.level=d.level+1
  else return end
  commit(avatar,d,v)
end
)luau";
	return "{\"purpose\":\"Tycoon/economy example with production chain, capacity progression, cooldown, selling and single-record atomic player saves.\",\"extension_points\":\"Add placed machines with configured template UIDs, conveyors, research, workers, contracts and price changes. Keep purchases and inventory changes in one versioned record; use bounded rates and validate every action on the server.\",\"script\":\"" + web::Escaping::JSONEscape(script) + "\"}";
}

inline std::string getWaveSurvivalExample()
{
	const std::string script = R"luau(--lua
-- Shared co-op wave survival rules and base health; enemy visuals can be spawned from authored templates.
local function fresh() return {schema="wave_defence_v1",wave=1,base_hp=100,enemies=3,enemy_hp=5,kills=0,players={}} end
local function stateNow() local s=game.getState(); if type(s)=="table" and s.schema=="wave_defence_v1" then return s end; return fresh() end
local function joined(s,uid) for _,p in ipairs(s.players) do if p.uid==uid then return true end end; return false end
local function render(s)
  for _,av in ipairs(game.getPlayers()) do
    local actions=s.base_hp>0 and {{id="attack",label="Attack an enemy"}} or {{id="wait",label="Base destroyed"}}
    game.setHUDMenu(av,"Wave Defence",actions,"Wave "..s.wave.."  Base HP "..s.base_hp.."\\nEnemies "..s.enemies.."  Current enemy HP "..s.enemy_hp.."  Team kills "..s.kills)
  end
end
function onUserGameSessionChanged(avatar,object,active)
  if not IS_SERVER then return end; local s=stateNow()
  if active and not joined(s,avatar.uid) then s.players[#s.players+1]={uid=avatar.uid} end
  if not active then for i,p in ipairs(s.players) do if p.uid==avatar.uid then table.remove(s.players,i); break end end end
  game.setState(s); render(s)
end
function onUserGameAction(avatar,object,action_id,values)
  if not IS_SERVER or action_id~="attack" then return end
  local s=stateNow(); if not joined(s,avatar.uid) or s.base_hp<=0 or s.enemies<=0 then return end
  local damage=game.randomInt(1,3); s.enemy_hp=s.enemy_hp-damage
  if s.enemy_hp<=0 then
    s.enemies=s.enemies-1; s.kills=s.kills+1; s.enemy_hp=5
    if s.enemies==0 then s.wave=s.wave+1; s.enemies=s.wave*3; s.base_hp=math.max(0,s.base_hp-game.randomInt(5,12)) end
  end
  game.setState(s); render(s)
end
if IS_SERVER then render(stateNow()) end
)luau";
	return "{\"purpose\":\"Co-op team/wave rules with shared base health, server-validated attacks, wave scaling and a clear place to connect 3D enemy templates.\",\"limitations\":\"This sample tracks abstract enemies in game state; it does not create enemy models or simulate navigation. Add bounded template spawning, target selection and one server loop for enemy attacks.\",\"script\":\"" + web::Escaping::JSONEscape(script) + "\"}";
}

inline std::string getBilliardsGameReference()
{
	return R"json({"purpose":"Build a billiards-style game using a server-side 2D state simulation and HUD shot controls.","model":"Represent the table and balls as bounded game state: each ball has x,y,vx,vy,radius,active. Validate shooter turn, cue-ball placement, normalized aim direction and power before setting cue velocity. Advance fixed substeps in game.createLoop; apply friction, resolve circle-circle and cushion collisions, detect pockets, stop at low speed, then award the next turn. Emit local sound/animation cues with game.emitEvent; keep all scoring and pocket rules in the server state.","controls":"A compact prototype can use eight HUD aim actions and several power actions, then use game.getInput pointer coordinates for finer aiming. Never accept client-submitted ball positions or scores.","physics_boundary":"Current game movement helpers use conservative AABB collision and do not expose rigid-body impulses, exact contact manifolds, spin, cue torque or guaranteed frame synchronization. A Luau circle simulator can provide arcade billiards, but realistic 3D billiards requires a dedicated authoritative physics feature."})json";
}
}
