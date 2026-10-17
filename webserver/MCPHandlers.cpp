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
#include <algorithm>
#include <vector>
#include <dll/include/IndigoException.h>
#include <utils/IndigoXMLDoc.h>
#include "MCPModeling.h"
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


static std::string callTool(Server& server, ServerAllWorldsState& world_state, ServerWorldState& world, const JSONParser& parser, const JSONNode& args, const std::string& tool_name, const UserID& user_id, const std::string& username)
{
	if(tool_name == "get_luau_reference")
	{
		const std::string topic = args.getChildStringValueWithDefaultVal(parser, "topic", "overview");
		if(topic == "overview") return "{\"language\":\"Luau event scripts for world objects; source must start with --lua\",\"workflow\":[\"Read get_object_script for the exact UID and preserve useful existing logic.\",\"Read events and the API topics needed for the requested behaviour.\",\"Decide whether each effect belongs on server, client, or both; avoid duplicate execution.\",\"Write the complete script with update_object_script for that same UID.\",\"Read it back with get_object_script and report server_execution_enabled/server_evaluator_reloaded.\",\"For visual/client-only behavior, ask the user to test in the Qt client and inspect Script Editor build/runtime results.\"],\"scope\":\"MCP saves the object's script and notifies connected clients. Luau execution is controlled by the world's server-script feature flag. Saving is not proof that a client-only visual effect works.\",\"topics\":[\"coding\",\"events\",\"objects\",\"materials\",\"avatars\",\"physics\",\"timers\",\"storage\",\"http\",\"audio\",\"animation\",\"particles\",\"limits\",\"examples\",\"xml\",\"winter\"]}";
		if(topic == "coding") return "{\"format\":\"First line: --lua. Vec3d(x,y,z) constructs a position table; Vec3f(x,y,z) constructs a Luau vector for axis/scale/material colour/velocity fields.\",\"execution\":\"The server/client reload executes the script body, so keep top-level code short and put repeated work in event callbacks/timers.\",\"edit_protocol\":\"Read the exact object's script first; preserve unrelated handlers/state; deliver a complete replacement; update that same UID; read it back. Do not claim compilation or runtime success from an MCP save response alone.\",\"diagnostics\":\"Script Editor/server logs report build and runtime errors. During MCP update, a compile/top-level error happens before the saved script/evaluator are replaced, so the previous version remains. A runtime error after a successful save disables that evaluator; correct the error and reload. Top-level external side effects are not transactional.\"}";
		if(topic == "events") return "{\"self_handlers\":[\"onUserTouchedObject(avatar, object)\",\"onUserUsedObject(avatar, object)\",\"onUserMovedNearToObject(avatar, object)\",\"onUserMovedAwayFromObject(avatar, object)\",\"onUserEnteredParcel(avatar, object, parcel)\",\"onUserExitedParcel(avatar, object, parcel)\",\"onUserEnteredVehicle(avatar, vehicle_object)\",\"onUserExitedVehicle(avatar, vehicle_object)\",\"onChatMessage(avatar, message)\"],\"other_object\":\"addEventListener(event_name, object_uid, handler_function) subscribes to an existing event; removeEventListener(event_name, object_uid, same_handler_function) unsubscribes and returns true if removed.\",\"behavior\":\"A function with a matching name automatically handles events for this script's object. E/use action triggers onUserUsedObject. Touch can repeat while contact continues; near/far use a spatial proximity check (~20 m). Parcel/vehicle events are discrete transitions.\",\"limits\":\"At most 100 active other-object event registrations per script.\"}";
		if(topic == "objects") return "{\"globals\":[\"this_object\",\"IS_CLIENT\",\"IS_SERVER\",\"Vec3d(x,y,z)\",\"Vec3f(x,y,z)\",\"getObjectForUID(uid)\",\"findObjectForUID(uid)\",\"getCurrentTime()\",\"showMessageToUser(message, avatar)\",\"addEventListener(event, uid, callback)\",\"removeEventListener(event, uid, same_callback)\"],\"readable_object_fields\":[\"uid\",\"model_url\",\"pos\",\"axis\",\"angle\",\"scale\",\"collidable\",\"dynamic\",\"sensor\",\"content\",\"target_url\",\"video_autoplay\",\"video_loop\",\"video_muted\",\"mass\",\"friction\",\"restitution\",\"centre_of_mass_offset_os\",\"audio_source_url\",\"audio_volume\",\"audio_loop\"],\"methods\":[\"getNumMaterials()\",\"getMaterial(index)\",\"playAudio()\",\"isPlayingAudio()\",\"getAnimationIndex(name)\",\"startPlayingAnimation(index)\"],\"write_example\":\"this_object.pos = Vec3d(0, 0, 2)\",\"notes\":\"pos is Vec3d; axis/scale and other vector fields are Luau vectors made with Vec3f. Object transforms/properties are server-backed and marked dirty by setters. getObjectForUID throws when missing; findObjectForUID returns nil. showMessageToUser only displays in the local GUI client.\"}";
		if(topic == "materials") return "{\"access\":\"Object:getNumMaterials() and Object:getMaterial(index)\",\"material_properties\":[\"colour\",\"colour_texture_url\",\"emission_rgb\",\"emission_texture_url\",\"normal_map_url\",\"roughness_val\",\"roughness_texture_url\",\"metallic_fraction_val\",\"opacity_val\",\"tex_matrix\",\"emission_lum_flux_or_lum\",\"hologram\",\"double_sided\"],\"note\":\"Use only documented Material fields. Material edits may be client- or server-side according to runtime behavior.\"}";
		if(topic == "avatars") return "{\"fields\":[\"pos\",\"name\",\"linear_velocity\",\"vehicle_inside\"],\"events\":\"Handlers receive an Avatar table. Pass it to showMessageToUser(message, avatar).\",\"write_limits\":\"Avatar name is read-only. Setting pos/linear_velocity only controls the local player's avatar in GUI client scripts; server-side assignment does not move a remote user. vehicle_inside is nil or a WorldObject.\"}";
		if(topic == "physics") return "{\"object_fields\":[\"collidable\",\"dynamic\",\"sensor\",\"mass\",\"friction\",\"restitution\",\"centre_of_mass_offset_os\"],\"lua_control\":\"Luau can set these existing WorldObject properties and the server replicates them. There is no binding for arbitrary forces, joints, collision-shape authoring, or direct access to Jolt.\",\"moving_platform\":\"For a lift, use a broad collidable non-dynamic object with a Luau script. Move its pos from a server timer at a modest interval. Scripted objects use kinematic collision bodies; clients interpolate replicated transforms and move that collider through the physics engine so grounded avatars inherit platform velocity. Avoid teleport-sized steps.\",\"example\":\"--lua\\nif IS_SERVER then\\n  this_object.collidable = true\\n  this_object.dynamic = false\\n  local start = this_object.pos\\n  local height = 4\\n  local period = 8\\n  createTimer(function(ob)\\n    local phase = (getCurrentTime() % period) / period\\n    local triangle = 1 - math.abs(2 * phase - 1)\\n    ob.pos = Vec3d(start.x, start.y, start.z + height * triangle)\\n  end, 0.05, true)\\nend\",\"caveats\":\"Enable server-side Lua. Very fast motion, small platforms, network stalls or long client frame pauses can still make standing unreliable. This API does not provide elevator constraints or control of avatar ground snapping.\"}";
		if(topic == "timers") return "{\"functions\":[\"createTimer(callback, interval_seconds, repeating): handle\",\"destroyTimer(handle)\"],\"callback\":\"callback(this_object)\",\"limits\":\"Maximum 4 active timers per script. Use a positive interval; cancel repeating timers when no longer needed. Do not create a busy loop.\"}";
		if(topic == "storage") return "{\"functions\":[\"objectstorage.getItem(key)\",\"objectstorage.setItem(key, value)\"],\"scope\":\"Persistent server storage scoped to the script object's UID; it survives server restart and script reload.\",\"constraints\":\"Key <= 256 bytes. Values must be serializable Lua data (tables/scalars), max nesting depth 16 and serialized size 64 KiB. Client-only storage reads nil and writes nothing.\"}";
		if(topic == "http") return "{\"functions\":[\"doHTTPGetRequestAsync(url, headers, onDone, onError)\",\"doHTTPPostRequestAsync(url, body, content_type, headers, onDone, onError)\",\"parseJSON(json)\",\"getSecret(name)\"],\"scope\":\"HTTP requests are server-side only and require the server HTTP feature to be enabled. Client calls do nothing.\",\"limits\":\"Requests are rate-limited to 5 per user per 300 seconds. getSecret reads account secrets; never hardcode API keys in object scripts.\",\"callbacks\":\"onDone(response) receives one table with response_code, response_message, mime_type and body_data (buffer). onError(error) receives one table with error_code and error_description.\"}";
		if(topic == "audio") return "{\"fields\":[\"audio_source_url\",\"audio_volume\",\"audio_loop\"],\"methods\":[\"object:playAudio()\",\"object:isPlayingAudio()\"],\"scope\":\"Playback methods are GUI-client features; changing source/volume/loop updates object properties. A server script can set properties but cannot itself render sound.\"}";
		if(topic == "animation") return "{\"methods\":[\"object:getAnimationIndex(name): number\",\"object:startPlayingAnimation(index)\"],\"workflow\":\"Run on the client that renders the model. Look up by exact animation name; -1 means not found. If found, call startPlayingAnimation(index).\",\"limits\":\"Animation playback is client-side and only works for a loaded animated model; it is not a skeleton/rig authoring API and does not create networked animation clips.\"}";
		if(topic == "particles") return "{\"global_table\":\"emitter\",\"methods\":[\"emitter.start()\",\"emitter.stop()\",\"emitter.burst(count)\",\"emitter.clear()\"],\"aliases\":[\"emitter.clearParticles()\"],\"scope\":\"These operate on this_object only when its content configures a particle emitter; they are GUI-client runtime controls and return true/false. burst count is clamped to 1..512.\"}";
		if(topic == "limits") return "{\"script_source\":\"Maximum 10,000 bytes per object script.\",\"execution\":\"Luau VM has a 16 MiB memory limit and each script execution is interrupted after 10,000 VM interrupts.\",\"timers\":\"4 per script\",\"listeners\":\"100 other-object event registrations per script\",\"http\":\"Server feature flag required; rate limited\",\"safe_design\":\"Bound loops and data; do expensive setup once; throttle repeated touch/proximity work; avoid per-frame timers and recursive event cascades.\"}";
		if(topic == "examples") return "{\"simple_interaction\":\"--lua\\nfunction onUserUsedObject(avatar, object)\\n  if IS_CLIENT then showMessageToUser('Hello ' .. avatar.name, avatar) end\\nend\",\"material_edit\":\"--lua\\nlocal count = this_object:getNumMaterials()\\nif count > 0 then\\n  local material = this_object:getMaterial(0)\\n  material.colour = Vec3f(0.8, 0.2, 0.1)\\nend\",\"cooldown_pattern\":\"--lua\\nlocal ready = true\\nfunction onUserTouchedObject(avatar, object)\\n  if not ready then return end\\n  ready = false\\n  if IS_CLIENT then showMessageToUser('Activated', avatar) end\\n  createTimer(function(ob) ready = true end, 2, false)\\nend\",\"cross_object\":\"local target = getObjectForUID(12345)\\naddEventListener('onUserUsedObject', target.uid, function(avatar, object)\\n  if IS_CLIENT then showMessageToUser('Target used', avatar) end\\nend)\",\"full_examples\":\"/example_luau_scripts contains project examples; verify their current APIs and fix any stale sample before reusing.\"}";
		if(topic == "xml") return "{\"supported_forms\":[\"follow_path\",\"dynamic_texture_update\",\"vehicle configuration elements supported by the client parser\"],\"rules\":\"Keep the XML declaration optional, use one <script> root, and preserve all unrelated elements. For a route use <follow_path><speed>10</speed><time_offset>0</time_offset><waypoint><pos>x y z</pos><type>CurveIn|CurveOut|Stop</type><pause_time>10</pause_time></waypoint>...</follow_path>. Coordinates are world metres with Z up. Stop maps to a station; pause_time is seconds. CurveIn/CurveOut are trimmed before validation. XML is not Luau and must not be prefixed with --lua.\",\"validation\":\"update_object_script parses XML before saving; clients parse the supported route/vehicle data when notified.\"}";
		if(topic == "winter") return "{\"purpose\":\"Winter provides deterministic, non-reactive object motion. Define either or both optional entry points; it is separate from XML follow_path and Luau event scripts.\",\"entry_points\":[\"def evalRotation(float time, WinterEnv env) vec3\",\"def evalTranslation(float time, WinterEnv env) vec3\"],\"rotation\":\"Return an axis-angle vector: its direction is the rotation axis and its length is the counter-clockwise angle in radians. Example: vec3(0, 0, 1) * time rotates around Z.\",\"translation\":\"Return an XYZ offset in metres from the object's authored position. Example: vec3(sin(time * 1.5) * 0.8, 0, 0). time is global world time.\",\"environment\":\"WinterEnv has integer instance_index and num_instances. The client currently evaluates ordinary objects with instance_index=0 and num_instances=1. The bundled resources/winter_stdlib.txt provides vec2/vec3/vec4 and matrix types, arithmetic helpers and standard math functions. composeRotations(vec3, vec3) is also available.\",\"editing\":\"Winter source is stored in the object's Script field without a --lua marker. Use update_object_script to apply the full Winter source; connected native desktop clients compile it as Winter. The MCP server does not compile Winter, and the web client does not support Winter evaluation. Internal reference: /about_scripting and the bundled Winter standard library.\"}";
		if(topic == "all") return "{\"topics\":[\"overview\",\"coding\",\"events\",\"objects\",\"materials\",\"avatars\",\"physics\",\"timers\",\"storage\",\"http\",\"audio\",\"animation\",\"particles\",\"limits\",\"examples\",\"xml\",\"winter\"],\"instruction\":\"Read overview and the specific API topics before editing. Luau guide: /about_luau_scripting; examples: /example_luau_scripts. XML and Winter are distinct formats.\"}";
		throw glare::Exception("Unknown script reference topic. Use overview, coding, events, objects, materials, avatars, physics, timers, storage, http, audio, animation, particles, limits, examples, xml, winter, or all.");
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
				world_state.lua_vms[ob->creator_id] = lua_vm;
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
