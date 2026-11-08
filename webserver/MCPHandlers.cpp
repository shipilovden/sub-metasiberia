/*=====================================================================
MCPHandlers.cpp
---------------
=====================================================================*/
#include "MCPHandlers.h"

#include "RequestInfo.h"
#include "ResponseUtils.h"
#include "Escaping.h"
#include "../server/ServerWorldState.h"
#include "../server/User.h"
#include "../shared/WorldObject.h"
#include "../shared/WorldMaterial.h"
#include "../shared/Parcel.h"
#include "../shared/Avatar.h"
#include "../shared/ResourceManager.h"
#include "../shared/WorldStateLock.h"
#include <JSONParser.h>
#include <Lock.h>
#include <StringUtils.h>
#include <Exception.h>
#include <ConPrint.h>
#include <lua/LuaScript.h>
#include <lua/LuaVM.h>
#include <Luau/Parser.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <mutex>
#include <set>
#include <tuple>
#include <utility>
#include <vector>
#include <dll/include/IndigoException.h>
#include <utils/IndigoXMLDoc.h>
#include "MCPModeling.h"
#include "MCPPubChem.h"
#include "MCPPolyfork.h"
#include "MCPGameExamples.h"
#include "MCPPrecision.h"
#include "../server/Server.h"
#include "../shared/SubstrataLuaVM.h"
#include "../shared/LuaScriptEvaluator.h"


namespace MCPHandlers
{

static std::string idJSON(const JSONParser& parser, const JSONNode& root)
{
	if(!root.hasChild("id")) return "null";
	const JSONNode& id = root.getChildNode(parser, "id");
	if(id.type == JSONNode::Type_String)
		return "\"" + web::Escaping::JSONEscape(id.getStringValue()) + "\"";
	if(id.type == JSONNode::Type_Number)
		return doubleToString(id.getDoubleValue());
	return "null";
}


static void writeJSON(web::ReplyInfo& reply, const std::string& body)
{
	web::ResponseUtils::writeHTTPOKHeaderAndData(reply, body.data(), body.size(), "application/json");
}


static void writeError(web::ReplyInfo& reply, const std::string& id, int code, const std::string& message)
{
	writeJSON(reply, "{\"jsonrpc\":\"2.0\",\"id\":" + id +
		",\"error\":{\"code\":" + toString(code) + ",\"message\":\"" + web::Escaping::JSONEscape(message) + "\"}}");
}


static std::string headerValue(const web::RequestInfo& request, const std::string& name)
{
	// equalCaseInsensitive folds only its first argument; the second must be lowercase.
	const std::string lowercase_name = ::toLowerCase(name);
	for(size_t i = 0; i < request.headers.size(); ++i)
		if(StringUtils::equalCaseInsensitive(request.headers[i].key, lowercase_name))
			return std::string(request.headers[i].value.data(), request.headers[i].value.size());
	return std::string();
}


static std::string cookieValue(const web::RequestInfo& request, const std::string& name)
{
	for(size_t i = 0; i < request.cookies.size(); ++i)
		if(StringUtils::equalCaseInsensitive(request.cookies[i].key, name))
			return request.cookies[i].value;
	return std::string();
}


static bool authenticateToken(const ServerAllWorldsState& world_state, const std::string& auth, UserID& user_id_out, std::string& username_out)
{
	const std::string prefix = "Substrata-Login ";
	if(auth.size() <= prefix.size() || auth.compare(0, prefix.size(), prefix) != 0)
		return false;
	const std::string token = auth.substr(prefix.size());
	const size_t dot = token.find('.');
	if(dot == std::string::npos || token.find('.', dot + 1) != std::string::npos)
		return false;
	try
	{
		const std::vector<unsigned char> name_bytes = StringUtils::convertHexToBinary(token.substr(0, dot));
		const std::vector<unsigned char> password_bytes = StringUtils::convertHexToBinary(token.substr(dot + 1));
		const std::string username((const char*)name_bytes.data(), name_bytes.size());
		const std::string password((const char*)password_bytes.data(), password_bytes.size());
		const auto it = world_state.name_to_users.find(username);
		if(it == world_state.name_to_users.end() || it->second.isNull() || !it->second->isPasswordValid(password))
			return false;
		user_id_out = it->second->id;
		username_out = it->second->name;
		return true;
	}
	catch(glare::Exception&)
	{
		return false;
	}
}


static bool authenticate(const ServerAllWorldsState& world_state, const web::RequestInfo& request, UserID& user_id_out, std::string& username_out)
{
	// The public reverse proxy may remove or rewrite Authorization.  The local
	// MCP bridge sends the same login token in a dedicated fallback header. Try
	// both values, while keeping the token out of the MCP URL and Codex config.
	if(authenticateToken(world_state, headerValue(request, "Authorization"), user_id_out, username_out))
		return true;
	if(authenticateToken(world_state, headerValue(request, "X-Substrata-Login"), user_id_out, username_out))
		return true;
	return authenticateToken(world_state, "Substrata-Login " + cookieValue(request, "mcp-login"), user_id_out, username_out);
}


static std::string getObjectScriptLog(ServerAllWorldsState& world_state,
	ServerWorldState& world, const JSONParser& parser, const JSONNode& args, const UserID& user_id)
{
	const double raw_uid = args.getChildDoubleValue(parser, "uid");
	if(!std::isfinite(raw_uid) || raw_uid < 0 || raw_uid > 9007199254740991.0 || raw_uid != std::floor(raw_uid))
		throw glare::Exception("uid must be an exact non-negative integer.");
	const double raw_limit = args.getChildDoubleValueWithDefaultVal(parser, "limit", 20);
	if(!std::isfinite(raw_limit) || raw_limit < 1 || raw_limit > 50 || raw_limit != std::floor(raw_limit))
		throw glare::Exception("limit must be an integer from 1 to 50.");
	const size_t limit = (size_t)raw_limit;
	Reference<UserScriptLog> log;
	UID object_uid;
	bool execution_enabled = false, evaluator_loaded = false, evaluator_failed = false;
	size_t active_timers = 0, active_players = 0, spawned_game_objects = 0, rejected_game_rpc_messages = 0, rejected_game_input_updates = 0, rejected_game_events = 0;
	bool has_game_state = false;
	uint32 game_state_version = 0;
	size_t game_state_bytes = 0;
	{
		WorldStateLock lock(world_state.mutex);
		WorldObjectRef object = Modeling::findObject(world, lock, UID((uint64)raw_uid));
		if(!Modeling::editable(*object, world, user_id))
			throw glare::Exception("Permission denied for this object's script diagnostics.");
		object_uid = object->uid;
		execution_enabled = BitUtils::isBitSet(world_state.feature_flag_info.feature_flags, ServerAllWorldsState::SERVER_SCRIPT_EXEC_FEATURE_FLAG);
		evaluator_loaded = object->lua_script_evaluator.nonNull();
		evaluator_failed = evaluator_loaded && object->lua_script_evaluator->hit_error;
		if(evaluator_loaded && !evaluator_failed)
			for(int i=0; i<LuaScriptEvaluator::MAX_NUM_TIMERS; ++i)
				if(object->lua_script_evaluator->timers[i].id >= 0) ++active_timers;
		active_players = world.getGameSessionParticipants(object_uid, lock).size();
		spawned_game_objects = world.getGameSpawnedObjectCount(object_uid, lock);
		rejected_game_rpc_messages = world.getRejectedGameRPCMessageCount(object_uid, lock);
		rejected_game_input_updates = world.getRejectedGameInputUpdateCount(object_uid, lock);
		rejected_game_events = world.getRejectedGameEventCount(object_uid, lock);
		GameStateSnapshot game_state;
		has_game_state = world.getGameStateSnapshot(object_uid, game_state, lock);
		if(has_game_state)
		{
			game_state_version = game_state.version;
			game_state_bytes = game_state.serialized_value.size();
		}
		const auto it = world_state.user_script_log.find(object->creator_id);
		if(it != world_state.user_script_log.end()) log = it->second;
	}

	std::vector<UserScriptLogMessage> matching;
	if(log.nonNull())
	{
		Lock log_lock(log->mutex);
		for(auto it = log->messages.beginIt(); it != log->messages.endIt(); ++it)
			if(it->script_ob_uid == object_uid)
				matching.push_back(*it);
	}
	const size_t total = matching.size();
	if(matching.size() > limit)
		matching.erase(matching.begin(), matching.end() - (ptrdiff_t)limit);
	std::string result = "{\"uid\":" + toString(object_uid.value()) +
		",\"server_execution_enabled\":" + std::string(execution_enabled ? "true" : "false") +
		",\"server_evaluator_loaded\":" + std::string(evaluator_loaded ? "true" : "false") +
		",\"server_evaluator_failed\":" + std::string(evaluator_failed ? "true" : "false") +
		",\"active_timers\":" + toString(active_timers) +
		",\"active_game_players\":" + toString(active_players) +
		",\"rejected_game_rpc_messages\":" + toString(rejected_game_rpc_messages) +
		",\"rejected_game_input_updates\":" + toString(rejected_game_input_updates) +
		",\"rejected_game_events\":" + toString(rejected_game_events) +
		",\"spawned_game_objects\":" + toString(spawned_game_objects) +
		",\"has_game_state\":" + std::string(has_game_state ? "true" : "false") +
		",\"game_state_version\":" + toString(game_state_version) +
		",\"game_state_bytes\":" + toString(game_state_bytes) +
		",\"total_matching_messages\":" + toString(total) + ",\"messages\":[";
	for(size_t i=0; i<matching.size(); ++i)
	{
		if(i) result += ",";
		const UserScriptLogMessage& message = matching[i];
		static const size_t MAX_DIAGNOSTIC_MESSAGE_BYTES = 4096;
		size_t message_bytes = myMin(message.msg.size(), MAX_DIAGNOSTIC_MESSAGE_BYTES);
		const bool message_truncated = message_bytes < message.msg.size();
		if(message_truncated)
			while(message_bytes > 0 && (((unsigned char)message.msg[message_bytes] & 0xC0) == 0x80)) --message_bytes;
		const std::string message_text = message.msg.substr(0, message_bytes);
		result += "{\"time\":\"" + web::Escaping::JSONEscape(message.time.RFC822FormatedString()) +
			"\",\"type\":\"" + std::string(message.msg_type == UserScriptLogMessage::MessageType_error ? "error" : "print") +
			"\",\"message\":\"" + web::Escaping::JSONEscape(message_text) +
			"\",\"message_truncated\":" + std::string(message_truncated ? "true" : "false") + "}";
	}
	result += "]}";
	return result;
}


static unsigned int gameSideConditionMask(const Luau::AstExpr* expression)
{
	bool negated = false;
	for(size_t depth = 0; expression && depth < 256; ++depth)
	{
		if(const Luau::AstExprGroup* group = expression->as<Luau::AstExprGroup>())
			expression = group->expr;
		else if(const Luau::AstExprUnary* unary = expression->as<Luau::AstExprUnary>())
		{
			if(unary->op != Luau::AstExprUnary::Not) return 0;
			negated = !negated;
			expression = unary->expr;
		}
		else break;
	}
	if(!expression) return 0;
	const Luau::AstExprGlobal* global = expression->as<Luau::AstExprGlobal>();
	if(!global || !global->name.value) return 0;
	unsigned int mask = 0;
	if(std::strcmp(global->name.value, "IS_SERVER") == 0) mask = 1u;
	else if(std::strcmp(global->name.value, "IS_CLIENT") == 0) mask = 2u;
	if(negated) mask = mask == 1u ? 2u : mask == 2u ? 1u : 0u;
	if(mask != 0u) return mask;
	return 0;
}


class LuauGameApiMemberVisitor : public Luau::AstVisitor
{
public:
	bool visit(Luau::AstExprIndexName* node) override
	{
		const Luau::AstExprGlobal* base = node->expr->as<Luau::AstExprGlobal>();
		if(base && base->name.value && std::strcmp(base->name.value, "game") == 0 && node->index.value)
			members.insert(std::make_tuple(node->location.begin.line, std::string(node->index.value), guard_mask));
		return true;
	}

	bool visit(Luau::AstExprIndexExpr* node) override
	{
		const Luau::AstExprGlobal* base = node->expr->as<Luau::AstExprGlobal>();
		const Luau::AstExprConstantString* key = node->index->as<Luau::AstExprConstantString>();
		if(base && base->name.value && std::strcmp(base->name.value, "game") == 0)
		{
			if(key)
				members.insert(std::make_tuple(node->location.begin.line, std::string(key->value.data, key->value.size), guard_mask));
			else
				dynamic_access_lines.insert(node->location.begin.line);
		}
		return true;
	}

	bool visit(Luau::AstStatIf* node) override
	{
		const unsigned int then_guard = gameSideConditionMask(node->condition);
		if(then_guard == 0u)
			return true; // Leave compound/dynamic conditions to the ordinary visitor; do not infer a guard.
		node->condition->visit(this);
		const unsigned int saved_guard = guard_mask;
		const unsigned int else_guard = then_guard == 1u ? 2u : 1u;
		guard_mask = saved_guard & then_guard;
		if(node->thenbody) node->thenbody->visit(this);
		if(node->elsebody)
		{
			guard_mask = saved_guard & else_guard;
			node->elsebody->visit(this);
		}
		guard_mask = saved_guard;
		return false; // The condition and both branches were visited with their guard context.
	}

