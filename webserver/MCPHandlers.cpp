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
#include "MCPModeling.h"
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
		if(topic == "overview") return "{\"language\":\"Luau\",\"marker\":\"--lua\",\"execution\":\"Scripts run on both client and server; object side effects vary.\",\"uid\":\"Use this_object.uid for this object's UID and getObjectForUID(uid) to find another object.\",\"documentation\":\"Internal source: /about_luau_scripting and /example_luau_scripts\",\"topics\":[\"events\",\"objects\",\"materials\",\"avatars\",\"timers\",\"storage\",\"http\",\"examples\"]}";
		if(topic == "events") return "{\"functions\":[\"onUserTouchedObject(avatar, object)\",\"onUserUsedObject(avatar, object)\",\"onUserMovedNearToObject(avatar, object)\",\"onUserMovedAwayFromObject(avatar, object)\",\"onUserEnteredParcel(avatar, object, parcel)\",\"onUserExitedParcel(avatar, object, parcel)\",\"onUserEnteredVehicle(avatar, vehicle_ob)\",\"onUserExitedVehicle(avatar, vehicle_ob)\"],\"other_objects\":\"addEventListener(event_name, ob_uid, handler) listens on another object's UID; use an existing handler function.\",\"rules\":\"Start source with --lua. The E key triggers onUserUsedObject. Touch events may repeat about every 0.5 seconds while contact continues. Near/far events use about 20 metres.\"}";
		if(topic == "objects") return "{\"globals\":[\"this_object:Object\",\"IS_CLIENT:boolean\",\"IS_SERVER:boolean\"],\"functions\":[\"getObjectForUID(uid):Object (throws if missing)\",\"showMessageToUser(msg, avatar)\"],\"object_attributes\":[\"uid\",\"model_url\",\"pos\",\"axis\",\"angle (radians)\",\"scale\",\"collidable\",\"dynamic\",\"sensor\",\"content\",\"video_autoplay\",\"video_loop\",\"video_muted\",\"mass\",\"friction\",\"restitution\",\"centre_of_mass_offset_os\",\"audio_source_url\",\"audio_volume\",\"getNumMaterials()\",\"getMaterial(index)\"],\"note\":\"Object attribute changes can have client-only or server-only immediate effects depending on the property. Use documented object properties; do not invent APIs.\"}";
		if(topic == "materials") return "{\"access\":\"Object:getNumMaterials() and Object:getMaterial(index)\",\"material_properties\":[\"colour\",\"colour_texture_url\",\"emission_rgb\",\"emission_texture_url\",\"normal_map_url\",\"roughness_val\",\"roughness_texture_url\",\"metallic_fraction_val\",\"opacity_val\",\"tex_matrix\",\"emission_lum_flux_or_lum\",\"hologram\",\"double_sided\"],\"note\":\"Use only documented Material fields. Material edits may be client- or server-side according to runtime behavior.\"}";
		if(topic == "avatars") return "{\"avatar_properties\":[\"pos\",\"name\",\"linear_velocity\",\"vehicle_inside\"],\"common_use\":\"Event handlers receive the Avatar userdata; pass it to showMessageToUser(msg, avatar).\"}";
		if(topic == "timers") return "{\"functions\":[\"createTimer(callback, interval_seconds, repeating):number\",\"destroyTimer(handle)\"],\"callback\":\"callback(object)\",\"limit\":\"At most 4 timers per script at a time. Destroy repeating timers when no longer needed.\"}";
		if(topic == "storage") return "{\"functions\":[\"objectstorage.getItem(key)\",\"objectstorage.setItem(key, serialisable_value)\"],\"scope\":\"Storage is persistent across server restarts and script reloads; keys are scoped to the script object's UID.\"}";
		if(topic == "http") return "{\"functions\":[\"doHTTPGetRequestAsync(url, headers, onDone, onError)\",\"doHTTPPostRequestAsync(url, body, content_type, headers, onDone, onError)\",\"parseJSON(json)\",\"getSecret(name)\"],\"limits\":\"HTTP requests are rate-limited to 5 per user per 300 seconds. Lua HTTP must be enabled by the server. getSecret reads account secrets; never hardcode API keys in object scripts.\",\"callbacks\":\"onDone receives response_code, response_message, mime_type, body_data; onError receives error_code and error_description.\"}";
		if(topic == "examples") return "{\"source\":\"Internal examples: /example_luau_scripts. Patterns include touch/used-object interactions, jump pads, object-to-object UID references, timers and persistent visitor counters. Read the relevant example before generating a similar script.\",\"minimal_use_example\":\"--lua\\nfunction onUserUsedObject(avatar, object)\\n  showMessageToUser('Hello ' .. avatar.name, avatar)\\nend\"}";
		if(topic == "all") return "{\"topics\":[\"overview\",\"events\",\"objects\",\"materials\",\"avatars\",\"timers\",\"storage\",\"http\",\"examples\"],\"instruction\":\"Call get_luau_reference again with each needed topic. Full internal reference: /about_luau_scripting; examples: /example_luau_scripts.\"}";
		throw glare::Exception("Unknown Luau reference topic. Use overview, events, objects, materials, avatars, timers, storage, http, examples, or all.");
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
		if(!script.empty() && !hasPrefix(script, "--lua")) throw glare::Exception("Luau object scripts must begin with --lua.");

		Reference<LuaScriptEvaluator> new_evaluator;
		bool server_execution_enabled = BitUtils::isBitSet(world_state.feature_flag_info.feature_flags, ServerAllWorldsState::SERVER_SCRIPT_EXEC_FEATURE_FLAG);
		if(!script.empty() && server_execution_enabled)
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
		return "{\"uid\":" + toString(ob->uid.value()) + ",\"saved\":true,\"bytes\":" + toString(script.size()) + ",\"server_execution_enabled\":" + std::string(server_execution_enabled ? "true" : "false") + ",\"server_evaluator_reloaded\":" + std::string(server_execution_enabled ? "true" : "false") + "}";
	}
	if(tool_name == "import_texture") return Modeling::importTexture(world_state,parser,args);
	if(tool_name == "create_image") return Modeling::imageObject(world_state,world,parser,args,user_id,username);
	if(tool_name == "apply_image_texture") return Modeling::applyImageTexture(world_state,world,parser,args,user_id,username);
	if(tool_name == "get_modeling_example")
		return Modeling::buildingExample(args.getChildStringValue(parser,"kind"));
	if(tool_name == "build_mesh")
		return Modeling::meshObject(world_state, world, parser, args, user_id, username);
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