	std::set<std::tuple<unsigned int, std::string, unsigned int>> members;
	std::set<unsigned int> dynamic_access_lines;
	unsigned int guard_mask = 3u; // 1=server, 2=client, 3=unrestricted, 0=statically unreachable.
};


struct LuauGameApiDiagnostics
{
	std::string warnings_json;
	std::string uses_json;
	std::string dynamic_accesses_json;
	bool warnings_truncated = false;
	bool uses_truncated = false;
	bool dynamic_accesses_truncated = false;
};


static const char* gameApiMemberExecutionSide(const std::string& member)
{
	static const char* const server_members[] = {
		"getPlayers", "getPlayersNear", "getPlayersOverlapping", "getAimTarget", "getAimPlayerTarget",
		"sweepAABB", "moveObjectWithSlide", "setHUDText", "clearHUD", "setHUDMenu", "clearHUDMenu",
		"createLoop", "spawnObject", "createVoxelObject", "spawnGroup", "destroyObject", "clearSpawnedObjects", "setObjectTransform",
		"getOverlaps", "getOverlapChanges", "getData", "setData", "getPlayerData", "setPlayerData",
		"isKeyDown", "isActionDown", "getInput", "setState", "emitEvent", "playAvatarSpawnEffect", "randomInt"
	};
	static const char* const client_members[] = {
		"createClientLoop", "setCamera", "followCamera", "releaseCamera", "setPlayerControl", "getLocalInput", "raycast",
		"isLocalActionDown", "sendToServer"
	};
	static const char* const shared_members[] = {"getState", "bindAction"};
	for(const char* name : server_members) if(member == name) return "server";
	for(const char* name : client_members) if(member == name) return "client";
	for(const char* name : shared_members) if(member == name) return "both";
	return "unknown";
}


static const char* gameApiGuardContext(unsigned int guard_mask)
{
	if(guard_mask == 1u) return "server";
	if(guard_mask == 2u) return "client";
	if(guard_mask == 0u) return "unreachable";
	return "unrestricted";
}


static LuauGameApiDiagnostics gameApiDiagnosticsJSON(const std::string& script)
{
	// Keep this advisory registry aligned with SubstrataLuaVM's `game` table.
	// The runtime remains authoritative because scripts can mutate tables,
	// shadow globals, or use game[computed_key] and aliases.
	static const char* const supported_members[] = {
		"getPlayers", "getPlayersNear", "getPlayersOverlapping", "getAimTarget", "getAimPlayerTarget",
		"sweepAABB", "moveObjectWithSlide", "setHUDText", "clearHUD", "setHUDMenu", "clearHUDMenu",
		"createLoop", "spawnObject", "createVoxelObject", "spawnGroup", "destroyObject", "clearSpawnedObjects", "setObjectTransform",
		"getOverlaps", "getOverlapChanges", "getData", "setData", "getPlayerData", "setPlayerData",
		"createClientLoop", "setCamera", "followCamera", "releaseCamera", "setPlayerControl", "getLocalInput", "raycast",
		"isKeyDown", "bindAction", "isActionDown", "isLocalActionDown", "getInput", "sendToServer",
		"getState", "setState", "emitEvent", "playAvatarSpawnEffect", "randomInt"
	};
	const std::set<std::string> supported(supported_members, supported_members + sizeof(supported_members) / sizeof(supported_members[0]));
	Luau::Allocator allocator;
	Luau::AstNameTable names(allocator);
	Luau::ParseResult parsed = Luau::Parser::parse(script.data(), script.size(), names, allocator);
	if(!parsed.root || !parsed.errors.empty())
	{
		LuauGameApiDiagnostics empty;
		empty.warnings_json = "null";
		empty.uses_json = "null";
		empty.dynamic_accesses_json = "null";
		return empty; // The bytecode compiler reports syntax errors separately.
	}
	LuauGameApiMemberVisitor visitor;
	parsed.root->visit(&visitor);
	LuauGameApiDiagnostics result;
	result.warnings_json = "[";
	result.uses_json = "[";
	result.dynamic_accesses_json = "[";
	bool first_warning = true, first_use = true;
	size_t emitted = 0, emitted_uses = 0;
	std::set<std::pair<unsigned int, std::string>> warned_members;
	for(const auto& member : visitor.members)
	{
		if(emitted_uses < 64)
		{
			if(first_use) first_use = false; else result.uses_json += ",";
			const std::string& api_member = std::get<1>(member);
			const unsigned int guard_mask = std::get<2>(member);
			const char* const execution_side = gameApiMemberExecutionSide(api_member);
			const bool side_mismatch = (guard_mask == 1u && std::strcmp(execution_side, "client") == 0) ||
				(guard_mask == 2u && std::strcmp(execution_side, "server") == 0);
			result.uses_json += "{\"member\":\"" + web::Escaping::JSONEscape(api_member) + "\",\"line\":" + toString(std::get<0>(member) + 1) +
				",\"execution_side\":\"" + execution_side + "\",\"guard_context\":\"" + gameApiGuardContext(guard_mask) +
				"\",\"side_mismatch\":" + std::string(side_mismatch ? "true" : "false") + "}";
			++emitted_uses;
		}
		else result.uses_truncated = true;
		const std::pair<unsigned int, std::string> warning_key(std::get<0>(member), std::get<1>(member));
		if(supported.count(std::get<1>(member)) != 0 || !warned_members.insert(warning_key).second) continue;
		if(emitted >= 32)
		{
			result.warnings_truncated = true;
			continue;
		}
		if(first_warning) first_warning = false; else result.warnings_json += ",";
		result.warnings_json += "{\"member\":\"" + web::Escaping::JSONEscape(std::get<1>(member)) + "\",\"line\":" + toString(std::get<0>(member) + 1) + "}";
		++emitted;
	}
	bool first_dynamic_access = true;
	size_t emitted_dynamic_accesses = 0;
	for(const unsigned int line : visitor.dynamic_access_lines)
	{
		if(emitted_dynamic_accesses >= 64)
		{
			result.dynamic_accesses_truncated = true;
			continue;
		}
		if(!first_dynamic_access) result.dynamic_accesses_json += ",";
		first_dynamic_access = false;
		result.dynamic_accesses_json += toString(line + 1);
		++emitted_dynamic_accesses;
	}
	result.warnings_json += "]";
	result.uses_json += "]";
	result.dynamic_accesses_json += "]";
	return result;
}

static std::string callTool(Server& server, ServerAllWorldsState& world_state, ServerWorldState& world, const JSONParser& parser, const JSONNode& args, const std::string& tool_name, const UserID& user_id, const std::string& username)
{
	if(tool_name == "get_luau_reference")
	{
		const std::string topic = args.getChildStringValueWithDefaultVal(parser, "topic", "overview");
		if(topic == "game_example") return getConnectFourGameExample();
		if(topic == "game_quest_example") return getQuestGameExample();
		if(topic == "game_resource_example") return getResourceGameExample();
		if(topic == "game_strategy_example") return getStrategyGameExample();
		if(topic == "game_rpg_example") return getRPGProgressionExample();
		if(topic == "game_monopoly_example") return getMonopolyExample();
		if(topic == "game_card_example") return getCardGameExample();
		if(topic == "game_billiards_example") return getBilliardsGameReference();
		if(topic == "game_economy_example") return getEconomyGameExample();
		if(topic == "game_wave_example") return getWaveSurvivalExample();
		if(topic == "game_survival_example") return getSurvivalGameExample();
		if(topic == "game_tycoon_example") return getTycoonGameExample();
		if(topic == "game_systems") return R"json({"purpose":"Choose an implementation pattern for games the current Luau primitives support.","quest":"Server-validate every stage transition with onUserGameAction. Persist per-account state using game.getPlayerData/setPlayerData; require authenticated users and check returned save errors. Use game.setState for shared match/world state and game.setHUDMenu for choices.","resources":"Represent each resource node by a world object UID. Add server event listeners, keep per-player inventory and personal cooldowns in one versioned player-data record, and validate distance, ownership, node availability and capacity before awarding items. Shared global node depletion and a separate player inventory are two independent saves; do not claim those writes are atomic.","strategy":"For turn-based tactics, store map, turn, units and action points in a bounded game state. Validate participant, turn, movement range, occupancy and costs on the server before changing state. Use game.spawnGroup/templates for 3D units; game.moveObjectWithSlide is kinematic AABB movement, not pathfinding.","shooter":"Can make a target-range or arena prototype. Client camera rays are client-reported; do not treat game.getAimTarget alone as trusted competitive hit proof. Server-authoritative avatar movement and robust hit validation/lag compensation are not currently supplied.","football":"Can make turn-based or arcade ball-control prototypes with server-owned scripted objects and score state. Full multiplayer football needs authoritative avatar/ball physics, possession arbitration and collision/contact validation.","chess":"Well-suited: validate legal moves, turn, check/checkmate and draw on the server; share the board via game.setState and render buttons/HUD or 3D pieces.","experience_and_combat":"Store XP, level, health, cooldown and encounter state in one versioned game.getPlayerData record. Validate damage, respawn and rewards on the server; use game.randomInt for server-side rolls. See game_rpg_example.","cards_and_dice":"Use game.randomInt(min,max), a server-only cryptographically strong bounded integer API, for dice, deck shuffles and loot. Never use predictable client-side randomness for shared outcomes. See game_monopoly_example and game_card_example.","billiards":"Model balls as circles in a server-side state loop with friction, cushion and pocket rules. This gives an arcade prototype; exact 3D cue physics needs new engine physics support. See game_billiards_example.","crafting_and_trade":"Represent recipes, item counts, prices and trade offers in bounded player-data records. Validate materials, capacities, prices, distance and cooldowns. Cross-account trades need an atomic two-account transaction before use for valuable production economies. See game_economy_example for safe single-account loot, crafting and shop transactions.","teams_base_waves":"Keep team membership, base resources and wave index in shared game state; keep individual loadouts in player data. Spawn enemies only from configured templates and cap living spawned units. See game_wave_example for a shared base/wave rules starter.","engineering":"These are Luau game designs/examples, not installed game modes. The Linux server and updated client must be built and active for live testing."})json";
		if(topic == "game_input") return R"json({"authoritative_input":{"keyboard_mouse_gamepad_buttons":["lowercase ASCII letters a-z","digits 0-9","left","right","up","down","space","enter","escape","gamepad_a","gamepad_b","gamepad_x","gamepad_y","gamepad_l1","gamepad_r1","gamepad_l3","gamepad_r3","gamepad_start","gamepad_select","gamepad_up","gamepad_down","gamepad_left","gamepad_right","mouse_left","mouse_middle","mouse_right"],"held":"On the server, call game.isKeyDown(avatar, key) from a game loop. It returns false for a non-participant or unsupported key. onUserKeyPressed/onUserKeyReleased are edge callbacks; use held state for continuous movement.","analog":"game.getInput(avatar) returns nil unless avatar is a focused participant. Otherwise it returns wheel={x,y} as accumulated pending scroll steps that are consumed by the first game.getInput(avatar) read; cache this result once per avatar per tick if multiple handlers need it. pointer={x,y} or nil, with normalized viewport coordinates in [0,1] and origin at top-left; and gamepad={left_x,left_y,right_x,right_y,left_trigger,right_trigger} when a controller is attached. Stick axes are clamped to [-1,1], triggers to [0,1]. The updated Qt client sends analog state at up to 30 Hz under SCRIPTED_GAME_ANALOG_INPUT_SUPPORT; older clients return zero/default analog state. For clients that send capability-gated snapshots, after 500 ms without a fresh snapshot the server returns neutral pointer/gamepad/wheel input, rejects the stale aim ray, and treats held keys as released. Do not treat it as a physics tick or use it to move the actual avatar.","client_camera":"Client game.raycast(max_distance) uses the current pointer ray when the pointer is in the viewport, otherwise the screen centre, and returns the local hit object/UID, position, normal and distance. It is presentation-only. For server-side object intersection and server-selected UIDs use game.getAimTarget(avatar, max_distance, include_world_objects?), but its camera ray and avatar transform are client-reported. It is not an anti-cheat aim or line-of-sight proof; validate state, cooldown, range, ownership and game rules before applying outcomes.","example":"--lua\nif IS_SERVER then\n  game.createLoop(function(host)\n    for _, avatar in ipairs(game.getPlayers()) do\n      local input = game.getInput(avatar)\n      if input and (game.isKeyDown(avatar, 'w') or game.isKeyDown(avatar, 'gamepad_up')) then\n        -- Update only a game-owned object here; never trust the client to award a hit or score.\n      end\n      if input and input.gamepad then\n        local move_x = input.gamepad.left_x\n        local move_y = -input.gamepad.left_y\n      end\n    end\n  end, 20)\nend"})json";
		if(topic == "presentation_sync") return R"json({"purpose":"Trigger one-shot local sound or animation presentation on currently focused game participants with ordered events, separate from latest-snapshot game state.","server":"Call game.emitEvent(name) from the authoritative game script; names contain 1-32 ASCII identifier characters, delivery is limited to 20 events per game per second and 256 recipients per event, with world-wide caps of 1,200 events and 12,000 recipient deliveries per second. It returns true when queued or false when a per-game rate, world event-rate, recipient-count or delivery-budget limit is reached.","client":"Implement onGameEvent(object, name, sequence, server_time). When timestamp support is negotiated, server_time is the synchronized world clock at emission; older compatible connections pass 0. Only compute elapsed_seconds = math.min(3600, math.max(0, getCurrentTime() - server_time)) and pass it to startPlayingAnimation when server_time > 0; otherwise start the clip without an elapsed offset. Call the object's local playAudio() or startPlayingAnimation() using a loaded sound and exact clip name/index. The monotonically increasing sequence can suppress duplicates. Animation playback stays local, but its loop phase can be aligned from server_time and a clip that is already loaded.","release":"The server must be rebuilt to expose game.emitEvent() and the event packet; clients must advertise SCRIPTED_GAME_EVENT_CLIENT_SUPPORT. The synchronized server_time argument additionally requires SCRIPTED_GAME_EVENT_TIME_SUPPORT on the server and SCRIPTED_GAME_EVENT_TIME_CLIENT_SUPPORT on the client; otherwise callbacks receive 0. Older server/client pairs do not negotiate this callback.","limits":"Events use the negotiated TCP capability and are ordered for participants focused when emitted, while their connection remains active. The server caps delivery at 1,200 events and 12,000 total recipient deliveries per second per world. Events are not persisted or replayed to late joiners/reconnecting clients, and older clients without the capability do not receive them. Playback and asset availability remain local. Timestamped events can align local looping clip phase, but do not synchronize audio, clip availability or rendered frames."})json";
		if(topic == "game_player_control") return R"json({"purpose":"Drive the local player's avatar from a Luau game client loop, using the existing collision-aware client controller, and read all currently focused local controls. Requires the updated Qt client and server support for focused game sessions. This is local presentation/input only; it does not make avatar movement authoritative on the server.","focus_prerequisite":"Declare an onUserKeyPressed server callback on the object so a nearby player can press E and give it game focus; the client loop does not start before focus is active.","api":{"game.createClientLoop(callback, tick_rate_hz)":"Run a local loop while the game owns E-focus; rates are 1..30 Hz.","game.getLocalInput()":"Client-only. Returns {available,move_x,move_y,jump,run,w,a,s,d,keys,pointer,mouse,gamepad}. keys is a sparse table of currently held supported keyboard keys, such as keys.e, keys.space or keys.left. pointer is nil outside the viewport or {x,y} normalized from 0 to 1 with origin at top-left. mouse contains held left/middle/right buttons. gamepad contains connected, left_x/left_y/right_x/right_y, left_trigger/right_trigger and held a/b/x/y/l1/r1/l3/r3/start/select/up/down/left/right buttons. Axes are -1..1 and triggers 0..1. available is false when game focus or viewport keyboard focus is absent.","game.setPlayerControl(move_x,move_y,jump?,run?)":"Client-only. Accepts finite axes in -1..1 (positive y forward, positive x right), normalizes diagonal movement, and returns true, empty_error or false, error. Requires the game to own E-focus, standard avatar camera mode and no vehicle. Refresh every client-loop tick; the command expires after 0.35 seconds, on focus loss, or when the loop stops. It takes precedence over physical movement for each accepted frame."},"example":"--lua\nif IS_CLIENT then\n  game.createClientLoop(function(object, dt)\n    local input = game.getLocalInput()\n    if input and input.available then\n      if input.mouse.left or input.gamepad.a then\n        local hit = game.raycast(30) -- local presentation query only\n        if hit then print('Selected object ' .. hit.object_uid) end\n      end\n      local camera_x = input.gamepad.right_x\n    end\n  end, 20)\nend","gameplay_boundary":"Use game.sendToServer and server callbacks to communicate intent, then validate score, hits, inventory and shared state on the server. The local avatar movement and its collision response are not server-authoritative."})json";
		if(topic == "game_bindings") return R"json({"purpose":"Define named digital actions for one game script and read them in authoritative server logic or local client presentation.","focus":"Call game.bindAction(name, controls) in script setup. This registers the game for E focus and forwards allow-listed keyboard, mouse-button and gamepad-button transitions to the server even without onUserKeyPressed. Bindings in the script source apply to all players using this game object. Calling game.bindAction again replaces that evaluator's mapping. A client-side runtime rebind does not automatically update the server evaluator; for per-player or runtime settings, explicitly send and validate the selected control through game state or RPC, then apply it to the server mapping.","api":{"game.bindAction(name, controls)":"Available on client and server. name is 1..32 ASCII letters, digits, '_', '-' or '.'. controls is a dense array of 1..8 unique supported names: a..z, 0..9, left/right/up/down, space, enter, escape, mouse_left/mouse_middle/mouse_right, gamepad_a/b/x/y/l1/r1/l3/r3/start/select/up/down/left/right. Returns true. At most 32 named actions per script.","game.isActionDown(avatar, name)":"Server-only held state for an active participant in this focused game. Returns true while any server-bound control is held; false for an unknown action or inactive participant.","game.isLocalActionDown(name)":"Client-only. Returns true while any locally bound control is held and this game owns local E focus. Use only for local presentation; validate shared outcomes on the server."},"example":"--lua\ngame.bindAction('jump', {'space', 'gamepad_a'})\ngame.bindAction('interact', {'e', 'gamepad_x'})\nif IS_SERVER then\n  game.createLoop(function(host, dt)\n    for _, avatar in ipairs(game.getPlayers()) do\n      if game.isActionDown(avatar, 'jump') then\n        -- Apply jump rules using server-owned game state.\n      end\n    end\n  end, 20)\nend\nif IS_CLIENT then\n  game.createClientLoop(function(object, dt)\n    if game.isLocalActionDown('jump') then\n      -- Play local effects or update local presentation.\n    end\n  end, 20)\nend","security":"The server derives held actions from its own validated session key state. Never trust a client-reported action result or UID for shared mutations. Binding changes are script state, not a persistent player settings profile."})json";
		if(topic == "prefabs") return R"json({"purpose":"Build reusable per-game prefab catalogs on the existing persistent game-data and atomic group-spawn APIs.","storage":"Store only template UIDs and bounded transform data with game.setData(key, schema_version, value). game.getData(key) returns value and schema_version. Version is the saved data schema; keep it stable for compatible changes and increase it when the stored structure changes.","lookup":"Resolve each saved UID at spawn time with findObjectForUID(uid), fail cleanly if any template was deleted, then build {template=object,offset={x,y,z},axis={x,y,z},angle=radians,scale={x,y,z}} entries for game.spawnGroup(entries, origin). This makes saved catalogs reusable across script reloads and server restarts while the source templates remain in the same world.","example":"--lua\nlocal function savePrefab(name, entries)\n  local catalog = game.getData('prefabs')\n  if type(catalog) ~= 'table' then catalog = {} end\n  catalog[name] = entries\n  return game.setData('prefabs', 1, catalog)\nend\n\nlocal function spawnPrefab(name, origin)\n  local catalog = game.getData('prefabs')\n  local prefab = type(catalog) == 'table' and catalog[name]\n  if type(prefab) ~= 'table' then return nil, 'Prefab not found.' end\n  local group = {}\n  for index, part in ipairs(prefab) do\n    local template = findObjectForUID(part.uid)\n    if not template then return nil, 'Template object is missing.' end\n    group[index] = {template=template, offset=part.offset}\n    if part.axis and part.angle then\n      group[index].axis = part.axis\n      group[index].angle = part.angle\n    end\n    if part.scale then group[index].scale = part.scale end\n  end\n  return game.spawnGroup(group, origin)\nend\n\n-- Replace these example IDs with existing templates the game can copy.\nlocal saved, reason = savePrefab('camp', {\n  {uid=12345, offset={x=0,y=0,z=0}},\n  {uid=12346, offset={x=3,y=0,z=0}}\n})","limits":"game.setData records are limited to 16 KiB, the per-game namespace to 256 KiB/64 records, and writes to 10 per second. game.spawnGroup accepts 1-64 entries and validates the entire group atomically. This is a script-managed per-game catalog, not a global editor library or a pool of already-spawned objects."})json";
		if(topic == "hud_progress") return R"json({"signature":"game.setHUDMenu(avatar, title, actions, body?, layout?, fields?, progress_bars?, text_widgets?)","progress_bars":"The optional seventh argument is a dense array of up to four bars, e.g. {{id='health',label='Health',value=75,max=100}}. Each bar needs a unique 1-32 character identifier, a 1-64 byte label, and finite numbers satisfying 0 <= value <= max <= 1,000,000,000. A title plus bars can form a progress-only menu without actions.","text_widgets":"The optional eighth argument is a dense array of up to eight positioned labels, e.g. {{id='score',text='Score: 12',x=0,y=0.4,width=0.2}}. Each requires a unique 1-32 character id, 1-256 byte text, normalized screen-center x and top y in -1..1, and width 0.05..0.8 kept on screen. Optional text_color and background_color tables use RGB components from 0 to 1; background_alpha controls card opacity. Labels can be used with actions, fields or progress bars and are replaced together with the menu. The menu updates remain limited to 60 per game per second and 1,200 per world per second.","release":"Requires the updated Linux server and Qt client. Each widget type uses its own negotiated capability: SCRIPTED_GAME_HUD_PROGRESS_SUPPORT for progress bars and SCRIPTED_GAME_HUD_TEXT_WIDGETS_SUPPORT for positioned text and SCRIPTED_GAME_HUD_TEXT_STYLE_SUPPORT for custom widget colours. Older clients omit unsupported styles; a widget-only menu is hidden if none of its elements are supported."})json";
		if(topic == "game") return R"json({"status":"Experimental game API with server-authoritative snapshots and validated client-to-server messages; this is not yet a complete general-purpose platform.","release_boundary":"The server-side APIs require a Linux server built and activated from this source. Full client/server features also require the updated Qt client; protocol extensions are capability-negotiated.","named_actions":"game.bindAction(name, controls) defines up to 32 script-wide actions from 1..8 keyboard, mouse or gamepad controls. game.isActionDown(avatar, name) reads server-side held state; game.isLocalActionDown(name) reads local presentation state. Read topic game_bindings for supported controls and per-player rebinding limits.","presentation_events":"Server game.emitEvent(name) sends an ordered event to currently focused capable clients; implement onGameEvent(object, name, sequence, server_time) for local presentation. Events are ephemeral; use game.setState for the latest shared state. Read topic presentation_sync for delivery limits and synchronized audio/animation timing.","camera_control":"Client-only local camera lease: game.setCamera(position, look_at, horizontal_fov_degrees?, transition_seconds?) accepts two world-space XYZ vectors, optional horizontal FOV in 30..120 degrees, and optional transition duration in 0..10 seconds. Omitted FOV keeps the current field of view; zero or omitted duration applies immediately; a positive duration smoothly transitions camera position, heading, pitch and FOV. A new command replaces an active transition from the current pose. game.followCamera(target_object, camera_offset, look_at_offset?, horizontal_fov_degrees?, transition_seconds?) uses a live object in the current world as a moving target; offsets are world-space, look_at_offset defaults to {0,0,0}, and the camera updates each client frame after any initial transition. If the target is deleted or leaves the world, following stops at the last camera pose. game.releaseCamera() restores the previous camera controller state. Both camera methods are local presentation only and cannot authorize server actions.","session":"Declare onUserKeyPressed, onUserKeyReleased, onUserGameAction, onUserGameMessage, or onUserGameSessionChanged to make an object enter E-focused game mode; the player looks at the nearby object and presses E. onUserGameSessionChanged(avatar, object, active) reports membership. Escape exits.","voxel_creation":{"signature":"game.createVoxelObject(spec)","spec_luau_example":"game.createVoxelObject({cells={{x=0,y=0,z=0,material=1},{x=1,y=0,z=0,material=2}}, position={x=100,y=200,z=1}, scale={x=0.25,y=0.25,z=0.25}, collidable=false, materials={{r=0.1,g=0.5,b=0.9},{r=0.9,g=0.35,b=0.1}}})", "spec_example":{"cells":[{"x":0,"y":0,"z":0,"material":1},{"x":1,"y":0,"z":0,"material":2}],"position":{"x":100,"y":200,"z":1},"scale":{"x":0.25,"y":0.25,"z":0.25},"collidable":false,"materials":[{"r":0.1,"g":0.5,"b":0.9},{"r":0.9,"g":0.35,"b":0.1}]},"limits":"1..256 unique integer cells per object, up to 65,536 cells per game and 1,048,576 per world; each coordinate -64..64; optional one-based cell material index selects an entry in either a 1..16 RGB materials palette or the legacy shared color; use color or materials, not both; optional scale axes 0.001..10 metres; RGB channels 0..1; optional collidable boolean defaults to true for blocking level geometry","returns":"created object, or nil and an error string","budget_scope":"Also counts voxel templates copied by game.spawnObject and game.spawnGroup"},"spawned_object_lifecycle":"game.destroyObject(object) deletes only a live copy created by that game. game.clearSpawnedObjects() removes every remaining live transient copy owned by this game host and returns true, removed_count; it preserves saved game data and rate-limit windows, making it suitable for a controlled round reset. Both methods are server-only, refuse writes in read-only mode, and cannot delete ordinary world objects or another game host\u0027s copies. game.spawnObject, game.spawnGroup and game.createVoxelObject also refuse creation while the server is in read-only mode; property and material writes on game-spawned copies are rejected too. game.setState and persistent game/player saves also return false with a reason.","server_api":["game.randomInt(min,max)","game.createVoxelObject(spec)","game.getPlayers()","game.getPlayersNear(object, radius)","game.getPlayersOverlapping(object)","game.getAimTarget(avatar, max_distance, include_world_objects?)","game.getAimPlayerTarget(avatar, max_distance)","game.isKeyDown(avatar, key)","game.getInput(avatar)","game.createLoop(callback, tick_rate_hz)","game.setState(table)","game.getState()","game.setHUDText(text, avatar, layout?)","game.clearHUD(avatar)","game.setHUDMenu(avatar, title, actions, body?, layout?, fields?, progress_bars?, text_widgets?)","game.clearHUDMenu(avatar, panel_id?)","game.spawnObject(template, Vec3d)","game.spawnGroup(entries, origin)","game.destroyObject(object)","game.clearSpawnedObjects()","game.setObjectTransform(object, transform)","game.sweepAABB(object, displacement)","game.moveObjectWithSlide(object, displacement)","game.getOverlaps(object)","game.getOverlapChanges(object)","game.getData(key)","game.setData(key, version, value)","game.getPlayerData(avatar, key)","game.setPlayerData(avatar, key, version, value)"],"client_api":["game.getState() (latest server snapshot)","game.sendToServer(name, table)","onGameStateChanged(object, version, state)","game.getLocalInput() (focused local keyboard, pointer, mouse and gamepad snapshot)","game.setPlayerControl(move_x, move_y, jump?, run?) (local client avatar control; requires updated Qt client and focused-session server support; not server-authoritative)","game.createClientLoop(callback, tick_rate_hz)","game.setCamera(position, look_at, horizontal_fov_degrees?, transition_seconds?)","game.followCamera(target_object, camera_offset, look_at_offset?, horizontal_fov_degrees?, transition_seconds?)","game.releaseCamera()","game.raycast(max_distance) returns {object, object_uid, position, normal, distance}; local physics ray uses the pointer when inside the viewport, otherwise screen centre; the game host is skipped, and the query is not server authorization","object.playAudio() (restart local object audio)","object.pauseAudio() (pause local object audio)","object.resumeAudio() (resume local object audio)","object.isPlayingAudio()","object.getAnimationIndex(name)","object.startPlayingAnimation(index_or_name, elapsed_seconds?): boolean (local clip transition; optional initial phase for timestamped cues)","object.stopPlayingAnimation(): boolean (pause current pose)","object.resumePlayingAnimation(): boolean (resume current clip)"],"object_transform":"game.setObjectTransform(object, {position={x,y,z}, axis={x,y,z}, angle=radians, scale={x,y,z}}) is server-only and accepts only a live object spawned by this game. position is required; axis and angle must be supplied together; scale is optional. The server validates finite bounds and scale, rechecks the game owner\u0027s parcel permissions at the new transform, and broadcasts the transform. Updates are limited to 600 per second per game and 12,000 per second per world. This is authoritative kinematic placement, not a physics simulation or contact event.","aim_query":"game.getAimTarget(avatar, max_distance, include_world_objects?) is server-only. It returns the nearest live object spawned by this game along the participant\u0027s latest camera ray; pass true as the third argument to also return the nearest collidable, non-sensor world object. Omit the flag or pass false to return only this game\u0027s spawned objects. Collidable world-object AABBs block the ray in either mode. The result includes object, server-selected object_uid, position, normal, and distance, or nil; max_distance is finite from 0 to 100 metres. Target selection does not grant write permission: object property changes still require the script owner to own that object, and transforms also recheck parcel placement permission. Updated clients provide the current camera ray in the negotiated input snapshot; older clients fall back to avatar view angles. The origin is checked against the latest avatar position, but direction is client-reported, so this is not an anti-cheat camera check. Limited to 10 calls per second per game and 200 per second per world; also consumes the shared spatial-query budget (60 per game, 600 per world). A 64-metre server spatial grid checks at most 8,192 ray candidates; denser rays return nil and an error.","player_overlap":"game.getPlayersOverlapping(object) is server-only and requires the updated Linux server release. object must be the live game host or a live object spawned by this game. Returns active participants whose conservative avatar AABB (0.6 m wide, 1.9 m tall from server-stored avatar position) intersects object.getAABBWS(); maximum 256 participants. Shares game.getPlayersNear and object-overlap spatial-query budgets (60 queries/sec/game, 600/sec/world). Avatar transforms are client-reported; this is a trigger-zone AABB query, not an exact physics contact or anti-cheat check.","player_proximity":"game.getPlayersNear(object, radius) is server-only. object must be the live game host or a live object spawned by this game; radius is finite from 0 to 1000 metres. Returns active session participants within Euclidean distance of the object bounds centre; max 256 participants. Shares the 60-per-second-per-game and 600-per-second-per-world spatial-query budgets with game.getOverlaps and game.getOverlapChanges. Avatar positions are client-reported snapshots, so this is proximity logic, not collision validation.","hud_text_layout":"game.setHUDText(text, avatar, {x=0,y=0.9}) places the text HUD at a normalized horizontal center x and top edge y, both within -1..1. Omit layout for the centered upper-screen default. Requires clients and servers advertising SCRIPTED_GAME_HUD_TEXT_LAYOUT_SUPPORT.","hud_layout":"game.setHUDMenu(avatar, title, actions, body?, {id='inventory',x=-0.7,y=0.8,width=0.3}, fields?) creates or replaces one named menu. Each action may add layout={x=0,y=0.2,width=0.3,height=0.08}; x is normalized horizontal center, y is normalized top edge in -1..1, width is 0.05..1 of screen width and height is 0.025..0.5 of vertical UI extent; bounds must keep the button on screen. Actions without layout use the automatic grid. This needs clients and servers advertising SCRIPTED_GAME_HUD_ACTION_LAYOUT_SUPPORT. fields is an optional array of up to four text inputs, bounded numeric inputs {id,label,type='number',min=0,max=100}, selectors {id,label,type='select',options={'Easy','Hard'}}, or boolean checkboxes {id,label,type='checkbox',checked=false}. Checkbox values are submitted as 'true' or 'false'; required checkboxes must be checked. Numeric submissions are finite values checked against min/max by the server. Select fields require 2-8 unique 1-64 byte options; onUserGameAction receives the submitted values table as its fourth argument, and the server validates field IDs, required values, numeric ranges, checkbox values, and selected options. Up to four named menus per player/game, eight buttons each; action IDs must be unique across open menus. Omit id for the default main panel. clearHUDMenu(avatar, id) clears one; clearHUDMenu(avatar) clears main. x is normalized horizontal center and y is normalized top position in -1..1 screen coordinates; width is 0.2..1.0. The old centered layout is the default. Numeric and checkbox fields require the new server release; older capable clients receive them as text inputs. Older server releases do not recognize type=number or type=checkbox.","spawn_group":"game.spawnGroup entries may set offset={x,y,z}, axis={x,y,z} with angle=radians (axis and angle together; angle within +/-2*pi), and absolute scale={x,y,z} (each 0.01..100). Omitted transforms preserve the template. The full group is validated and placed atomically, with at most 64 entries. Each game may own 1,024 live spawned objects, all games in one world may own 32,768 total, and creation is limited to 128 per second per game and 1,024 per second per world.","rpc":"A focused client script can call game.sendToServer(name, plain_table). It returns true when queued, false when unsupported, unfocused, over the client 20 messages/s limit, or not serializable. The server independently enforces 20 messages/s per participant/game and 1,200 messages/s per world. Names are 1-32 ASCII identifier characters, payloads are limited to 4 KiB, and server handlers receive onUserGameMessage(avatar, object, name, payload). The server rechecks active session, distance and payload shape; client messages never authorize mutations by themselves.","input":"game.getInput(avatar) returns gamepad={left_x,left_y,right_x,right_y,left_trigger,right_trigger} when connected, pointer={x,y} when the cursor is inside the viewport, and wheel={x,y} scroll steps. Pointer values are normalized from 0 to 1. Wheel deltas accumulate on the server (capped at 100 steps per axis) and are consumed once per game.getInput call. Focused Qt sends input snapshots at up to 30 Hz; the server allows up to 60 updates per second per participant and 3,600 per second per world, dropping excess updates. Rejected update counts are exposed by get_object_script_log; wheel support is capability-negotiated between the updated client and server. onUserKeyPressed/onUserKeyReleased support keyboard keys, mouse_left/mouse_middle/mouse_right, gamepad_a/b/x/y, gamepad_l1/r1/l3/r3, gamepad_start/select and gamepad_up/down/left/right; game.isKeyDown reads held digital state on the server.","state_sync":"game.setState accepts a serializable Luau table up to 16 KiB per game and 8 MiB total per world, returning true, new_version or false, reason; an update that exceeds the world budget is rejected without replacing the prior snapshot. The server retains the latest transient snapshot, sends it to active participants, and delivers the current snapshot to a player joining late. Client scripts receive the reconstructed full state in onGameStateChanged(object, version, state) and game.getState(), even when a capable client receives a smaller versioned wire delta. Late joiners, periodic checkpoints, and older clients receive full snapshots. The delta wire format is negotiated by client/server capability bits; gameplay semantics do not change. Updates are limited to 10 per second per game and 600 per second per world; use game.setData for durable saves.","collision_control":"Voxel objects created by game.createVoxelObject collide by default; use collidable=false for decorative pieces. A script may also set collidable=false or sensor=true on its live game-owned copy (object.collidable = false, object.sensor = true); these property changes replicate to clients but are not persisted as world assets, with a limit of 600 property updates per game and 12,000 per world per second. game.getOverlaps includes non-collidable and sensor game copies for trigger logic; sweeps ignore non-collidable/sensor obstacles. Keep game objects static/kinematic: dynamic=true does not add authoritative force simulation.","collision_query":"game.moveObjectWithSlide(object, displacement) performs up to three server-side conservative AABB sweep-and-slide legs for a live object spawned by this game and atomically applies the accepted transform. It returns true, result with position, moved, blocked, and contacts (object, uid, normal, leg fraction), or false, reason. For custom response without moving, game.sweepAABB(object, displacement) returns the nearest blocking object, uid, fraction, distance, contact-origin position and axis normal, or nil when clear. Both check other collidable objects spawned by the game and collidable world-object AABBs; sensor and non-collidable objects are ignored. Displacement is at most 100 metres. They allow up to 30 collision scans per second per game and share a 200-world-scan-per-second cap with aim queries, plus the spatial-query budget of 60 per second per game and 600 per second per world. A 64-metre server spatial grid narrows both queries to at most 8,192 candidates; a denser swept region returns false/nil with an error. This prevents AABB tunnelling for scripted movement but is not a mesh contact manifold, force simulation or avatar collision. Use game.moveObjectWithSlide for ordinary kinematic movement with wall sliding; use game.sweepAABB plus game.setObjectTransform only for custom collision response.","rules":"Keep shared rules and writes on the authoritative server. Client loops, local player control, camera lease and raycast are local presentation/query APIs and do not authorize shared game actions. Server and client loops are limited to 1-30 Hz and four catch-up steps. The server polls scripts about every 100 ms and dispatches at most 64 due timer events per poll; additional due events remain queued for later ticks. A server fixed-step loop retains up to 5 seconds of pending simulation time, processes at most four steps per dispatch, then drains the backlog on later ticks; delay beyond five seconds is capped. The client loop requires active local E focus. Spawned objects are transient authorized copies; one group call accepts at most 64 entries, each game may own 1,024 live copies, all games in one world may own 32,768, and creation is limited to 128 copies per second per game and 1,024 per second per world. game.setObjectTransform changes the authoritative position/rotation/scale of one live object spawned by that game, checks parcel permissions again, broadcasts the new transform, and is limited to 600 updates per second per game and 12,000 per second per world; it does not simulate forces or collision response. game.getOverlaps(object) scans only live spawned objects owned by the same game and returns at most 256 objects with intersecting world-space AABBs, and returns an error when more than 256 intersect. Object-overlap and player-proximity queries share a 60-query-per-second budget per game and 600 per second across the world. game.getOverlapChanges(object) returns entered and exited overlap arrays since the last query for that object, with snapshots limited to 16,384 pairs per game. These are broad-phase AABB edges, not exact physics events. Persistent game/player saves are versioned and quota-limited. Each VM is capped at 128 MiB. The server also caps aggregate Lua allocation across all owner VMs and worlds at 1 GiB. Remaining server memory is shared across active VMs without lowering a VM below its current allocation; when exhausted, creation of another VM is refused. Each client shares one 128 MiB VM across its loaded object scripts. The new server advertises the optional delta transport; older server/client pairs continue using full snapshots. See /about_luau_scripting for signatures and examples.","not_yet_supported":["arbitrary custom HUD widgets","prefab pooling and reusable authored prefab libraries","exact physics contact manifolds, force/joint simulation and avatar collisions; game.sweepAABB and game.getOverlaps are conservative AABB queries","authoritative audio playback, synchronized clip availability/blending or guaranteed identical render frames; timestamped game events can align local looping clip phase","a dedicated diagnostics harness; each VM is capped at 128 MiB, the server aggregate is capped at 1 GiB, and spawned objects, voxel cells, snapshots, HUD, transforms and spatial queries have additional limits"]})json";
		if(topic == "overview") return "{\"language\":\"Luau event scripts for world objects; source must start with --lua\",\"workflow\":[\"Read get_object_script for the exact UID and preserve useful existing logic.\",\"Read events and the API topics needed for the requested behaviour.\",\"Decide whether each effect belongs on server, client, or both; avoid duplicate execution.\",\"For Luau, call validate_object_script on the complete draft to catch syntax and bytecode-load errors without running or saving it. Review game_api_warnings for misspelled direct or literal-string game API accesses, game_api_uses for documented client/server execution sides, and game_api_dynamic_accesses for computed game[key] references that need manual review; confirm the actual IS_SERVER/IS_CLIENT guard because the tool does not analyze control flow.\",\"Write the validated complete script with update_object_script for that same UID.\",\"Read it back with get_object_script, query get_object_script_log for server diagnostics when available, and report server_execution_enabled/server_evaluator_reloaded.\",\"For visual/client-only behavior, ask the user to test in the Qt client and inspect Script Editor build/runtime results.\"],\"scope\":\"Validation compiles Luau syntax/bytecode without executing or saving the draft. It also reports advisory warnings for explicit game.member names outside the documented built-in table, inventories up to 64 explicit member uses with documented execution sides, and reports up to 64 source lines containing computed game[key] accesses. The inventory does not analyze IS_SERVER/IS_CLIENT control flow. Aliases, custom extensions and runtime behavior are not fully type-checked; literal keys such as game['spawnObject'] are inventoried. MCP saves the object's script and notifies connected clients. Luau execution is controlled by the world's server-script feature flag. Saving is not proof that a client-only visual effect works.\",\"topics\":[\"overview\",\"coding\",\"events\",\"interaction\",\"game\",\"game_input\",\"game_bindings\",\"game_example\",\"game_quest_example\",\"game_resource_example\",\"game_strategy_example\",\"game_rpg_example\",\"game_monopoly_example\",\"game_card_example\",\"game_billiards_example\",\"game_economy_example\",\"game_wave_example\",\"game_survival_example\",\"game_tycoon_example\",\"game_systems\",\"prefabs\",\"hud_progress\",\"presentation_sync\",\"game_player_control\",\"objects\",\"materials\",\"avatars\",\"physics\",\"timers\",\"storage\",\"http\",\"audio\",\"animation\",\"door\",\"particles\",\"limits\",\"examples\",\"xml\",\"winter\"]}";
		if(topic == "coding") return "{\"format\":\"First line: --lua. Vec3d(x,y,z) constructs a position table; Vec3f(x,y,z) constructs a Luau vector for axis/scale/material colour/velocity fields.\",\"execution\":\"The server/client reload executes the script body, so keep top-level code short and put repeated work in event callbacks/timers.\",\"edit_protocol\":\"Read the exact object's script first; preserve unrelated handlers/state; deliver a complete replacement; update that same UID; read it back. Do not claim compilation or runtime success from an MCP save response alone.\",\"diagnostics\":\"get_object_script_log returns recent server-side print/runtime errors, evaluator and timer state, active game players/transient objects, and synchronized state version/size for one editable UID; Script Editor/client logs cover local compilation and visual playback. During MCP update, a compile/top-level error happens before the saved script/evaluator are replaced, so the previous version remains. A runtime error after a successful save disables that evaluator; correct the error and reload. Top-level external side effects are not transactional.\"}";
		if(topic == "interaction") return R"json({"signature":"game.setInteractionPrompt(uid, text, input, visible?)","input":"Use e, mouse_left, both, a supported key such as f, or key+mouse_left such as f+mouse_left. Supported key names: a-z, 0-9, f1-f12, space, enter, escape, backspace, delete, pageup, pagedown, home, end, minus, equals, plus, left_bracket, right_bracket, left, right, up, down. Visibility controls the label only.","action":"The assigned keyboard key and/or left click invokes the object onUserUsedObject(avatar, object) handler. Put any supported server-authoritative action in that handler and update the prompt when state changes.","checks":"The server checks for a use handler and 8 m range. Clicks additionally validate target, occlusion and cooldown. Custom keyboard interactions are rate-limited."})json";
		if(topic == "door") return R"json({"purpose":"Reusable Luau door controller configuration for hinge and sliding doors. Set exact UIDs, hinge pivot/axis or slide direction/distance for each model; arbitrary geometry cannot reveal an intended hinge automatically.","interaction":"game.setInteractionPrompt(uid,text,input,visible?) is server-only. input accepts e, mouse_left, both, any supported letter/digit or space/enter/escape/arrow key; append +mouse_left for keyboard plus click. The same onUserUsedObject handler runs for each configured trigger. Re-call on every state transition to change Open/Close text. Mouse clicks are server-checked for <=8 m range, target bounds, occlusion and per-client/object cooldown; E use is range-checked too.","configuration":{"button_uid":"interactive button UID","part_uids":"all moving panel/leaf UIDs","mode":"hinge|slide","pivot":"world Vec3d hinge point","axis":"normalized world Vec3f axis","direction":"hinge sign (+1/-1) or slide Vec3f","open_angle":"radians","distance":"metres","speed":"seconds","auto_close":"seconds or 0","open_text":"label while closed","close_text":"label while open","input":"e|mouse_left|both","button_animation":"optional exact client animation clip","sound":"optional button audio source"},"workflow":"On server, preserve each part base transform, compute desired hinge positions with Rodrigues rotation around pivot or translate along direction, interpolate with bounded createTimer, and update every configured part. Update prompt on each state change and use a generation token to cancel stale timers. Client-side onUserUsedObject plays the configured button clip/audio when loaded. For oriented meshes, compose authored orientation with hinge rotation; verify asset pivot and axis in-world."})json";if(topic == "events") return "{\"self_handlers\":[\"onUserTouchedObject(avatar, object)\",\"onUserUsedObject(avatar, object)\",\"onUserMovedNearToObject(avatar, object)\",\"onUserMovedAwayFromObject(avatar, object)\",\"onUserEnteredParcel(avatar, object, parcel)\",\"onUserExitedParcel(avatar, object, parcel)\",\"onUserEnteredVehicle(avatar, vehicle_object)\",\"onUserExitedVehicle(avatar, vehicle_object)\",\"onChatMessage(avatar, message)\",\"onUserKeyPressed(avatar, object, key)\",\"onUserKeyReleased(avatar, object, key)\",\"onUserGameAction(avatar, object, action_id, values)\",\"onUserGameMessage(avatar, object, name, payload)\",\"onUserGameSessionChanged(avatar, object, active)\",\"onGameStateChanged(object, version, state)\"],\"other_object\":\"addEventListener(event_name, object_uid, handler_function) subscribes to an existing event; removeEventListener(event_name, object_uid, same_handler_function) unsubscribes and returns true if removed.\",\"touch\":\"Client physics reports contacts; server callbacks require a live sender within 2.5 metres of the target world AABB. The server does not simulate the collision, so this is not secure hit detection.\",\"behavior\":\"A function with a matching name automatically handles events for this script's object. E/use action triggers onUserUsedObject. Touch can repeat while contact continues. Near/far transitions are checked against the server's latest stored avatar position and deduplicated per avatar/object at 20 m; avatar movement itself is still client-reported. Parcel/vehicle events are discrete transitions.\",\"limits\":\"At most 100 active other-object event registrations per script.\"}";
		if(topic == "objects") return "{\"globals\":[\"this_object\",\"IS_CLIENT\",\"IS_SERVER\",\"Vec3d(x,y,z)\",\"Vec3f(x,y,z)\",\"getObjectForUID(uid)\",\"findObjectForUID(uid)\",\"getCurrentTime()\",\"showMessageToUser(message, avatar)\",\"addEventListener(event, uid, callback)\",\"removeEventListener(event, uid, same_callback)\"],\"readable_object_fields\":[\"uid\",\"model_url\",\"pos\",\"axis\",\"angle\",\"scale\",\"collidable\",\"dynamic\",\"sensor\",\"content\",\"target_url\",\"video_autoplay\",\"video_loop\",\"video_muted\",\"mass\",\"friction\",\"restitution\",\"centre_of_mass_offset_os\",\"audio_source_url\",\"audio_volume\",\"audio_loop\"],\"methods\":[\"getNumMaterials()\",\"getMaterial(index)\",\"playAudio()\",\"pauseAudio()\",\"resumeAudio()\",\"isPlayingAudio()\",\"getAnimationIndex(name)\",\"startPlayingAnimation(index_or_name, elapsed_seconds?): boolean\",\"stopPlayingAnimation(): boolean\",\"resumePlayingAnimation(): boolean\"],\"write_example\":\"this_object.pos = Vec3d(0, 0, 2)\",\"notes\":\"pos is Vec3d; axis/scale and other vector fields are Luau vectors made with Vec3f. Object transforms/properties are server-backed and marked dirty by setters. Audio and animation methods are GUI-client presentation controls on loaded resources; animation names are exact, and stop/resume preserve the local pose. getObjectForUID throws when missing; findObjectForUID returns nil. showMessageToUser only displays in the local GUI client.\"}";
		if(topic == "materials") return "{\"access\":\"Object:getNumMaterials() and Object:getMaterial(index), where index is zero-based from 0 to getNumMaterials()-1\",\"material_properties\":[\"colour\",\"colour_texture_url\",\"emission_rgb\",\"emission_texture_url\",\"normal_map_url\",\"roughness_val\",\"roughness_texture_url\",\"metallic_fraction_val\",\"opacity_val\",\"tex_matrix\",\"emission_lum_flux_or_lum\",\"hologram\",\"double_sided\"],\"write_behavior\":\"Material assignments are authoritative server-side only. A client-side assignment is ignored. The script must own the object (same creator as its host); game scripts can update the material of their own spawned objects. Changes are replicated to clients. Updates to ordinary owned objects are persisted; game-spawned copies remain transient. Material changes to game-spawned objects are limited to 600 per game per second and 12,000 per world per second.\",\"example\":\"if IS_SERVER then\\n  local material = this_object:getMaterial(0)\\n  material.colour = Vec3f(0.1, 0.6, 1.0)\\nend\"}";
		if(topic == "avatars") return "{\"fields\":[\"pos\",\"name\",\"linear_velocity\",\"vehicle_inside\"],\"events\":\"Handlers receive an Avatar table. Pass it to showMessageToUser(message, avatar).\",\"write_limits\":\"Avatar name is read-only. Setting pos/linear_velocity only controls the local player's avatar in GUI client scripts; server-side assignment does not move a remote user. vehicle_inside is nil or a WorldObject.\"}";
		if(topic == "physics") return "{\"object_fields\":[\"collidable\",\"dynamic\",\"sensor\",\"mass\",\"friction\",\"restitution\",\"centre_of_mass_offset_os\"],\"lua_control\":\"Luau can set these existing WorldObject properties and the server replicates them. There is no binding for arbitrary forces, joints, collision-shape authoring, or direct access to Jolt.\",\"game_collision\":\"Server game scripts can call game.sweepAABB(object, displacement) to query the nearest conservative swept-AABB collision against collidable world objects and collidable same-game spawned objects; sensors are ignored. It returns the object, fraction, distance, contact-origin position and axis normal; displacement is limited to 100 metres. Calls allow up to 30 per second per game, share a 200-per-second world-scan cap across collision and aim queries, and also use the spatial-query budget of 60 per second per game and 600 per second per world. Combine the result with game.setObjectTransform for scripted kinematic movement. It does not apply forces or simulate avatar/mesh contacts.\",\"game_kinematic_movement\":\"For a live object spawned by the same game, use game.moveObjectWithSlide(object, displacement) for bounded server-side AABB sweep-and-slide movement with up to three collision legs. It applies the accepted transform and returns the resulting position and contact normals. Use game.sweepAABB(object, displacement) only when custom collision response is needed, then apply a validated game.setObjectTransform. These are conservative AABB queries, not mesh contacts or force simulation; displacement is limited to 100 metres and calls are rate-limited.\",\"moving_platform\":\"For a lift, use a broad collidable non-dynamic object with a Luau script. Move its pos from a server timer at a modest interval. Scripted objects use kinematic collision bodies; clients interpolate replicated transforms and move that collider through the physics engine so grounded avatars inherit platform velocity. Avoid teleport-sized steps.\",\"example\":\"--lua\\nif IS_SERVER then\\n  this_object.collidable = true\\n  this_object.dynamic = false\\n  local start = this_object.pos\\n  local height = 4\\n  local period = 8\\n  createTimer(function(ob)\\n    local phase = (getCurrentTime() % period) / period\\n    local triangle = 1 - math.abs(2 * phase - 1)\\n    ob.pos = Vec3d(start.x, start.y, start.z + height * triangle)\\n  end, 0.05, true)\\nend\",\"caveats\":\"Enable server-side Lua. Very fast motion, small platforms, network stalls or long client frame pauses can still make standing unreliable. This API does not provide elevator constraints or control of avatar ground snapping.\"}";
		if(topic == "timers") return "{\"functions\":[\"createTimer(callback, interval_seconds, repeating): handle\",\"destroyTimer(handle)\"],\"callback\":\"callback(this_object)\",\"limits\":\"Interval must be finite and at most 86400 seconds; values below 0.1 seconds are clamped to 0.1. Maximum 4 ordinary timers plus one separately scheduled game loop per script evaluator; destroyTimer(handle) stops either task type. Client timers run locally without game-focus and are capped at 64 callbacks and 256 due-event removals per rendered frame across the client; excess due timers stay queued or are deferred. Use client timers for presentation such as NPC animation selection; keep shared rules and world mutations on the server. Use game.createLoop for authoritative game updates and game.createClientLoop for focused-game presentation. Cancel repeating timers when no longer needed.\"}";
		if(topic == "storage") return "{\"functions\":[\"objectstorage.getItem(key)\",\"objectstorage.setItem(key, value)\"],\"scope\":\"Persistent server storage scoped to the script object's UID; it survives server restart and script reload.\",\"constraints\":\"Key <= 256 bytes. Values must be serializable Lua data (tables/scalars), max nesting depth 16 and serialized size 64 KiB. Client-only storage reads nil and writes nothing.\"}";
		if(topic == "http") return "{\"functions\":[\"doHTTPGetRequestAsync(url, headers, onDone, onError)\",\"doHTTPPostRequestAsync(url, body, content_type, headers, onDone, onError)\",\"parseJSON(json)\",\"getSecret(name)\"],\"scope\":\"HTTP requests are server-side only and require the server HTTP feature to be enabled. Client calls do nothing.\",\"limits\":\"Requests are rate-limited to 5 per user per 300 seconds. getSecret reads account secrets; never hardcode API keys in object scripts.\",\"callbacks\":\"onDone(response) receives one table with response_code, response_message, mime_type and body_data (buffer). onError(error) receives one table with error_code and error_description.\"}";
		if(topic == "model_nodes") return "{\"side\":\"client presentation API; works on any loaded GLB/BMesh whose node hierarchy is preserved\",\"methods\":[\"object:getNodeCount()\",\"object:getNodeIndex(name)\",\"object:getNodeInfo(index_or_name)\",\"object:getBoneCount()\",\"object:getBoneInfo(bone_index)\",\"object:setNodeTransform(index_or_name, {translation=Vec3f(...), rotation={x=...,y=...,z=...,w=...}, scale=Vec3f(...)})\",\"object:clearNodeTransform(index_or_name)\",\"object:getAnimationCount()\",\"object:getAnimationName(index)\",\"object:getAnimationIndex(name)\",\"object:startPlayingAnimation(index_or_name, elapsed_seconds?)\"],\"indices\":\"Node, bone and animation indices are zero-based. getNodeInfo returns name, parent_index, is_bone, bind-pose translation/rotation/scale. getBoneInfo maps the zero-based joint slot to that node record.\",\"transform\":\"setNodeTransform applies a bounded local post-transform after the authored pose/animation. Translation defaults to zero, rotation to identity quaternion, scale to one; clearNodeTransform removes the override.\",\"limits\":\"Model data must be loaded on the client. The renderer supports at most 256 rigid mesh joints per object; GLB files over that limit keep their original static import path and setNodeTransform returns false. Node transforms are local presentation and are not independently network-replicated. For multiplayer, send bounded authoritative game state/events and apply the same node pose on each capable client. Collision shapes do not follow procedural node overrides. New Qt clients are required.\",\"example\":\"if IS_CLIENT then local i=this_object:getNodeIndex('Boom'); if i>=0 then this_object:setNodeTransform(i,{rotation={x=0,y=0,z=0.25,w=0.9682458}}) end end\"}";
		if(topic == "avatar_effect") return "{\"function\":\"game.playAvatarSpawnEffect(avatar)\",\"side\":\"server only\",\"behavior\":\"Pass a live avatar table from an event or game.getPlayers(). The server verifies that avatar is live in this world, then broadcasts the standard blue materialisation effect to capable clients. Returns true when queued. Clients start the existing two-second effect, including when the avatar model is still loading. It does not move or respawn the avatar.\",\"release\":\"Requires a rebuilt Linux server and Qt clients advertising avatar materialisation support.\",\"example\":\"if IS_SERVER then game.playAvatarSpawnEffect(avatar) end\"}";
		if(topic == "audio") return "{\"fields\":[\"audio_source_url\",\"audio_volume\",\"audio_loop\"],\"methods\":[\"object:playAudio()\",\"object:isPlayingAudio()\"],\"scope\":\"Playback methods are GUI-client features; changing source/volume/loop updates object properties. A server script can set properties but cannot itself render sound.\"}";
		if(topic == "animation") return "{\"methods\":[\"object:getAnimationIndex(name): number\",\"object:startPlayingAnimation(index_or_name, elapsed_seconds?): boolean\",\"object:stopPlayingAnimation(): boolean\",\"object:resumePlayingAnimation(): boolean\"],\"workflow\":\"Run on the client that renders the model. Pass the exact animation name directly, or use getAnimationIndex(name) and pass its integer result; -1 means not found. startPlayingAnimation returns true only when the named or indexed clip exists and the animated model is loaded, otherwise false. Its optional elapsed_seconds argument sets initial loop phase in 0..3600 seconds; combine max(0, getCurrentTime()-server_time) from onGameEvent to align a clip across clients. stopPlayingAnimation freezes the current pose, including when a model has only one clip; resumePlayingAnimation continues that clip from the frozen pose.\",\"npc_state_selection\":\"For automatic walk/idle selection, guard a createTimer(interval >= 0.1) with if IS_CLIENT, compare this_object.pos with its prior position, and switch to available clips when possible. If the model has no idle clip, call this_object:stopPlayingAnimation() while stationary and this_object:resumePlayingAnimation() when movement resumes. Store the last requested clip locally because there is no API to query the currently playing clip. Client timers run without E-game focus and share a per-frame budget of 64 callbacks and 256 due-event removals; excess timers remain queued or are deferred.\",\"external_glb\":\"Polyfork GLB clips are local client presentation and can be played by exact name once that external model has loaded. getAnimationIndex returns -1 before its mesh/clip data is available. Example: if IS_CLIENT then local attempts=0; local timer; timer=createTimer(function(ob) attempts=attempts+1; local index=ob:getAnimationIndex('Idle'); if attempts>=40 or (index>=0 and ob:startPlayingAnimation('Idle')) then destroyTimer(timer) end end,0.25,true) end. Keep retries bounded, replace Idle with a name from the asset clips array, and explain if playback still returns false; server get_rig only inspects native BMesh models.\",\",\"limits\":\"Animation playback and pause are client-side and only work for a loaded animated model; they are not a skeleton/rig authoring API and do not create networked animation clips.\"}";
		if(topic == "particles") return "{\"global_table\":\"emitter\",\"methods\":[\"emitter.start()\",\"emitter.stop()\",\"emitter.burst(count)\",\"emitter.clear()\"],\"aliases\":[\"emitter.clearParticles()\"],\"scope\":\"These operate on this_object only when its content configures a particle emitter; they are GUI-client runtime controls and return true/false. burst count is clamped to 1..512.\"}";
		if(topic == "limits") return "{\"script_source\":\"Maximum 10,000 bytes per object script.\",\"execution\":\"Object Luau scripts share one VM per server script owner and one VM across loaded object scripts on each client. Each VM has a 128 MiB cap, the server has a 1 GiB aggregate cap across owner VMs and worlds, and each script execution is interrupted after 10,000 VM interrupts.\",\"timers\":\"4 per script\",\"listeners\":\"100 other-object event registrations per script\",\"http\":\"Server feature flag required; rate limited\",\"safe_design\":\"Bound loops and data; do expensive setup once; throttle repeated touch/proximity work; avoid per-frame timers and recursive event cascades. Game objects, persistent state, snapshots, HUD, transforms and spatial queries have separate documented quotas.\"}";
		if(topic == "examples") return "{\"simple_interaction\":\"--lua\\nfunction onUserUsedObject(avatar, object)\\n  if IS_CLIENT then showMessageToUser('Hello ' .. avatar.name, avatar) end\\nend\",\"material_edit\":\"--lua\\nlocal count = this_object:getNumMaterials()\\nif count > 0 then\\n  local material = this_object:getMaterial(0)\\n  material.colour = Vec3f(0.8, 0.2, 0.1)\\nend\",\"cooldown_pattern\":\"--lua\\nlocal ready = true\\nfunction onUserTouchedObject(avatar, object)\\n  if not ready then return end\\n  ready = false\\n  if IS_CLIENT then showMessageToUser('Activated', avatar) end\\n  createTimer(function(ob) ready = true end, 2, false)\\nend\",\"cross_object\":\"local target = getObjectForUID(12345)\\naddEventListener('onUserUsedObject', target.uid, function(avatar, object)\\n  if IS_CLIENT then showMessageToUser('Target used', avatar) end\\nend)\",\"full_examples\":\"/example_luau_scripts contains project examples; verify their current APIs and fix any stale sample before reusing.\"}";
		if(topic == "xml") return "{\"supported_forms\":[\"follow_path\",\"dynamic_texture_update\",\"vehicle configuration elements supported by the client parser\"],\"rules\":\"Keep the XML declaration optional, use one <script> root, and preserve all unrelated elements. For a route use <follow_path><speed>10</speed><time_offset>0</time_offset><waypoint><pos>x y z</pos><type>CurveIn|CurveOut|Stop</type><pause_time>10</pause_time></waypoint>...</follow_path>. Coordinates are world metres with Z up. Stop maps to a station; pause_time is seconds. CurveIn/CurveOut are trimmed before validation. XML is not Luau and must not be prefixed with --lua.\",\"validation\":\"update_object_script parses XML before saving; clients parse the supported route/vehicle data when notified.\"}";
		if(topic == "winter") return "{\"purpose\":\"Winter provides deterministic, non-reactive object motion. Define either or both optional entry points; it is separate from XML follow_path and Luau event scripts.\",\"entry_points\":[\"def evalRotation(float time, WinterEnv env) vec3\",\"def evalTranslation(float time, WinterEnv env) vec3\"],\"rotation\":\"Return an axis-angle vector: its direction is the rotation axis and its length is the counter-clockwise angle in radians. Example: vec3(0, 0, 1) * time rotates around Z.\",\"translation\":\"Return an XYZ offset in metres from the object's authored position. Example: vec3(sin(time * 1.5) * 0.8, 0, 0). time is global world time.\",\"environment\":\"WinterEnv has integer instance_index and num_instances. The client currently evaluates ordinary objects with instance_index=0 and num_instances=1. The bundled resources/winter_stdlib.txt provides vec2/vec3/vec4 and matrix types, arithmetic helpers and standard math functions. composeRotations(vec3, vec3) is also available.\",\"editing\":\"Winter source is stored in the object's Script field without a --lua marker. Use update_object_script to apply the full Winter source; connected native desktop clients compile it as Winter. The MCP server does not compile Winter, and the web client does not support Winter evaluation. Internal reference: /about_scripting and the bundled Winter standard library.\"}";
		if(topic == "all") return "{\"topics\":[\"overview\",\"coding\",\"events\",\"interaction\",\"game\",\"game_input\",\"game_bindings\",\"game_example\",\"game_quest_example\",\"game_resource_example\",\"game_strategy_example\",\"game_rpg_example\",\"game_monopoly_example\",\"game_card_example\",\"game_billiards_example\",\"game_economy_example\",\"game_wave_example\",\"game_survival_example\",\"game_tycoon_example\",\"game_systems\",\"prefabs\",\"hud_progress\",\"presentation_sync\",\"game_player_control\",\"objects\",\"materials\",\"avatars\",\"physics\",\"timers\",\"storage\",\"http\",\"audio\",\"animation\",\"door\",\"particles\",\"limits\",\"examples\",\"xml\",\"winter\"],\"instruction\":\"Read overview and the specific API topics before editing. Luau guide: /about_luau_scripting; examples: /example_luau_scripts. XML and Winter are distinct formats.\"}";
		throw glare::Exception("Unknown script reference topic. Use overview, coding, events, interaction, game, game_input, game_bindings, game_example, game_quest_example, game_resource_example, game_strategy_example, game_rpg_example, game_monopoly_example, game_card_example, game_billiards_example, game_economy_example, game_wave_example, game_survival_example, game_tycoon_example, game_systems, prefabs, hud_progress, presentation_sync, game_player_control, objects, materials, avatars, physics, timers, storage, http, audio, animation, door, particles, limits, examples, xml, winter, or all.");
	}
	if(tool_name == "search_pubchem_compounds") return MCPScientific::search(parser,args);
	if(tool_name == "get_pubchem_record") return MCPScientific::getRecord(parser,args);
	if(tool_name == "get_pubchem_compound") return MCPScientific::getCompound(parser,args);
	if(tool_name == "create_pubchem_molecule") return MCPScientific::create(parser,args,world_state,world,user_id,username);
	if(tool_name == "search_polyfork_assets") return MCPPolyfork::search(parser,args);
	if(tool_name == "get_polyfork_asset") return MCPPolyfork::getAsset(parser,args);
	if(tool_name == "place_polyfork_model") return MCPPolyfork::place(world_state,world,parser,args,user_id,username);
	if(tool_name == "get_object_script_log") return getObjectScriptLog(world_state, world, parser, args, user_id);
	if(tool_name == "validate_object_script")
	{
		const UID uid = Modeling::objectUID(parser, args);
		const std::string script = args.getChildStringValue(parser, "script");
		auto invalid = [&uid](const std::string& message)
		{
			return "{\"uid\":" + toString(uid.value()) + ",\"valid\":false,\"phase\":\"compile\",\"executed\":false,\"saved\":false,\"message\":\"" + web::Escaping::JSONEscape(message) + "\"}";
		};
		{
			WorldStateLock lock(world_state.mutex);
			WorldObjectRef ob = Modeling::findObject(world, lock, uid);
			if(!Modeling::editable(*ob, world, user_id)) throw glare::Exception("Permission denied for this object's script.");
		}
		if(script.size() > WorldObject::MAX_SCRIPT_SIZE)
			return invalid("Script exceeds the 10000-byte object script limit.");
		if(script.find('\0') != std::string::npos)
			return invalid("Script cannot contain a NUL byte.");
		if(!hasPrefix(script, "--lua"))
			return invalid("Luau source must begin with --lua.");
		static std::mutex validation_mutex;
		static std::chrono::steady_clock::time_point last_validation_time;
		std::unique_lock<std::mutex> validation_lock(validation_mutex, std::try_to_lock);
		if(!validation_lock.owns_lock())
			return invalid("Another Luau compile check is running; retry after it completes.");
		const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
		if(last_validation_time.time_since_epoch().count() != 0 && now - last_validation_time < std::chrono::milliseconds(100))
			return invalid("Compile checks are limited to 10 requests per second on this server; retry shortly.");
		last_validation_time = now;
		try
		{
			LuaVM vm;
			vm.max_total_mem_allowed = 32 * 1024 * 1024;
			LuaScriptOptions options;
			options.max_num_interrupts = 10000;
			LuaScript compiled(&vm, options, script);
			const LuauGameApiDiagnostics api_diagnostics = gameApiDiagnosticsJSON(script);
			return "{\"uid\":" + toString(uid.value()) + ",\"valid\":true,\"phase\":\"compile\",\"compiled\":true,\"executed\":false,\"saved\":false,\"game_api_warnings\":" + api_diagnostics.warnings_json + ",\"game_api_warnings_truncated\":" + (api_diagnostics.warnings_truncated ? "true" : "false") + ",\"game_api_warning_scope\":\"Advisory only: flags explicit game.member names and literal-string game['member'] keys outside the built-in table.\",\"game_api_uses\":" + api_diagnostics.uses_json + ",\"game_api_uses_truncated\":" + (api_diagnostics.uses_truncated ? "true" : "false") + ",\"game_api_use_scope\":\"The list is capped at 64 explicit member uses and truncated is reported. Entries include documented execution side, guard_context, and side_mismatch. Guard detection recognizes only direct optionally parenthesized or negated if IS_SERVER or if IS_CLIENT branches; compound conditions and runtime availability are not resolved.\",\"game_api_dynamic_accesses\":" + api_diagnostics.dynamic_accesses_json + ",\"game_api_dynamic_accesses_truncated\":" + (api_diagnostics.dynamic_accesses_truncated ? "true" : "false") + ",\"game_api_dynamic_access_scope\":\"Lists up to 64 source lines that use computed game[key] indexing. The key and its runtime value are not resolved; review those lines manually against the API reference.\",\"validation_scope\":\"Luau syntax and bytecode loading were checked; script execution and client/server API availability were not checked.\"}";
		}
		catch(LuaScriptExcepWithLocation& e)
		{
			return invalid(e.messageWithLocations());
		}
		catch(std::exception& e)
		{
			return invalid(e.what());
		}
	}
	if(tool_name == "get_object_script" || tool_name == "update_object_script")
	{
		WorldStateLock lock(world_state.mutex);
		WorldObjectRef ob = Modeling::findObject(world, lock, Modeling::objectUID(parser, args));
		if(!Modeling::editable(*ob, world, user_id)) throw glare::Exception("Permission denied for this object's script.");
		if(tool_name == "get_object_script")
			return "{\"uid\":" + toString(ob->uid.value()) + ",\"script\":\"" + web::Escaping::JSONEscape(ob->script) + "\",\"bytes\":" + toString(ob->script.size()) + "}";

		if(world_state.isInReadOnlyMode()) throw glare::Exception("Server is in read-only mode.");
		const std::string script = args.getChildStringValue(parser, "script");
		if(script.size() > WorldObject::MAX_SCRIPT_SIZE) throw glare::Exception("Script exceeds the 10000-byte object script limit.");
		if(script.find('\0') != std::string::npos) throw glare::Exception("Script cannot contain a NUL byte.");
		const bool is_luau = hasPrefix(script, "--lua");
		const bool is_xml = hasPrefix(script, "<?xml") || hasPrefix(script, "<script");
		if(!script.empty() && is_xml)
		{
			try
			{
				IndigoXMLDoc doc(script.c_str(), script.size());
				if(std::string(doc.getRootElement().name()) != "script")
					throw glare::Exception("XML object scripts must have a <script> root element.");
			}
			catch(const glare::Exception& e)
			{
				throw glare::Exception(std::string("Invalid Metasiberia XML object script: ") + e.what());
			}
		}

		Reference<LuaScriptEvaluator> new_evaluator;
		bool server_execution_enabled = BitUtils::isBitSet(world_state.feature_flag_info.feature_flags, ServerAllWorldsState::SERVER_SCRIPT_EXEC_FEATURE_FLAG);
		if(!script.empty() && is_luau && server_execution_enabled)
		{
			if(!ob->creator_id.valid()) throw glare::Exception("Cannot start a server-side object script without a valid creator account.");
			Reference<SubstrataLuaVM> lua_vm;
			auto vm_it = world_state.lua_vms.find(ob->creator_id);
			if(vm_it == world_state.lua_vms.end())
			{
				lua_vm = new SubstrataLuaVM(SubstrataLuaVM::SubstrataLuaVMArgs(&server));
				world_state.registerLuaVMForUser(ob->creator_id, lua_vm);
			}
			else lua_vm = vm_it->second;
			new_evaluator = new LuaScriptEvaluator(lua_vm, &server, script, ob.ptr(), &world, lock);
		}

		ob->script = script;
		ob->lua_script_evaluator = new_evaluator;
		BitUtils::setBit(ob->changed_flags, WorldObject::SCRIPT_CHANGED);
		ob->last_modified_time = TimeStamp::currentTime();
		ob->from_remote_other_dirty = true;
		world.addWorldObjectAsDBDirty(ob, lock);
		world.getDirtyFromRemoteObjects(lock).insert(ob);
		world_state.markAsChanged();
		const char* const script_format = script.empty() ? "empty" : is_luau ? "luau" : is_xml ? "xml" : "winter";
		return "{\"uid\":" + toString(ob->uid.value()) + ",\"saved\":true,\"bytes\":" + toString(script.size()) + ",\"format\":\"" + script_format + "\",\"server_execution_enabled\":" + std::string(server_execution_enabled ? "true" : "false") + ",\"server_evaluator_reloaded\":" + std::string(server_execution_enabled && is_luau ? "true" : "false") + "}";
	}
	if(tool_name == "import_texture") return Modeling::importTexture(world_state,parser,args);
	if(tool_name == "create_image") return Modeling::imageObject(world_state,world,parser,args,user_id,username);
	if(tool_name == "create_reference_plane") return Modeling::referencePlaneObject(world_state,world,parser,args,user_id,username);
	if(tool_name == "apply_image_texture") return Modeling::applyImageTexture(world_state,world,parser,args,user_id,username);
	if(tool_name == "get_modeling_example")
		return Modeling::buildingExample(args.getChildStringValue(parser,"kind"));
	if(tool_name == "measure_object") return Modeling::measureObject(world_state,world,parser,args);
	if(tool_name == "resize_object") return Modeling::precisionTransform(world_state,world,parser,args,user_id,"resize");
	if(tool_name == "align_object") return Modeling::precisionTransform(world_state,world,parser,args,user_id,"align");
	if(tool_name == "snap_object_to_grid") return Modeling::precisionTransform(world_state,world,parser,args,user_id,"snap");
	if(tool_name == "build_parametric") return Modeling::Building::create(world_state,world,parser,args,user_id,username);
	if(tool_name == "preview_parametric") return Modeling::Building::preview(world_state,world,parser,args,user_id);
	if(tool_name == "get_parametric") return Modeling::Building::get(world_state,world,parser,args,user_id);
	if(tool_name == "update_parametric_component") return Modeling::Building::update(world_state,world,parser,args,user_id,username);
	if(tool_name == "build_assembly") return Modeling::Assembly::create(world_state,world,parser,args,user_id,username);
	if(tool_name == "preview_assembly") return Modeling::Assembly::preview(world_state,world,parser,args,user_id);
	if(tool_name == "get_assembly") return Modeling::Assembly::get(world_state,world,parser,args,user_id);
	if(tool_name == "update_assembly_component") return Modeling::Assembly::update(world_state,world,parser,args,user_id,username);
	if(tool_name == "build_mesh")
		return Modeling::meshObject(world_state, world, parser, args, user_id, username);
	if(tool_name == "convert_mesh") return Modeling::convertMesh(world_state,world,parser,args,user_id,username);
	if(tool_name == "simplify_mesh") return Modeling::simplifyMesh(world_state,world,parser,args,user_id,username);
	if(tool_name == "subdivide_mesh" || tool_name == "remesh_mesh" || tool_name == "sculpt_mesh" || tool_name == "edit_mesh_uv")
		return Modeling::advancedMeshEdit(world_state,world,parser,args,user_id,username,tool_name);
	if(tool_name == "get_rig") return Modeling::getRig(world_state,world,parser,args,user_id);
	if(tool_name == "create_skeleton") return Modeling::createSkeleton(world_state,world,parser,args,user_id,username);
	if(tool_name == "set_skin_weights") return Modeling::setSkinWeights(world_state,world,parser,args,user_id,username);
	if(tool_name == "set_animation_clip") return Modeling::setAnimationClip(world_state,world,parser,args,user_id,username);
	if(tool_name == "deform_mesh")
		return Modeling::deformMesh(world_state,world,parser,args,user_id,username);
	if(tool_name == "edit_mesh_topology")
		return Modeling::editMeshTopology(world_state,world,parser,args,user_id,username);
	if(tool_name == "unwrap_mesh_uv")
		return Modeling::unwrapMeshUV(world_state,world,parser,args,user_id,username);
	if(tool_name == "boolean_mesh")
		return Modeling::booleanMesh(world_state,world,parser,args,user_id,username);
	if(tool_name == "get_capabilities")
		return Modeling::capabilities();
	if(tool_name == "check_build_permissions")
		return Modeling::checkBuildPermissions(world_state, world, parser, args, user_id);
	if(tool_name == "get_object")
		return Modeling::getObject(world_state, world, parser, args);
	if(tool_name == "get_geometry")
		return Modeling::getGeometry(world_state, world, parser, args);
	if(tool_name == "validate_mesh")
		return Modeling::validateMesh(world_state, world, parser, args);
	if(tool_name == "duplicate_object")
		return Modeling::duplicateObject(world_state, world, parser, args, user_id, username);
	if(tool_name == "create_cube")
		return Modeling::cube(world_state, world, parser, args, user_id, username);
	if(tool_name == "create_voxel_object")
		return Modeling::voxelObject(world_state, world, parser, args, user_id, username);
	if(tool_name == "create_object")
		return Modeling::resourceObject(world_state, world, parser, args, user_id, username);
	if(tool_name == "update_object")
		return Modeling::editObject(world_state, world, parser, args, user_id, username, false);
	if(tool_name == "delete_object")
		return Modeling::editObject(world_state, world, parser, args, user_id, username, true);

	WorldStateLock all_lock(world_state.mutex);
	if(tool_name == "list_parcels_in_bounds")
	{
		const Vec3d lo=Modeling::vector(parser,args,"bounds_min",Vec3d(0,0,0),-1e7,1e7);
		const Vec3d hi=Modeling::vector(parser,args,"bounds_max",Vec3d(0,0,0),-1e7,1e7);
		if(lo.x>=hi.x || lo.y>=hi.y || lo.z>=hi.z) throw glare::Exception("bounds_min must be below bounds_max on all three world axes.");
		const size_t offset=(size_t)Modeling::integer(parser,args,"offset",0,0,1000000);
		const size_t limit=(size_t)Modeling::integer(parser,args,"limit",100,1,500);
		const Vec3d centre=(lo+hi)*0.5;
		std::vector<std::pair<double,ParcelRef>> matches;
		for(const auto& entry:world.getParcels(all_lock))
		{
			const ParcelRef& parcel=entry.second;
			// Parcels loaded from persistent state are inserted into this authoritative map
			// while retaining State_JustCreated; don't require State_Alive here.
			if(parcel->state==Parcel::State_Dead) continue;
			// Parcels define 2D land lots. Use the requested XY footprint for discovery,
			// regardless of the caller's Z range, and return z_bounds as metadata.
			if(parcel->aabb_max.x<lo.x || parcel->aabb_min.x>hi.x || parcel->aabb_max.y<lo.y || parcel->aabb_min.y>hi.y) continue;
			const Vec3d d=(parcel->aabb_min+parcel->aabb_max)*0.5-centre;
			matches.push_back(std::make_pair(d.x*d.x+d.y*d.y+d.z*d.z,parcel));
		}
		std::sort(matches.begin(),matches.end(),[](const std::pair<double,ParcelRef>& a,const std::pair<double,ParcelRef>& b){return a.first!=b.first?a.first<b.first:a.second->id.value()<b.second->id.value();});
		const size_t end=std::min(matches.size(),offset+limit);
		std::string out="{\"total\":"+toString(matches.size())+",\"offset\":"+toString(offset)+",\"limit\":"+toString(limit)+",\"bounds_min\":"+Modeling::vecJSON(lo)+",\"bounds_max\":"+Modeling::vecJSON(hi)+",\"parcels\":[";
		for(size_t i=offset;i<end;++i)
		{
			if(i>offset) out+=",";
			const Parcel& parcel=*matches[i].second;
			out+="{\"id\":"+toString(parcel.id.value())+",\"title\":\""+web::Escaping::JSONEscape(parcel.getUseTitle())+"\",\"bounds_min\":"+Modeling::vecJSON(parcel.aabb_min)+",\"bounds_max\":"+Modeling::vecJSON(parcel.aabb_max)+",\"z_bounds\":["+doubleToString(parcel.zbounds.x)+","+doubleToString(parcel.zbounds.y)+"],\"vertices_xy\":[";
			for(size_t v=0;v<4;++v)
			{
				if(v) out+=",";
				out+="["+doubleToString(parcel.verts[v].x)+","+doubleToString(parcel.verts[v].y)+"]";
			}
			out+="]}";
		}
		out+="]";
		if(end<matches.size()) out+=",\"next_offset\":"+toString(end);
		else out+=",\"next_offset\":null";
		return out+"}";
	}

	if(tool_name == "get_world_info")
		return "{\"name\":\"" + web::Escaping::JSONEscape(world.details.name) + "\",\"description\":\"" + web::Escaping::JSONEscape(world.details.description) + "\"}";
	if(tool_name == "list_resources")
	{
		int limit = Modeling::integer(parser,args,"limit",100,1,200);
		const std::string search=args.getChildStringValueWithDefaultVal(parser,"search","");
		std::string out = "{\"resources\":[";
		bool first = true;
		Lock resource_lock(world_state.resource_manager->getMutex());
		for(const auto& resource_pair : world_state.resource_manager->getResourcesForURL())
		{
			const std::string url = toStdString(resource_pair.first);
			if(!hasExtension(url, "bmesh") || (!search.empty() && url.find(search)==std::string::npos) || resource_pair.second->getState()!=Resource::State_Present)
				continue;
			if(!first) out += ",";
			first = false;
			out += "{\"model_url\":\"" + web::Escaping::JSONEscape(url) + "\"}";
			if(--limit <= 0) break;
		}
		return out + "]}";
	}
	if(tool_name == "list_avatars")
	{
		std::string out = "{\"avatars\":[";
		bool first = true;
		for(const auto& p : world.getAvatars(all_lock))
		{
			if(!first) out += ",";
			first = false;
			out += "{\"uid\":" + toString(p.second->uid.value()) + ",\"is_you\":" + std::string(p.second->name==username ? "true" : "false") + ",\"heading\":" + doubleToString(p.second->rotation.z) + ",\"name\":\"" + web::Escaping::JSONEscape(p.second->getUseName()) + "\",\"x\":" + doubleToString(p.second->pos.x) + ",\"y\":" + doubleToString(p.second->pos.y) + ",\"z\":" + doubleToString(p.second->pos.z) + "}";
		}
		return out + "]}";
	}
	if(tool_name == "list_objects_near")
	{
		const Vec3d center = args.hasChild("pos") ? Modeling::position(parser,args,Vec3d(0,0,0)) : Modeling::nearAvatar(world,all_lock,username);
		const double radius = Modeling::number(parser,args,"radius",50,0.1,1000);
		const size_t limit = (size_t)Modeling::integer(parser,args,"limit",50,1,200);
		std::vector<std::pair<double,WorldObjectRef>> nearby;
		for(const auto& entry : world.getObjects(all_lock))
		{
			if(entry.second->state==WorldObject::State_Dead) continue;
			const Vec3d d=entry.second->pos-center;
			const double dist=d.x*d.x+d.y*d.y+d.z*d.z;
			if(dist<=radius*radius) nearby.push_back(std::make_pair(dist,entry.second));
		}
		std::sort(nearby.begin(),nearby.end(),[](const std::pair<double,WorldObjectRef>& a,const std::pair<double,WorldObjectRef>& b){ return a.first<b.first; });
		std::string out="{\"objects\":[";
		for(size_t i=0;i<std::min(limit,nearby.size());++i) { if(i) out+=",";out+=Modeling::describe(*nearby[i].second); }
		return out+"]}";
	}
	if(tool_name == "list_objects_in_bounds")
	{
		const Vec3d lo=Modeling::vector(parser,args,"bounds_min",Vec3d(0,0,0),-1e7,1e7);
		const Vec3d hi=Modeling::vector(parser,args,"bounds_max",Vec3d(0,0,0),-1e7,1e7);
		if(lo.x>=hi.x || lo.y>=hi.y || lo.z>=hi.z) throw glare::Exception("bounds_min must be below bounds_max on all three world axes.");
		const size_t offset=(size_t)Modeling::integer(parser,args,"offset",0,0,1000000);
		const size_t limit=(size_t)Modeling::integer(parser,args,"limit",100,1,500);
		const Vec3d centre=(lo+hi)*0.5;
		std::vector<std::pair<double,WorldObjectRef>> matches;
		for(const auto& entry:world.getObjects(all_lock))
		{
			const WorldObjectRef& ob=entry.second;
			if(ob->state==WorldObject::State_Dead) continue;
			const js::AABBox box=ob->getAABBWS();
			if(box.max_[0]<lo.x || box.min_[0]>hi.x || box.max_[1]<lo.y || box.min_[1]>hi.y || box.max_[2]<lo.z || box.min_[2]>hi.z) continue;
			const Vec3d d=ob->pos-centre;
			matches.push_back(std::make_pair(d.x*d.x+d.y*d.y+d.z*d.z,ob));
		}
		std::sort(matches.begin(),matches.end(),[](const std::pair<double,WorldObjectRef>& a,const std::pair<double,WorldObjectRef>& b){return a.first<b.first;});
		const size_t end=std::min(matches.size(),offset+limit);
		std::string out="{\"total\":"+toString(matches.size())+",\"offset\":"+toString(offset)+",\"limit\":"+toString(limit)+",\"bounds_min\":"+Modeling::vecJSON(lo)+",\"bounds_max\":"+Modeling::vecJSON(hi)+",\"objects\":[";
		for(size_t i=offset;i<end;++i) { if(i>offset) out+=",";out+=Modeling::describe(*matches[i].second); }
		out+="]";
		if(end<matches.size()) out+=",\"next_offset\":"+toString(end);
		else out+=",\"next_offset\":null";
		return out+"}";
	}
	throw glare::Exception("Unknown tool: " + tool_name);
}


void handleMCPRequest(Server& server, ServerAllWorldsState& world_state, const web::RequestInfo& request_info, web::ReplyInfo& reply_info)
{
	if(request_info.verb != "POST")
	{
		web::ResponseUtils::writeHTTPNotFoundHeaderAndData(reply_info, "MCP accepts POST requests only.");
		return;
	}
	if(request_info.post_content.size() > 8 * 1024 * 1024)
	{
		writeError(reply_info, "null", -32600, "MCP request is too large.");
		return;
	}

	UserID acting_user;
	std::string username;
	ServerWorldStateRef selected_world;
	std::string selected_world_key;
	{
		Lock lock(world_state.mutex);
		if(!authenticate(world_state, request_info, acting_user, username))
		{
			const std::string auth = headerValue(request_info, "Authorization");
			const std::string fallback = headerValue(request_info, "X-Substrata-Login");
			const std::string cookie = cookieValue(request_info, "mcp-login");
			conPrint("MCP authentication failed (authorization_header=" + boolToString(!auth.empty()) +
				", fallback_header=" + boolToString(!fallback.empty()) +
				", cookie=" + boolToString(!cookie.empty()) + ").");
			web::ResponseUtils::writeHTTPUnauthorizedHeaderAndData(reply_info, "Valid Substrata-Login credentials are required.");
			return;
		}

		// Never silently fall back to the root world when client context is missing.
		const std::string requested_world_header = headerValue(request_info, "X-Substrata-World");
		if(requested_world_header.empty()) { writeError(reply_info,"null",-32600,"Missing active world context. Reconnect the Metasiberia MCP client."); return; }
		const std::string requested_world = requested_world_header.empty() || requested_world_header == "__root__" ? std::string() : requested_world_header;
		auto world_it = world_state.world_states.find(requested_world);
		if(world_it == world_state.world_states.end() || world_it->second.isNull())
		{
			web::ResponseUtils::writeHTTPNotFoundHeaderAndData(reply_info, "The connected world is not available on the server.");
			return;
		}
		selected_world = world_it->second;
		selected_world_key = requested_world;
	}

	std::string request_id = "null";
	bool tool_call = false;
	try
	{
		const std::string body((const char*)request_info.post_content.data(), request_info.post_content.size());
		JSONParser parser;
		parser.parseBuffer(body.data(), body.size());
		if(parser.nodes.empty() || parser.nodes[0].type != JSONNode::Type_Object)
			throw glare::Exception("Expected a JSON-RPC object.");
		const JSONNode& root = parser.nodes[0];
		const std::string id = idJSON(parser, root);
		request_id = id;
		const std::string method = root.getChildStringValue(parser, "method");
		if(method == "notifications/initialized" && !root.hasChild("id"))
		{
			web::ResponseUtils::writeRawString(reply_info, "HTTP/1.1 202 Accepted\r\nConnection: Keep-Alive\r\nContent-Length: 0\r\n\r\n");
			return;
		}
		if(method == "initialize")
			writeJSON(reply_info, "{\"jsonrpc\":\"2.0\",\"id\":" + id + ",\"result\":{\"protocolVersion\":\"2024-11-05\",\"capabilities\":{\"tools\":{}},\"serverInfo\":{\"name\":\"Metasiberia\",\"version\":\"1\"}}}");
		else if(method == "tools/list")
			writeJSON(reply_info, "{\"jsonrpc\":\"2.0\",\"id\":" + id + ",\"result\":" + Modeling::toolsListJSON() + "}");
		else if(method == "tools/call")
		{
			tool_call = true;
			const JSONNode& params = root.getChildObject(parser, "params");
			const std::string tool_name = params.getChildStringValue(parser, "name");
			JSONNode empty_args;
			empty_args.type = JSONNode::Type_Object;
			const JSONNode& args = params.hasChild("arguments") ? params.getChildObject(parser, "arguments") : empty_args;
			std::string result = callTool(server, world_state, *selected_world, parser, args, tool_name, acting_user, username);
			result.insert(1,"\"world\":\"" + web::Escaping::JSONEscape(selected_world_key) + "\",");
			// Keep the structured result and also expose it as text.  Codex clients
			// consume the MCP content array, and an empty text item made successful
			// creations look like an unverified claim with no UID or coordinates.
			writeJSON(reply_info, "{\"jsonrpc\":\"2.0\",\"id\":" + id + ",\"result\":{\"content\":[{\"type\":\"text\",\"text\":\"" + web::Escaping::JSONEscape(result) + "\"}],\"structuredContent\":" + result + ",\"isError\":false}}");
		}
		else
			writeError(reply_info, id, -32601, "Method not found.");
	}
	catch(Indigo::IndigoException& e)
	{
		const std::string message(e.what().dataPtr(),e.what().length());
		writeJSON(reply_info, "{\"jsonrpc\":\"2.0\",\"id\":" + request_id + ",\"result\":{\"isError\":true,\"content\":[{\"type\":\"text\",\"text\":\"" + web::Escaping::JSONEscape(message) + "\"}]}}");
	}
	catch(glare::Exception& e)
	{
		if(tool_call)
			writeJSON(reply_info, "{\"jsonrpc\":\"2.0\",\"id\":" + request_id + ",\"result\":{\"isError\":true,\"content\":[{\"type\":\"text\",\"text\":\"" + web::Escaping::JSONEscape(e.what()) + "\"}]}}");
		else writeError(reply_info, request_id, -32603, e.what());
	}
}

} // namespace MCPHandlers
