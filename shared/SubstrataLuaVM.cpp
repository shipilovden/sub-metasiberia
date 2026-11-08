/*=====================================================================
SubstrataLuaVM.cpp
------------------
Copyright Glare Technologies Limited 2024 -
=====================================================================*/
#include "SubstrataLuaVM.h"


#include "../shared/LuaScriptEvaluator.h"
#include "../shared/ObjectEventHandlers.h"
#include "WorldObject.h"
#include "ObjectCopyPermissions.h"
#include "MessageUtils.h"
#include "Protocol.h"
#if GUI_CLIENT
#include "../gui_client/PlayerPhysics.h"
#include "../gui_client/GUIClient.h"
#include "../gui_client/ParticleEmitterSettings.h"
#include "../gui_client/VehiclePhysics.h"
#elif SERVER
#include "../server/Server.h"
#include "../server/WorkerThread.h"
#include "../server/LuaHTTPRequestManager.h"
#endif
#include <lua/LuaVM.h>
#include <lua/LuaScript.h>
#include <lua/LuaUtils.h>
#include <lua/LuaSerialisation.h>
#include <utils/StringUtils.h>
#include <utils/RuntimeCheck.h>
#include <utils/JSONParser.h>
#include <lualib.h>
#include <Luau/Common.h>
#include <BufferViewInStream.h>
#include <algorithm>
#include <cctype>
#include <CryptoRNG.h>
#include <cmath>
#include <chrono>
#include <limits>
#include <set>
#include <tuple>
#include <vector>


// String atom tables.  Used for fast selection of table fields without having to do string comparisons.
struct StringAtom
{
	const char* str;
	int atom;
};

enum StringAtomEnum
{
	// Common
	Atom_uid,
	Atom_idx,

	// WorldObject
	Atom_model_url,
	Atom_pos,
	Atom_axis,
	Atom_angle,
	Atom_scale,
	Atom_collidable,
	Atom_dynamic,
	Atom_sensor,
	Atom_content,
	Atom_target_url,
	Atom_video_autoplay,
	Atom_video_loop,
	Atom_video_muted,
	Atom_mass,
	Atom_friction,
	Atom_restitution,
	Atom_centre_of_mass_offset_os,
	Atom_audio_source_url,
	Atom_audio_volume,
	Atom_getNumMaterials,
	Atom_getMaterial,
	Atom_script,
	Atom_materials,
	Atom_audio_loop,
	Atom_playAudio, // resets cur read index to start of audio, starts playing the audio (unpauses if paused)
	Atom_pauseAudio,
	Atom_resumeAudio,
	Atom_isPlayingAudio,
	Atom_startPlayingAnimation,
	Atom_stopPlayingAnimation,
	Atom_resumePlayingAnimation,
	Atom_getAnimationIndex,
	Atom_getAnimationCount,
	Atom_getAnimationName,
	Atom_getNodeCount,
	Atom_getNodeIndex,
	Atom_getNodeInfo,
	Atom_getBoneCount,
	Atom_getBoneInfo,
	Atom_setNodeTransform,
	Atom_clearNodeTransform,
	Atom_start,
	Atom_stop,
	Atom_burst,
	Atom_clearParticles,
	Atom_particleStart,
	Atom_particleStop,
	Atom_particleBurst,
	Atom_particleClear,

	// WorldMaterial
	Atom_colour,
	Atom_colour_texture_url,
	Atom_emission_rgb,
	Atom_emission_texture_url,
	Atom_normal_map_url,
	Atom_roughness_val,
	Atom_roughness_texture_url,
	Atom_metallic_fraction_val,
	Atom_opacity_val,
	Atom_tex_matrix,
	Atom_emission_lum_flux_or_lum,
	Atom_hologram,
	Atom_double_sided,

	// Avatar
	Atom_name,
	Atom_linear_velocity,
	Atom_vehicle_inside,

	// Events
	Atom_onUserUsedObject,
	Atom_onUserGameSessionChanged,
	Atom_onUserKeyPressed,
	Atom_onUserKeyReleased,
	Atom_onUserGameAction,
	Atom_onUserGameMessage,
	Atom_onGameStateChanged,
	Atom_onGameEvent,
	Atom_onUserTouchedObject,
	Atom_onUserMovedNearToObject,
	Atom_onUserMovedAwayFromObject,
	Atom_onUserEnteredParcel,
	Atom_onUserExitedParcel,
	Atom_onUserEnteredVehicle,
	Atom_onUserExitedVehicle,
	Atom_onChatMessage,
};

static StringAtom string_atoms[] = 
{
	// Common
	StringAtom({"uid",						Atom_uid						}),
	StringAtom({"idx",						Atom_idx						}),

	// WorldObject
	StringAtom({"model_url",				Atom_model_url					}),
	StringAtom({"pos",						Atom_pos						}),
	StringAtom({"axis",						Atom_axis						}),
	StringAtom({"angle",					Atom_angle						}),
	StringAtom({"scale",					Atom_scale						}),
	StringAtom({"collidable",				Atom_collidable					}),
	StringAtom({"dynamic",					Atom_dynamic,					}),
	StringAtom({"sensor",					Atom_sensor,					}),
	StringAtom({"content",					Atom_content,					}),
	StringAtom({"target_url",				Atom_target_url,				}),
	StringAtom({"video_autoplay",			Atom_video_autoplay,			}),
	StringAtom({"video_loop",				Atom_video_loop,				}),
	StringAtom({"video_muted",				Atom_video_muted,				}),
	StringAtom({"mass",						Atom_mass,						}),
	StringAtom({"friction",					Atom_friction,					}),
	StringAtom({"restitution",				Atom_restitution,				}),
	StringAtom({"centre_of_mass_offset_os",	Atom_centre_of_mass_offset_os,	}),
	StringAtom({"audio_source_url",			Atom_audio_source_url,			}),
	StringAtom({"audio_volume",				Atom_audio_volume,				}),
	StringAtom({"getNumMaterials",			Atom_getNumMaterials,			}),
	StringAtom({"getMaterial",				Atom_getMaterial,				}),
	StringAtom({"script",					Atom_script,					}),
	StringAtom({"materials",				Atom_materials,					}),
	StringAtom({"audio_loop",				Atom_audio_loop,				}),
	StringAtom({"playAudio",				Atom_playAudio,					}),
	StringAtom({"pauseAudio",               Atom_pauseAudio,                }),
	StringAtom({"resumeAudio",              Atom_resumeAudio,               }),
	StringAtom({"isPlayingAudio",			Atom_isPlayingAudio,			}),
	StringAtom({"startPlayingAnimation",	Atom_startPlayingAnimation,		}),
	StringAtom({"stopPlayingAnimation",	Atom_stopPlayingAnimation,		}),
	StringAtom({"resumePlayingAnimation",	Atom_resumePlayingAnimation,	}),
	StringAtom({"getAnimationIndex",		Atom_getAnimationIndex,			}),
	StringAtom({"getAnimationCount",		Atom_getAnimationCount,			}),
	StringAtom({"getAnimationName",		Atom_getAnimationName,			}),
	StringAtom({"getNodeCount",			Atom_getNodeCount,			}),
	StringAtom({"getNodeIndex",			Atom_getNodeIndex,			}),
	StringAtom({"getNodeInfo",			Atom_getNodeInfo,			}),
	StringAtom({"getBoneCount",			Atom_getBoneCount,			}),
	StringAtom({"getBoneInfo",			Atom_getBoneInfo,			}),
	StringAtom({"setNodeTransform",		Atom_setNodeTransform,			}),
	StringAtom({"clearNodeTransform",		Atom_clearNodeTransform,		}),
	StringAtom({"start",					Atom_start,						}),
	StringAtom({"stop",						Atom_stop,						}),
	StringAtom({"burst",					Atom_burst,						}),
	StringAtom({"clearParticles",			Atom_clearParticles,			}),
	StringAtom({"particleStart",			Atom_particleStart,				}),
	StringAtom({"particleStop",				Atom_particleStop,				}),
	StringAtom({"particleBurst",			Atom_particleBurst,				}),
	StringAtom({"particleClear",			Atom_particleClear,				}),

	// WorldMaterial
	StringAtom({"colour",					Atom_colour,					}),
	StringAtom({"colour_texture_url",		Atom_colour_texture_url,		}),
	StringAtom({"emission_rgb",				Atom_emission_rgb,				}),
	StringAtom({"emission_texture_url",		Atom_emission_texture_url,		}),
	StringAtom({"normal_map_url",			Atom_normal_map_url,			}),
	StringAtom({"roughness_val",			Atom_roughness_val,				}),
	StringAtom({"roughness_texture_url",	Atom_roughness_texture_url,		}),
	StringAtom({"metallic_fraction_val",	Atom_metallic_fraction_val,		}),
	StringAtom({"opacity_val",				Atom_opacity_val,				}),
	StringAtom({"tex_matrix",				Atom_tex_matrix,				}),
	StringAtom({"emission_lum_flux_or_lum",	Atom_emission_lum_flux_or_lum,	}),
	StringAtom({"hologram",					Atom_hologram,					}),
	StringAtom({"double_sided",				Atom_double_sided,				}),

	// Avatar
	StringAtom({"name",						Atom_name,						}),
	StringAtom({"linear_velocity",			Atom_linear_velocity,			}),
	StringAtom({"vehicle_inside",			Atom_vehicle_inside,			}),

	// Events:
	StringAtom({"onUserUsedObject",			Atom_onUserUsedObject,			}),
	StringAtom({"onUserGameSessionChanged", Atom_onUserGameSessionChanged, }),
	StringAtom({"onUserKeyPressed",			Atom_onUserKeyPressed,			}),
	StringAtom({"onUserKeyReleased",			Atom_onUserKeyReleased,			}),
	StringAtom({"onUserGameAction",			Atom_onUserGameAction,			}),
	StringAtom({"onUserGameMessage",			Atom_onUserGameMessage,			}),
	StringAtom({"onGameStateChanged",		Atom_onGameStateChanged,		}),
	StringAtom({"onGameEvent",				Atom_onGameEvent,				}),
	StringAtom({"onUserTouchedObject",		Atom_onUserTouchedObject,		}),
	StringAtom({"onUserMovedNearToObject",	Atom_onUserMovedNearToObject,	}),
	StringAtom({"onUserMovedAwayFromObject",Atom_onUserMovedAwayFromObject,	}),
	StringAtom({"onUserEnteredParcel",		Atom_onUserEnteredParcel,		}),
	StringAtom({"onUserExitedParcel",		Atom_onUserExitedParcel,		}),
	StringAtom({"onUserEnteredVehicle",		Atom_onUserEnteredVehicle,		}),
	StringAtom({"onUserExitedVehicle",		Atom_onUserExitedVehicle,		}),
	StringAtom({"onChatMessage",			Atom_onChatMessage,				}),
};


#if GUI_CLIENT
// Define this function to satisfy the Thread Safety Analysis.  Checks that we hold the world state lock.
// ASSERT_CAPABILITY means that after this function has finished executing, the calling thread is guaranteed to hold world_state->mutex.
static inline void checkHoldWorldStateMutex(LuaScriptEvaluator* script_evaluator, WorldState* world_state) ASSERT_CAPABILITY(world_state->mutex)
{
	// Don't bother checking the actual mutex addresses, just rely on the type system (WorldStateLock) and assume we don't mix up multiple WorldStateLocks, which should be unlikely.
	if(!(script_evaluator->cur_world_state_lock)) // && (&script_evaluator->cur_world_state_lock->getMutex() == &world_state->mutex)))
	{
		assert(0);
		throw glare::Exception("Internal error: didn't hold correct world state lock");
	}
}
#else // else if SERVER:
static inline void checkHoldWorldStateMutex(LuaScriptEvaluator* script_evaluator, ServerAllWorldsState* world_state) ASSERT_CAPABILITY(world_state->mutex)
{
	if(!(script_evaluator->cur_world_state_lock))
	{
		assert(0);
		throw glare::Exception("Internal error: didn't hold world state lock");
	}
}
#endif


// Construct a WorldMaterial from a table on the lua stack
#if 0
static WorldMaterialRef getTableWorldMaterial(lua_State* state, int table_index)
{
	WorldMaterialRef mat = new WorldMaterial();
	mat->colour_texture_url = LuaUtils::getTableStringField(state, table_index, "colour_texture_url");
	mat->roughness.val = (float)LuaUtils::getTableNumberFieldWithDefault(state, table_index, "roughness_val", 0.5);
	mat->tex_matrix = LuaUtils::getTableMatrix2fFieldWithDefault(state, table_index, "tex_matrix", Matrix2f::identity());

	return mat;
}
#endif


static std::string errorContextString(lua_State* state)
{
	return "\n" + LuaUtils::getCallStackAsString(state);
}


static void checkNumArgs(lua_State* state, int num_args_required)
{
	const int num_args = lua_gettop(state);
	if(num_args < num_args_required)
		throw glare::Exception("Expected " + toString(num_args_required) + " arg(s) to function, got " + toString(num_args) + "." + errorContextString(state));
}


#if 0 // GUI_CLIENT
static void enqueueMessageToSend(ClientThread& client_thread, SocketBufferOutStream& packet)
{
	MessageUtils::updatePacketLengthField(packet);

	client_thread.enqueueDataToSend(packet.buf);
}
#endif


#if 0 // TEMP DISABLED
static int createObject(lua_State* state)
{
	// Expected args:
	// Arg 1: ob_params : Table

	checkNumArgs(state, /*num_args_required*/1);
	//const int initial_stack_size = lua_gettop(state);

	if(!lua_istable(state, 1))
		throw glare::Exception("createObject(): arg 1 (ob_params) was not a table" + errorContextString(state));

	const int ob_params_table_index = 1;

	WorldObjectRef ob = new WorldObject();
	
	ob->model_url = LuaUtils::getTableStringFieldWithEmptyDefault(state, ob_params_table_index, "model_url"); // TODO: check URL length
	ob->pos = LuaUtils::getTableVec3dField(state, ob_params_table_index, "pos");
	ob->axis = LuaUtils::getTableVec3fFieldWithDefault(state, ob_params_table_index, "axis", Vec3f(1,0,0));
	ob->angle = (float)LuaUtils::getTableNumberFieldWithDefault(state, ob_params_table_index, "angle", 0.0);
	ob->scale = LuaUtils::getTableVec3fFieldWithDefault(state, ob_params_table_index, "scale", Vec3f(1,1,1));

	ob->setCollidable(LuaUtils::getTableBoolFieldWithDefault(state, ob_params_table_index, "collidable", /*default val=*/true));
	ob->setDynamic(LuaUtils::getTableBoolFieldWithDefault(state, ob_params_table_index, "dynamic", /*default val=*/true));
	
	ob->content = LuaUtils::getTableStringFieldWithEmptyDefault(state, ob_params_table_index, "content");
	ob->script = LuaUtils::getTableStringFieldWithEmptyDefault(state, ob_params_table_index, "script");

	// Materials

	const int value_type = lua_rawgetfield(state, ob_params_table_index, "materials"); // Push field value onto stack (use lua_rawgetfield to avoid metamethod call).  Returns the type of the pushed value.
	if(value_type == LUA_TTABLE)
	{
		const int max_num_mats = 100;
		for(int i=1; i<=max_num_mats; ++i)
		{
			runtimeCheck(lua_istable(state, -1)); // Materials table should still be on top of stack

			const int mat_type = lua_rawgeti(state, /*table index=*/-1, i);
			if(mat_type == LUA_TTABLE)
			{
				// Parse table
				WorldMaterialRef mat = getTableWorldMaterial(state, -1);
				ob->materials.push_back(mat);

				lua_pop(state, 1); // Pop material value
			}
			else
			{
				lua_pop(state, 1); // Pop null value
				break;
			}
		}

	}
	lua_pop(state, 1); // Pop materials value

	//assert(lua_gettop(state) == initial_stack_size); // Check stack is same size as at the start of the function

#if GUI_CLIENT
	SubstrataLuaVM* sub_lua_vm = (SubstrataLuaVM*)lua_callbacks(state)->userdata;

	// Send CreateObject message to server
	{
		MessageUtils::initPacket(sub_lua_vm->gui_client->scratch_packet, Protocol::CreateObject);
		ob->writeToNetworkStream(sub_lua_vm->gui_client->scratch_packet, sub_lua_vm->gui_client->server_protocol_version);

		enqueueMessageToSend(*sub_lua_vm->gui_client->client_thread, sub_lua_vm->gui_client->scratch_packet);
	}
#elif SERVER

	// Insert object into world state
	SubstrataLuaVM* sub_lua_vm = (SubstrataLuaVM*)lua_callbacks(state)->userdata;
	LuaScript* script = (LuaScript*)lua_getthreaddata(state);
	LuaScriptEvaluator* script_evaluator = (LuaScriptEvaluator*)script->userdata;

	{
		ob->uid = sub_lua_vm->server->world_state->getNextObjectUID();
		ob->state = WorldObject::State_JustCreated;
		ob->from_remote_other_dirty = true;
	
		//cur_world_state->addWorldObjectAsDBDirty(new_ob); // TEMP: don't add to DB

		script_evaluator->world_state->getDirtyFromRemoteObjects(*script_evaluator->cur_world_state_lock).insert(ob);
		script_evaluator->world_state->getObjects(*script_evaluator->cur_world_state_lock).insert(std::make_pair(ob->uid, ob));
	}

#endif

	return 0; // Count of returned values
}
#endif


// Find a WorldObject for the given UID.  Throws exception if no such object with UID found.
static WorldObject* getWorldObjectForUID(LuaScriptEvaluator* script_evaluator, const UID uid)
#if GUI_CLIENT
	
#endif
{
	// See if the UID is that of the script_evaluator object, in which case we can use the script_evaluator pointer and avoid the map lookup.
	if(uid == script_evaluator->world_object->uid)
		return script_evaluator->world_object;
	else
	{
#if GUI_CLIENT
		{
			SubstrataLuaVM* sub_lua_vm = script_evaluator->substrata_lua_vm.ptr(); //(SubstrataLuaVM*)lua_callbacks(state)->userdata;
			WorldState* world_state = sub_lua_vm->gui_client->world_state.ptr();

			checkHoldWorldStateMutex(script_evaluator, world_state);

			auto res = world_state->objects.find(uid);
			if(res == world_state->objects.end())
				throw glare::Exception("getObjectForUID(): No object with UID " + uid.toString());

			return res.getValue().ptr();
		}
#elif SERVER
		{
			if(script_evaluator->cur_world_state_lock == nullptr)
			{
				assert(0);
				throw glare::Exception("Internal error: cur_world_state_lock was null");
			}


			ServerWorldState::ObjectMapType& objects = script_evaluator->world_state->getObjects(*script_evaluator->cur_world_state_lock);

			auto res = objects.find(uid);
			if(res == objects.end())
				throw glare::Exception("getObjectForUID(): No object with UID " + uid.toString());

			return res->second.ptr();
		}
#endif
	}
}


static int luaGetWorldObjectForUID(lua_State* state)
{
	// Expected args:
	// Arg 1: uid : Number

	checkNumArgs(state, /*num_args_required*/1);

	const UID uid((uint64)LuaUtils::getDoubleArg(state, /*index=*/1));

	LuaScript* script = (LuaScript*)lua_getthreaddata(state); // NOTE: this double pointer-chasing sucks
	LuaScriptEvaluator* script_evaluator = (LuaScriptEvaluator*)script->userdata;

	/*WorldObject* ob =*/ getWorldObjectForUID(script_evaluator, uid); // Just call this to throw an excep if no such object exists

	script_evaluator->pushWorldObjectTableOntoStack(uid);
	return 1;
}


// Optional lookup for event-driven scripts where an object may be removed
// between events. Unlike getObjectForUID(), a missing UID returns nil rather
// than raising a runtime error and disabling the evaluator.
static int luaFindWorldObjectForUID(lua_State* state)
{
	checkNumArgs(state, /*num_args_required=*/1);
	const UID uid((uint64)LuaUtils::getDoubleArg(state, /*index=*/1));

	LuaScript* script = (LuaScript*)lua_getthreaddata(state);
	LuaScriptEvaluator* script_evaluator = (LuaScriptEvaluator*)script->userdata;
	bool found = false;
#if GUI_CLIENT
	SubstrataLuaVM* sub_lua_vm = script_evaluator->substrata_lua_vm.ptr();
	WorldState* world_state = sub_lua_vm->gui_client->world_state.ptr();
	checkHoldWorldStateMutex(script_evaluator, world_state);
	found = world_state->objects.contains(uid);
#elif SERVER
	if(script_evaluator->cur_world_state_lock == nullptr)
		throw glare::Exception("findObjectForUID(): current world state lock is missing" + errorContextString(state));
	found = script_evaluator->world_state->getObjects(*script_evaluator->cur_world_state_lock).count(uid) != 0;
#endif
	if(found)
		script_evaluator->pushWorldObjectTableOntoStack(uid);
	else
		lua_pushnil(state);
	return 1;
}


static int luaVec3fConstructor(lua_State* state)
{
	checkNumArgs(state, /*num_args_required=*/3);
	const float x = (float)LuaUtils::getDoubleArg(state, /*index=*/1);
	const float y = (float)LuaUtils::getDoubleArg(state, /*index=*/2);
	const float z = (float)LuaUtils::getDoubleArg(state, /*index=*/3);
	lua_pushvector(state, x, y, z);
	return 1;
}


static int getCurrentTime(lua_State* state)
{
	// Expected args:
	// none

	checkNumArgs(state, /*num_args_required*/0);

	SubstrataLuaVM* sub_lua_vm = (SubstrataLuaVM*)lua_callbacks(state)->userdata;
#if GUI_CLIENT
	lua_pushnumber(state, sub_lua_vm->gui_client->world_state->getCurrentGlobalTime()); // NOTE: do we want to use global time for this?
#elif SERVER
	lua_pushnumber(state, sub_lua_vm->server->getCurrentGlobalTime()); // NOTE: do we want to use global time for this?
#endif
	return 1;
}


static const int MAX_NUM_OB_EVENT_LISTENS = 100;

static HandlerList* getEventHandlerList(ObjectEventHandlers& handlers, int event_name_atom);


static int luaAddEventListener(lua_State* state)
{
	// Expected args:
	// Arg 1: event_name : String
	// Arg 2: ob_uid : UID
	// Arg 3: handler : Function

	checkNumArgs(state, /*num_args_required*/3);
	if(lua_type(state, /*index=*/3) != LUA_TFUNCTION)
		throw glare::Exception("addEventListener(): arg 3 must be a function" + errorContextString(state));

	int event_name_atom = -1;
	const char* event_name = LuaUtils::getStringAndAtom(state, /*index=*/1, event_name_atom);

	// Get object UID
	const UID ob_uid = UID((uint64)LuaUtils::getDoubleArg(state, /*index=*/2));

	LuaScript* script = (LuaScript*)lua_getthreaddata(state); // NOTE: this double pointer-chasing sucks
	LuaScriptEvaluator* script_evaluator = (LuaScriptEvaluator*)script->userdata;

	const void* handler_func_ptr = lua_topointer(state, /*index=*/3);

#if GUI_CLIENT
	SubstrataLuaVM* sub_lua_vm = script_evaluator->substrata_lua_vm.ptr();
	WorldState* world_state = sub_lua_vm->gui_client->world_state.ptr();
#endif

	
	// For the client, we may be trying to add an event listener for an object that has not been sent from the server and loaded yet.
	// In this case getWorldObjectForUID will throw an exception.
	// To handle it we will add the event handler to world_state->pending_event_handlers, so that the event handler can be assigned to the object later
	// when it is actually loaded.  See pending_event_handlers usage in GUIClient::timerEvent.
	WorldObject* ob = nullptr;
	Reference<ObjectEventHandlers> ob_event_handlers;

#if GUI_CLIENT
	try
	{
		ob = getWorldObjectForUID(script_evaluator, ob_uid); // Try and get object, but handle the exception if not found.
	}
	catch(glare::Exception&)
	{}

	if(ob)
	{
		// Object exists, so add event handler to it directly.
		ob_event_handlers = ob->getOrCreateEventHandlers();
	}
	else
	{
		// conPrint("================ luaAddEventListener(): Object " + ob_uid.toString() + " was null (not loaded yet), adding to pending_event_handlers instead.");
		if(world_state->pending_event_handlers[ob_uid].isNull())
			world_state->pending_event_handlers[ob_uid] = new ObjectEventHandlers();
		ob_event_handlers = world_state->pending_event_handlers[ob_uid];
	}
#else
	ob = getWorldObjectForUID(script_evaluator, ob_uid);
	ob_event_handlers = ob->getOrCreateEventHandlers();
#endif

	// ob may be null at this point
	runtimeCheck(ob_event_handlers.nonNull()); // ob_event_handlers should be non-null
	HandlerList* selected_handlers = getEventHandlerList(*ob_event_handlers, event_name_atom);
	if(!selected_handlers)
		throw glare::Exception("Unknown event '" + std::string(event_name) + "'" + errorContextString(state));
	bool already_registered = false;
	for(size_t i = 0; i < selected_handlers->handler_funcs.size(); ++i)
		if(selected_handlers->handler_funcs[i].script.ob == script_evaluator && selected_handlers->handler_funcs[i].function_ptr == handler_func_ptr)
			already_registered = true;
	if(!already_registered && script_evaluator->num_obs_event_listening >= MAX_NUM_OB_EVENT_LISTENS)
		throw glare::Exception("Script added too many event listeners, max is " + toString(MAX_NUM_OB_EVENT_LISTENS) + errorContextString(state));
	const int handler_func_ref = lua_ref(state, /*index=*/3); // Get a reference to the handler function


	HandlerFunc handler_func;
	handler_func.script = WeakReference<LuaScriptEvaluator>(script_evaluator);
	handler_func.handler_func_ref = handler_func_ref;
	handler_func.function_ptr = handler_func_ptr;

	[[maybe_unused]] bool added_spatial_event = false;
	bool added = false;
	switch(event_name_atom)
	{
	case Atom_onUserUsedObject:
		assert(stringEqual(event_name, "onUserUsedObject"));
		added = ob_event_handlers->onUserUsedObject_handlers.addHandler(handler_func);
		break;
	case Atom_onUserGameSessionChanged:
		assert(stringEqual(event_name, "onUserGameSessionChanged"));
		added = ob_event_handlers->onUserGameSessionChanged_handlers.addHandler(handler_func);
		break;
	case Atom_onUserKeyPressed:
		assert(stringEqual(event_name, "onUserKeyPressed"));
		added = ob_event_handlers->onUserKeyPressed_handlers.addHandler(handler_func);
		break;
	case Atom_onUserKeyReleased:
		assert(stringEqual(event_name, "onUserKeyReleased"));
		added = ob_event_handlers->onUserKeyReleased_handlers.addHandler(handler_func);
		break;
	case Atom_onUserGameAction:
		assert(stringEqual(event_name, "onUserGameAction"));
		added = ob_event_handlers->onUserGameAction_handlers.addHandler(handler_func);
		break;
	case Atom_onUserGameMessage:
		assert(stringEqual(event_name, "onUserGameMessage"));
		added = ob_event_handlers->onUserGameMessage_handlers.addHandler(handler_func);
		break;
	case Atom_onGameStateChanged:
		assert(stringEqual(event_name, "onGameStateChanged"));
		added = ob_event_handlers->onGameStateChanged_handlers.addHandler(handler_func);
		break;
	case Atom_onGameEvent:
		assert(stringEqual(event_name, "onGameEvent"));
		added = ob_event_handlers->onGameEvent_handlers.addHandler(handler_func);
		break;
	case Atom_onUserTouchedObject:
		assert(stringEqual(event_name, "onUserTouchedObject"));
		added = ob_event_handlers->onUserTouchedObject_handlers.addHandler(handler_func);
		break;
	case Atom_onUserMovedNearToObject:
		assert(stringEqual(event_name, "onUserMovedNearToObject"));
		added = ob_event_handlers->onUserMovedNearToObject_handlers.addHandler(handler_func);
		added_spatial_event = true;
		break;
	case Atom_onUserMovedAwayFromObject:
		assert(stringEqual(event_name, "onUserMovedAwayFromObject"));
		added = ob_event_handlers->onUserMovedAwayFromObject_handlers.addHandler(handler_func);
		added_spatial_event = true;
		break;
	case Atom_onUserEnteredParcel:
		assert(stringEqual(event_name, "onUserEnteredParcel"));
		added = ob_event_handlers->onUserEnteredParcel_handlers.addHandler(handler_func);
		added_spatial_event = true;
		break;
	case Atom_onUserExitedParcel:
		assert(stringEqual(event_name, "onUserExitedParcel"));
		added = ob_event_handlers->onUserExitedParcel_handlers.addHandler(handler_func);
		added_spatial_event = true;
		break;
	case Atom_onUserEnteredVehicle:
		assert(stringEqual(event_name, "onUserEnteredVehicle"));
		added = ob_event_handlers->onUserEnteredVehicle_handlers.addHandler(handler_func);
		break;
	case Atom_onUserExitedVehicle:
		assert(stringEqual(event_name, "onUserExitedVehicle"));
		added = ob_event_handlers->onUserExitedVehicle_handlers.addHandler(handler_func);
		break;
	case Atom_onChatMessage:
		assert(stringEqual(event_name, "onChatMessage"));
		added = ob_event_handlers->onChatMessage_handlers.addHandler(handler_func);
		break;
	default:
		throw glare::Exception("Unknown event '" + std::string(event_name) + "'" + errorContextString(state));
	}

	if(added)
	{
		script_evaluator->num_obs_event_listening++;
	}
	else
		lua_unref(state, handler_func_ref); // Duplicate registrations do not consume a slot or retain a Lua reference.

#if GUI_CLIENT
	if(added_spatial_event && ob)
		sub_lua_vm->gui_client->scripted_ob_proximity_checker.addObject(ob);
#endif

	return 0;
}


static HandlerList* getEventHandlerList(ObjectEventHandlers& handlers, int event_name_atom)
{
	switch(event_name_atom)
	{
	case Atom_onUserUsedObject: return &handlers.onUserUsedObject_handlers;
	case Atom_onUserGameSessionChanged: return &handlers.onUserGameSessionChanged_handlers;
	case Atom_onUserKeyPressed: return &handlers.onUserKeyPressed_handlers;
	case Atom_onUserKeyReleased: return &handlers.onUserKeyReleased_handlers;
	case Atom_onUserGameAction: return &handlers.onUserGameAction_handlers;
	case Atom_onUserGameMessage: return &handlers.onUserGameMessage_handlers;
	case Atom_onGameEvent: return &handlers.onGameEvent_handlers;
	case Atom_onUserTouchedObject: return &handlers.onUserTouchedObject_handlers;
	case Atom_onUserMovedNearToObject: return &handlers.onUserMovedNearToObject_handlers;
	case Atom_onUserMovedAwayFromObject: return &handlers.onUserMovedAwayFromObject_handlers;
	case Atom_onUserEnteredParcel: return &handlers.onUserEnteredParcel_handlers;
	case Atom_onUserExitedParcel: return &handlers.onUserExitedParcel_handlers;
	case Atom_onUserEnteredVehicle: return &handlers.onUserEnteredVehicle_handlers;
	case Atom_onUserExitedVehicle: return &handlers.onUserExitedVehicle_handlers;
	case Atom_onChatMessage: return &handlers.onChatMessage_handlers;
	default: return nullptr;
	}
}


static int luaRemoveEventListener(lua_State* state)
{
	checkNumArgs(state, /*num_args_required=*/3);
	int event_name_atom = -1;
	LuaUtils::getStringAndAtom(state, /*index=*/1, event_name_atom);
	const UID ob_uid((uint64)LuaUtils::getDoubleArg(state, /*index=*/2));
	if(lua_type(state, /*index=*/3) != LUA_TFUNCTION)
		throw glare::Exception("removeEventListener(): arg 3 must be a function" + errorContextString(state));
	const void* function_ptr = lua_topointer(state, /*index=*/3);

	LuaScript* script = (LuaScript*)lua_getthreaddata(state);
	LuaScriptEvaluator* script_evaluator = (LuaScriptEvaluator*)script->userdata;
	Reference<ObjectEventHandlers> event_handlers;
#if GUI_CLIENT
	SubstrataLuaVM* sub_lua_vm = script_evaluator->substrata_lua_vm.ptr();
	WorldState* world_state = sub_lua_vm->gui_client->world_state.ptr();
	checkHoldWorldStateMutex(script_evaluator, world_state);
	try
	{
		WorldObject* ob = getWorldObjectForUID(script_evaluator, ob_uid);
		event_handlers = ob->getOrCreateEventHandlers();
	}
	catch(glare::Exception&)
	{
		auto it = world_state->pending_event_handlers.find(ob_uid);
		if(it != world_state->pending_event_handlers.end())
			event_handlers = it->second;
	}
#elif SERVER
	if(script_evaluator->cur_world_state_lock == nullptr)
		throw glare::Exception("removeEventListener(): current world state lock is missing" + errorContextString(state));
	try
	{
		WorldObject* ob = getWorldObjectForUID(script_evaluator, ob_uid);
		event_handlers = ob->getOrCreateEventHandlers();
	}
	catch(glare::Exception&)
	{}
#endif

	bool removed = false;
	if(event_handlers.nonNull())
	{
		HandlerList* handlers = getEventHandlerList(*event_handlers, event_name_atom);
		if(!handlers)
			throw glare::Exception("removeEventListener(): unknown event name" + errorContextString(state));
		for(size_t i = 0; i < handlers->handler_funcs.size(); ++i)
		{
			const HandlerFunc handler = handlers->handler_funcs[i];
			if(handler.script.ob == script_evaluator && handler.function_ptr == function_ptr)
			{
				handlers->removeHandlerAtIndex(i);
				lua_unref(state, handler.handler_func_ref);
				--script_evaluator->num_obs_event_listening;
				removed = true;
				break;
			}
		}
	}

#if GUI_CLIENT
	if(removed && event_handlers.nonNull())
	{
		WorldObject* ob = nullptr;
		try { ob = getWorldObjectForUID(script_evaluator, ob_uid); } catch(glare::Exception&) {}
		if(ob && event_handlers->onUserMovedNearToObject_handlers.handler_funcs.empty() &&
			event_handlers->onUserMovedAwayFromObject_handlers.handler_funcs.empty() &&
			event_handlers->onUserEnteredParcel_handlers.handler_funcs.empty() &&
			event_handlers->onUserExitedParcel_handlers.handler_funcs.empty())
			sub_lua_vm->gui_client->scripted_ob_proximity_checker.removeObject(ob);
	}
#endif
	lua_pushboolean(state, removed ? 1 : 0);
	return 1;
}


static int showMessageToUser(lua_State* state)
{
	// Expected args:
	// Arg 1: msg : String
	// Arg 2: av : Avatar

	checkNumArgs(state, /*num_args_required*/2);

	const std::string msg = LuaUtils::getString(state, /*index=*/1);
	if(msg.size() > 1024)
		throw glare::Exception("showMessageToUser(): message must be at most 1024 bytes" + errorContextString(state));
	const UID av_uid = UID((uint64)LuaUtils::getTableNumberField(state, /*table index=*/2, "uid"));

#if GUI_CLIENT
	SubstrataLuaVM* sub_lua_vm = (SubstrataLuaVM*)lua_callbacks(state)->userdata;
	if(av_uid == sub_lua_vm->gui_client->client_avatar_uid)
	{
		// We want to send to ourselves
		sub_lua_vm->gui_client->showScriptMessage(msg);
	}
#elif SERVER
	SubstrataLuaVM* sub_lua_vm = (SubstrataLuaVM*)lua_callbacks(state)->userdata;
	LuaScript* script = (LuaScript*)lua_getthreaddata(state);
	LuaScriptEvaluator* script_evaluator = script ? (LuaScriptEvaluator*)script->userdata : nullptr;
	if(script_evaluator && script_evaluator->world_state && sub_lua_vm->server)
		sub_lua_vm->server->enqueueScriptMessageForAvatar(av_uid, msg, script_evaluator->world_state);
#endif

	return 0;
}


static int gameSetHUDText(lua_State* state)
{
	if(lua_gettop(state) > 3)
		throw glare::Exception("game.setHUDText(): expected text, avatar and optional layout {x=?, y=?}" + errorContextString(state));
	checkNumArgs(state, /*num_args_required=*/2);
	const std::string text = LuaUtils::getString(state, /*index=*/1);
	if(text.empty() || text.size() > 2048)
		throw glare::Exception("gameSetHUDText(): text must contain 1 to 2048 bytes" + errorContextString(state));
	const UID avatar_uid((uint64)LuaUtils::getTableNumberField(state, /*table index=*/2, "uid"));
	bool has_layout = false;
	float center_x = 0.f;
	float top_y = 0.55f;
	if(lua_gettop(state) >= 3 && !lua_isnil(state, 3))
	{
		if(!lua_istable(state, 3))
			throw glare::Exception("game.setHUDText(): layout must be {x=?, y=?}" + errorContextString(state));
		lua_getfield(state, 3, "x");
		if(!lua_isnumber(state, -1))
		{
			lua_pop(state, 1);
			throw glare::Exception("game.setHUDText(): layout.x must be a number" + errorContextString(state));
		}
		center_x = (float)lua_tonumber(state, -1);
		lua_pop(state, 1);
		lua_getfield(state, 3, "y");
		if(!lua_isnumber(state, -1))
		{
			lua_pop(state, 1);
			throw glare::Exception("game.setHUDText(): layout.y must be a number" + errorContextString(state));
		}
		top_y = (float)lua_tonumber(state, -1);
		lua_pop(state, 1);
		if(!isFinite(center_x) || center_x < -1.f || center_x > 1.f || !isFinite(top_y) || top_y < -1.f || top_y > 1.f)
			throw glare::Exception("game.setHUDText(): layout x/y must be finite normalized screen coordinates within -1..1" + errorContextString(state));
		has_layout = true;
	}
	bool queued = false;
#if SERVER
	SubstrataLuaVM* sub_lua_vm = (SubstrataLuaVM*)lua_callbacks(state)->userdata;
	LuaScript* script = (LuaScript*)lua_getthreaddata(state);
	LuaScriptEvaluator* script_evaluator = script ? (LuaScriptEvaluator*)script->userdata : nullptr;
	if(script_evaluator && script_evaluator->world_object && script_evaluator->world_state && sub_lua_vm->server)
	{
		sub_lua_vm->server->enqueueGameHUDForAvatar(avatar_uid, script_evaluator->world_object->uid,
			/*visible=*/true, text, script_evaluator->world_state, has_layout, center_x, top_y);
		queued = true;
	}
#endif
	lua_pushboolean(state, queued ? 1 : 0);
	return 1;
}

// Configure the server-owned interaction affordance for a world object.
// setInteractionPrompt(uid, text, "e"|"mouse_left"|"both"|key|key+mouse_left, visible?)
static int gameSetInteractionPrompt(lua_State* state)
{
	checkNumArgs(state, 3);
	const double raw_uid = lua_tonumber(state, 1);
	if(!std::isfinite(raw_uid) || raw_uid <= 0 || raw_uid > 9007199254740991.0 || std::floor(raw_uid) != raw_uid)
		throw glare::Exception("setInteractionPrompt(): uid must be a positive integer" + errorContextString(state));
	const std::string text = LuaUtils::getStringArg(state, 2);
	if(text.size() > 128) throw glare::Exception("setInteractionPrompt(): text must be at most 128 bytes" + errorContextString(state));
	std::string input = LuaUtils::getStringArg(state, 3);
	for(char& c : input) c = (char)std::tolower((unsigned char)c);
	uint32 input_mode = 0;
	uint32 key_code = 0;
	if(input == "e") { input_mode = 0; key_code = Protocol::ScriptKey_CharacterBase + 'e'; }
	else if(input == "mouse_left") input_mode = 1;
	else if(input == "both") { input_mode = 2; key_code = Protocol::ScriptKey_CharacterBase + 'e'; }
	else
	{
		const std::string mouse_suffix = "+mouse_left";
		if(input.size() > mouse_suffix.size() && input.compare(input.size() - mouse_suffix.size(), mouse_suffix.size(), mouse_suffix) == 0)
		{
			input_mode = 2;
			input.resize(input.size() - mouse_suffix.size());
		}
		if(input == "w") key_code = Protocol::ScriptKey_W;
		else if(input == "a") key_code = Protocol::ScriptKey_A;
		else if(input == "s") key_code = Protocol::ScriptKey_S;
		else if(input == "d") key_code = Protocol::ScriptKey_D;
		else if(input.size() == 2 && input[0] == 'f' && input[1] >= '1' && input[1] <= '9')
			key_code = Protocol::ScriptKey_FunctionBase + (uint32)(input[1] - '0');
		else if(input.size() == 3 && input[0] == 'f' && input[1] == '1' && input[2] >= '0' && input[2] <= '2')
			key_code = Protocol::ScriptKey_FunctionBase + (uint32)(10 + input[2] - '0');
		else if(input.size() == 1 && ((input[0] >= 'a' && input[0] <= 'z') || (input[0] >= '0' && input[0] <= '9')))
			key_code = Protocol::ScriptKey_CharacterBase + (uint32)input[0];
		else if(input == "space") key_code = Protocol::ScriptKey_Space;
		else if(input == "enter") key_code = Protocol::ScriptKey_Enter;
		else if(input == "escape") key_code = Protocol::ScriptKey_Escape;
		else if(input == "backspace") key_code = Protocol::ScriptKey_Backspace;
		else if(input == "delete") key_code = Protocol::ScriptKey_Delete;
		else if(input == "pageup") key_code = Protocol::ScriptKey_PageUp;
		else if(input == "pagedown") key_code = Protocol::ScriptKey_PageDown;
		else if(input == "home") key_code = Protocol::ScriptKey_Home;
		else if(input == "end") key_code = Protocol::ScriptKey_End;
		else if(input == "minus") key_code = Protocol::ScriptKey_Minus;
		else if(input == "equals") key_code = Protocol::ScriptKey_Equals;
		else if(input == "plus") key_code = Protocol::ScriptKey_Plus;
		else if(input == "left_bracket") key_code = Protocol::ScriptKey_LeftBracket;
		else if(input == "right_bracket") key_code = Protocol::ScriptKey_RightBracket;
		else if(input == "left") key_code = Protocol::ScriptKey_Left;
		else if(input == "right") key_code = Protocol::ScriptKey_Right;
		else if(input == "up") key_code = Protocol::ScriptKey_Up;
		else if(input == "down") key_code = Protocol::ScriptKey_Down;
		else throw glare::Exception("setInteractionPrompt(): input must be e, mouse_left, both, a supported key name, or key+mouse_left" + errorContextString(state));
	}
	bool queued = false;
#if SERVER
	ScriptInteractionPrompt prompt;
	prompt.input_mode = input_mode;
	prompt.key_code = key_code;
	prompt.visible = lua_gettop(state) < 4 || lua_toboolean(state, 4) != 0;
	prompt.text = text;
	SubstrataLuaVM* sub_lua_vm = (SubstrataLuaVM*)lua_callbacks(state)->userdata;
	LuaScript* script = (LuaScript*)lua_getthreaddata(state);
	LuaScriptEvaluator* evaluator = script ? (LuaScriptEvaluator*)script->userdata : nullptr;
	if(evaluator && evaluator->world_state && sub_lua_vm->server)
	{
		const UID uid((uint64)raw_uid);
		sub_lua_vm->server->enqueueScriptInteractionPromptUpdate(uid, prompt, evaluator->world_state);
		queued = true;
	}
#endif
	lua_pushboolean(state, queued ? 1 : 0);
	return 1;
}

// Cryptographically strong server-side bounded randomness for dice, shuffled
// decks, loot tables and other competitive game rules.
static int gameRandomInt(lua_State* state)
{
	checkNumArgs(state, 2);
	const double lower = lua_tonumber(state, 1), upper = lua_tonumber(state, 2);
	if(!std::isfinite(lower) || !std::isfinite(upper) || std::floor(lower) != lower || std::floor(upper) != upper ||
		lower < -1000000 || upper > 1000000 || upper < lower || upper - lower >= 1000000)
		throw glare::Exception("game.randomInt(): expected integer bounds with lower <= upper and a range of at most 1,000,000 values" + errorContextString(state));
#if SERVER
	const uint32 span = (uint32)(upper - lower + 1.0);
	const uint32 threshold = (uint32(0) - span) % span;
	uint32 sample = 0;
	do { CryptoRNG::getRandomBytes((uint8*)&sample, sizeof(sample)); } while(sample < threshold);
	lua_pushnumber(state, lower + (sample % span));
	return 1;
#else
	lua_pushnil(state);
	LuaUtils::pushString(state, "game.randomInt() is available only on the authoritative server.");
	return 2;
#endif
}


static int gameClearHUD(lua_State* state)
{
	checkNumArgs(state, /*num_args_required=*/1);
	const UID avatar_uid((uint64)LuaUtils::getTableNumberField(state, /*table index=*/1, "uid"));
	bool queued = false;
#if SERVER
	SubstrataLuaVM* sub_lua_vm = (SubstrataLuaVM*)lua_callbacks(state)->userdata;
	LuaScript* script = (LuaScript*)lua_getthreaddata(state);
	LuaScriptEvaluator* script_evaluator = script ? (LuaScriptEvaluator*)script->userdata : nullptr;
	if(script_evaluator && script_evaluator->world_object && script_evaluator->world_state && sub_lua_vm->server)
	{
		sub_lua_vm->server->enqueueGameHUDForAvatar(avatar_uid, script_evaluator->world_object->uid,
			/*visible=*/false, std::string(), script_evaluator->world_state);
		queued = true;
	}
#endif
	lua_pushboolean(state, queued ? 1 : 0);
	return 1;
}


static bool isValidGameHUDActionID(const std::string& id)
{
	if(id.empty() || id.size() > 32)
		return false;
	for(const unsigned char c : id)
		if(!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
			(c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.'))
			return false;
	return true;
}


static bool isValidGameHUDPanelID(const std::string& id)
{
	if(id.empty() || id.size() > 24) return false;
	for(const unsigned char c : id)
		if(!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
			(c >= '0' && c <= '9') || c == '_' || c == '-'))
			return false;
	return true;
}


static int gameSetHUDMenu(lua_State* state)
{
	if(lua_gettop(state) > 8)
		throw glare::Exception("game.setHUDMenu(): expected avatar, title, actions, optional body, layout, fields, progress bars, and positioned text widgets" + errorContextString(state));
	checkNumArgs(state, /*num_args_required=*/3);
	const UID avatar_uid((uint64)LuaUtils::getTableNumberField(state, /*table index=*/1, "uid"));
	const std::string title = LuaUtils::getStringArg(state, /*index=*/2);
	if(title.size() > 128)
		throw glare::Exception("game.setHUDMenu(): title must be at most 128 bytes" + errorContextString(state));
	const std::string body = lua_gettop(state) >= 4 && !lua_isnil(state, 4) ? LuaUtils::getStringArg(state, /*index=*/4) : std::string();
	if(body.size() > 2048)
		throw glare::Exception("game.setHUDMenu(): body must be at most 2048 bytes" + errorContextString(state));
	if(!lua_istable(state, 3))
		throw glare::Exception("game.setHUDMenu(): arg 3 must be an array of action tables" + errorContextString(state));
	double menu_center_x = 0.0;
	double menu_top_y = 0.48;
	double menu_width = 0.8;
	std::string panel_id = "main";
	if(lua_gettop(state) >= 5 && !lua_isnil(state, 5))
	{
		if(!lua_istable(state, 5))
			throw glare::Exception("game.setHUDMenu(): arg 5 must be a layout table {id=?, x=?, y=?, width=?}" + errorContextString(state));
		auto read_layout_number = [state](const char* key, double fallback) -> double {
			lua_getfield(state, 5, key);
			if(lua_isnil(state, -1))
			{
				lua_pop(state, 1);
				return fallback;
			}
			if(!lua_isnumber(state, -1))
			{
				lua_pop(state, 1);
				throw glare::Exception(std::string("game.setHUDMenu(): layout field '") + key + "' must be a number" + errorContextString(state));
			}
			const double value = lua_tonumber(state, -1);
			lua_pop(state, 1);
			return value;
		};
		menu_center_x = read_layout_number("x", menu_center_x);
		menu_top_y = read_layout_number("y", menu_top_y);
		menu_width = read_layout_number("width", menu_width);
		lua_getfield(state, 5, "id");
		if(!lua_isnil(state, -1)) panel_id = LuaUtils::getStringArg(state, -1);
		lua_pop(state, 1);
		if(!isValidGameHUDPanelID(panel_id))
			throw glare::Exception("game.setHUDMenu(): layout id must be 1-24 ASCII letters, digits, '_' or '-'" + errorContextString(state));
		if(!isFinite(menu_center_x) || menu_center_x < -1.0 || menu_center_x > 1.0 ||
			!isFinite(menu_top_y) || menu_top_y < -1.0 || menu_top_y > 1.0 ||
			!isFinite(menu_width) || menu_width < 0.2 || menu_width > 1.0)
			throw glare::Exception("game.setHUDMenu(): layout x/y must be within -1..1 and width within 0.2..1" + errorContextString(state));
	}

#if SERVER
	GameHUDMenu menu;
	menu.panel_id = panel_id;
	menu.title = title;
	menu.body = body;
	menu.center_x = (float)menu_center_x;
	menu.top_y = (float)menu_top_y;
	menu.width = (float)menu_width;
#endif
	std::vector<std::string> action_ids;
	bool found_array_end = false;
	for(int i = 1; i <= 9; ++i)
	{
		lua_rawgeti(state, /*table index=*/3, i);
		if(lua_isnil(state, -1))
		{
			lua_pop(state, 1);
			found_array_end = true;
			continue;
		}
		if(found_array_end)
		{
			lua_pop(state, 1);
			throw glare::Exception("game.setHUDMenu(): actions must be a dense array" + errorContextString(state));
		}
		if(i > 8)
		{
			lua_pop(state, 1);
			throw glare::Exception("game.setHUDMenu(): at most 8 actions are allowed" + errorContextString(state));
		}
		if(!lua_istable(state, -1))
		{
			lua_pop(state, 1);
			throw glare::Exception("game.setHUDMenu(): each action must be a table" + errorContextString(state));
		}

		lua_getfield(state, -1, "id");
		const std::string id = LuaUtils::getStringArg(state, /*index=*/-1);
		lua_pop(state, 1);
		lua_getfield(state, -1, "label");
		const std::string label = LuaUtils::getStringArg(state, /*index=*/-1);
		lua_pop(state, 1); // label
		bool action_has_layout = false;
		float action_center_x = 0.f, action_top_y = 0.f, action_layout_width = 0.f, action_layout_height = 0.f;
		lua_getfield(state, -1, "layout");
		if(!lua_isnil(state, -1))
		{
			if(!lua_istable(state, -1))
			{
				lua_pop(state, 2);
				throw glare::Exception("game.setHUDMenu(): action layout must be {x=?, y=?, width=?, height=?}" + errorContextString(state));
			}
			auto read_action_layout_number = [state](const char* key) -> double {
				lua_getfield(state, -1, key);
				if(!lua_isnumber(state, -1))
				{
					lua_pop(state, 1);
					throw glare::Exception(std::string("game.setHUDMenu(): action layout field '") + key + "' must be a number" + errorContextString(state));
				}
				const double value = lua_tonumber(state, -1);
				lua_pop(state, 1);
				return value;
			};
			const double action_x = read_action_layout_number("x");
			const double action_y = read_action_layout_number("y");
			const double action_width = read_action_layout_number("width");
			const double action_height = read_action_layout_number("height");
			if(!isFinite(action_x) || action_x < -1.0 || action_x > 1.0 ||
				!isFinite(action_y) || action_y < -1.0 || action_y > 1.0 ||
				!isFinite(action_width) || action_width < 0.05 || action_width > 1.0 ||
				!isFinite(action_height) || action_height < 0.025 || action_height > 0.5 ||
				action_x - action_width * 0.5 < -1.0 || action_x + action_width * 0.5 > 1.0 ||
				action_y - action_height < -1.0)
			{
				lua_pop(state, 2);
				throw glare::Exception("game.setHUDMenu(): action layout must fit the screen; x/y are normalized centers/top positions, width is 0.05..1 and height is 0.025..0.5" + errorContextString(state));
			}
			action_has_layout = true;
			action_center_x = (float)action_x;
			action_top_y = (float)action_y;
			action_layout_width = (float)action_width;
			action_layout_height = (float)action_height;
		}
		lua_pop(state, 2); // optional layout and action table

		if(!isValidGameHUDActionID(id) || label.empty() || label.size() > 64)
			throw glare::Exception("game.setHUDMenu(): action id must be 1-32 ASCII identifier characters and label 1-64 bytes" + errorContextString(state));
		for(const std::string& existing_id : action_ids)
			if(existing_id == id)
				throw glare::Exception("game.setHUDMenu(): action ids must be unique" + errorContextString(state));
		action_ids.push_back(id);
#if SERVER
		GameHUDMenuAction action;
		action.id = id;
		action.label = label;
		action.has_layout = action_has_layout;
		action.center_x = action_center_x;
		action.top_y = action_top_y;
		action.width = action_layout_width;
		action.height = action_layout_height;
		menu.actions.push_back(action);
#endif
	}
	size_t form_field_count = 0;
	if(lua_gettop(state) >= 6 && !lua_isnil(state, 6))
	{
		if(!lua_istable(state, 6)) throw glare::Exception("game.setHUDMenu(): arg 6 must be an array of field tables" + errorContextString(state));
		bool found_end = false;
		std::set<std::string> field_ids;
		for(int i = 1; i <= 4; ++i)
		{
			lua_rawgeti(state, 6, i);
			if(lua_isnil(state, -1)) { lua_pop(state, 1); found_end = true; continue; }
			if(found_end) { lua_pop(state, 1); throw glare::Exception("game.setHUDMenu(): fields must be a dense array" + errorContextString(state)); }
			if(!lua_istable(state, -1)) { lua_pop(state, 1); throw glare::Exception("game.setHUDMenu(): each field must be a table" + errorContextString(state)); }
			lua_getfield(state, -1, "id");
			const std::string id = LuaUtils::getStringArg(state, -1);
			lua_pop(state, 1);
			lua_getfield(state, -1, "label");
			const std::string label = LuaUtils::getStringArg(state, -1);
			lua_pop(state, 1);
			lua_getfield(state, -1, "required");
			if(!lua_isnil(state, -1) && !lua_isboolean(state, -1))
			{
				lua_pop(state, 2);
				throw glare::Exception("game.setHUDMenu(): field 'required' must be a boolean" + errorContextString(state));
			}
			const bool required = !lua_isnil(state, -1) && lua_toboolean(state, -1) != 0;
			lua_pop(state, 1);
			lua_getfield(state, -1, "type");
			const std::string field_type = lua_isnil(state, -1) ? "text" : LuaUtils::getStringArg(state, -1);
			lua_pop(state, 1);
			if(field_type != "text" && field_type != "select" && field_type != "number" && field_type != "checkbox")
			{
				lua_pop(state, 1);
				throw glare::Exception("game.setHUDMenu(): field type must be 'text', 'select', 'number', or 'checkbox'" + errorContextString(state));
			}
		bool checkbox_checked = false;
		if(field_type == "checkbox")
		{
			lua_getfield(state, -1, "checked");
			if(!lua_isnil(state, -1) && lua_type(state, -1) != LUA_TBOOLEAN)
			{
				lua_pop(state, 2);
				throw glare::Exception("game.setHUDMenu(): checkbox field 'checked' must be a boolean" + errorContextString(state));
			}
			checkbox_checked = !lua_isnil(state, -1) && lua_toboolean(state, -1) != 0;
			lua_pop(state, 1);
		}
		double numeric_minimum = 0.0, numeric_maximum = 0.0;
		if(field_type == "number")
		{
			lua_getfield(state, -1, "min");
			const bool min_is_number = lua_isnumber(state, -1) != 0;
			numeric_minimum = min_is_number ? lua_tonumber(state, -1) : 0.0;
			lua_pop(state, 1);
			lua_getfield(state, -1, "max");
			const bool max_is_number = lua_isnumber(state, -1) != 0;
			numeric_maximum = max_is_number ? lua_tonumber(state, -1) : 0.0;
			lua_pop(state, 1);
			if(!min_is_number || !max_is_number || !isFinite(numeric_minimum) || !isFinite(numeric_maximum) ||
				numeric_minimum < -1.0e9 || numeric_maximum > 1.0e9 || numeric_minimum >= numeric_maximum)
			{
				lua_pop(state, 1);
				throw glare::Exception("game.setHUDMenu(): number fields require finite min/max bounds within +/-1,000,000,000 with min < max" + errorContextString(state));
			}
		}
			std::vector<std::string> options;
			if(field_type == "select")
			{
				lua_getfield(state, -1, "options");
				if(!lua_istable(state, -1))
				{
					lua_pop(state, 2);
					throw glare::Exception("game.setHUDMenu(): select fields require an options array" + errorContextString(state));
				}
				bool option_array_end = false;
				std::set<std::string> unique_options;
				for(int option_i = 1; option_i <= 9; ++option_i)
				{
					lua_rawgeti(state, -1, option_i);
					if(lua_isnil(state, -1)) { lua_pop(state, 1); option_array_end = true; continue; }
					if(option_array_end || option_i > 8 || lua_type(state, -1) != LUA_TSTRING)
					{
						lua_pop(state, 3);
						throw glare::Exception("game.setHUDMenu(): select options must be a dense array of at most 8 strings" + errorContextString(state));
					}
					const std::string option = LuaUtils::getStringArg(state, -1);
					lua_pop(state, 1);
					if(option.empty() || option.size() > 64 || !unique_options.insert(option).second)
					{
						lua_pop(state, 2);
						throw glare::Exception("game.setHUDMenu(): each select option must be unique and 1-64 bytes" + errorContextString(state));
					}
					options.push_back(option);
				}
				lua_pushnil(state);
				while(lua_next(state, -2) != 0)
				{
					const bool valid_key = lua_type(state, -2) == LUA_TNUMBER &&
						std::floor(lua_tonumber(state, -2)) == lua_tonumber(state, -2) &&
						lua_tonumber(state, -2) >= 1 && lua_tonumber(state, -2) <= 8;
					lua_pop(state, 1); // value, leaving key for lua_next
					if(!valid_key)
					{
						lua_pop(state, 2); // key and options table
						throw glare::Exception("game.setHUDMenu(): select options must be a dense array of at most 8 values" + errorContextString(state));
					}
				}
				lua_pop(state, 1); // options array
				if(options.size() < 2)
				{
					lua_pop(state, 1); // field table
					throw glare::Exception("game.setHUDMenu(): select fields require 2-8 options" + errorContextString(state));
				}
			}
			lua_pop(state, 1); // field table
			if(!isValidGameHUDActionID(id) || label.empty() || label.size() > 64 || !field_ids.insert(id).second)
				throw glare::Exception("game.setHUDMenu(): field id must be unique, label 1-64 bytes, and id a valid identifier" + errorContextString(state));
#if SERVER
			GameHUDFormField field;
			field.id = id;
			field.label = label;
			field.type = field_type == "select" ? GameHUDFormField::Select : field_type == "number" ? GameHUDFormField::Number :
				field_type == "checkbox" ? GameHUDFormField::Checkbox : GameHUDFormField::Text;
			field.options = options;
			field.required = required;
			field.minimum = numeric_minimum;
			field.maximum = numeric_maximum;
			field.checked = checkbox_checked;
			menu.fields.push_back(field);
#endif
			++form_field_count;
		}
		lua_pushnil(state);
		while(lua_next(state, 6) != 0)
		{
			const bool valid_key = lua_type(state, -2) == LUA_TNUMBER &&
				std::floor(lua_tonumber(state, -2)) == lua_tonumber(state, -2) &&
				lua_tonumber(state, -2) >= 1 && lua_tonumber(state, -2) <= 4;
			lua_pop(state, 1); // value, leaving the key for lua_next
			if(!valid_key)
			{
				lua_pop(state, 1);
				throw glare::Exception("game.setHUDMenu(): fields must be a dense array of at most 4 values" + errorContextString(state));
			}
		}
	}
	size_t progress_bar_count = 0;
	if(lua_gettop(state) >= 7 && !lua_isnil(state, 7))
	{
		if(!lua_istable(state, 7)) throw glare::Exception("game.setHUDMenu(): arg 7 must be an array of progress-bar tables" + errorContextString(state));
		bool found_end = false;
		std::set<std::string> progress_ids;
		for(int i = 1; i <= 5; ++i)
		{
			lua_rawgeti(state, 7, i);
			if(lua_isnil(state, -1)) { lua_pop(state, 1); found_end = true; continue; }
			if(found_end) { lua_pop(state, 1); throw glare::Exception("game.setHUDMenu(): progress bars must be a dense array" + errorContextString(state)); }
			if(i > 4 || !lua_istable(state, -1)) { lua_pop(state, 1); throw glare::Exception("game.setHUDMenu(): progress bars must be an array of at most 4 tables" + errorContextString(state)); }
			lua_getfield(state, -1, "id");
			const std::string id = LuaUtils::getStringArg(state, -1);
			lua_pop(state, 1);
			lua_getfield(state, -1, "label");
			const std::string label = LuaUtils::getStringArg(state, -1);
			lua_pop(state, 1);
			lua_getfield(state, -1, "value");
			const bool value_is_number = lua_isnumber(state, -1) != 0;
			const double value = value_is_number ? lua_tonumber(state, -1) : 0.0;
			lua_pop(state, 1);
			lua_getfield(state, -1, "max");
			const bool max_is_number = lua_isnumber(state, -1) != 0;
			const double maximum = max_is_number ? lua_tonumber(state, -1) : 0.0;
			lua_pop(state, 1);
			lua_pop(state, 1); // progress table
			if(!isValidGameHUDActionID(id) || label.empty() || label.size() > 64 || !progress_ids.insert(id).second ||
				!value_is_number || !max_is_number || !isFinite(value) || !isFinite(maximum) || value < 0.0 || maximum <= 0.0 || maximum > 1.0e9 || value > maximum)
				throw glare::Exception("game.setHUDMenu(): each progress bar needs a unique id, a 1-64 byte label, and finite 0 <= value <= max <= 1,000,000,000" + errorContextString(state));
			++progress_bar_count;
#if SERVER
			GameHUDProgressBar progress;
			progress.id = id;
			progress.label = label;
			progress.value = (float)value;
			progress.maximum = (float)maximum;
			menu.progress_bars.push_back(progress);
#endif
		}
		lua_pushnil(state);
		while(lua_next(state, 7) != 0)
		{
			const bool valid_key = lua_type(state, -2) == LUA_TNUMBER &&
				std::floor(lua_tonumber(state, -2)) == lua_tonumber(state, -2) &&
				lua_tonumber(state, -2) >= 1 && lua_tonumber(state, -2) <= 4;
			lua_pop(state, 1);
			if(!valid_key) { lua_pop(state, 1); throw glare::Exception("game.setHUDMenu(): progress bars must be a dense array of at most 4 values" + errorContextString(state)); }
		}
	}
	size_t text_widget_count = 0;
	if(lua_gettop(state) >= 8 && !lua_isnil(state, 8))
	{
		if(!lua_istable(state, 8)) throw glare::Exception("game.setHUDMenu(): arg 8 must be an array of positioned text-widget tables" + errorContextString(state));
		bool found_end = false;
		std::set<std::string> widget_ids;
		for(int i = 1; i <= 9; ++i)
		{
			lua_rawgeti(state, 8, i);
			if(lua_isnil(state, -1)) { lua_pop(state, 1); found_end = true; continue; }
			if(found_end) { lua_pop(state, 1); throw glare::Exception("game.setHUDMenu(): text widgets must be a dense array" + errorContextString(state)); }
			if(i > 8 || !lua_istable(state, -1)) { lua_pop(state, 1); throw glare::Exception("game.setHUDMenu(): text widgets must be an array of at most 8 tables" + errorContextString(state)); }
			lua_getfield(state, -1, "id");
			const std::string id = LuaUtils::getStringArg(state, -1);
			lua_pop(state, 1);
			lua_getfield(state, -1, "text");
			const std::string text = LuaUtils::getStringArg(state, -1);
			lua_pop(state, 1);
			auto read_widget_number = [state](const char* key) -> double {
				lua_getfield(state, -1, key);
				if(!lua_isnumber(state, -1)) { lua_pop(state, 1); throw glare::Exception(std::string("game.setHUDMenu(): text widget field '") + key + "' must be a number" + errorContextString(state)); }
				const double value = lua_tonumber(state, -1);
				lua_pop(state, 1);
				return value;
			};
			auto read_widget_colour = [state](const char* key, float& r, float& g, float& b) {
				lua_getfield(state, -1, key);
				if(lua_isnil(state, -1)) { lua_pop(state, 1); return; }
				if(!lua_istable(state, -1)) { lua_pop(state, 1); throw glare::Exception(std::string("game.setHUDMenu(): text widget field '") + key + "' must be a {r,g,b} colour table" + errorContextString(state)); }
				auto read_component = [state](const char* component) -> float {
					lua_getfield(state, -1, component);
					if(!lua_isnumber(state, -1)) { lua_pop(state, 1); throw glare::Exception(std::string("game.setHUDMenu(): colour component '") + component + "' must be a number" + errorContextString(state)); }
					const double value = lua_tonumber(state, -1);
					lua_pop(state, 1);
					if(!isFinite(value) || value < 0.0 || value > 1.0)
						throw glare::Exception("game.setHUDMenu(): colour components must be finite numbers from 0 to 1" + errorContextString(state));
					return (float)value;
				};
				r = read_component("r");
				g = read_component("g");
				b = read_component("b");
				lua_pop(state, 1);
			};
			const double x = read_widget_number("x");
			const double y = read_widget_number("y");
			const double width = read_widget_number("width");
			float text_r = 0.92f, text_g = 0.92f, text_b = 0.95f;
			float background_r = 0.f, background_g = 0.f, background_b = 0.f, background_alpha = 0.f;
			read_widget_colour("text_color", text_r, text_g, text_b);
			read_widget_colour("background_color", background_r, background_g, background_b);
			lua_getfield(state, -1, "background_alpha");
			if(!lua_isnil(state, -1))
			{
				if(!lua_isnumber(state, -1)) { lua_pop(state, 2); throw glare::Exception("game.setHUDMenu(): background_alpha must be a number from 0 to 1" + errorContextString(state)); }
				const double alpha = lua_tonumber(state, -1);
				if(!isFinite(alpha) || alpha < 0.0 || alpha > 1.0) { lua_pop(state, 2); throw glare::Exception("game.setHUDMenu(): background_alpha must be a finite number from 0 to 1" + errorContextString(state)); }
				background_alpha = (float)alpha;
			}
			lua_pop(state, 1);
			lua_pop(state, 1); // widget table
			if(!isValidGameHUDActionID(id) || !widget_ids.insert(id).second || text.empty() || text.size() > 256 ||
				!isFinite(x) || x < -1.0 || x > 1.0 || !isFinite(y) || y < -1.0 || y > 1.0 ||
				!isFinite(width) || width < 0.05 || width > 0.8 || x - width * 0.5 < -1.0 || x + width * 0.5 > 1.0)
				throw glare::Exception("game.setHUDMenu(): each text widget needs a unique id, 1-256 byte text, on-screen x/y, and width 0.05..0.8" + errorContextString(state));
			++text_widget_count;
#if SERVER
			GameHUDTextWidget widget;
			widget.id = id;
			widget.text = text;
			widget.center_x = (float)x;
			widget.top_y = (float)y;
			widget.width = (float)width;
			widget.text_r = text_r; widget.text_g = text_g; widget.text_b = text_b;
			widget.background_r = background_r; widget.background_g = background_g; widget.background_b = background_b;
			widget.background_alpha = background_alpha;
			menu.text_widgets.push_back(widget);
#endif
		}
		lua_pushnil(state);
		while(lua_next(state, 8) != 0)
		{
			const bool valid_key = lua_type(state, -2) == LUA_TNUMBER &&
				std::floor(lua_tonumber(state, -2)) == lua_tonumber(state, -2) &&
				lua_tonumber(state, -2) >= 1 && lua_tonumber(state, -2) <= 8;
			lua_pop(state, 1);
			if(!valid_key) { lua_pop(state, 1); throw glare::Exception("game.setHUDMenu(): text widgets must be a dense array of at most 8 values" + errorContextString(state)); }
		}
	}
	if(form_field_count > 0 && action_ids.empty())
		throw glare::Exception("game.setHUDMenu(): form fields require at least one submit action" + errorContextString(state));
	const bool has_menu_content = !action_ids.empty() || progress_bar_count > 0 || text_widget_count > 0;
	if((!has_menu_content && !title.empty()) || (has_menu_content && title.empty()))
		throw glare::Exception("game.setHUDMenu(): provide a title and at least one action, progress bar, or text widget, or clear the menu" + errorContextString(state));

	bool queued = false;
#if SERVER
	SubstrataLuaVM* sub_lua_vm = (SubstrataLuaVM*)lua_callbacks(state)->userdata;
	LuaScript* script = (LuaScript*)lua_getthreaddata(state);
	LuaScriptEvaluator* script_evaluator = script ? (LuaScriptEvaluator*)script->userdata : nullptr;
	if(script_evaluator && script_evaluator->world_object && script_evaluator->world_state &&
		script_evaluator->cur_world_state_lock && sub_lua_vm->server)
	{
		ServerWorldState& world = *script_evaluator->world_state;
		WorldStateLock& lock = *script_evaluator->cur_world_state_lock;
		const UID game_uid = script_evaluator->world_object->uid;
		const std::vector<UID> participants = world.getGameSessionParticipants(game_uid, lock);
		for(const UID& participant_uid : participants)
			if(participant_uid == avatar_uid)
			{
				if(!world.allowGameHUDUpdate(game_uid, std::chrono::steady_clock::now(), lock))
					break;
				std::vector<GameHUDMenu> existing_menus;
				world.getGameHUDMenus(game_uid, avatar_uid, existing_menus, lock);
				bool panel_exists = false;
				for(const GameHUDMenu& existing : existing_menus)
				{
					panel_exists = panel_exists || existing.panel_id == panel_id;
					if(existing.panel_id == panel_id) continue;
					for(const GameHUDMenuAction& old_action : existing.actions)
						for(const GameHUDMenuAction& new_action : menu.actions)
							if(old_action.id == new_action.id)
								throw glare::Exception("game.setHUDMenu(): action ids must be unique across all open panels for this player" + errorContextString(state));
				}
				if(!panel_exists && existing_menus.size() >= 4)
					throw glare::Exception("game.setHUDMenu(): a player may have at most four named menus open for one game" + errorContextString(state));
				world.setGameHUDMenu(game_uid, avatar_uid, menu, lock);
				sub_lua_vm->server->enqueueGameHUDMenuForAvatar(avatar_uid, game_uid, menu, script_evaluator->world_state);
				queued = true;
				break;
			}
	}
#endif
	lua_pushboolean(state, queued ? 1 : 0);
	return 1;
}


static int gameClearHUDMenu(lua_State* state)
{
	if(lua_gettop(state) > 2) throw glare::Exception("game.clearHUDMenu(): expected avatar and optional panel id" + errorContextString(state));
	checkNumArgs(state, /*num_args_required=*/1);
	const UID avatar_uid((uint64)LuaUtils::getTableNumberField(state, /*table index=*/1, "uid"));
	const std::string panel_id = lua_gettop(state) >= 2 ? LuaUtils::getStringArg(state, /*index=*/2) : "main";
	if(!isValidGameHUDPanelID(panel_id)) throw glare::Exception("game.clearHUDMenu(): panel id must be 1-24 ASCII letters, digits, '_' or '-'" + errorContextString(state));
	bool queued = false;
#if SERVER
	GameHUDMenu empty_menu;
	empty_menu.panel_id = panel_id;
	SubstrataLuaVM* sub_lua_vm = (SubstrataLuaVM*)lua_callbacks(state)->userdata;
	LuaScript* script = (LuaScript*)lua_getthreaddata(state);
	LuaScriptEvaluator* script_evaluator = script ? (LuaScriptEvaluator*)script->userdata : nullptr;
	if(script_evaluator && script_evaluator->world_object && script_evaluator->world_state &&
		script_evaluator->cur_world_state_lock && sub_lua_vm->server)
	{
		const UID game_uid = script_evaluator->world_object->uid;
		ServerWorldState& world = *script_evaluator->world_state;
		WorldStateLock& lock = *script_evaluator->cur_world_state_lock;
		const std::vector<UID> participants = world.getGameSessionParticipants(game_uid, lock);
		for(const UID& participant_uid : participants)
			if(participant_uid == avatar_uid)
			{
				world.setGameHUDMenu(game_uid, avatar_uid, empty_menu, lock);
				sub_lua_vm->server->enqueueGameHUDMenuForAvatar(avatar_uid, game_uid, empty_menu, script_evaluator->world_state);
				queued = true;
				break;
			}
	}
#endif
	lua_pushboolean(state, queued ? 1 : 0);
	return 1;
}


static int gameGetPlayers(lua_State* state)
{
	checkNumArgs(state, /*num_args_required=*/0);
	lua_createtable(state, /*num array elems=*/0, /*num non-array elems=*/0);
#if SERVER
	SubstrataLuaVM* sub_lua_vm = (SubstrataLuaVM*)lua_callbacks(state)->userdata;
	LuaScript* script = (LuaScript*)lua_getthreaddata(state);
	LuaScriptEvaluator* script_evaluator = script ? (LuaScriptEvaluator*)script->userdata : nullptr;
	if(script_evaluator && script_evaluator->world_object && script_evaluator->world_state &&
		script_evaluator->cur_world_state_lock && sub_lua_vm->server)
	{
		const std::vector<UID> participant_uids = script_evaluator->world_state->getGameSessionParticipants(
			script_evaluator->world_object->uid, *script_evaluator->cur_world_state_lock);
		const ServerWorldState::AvatarMapType& avatars = script_evaluator->world_state->getAvatars(*script_evaluator->cur_world_state_lock);
		int result_index = 1;
		for(const UID& avatar_uid : participant_uids)
		{
			const auto avatar_it = avatars.find(avatar_uid);
			if(avatar_it != avatars.end() && avatar_it->second->state != Avatar::State_Dead)
			{
				script_evaluator->pushAvatarTableOntoStack(avatar_uid);
				lua_rawseti(state, /*table index=*/-2, result_index++);
			}
			else
				script_evaluator->world_state->setGameSessionParticipant(script_evaluator->world_object->uid,
					avatar_uid, /*active=*/false, *script_evaluator->cur_world_state_lock);
		}
	}
#endif
	return 1;
}


// Return active game participants whose latest server-stored avatar position
// lies within a bounded radius of the game host or one of its spawned objects.
// Avatar movement is client-reported, so this is a server-side proximity query,
// not a physics collision event or movement anti-cheat check.
static int gameGetPlayersNear(lua_State* state)
{
#if SERVER
	checkNumArgs(state, /*num_args_required=*/2);
	if(!lua_istable(state, 1))
		throw glare::Exception("game.getPlayersNear(): arg 1 must be a game object" + errorContextString(state));
	const double raw_uid = LuaUtils::getTableNumberField(state, /*table index=*/1, "uid");
	if(!std::isfinite(raw_uid) || raw_uid < 1 || raw_uid > 9007199254740991.0 || raw_uid != std::floor(raw_uid))
		throw glare::Exception("game.getPlayersNear(): object.uid must be a positive exact integer" + errorContextString(state));
	const double radius = LuaUtils::getDoubleArg(state, /*index=*/2);
	if(!std::isfinite(radius) || radius < 0.0 || radius > 1000.0)
		throw glare::Exception("game.getPlayersNear(): radius must be finite and between 0 and 1000 metres" + errorContextString(state));
	LuaScript* script = (LuaScript*)lua_getthreaddata(state);
	LuaScriptEvaluator* script_evaluator = script ? (LuaScriptEvaluator*)script->userdata : nullptr;
	if(!script_evaluator || !script_evaluator->world_object || !script_evaluator->world_state || !script_evaluator->cur_world_state_lock)
	{
		lua_pushnil(state);
		LuaUtils::pushString(state, "Game proximity queries require the authoritative server script context.");
		return 2;
	}
	WorldStateLock& lock = *script_evaluator->cur_world_state_lock;
	ServerWorldState& world = *script_evaluator->world_state;
	const UID game_uid = script_evaluator->world_object->uid;
	const UID target_uid((uint64)raw_uid);
	auto& objects = world.getObjects(lock);
	const auto target_it = objects.find(target_uid);
	if(target_it == objects.end() || target_it->second->state == WorldObject::State_Dead ||
		(target_uid != game_uid && target_it->second->game_spawn_owner_uid != game_uid))
	{
		lua_pushnil(state);
		LuaUtils::pushString(state, "game.getPlayersNear() accepts only the live game host or an object spawned by this game.");
		return 2;
	}
	if(!world.allowGameSpatialQuery(game_uid, lock))
	{
		lua_pushnil(state);
		LuaUtils::pushString(state, "game.getPlayersNear(): spatial-query budget reached (60 per second per game, 600 per second shared across this world).");
		return 2;
	}
	const Vec4f target_center = target_it->second->getCentroidWS();
	const double radius_squared = radius * radius;
	const std::vector<UID> participant_uids = world.getGameSessionParticipants(game_uid, lock);
	if(participant_uids.size() > 256)
	{
		lua_pushnil(state);
		LuaUtils::pushString(state, "game.getPlayersNear(): the game has more than 256 active participants; reduce the session size.");
		return 2;
	}
	const ServerWorldState::AvatarMapType& avatars = world.getAvatars(lock);
	std::vector<UID> nearby_uids;
	nearby_uids.reserve(participant_uids.size());
	for(const UID& avatar_uid : participant_uids)
	{
		const auto avatar_it = avatars.find(avatar_uid);
		if(avatar_it == avatars.end() || avatar_it->second->state != Avatar::State_Alive)
			continue;
		const Vec3d delta = avatar_it->second->pos - Vec3d(target_center[0], target_center[1], target_center[2]);
		if(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z <= radius_squared)
			nearby_uids.push_back(avatar_uid);
	}
	lua_createtable(state, (int)nearby_uids.size(), 0);
	for(size_t i = 0; i < nearby_uids.size(); ++i)
	{
		script_evaluator->pushAvatarTableOntoStack(nearby_uids[i]);
		lua_rawseti(state, /*table index=*/-2, (int)i + 1);
	}
	return 1;
#else
	(void)state;
	lua_pushnil(state);
	LuaUtils::pushString(state, "game.getPlayersNear() is available only on the authoritative server.");
	return 2;
#endif
}


// Return active game participants whose conservative avatar bounds overlap a
// live game object's world-space AABB. This is a trigger-zone query, not a
// physics contact event or avatar-movement authority check.
static int gameGetPlayersOverlapping(lua_State* state)
{
#if SERVER
	checkNumArgs(state, /*num_args_required=*/1);
	if(!lua_istable(state, 1))
		throw glare::Exception("game.getPlayersOverlapping(): arg 1 must be a game object" + errorContextString(state));
	const double raw_uid = LuaUtils::getTableNumberField(state, /*table index=*/1, "uid");
	if(!std::isfinite(raw_uid) || raw_uid < 1 || raw_uid > 9007199254740991.0 || raw_uid != std::floor(raw_uid))
		throw glare::Exception("game.getPlayersOverlapping(): object.uid must be a positive exact integer" + errorContextString(state));
	LuaScript* script = (LuaScript*)lua_getthreaddata(state);
	LuaScriptEvaluator* script_evaluator = script ? (LuaScriptEvaluator*)script->userdata : nullptr;
	if(!script_evaluator || !script_evaluator->world_object || !script_evaluator->world_state || !script_evaluator->cur_world_state_lock)
	{
		lua_pushnil(state);
		LuaUtils::pushString(state, "Game player-overlap queries require the authoritative server script context.");
		return 2;
	}
	WorldStateLock& lock = *script_evaluator->cur_world_state_lock;
	ServerWorldState& world = *script_evaluator->world_state;
	const UID game_uid = script_evaluator->world_object->uid;
	const UID target_uid((uint64)raw_uid);
	auto& objects = world.getObjects(lock);
	const auto target_it = objects.find(target_uid);
	if(target_it == objects.end() || target_it->second->state == WorldObject::State_Dead ||
		(target_uid != game_uid && target_it->second->game_spawn_owner_uid != game_uid))
	{
		lua_pushnil(state);
		LuaUtils::pushString(state, "game.getPlayersOverlapping() accepts only the live game host or an object spawned by this game.");
		return 2;
	}
	if(!world.allowGameSpatialQuery(game_uid, lock))
	{
		lua_pushnil(state);
		LuaUtils::pushString(state, "game.getPlayersOverlapping(): spatial-query budget reached (60 per second per game, 600 per second shared across this world).");
		return 2;
	}
	const js::AABBox target_bounds = target_it->second->getAABBWS();
	const std::vector<UID> participant_uids = world.getGameSessionParticipants(game_uid, lock);
	if(participant_uids.size() > 256)
	{
		lua_pushnil(state);
		LuaUtils::pushString(state, "game.getPlayersOverlapping(): the game has more than 256 active participants; reduce the session size.");
		return 2;
	}
	static const double PLAYER_HALF_WIDTH = 0.30;
	static const double PLAYER_HEIGHT = 1.90;
	const ServerWorldState::AvatarMapType& avatars = world.getAvatars(lock);
	std::vector<UID> overlapping_uids;
	overlapping_uids.reserve(participant_uids.size());
	for(const UID& avatar_uid : participant_uids)
	{
		const auto avatar_it = avatars.find(avatar_uid);
		if(avatar_it == avatars.end() || avatar_it->second->state != Avatar::State_Alive || !avatar_it->second->pos.isFinite())
			continue;
		const Vec3d& pos = avatar_it->second->pos;
		const js::AABBox player_bounds(
			Vec4f((float)(pos.x - PLAYER_HALF_WIDTH), (float)(pos.y - PLAYER_HALF_WIDTH), (float)pos.z, 1.f),
			Vec4f((float)(pos.x + PLAYER_HALF_WIDTH), (float)(pos.y + PLAYER_HALF_WIDTH), (float)(pos.z + PLAYER_HEIGHT), 1.f));
		if(player_bounds.intersectsAABB(target_bounds))
			overlapping_uids.push_back(avatar_uid);
	}
	lua_createtable(state, (int)overlapping_uids.size(), 0);
	for(size_t i = 0; i < overlapping_uids.size(); ++i)
	{
		script_evaluator->pushAvatarTableOntoStack(overlapping_uids[i]);
		lua_rawseti(state, /*table index=*/-2, (int)i + 1);
	}
	return 1;
#else
	(void)state;
	lua_pushnil(state);
	LuaUtils::pushString(state, "game.getPlayersOverlapping() is available only on the authoritative server.");
	return 2;
#endif
}


static bool isValidGameSaveKey(const std::string& key)
{
	return !key.empty() && key.size() <= 64;
}


#if SERVER
static const size_t MAX_GAME_SAVE_RECORDS = 64;
static const size_t MAX_GAME_SAVE_VALUE_BYTES = 16 * 1024;
static const size_t MAX_GAME_SAVE_NAMESPACE_BYTES = 256 * 1024;

struct GameSaveNamespaceUsage
{
	size_t record_count = 0;
	size_t byte_count = 0;
};


static GameSaveNamespaceUsage getGameSaveNamespaceUsage(ServerAllWorldsState& all_world_state, const UID& game_object_uid, const std::string& prefix)
{
	GameSaveNamespaceUsage usage;
	const ObjectStorageKey first_key(game_object_uid, prefix);
	for(auto it = all_world_state.object_storage_items.lower_bound(first_key); it != all_world_state.object_storage_items.end(); ++it)
	{
		if(it->first.ob_uid != game_object_uid || it->first.key_string.compare(0, prefix.size(), prefix) != 0)
			break;
		usage.record_count++;
		usage.byte_count += it->second->data.size();
	}
	return usage;
}


static void pushGameStoredValue(lua_State* state, SubstrataLuaVM* lua_vm, ServerAllWorldsState& all_world_state,
	const UID& game_object_uid, const std::string& storage_key)
{
	const auto it = all_world_state.object_storage_items.find(ObjectStorageKey(game_object_uid, storage_key));
	if(it == all_world_state.object_storage_items.end())
	{
		lua_pushnil(state);
		lua_pushnumber(state, 0);
		return;
	}

	try
	{
		if(it->second->data.size() > MAX_GAME_SAVE_VALUE_BYTES + sizeof(uint32))
			throw glare::Exception("Stored game data exceeds the supported record size.");
		BufferViewInStream stream(ArrayRef<uint8>(it->second->data.data(), it->second->data.size()));
		const uint32 stored_version = stream.readUInt32();
		if(stored_version == 0 || stored_version > 65535)
			throw glare::Exception("Stored game data has an invalid schema version.");
		LuaSerialisation::deserialise(state, lua_vm->metatable_uid_to_ref_map, stream);
		if(!stream.endOfStream())
		{
			lua_pop(state, 1);
			throw glare::Exception("Stored game data contains trailing bytes.");
		}
		lua_pushnumber(state, stored_version);
	}
	catch(glare::Exception& e)
	{
		throw glare::Exception("Error reading saved game data: " + std::string(e.what()));
	}
}


static bool writeGameStoredValue(lua_State* state, LuaScriptEvaluator* script_evaluator, SubstrataLuaVM* lua_vm,
	ServerAllWorldsState& all_world_state, const UID& game_object_uid, const UserID& rate_limit_user_id,
	const std::string& namespace_prefix, const std::string& key, uint32 version, int value_stack_index, std::string& error_out)
{
	if(!script_evaluator || !script_evaluator->cur_world_state_lock || !game_object_uid.valid())
	{
		error_out = "Game data is unavailable in this script context.";
		return false;
	}
	if(!lua_vm || !lua_vm->server || lua_vm->server->world_state->read_only_mode)
	{
		error_out = "Game data cannot be saved while the server is in read-only mode.";
		return false;
	}
	if(lua_type(state, value_stack_index) == LUA_TNIL)
	{
		error_out = "Saved game data cannot be nil; store an explicit value such as false or an empty table.";
		return false;
	}
	if(!all_world_state.allowGameStorageWrite(game_object_uid, rate_limit_user_id, std::chrono::steady_clock::now()))
	{
		error_out = "Game data write limit exceeded (10 writes per second).";
		return false;
	}

	BufferOutStream value_stream;
	value_stream.writeUInt32(version);
	LuaSerialisation::SerialisationOptions options;
	options.max_depth = 16;
	options.max_serialised_size_B = MAX_GAME_SAVE_VALUE_BYTES;
	try
	{
		LuaSerialisation::serialise(state, value_stack_index, options, value_stream);
	}
	catch(glare::Exception& e)
	{
		error_out = "Could not serialise game data: " + std::string(e.what());
		return false;
	}

	if(value_stream.buf.size() > MAX_GAME_SAVE_VALUE_BYTES + sizeof(uint32))
	{
		error_out = "Saved game data exceeds the 16 KiB per-record limit.";
		return false;
	}

	const std::string full_key = namespace_prefix + key;
	const ObjectStorageKey storage_key(game_object_uid, full_key);
	const auto existing_it = all_world_state.object_storage_items.find(storage_key);
	const size_t existing_size = existing_it == all_world_state.object_storage_items.end() ? 0 : existing_it->second->data.size();
	if(existing_it != all_world_state.object_storage_items.end())
	{
		if(existing_size < sizeof(uint32))
		{
			error_out = "Existing game save is corrupt; use a new key to preserve recoverability.";
			return false;
		}
		try
		{
			BufferViewInStream existing_stream(ArrayRef<uint8>(existing_it->second->data.data(), existing_size));
			const uint32 existing_version = existing_stream.readUInt32();
			if(existing_version == 0 || existing_version > 65535)
			{
				error_out = "Existing game save has an invalid schema version; use a new key to preserve recoverability.";
				return false;
			}
			if(version < existing_version)
			{
				error_out = "Game save schema version cannot be downgraded.";
				return false;
			}
		}
		catch(glare::Exception& e)
		{
			error_out = "Could not read existing game save version: " + std::string(e.what());
			return false;
		}
	}
	const GameSaveNamespaceUsage usage = getGameSaveNamespaceUsage(all_world_state, game_object_uid, namespace_prefix);
	if(existing_it == all_world_state.object_storage_items.end() && usage.record_count >= MAX_GAME_SAVE_RECORDS)
	{
		error_out = "Game save namespace is full (64 records).";
		return false;
	}
	if(usage.byte_count - existing_size + value_stream.buf.size() > MAX_GAME_SAVE_NAMESPACE_BYTES)
	{
		error_out = "Game save namespace is full (256 KiB).";
		return false;
	}

	try
	{
		ObjectStorageItemRef item = all_world_state.getOrCreateObjectStorageItem(storage_key);
		item->data = value_stream.buf;
		all_world_state.db_dirty_object_storage_items.insert(item);
		all_world_state.markAsChanged();
	}
	catch(glare::Exception& e)
	{
		error_out = "Could not persist game data: " + std::string(e.what());
		return false;
	}
	return true;
}
#endif // SERVER


static uint32 readGameSaveVersionArg(lua_State* state, int index)
{
	const double raw_version = LuaUtils::getDoubleArg(state, index);
	if(!std::isfinite(raw_version) || raw_version < 1 || raw_version > 65535 || std::floor(raw_version) != raw_version)
		throw glare::Exception("Game save schema version must be an integer from 1 to 65535" + errorContextString(state));
	return (uint32)raw_version;
}


static UID getGameSaveAvatarUID(lua_State* state, int table_index)
{
	lua_getfield(state, table_index, "uid");
	if(lua_type(state, -1) != LUA_TNUMBER)
	{
		lua_pop(state, 1);
		throw glare::Exception("Avatar uid must be an exact non-negative integer" + errorContextString(state));
	}
	const double raw_uid = lua_tonumber(state, -1);
	lua_pop(state, 1);
	// Lua numbers are doubles. Refuse values beyond the exact integer range so
	// the identity lookup cannot silently round a caller-supplied avatar UID.
	const double largest_exact_integer = 9007199254740991.0; // 2^53 - 1
	if(!std::isfinite(raw_uid) || raw_uid < 0 || raw_uid > largest_exact_integer || std::floor(raw_uid) != raw_uid)
		throw glare::Exception("Avatar uid must be an exact non-negative integer" + errorContextString(state));
	return UID((uint64)raw_uid);
}


static bool isValidGameKeyName(const std::string& key)
{
	if(key.size() == 1)
		return (key[0] >= 'a' && key[0] <= 'z') || (key[0] >= '0' && key[0] <= '9');
	return key == "left" || key == "right" || key == "up" || key == "down" ||
		key == "space" || key == "enter" || key == "escape" ||
		key == "gamepad_a" || key == "gamepad_b" || key == "gamepad_x" || key == "gamepad_y" ||
		key == "gamepad_l1" || key == "gamepad_r1" || key == "gamepad_l3" || key == "gamepad_r3" ||
		key == "gamepad_start" || key == "gamepad_select" || key == "gamepad_up" || key == "gamepad_down" ||
		key == "gamepad_left" || key == "gamepad_right" ||
		key == "mouse_left" || key == "mouse_middle" || key == "mouse_right";
}


static uint32 gameKeyCodeForName(const std::string& key)
{
	if(key.size() == 1 && ((key[0] >= 'a' && key[0] <= 'z') || (key[0] >= '0' && key[0] <= '9')))
		return Protocol::ScriptKey_CharacterBase + (unsigned char)key[0];
	if(key == "left") return Protocol::ScriptKey_Left;
	if(key == "right") return Protocol::ScriptKey_Right;
	if(key == "up") return Protocol::ScriptKey_Up;
	if(key == "down") return Protocol::ScriptKey_Down;
	// The client keeps the original dedicated WASD codes for avatar movement;
	// the server translates those codes to lowercase key names before storing them.
	if(key == "w") return Protocol::ScriptKey_W;
	if(key == "a") return Protocol::ScriptKey_A;
	if(key == "s") return Protocol::ScriptKey_S;
	if(key == "d") return Protocol::ScriptKey_D;
	if(key == "space") return Protocol::ScriptKey_Space;
	if(key == "enter") return Protocol::ScriptKey_Enter;
	if(key == "escape") return Protocol::ScriptKey_Escape;
	if(key == "gamepad_a") return Protocol::ScriptKey_GamepadA;
	if(key == "gamepad_b") return Protocol::ScriptKey_GamepadB;
	if(key == "gamepad_x") return Protocol::ScriptKey_GamepadX;
	if(key == "gamepad_y") return Protocol::ScriptKey_GamepadY;
	if(key == "gamepad_l1") return Protocol::ScriptKey_GamepadL1;
	if(key == "gamepad_r1") return Protocol::ScriptKey_GamepadR1;
	if(key == "gamepad_l3") return Protocol::ScriptKey_GamepadL3;
	if(key == "gamepad_r3") return Protocol::ScriptKey_GamepadR3;
	if(key == "gamepad_start") return Protocol::ScriptKey_GamepadStart;
	if(key == "gamepad_select") return Protocol::ScriptKey_GamepadSelect;
	if(key == "gamepad_up") return Protocol::ScriptKey_GamepadUp;
	if(key == "gamepad_down") return Protocol::ScriptKey_GamepadDown;
	if(key == "gamepad_left") return Protocol::ScriptKey_GamepadLeft;
	if(key == "gamepad_right") return Protocol::ScriptKey_GamepadRight;
	if(key == "mouse_left") return Protocol::ScriptKey_MouseLeft;
	if(key == "mouse_middle") return Protocol::ScriptKey_MouseMiddle;
	if(key == "mouse_right") return Protocol::ScriptKey_MouseRight;
	return 0;
}


static bool getBoundGameAction(LuaScriptEvaluator* evaluator, const std::string& action, const std::vector<std::string>*& controls_out)
{
	if(!evaluator) return false;
	const auto it = evaluator->game_action_bindings.find(action);
	if(it == evaluator->game_action_bindings.end()) return false;
	controls_out = &it->second;
	return true;
}


static int gameBindAction(lua_State* state)
{
	if(lua_gettop(state) != 2)
		throw glare::Exception("game.bindAction(): expected an action name and controls array" + errorContextString(state));
	checkNumArgs(state, /*num_args_required=*/2);
	const std::string action = LuaUtils::getStringArg(state, /*index=*/1);
	if(!isValidGameHUDActionID(action))
		throw glare::Exception("game.bindAction(): action name must be 1-32 ASCII letters, digits, '_', '-' or '.'" + errorContextString(state));
	if(!lua_istable(state, 2))
		throw glare::Exception("game.bindAction(): controls must be a dense array of 1..8 key/button names" + errorContextString(state));

	std::map<int, std::string> indexed_controls;
	std::set<std::string> unique_controls;
	lua_pushnil(state);
	while(lua_next(state, 2) != 0)
	{
		const bool key_ok = lua_type(state, -2) == LUA_TNUMBER && std::floor(lua_tonumber(state, -2)) == lua_tonumber(state, -2) &&
			lua_tonumber(state, -2) >= 1 && lua_tonumber(state, -2) <= 8;
		const int index = key_ok ? (int)lua_tonumber(state, -2) : 0;
		if(!key_ok || lua_type(state, -1) != LUA_TSTRING)
		{
			lua_pop(state, 2);
			throw glare::Exception("game.bindAction(): controls must be a dense array of 1..8 supported key/button names" + errorContextString(state));
		}
		const std::string control = LuaUtils::getStringArg(state, /*index=*/-1);
		if(!isValidGameKeyName(control) || !unique_controls.insert(control).second || !indexed_controls.insert(std::make_pair(index, control)).second)
		{
			lua_pop(state, 2);
			throw glare::Exception("game.bindAction(): every control must be a unique supported key/button name" + errorContextString(state));
		}
		lua_pop(state, 1);
	}
	if(indexed_controls.empty())
		throw glare::Exception("game.bindAction(): bind at least one control" + errorContextString(state));
	std::vector<std::string> controls;
	for(size_t i=0; i<indexed_controls.size(); ++i)
	{
		const auto it = indexed_controls.find((int)i + 1);
		if(it == indexed_controls.end())
			throw glare::Exception("game.bindAction(): controls must be a dense array starting at index 1" + errorContextString(state));
		controls.push_back(it->second);
	}

	LuaScript* script = (LuaScript*)lua_getthreaddata(state);
	LuaScriptEvaluator* evaluator = script ? (LuaScriptEvaluator*)script->userdata : nullptr;
	if(!evaluator || !evaluator->world_object)
		throw glare::Exception("game.bindAction(): this script has no live host object" + errorContextString(state));
	if(evaluator->game_action_bindings.find(action) == evaluator->game_action_bindings.end() && evaluator->game_action_bindings.size() >= 32)
		throw glare::Exception("game.bindAction(): at most 32 named actions may be bound per script" + errorContextString(state));
	evaluator->game_action_bindings[action] = controls;
	if(!evaluator->game_action_input_binding_registered)
	{
		Reference<ObjectEventHandlers> handlers = evaluator->world_object->getOrCreateEventHandlers();
		handlers->game_action_input_binding_count++;
		evaluator->game_action_input_event_handlers = handlers;
		evaluator->game_action_input_binding_registered = true;
	}
	lua_pushboolean(state, 1);
	return 1;
}


static int gameIsActionDown(lua_State* state)
{
	if(lua_gettop(state) != 2)
		throw glare::Exception("game.isActionDown(): expected avatar and action name" + errorContextString(state));
	checkNumArgs(state, /*num_args_required=*/2);
	if(!lua_istable(state, 1))
		throw glare::Exception("game.isActionDown(): arg 1 must be an avatar" + errorContextString(state));
	const UID avatar_uid = getGameSaveAvatarUID(state, /*table_index=*/1);
	const std::string action = LuaUtils::getStringArg(state, /*index=*/2);
	const std::vector<std::string>* controls = nullptr;
	LuaScript* script = (LuaScript*)lua_getthreaddata(state);
	LuaScriptEvaluator* evaluator = script ? (LuaScriptEvaluator*)script->userdata : nullptr;
	if(!getBoundGameAction(evaluator, action, controls)) { lua_pushboolean(state, 0); return 1; }
#if SERVER
	SubstrataLuaVM* lua_vm = (SubstrataLuaVM*)lua_callbacks(state)->userdata;
	if(evaluator->world_state && evaluator->world_object && evaluator->cur_world_state_lock && lua_vm->server &&
		evaluator->world_state->isGameSessionParticipant(evaluator->world_object->uid, avatar_uid, *evaluator->cur_world_state_lock))
	{
		for(const std::string& control : *controls)
			if(evaluator->world_state->isGameSessionKeyDown(evaluator->world_object->uid, avatar_uid, control, *evaluator->cur_world_state_lock))
			{ lua_pushboolean(state, 1); return 1; }
	}
#endif
	lua_pushboolean(state, 0);
	return 1;
}


static int gameIsLocalActionDown(lua_State* state)
{
	if(lua_gettop(state) != 1)
		throw glare::Exception("game.isLocalActionDown(): expected action name" + errorContextString(state));
	checkNumArgs(state, /*num_args_required=*/1);
	const std::string action = LuaUtils::getStringArg(state, /*index=*/1);
	const std::vector<std::string>* controls = nullptr;
	LuaScript* script = (LuaScript*)lua_getthreaddata(state);
	LuaScriptEvaluator* evaluator = script ? (LuaScriptEvaluator*)script->userdata : nullptr;
	if(!getBoundGameAction(evaluator, action, controls)) { lua_pushboolean(state, 0); return 1; }
#if GUI_CLIENT
	SubstrataLuaVM* lua_vm = (SubstrataLuaVM*)lua_callbacks(state)->userdata;
	GameLocalInput input;
	if(evaluator->world_object && lua_vm->gui_client && lua_vm->gui_client->getGameLocalInput(evaluator->world_object->uid, input))
	{
		for(const std::string& control : *controls)
		{
			const uint32 code = gameKeyCodeForName(control);
			if(code != 0 && (input.keys_down.count(code) > 0 || input.buttons_down.count(code) > 0))
			{ lua_pushboolean(state, 1); return 1; }
		}
	}
#endif
	lua_pushboolean(state, 0);
	return 1;
}


// Server-authoritative held input state for game loops. The state is updated
// before press/release callbacks and discarded when the player's session ends.
static int gameIsKeyDown(lua_State* state)
{
	checkNumArgs(state, /*num_args_required=*/2);
	if(!lua_istable(state, 1))
		throw glare::Exception("game.isKeyDown(): arg 1 must be an avatar" + errorContextString(state));
	const UID avatar_uid = getGameSaveAvatarUID(state, /*table_index=*/1);
	const std::string key = LuaUtils::getStringArg(state, /*index=*/2);
	if(!isValidGameKeyName(key))
		throw glare::Exception("game.isKeyDown(): key must be a supported keyboard, mouse, or gamepad button name" + errorContextString(state));

#if SERVER
	SubstrataLuaVM* lua_vm = (SubstrataLuaVM*)lua_callbacks(state)->userdata;
	LuaScript* script = (LuaScript*)lua_getthreaddata(state);
	LuaScriptEvaluator* script_evaluator = script ? (LuaScriptEvaluator*)script->userdata : nullptr;
	if(script_evaluator && script_evaluator->world_object && script_evaluator->world_state &&
		script_evaluator->cur_world_state_lock && lua_vm->server)
	{
		const bool is_down = script_evaluator->world_state->isGameSessionKeyDown(
			script_evaluator->world_object->uid, avatar_uid, key, *script_evaluator->cur_world_state_lock);
		lua_pushboolean(state, is_down ? 1 : 0);
		return 1;
	}
#endif
	lua_pushboolean(state, 0);
	return 1;
}


static int gameGetInput(lua_State* state)
{
	checkNumArgs(state, /*num_args_required=*/1);
	if(!lua_istable(state, 1))
		throw glare::Exception("game.getInput(): arg 1 must be an avatar" + errorContextString(state));
	const UID avatar_uid = getGameSaveAvatarUID(state, /*table_index=*/1);
#if SERVER
	SubstrataLuaVM* lua_vm = (SubstrataLuaVM*)lua_callbacks(state)->userdata;
	LuaScript* script = (LuaScript*)lua_getthreaddata(state);
	LuaScriptEvaluator* script_evaluator = script ? (LuaScriptEvaluator*)script->userdata : nullptr;
	if(script_evaluator && script_evaluator->world_object && script_evaluator->world_state &&
		script_evaluator->cur_world_state_lock && lua_vm->server &&
		script_evaluator->world_state->isGameSessionParticipant(script_evaluator->world_object->uid,
			avatar_uid, *script_evaluator->cur_world_state_lock))
	{
		GameInputState input;
		script_evaluator->world_state->getGameSessionInputState(script_evaluator->world_object->uid,
			avatar_uid, input, *script_evaluator->cur_world_state_lock);
		lua_newtable(state);
		lua_newtable(state);
		lua_pushnumber(state, input.wheel_x); lua_setfield(state, -2, "x");
		lua_pushnumber(state, input.wheel_y); lua_setfield(state, -2, "y");
		lua_setfield(state, -2, "wheel");
		if(input.gamepad_connected)
		{
			lua_newtable(state);
			lua_pushnumber(state, input.left_x); lua_setfield(state, -2, "left_x");
			lua_pushnumber(state, input.left_y); lua_setfield(state, -2, "left_y");
			lua_pushnumber(state, input.right_x); lua_setfield(state, -2, "right_x");
			lua_pushnumber(state, input.right_y); lua_setfield(state, -2, "right_y");
			lua_pushnumber(state, input.left_trigger); lua_setfield(state, -2, "left_trigger");
			lua_pushnumber(state, input.right_trigger); lua_setfield(state, -2, "right_trigger");
			lua_setfield(state, -2, "gamepad");
		}
		if(input.pointer_available)
		{
			lua_newtable(state);
			lua_pushnumber(state, input.pointer_x); lua_setfield(state, -2, "x");
			lua_pushnumber(state, input.pointer_y); lua_setfield(state, -2, "y");
			lua_setfield(state, -2, "pointer");
		}
		else
		{
			lua_pushnil(state); lua_setfield(state, -2, "pointer");
		}
		return 1;
	}
#endif
	lua_pushnil(state);
	return 1;
}


#if GUI_CLIENT
static int gameSendToServer(lua_State* state)
{
	checkNumArgs(state, /*num_args_required=*/2);
	if(!lua_istable(state, 2))
		throw glare::Exception("game.sendToServer(): arg 2 must be a plain serializable table" + errorContextString(state));
	const std::string name = LuaUtils::getStringArg(state, /*index=*/1);
	if(name.empty() || name.size() > 32)
		throw glare::Exception("game.sendToServer(): message name must contain 1-32 ASCII identifier characters" + errorContextString(state));
	for(unsigned char c : name)
		if(!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'))
			throw glare::Exception("game.sendToServer(): message name must contain 1-32 ASCII identifier characters" + errorContextString(state));
	SubstrataLuaVM* lua_vm = (SubstrataLuaVM*)lua_callbacks(state)->userdata;
	LuaScript* script = (LuaScript*)lua_getthreaddata(state);
	LuaScriptEvaluator* script_evaluator = script ? (LuaScriptEvaluator*)script->userdata : nullptr;
	if(!lua_vm || !lua_vm->gui_client || !script_evaluator || !script_evaluator->world_object)
	{
		lua_pushboolean(state, 0);
		return 1;
	}
	BufferOutStream stream;
	LuaSerialisation::SerialisationOptions options;
	options.max_depth = 8;
	options.max_serialised_size_B = 4096;
	try
	{
		LuaSerialisation::serialise(state, /*value_stack_index=*/2, options, stream);
	}
	catch(const glare::Exception&)
	{
		lua_pushboolean(state, 0);
		return 1;
	}
	if(stream.buf.empty() || stream.buf.size() > 4096)
	{
		lua_pushboolean(state, 0);
		return 1;
	}
	const std::vector<uint8> payload(stream.buf.begin(), stream.buf.end());
	lua_pushboolean(state, lua_vm->gui_client->sendScriptGameRPC(script_evaluator->world_object->uid, name, payload) ? 1 : 0);
	return 1;
}
#endif


static int gameGetState(lua_State* state)
{
	checkNumArgs(state, /*num_args_required=*/0);
#if SERVER
	SubstrataLuaVM* lua_vm = (SubstrataLuaVM*)lua_callbacks(state)->userdata;
	LuaScript* script = (LuaScript*)lua_getthreaddata(state);
	LuaScriptEvaluator* script_evaluator = script ? (LuaScriptEvaluator*)script->userdata : nullptr;
	if(script_evaluator && script_evaluator->world_object && script_evaluator->world_state &&
		script_evaluator->cur_world_state_lock && lua_vm->server)
	{
		GameStateSnapshot snapshot;
		if(script_evaluator->world_state->getGameStateSnapshot(script_evaluator->world_object->uid,
			snapshot, *script_evaluator->cur_world_state_lock))
		{
			BufferViewInStream stream(ArrayRef<uint8>(snapshot.serialized_value.data(), snapshot.serialized_value.size()));
			LuaSerialisation::deserialise(state, lua_vm->metatable_uid_to_ref_map, stream);
			if(!stream.endOfStream())
				throw glare::Exception("Stored game state contains trailing bytes.");
			lua_pushnumber(state, snapshot.version);
			return 2;
		}
	}
#endif
#if GUI_CLIENT
	SubstrataLuaVM* lua_vm = (SubstrataLuaVM*)lua_callbacks(state)->userdata;
	LuaScript* script = (LuaScript*)lua_getthreaddata(state);
	LuaScriptEvaluator* script_evaluator = script ? (LuaScriptEvaluator*)script->userdata : nullptr;
	if(lua_vm && lua_vm->gui_client && script_evaluator && script_evaluator->world_object)
	{
		uint32 version = 0;
		std::vector<uint8> serialized_value;
		if(lua_vm->gui_client->getReceivedScriptGameState(script_evaluator->world_object->uid, version, serialized_value))
		{
			BufferViewInStream stream(ArrayRef<uint8>(serialized_value.data(), serialized_value.size()));
			LuaSerialisation::deserialise(state, lua_vm->metatable_uid_to_ref_map, stream);
			if(!stream.endOfStream())
				throw glare::Exception("Received game state contains trailing bytes.");
			lua_pushnumber(state, version);
			return 2;
		}
	}
#endif
	lua_pushnil(state);
	lua_pushnumber(state, 0);
	return 2;
}


#if SERVER
static int gamePlayAvatarSpawnEffect(lua_State* state)
{
	checkNumArgs(state, /*num_args_required=*/1);
	if(!lua_istable(state, 1))
		throw glare::Exception("game.playAvatarSpawnEffect(): arg 1 must be an avatar" + errorContextString(state));
	const UID avatar_uid = getGameSaveAvatarUID(state, /*table_index=*/1);
	LuaScript* script = (LuaScript*)lua_getthreaddata(state);
	LuaScriptEvaluator* script_evaluator = script ? (LuaScriptEvaluator*)script->userdata : nullptr;
	SubstrataLuaVM* lua_vm = (SubstrataLuaVM*)lua_callbacks(state)->userdata;
	bool queued = false;
	if(avatar_uid.valid() && script_evaluator && script_evaluator->world_state && script_evaluator->cur_world_state_lock &&
		script_evaluator->world_object && lua_vm->server)
	{
		const auto avatar_it = script_evaluator->world_state->getAvatars(*script_evaluator->cur_world_state_lock).find(avatar_uid);
		if(avatar_it != script_evaluator->world_state->getAvatars(*script_evaluator->cur_world_state_lock).end() &&
			avatar_it->second->state != Avatar::State_Dead)
		{
			lua_vm->server->enqueueAvatarMaterialiseEffect(avatar_uid, script_evaluator->world_state);
			queued = true;
		}
	}
	lua_pushboolean(state, queued ? 1 : 0);
	return 1;
}


static int gameEmitEvent(lua_State* state)
{
	checkNumArgs(state, /*num_args_required=*/1);
	const std::string name = LuaUtils::getStringArg(state, /*index=*/1);
	if(name.empty() || name.size() > 32)
		throw glare::Exception("game.emitEvent(): name must contain 1-32 ASCII identifier characters" + errorContextString(state));
	for(unsigned char c : name)
		if(!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'))
			throw glare::Exception("game.emitEvent(): name must contain 1-32 ASCII identifier characters" + errorContextString(state));
	SubstrataLuaVM* lua_vm = (SubstrataLuaVM*)lua_callbacks(state)->userdata;
	LuaScript* script = (LuaScript*)lua_getthreaddata(state);
	LuaScriptEvaluator* script_evaluator = script ? (LuaScriptEvaluator*)script->userdata : nullptr;
	if(script_evaluator && script_evaluator->world_object && script_evaluator->world_state &&
		script_evaluator->cur_world_state_lock && lua_vm->server)
	{
		WorldStateLock& world_lock = *script_evaluator->cur_world_state_lock;
		const std::vector<UID> recipients = script_evaluator->world_state->getGameSessionParticipants(
			script_evaluator->world_object->uid, world_lock);
		uint32 sequence = 0;
		if(script_evaluator->world_state->allocateGameEventSequence(script_evaluator->world_object->uid,
			recipients.size(), std::chrono::steady_clock::now(), sequence, world_lock))
		{
			lua_vm->server->enqueueGameEvent(script_evaluator->world_object->uid, sequence,
				lua_vm->server->getCurrentGlobalTime(), name, recipients, script_evaluator->world_state);
			lua_pushboolean(state, 1);
			return 1;
		}
	}
	lua_pushboolean(state, 0);
	return 1;
}
#endif


static int gameSetState(lua_State* state)
{
	checkNumArgs(state, /*num_args_required=*/1);
	if(!lua_istable(state, 1))
		throw glare::Exception("game.setState(): arg 1 must be a table" + errorContextString(state));
#if SERVER
	static const size_t MAX_GAME_STATE_BYTES = 16 * 1024;
	SubstrataLuaVM* lua_vm = (SubstrataLuaVM*)lua_callbacks(state)->userdata;
	LuaScript* script = (LuaScript*)lua_getthreaddata(state);
	LuaScriptEvaluator* script_evaluator = script ? (LuaScriptEvaluator*)script->userdata : nullptr;
	if(script_evaluator && script_evaluator->world_object && script_evaluator->world_state &&
		script_evaluator->cur_world_state_lock && lua_vm->server)
	{
		WorldStateLock& lock = *script_evaluator->cur_world_state_lock;
		if(lua_vm->server->world_state->read_only_mode)
		{
			lua_pushboolean(state, 0);
			LuaUtils::pushString(state, "Game state cannot be updated while the server is in read-only mode.");
			return 2;
		}
		BufferOutStream stream;
		LuaSerialisation::SerialisationOptions options;
		options.max_depth = 16;
		options.max_serialised_size_B = MAX_GAME_STATE_BYTES;
		try
		{
			LuaSerialisation::serialise(state, /*value_stack_index=*/1, options, stream);
		}
		catch(glare::Exception& e)
		{
			lua_pushboolean(state, 0);
			LuaUtils::pushString(state, "Could not serialise game state: " + std::string(e.what()));
			return 2;
		}
		if(stream.buf.empty() || stream.buf.size() > MAX_GAME_STATE_BYTES)
		{
			lua_pushboolean(state, 0);
			LuaUtils::pushString(state, "Game state exceeds the 16 KiB snapshot limit.");
			return 2;
		}
		GameStateSnapshot previous;
		uint32 version = script_evaluator->world_state->getGameStateSnapshot(
			script_evaluator->world_object->uid, previous, lock) ? previous.version : 0;
		if(version == 0xFFFFFFFFu || !script_evaluator->world_state->allowGameStateUpdate(
			script_evaluator->world_object->uid, std::chrono::steady_clock::now(), lock))
		{
			lua_pushboolean(state, 0);
			LuaUtils::pushString(state, version == 0xFFFFFFFFu ? "Game state version limit reached." : "Game state update limit exceeded (10 updates per second per game, 600 per second per world)." );
			return 2;
		}
		++version;
		std::vector<uint8> serialized_value(stream.buf.begin(), stream.buf.end());
		if(!script_evaluator->world_state->setGameStateSnapshot(script_evaluator->world_object->uid, version, serialized_value, lock))
		{
			lua_pushboolean(state, 0);
			LuaUtils::pushString(state, "The world's shared game-state snapshot budget is full (8 MiB). Reduce this game's saved state or remove inactive game objects before retrying.");
			return 2;
		}
		lua_vm->server->enqueueGameStateUpdate(script_evaluator->world_object->uid, version, version - 1,
			previous.serialized_value, serialized_value, script_evaluator->world_state);
		lua_pushboolean(state, 1);
		lua_pushnumber(state, version);
		return 2;
	}
#endif
	lua_pushboolean(state, 0);
	LuaUtils::pushString(state, "Game state updates are available only on the authoritative server.");
	return 2;
}


static int gameGetData(lua_State* state)
{
	checkNumArgs(state, /*num_args_required=*/1);
	const std::string key = LuaUtils::getStringArg(state, /*index=*/1);
	if(!isValidGameSaveKey(key))
		throw glare::Exception("game.getData(): key must contain 1-64 bytes" + errorContextString(state));
#if SERVER
	SubstrataLuaVM* lua_vm = (SubstrataLuaVM*)lua_callbacks(state)->userdata;
	LuaScript* script = (LuaScript*)lua_getthreaddata(state);
	LuaScriptEvaluator* script_evaluator = script ? (LuaScriptEvaluator*)script->userdata : nullptr;
	if(script_evaluator && script_evaluator->world_object && script_evaluator->cur_world_state_lock && lua_vm->server)
	{
		ServerAllWorldsState& all_world_state = *lua_vm->server->world_state;
		checkHoldWorldStateMutex(script_evaluator, &all_world_state);
		pushGameStoredValue(state, lua_vm, all_world_state, script_evaluator->world_object->uid, "game:v1:state:" + key);
		return 2;
	}
#endif
	lua_pushnil(state);
	lua_pushnumber(state, 0);
	return 2;
}


static int gameSetData(lua_State* state)
{
	checkNumArgs(state, /*num_args_required=*/3);
	const std::string key = LuaUtils::getStringArg(state, /*index=*/1);
	if(!isValidGameSaveKey(key))
		throw glare::Exception("game.setData(): key must contain 1-64 bytes" + errorContextString(state));
	const uint32 version = readGameSaveVersionArg(state, /*index=*/2);
#if SERVER
	SubstrataLuaVM* lua_vm = (SubstrataLuaVM*)lua_callbacks(state)->userdata;
	LuaScript* script = (LuaScript*)lua_getthreaddata(state);
	LuaScriptEvaluator* script_evaluator = script ? (LuaScriptEvaluator*)script->userdata : nullptr;
	if(script_evaluator && script_evaluator->world_object && script_evaluator->cur_world_state_lock && lua_vm->server)
	{
		ServerAllWorldsState& all_world_state = *lua_vm->server->world_state;
		checkHoldWorldStateMutex(script_evaluator, &all_world_state);
		std::string error;
		const bool saved = writeGameStoredValue(state, script_evaluator, lua_vm, all_world_state,
			script_evaluator->world_object->uid, UserID::invalidUserID(), "game:v1:state:", key, version, /*value_stack_index=*/3, error);
		lua_pushboolean(state, saved ? 1 : 0);
		if(!saved) LuaUtils::pushString(state, error);
		return saved ? 1 : 2;
	}
#endif
	lua_pushboolean(state, 0);
	LuaUtils::pushString(state, "Game persistence is available only on the authoritative server.");
	return 2;
}


static int gameGetPlayerData(lua_State* state)
{
	checkNumArgs(state, /*num_args_required=*/2);
	if(!lua_istable(state, 1))
		throw glare::Exception("game.getPlayerData(): arg 1 must be an avatar" + errorContextString(state));
	const UID avatar_uid = getGameSaveAvatarUID(state, /*table_index=*/1);
	const std::string key = LuaUtils::getStringArg(state, /*index=*/2);
	if(!isValidGameSaveKey(key))
		throw glare::Exception("game.getPlayerData(): key must contain 1-64 bytes" + errorContextString(state));
#if SERVER
	SubstrataLuaVM* lua_vm = (SubstrataLuaVM*)lua_callbacks(state)->userdata;
	LuaScript* script = (LuaScript*)lua_getthreaddata(state);
	LuaScriptEvaluator* script_evaluator = script ? (LuaScriptEvaluator*)script->userdata : nullptr;
	if(script_evaluator && script_evaluator->world_object && script_evaluator->world_state &&
		script_evaluator->cur_world_state_lock && lua_vm->server)
	{
		WorldStateLock& lock = *script_evaluator->cur_world_state_lock;
		UserID user_id;
		if(script_evaluator->world_state->getGameSessionUserID(script_evaluator->world_object->uid, avatar_uid, user_id, lock) && user_id.valid())
		{
			ServerAllWorldsState& all_world_state = *lua_vm->server->world_state;
			checkHoldWorldStateMutex(script_evaluator, &all_world_state);
			const std::string prefix = "game:v1:player:" + user_id.toString() + ":";
			pushGameStoredValue(state, lua_vm, all_world_state, script_evaluator->world_object->uid, prefix + key);
			return 2;
		}
	}
#endif
	lua_pushnil(state);
	lua_pushnumber(state, 0);
	return 2;
}


static int gameSetPlayerData(lua_State* state)
{
	checkNumArgs(state, /*num_args_required=*/4);
	if(!lua_istable(state, 1))
		throw glare::Exception("game.setPlayerData(): arg 1 must be an avatar" + errorContextString(state));
	const UID avatar_uid = getGameSaveAvatarUID(state, /*table_index=*/1);
	const std::string key = LuaUtils::getStringArg(state, /*index=*/2);
	if(!isValidGameSaveKey(key))
		throw glare::Exception("game.setPlayerData(): key must contain 1-64 bytes" + errorContextString(state));
	const uint32 version = readGameSaveVersionArg(state, /*index=*/3);
#if SERVER
	SubstrataLuaVM* lua_vm = (SubstrataLuaVM*)lua_callbacks(state)->userdata;
	LuaScript* script = (LuaScript*)lua_getthreaddata(state);
	LuaScriptEvaluator* script_evaluator = script ? (LuaScriptEvaluator*)script->userdata : nullptr;
	if(script_evaluator && script_evaluator->world_object && script_evaluator->world_state &&
		script_evaluator->cur_world_state_lock && lua_vm->server)
	{
		WorldStateLock& lock = *script_evaluator->cur_world_state_lock;
		UserID user_id;
		if(!script_evaluator->world_state->getGameSessionUserID(script_evaluator->world_object->uid, avatar_uid, user_id, lock))
		{
			lua_pushboolean(state, 0);
			LuaUtils::pushString(state, "Player is not in this game session.");
			return 2;
		}
		if(!user_id.valid())
		{
			lua_pushboolean(state, 0);
			LuaUtils::pushString(state, "Player saves require an authenticated account.");
			return 2;
		}

		ServerAllWorldsState& all_world_state = *lua_vm->server->world_state;
		checkHoldWorldStateMutex(script_evaluator, &all_world_state);
		std::string error;
		const std::string prefix = "game:v1:player:" + user_id.toString() + ":";
		const bool saved = writeGameStoredValue(state, script_evaluator, lua_vm, all_world_state,
			script_evaluator->world_object->uid, user_id, prefix, key, version, /*value_stack_index=*/4, error);
		lua_pushboolean(state, saved ? 1 : 0);
		if(!saved) LuaUtils::pushString(state, error);
		return saved ? 1 : 2;
	}
#endif
	lua_pushboolean(state, 0);
	LuaUtils::pushString(state, "Player persistence is available only on the authoritative server.");
	return 2;
}


#if SERVER
// Create a bounded voxel object directly from a game script so games do not
// need a pre-placed world object for every piece of geometry.
static int gameCreateVoxelObject(lua_State* state)
{
	checkNumArgs(state, /*num_args_required=*/1);
	if(!lua_istable(state, 1))
		throw glare::Exception("game.createVoxelObject(): arg 1 must be a specification table {cells, position, scale?, collidable?, color? or materials?}" + errorContextString(state));
	lua_getfield(state, 1, "cells");
	if(!lua_istable(state, -1))
	{
		lua_pop(state, 1);
		throw glare::Exception("game.createVoxelObject(): cells must be an array of {x,y,z} integer tables" + errorContextString(state));
	}
	std::vector<Voxel> voxels;
	std::set<std::tuple<int,int,int>> unique_cells;
	bool found_array_end = false;
	for(int i = 1; i <= 257; ++i)
	{
		lua_rawgeti(state, -1, i);
		if(lua_isnil(state, -1))
		{
			lua_pop(state, 1);
			found_array_end = true;
			continue;
		}
		if(found_array_end || i > 256 || !lua_istable(state, -1))
		{
			lua_pop(state, 2);
			throw glare::Exception("game.createVoxelObject(): cells must be a dense array of 1..256 coordinate tables" + errorContextString(state));
		}
		auto read_cell_coordinate = [state](const char* field) -> int {
			const double value = LuaUtils::getTableNumberField(state, -1, field);
			if(!std::isfinite(value) || value != std::floor(value) || value < -64 || value > 64)
				throw glare::Exception(std::string("game.createVoxelObject(): cell ") + field + " must be an integer in -64..64" + errorContextString(state));
			return (int)value;
		};
		const int x = read_cell_coordinate("x");
		const int y = read_cell_coordinate("y");
		const int z = read_cell_coordinate("z");
		int material_index = 1;
		lua_getfield(state, -1, "material");
		if(!lua_isnil(state, -1))
		{
			if(!lua_isnumber(state, -1))
			{
				lua_pop(state, 2);
				throw glare::Exception("game.createVoxelObject(): cell material must be a one-based integer palette index" + errorContextString(state));
			}
			const double raw_material_index = lua_tonumber(state, -1);
			if(!std::isfinite(raw_material_index) || raw_material_index != std::floor(raw_material_index) || raw_material_index < 1 || raw_material_index > 16)
			{
				lua_pop(state, 2);
				throw glare::Exception("game.createVoxelObject(): cell material index must be in 1..16" + errorContextString(state));
			}
			material_index = (int)raw_material_index;
		}
		lua_pop(state, 1); // material
		lua_pop(state, 1);
		if(!unique_cells.insert(std::make_tuple(x,y,z)).second)
		{
			lua_pop(state, 1);
			throw glare::Exception("game.createVoxelObject(): duplicate cell coordinates are not allowed" + errorContextString(state));
		}
		voxels.push_back(Voxel(Vec3<int>(x,y,z), material_index - 1));
	}
	lua_pop(state, 1); // cells
	if(voxels.empty())
		throw glare::Exception("game.createVoxelObject(): cells must contain at least one voxel" + errorContextString(state));

	lua_getfield(state, 1, "position");
	if(!lua_istable(state, -1))
	{
		lua_pop(state, 1);
		throw glare::Exception("game.createVoxelObject(): position must be a {x,y,z} table" + errorContextString(state));
	}
	const Vec3d destination = LuaUtils::getVec3d(state, -1);
	lua_pop(state, 1);
	if(!destination.isFinite() || std::abs(destination.x) > 1.0e7 || std::abs(destination.y) > 1.0e7 || std::abs(destination.z) > 1.0e7)
		throw glare::Exception("game.createVoxelObject(): position must be finite and within +/-10,000,000 metres" + errorContextString(state));

	Vec3f scale(0.1f, 0.1f, 0.1f);
	lua_getfield(state, 1, "scale");
	if(!lua_isnil(state, -1))
	{
		if(!lua_istable(state, -1))
		{
			lua_pop(state, 1);
			throw glare::Exception("game.createVoxelObject(): scale must be a {x,y,z} table" + errorContextString(state));
		}
		scale = Vec3f((float)LuaUtils::getTableNumberField(state, -1, "x"),
			(float)LuaUtils::getTableNumberField(state, -1, "y"), (float)LuaUtils::getTableNumberField(state, -1, "z"));
	}
	lua_pop(state, 1);
	if(!scale.isFinite() || scale.x < 0.001f || scale.y < 0.001f || scale.z < 0.001f ||
		scale.x > 10.f || scale.y > 10.f || scale.z > 10.f)
		throw glare::Exception("game.createVoxelObject(): scale components must be within 0.001..10 metres" + errorContextString(state));

	bool collidable = true; // Preserve WorldObject's default: generated level geometry blocks player/object movement.
	lua_getfield(state, 1, "collidable");
	if(!lua_isnil(state, -1))
	{
		if(!lua_isboolean(state, -1))
		{
			lua_pop(state, 1);
			throw glare::Exception("game.createVoxelObject(): collidable must be a boolean when provided" + errorContextString(state));
		}
		collidable = lua_toboolean(state, -1) != 0;
	}
	lua_pop(state, 1);

	auto read_material_colour = [state](int table_index) -> Colour3f {
		if(!lua_istable(state, table_index))
			throw glare::Exception("game.createVoxelObject(): each material must be an {r,g,b} table with channels from 0 to 1" + errorContextString(state));
		const double r = LuaUtils::getTableNumberField(state, table_index, "r");
		const double g = LuaUtils::getTableNumberField(state, table_index, "g");
		const double b = LuaUtils::getTableNumberField(state, table_index, "b");
		if(!std::isfinite(r) || !std::isfinite(g) || !std::isfinite(b) || r < 0 || r > 1 || g < 0 || g > 1 || b < 0 || b > 1)
			throw glare::Exception("game.createVoxelObject(): material color channels must be finite and within 0..1" + errorContextString(state));
		return Colour3f((float)r, (float)g, (float)b);
	};
	std::vector<Colour3f> material_colours;
	lua_getfield(state, 1, "materials");
	const bool has_materials = !lua_isnil(state, -1);
	if(has_materials)
	{
		if(!lua_istable(state, -1))
		{
			lua_pop(state, 1);
			throw glare::Exception("game.createVoxelObject(): materials must be a dense array of 1..16 {r,g,b} tables" + errorContextString(state));
		}
		bool found_material_end = false;
		for(int i = 1; i <= 17; ++i)
		{
			lua_rawgeti(state, -1, i);
			if(lua_isnil(state, -1))
			{
				lua_pop(state, 1);
				found_material_end = true;
				continue;
			}
			if(found_material_end || i > 16)
			{
				lua_pop(state, 2);
				throw glare::Exception("game.createVoxelObject(): materials must be a dense array of 1..16 color tables" + errorContextString(state));
			}
			material_colours.push_back(read_material_colour(-1));
			lua_pop(state, 1);
		}
	}
	lua_pop(state, 1); // materials
	if(has_materials && material_colours.empty())
		throw glare::Exception("game.createVoxelObject(): materials must contain at least one color table" + errorContextString(state));
	Colour3f single_colour(0.85f);
	lua_getfield(state, 1, "color");
	const bool has_single_colour = !lua_isnil(state, -1);
	if(has_single_colour)
		single_colour = read_material_colour(-1);
	lua_pop(state, 1);
	if(has_materials && has_single_colour)
		throw glare::Exception("game.createVoxelObject(): provide either color or materials, not both" + errorContextString(state));
	if(material_colours.empty()) material_colours.push_back(single_colour);
	for(const Voxel& voxel : voxels)
		if(voxel.mat_index < 0 || (size_t)voxel.mat_index >= material_colours.size())
			throw glare::Exception("game.createVoxelObject(): a cell material index has no matching entry in materials" + errorContextString(state));

	SubstrataLuaVM* sub_lua_vm = (SubstrataLuaVM*)lua_callbacks(state)->userdata;
	LuaScript* script = (LuaScript*)lua_getthreaddata(state);
	LuaScriptEvaluator* script_evaluator = script ? (LuaScriptEvaluator*)script->userdata : nullptr;
	if(!script_evaluator || !script_evaluator->world_object || !script_evaluator->world_state ||
		!script_evaluator->cur_world_state_lock || !sub_lua_vm->server)
	{
		lua_pushnil(state);
		LuaUtils::pushString(state, "Game object creation is unavailable in this script context.");
		return 2;
	}
	WorldStateLock& lock = *script_evaluator->cur_world_state_lock;
	ServerWorldState& world = *script_evaluator->world_state;
	const UID game_uid = script_evaluator->world_object->uid;
	const UserID game_owner = script_evaluator->world_object->creator_id;
	if(!game_uid.valid() || !game_owner.valid() || world.getGameSessionParticipants(game_uid, lock).empty())
	{
		lua_pushnil(state);
		LuaUtils::pushString(state, "The game must have a valid owner and at least one active player before creating objects.");
		return 2;
	}
	if(sub_lua_vm->server->world_state->read_only_mode)
	{
		lua_pushnil(state);
		LuaUtils::pushString(state, "The server is in read-only mode.");
		return 2;
	}
	if(!world.canAddGameSpawnedObjects(game_uid, 1, lock))
	{
		lua_pushnil(state);
		LuaUtils::pushString(state, "The game object limit or creation rate limit has been reached.");
		return 2;
	}
	if(!world.canAddGameSpawnedVoxelCells(game_uid, voxels.size(), lock))
	{
		lua_pushnil(state);
		LuaUtils::pushString(state, "The per-game or per-world voxel geometry budget has been reached.");
		return 2;
	}

	WorldObjectRef spawned = new WorldObject();
	spawned->object_type = WorldObject::ObjectType_VoxelGroup;
	spawned->pos = destination;
	spawned->axis = Vec3f(0,0,1);
	spawned->angle = 0.f;
	spawned->scale = scale;
	spawned->setCollidable(collidable);
	spawned->transformChanged();
	spawned->setDynamic(false);
	spawned->physics_owner_id = std::numeric_limits<uint32>::max();
	spawned->last_physics_ownership_change_global_time = 0;
	for(const Colour3f& colour : material_colours)
	{
		WorldMaterialRef material = new WorldMaterial();
		material->colour_rgb = colour;
		spawned->materials.push_back(material);
	}
	for(const Voxel& voxel : voxels) spawned->getDecompressedVoxels().push_back(voxel);
	spawned->setAABBOS(spawned->getDecompressedVoxelGroup().getAABB());
	spawned->compressVoxels();
	spawned->creator_id = game_owner;
	spawned->creator_name = script_evaluator->world_object->creator_name;
	spawned->created_time = spawned->last_modified_time = TimeStamp::currentTime();
	spawned->game_spawn_owner_uid = game_uid;
	if(!userHasObjectCreationPermissions(*spawned, game_owner, world, lock))
	{
		lua_pushnil(state);
		LuaUtils::pushString(state, "The game owner does not have permission to place an object at that position.");
		return 2;
	}
	spawned->uid = sub_lua_vm->server->world_state->getNextObjectUID();
	spawned->state = WorldObject::State_JustCreated;
	spawned->from_remote_other_dirty = true;
	if(!world.addGameSpawnedVoxelObject(game_uid, spawned->uid, voxels.size(), lock))
	{
		lua_pushnil(state);
		LuaUtils::pushString(state, "The game object limit has been reached.");
		return 2;
	}
	auto& objects = world.getObjects(lock);
	world.getDirtyFromRemoteObjects(lock).insert(spawned);
	objects.insert(std::make_pair(spawned->uid, spawned));
	world.refreshGameRaySpatialIndexObject(*spawned, lock);
	markLODChunkAsNeedsRebuildForChangedObject(&world, spawned.ptr(), lock);
	sub_lua_vm->server->world_state->markAsChanged();
	script_evaluator->pushWorldObjectTableOntoStack(spawned->uid);
	return 1;
}

// Spawn a transient visual object by cloning a template in the same world.
// Game scripts cannot inject model URLs/scripts or create arbitrary executable objects.
static bool canUseGameSpawnTemplate(const WorldObject& template_object, const UserID& game_owner,
	ServerWorldState& world, WorldStateLock& lock, std::string& error_out)
{
	if(template_object.object_type != WorldObject::ObjectType_Generic &&
		template_object.object_type != WorldObject::ObjectType_VoxelGroup &&
		template_object.object_type != WorldObject::ObjectType_Text)
	{
		error_out = "Only generic, voxel, and text objects can be used as game templates.";
		return false;
	}
	if(!template_object.scale.isFinite() || template_object.scale.x <= 0 || template_object.scale.y <= 0 || template_object.scale.z <= 0 ||
		template_object.scale.x > 100 || template_object.scale.y > 100 || template_object.scale.z > 100)
	{
		error_out = "The template has an invalid or oversized scale.";
		return false;
	}
	if(!ObjectCopyPermissions::canCopyObject(template_object, game_owner, world.details.owner_id, world.getParcels(lock)))
	{
		error_out = "The game owner does not have permission to use this object as a template.";
		return false;
	}
	return true;
}


static size_t getGameSpawnTemplateVoxelCellCount(const WorldObject& object)
{
	if(object.object_type != WorldObject::ObjectType_VoxelGroup)
		return 0;
	const glare::AllocatorVector<Voxel, 16>& decompressed = object.getDecompressedVoxelGroup().voxels;
	if(!decompressed.empty())
		return decompressed.size();
	const Reference<glare::SharedImmutableArray<uint8> >& compressed = object.getCompressedVoxels();
	if(compressed.nonNull() && compressed->size() > 0)
	{
		VoxelGroup group;
		WorldObject::decompressVoxelGroup(compressed->data(), compressed->size(), /*mem_allocator=*/NULL, group);
		return group.voxels.size();
	}
	return object.getDecompressedVoxelGroup().voxels.size();
}


static WorldObjectRef makeGameSpawnCopy(const WorldObject& template_object, const UID& game_uid, const UserID& game_owner,
	const std::string& game_owner_name, const Vec3d& destination, ServerAllWorldsState& all_world_state,
	ServerWorldState& world, WorldStateLock& lock, std::string& error_out)
{
	if(!destination.isFinite() || std::abs(destination.x) > 1.0e7 || std::abs(destination.y) > 1.0e7 || std::abs(destination.z) > 1.0e7)
	{
		error_out = "Spawn position is outside the allowed world bounds.";
		return WorldObjectRef();
	}
	if(!canUseGameSpawnTemplate(template_object, game_owner, world, lock, error_out))
		return WorldObjectRef();
	WorldObjectRef spawned = new WorldObject();
	spawned->copyNetworkStateFrom(template_object);
	for(auto& material : spawned->materials)
		if(material.nonNull()) material = material->clone();
	spawned->pos = destination;
	spawned->transformChanged();
	spawned->script.clear();
	spawned->target_url.clear();
	spawned->lightmap_url = URLString();
	spawned->chunk_batch0_start = spawned->chunk_batch0_end = spawned->chunk_batch1_start = spawned->chunk_batch1_end = 0;
	spawned->flags &= ~WorldObject::SUMMONED_FLAG;
	spawned->setDynamic(false);
	spawned->physics_owner_id = std::numeric_limits<uint32>::max();
	spawned->last_physics_ownership_change_global_time = 0;
	spawned->creator_id = game_owner;
	spawned->creator_name = game_owner_name;
	spawned->created_time = spawned->last_modified_time = TimeStamp::currentTime();
	spawned->game_spawn_owner_uid = game_uid;
	if(!userHasObjectCreationPermissions(*spawned, game_owner, world, lock))
	{
		error_out = "The game owner does not have permission to place an object at that position.";
		return WorldObjectRef();
	}
	spawned->uid = all_world_state.getNextObjectUID();
	spawned->state = WorldObject::State_JustCreated;
	spawned->from_remote_other_dirty = true;
	return spawned;
}


static int gameSpawnObject(lua_State* state)
{
	checkNumArgs(state, /*num_args_required=*/2);
	if(!lua_istable(state, 1))
		throw glare::Exception("game.spawnObject(): arg 1 must be a world object template" + errorContextString(state));

	const UID template_uid((uint64)LuaUtils::getTableNumberField(state, /*table index=*/1, "uid"));
	const Vec3d destination = LuaUtils::getVec3d(state, /*index=*/2);
	SubstrataLuaVM* sub_lua_vm = (SubstrataLuaVM*)lua_callbacks(state)->userdata;
	LuaScript* script = (LuaScript*)lua_getthreaddata(state);
	LuaScriptEvaluator* script_evaluator = script ? (LuaScriptEvaluator*)script->userdata : nullptr;
	if(!script_evaluator || !script_evaluator->world_object || !script_evaluator->world_state ||
		!script_evaluator->cur_world_state_lock || !sub_lua_vm->server)
	{
		lua_pushnil(state);
		LuaUtils::pushString(state, "Game object creation is unavailable in this script context.");
		return 2;
	}

	WorldStateLock& lock = *script_evaluator->cur_world_state_lock;
	ServerWorldState& world = *script_evaluator->world_state;
	const UID game_uid = script_evaluator->world_object->uid;
	const UserID game_owner = script_evaluator->world_object->creator_id;
	if(!game_uid.valid() || !game_owner.valid())
	{
		lua_pushnil(state);
		LuaUtils::pushString(state, "The game object must have a valid owner before it can create objects.");
		return 2;
	}
	if(world.getGameSessionParticipants(game_uid, lock).empty())
	{
		lua_pushnil(state);
		LuaUtils::pushString(state, "Join the game before spawning game objects.");
		return 2;
	}
	// Script evaluators run while the global world-state mutex is already held;
	// read the guarded field under that lock instead of calling the locking getter.
	if(sub_lua_vm->server->world_state->read_only_mode)
	{
		lua_pushnil(state);
		LuaUtils::pushString(state, "The server is in read-only mode.");
		return 2;
	}

	auto& objects = world.getObjects(lock);
	const auto template_it = objects.find(template_uid);
	if(template_it == objects.end() || template_it->second->state == WorldObject::State_Dead)
	{
		lua_pushnil(state);
		LuaUtils::pushString(state, "The game object template no longer exists.");
		return 2;
	}
	const WorldObject& template_object = *template_it->second;
	if(!world.canAddGameSpawnedObjects(game_uid, 1, lock))
	{
		lua_pushnil(state);
		LuaUtils::pushString(state, "The game object limit or creation rate limit has been reached.");
		return 2;
	}
	const size_t voxel_cell_count = getGameSpawnTemplateVoxelCellCount(template_object);
	if(voxel_cell_count > 0 && !world.canAddGameSpawnedVoxelCells(game_uid, voxel_cell_count, lock))
	{
		lua_pushnil(state);
		LuaUtils::pushString(state, "The per-game or per-world voxel geometry budget has been reached.");
		return 2;
	}
	std::string spawn_error;
	WorldObjectRef spawned = makeGameSpawnCopy(template_object, game_uid, game_owner,
		script_evaluator->world_object->creator_name, destination, *sub_lua_vm->server->world_state, world, lock, spawn_error);
	if(spawned.isNull())
	{
		lua_pushnil(state);
		LuaUtils::pushString(state, spawn_error);
		return 2;
	}
	const bool registered = voxel_cell_count > 0 ?
		world.addGameSpawnedVoxelObject(game_uid, spawned->uid, voxel_cell_count, lock) :
		world.addGameSpawnedObject(game_uid, spawned->uid, lock);
	if(!registered)
	{
		lua_pushnil(state);
		LuaUtils::pushString(state, "The game object limit has been reached.");
		return 2;
	}
	world.getDirtyFromRemoteObjects(lock).insert(spawned);
	objects.insert(std::make_pair(spawned->uid, spawned));
	world.refreshGameRaySpatialIndexObject(*spawned, lock);
	markLODChunkAsNeedsRebuildForChangedObject(&world, spawned.ptr(), lock);
	sub_lua_vm->server->world_state->markAsChanged();
	script_evaluator->pushWorldObjectTableOntoStack(spawned->uid);
	return 1;
}


// Spawn a bounded set of transient copies as one placement operation. Each
// entry names an existing template, a local XYZ offset, and optional transform overrides.
static int gameSpawnGroup(lua_State* state)
{
	checkNumArgs(state, /*num_args_required=*/2);
	if(!lua_istable(state, 1))
		throw glare::Exception("game.spawnGroup(): arg 1 must be a dense array of {template=object, offset={x,y,z}, axis={x,y,z}, angle=radians, scale={x,y,z}} entries" + errorContextString(state));
	const Vec3d origin = LuaUtils::getVec3d(state, /*index=*/2);
	if(!origin.isFinite() || std::abs(origin.x) > 1.0e7 || std::abs(origin.y) > 1.0e7 || std::abs(origin.z) > 1.0e7)
	{
		lua_pushnil(state);
		LuaUtils::pushString(state, "Group origin is outside the allowed world bounds.");
		return 2;
	}
	std::vector<UID> template_uids;
	std::vector<Vec3d> offsets;
	std::vector<bool> has_rotations;
	std::vector<Vec3f> rotation_axes;
	std::vector<float> rotation_angles;
	std::vector<bool> has_scales;
	std::vector<Vec3f> scales;
	bool found_array_end = false;
	for(int i = 1; i <= 65; ++i)
	{
		lua_rawgeti(state, /*table index=*/1, i);
		if(lua_isnil(state, -1))
		{
			lua_pop(state, 1);
			found_array_end = true;
			continue;
		}
		if(found_array_end)
		{
			lua_pop(state, 1);
			throw glare::Exception("game.spawnGroup(): entries must be a dense array" + errorContextString(state));
		}
		if(i > 64)
		{
			lua_pop(state, 1);
			throw glare::Exception("game.spawnGroup(): at most 64 objects may be created by one call" + errorContextString(state));
		}
		if(!lua_istable(state, -1))
		{
			lua_pop(state, 1);
			throw glare::Exception("game.spawnGroup(): each entry must be a table" + errorContextString(state));
		}
		lua_getfield(state, -1, "template");
		if(!lua_istable(state, -1))
		{
			lua_pop(state, 2);
			throw glare::Exception("game.spawnGroup(): each entry needs a world object in template" + errorContextString(state));
		}
		const double raw_uid = LuaUtils::getTableNumberField(state, /*table index=*/-1, "uid");
		lua_pop(state, 1);
		if(!std::isfinite(raw_uid) || raw_uid < 1 || raw_uid > 9007199254740991.0 || raw_uid != std::floor(raw_uid))
		{
			lua_pop(state, 1);
			throw glare::Exception("game.spawnGroup(): template.uid must be a positive exact integer" + errorContextString(state));
		}
		Vec3d offset(0,0,0);
		lua_getfield(state, -1, "offset");
		if(!lua_isnil(state, -1))
		{
			if(!lua_istable(state, -1))
			{
				lua_pop(state, 2);
				throw glare::Exception("game.spawnGroup(): offset must be an {x,y,z} table" + errorContextString(state));
			}
			offset = Vec3d(LuaUtils::getTableNumberField(state, /*table index=*/-1, "x"),
				LuaUtils::getTableNumberField(state, /*table index=*/-1, "y"),
				LuaUtils::getTableNumberField(state, /*table index=*/-1, "z"));
		}
		lua_pop(state, 2); // offset and entry
		if(!offset.isFinite() || std::abs(offset.x) > 10000 || std::abs(offset.y) > 10000 || std::abs(offset.z) > 10000)
			throw glare::Exception("game.spawnGroup(): offsets must be finite and within +/-10,000 metres" + errorContextString(state));
		bool has_rotation = false;
		Vec3f rotation_axis(0.f, 0.f, 1.f);
		float rotation_angle = 0.f;
		lua_rawgeti(state, /*table index=*/1, i);
		lua_getfield(state, -1, "axis");
		if(!lua_isnil(state, -1))
		{
			if(!lua_istable(state, -1))
			{
				lua_pop(state, 2);
				throw glare::Exception("game.spawnGroup(): axis must be a vector table {x,y,z}" + errorContextString(state));
			}
			rotation_axis = Vec3f((float)LuaUtils::getTableNumberField(state, -1, "x"),
				(float)LuaUtils::getTableNumberField(state, -1, "y"), (float)LuaUtils::getTableNumberField(state, -1, "z"));
			has_rotation = true;
		}
		lua_pop(state, 1); // axis
		lua_getfield(state, -1, "angle");
		bool has_angle = false;
		if(!lua_isnil(state, -1))
		{
			if(!lua_isnumber(state, -1))
			{
				lua_pop(state, 2);
				throw glare::Exception("game.spawnGroup(): angle must be a number in radians" + errorContextString(state));
			}
			rotation_angle = (float)lua_tonumber(state, -1);
			has_angle = true;
			if(!has_rotation)
			{
				lua_pop(state, 2);
				throw glare::Exception("game.spawnGroup(): angle requires an axis vector" + errorContextString(state));
			}
		}
		lua_pop(state, 1); // angle
		const float axis_length = rotation_axis.length();
		if(has_rotation != has_angle)
		{
			lua_pop(state, 1); // entry
			throw glare::Exception("game.spawnGroup(): provide axis and angle together" + errorContextString(state));
		}
		if(has_rotation && (!rotation_axis.isFinite() || !isFinite(rotation_angle) ||
			axis_length < 1.0e-6f || axis_length > 10000.f || std::abs(rotation_angle) > 6.2831854f))
		{
			lua_pop(state, 1); // entry
			throw glare::Exception("game.spawnGroup(): axis must be non-zero and angle must be within -2*pi..2*pi radians" + errorContextString(state));
		}
		if(has_rotation) rotation_axis *= 1.f / axis_length;
		bool has_scale = false;
		Vec3f instance_scale(1.f, 1.f, 1.f);
		lua_getfield(state, -1, "scale");
		if(!lua_isnil(state, -1))
		{
			if(!lua_istable(state, -1))
			{
				lua_pop(state, 2);
				throw glare::Exception("game.spawnGroup(): scale must be a vector table {x,y,z}" + errorContextString(state));
			}
			instance_scale = Vec3f((float)LuaUtils::getTableNumberField(state, -1, "x"),
				(float)LuaUtils::getTableNumberField(state, -1, "y"), (float)LuaUtils::getTableNumberField(state, -1, "z"));
			has_scale = true;
		}
		lua_pop(state, 2); // scale and entry
		if(has_scale && (!instance_scale.isFinite() || instance_scale.x < 0.01f || instance_scale.y < 0.01f || instance_scale.z < 0.01f ||
			instance_scale.x > 100.f || instance_scale.y > 100.f || instance_scale.z > 100.f))
			throw glare::Exception("game.spawnGroup(): scale components must be finite and within 0.01..100" + errorContextString(state));
		template_uids.push_back(UID((uint64)raw_uid));
		offsets.push_back(offset);
		has_rotations.push_back(has_rotation);
		rotation_axes.push_back(rotation_axis);
		rotation_angles.push_back(rotation_angle);
		has_scales.push_back(has_scale);
		scales.push_back(instance_scale);
	}
	if(template_uids.empty())
		throw glare::Exception("game.spawnGroup(): provide 1-64 entries" + errorContextString(state));

	SubstrataLuaVM* sub_lua_vm = (SubstrataLuaVM*)lua_callbacks(state)->userdata;
	LuaScript* script = (LuaScript*)lua_getthreaddata(state);
	LuaScriptEvaluator* script_evaluator = script ? (LuaScriptEvaluator*)script->userdata : nullptr;
	if(!script_evaluator || !script_evaluator->world_object || !script_evaluator->world_state ||
		!script_evaluator->cur_world_state_lock || !sub_lua_vm->server)
	{
		lua_pushnil(state);
		LuaUtils::pushString(state, "Game object creation is unavailable in this script context.");
		return 2;
	}
	WorldStateLock& lock = *script_evaluator->cur_world_state_lock;
	ServerWorldState& world = *script_evaluator->world_state;
	const UID game_uid = script_evaluator->world_object->uid;
	const UserID game_owner = script_evaluator->world_object->creator_id;
	if(!game_uid.valid() || !game_owner.valid())
	{
		lua_pushnil(state);
		LuaUtils::pushString(state, "The game object must have a valid owner before it can create objects.");
		return 2;
	}
	if(world.getGameSessionParticipants(game_uid, lock).empty())
	{
		lua_pushnil(state);
		LuaUtils::pushString(state, "Join the game before spawning game objects.");
		return 2;
	}
	// The evaluator already holds the global world-state mutex.
	if(sub_lua_vm->server->world_state->read_only_mode)
	{
		lua_pushnil(state);
		LuaUtils::pushString(state, "The server is in read-only mode.");
		return 2;
	}

	auto& objects = world.getObjects(lock);
	if(!world.canAddGameSpawnedObjects(game_uid, template_uids.size(), lock))
	{
		lua_pushnil(state);
		LuaUtils::pushString(state, "The game object limit or creation rate limit cannot fit this full group.");
		return 2;
	}
	std::vector<size_t> template_voxel_counts(template_uids.size(), 0);
	std::map<UID, size_t> voxel_count_cache;
	size_t total_voxel_cells = 0;
	for(size_t i = 0; i < template_uids.size(); ++i)
	{
		const auto template_it = objects.find(template_uids[i]);
		if(template_it == objects.end() || template_it->second->state == WorldObject::State_Dead)
		{
			lua_pushnil(state);
			LuaUtils::pushString(state, "A game object template no longer exists; no group objects were placed.");
			return 2;
		}
		const auto cached_it = voxel_count_cache.find(template_uids[i]);
		const size_t cell_count = cached_it != voxel_count_cache.end() ? cached_it->second :
			getGameSpawnTemplateVoxelCellCount(*template_it->second);
		voxel_count_cache[template_uids[i]] = cell_count;
		template_voxel_counts[i] = cell_count;
		if(total_voxel_cells > std::numeric_limits<size_t>::max() - cell_count)
		{
			lua_pushnil(state);
			LuaUtils::pushString(state, "The full group exceeds the bounded voxel geometry budget.");
			return 2;
		}
		total_voxel_cells += cell_count;
	}
	if(total_voxel_cells > 0 && !world.canAddGameSpawnedVoxelCells(game_uid, total_voxel_cells, lock))
	{
		lua_pushnil(state);
		LuaUtils::pushString(state, "The per-game or per-world voxel geometry budget cannot fit this full group.");
		return 2;
	}
	std::vector<WorldObjectRef> spawned_objects;
	spawned_objects.reserve(template_uids.size());
	for(size_t i = 0; i < template_uids.size(); ++i)
	{
		const auto template_it = objects.find(template_uids[i]);
		if(template_it == objects.end() || template_it->second->state == WorldObject::State_Dead)
		{
			lua_pushnil(state);
			LuaUtils::pushString(state, "A game object template no longer exists; no group objects were placed.");
			return 2;
		}
		const Vec3d destination = origin + offsets[i];
		std::string spawn_error;
		WorldObjectRef spawned = makeGameSpawnCopy(*template_it->second, game_uid, game_owner,
			script_evaluator->world_object->creator_name, destination, *sub_lua_vm->server->world_state, world, lock, spawn_error);
		if(spawned.isNull())
		{
			lua_pushnil(state);
			LuaUtils::pushString(state, spawn_error + " No group objects were placed.");
			return 2;
		}
		if(has_rotations[i])
		{
			spawned->axis = rotation_axes[i];
			spawned->angle = rotation_angles[i];
		}
		if(has_scales[i])
			spawned->scale = scales[i];
		if(has_rotations[i] || has_scales[i])
			spawned->transformChanged();
		spawned_objects.push_back(spawned);
	}
	std::vector<std::pair<UID, size_t>> spawned_cell_counts;
	spawned_cell_counts.reserve(spawned_objects.size());
	for(size_t i = 0; i < spawned_objects.size(); ++i)
		spawned_cell_counts.push_back(std::make_pair(spawned_objects[i]->uid, template_voxel_counts[i]));
	if(!world.addGameSpawnedVoxelObjects(game_uid, spawned_cell_counts, lock))
	{
		lua_pushnil(state);
		LuaUtils::pushString(state, "The game object limit does not have capacity for this full group.");
		return 2;
	}
	for(const WorldObjectRef& object : spawned_objects)
	{
		world.getDirtyFromRemoteObjects(lock).insert(object);
		objects.insert(std::make_pair(object->uid, object));
		world.refreshGameRaySpatialIndexObject(*object, lock);
		markLODChunkAsNeedsRebuildForChangedObject(&world, object.ptr(), lock);
	}
	sub_lua_vm->server->world_state->markAsChanged();
	lua_createtable(state, (int)spawned_objects.size(), 0);
	for(size_t i = 0; i < spawned_objects.size(); ++i)
	{
		script_evaluator->pushWorldObjectTableOntoStack(spawned_objects[i]->uid);
		lua_rawseti(state, -2, (int)i + 1);
	}
	return 1;
}


// Destroy only transient objects spawned by this exact game object.
static int gameDestroyObject(lua_State* state)
{
	checkNumArgs(state, /*num_args_required=*/1);
	if(!lua_istable(state, 1))
		throw glare::Exception("game.destroyObject(): arg 1 must be a game object" + errorContextString(state));

	const UID object_uid((uint64)LuaUtils::getTableNumberField(state, /*table index=*/1, "uid"));
	SubstrataLuaVM* sub_lua_vm = (SubstrataLuaVM*)lua_callbacks(state)->userdata;
	LuaScript* script = (LuaScript*)lua_getthreaddata(state);
	LuaScriptEvaluator* script_evaluator = script ? (LuaScriptEvaluator*)script->userdata : nullptr;
	if(!script_evaluator || !script_evaluator->world_object || !script_evaluator->world_state || !script_evaluator->cur_world_state_lock)
	{
		lua_pushboolean(state, 0);
		return 1;
	}

	WorldStateLock& lock = *script_evaluator->cur_world_state_lock;
	ServerWorldState& world = *script_evaluator->world_state;
	// Keep transient game-object deletion consistent with other server writes.
	// The evaluator holds the world-state lock, so read the guarded server flag directly.
	if(!sub_lua_vm || !sub_lua_vm->server)
	{
		lua_pushboolean(state, 0);
		LuaUtils::pushString(state, "Game object removal requires an authoritative server script context.");
		return 2;
	}
	if(sub_lua_vm->server->world_state->read_only_mode)
	{
		lua_pushboolean(state, 0);
		LuaUtils::pushString(state, "Game objects cannot be destroyed while the server is in read-only mode.");
		return 2;
	}
	auto& objects = world.getObjects(lock);
	auto object_it = objects.find(object_uid);
	if(object_it == objects.end() || object_it->second->state == WorldObject::State_Dead ||
		object_it->second->game_spawn_owner_uid != script_evaluator->world_object->uid)
	{
		lua_pushboolean(state, 0);
		return 1;
	}

	WorldObjectRef object = object_it->second;
	object->state = WorldObject::State_Dead;
	object->from_remote_other_dirty = true;
	world.removeGameSpawnedObject(script_evaluator->world_object->uid, object_uid, lock);
	world.getDirtyFromRemoteObjects(lock).insert(object);
	markLODChunkAsNeedsRebuildForChangedObject(&world, object.ptr(), lock);
	script_evaluator->substrata_lua_vm->server->world_state->markAsChanged();
	lua_pushboolean(state, 1);
	return 1;
}


// Remove every transient object owned by this game host. Unlike deleting the
// host itself, this preserves the game's rate-limit windows and saved data.
static int gameClearSpawnedObjects(lua_State* state)
{
	if(lua_gettop(state) != 0)
		throw glare::Exception("game.clearSpawnedObjects(): expected no arguments" + errorContextString(state));
	LuaScript* script = (LuaScript*)lua_getthreaddata(state);
	LuaScriptEvaluator* script_evaluator = script ? (LuaScriptEvaluator*)script->userdata : nullptr;
	SubstrataLuaVM* sub_lua_vm = (SubstrataLuaVM*)lua_callbacks(state)->userdata;
	if(!script_evaluator || !script_evaluator->world_object || !script_evaluator->world_state ||
		!script_evaluator->cur_world_state_lock || !sub_lua_vm || !sub_lua_vm->server)
	{
		lua_pushboolean(state, 0);
		LuaUtils::pushString(state, "game.clearSpawnedObjects() requires an authoritative server script context.");
		return 2;
	}
	if(sub_lua_vm->server->world_state->read_only_mode)
	{
		lua_pushboolean(state, 0);
		LuaUtils::pushString(state, "Game objects cannot be destroyed while the server is in read-only mode.");
		return 2;
	}

	WorldStateLock& lock = *script_evaluator->cur_world_state_lock;
	ServerWorldState& world = *script_evaluator->world_state;
	const UID game_uid = script_evaluator->world_object->uid;
	const std::vector<UID> spawned_uids = world.getGameSpawnedObjectUIDs(game_uid, lock);
	auto& objects = world.getObjects(lock);
	uint32 removed_count = 0;
	for(const UID& uid : spawned_uids)
	{
		const auto it = objects.find(uid);
		if(it == objects.end() || it->second->state == WorldObject::State_Dead ||
			it->second->game_spawn_owner_uid != game_uid)
		{
			world.removeGameSpawnedObject(game_uid, uid, lock);
			continue;
		}
		WorldObjectRef object = it->second;
		object->state = WorldObject::State_Dead;
		object->from_remote_other_dirty = true;
		world.getDirtyFromRemoteObjects(lock).insert(object);
		markLODChunkAsNeedsRebuildForChangedObject(&world, object.ptr(), lock);
		world.removeGameSpawnedObject(game_uid, uid, lock);
		removed_count++;
	}
	if(removed_count > 0)
		sub_lua_vm->server->world_state->markAsChanged();
	lua_pushboolean(state, 1);
	lua_pushinteger(state, (int)removed_count);
	return 2;
}


// Apply an authoritative transform only to an object spawned by this game.
// The permission check uses a candidate copy so denied moves never mutate world state.
static int gameSetObjectTransform(lua_State* state)
{
	checkNumArgs(state, /*num_args_required=*/2);
	if(!lua_istable(state, 1) || !lua_istable(state, 2))
		throw glare::Exception("game.setObjectTransform(): expected a game object and a transform table {position={x,y,z}, axis={x,y,z}, angle=radians, scale={x,y,z}}" + errorContextString(state));
	const double raw_uid = LuaUtils::getTableNumberField(state, /*table index=*/1, "uid");
	if(!std::isfinite(raw_uid) || raw_uid < 1 || raw_uid > 9007199254740991.0 || raw_uid != std::floor(raw_uid))
		throw glare::Exception("game.setObjectTransform(): object.uid must be a positive exact integer" + errorContextString(state));
	LuaScript* script = (LuaScript*)lua_getthreaddata(state);
	LuaScriptEvaluator* script_evaluator = script ? (LuaScriptEvaluator*)script->userdata : nullptr;
	SubstrataLuaVM* sub_lua_vm = (SubstrataLuaVM*)lua_callbacks(state)->userdata;
	if(!script_evaluator || !script_evaluator->world_object || !script_evaluator->world_state ||
		!script_evaluator->cur_world_state_lock || !sub_lua_vm->server)
	{
		lua_pushboolean(state, 0);
		LuaUtils::pushString(state, "Game object transforms require the authoritative server script context.");
		return 2;
	}
	const UID game_uid = script_evaluator->world_object->uid;
	const UID object_uid((uint64)raw_uid);
	WorldStateLock& lock = *script_evaluator->cur_world_state_lock;
	ServerWorldState& world = *script_evaluator->world_state;
	if(sub_lua_vm->server->world_state->read_only_mode)
	{
		lua_pushboolean(state, 0);
		LuaUtils::pushString(state, "The server is in read-only mode.");
		return 2;
	}
	auto& objects = world.getObjects(lock);
	auto object_it = objects.find(object_uid);
	if(object_it == objects.end() || object_it->second->state == WorldObject::State_Dead ||
		object_it->second->game_spawn_owner_uid != game_uid)
	{
		lua_pushboolean(state, 0);
		LuaUtils::pushString(state, "game.setObjectTransform() accepts only a live object spawned by this game.");
		return 2;
	}
	WorldObject& object = *object_it->second;
	if(!lua_istable(state, 2))
		throw glare::Exception("game.setObjectTransform(): transform must be a table" + errorContextString(state));
	lua_getfield(state, 2, "position");
	if(!lua_istable(state, -1))
	{
		lua_pop(state, 1);
		throw glare::Exception("game.setObjectTransform(): transform.position must be an {x,y,z} table" + errorContextString(state));
	}
	const Vec3d position(LuaUtils::getTableNumberField(state, /*table index=*/-1, "x"),
		LuaUtils::getTableNumberField(state, /*table index=*/-1, "y"),
		LuaUtils::getTableNumberField(state, /*table index=*/-1, "z"));
	lua_pop(state, 1);
	if(!position.isFinite() || std::abs(position.x) > 1.0e7 || std::abs(position.y) > 1.0e7 || std::abs(position.z) > 1.0e7)
		throw glare::Exception("game.setObjectTransform(): position must be finite and within +/-10,000,000 metres" + errorContextString(state));
	Vec3f axis = object.axis;
	float angle = object.angle;
	Vec3f scale = object.scale;
	lua_getfield(state, 2, "axis");
	const bool has_axis = !lua_isnil(state, -1);
	if(has_axis)
	{
		if(!lua_istable(state, -1))
		{
			lua_pop(state, 1);
			throw glare::Exception("game.setObjectTransform(): axis must be an {x,y,z} table" + errorContextString(state));
		}
		axis = Vec3f((float)LuaUtils::getTableNumberField(state, /*table index=*/-1, "x"),
			(float)LuaUtils::getTableNumberField(state, /*table index=*/-1, "y"),
			(float)LuaUtils::getTableNumberField(state, /*table index=*/-1, "z"));
	}
	lua_pop(state, 1);
	lua_getfield(state, 2, "angle");
	const bool has_angle = !lua_isnil(state, -1);
	if(has_angle) angle = (float)LuaUtils::getDoubleArg(state, /*index=*/-1);
	lua_pop(state, 1);
	if(has_axis != has_angle)
		throw glare::Exception("game.setObjectTransform(): axis and angle must be provided together" + errorContextString(state));
	if(has_axis)
	{
		const double axis_length = std::sqrt((double)axis.x*axis.x + (double)axis.y*axis.y + (double)axis.z*axis.z);
		if(!axis.isFinite() || !std::isfinite(axis_length) || axis_length < 1.0e-8 || !std::isfinite(angle) || std::abs(angle) > 6.283185307179586)
			throw glare::Exception("game.setObjectTransform(): rotation axis must be finite and non-zero; angle must be finite and within +/-2*pi" + errorContextString(state));
		axis = normalise(axis);
	}
	lua_getfield(state, 2, "scale");
	if(!lua_isnil(state, -1))
	{
		if(!lua_istable(state, -1))
		{
			lua_pop(state, 1);
			throw glare::Exception("game.setObjectTransform(): scale must be an {x,y,z} table" + errorContextString(state));
		}
		scale = Vec3f((float)LuaUtils::getTableNumberField(state, /*table index=*/-1, "x"),
			(float)LuaUtils::getTableNumberField(state, /*table index=*/-1, "y"),
			(float)LuaUtils::getTableNumberField(state, /*table index=*/-1, "z"));
	}
	lua_pop(state, 1);
	if(!scale.isFinite() || scale.x <= 0 || scale.y <= 0 || scale.z <= 0 || scale.x > 100 || scale.y > 100 || scale.z > 100)
		throw glare::Exception("game.setObjectTransform(): scale components must be finite and between 0 and 100" + errorContextString(state));
	WorldObject candidate;
	candidate.copyNetworkStateFrom(object);
	candidate.setTransformAndHistory(position, axis, angle);
	candidate.scale = scale;
	candidate.transformChanged();
	if(!userHasObjectCreationPermissions(candidate, script_evaluator->world_object->creator_id, world, lock))
	{
		lua_pushboolean(state, 0);
		LuaUtils::pushString(state, "The game owner does not have permission to move the object to that position.");
		return 2;
	}
	if(!world.allowGameObjectTransformUpdate(game_uid, lock))
	{
		lua_pushboolean(state, 0);
		LuaUtils::pushString(state, "The game reached its 600 object-transform updates per second limit.");
		return 2;
	}
	object.setTransformAndHistory(position, axis, angle);
	object.scale = scale;
	object.transformChanged();
	world.refreshGameRaySpatialIndexObject(object, lock);
	object.last_transform_update_avatar_uid = std::numeric_limits<uint32>::max();
	object.from_remote_transform_dirty = true;
	world.getDirtyFromRemoteObjects(lock).insert(object_it->second);
	markLODChunkAsNeedsRebuildForChangedObject(&world, &object, lock);
	sub_lua_vm->server->world_state->markAsChanged();
	lua_pushboolean(state, 1);
	return 1;
}


// Return the game-owned transient objects whose world-space bounds overlap
// the supplied game object. This is a bounded server-side broad-phase query,
// not a physics contact manifold or an avatar collision event.
static int gameGetOverlaps(lua_State* state)
{
	checkNumArgs(state, /*num_args_required=*/1);
	if(!lua_istable(state, 1))
		throw glare::Exception("game.getOverlaps(): arg 1 must be a game object" + errorContextString(state));
	const double raw_uid = LuaUtils::getTableNumberField(state, /*table index=*/1, "uid");
	if(!std::isfinite(raw_uid) || raw_uid < 1 || raw_uid > 9007199254740991.0 || raw_uid != std::floor(raw_uid))
		throw glare::Exception("game.getOverlaps(): object.uid must be a positive exact integer" + errorContextString(state));
	LuaScript* script = (LuaScript*)lua_getthreaddata(state);
	LuaScriptEvaluator* script_evaluator = script ? (LuaScriptEvaluator*)script->userdata : nullptr;
	if(!script_evaluator || !script_evaluator->world_object || !script_evaluator->world_state || !script_evaluator->cur_world_state_lock)
	{
		lua_pushnil(state);
		LuaUtils::pushString(state, "Game overlap queries require the authoritative server script context.");
		return 2;
	}
	WorldStateLock& lock = *script_evaluator->cur_world_state_lock;
	ServerWorldState& world = *script_evaluator->world_state;
	const UID game_uid = script_evaluator->world_object->uid;
	auto& objects = world.getObjects(lock);
	const UID target_uid((uint64)raw_uid);
	const auto target_it = objects.find(target_uid);
	if(target_it == objects.end() || target_it->second->state == WorldObject::State_Dead ||
		target_it->second->game_spawn_owner_uid != game_uid)
	{
		lua_pushnil(state);
		LuaUtils::pushString(state, "game.getOverlaps() accepts only a live object spawned by this game.");
		return 2;
	}
	if(!world.allowGameSpatialQuery(game_uid, lock))
	{
		lua_pushnil(state);
		LuaUtils::pushString(state, "game.getOverlaps(): spatial-query budget reached (60 per second per game, 600 per second shared across this world).");
		return 2;
	}
	const js::AABBox target_bounds = target_it->second->getAABBWS();
	const std::vector<UID> spawned_uids = world.getGameSpawnedObjectUIDs(game_uid, lock);
	std::vector<UID> overlapping_uids;
	overlapping_uids.reserve(16);
	for(const UID& uid : spawned_uids)
	{
		if(uid == target_uid) continue;
		const auto object_it = objects.find(uid);
		if(object_it == objects.end() || object_it->second->state == WorldObject::State_Dead) continue;
		if(object_it->second->getAABBWS().intersectsAABB(target_bounds))
		{
			if(overlapping_uids.size() >= 256)
			{
				lua_pushnil(state);
				LuaUtils::pushString(state, "game.getOverlaps(): result exceeds the 256-object limit; reduce the active group size or split the query.");
				return 2;
			}
			overlapping_uids.push_back(uid);
		}
	}
	lua_createtable(state, (int)overlapping_uids.size(), 0);
	for(size_t i = 0; i < overlapping_uids.size(); ++i)
	{
		script_evaluator->pushWorldObjectTableOntoStack(overlapping_uids[i]);
		lua_rawseti(state, -2, (int)i + 1);
	}
	return 1;
}


// Ray against a bounded AABB, returning the first entry distance and face normal.
static bool gameRayAABB(const js::AABBox& bounds, const Vec3d& origin, const Vec3d& direction,
	double max_distance, double& distance_out, Vec3d& normal_out)
{
	double near_t = -std::numeric_limits<double>::infinity();
	double far_t = max_distance;
	normal_out = -direction;
	for(int axis = 0; axis < 3; ++axis)
	{
		const double o = origin[axis];
		const double d = direction[axis];
		const double lower = bounds.min_[axis];
		const double upper = bounds.max_[axis];
		if(!std::isfinite(o) || !std::isfinite(d) || !std::isfinite(lower) || !std::isfinite(upper) || lower > upper)
			return false;
		if(std::abs(d) < 1.0e-10)
		{
			// A ray travelling along a face is tangent to the AABB, not
			// entering it. Treat face contact as outside for this axis.
			if(o <= lower || o >= upper) return false;
			continue;
		}
		double t1 = (lower - o) / d;
		double t2 = (upper - o) / d;
		Vec3d entry_normal(0,0,0);
		entry_normal[axis] = -1.0;
		if(t1 > t2)
		{
			std::swap(t1, t2);
			entry_normal[axis] = 1.0;
		}
		if(t1 > near_t)
		{
			near_t = t1;
			normal_out = entry_normal;
		}
		far_t = std::min(far_t, t2);
		if(near_t > far_t) return false;
	}
	if(far_t < 0.0 || near_t > max_distance) return false;
	// If the ray only touches the box at its origin and then leaves, it is
	// moving away from the obstacle. Preserve an immediate hit for rays that
	// start strictly inside a box (far_t > 0).
	if(near_t < 0.0 && far_t <= 1.0e-10) return false;
	distance_out = std::max(0.0, near_t);
	return true;
}


// Sweep a game object's world-space AABB along a displacement. Expanding each
// obstacle by the moving object's half-extents is the conservative AABB
// equivalent of a point ray, and prevents fast game pieces tunnelling through
// thin collidable objects between fixed-step updates.
static int gameSweepAABB(lua_State* state)
{
	checkNumArgs(state, /*num_args_required=*/2);
	if(!lua_istable(state, 1))
		throw glare::Exception("game.sweepAABB(): arg 1 must be a game object" + errorContextString(state));
	const double raw_uid = LuaUtils::getTableNumberField(state, /*table index=*/1, "uid");
	if(!std::isfinite(raw_uid) || raw_uid < 1 || raw_uid > 9007199254740991.0 || raw_uid != std::floor(raw_uid))
		throw glare::Exception("game.sweepAABB(): object.uid must be a positive exact integer" + errorContextString(state));
	const Vec3d displacement = LuaUtils::getVec3d(state, /*index=*/2);
	const double displacement_length = displacement.length();
	if(!displacement.isFinite() || !std::isfinite(displacement_length) || displacement_length > 100.0)
		throw glare::Exception("game.sweepAABB(): displacement must be finite and no longer than 100 metres" + errorContextString(state));
	LuaScript* script = (LuaScript*)lua_getthreaddata(state);
	LuaScriptEvaluator* script_evaluator = script ? (LuaScriptEvaluator*)script->userdata : nullptr;
	if(!script_evaluator || !script_evaluator->world_object || !script_evaluator->world_state || !script_evaluator->cur_world_state_lock)
	{
		lua_pushnil(state);
		LuaUtils::pushString(state, "AABB sweeps require the authoritative server script context.");
		return 2;
	}
	WorldStateLock& lock = *script_evaluator->cur_world_state_lock;
	ServerWorldState& world = *script_evaluator->world_state;
	const UID game_uid = script_evaluator->world_object->uid;
	auto& objects = world.getObjects(lock);
	const UID target_uid((uint64)raw_uid);
	const auto target_it = objects.find(target_uid);
	if(target_it == objects.end() || target_it->second->state == WorldObject::State_Dead ||
		target_it->second->game_spawn_owner_uid != game_uid)
	{
		lua_pushnil(state);
		LuaUtils::pushString(state, "game.sweepAABB() accepts only a live object spawned by this game.");
		return 2;
	}
	if(displacement_length < 1.0e-9)
	{
		lua_pushnil(state);
		return 1;
	}
	if(!world.allowGameCollisionQuery(game_uid, lock) || !world.allowGameSpatialQuery(game_uid, lock))
	{
		lua_pushnil(state);
		LuaUtils::pushString(state, "game.sweepAABB(): query budget reached (collision: 30 per second per game and 200 world scans per second; spatial: 60 per second per game and 600 per second per world).");
		return 2;
	}
	const js::AABBox moving_bounds = target_it->second->getAABBWS();
	const Vec3d origin(
		((double)moving_bounds.min_[0] + moving_bounds.max_[0]) * 0.5,
		((double)moving_bounds.min_[1] + moving_bounds.max_[1]) * 0.5,
		((double)moving_bounds.min_[2] + moving_bounds.max_[2]) * 0.5);
	const Vec3d half_extents(
		((double)moving_bounds.max_[0] - moving_bounds.min_[0]) * 0.5,
		((double)moving_bounds.max_[1] - moving_bounds.min_[1]) * 0.5,
		((double)moving_bounds.max_[2] - moving_bounds.min_[2]) * 0.5);
	const Vec3d direction = displacement / displacement_length;
	const Vec3d sweep_min(
		std::min(origin.x, origin.x + displacement.x) - half_extents.x,
		std::min(origin.y, origin.y + displacement.y) - half_extents.y,
		std::min(origin.z, origin.z + displacement.z) - half_extents.z);
	const Vec3d sweep_max(
		std::max(origin.x, origin.x + displacement.x) + half_extents.x,
		std::max(origin.y, origin.y + displacement.y) + half_extents.y,
		std::max(origin.z, origin.z + displacement.z) + half_extents.z);
	std::vector<UID> candidate_uids;
	if(!world.getGameRaySpatialCandidatesForBounds(sweep_min, sweep_max, candidate_uids, lock))
	{
		lua_pushnil(state);
		LuaUtils::pushString(state, "game.sweepAABB(): the swept region crosses more than 8,192 candidate objects; shorten the displacement or reduce local object density.");
		return 2;
	}
	UID hit_uid;
	double hit_distance = displacement_length;
	Vec3d hit_normal(0,0,0);
	for(const UID& uid : candidate_uids)
	{
		const auto obstacle_it = objects.find(uid);
		if(obstacle_it == objects.end()) continue;
		const WorldObject& obstacle = *obstacle_it->second;
		if(uid == game_uid || uid == target_uid || obstacle.state == WorldObject::State_Dead)
			continue;
		if(obstacle.game_spawn_owner_uid.valid() && obstacle.game_spawn_owner_uid != game_uid) continue;
		if(!obstacle.isCollidable() || (obstacle.flags & WorldObject::IS_SENSOR_FLAG) != 0) continue;
		const js::AABBox obstacle_bounds = obstacle.getAABBWS();
		const Vec3d lower(
			(double)obstacle_bounds.min_[0] - half_extents.x,
			(double)obstacle_bounds.min_[1] - half_extents.y,
			(double)obstacle_bounds.min_[2] - half_extents.z);
		const Vec3d upper(
			(double)obstacle_bounds.max_[0] + half_extents.x,
			(double)obstacle_bounds.max_[1] + half_extents.y,
			(double)obstacle_bounds.max_[2] + half_extents.z);
		const js::AABBox expanded_bounds(
			Vec4f((float)lower.x, (float)lower.y, (float)lower.z, 1.f),
			Vec4f((float)upper.x, (float)upper.y, (float)upper.z, 1.f));
		double distance = 0.0;
		Vec3d normal;
		if(gameRayAABB(expanded_bounds, origin, direction, hit_distance, distance, normal) && distance <= hit_distance)
		{
			hit_uid = uid;
			hit_distance = distance;
			hit_normal = normal;
		}
	}
	if(!hit_uid.valid())
	{
		lua_pushnil(state);
		return 1;
	}
	lua_createtable(state, /*num array elems=*/0, /*num non-array elems=*/6);
	script_evaluator->pushWorldObjectTableOntoStack(hit_uid);
	lua_setfield(state, -2, "object");
	lua_pushnumber(state, (double)hit_uid.value());
	lua_setfield(state, -2, "object_uid");
	lua_pushnumber(state, hit_distance / displacement_length);
	lua_setfield(state, -2, "fraction");
	lua_pushnumber(state, hit_distance);
	lua_setfield(state, -2, "distance");
	LuaUtils::pushVec3d(state, hit_normal);
	lua_setfield(state, -2, "normal");
	LuaUtils::pushVec3d(state, target_it->second->pos + direction * hit_distance);
	lua_setfield(state, -2, "position");
	return 1;
}


// Move a game-owned object atomically along a displacement, sliding its
// remaining motion along up to three conservative AABB contact planes.
static int gameMoveObjectWithSlide(lua_State* state)
{
	checkNumArgs(state, /*num_args_required=*/2);
	if(!lua_istable(state, 1))
		throw glare::Exception("game.moveObjectWithSlide(): arg 1 must be a game object" + errorContextString(state));
	const double raw_uid = LuaUtils::getTableNumberField(state, /*table index=*/1, "uid");
	if(!std::isfinite(raw_uid) || raw_uid < 1 || raw_uid > 9007199254740991.0 || raw_uid != std::floor(raw_uid))
		throw glare::Exception("game.moveObjectWithSlide(): object.uid must be a positive exact integer" + errorContextString(state));
	const Vec3d displacement = LuaUtils::getVec3d(state, /*index=*/2);
	const double displacement_length = displacement.length();
	if(!displacement.isFinite() || !std::isfinite(displacement_length) || displacement_length > 100.0)
		throw glare::Exception("game.moveObjectWithSlide(): displacement must be finite and no longer than 100 metres" + errorContextString(state));
	LuaScript* script = (LuaScript*)lua_getthreaddata(state);
	LuaScriptEvaluator* script_evaluator = script ? (LuaScriptEvaluator*)script->userdata : nullptr;
	SubstrataLuaVM* sub_lua_vm = (SubstrataLuaVM*)lua_callbacks(state)->userdata;
	if(!script_evaluator || !script_evaluator->world_object || !script_evaluator->world_state ||
		!script_evaluator->cur_world_state_lock || !sub_lua_vm->server)
	{
		lua_pushboolean(state, 0);
		LuaUtils::pushString(state, "Collision movement requires the authoritative server script context.");
		return 2;
	}
	const UID game_uid = script_evaluator->world_object->uid;
	WorldStateLock& lock = *script_evaluator->cur_world_state_lock;
	ServerWorldState& world = *script_evaluator->world_state;
	if(sub_lua_vm->server->world_state->read_only_mode)
	{
		lua_pushboolean(state, 0);
		LuaUtils::pushString(state, "The server is in read-only mode.");
		return 2;
	}
	auto& objects = world.getObjects(lock);
	const UID object_uid((uint64)raw_uid);
	const auto object_it = objects.find(object_uid);
	if(object_it == objects.end() || object_it->second->state == WorldObject::State_Dead ||
		object_it->second->game_spawn_owner_uid != game_uid)
	{
		lua_pushboolean(state, 0);
		LuaUtils::pushString(state, "game.moveObjectWithSlide() accepts only a live object spawned by this game.");
		return 2;
	}
	if(displacement_length < 1.0e-9)
	{
		lua_pushboolean(state, 1);
		lua_createtable(state, 0, 3);
		LuaUtils::pushVec3d(state, object_it->second->pos); lua_setfield(state, -2, "position");
		LuaUtils::pushVec3d(state, Vec3d(0,0,0)); lua_setfield(state, -2, "moved");
		lua_pushboolean(state, 0); lua_setfield(state, -2, "blocked");
		lua_createtable(state, 0, 0); lua_setfield(state, -2, "contacts");
		return 2;
	}
	if(!world.allowGameCollisionQuery(game_uid, lock) || !world.allowGameSpatialQuery(game_uid, lock))
	{
		lua_pushboolean(state, 0);
		LuaUtils::pushString(state, "game.moveObjectWithSlide(): query budget reached (collision: 30 per second per game and 200 world scans per second; spatial: 60 per second per game and 600 per second per world).");
		return 2;
	}
	const js::AABBox moving_bounds = object_it->second->getAABBWS();
	const Vec3d origin(
		((double)moving_bounds.min_[0] + moving_bounds.max_[0]) * 0.5,
		((double)moving_bounds.min_[1] + moving_bounds.max_[1]) * 0.5,
		((double)moving_bounds.min_[2] + moving_bounds.max_[2]) * 0.5);
	const Vec3d half_extents(
		((double)moving_bounds.max_[0] - moving_bounds.min_[0]) * 0.5,
		((double)moving_bounds.max_[1] - moving_bounds.min_[1]) * 0.5,
		((double)moving_bounds.max_[2] - moving_bounds.min_[2]) * 0.5);
	// The slide solver may scan up to three contact legs. Build a swept broad-
	// phase box once and keep only objects intersecting that corridor, instead
	// of walking the complete world object map for every leg.
	const Vec3d sweep_min(
		std::min(origin.x, origin.x + displacement.x) - half_extents.x,
		std::min(origin.y, origin.y + displacement.y) - half_extents.y,
		std::min(origin.z, origin.z + displacement.z) - half_extents.z);
	const Vec3d sweep_max(
		std::max(origin.x, origin.x + displacement.x) + half_extents.x,
		std::max(origin.y, origin.y + displacement.y) + half_extents.y,
		std::max(origin.z, origin.z + displacement.z) + half_extents.z);
	std::vector<UID> candidate_uids;
	if(!world.getGameRaySpatialCandidatesForBounds(sweep_min, sweep_max, candidate_uids, lock))
	{
		lua_pushboolean(state, 0);
		LuaUtils::pushString(state, "game.moveObjectWithSlide(): the swept region crosses more than 8,192 candidate objects; shorten the displacement or reduce local object density.");
		return 2;
	}
	struct CandidateObstacle { UID uid; js::AABBox bounds; };
	std::vector<CandidateObstacle> candidate_obstacles;
	candidate_obstacles.reserve(candidate_uids.size());
	for(const UID& candidate_uid : candidate_uids)
	{
		const auto obstacle_it = objects.find(candidate_uid);
		if(obstacle_it == objects.end()) continue;
		const WorldObject& obstacle = *obstacle_it->second;
		if(candidate_uid == game_uid || candidate_uid == object_uid || obstacle.state == WorldObject::State_Dead ||
			!obstacle.isCollidable() || (obstacle.flags & WorldObject::IS_SENSOR_FLAG) != 0)
			continue;
		if(obstacle.game_spawn_owner_uid.valid() && obstacle.game_spawn_owner_uid != game_uid) continue;
		const js::AABBox bounds = obstacle.getAABBWS();
		if((double)bounds.max_[0] < sweep_min.x || (double)bounds.min_[0] > sweep_max.x ||
			(double)bounds.max_[1] < sweep_min.y || (double)bounds.min_[1] > sweep_max.y ||
			(double)bounds.max_[2] < sweep_min.z || (double)bounds.min_[2] > sweep_max.z)
			continue;
		candidate_obstacles.push_back(CandidateObstacle{candidate_uid, bounds});
	}
	Vec3d moved(0,0,0);
	Vec3d remaining = displacement;
	auto length_squared = [](const Vec3d& value) { return value.x * value.x + value.y * value.y + value.z * value.z; };
	struct Contact { UID uid; Vec3d normal; double fraction; };
	std::vector<Contact> contacts;
	contacts.reserve(3);
	for(int iteration = 0; iteration < 3 && length_squared(remaining) > 1.0e-18; ++iteration)
	{
		const double leg_length = remaining.length();
		const Vec3d direction = remaining / leg_length;
		UID hit_uid;
		double hit_distance = leg_length;
		Vec3d hit_normal(0,0,0);
		for(const auto& candidate : candidate_obstacles)
		{
			const UID& uid = candidate.uid;
			const js::AABBox& obstacle_bounds = candidate.bounds;
			const Vec3d lower((double)obstacle_bounds.min_[0] - half_extents.x,
				(double)obstacle_bounds.min_[1] - half_extents.y, (double)obstacle_bounds.min_[2] - half_extents.z);
			const Vec3d upper((double)obstacle_bounds.max_[0] + half_extents.x,
				(double)obstacle_bounds.max_[1] + half_extents.y, (double)obstacle_bounds.max_[2] + half_extents.z);
			const js::AABBox expanded_bounds(Vec4f((float)lower.x,(float)lower.y,(float)lower.z,1.f),
				Vec4f((float)upper.x,(float)upper.y,(float)upper.z,1.f));
			double distance = 0.0;
			Vec3d normal;
			const Vec3d leg_origin = origin + moved;
			const bool starts_inside = leg_origin.x > lower.x && leg_origin.x < upper.x &&
				leg_origin.y > lower.y && leg_origin.y < upper.y && leg_origin.z > lower.z && leg_origin.z < upper.z;
			if(starts_inside)
			{
				// Recover from a pre-existing overlap by allowing movement through
				// its nearest exit face; movement into that face is blocked.
				int exit_axis = 0;
				double exit_distance = std::min(leg_origin.x - lower.x, upper.x - leg_origin.x);
				for(int axis = 1; axis < 3; ++axis)
				{
					const double axis_distance = std::min(leg_origin[axis] - lower[axis], upper[axis] - leg_origin[axis]);
					if(axis_distance < exit_distance) { exit_axis = axis; exit_distance = axis_distance; }
				}
				normal = Vec3d(0,0,0);
				normal[exit_axis] = leg_origin[exit_axis] - lower[exit_axis] <= upper[exit_axis] - leg_origin[exit_axis] ? -1.0 : 1.0;
				const double moving_out = direction.x * normal.x + direction.y * normal.y + direction.z * normal.z;
				if(moving_out > 1.0e-10) continue;
				distance = 0.0;
			}
			else if(!gameRayAABB(expanded_bounds, leg_origin, direction, hit_distance, distance, normal))
				continue;
			if(distance <= hit_distance)
			{
				hit_uid = uid;
				hit_distance = distance;
				hit_normal = normal;
			}
		}
		if(!hit_uid.valid())
		{
			moved += remaining;
			remaining = Vec3d(0,0,0);
			break;
		}
		const double travel_distance = std::max(0.0, hit_distance - 0.001);
		const double travel_fraction = travel_distance / leg_length;
		moved += remaining * travel_fraction;
		Vec3d residual = remaining * (1.0 - travel_fraction);
		const double into_normal = residual.x * hit_normal.x + residual.y * hit_normal.y + residual.z * hit_normal.z;
		if(into_normal < 0.0) residual = residual - hit_normal * into_normal;
		contacts.push_back(Contact{hit_uid, hit_normal, travel_fraction});
		remaining = residual;
	}
	const Vec3d position = object_it->second->pos + moved;
	if(!position.isFinite() || std::abs(position.x) > 1.0e7 || std::abs(position.y) > 1.0e7 || std::abs(position.z) > 1.0e7)
	{
		lua_pushboolean(state, 0);
		LuaUtils::pushString(state, "game.moveObjectWithSlide(): resulting position must remain within +/-10,000,000 metres.");
		return 2;
	}
	if(length_squared(moved) > 1.0e-18)
	{
		if(!world.allowGameObjectTransformUpdate(game_uid, lock))
		{
			lua_pushboolean(state, 0);
			LuaUtils::pushString(state, "The game reached its 600 object-transform updates per second limit.");
			return 2;
		}
		WorldObject candidate;
		candidate.copyNetworkStateFrom(*object_it->second);
		candidate.setTransformAndHistory(position, object_it->second->axis, object_it->second->angle);
		candidate.scale = object_it->second->scale;
		candidate.transformChanged();
		if(!userHasObjectCreationPermissions(candidate, script_evaluator->world_object->creator_id, world, lock))
		{
			lua_pushboolean(state, 0);
			LuaUtils::pushString(state, "The game owner does not have permission to move the object to that position.");
			return 2;
		}
		WorldObject& object = *object_it->second;
		const Vec3f axis = object.axis;
		const float angle = object.angle;
		object.setTransformAndHistory(position, axis, angle);
		object.transformChanged();
		world.refreshGameRaySpatialIndexObject(object, lock);
		object.last_transform_update_avatar_uid = std::numeric_limits<uint32>::max();
		object.from_remote_transform_dirty = true;
		world.getDirtyFromRemoteObjects(lock).insert(object_it->second);
		markLODChunkAsNeedsRebuildForChangedObject(&world, &object, lock);
		sub_lua_vm->server->world_state->markAsChanged();
	}
	lua_pushboolean(state, 1);
	lua_createtable(state, 0, 4);
	LuaUtils::pushVec3d(state, position); lua_setfield(state, -2, "position");
	LuaUtils::pushVec3d(state, moved); lua_setfield(state, -2, "moved");
	lua_pushboolean(state, contacts.empty() ? 0 : 1); lua_setfield(state, -2, "blocked");
	lua_createtable(state, (int)contacts.size(), 0);
	for(size_t i = 0; i < contacts.size(); ++i)
	{
		lua_createtable(state, 0, 4);
		script_evaluator->pushWorldObjectTableOntoStack(contacts[i].uid); lua_setfield(state, -2, "object");
		lua_pushnumber(state, (double)contacts[i].uid.value()); lua_setfield(state, -2, "object_uid");
		LuaUtils::pushVec3d(state, contacts[i].normal); lua_setfield(state, -2, "normal");
		lua_pushnumber(state, contacts[i].fraction); lua_setfield(state, -2, "fraction");
		lua_rawseti(state, -2, (int)i + 1);
	}
	lua_setfield(state, -2, "contacts");
	return 2;
}


// Server-side target selection for a game. The ray uses the server's latest
// avatar transform, which is client-reported; the server independently checks
// the hit against live objects owned by this game.
static int gameGetAimTarget(lua_State* state)
{
#if SERVER
	if(lua_gettop(state) < 2 || lua_gettop(state) > 3)
		throw glare::Exception("game.getAimTarget(): expected avatar, max_distance, and optional include_world_objects boolean" + errorContextString(state));
	if(!lua_istable(state, 1))
		throw glare::Exception("game.getAimTarget(): arg 1 must be an avatar" + errorContextString(state));
	const UID avatar_uid = getGameSaveAvatarUID(state, /*table_index=*/1);
	const double max_distance = LuaUtils::getDoubleArg(state, /*index=*/2);
	if(!std::isfinite(max_distance) || max_distance <= 0.0 || max_distance > 100.0)
		throw glare::Exception("game.getAimTarget(): max distance must be between 0 and 100 metres" + errorContextString(state));
	bool include_world_objects = false;
	if(lua_gettop(state) >= 3)
	{
		if(!lua_isboolean(state, 3))
			throw glare::Exception("game.getAimTarget(): include_world_objects must be a boolean" + errorContextString(state));
		include_world_objects = lua_toboolean(state, 3) != 0;
	}
	LuaScript* script = (LuaScript*)lua_getthreaddata(state);
	LuaScriptEvaluator* script_evaluator = script ? (LuaScriptEvaluator*)script->userdata : nullptr;
	if(!script_evaluator || !script_evaluator->world_object || !script_evaluator->world_state || !script_evaluator->cur_world_state_lock)
	{
		lua_pushnil(state);
		LuaUtils::pushString(state, "Aim target queries require the authoritative server script context.");
		return 2;
	}
	WorldStateLock& lock = *script_evaluator->cur_world_state_lock;
	ServerWorldState& world = *script_evaluator->world_state;
	const UID game_uid = script_evaluator->world_object->uid;
	if(!world.isGameSessionParticipant(game_uid, avatar_uid, lock))
	{
		lua_pushnil(state);
		LuaUtils::pushString(state, "game.getAimTarget() requires an active participant in this game.");
		return 2;
	}
	if(!world.allowGameAimQuery(game_uid, lock) || !world.allowGameSpatialQuery(game_uid, lock))
	{
		lua_pushnil(state);
		LuaUtils::pushString(state, "game.getAimTarget(): aim-query budget reached (10 per second per game, 200 world scans per second) or spatial-query budget reached (60 per second per game, 600 per second per world).");
		return 2;
	}
	const ServerWorldState::AvatarMapType& avatars = world.getAvatars(lock);
	const auto avatar_it = avatars.find(avatar_uid);
	if(avatar_it == avatars.end() || avatar_it->second->state != Avatar::State_Alive ||
		!avatar_it->second->pos.isFinite() || !avatar_it->second->rotation.isFinite())
	{
		lua_pushnil(state);
		return 1;
	}
	const Avatar& avatar = *avatar_it->second;
	const double heading = avatar.rotation.z;
	const double pitch = avatar.rotation.y;
	const double sin_pitch = std::sin(pitch);
	Vec3d origin = avatar.pos;
	Vec3d direction(sin_pitch * std::cos(heading), sin_pitch * std::sin(heading), std::cos(pitch));
	// New clients send their current camera ray with the normal game input
	// snapshot. Fall back to the avatar's reported view angles for older clients.
	Vec3d client_aim_origin, client_aim_direction;
	if(world.getGameSessionAimRay(game_uid, avatar_uid, client_aim_origin, client_aim_direction, lock))
	{
		const double client_direction_length = client_aim_direction.length();
		// The camera origin is supplied by the client so a game can aim from
		// its third-person camera. Keep it close to the server's latest avatar
		// transform so a modified client cannot raycast from an arbitrary point
		// elsewhere in the world.
		static const double MAX_CLIENT_AIM_ORIGIN_OFFSET = 30.0;
		if(!client_aim_origin.isFinite() || !client_aim_direction.isFinite() ||
			std::abs(client_aim_origin.x) > 1.0e7 || std::abs(client_aim_origin.y) > 1.0e7 || std::abs(client_aim_origin.z) > 1.0e7 ||
			!std::isfinite(client_direction_length) || client_direction_length < 0.95 || client_direction_length > 1.05 ||
			client_aim_origin.getDist2(avatar.pos) > Maths::square(MAX_CLIENT_AIM_ORIGIN_OFFSET))
		{
			lua_pushnil(state);
			LuaUtils::pushString(state, "game.getAimTarget(): the camera ray must be finite, have a unit direction and start within 30 metres of the server-reported avatar position.");
			return 2;
		}
		origin = client_aim_origin;
		direction = normalise(client_aim_direction);
	}
	std::vector<UID> candidate_uids;
	if(!world.getGameRaySpatialCandidates(origin, direction, max_distance, candidate_uids, lock))
	{
		lua_pushnil(state);
		LuaUtils::pushString(state, "game.getAimTarget(): the ray crosses a dense region with more than 8,192 candidate objects; use a shorter ray or reduce local object density.");
		return 2;
	}
	auto& objects = world.getObjects(lock);
	UID hit_uid;
	double hit_distance = max_distance;
	Vec3d hit_normal(0,0,0);
	for(const UID& uid : candidate_uids)
	{
		const auto object_it = objects.find(uid);
		if(object_it == objects.end()) continue;
		const WorldObject& object = *object_it->second;
		if(uid == game_uid || object.state == WorldObject::State_Dead)
			continue;
		const bool is_game_target = object.game_spawn_owner_uid == game_uid;
		if(!is_game_target && (!object.isCollidable() || (object.flags & WorldObject::IS_SENSOR_FLAG) != 0)) continue;
		double distance = 0.0;
		Vec3d normal;
		if(gameRayAABB(object.getAABBWS(), origin, direction, hit_distance, distance, normal) && distance < hit_distance)
		{
			hit_uid = (is_game_target || include_world_objects) ? uid : UID::invalidUID();
			hit_distance = distance;
			hit_normal = normal;
		}
	}
	if(!hit_uid.valid())
	{
		lua_pushnil(state);
		return 1;
	}
	lua_createtable(state, /*num array elems=*/0, /*num non-array elems=*/5);
	script_evaluator->pushWorldObjectTableOntoStack(hit_uid);
	lua_setfield(state, -2, "object");
	lua_pushnumber(state, (double)hit_uid.value());
	lua_setfield(state, -2, "object_uid");
	LuaUtils::pushVec3d(state, origin + direction * hit_distance);
	lua_setfield(state, -2, "position");
	LuaUtils::pushVec3d(state, hit_normal);
	lua_setfield(state, -2, "normal");
	lua_pushnumber(state, hit_distance);
	lua_setfield(state, -2, "distance");
	return 1;
#else
	(void)state;
	lua_pushnil(state);
	LuaUtils::pushString(state, "game.getAimTarget() is available only on the authoritative server.");
	return 2;
#endif
}


// Select the nearest participating avatar under the current client camera ray.
// This uses a bounded standing-player AABB and collidable objects occlude hits.
static int gameGetAimPlayerTarget(lua_State* state)
{
#if SERVER
	checkNumArgs(state, /*num_args_required=*/2);
	if(!lua_istable(state, 1))
		throw glare::Exception("game.getAimPlayerTarget(): arg 1 must be the targeting avatar" + errorContextString(state));
	const UID shooter_uid = getGameSaveAvatarUID(state, /*table_index=*/1);
	const double max_distance = LuaUtils::getDoubleArg(state, /*index=*/2);
	if(!std::isfinite(max_distance) || max_distance <= 0.0 || max_distance > 100.0)
		throw glare::Exception("game.getAimPlayerTarget(): max distance must be between 0 and 100 metres" + errorContextString(state));
	LuaScript* script = (LuaScript*)lua_getthreaddata(state);
	LuaScriptEvaluator* script_evaluator = script ? (LuaScriptEvaluator*)script->userdata : nullptr;
	if(!script_evaluator || !script_evaluator->world_object || !script_evaluator->world_state || !script_evaluator->cur_world_state_lock)
	{
		lua_pushnil(state);
		LuaUtils::pushString(state, "Aim-player queries require the authoritative server script context.");
		return 2;
	}
	WorldStateLock& lock = *script_evaluator->cur_world_state_lock;
	ServerWorldState& world = *script_evaluator->world_state;
	const UID game_uid = script_evaluator->world_object->uid;
	if(!world.isGameSessionParticipant(game_uid, shooter_uid, lock))
	{
		lua_pushnil(state);
		LuaUtils::pushString(state, "game.getAimPlayerTarget() requires an active participant in this game.");
		return 2;
	}
	if(!world.allowGameAimQuery(game_uid, lock) || !world.allowGameSpatialQuery(game_uid, lock))
	{
		lua_pushnil(state);
		LuaUtils::pushString(state, "game.getAimPlayerTarget(): aim-query budget reached (10 per second per game, 200 world scans per second) or spatial-query budget reached (60 per second per game, 600 per second per world).");
		return 2;
	}
	const auto& avatars = world.getAvatars(lock);
	const auto shooter_it = avatars.find(shooter_uid);
	if(shooter_it == avatars.end() || shooter_it->second->state != Avatar::State_Alive ||
		!shooter_it->second->pos.isFinite() || !shooter_it->second->rotation.isFinite())
	{
		lua_pushnil(state);
		return 1;
	}
	const Avatar& shooter = *shooter_it->second;
	const double heading = shooter.rotation.z;
	const double pitch = shooter.rotation.y;
	const double sin_pitch = std::sin(pitch);
	Vec3d origin = shooter.pos;
	Vec3d direction(sin_pitch * std::cos(heading), sin_pitch * std::sin(heading), std::cos(pitch));
	Vec3d client_aim_origin, client_aim_direction;
	if(world.getGameSessionAimRay(game_uid, shooter_uid, client_aim_origin, client_aim_direction, lock))
	{
		const double direction_length = client_aim_direction.length();
		if(!client_aim_origin.isFinite() || !client_aim_direction.isFinite() ||
			std::abs(client_aim_origin.x) > 1.0e7 || std::abs(client_aim_origin.y) > 1.0e7 || std::abs(client_aim_origin.z) > 1.0e7 ||
			!std::isfinite(direction_length) || direction_length < 0.95 || direction_length > 1.05 ||
			client_aim_origin.getDist2(shooter.pos) > Maths::square(30.0))
		{
			lua_pushnil(state);
			LuaUtils::pushString(state, "game.getAimPlayerTarget(): the camera ray must be finite, have a unit direction and start within 30 metres of the targeting avatar.");
			return 2;
		}
		origin = client_aim_origin;
		direction = normalise(client_aim_direction);
	}
	const std::vector<UID> participants = world.getGameSessionParticipants(game_uid, lock);
	double nearest_player_distance = max_distance;
	UID nearest_player_uid;
	Vec3d nearest_player_normal(0,0,0);
	static const double PLAYER_HALF_WIDTH = 0.30;
	static const double PLAYER_HEIGHT = 1.90;
	for(const UID& target_uid : participants)
	{
		if(target_uid == shooter_uid) continue;
		const auto target_it = avatars.find(target_uid);
		if(target_it == avatars.end() || target_it->second->state != Avatar::State_Alive || !target_it->second->pos.isFinite()) continue;
		const Vec3d& pos = target_it->second->pos;
		const js::AABBox player_bounds(
			Vec4f((float)(pos.x - PLAYER_HALF_WIDTH), (float)(pos.y - PLAYER_HALF_WIDTH), (float)pos.z, 1.f),
			Vec4f((float)(pos.x + PLAYER_HALF_WIDTH), (float)(pos.y + PLAYER_HALF_WIDTH), (float)(pos.z + PLAYER_HEIGHT), 1.f));
		double distance = 0.0;
		Vec3d normal;
		if(gameRayAABB(player_bounds, origin, direction, nearest_player_distance, distance, normal) && distance < nearest_player_distance)
		{
			nearest_player_distance = distance;
			nearest_player_uid = target_uid;
			nearest_player_normal = normal;
		}
	}
	if(!nearest_player_uid.valid())
	{
		lua_pushnil(state);
		return 1;
	}
	// Do not report a player visible through any collidable world or game object.
	std::vector<UID> candidate_uids;
	if(!world.getGameRaySpatialCandidates(origin, direction, nearest_player_distance, candidate_uids, lock))
	{
		lua_pushnil(state);
		LuaUtils::pushString(state, "game.getAimPlayerTarget(): the ray crosses a dense region with more than 8,192 candidate objects; use a shorter ray or reduce local object density.");
		return 2;
	}
	const auto& objects = world.getObjects(lock);
	for(const UID& uid : candidate_uids)
	{
		const auto object_it = objects.find(uid);
		if(object_it == objects.end()) continue;
		const WorldObject& object = *object_it->second;
		if(uid == game_uid || object.state == WorldObject::State_Dead || !object.isCollidable() ||
			(object.flags & WorldObject::IS_SENSOR_FLAG) != 0) continue;
		double distance = 0.0;
		Vec3d normal;
		if(gameRayAABB(object.getAABBWS(), origin, direction, nearest_player_distance, distance, normal) &&
			distance < nearest_player_distance)
		{
			lua_pushnil(state);
			return 1;
		}
	}
	lua_createtable(state, /*num array elems=*/0, /*num non-array elems=*/5);
	script_evaluator->pushAvatarTableOntoStack(nearest_player_uid);
	lua_setfield(state, -2, "avatar");
	lua_pushnumber(state, (double)nearest_player_uid.value());
	lua_setfield(state, -2, "avatar_uid");
	LuaUtils::pushVec3d(state, origin + direction * nearest_player_distance);
	lua_setfield(state, -2, "position");
	LuaUtils::pushVec3d(state, nearest_player_normal);
	lua_setfield(state, -2, "normal");
	lua_pushnumber(state, nearest_player_distance);
	lua_setfield(state, -2, "distance");
	return 1;
#else
	(void)state;
	lua_pushnil(state);
	LuaUtils::pushString(state, "game.getAimPlayerTarget() is available only on the authoritative server.");
	return 2;
#endif
}


// Return overlap enter/exit edges since this game last queried the same object.
// The stored snapshot belongs to the game and is bounded by its spawned-object limit.
static int gameGetOverlapChanges(lua_State* state)
{
	checkNumArgs(state, /*num_args_required=*/1);
	if(!lua_istable(state, 1))
		throw glare::Exception("game.getOverlapChanges(): arg 1 must be a game object" + errorContextString(state));
	const double raw_uid = LuaUtils::getTableNumberField(state, /*table index=*/1, "uid");
	if(!std::isfinite(raw_uid) || raw_uid < 1 || raw_uid > 9007199254740991.0 || raw_uid != std::floor(raw_uid))
		throw glare::Exception("game.getOverlapChanges(): object.uid must be a positive exact integer" + errorContextString(state));
	LuaScript* script = (LuaScript*)lua_getthreaddata(state);
	LuaScriptEvaluator* script_evaluator = script ? (LuaScriptEvaluator*)script->userdata : nullptr;
	if(!script_evaluator || !script_evaluator->world_object || !script_evaluator->world_state || !script_evaluator->cur_world_state_lock)
	{
		lua_pushnil(state);
		LuaUtils::pushString(state, "Game overlap queries require the authoritative server script context.");
		return 2;
	}
	WorldStateLock& lock = *script_evaluator->cur_world_state_lock;
	ServerWorldState& world = *script_evaluator->world_state;
	const UID game_uid = script_evaluator->world_object->uid;
	auto& objects = world.getObjects(lock);
	const UID target_uid((uint64)raw_uid);
	const auto target_it = objects.find(target_uid);
	if(target_it == objects.end() || target_it->second->state == WorldObject::State_Dead ||
		target_it->second->game_spawn_owner_uid != game_uid)
	{
		lua_pushnil(state);
		LuaUtils::pushString(state, "game.getOverlapChanges() accepts only a live object spawned by this game.");
		return 2;
	}
	if(!world.allowGameSpatialQuery(game_uid, lock))
	{
		lua_pushnil(state);
		LuaUtils::pushString(state, "game.getOverlapChanges(): spatial-query budget reached (60 per second per game, 600 per second shared across this world).");
		return 2;
	}
	const js::AABBox target_bounds = target_it->second->getAABBWS();
	const std::vector<UID> spawned_uids = world.getGameSpawnedObjectUIDs(game_uid, lock);
	std::vector<UID> overlapping_uids;
	overlapping_uids.reserve(16);
	for(const UID& uid : spawned_uids)
	{
		if(uid == target_uid) continue;
		const auto object_it = objects.find(uid);
		if(object_it == objects.end() || object_it->second->state == WorldObject::State_Dead) continue;
		if(object_it->second->getAABBWS().intersectsAABB(target_bounds))
		{
			if(overlapping_uids.size() >= 256)
			{
				lua_pushnil(state);
				LuaUtils::pushString(state, "game.getOverlapChanges(): result exceeds the 256-object limit; reduce the active group size or split the query.");
				return 2;
			}
			overlapping_uids.push_back(uid);
		}
	}
	GameOverlapChangeSet changes;
	if(!world.updateGameOverlapSnapshot(game_uid, target_uid, overlapping_uids, changes, lock))
	{
		lua_pushnil(state);
		LuaUtils::pushString(state, "game.getOverlapChanges(): the per-game overlap snapshot limit of 16,384 pairs has been reached.");
		return 2;
	}
	lua_createtable(state, 0, 2);
	const auto pushUIDArray = [state, script_evaluator](const std::vector<UID>& uids)
	{
		lua_createtable(state, (int)uids.size(), 0);
		for(size_t i = 0; i < uids.size(); ++i)
		{
			script_evaluator->pushWorldObjectTableOntoStack(uids[i]);
			lua_rawseti(state, -2, (int)i + 1);
		}
	};
	pushUIDArray(changes.entered);
	lua_setfield(state, -2, "entered");
	pushUIDArray(changes.exited);
	lua_setfield(state, -2, "exited");
	return 1;
}
#else
// Client scripts share the same API shape, but world mutations are authoritative
// and therefore only happen on the server.
static int gameCreateVoxelObject(lua_State* state)
{
	(void)state;
	lua_pushnil(state);
	LuaUtils::pushString(state, "game.createVoxelObject() is available only on the authoritative server.");
	return 2;
}

static int gameSpawnObject(lua_State* state)
{
	(void)state;
	lua_pushnil(state);
	LuaUtils::pushString(state, "game.spawnObject() is available only on the authoritative server.");
	return 2;
}

static int gameSpawnGroup(lua_State* state)
{
	(void)state;
	lua_pushnil(state);
	LuaUtils::pushString(state, "game.spawnGroup() is available only on the authoritative server.");
	return 2;
}

static int gameDestroyObject(lua_State* state)
{
	(void)state;
	lua_pushboolean(state, 0);
	return 1;
}

static int gameClearSpawnedObjects(lua_State* state)
{
	(void)state;
	lua_pushboolean(state, 0);
	LuaUtils::pushString(state, "game.clearSpawnedObjects() is available only on the authoritative server.");
	return 2;
}

static int gameSetObjectTransform(lua_State* state)
{
	(void)state;
	lua_pushboolean(state, 0);
	LuaUtils::pushString(state, "game.setObjectTransform() is available only on the authoritative server.");
	return 2;
}

static int gameGetOverlaps(lua_State* state)
{
	(void)state;
	lua_pushnil(state);
	LuaUtils::pushString(state, "game.getOverlaps() is available only on the authoritative server.");
	return 2;
}

static int gameSweepAABB(lua_State* state)
{
	(void)state;
	lua_pushnil(state);
	LuaUtils::pushString(state, "game.sweepAABB() is available only on the authoritative server.");
	return 2;
}

static int gameMoveObjectWithSlide(lua_State* state)
{
	(void)state;
	lua_pushboolean(state, 0);
	LuaUtils::pushString(state, "game.moveObjectWithSlide() is available only on the authoritative server.");
	return 2;
}

static int gameGetOverlapChanges(lua_State* state)
{
	(void)state;
	lua_pushnil(state);
	LuaUtils::pushString(state, "game.getOverlapChanges() is available only on the authoritative server.");
	return 2;
}
#endif // SERVER


static int objectStorageGetItem(lua_State* state)
{
	// Expected args:
	// Arg 1: key : String

	checkNumArgs(state, /*num_args_required*/1);

	const std::string key_string = LuaUtils::getStringArg(state, /*index=*/1);

#if GUI_CLIENT
	lua_pushnil(state); // Return nil 
#endif
#if SERVER
	LuaScript* script = (LuaScript*)lua_getthreaddata(state);
	LuaScriptEvaluator* script_evaluator = (LuaScriptEvaluator*)script->userdata;
	SubstrataLuaVM* sub_lua_vm = (SubstrataLuaVM*)lua_callbacks(state)->userdata;

	ServerAllWorldsState* world_state = sub_lua_vm->server->world_state.ptr();
	checkHoldWorldStateMutex(script_evaluator, world_state);

	ObjectStorageKey key;
	key.ob_uid = script_evaluator->world_object->uid;
	key.key_string = key_string;
	auto res = world_state->object_storage_items.find(key);
	if(res == world_state->object_storage_items.end())
	{
		lua_pushnil(state); // Return nil if no item found for key.
	}
	else
	{
		try
		{
			BufferViewInStream stream(ArrayRef<uint8>(res->second->data.data(), res->second->data.size()));
			LuaSerialisation::deserialise(state, sub_lua_vm->metatable_uid_to_ref_map, stream); // Pushes deserialised Lua value onto Lua stack.
		}
		catch(glare::Exception& /*e*/)
		{
			throw glare::Exception("Error while deserialising Lua object.");
		}
	}
#endif
	return 1;
}


static int objectStorageSetItem(lua_State* state)
{
	// Expected args:
	// Arg 1: key : string
	// Arg 2: value : Any serialisable Lua object

	checkNumArgs(state, /*num_args_required*/2);

	const std::string key_string = LuaUtils::getStringArg(state, /*index=*/1);
	if(key_string.size() > 256)
		throw glare::Exception("Key is too long");

#if SERVER
	LuaScript* script = (LuaScript*)lua_getthreaddata(state);
	LuaScriptEvaluator* script_evaluator = (LuaScriptEvaluator*)script->userdata;
	SubstrataLuaVM* sub_lua_vm = (SubstrataLuaVM*)lua_callbacks(state)->userdata;

	ServerAllWorldsState* world_state = sub_lua_vm->server->world_state.ptr();
	checkHoldWorldStateMutex(script_evaluator, world_state);

	try
	{
		// Serialise the Lua value to a buffer.
		BufferOutStream buf_stream;
		LuaSerialisation::SerialisationOptions options;
		options.max_depth = 16;
		options.max_serialised_size_B = 1 << 16;
		LuaSerialisation::serialise(state, /*stack index=*/2, options, buf_stream);

		// Look up ObjectStorage item, create if not present already.
		ObjectStorageKey key;
		key.ob_uid = script_evaluator->world_object->uid;
		key.key_string = key_string;

		Reference<ObjectStorageItem> item = world_state->getOrCreateObjectStorageItem(key); // May throw
		assert(item->key == key);

		// Set the item data
		item->data = buf_stream.buf;

		world_state->db_dirty_object_storage_items.insert(item);
		world_state->markAsChanged();
	}
	catch(glare::Exception& e)
	{
		throw glare::Exception("Error serialising Lua object: " + e.what());
	}
#endif
	
	return 0;
}


static int doHTTPGetRequestAsync(lua_State* state)
{
	// Expected args:
	// Arg 1: URL : string
	// Arg 2: additional header lines : table
	// Arg 3: onDone callback
	// Arg 4: onError callback

	// onDone gets passed:
	// 
	// {
	//   response_code: number
	//   response_message : string
	//   mime_type : string
	//   body_data : buffer
	// }

	// onError gets passed
	// {
	//	  error_code : number
	//	  error_description : string
	// }

	checkNumArgs(state, /*num_args_required*/4);

	const std::string URL_string = LuaUtils::getStringArg(state, /*index=*/1);
	LuaUtils::checkArgIsFunction(state, /*index=*/3);
	LuaUtils::checkArgIsFunction(state, /*index=*/4);

#if SERVER
	LuaScript* script = (LuaScript*)lua_getthreaddata(state);
	LuaScriptEvaluator* script_evaluator = (LuaScriptEvaluator*)script->userdata;
	SubstrataLuaVM* sub_lua_vm = (SubstrataLuaVM*)lua_callbacks(state)->userdata;

	Reference<LuaHTTPRequest> request = new LuaHTTPRequest();
	request->script_user_id = script_evaluator->world_object->creator_id;
	request->lua_script_evaluator = script_evaluator;
	request->request_type = "GET";
	request->URL = URL_string;

	LuaUtils::checkValueIsTable(state, /*index=*/2);
	lua_pushnil(state); // Push first key onto stack
	while(1)
	{
		int notdone = lua_next(state, /*table_index=*/2); // pops a key from the stack, and pushes a key-value pair from the table at the given index
		if(notdone == 0)
			break;

		request->additional_headers.push_back(LuaUtils::getString(state, -2));
		request->additional_headers.back() += ": ";
		request->additional_headers.back() += LuaUtils::getString(state, -1);

		lua_pop(state, 1); // Remove value, keep key on stack for next lua_next call
	}

	request->onDone_ref  = lua_ref(state, /*index=*/3);
	request->onError_ref = lua_ref(state, /*index=*/4);

	sub_lua_vm->server->enqueueLuaHTTPRequest(request);
#endif
	return 0;
}


static int doHTTPPostRequestAsync(lua_State* state)
{
	// Expected args:
	// Arg 1: URL : string
	// Arg 2: post_content : string
	// Arg 3: content_type: string
	// Arg 4: additional header lines : table
	// Arg 5: onDone callback
	// Arg 6: onError callback

	// onDone gets passed:
	// 
	// {
	//   response_code: number
	//   response_message : string
	//   mime_type : string
	//   body_data : buffer
	// }

	// onError gets passed
	// {
	//	  error_code : number
	//	  error_description : string
	// }

	checkNumArgs(state, /*num_args_required*/6);

	const std::string URL_string = LuaUtils::getStringArg(state, /*index=*/1);
	const std::string post_content = LuaUtils::getStringArg(state, /*index=*/2);
	const std::string content_type = LuaUtils::getStringArg(state, /*index=*/3);
	// Arg 4 is additional header lines, parsed below.
	LuaUtils::checkArgIsFunction(state, /*index=*/5);
	LuaUtils::checkArgIsFunction(state, /*index=*/6);

#if SERVER
	LuaScript* script = (LuaScript*)lua_getthreaddata(state);
	LuaScriptEvaluator* script_evaluator = (LuaScriptEvaluator*)script->userdata;
	SubstrataLuaVM* sub_lua_vm = (SubstrataLuaVM*)lua_callbacks(state)->userdata;

	Reference<LuaHTTPRequest> request = new LuaHTTPRequest();
	request->script_user_id = script_evaluator->world_object->creator_id;
	request->lua_script_evaluator = script_evaluator;
	request->request_type = "POST";
	request->URL = URL_string;
	request->post_content = post_content;
	request->content_type = content_type;

	LuaUtils::checkValueIsTable(state, /*index=*/4);
	lua_pushnil(state); // Push first key onto stack
	while(1)
	{
		int notdone = lua_next(state, /*table_index=*/4); // pops a key from the stack, and pushes a key-value pair from the table at the given index
		if(notdone == 0)
			break;

		request->additional_headers.push_back(LuaUtils::getString(state, -2));
		request->additional_headers.back() += ": ";
		request->additional_headers.back() += LuaUtils::getString(state, -1);

		lua_pop(state, 1); // Remove value, keep key on stack for next lua_next call
	}

	request->onDone_ref  = lua_ref(state, /*index=*/5);
	request->onError_ref = lua_ref(state, /*index=*/6);

	sub_lua_vm->server->enqueueLuaHTTPRequest(request);
#endif
	return 0;
}


static int getSecret(lua_State* state)
{
	// Expected args:
	// Arg 1: secret_name : string

	checkNumArgs(state, /*num_args_required*/1);

	const std::string secret_name = LuaUtils::getStringArg(state, /*index=*/1);

#if GUI_CLIENT
	lua_pushnil(state);
#endif
#if SERVER
	LuaScript* script = (LuaScript*)lua_getthreaddata(state);
	LuaScriptEvaluator* script_evaluator = (LuaScriptEvaluator*)script->userdata;
	SubstrataLuaVM* sub_lua_vm = (SubstrataLuaVM*)lua_callbacks(state)->userdata;

	ServerAllWorldsState* world_state = sub_lua_vm->server->world_state.ptr();
	checkHoldWorldStateMutex(script_evaluator, world_state);

	const UserSecretKey key(/*user_id = */script_evaluator->world_object->creator_id, secret_name);
	auto res = world_state->user_secrets.find(key);
	if(res == world_state->user_secrets.end())
	{
		lua_pushnil(state); // Return nil if no item found for key.
	}
	else
	{
		LuaUtils::pushString(state, res->second->value);
	}
#endif
	return 1;
}


// Pushes the converted JSON node onto the top of the Lua stack
static void pushJSONLuaNode(lua_State* state, const JSONParser& parser, const JSONNode& node)
{
	if(!lua_checkstack(state, /*size=*/1)) // Make sure there is space for the value on the Lua stack
		throw glare::Exception("Failed to alloc lua stack space");

	switch (node.type)
	{
	case JSONNode::Type_Number:
	{
		lua_pushnumber(state, node.getDoubleValue());
		break;	
	}
	case JSONNode::Type_String:
	{
		const std::string& string_val = node.getStringValue();
		lua_pushlstring(state, string_val.c_str(), string_val.size());
		break;	
	}
	case JSONNode::Type_Boolean:
	{
		lua_pushboolean(state, node.getBoolValue());
		break;
	}
	case JSONNode::Type_Array:
	{
		lua_createtable(state, /*num array elems hint=*/(int)node.child_indices.size(), /*num other elems hint=*/0);
		for(size_t i=0; i<node.child_indices.size(); ++i)
		{
			runtimeCheck(node.child_indices[i] < (uint32)parser.nodes.size());
			pushJSONLuaNode(state, parser, parser.nodes[node.child_indices[i]]);

			lua_rawseti(state, /*table index=*/-2, 1 + (int)i);
		}

		break;
	}
	case JSONNode::Type_Object:
	{
		lua_createtable(state, /*num array elems hint=*/0, /*num other elems hint=*/(int)node.name_val_pairs.size());
		for(size_t i=0; i<node.name_val_pairs.size(); ++i)
		{
			runtimeCheck(node.name_val_pairs[i].value_node_index < (uint32)parser.nodes.size());
			pushJSONLuaNode(state, parser, parser.nodes[node.name_val_pairs[i].value_node_index]);

			lua_setfield(state, /*table index=*/-2, node.name_val_pairs[i].name.c_str());
		}

		break;
	}
	case JSONNode::Type_Null:
	{
		lua_pushnil(state);
		break;
	}
	default:
		runtimeCheckFailed("Invalid JSON type");
	}
}


static int parseJSON(lua_State* state)
{
	// arg 1: json : string

	// Returns lua object

	checkNumArgs(state, /*num_args_required*/1);

	const std::string json_string = LuaUtils::getStringArg(state, /*index=*/1);

	try
	{
		JSONParser parser;
		parser.parseBuffer(json_string.c_str(), json_string.size());

		runtimeCheck(!parser.nodes.empty());

		pushJSONLuaNode(state, parser, parser.nodes[0]);
	}
	catch(glare::Exception& e)
	{
		throw glare::Exception("Error while parsing JSON: " + e.what());
	}

	return 1;
}


static int createTimer(lua_State* state)
{
	// arg 1: onTimerEvent : function
	// arg 2: interval_time_s : Number   The interval time, in seconds
	// arg 3: repeating : bool

	// Returns a timer handle : Number

	checkNumArgs(state, /*num_args_required*/3);

	if(lua_type(state, /*index=*/1) != LUA_TFUNCTION)
		throw glare::Exception("createTimer(): arg 1 must be a function" + errorContextString(state));

	const double raw_interval_time = LuaUtils::getDoubleArg(state, /*index=*/2);
	const bool repeating = LuaUtils::getBool(state, /*index=*/3);
	if(!isFinite(raw_interval_time) || raw_interval_time > 86400.0)
		throw glare::Exception("createTimer(): interval must be finite and no greater than 86400 seconds" + errorContextString(state));

	// Don't allow the interval time to be too low.
	const double interval_time = myMax(0.1, raw_interval_time);

	SubstrataLuaVM* sub_lua_vm = (SubstrataLuaVM*)lua_callbacks(state)->userdata;

	LuaScript* script = (LuaScript*)lua_getthreaddata(state);
	LuaScriptEvaluator* script_evaluator = (LuaScriptEvaluator*)script->userdata;
	int free_timer_index = -1;
	int ordinary_timer_count = 0;
	for(int i=0; i<LuaScriptEvaluator::MAX_NUM_TIMERS; ++i)
	{
		if(script_evaluator->timers[i].id == -1)
		{
			if(free_timer_index == -1) free_timer_index = i;
		}
		else if(script_evaluator->timers[i].fixed_step_s <= 0.0)
			++ordinary_timer_count;
	}
	if(ordinary_timer_count >= LuaScriptEvaluator::MAX_NUM_ORDINARY_TIMERS)
		throw glare::Exception("createTimer(): four ordinary timers are already active for this script." + errorContextString(state));
	if(free_timer_index == -1)
		throw glare::Exception("createTimer(): no task slot is available." + errorContextString(state));
	const int onTimerEvent_ref = lua_ref(state, /*index=*/1);
#if GUI_CLIENT
	const double cur_time = sub_lua_vm->gui_client->total_timer.elapsed();
	ScriptTimerQueue& timer_queue = sub_lua_vm->gui_client->timer_queue;
#elif SERVER
	const double cur_time = sub_lua_vm->server->total_timer.elapsed();
	ScriptTimerQueue& timer_queue = sub_lua_vm->server->timer_queue;
#endif

	// Confirm a slot is available before creating the Lua registry reference, so
	// an exhausted timer table cannot retain callbacks that were never scheduled.
	{
		const int i = free_timer_index;
		const int timer_id = script_evaluator->next_timer_id++; // Make new unique timer id (to avoid ABA problem)

		// Record in slot
		script_evaluator->timers[i].id = timer_id;
		script_evaluator->timers[i].onTimerEvent_ref = onTimerEvent_ref;
		script_evaluator->timers[i].last_trigger_time = cur_time;
		script_evaluator->timers[i].fixed_step_s = 0.0;
		script_evaluator->timers[i].fixed_step_accumulator_s = 0.0;

		ScriptTimerQueueTimer timer;
		timer.onTimerEvent_ref = onTimerEvent_ref;
		timer.tigger_time = cur_time + interval_time;
		timer.repeating = repeating;
		timer.period = interval_time;
		timer.timer_index = i;
		timer.timer_id = timer_id;
		//timer.lua_script_evaluator_handle = script_evaluator->generational_handle;
		timer.lua_script_evaluator = script_evaluator;
		try
		{
			timer_queue.addTimer(cur_time, timer);
		}
		catch(...)
		{
			script_evaluator->destroyTimer(i);
			throw;
		}

		lua_pushnumber(state, (double)timer_id); // Push the stable timer handle.
		return 1; // Count of returned values
	}
//#else
//	throw glare::Exception("createTimer(): todo on server.");
//#endif
}


// Creates a bounded server-authoritative fixed-step game loop.
// The server polls scripts at about 10 Hz and performs at most four catch-up steps per poll.
static int createGameLoop(lua_State* state)
{
	// arg 1: onUpdate(this_object, fixed_delta_time_seconds) : function
	// arg 2: tick_rate_hz : Number (1..30)
	checkNumArgs(state, /*num_args_required=*/2);
	if(lua_type(state, /*index=*/1) != LUA_TFUNCTION)
		throw glare::Exception("createGameLoop(): arg 1 must be a function" + errorContextString(state));

	const double tick_rate_hz = LuaUtils::getDoubleArg(state, /*index=*/2);
	if(!isFinite(tick_rate_hz) || tick_rate_hz < 1.0 || tick_rate_hz > 30.0)
		throw glare::Exception("createGameLoop(): tick rate must be between 1 and 30 Hz" + errorContextString(state));

#if GUI_CLIENT
	// World-object scripts are also loaded by clients. A client-side no-op keeps
	// shared scripts from failing during initialization; -1 means unavailable.
	lua_pushnumber(state, -1.0);
	return 1;
#elif SERVER
	SubstrataLuaVM* sub_lua_vm = (SubstrataLuaVM*)lua_callbacks(state)->userdata;
	LuaScript* script = (LuaScript*)lua_getthreaddata(state);
	LuaScriptEvaluator* script_evaluator = (LuaScriptEvaluator*)script->userdata;
	const double cur_time = sub_lua_vm->server->total_timer.elapsed();
	int active_game_loop_count = 0;
	for(int i=0; i<LuaScriptEvaluator::MAX_NUM_TIMERS; ++i)
		if(script_evaluator->timers[i].id != -1 && script_evaluator->timers[i].fixed_step_s > 0.0)
			++active_game_loop_count;
	if(active_game_loop_count >= LuaScriptEvaluator::MAX_NUM_GAME_LOOPS)
		throw glare::Exception("createGameLoop(): this script already has an active game loop" + errorContextString(state));
	int free_timer_index = -1;
	for(int i=0; i<LuaScriptEvaluator::MAX_NUM_TIMERS; ++i)
		if(script_evaluator->timers[i].id == -1) { free_timer_index = i; break; }
	if(free_timer_index == -1)
		throw glare::Exception("createGameLoop(): no scheduler slot is available after reserving slots for four ordinary timers and one game loop" + errorContextString(state));
	const int callback_ref = lua_ref(state, /*index=*/1);
	{
		const int i = free_timer_index;
		LuaScriptEvaluator::LuaTimerInfo& timer_info = script_evaluator->timers[i];

		const int timer_id = script_evaluator->next_timer_id++;
		const double fixed_step_s = 1.0 / tick_rate_hz;
		timer_info.id = timer_id;
		timer_info.onTimerEvent_ref = callback_ref;
		timer_info.last_trigger_time = cur_time;
		timer_info.fixed_step_s = fixed_step_s;
		timer_info.fixed_step_accumulator_s = 0.0;

		ScriptTimerQueueTimer timer;
		timer.onTimerEvent_ref = callback_ref;
		timer.tigger_time = cur_time + 0.1;
		timer.repeating = true;
		timer.period = 0.1;
		timer.timer_index = i;
		timer.timer_id = timer_id;
		timer.lua_script_evaluator = script_evaluator;
		try
		{
			sub_lua_vm->server->timer_queue.addTimer(cur_time, timer);
		}
		catch(...)
		{
			script_evaluator->destroyTimer(i);
			throw;
		}

		lua_pushnumber(state, (double)timer_id);
		return 1;
	}
#endif
}


// A client-only fixed-step loop for local presentation (HUD animation, visual
// interpolation, etc.). Shared game rules must remain in game.createLoop().
// The client loop is available only while this object owns the local E-focus.
static int createClientGameLoop(lua_State* state)
{
	checkNumArgs(state, /*num_args_required=*/2);
	if(lua_type(state, /*index=*/1) != LUA_TFUNCTION)
		throw glare::Exception("game.createClientLoop(): arg 1 must be a function" + errorContextString(state));

	const double tick_rate_hz = LuaUtils::getDoubleArg(state, /*index=*/2);
	if(!isFinite(tick_rate_hz) || tick_rate_hz < 1.0 || tick_rate_hz > 30.0)
		throw glare::Exception("game.createClientLoop(): tick rate must be between 1 and 30 Hz" + errorContextString(state));

#if GUI_CLIENT
	SubstrataLuaVM* sub_lua_vm = (SubstrataLuaVM*)lua_callbacks(state)->userdata;
	LuaScript* script = (LuaScript*)lua_getthreaddata(state);
	LuaScriptEvaluator* script_evaluator = script ? (LuaScriptEvaluator*)script->userdata : nullptr;
	if(!script_evaluator || !script_evaluator->world_object || !sub_lua_vm->gui_client ||
		!sub_lua_vm->gui_client->isGameInputFocused(script_evaluator->world_object->uid))
	{
		lua_pushnumber(state, -1.0); // No local game session currently owns input.
		return 1;
	}

	int active_game_loop_count = 0;
	for(int i=0; i<LuaScriptEvaluator::MAX_NUM_TIMERS; ++i)
		if(script_evaluator->timers[i].id != -1 && script_evaluator->timers[i].fixed_step_s > 0.0)
			++active_game_loop_count;
	if(active_game_loop_count >= LuaScriptEvaluator::MAX_NUM_GAME_LOOPS)
		throw glare::Exception("game.createClientLoop(): this script already has an active game loop" + errorContextString(state));

	int free_timer_index = -1;
	for(int i=0; i<LuaScriptEvaluator::MAX_NUM_TIMERS; ++i)
		if(script_evaluator->timers[i].id == -1) { free_timer_index = i; break; }
	if(free_timer_index == -1)
		throw glare::Exception("game.createClientLoop(): no scheduler slot is available after reserving slots for four ordinary timers and one game loop" + errorContextString(state));
	const int callback_ref = lua_ref(state, /*index=*/1);
	{
		const int i = free_timer_index;
		LuaScriptEvaluator::LuaTimerInfo& timer_info = script_evaluator->timers[i];

		const int timer_id = script_evaluator->next_timer_id++;
		const double cur_time = sub_lua_vm->gui_client->total_timer.elapsed();
		const double fixed_step_s = 1.0 / tick_rate_hz;
		timer_info.id = timer_id;
		timer_info.onTimerEvent_ref = callback_ref;
		timer_info.last_trigger_time = cur_time;
		timer_info.fixed_step_s = fixed_step_s;
		timer_info.fixed_step_accumulator_s = 0.0;

		// Poll at 20 Hz; fixed-step accumulation still respects the requested rate.
		// A missed poll is capped to four updates by the client scheduler.
		ScriptTimerQueueTimer timer;
		timer.onTimerEvent_ref = callback_ref;
		timer.tigger_time = cur_time + 0.05;
		timer.repeating = true;
		timer.period = 0.05;
		timer.timer_index = i;
		timer.timer_id = timer_id;
		timer.lua_script_evaluator = script_evaluator;
		try
		{
			sub_lua_vm->gui_client->client_game_loop_timer_queue.addTimer(cur_time, timer);
		}
		catch(...)
		{
			script_evaluator->destroyTimer(i);
			throw;
		}

		lua_pushnumber(state, (double)timer_id);
		return 1;
	}
#elif SERVER
	// Keep shared scripts loadable on the server. Client presentation never
	// participates in authoritative simulation.
	lua_pushnumber(state, -1.0);
	return 1;
#endif
}


// Temporary local camera control for the object that owns the current E-focused
// game session. The server deliberately returns unavailable: shared rules must
// never depend on a player's camera.
static int gameSetCamera(lua_State* state)
{
	if(lua_gettop(state) > 4)
		throw glare::Exception("game.setCamera(): expected camera position, look-at position, optional horizontal field of view in degrees, and optional transition duration in seconds" + errorContextString(state));
	checkNumArgs(state, /*num_args_required=*/2);
	const Vec3d camera_position = LuaUtils::getVec3d(state, /*index=*/1);
	const Vec3d look_at_position = LuaUtils::getVec3d(state, /*index=*/2);
	const double horizontal_fov_degrees = lua_gettop(state) >= 3 && !lua_isnil(state, 3) ? LuaUtils::getDoubleArg(state, /*index=*/3) : 0.0;
	const double transition_seconds = lua_gettop(state) >= 4 && !lua_isnil(state, 4) ? LuaUtils::getDoubleArg(state, /*index=*/4) : 0.0;
	bool succeeded = false;
	std::string error;
#if GUI_CLIENT
	SubstrataLuaVM* sub_lua_vm = (SubstrataLuaVM*)lua_callbacks(state)->userdata;
	LuaScript* script = (LuaScript*)lua_getthreaddata(state);
	LuaScriptEvaluator* script_evaluator = script ? (LuaScriptEvaluator*)script->userdata : nullptr;
	if(script_evaluator && script_evaluator->world_object && sub_lua_vm->gui_client)
		succeeded = sub_lua_vm->gui_client->setGameCamera(
			script_evaluator->world_object->uid, camera_position, look_at_position, horizontal_fov_degrees, transition_seconds, error);
	else
		error = "Camera control is available only from a local world-object game session.";
#elif SERVER
	error = "Camera control is client-side only; shared game rules run on the server.";
#endif
	lua_pushboolean(state, succeeded ? 1 : 0);
	LuaUtils::pushString(state, error);
	return 2;
}


// Local presentation camera that continuously follows an existing world object.
// Offsets are in world coordinates; the server never sees or depends on this camera.
static int gameFollowCamera(lua_State* state)
{
	if(lua_gettop(state) > 5)
		throw glare::Exception("game.followCamera(): expected target object, camera offset, optional look-at offset, horizontal field of view, and transition duration" + errorContextString(state));
	checkNumArgs(state, /*num_args_required=*/2);
	if(!lua_istable(state, 1))
		throw glare::Exception("game.followCamera(): arg 1 must be a world object" + errorContextString(state));
	const double raw_target_uid = LuaUtils::getTableNumberField(state, /*table index=*/1, "uid");
	if(!std::isfinite(raw_target_uid) || raw_target_uid < 1 || raw_target_uid > 9007199254740991.0 || raw_target_uid != std::floor(raw_target_uid))
		throw glare::Exception("game.followCamera(): target.uid must be a positive exact integer" + errorContextString(state));
	const Vec3d camera_offset = LuaUtils::getVec3d(state, /*index=*/2);
	const Vec3d look_at_offset = lua_gettop(state) >= 3 && !lua_isnil(state, 3) ? LuaUtils::getVec3d(state, /*index=*/3) : Vec3d(0,0,0);
	const double horizontal_fov_degrees = lua_gettop(state) >= 4 && !lua_isnil(state, 4) ? LuaUtils::getDoubleArg(state, /*index=*/4) : 0.0;
	const double transition_seconds = lua_gettop(state) >= 5 && !lua_isnil(state, 5) ? LuaUtils::getDoubleArg(state, /*index=*/5) : 0.0;
	bool succeeded = false;
	std::string error;
#if GUI_CLIENT
	SubstrataLuaVM* sub_lua_vm = (SubstrataLuaVM*)lua_callbacks(state)->userdata;
	LuaScript* script = (LuaScript*)lua_getthreaddata(state);
	LuaScriptEvaluator* script_evaluator = script ? (LuaScriptEvaluator*)script->userdata : nullptr;
	if(script_evaluator && script_evaluator->world_object && script_evaluator->cur_world_state_lock && sub_lua_vm->gui_client)
		succeeded = sub_lua_vm->gui_client->followGameCamera(script_evaluator->world_object->uid, UID((uint64)raw_target_uid),
			camera_offset, look_at_offset, horizontal_fov_degrees, transition_seconds, *script_evaluator->cur_world_state_lock, error);
	else
		error = "Camera follow is available only from a local world-object game session.";
#elif SERVER
	error = "Camera follow is client-side only; shared game rules run on the server.";
#endif
	lua_pushboolean(state, succeeded ? 1 : 0);
	LuaUtils::pushString(state, error);
	return 2;
}


static int gameReleaseCamera(lua_State* state)
{
	checkNumArgs(state, /*num_args_required=*/0);
	bool succeeded = false;
	std::string error;
#if GUI_CLIENT
	SubstrataLuaVM* sub_lua_vm = (SubstrataLuaVM*)lua_callbacks(state)->userdata;
	LuaScript* script = (LuaScript*)lua_getthreaddata(state);
	LuaScriptEvaluator* script_evaluator = script ? (LuaScriptEvaluator*)script->userdata : nullptr;
	if(script_evaluator && script_evaluator->world_object && sub_lua_vm->gui_client)
	{
		succeeded = sub_lua_vm->gui_client->releaseGameCamera(script_evaluator->world_object->uid);
		if(!succeeded)
			error = "This game does not own the active camera lease.";
	}
	else
		error = "Camera control is available only from a local world-object game session.";
#elif SERVER
	error = "Camera control is client-side only; shared game rules run on the server.";
#endif
	lua_pushboolean(state, succeeded ? 1 : 0);
	LuaUtils::pushString(state, error);
	return 2;
}


// Local avatar presentation control for the focused game session. Calls should
// be refreshed from game.createClientLoop(); the client expires stale control.
static int gameSetPlayerControl(lua_State* state)
{
	if(lua_gettop(state) > 4)
		throw glare::Exception("game.setPlayerControl(): expected move_x, move_y, optional jump, and optional run" + errorContextString(state));
	checkNumArgs(state, /*num_args_required=*/2);
	const double raw_move_x = LuaUtils::getDoubleArg(state, /*index=*/1);
	const double raw_move_y = LuaUtils::getDoubleArg(state, /*index=*/2);
	if(!isFinite(raw_move_x) || !isFinite(raw_move_y) || raw_move_x < -1.0 || raw_move_x > 1.0 || raw_move_y < -1.0 || raw_move_y > 1.0)
		throw glare::Exception("game.setPlayerControl(): movement axes must be finite values from -1 to 1" + errorContextString(state));
	const bool jump = lua_gettop(state) >= 3 && lua_toboolean(state, 3) != 0;
	const bool run = lua_gettop(state) >= 4 && lua_toboolean(state, 4) != 0;
	bool succeeded = false;
	std::string error;
#if GUI_CLIENT
	SubstrataLuaVM* sub_lua_vm = (SubstrataLuaVM*)lua_callbacks(state)->userdata;
	LuaScript* script = (LuaScript*)lua_getthreaddata(state);
	LuaScriptEvaluator* script_evaluator = script ? (LuaScriptEvaluator*)script->userdata : nullptr;
	if(script_evaluator && script_evaluator->world_object && sub_lua_vm->gui_client)
		succeeded = sub_lua_vm->gui_client->setGamePlayerControl(script_evaluator->world_object->uid,
			(float)raw_move_x, (float)raw_move_y, jump, run, error);
	else
		error = "Player control is available only from a local world-object game session.";
#elif SERVER
	error = "Player control is client-side presentation; shared game rules run on the server.";
#endif
	lua_pushboolean(state, succeeded ? 1 : 0);
	LuaUtils::pushString(state, error);
	return 2;
}


// Snapshot of local controls for the focused game. Shared gameplay rules must
// still be handled by the authoritative server callbacks.
static int gameGetLocalInput(lua_State* state)
{
	checkNumArgs(state, /*num_args_required=*/0);
#if GUI_CLIENT
	SubstrataLuaVM* sub_lua_vm = (SubstrataLuaVM*)lua_callbacks(state)->userdata;
	LuaScript* script = (LuaScript*)lua_getthreaddata(state);
	LuaScriptEvaluator* script_evaluator = script ? (LuaScriptEvaluator*)script->userdata : nullptr;
	GameLocalInput input;
	const bool available = script_evaluator && script_evaluator->world_object && sub_lua_vm->gui_client &&
		sub_lua_vm->gui_client->getGameLocalInput(script_evaluator->world_object->uid, input);
	const PlayerPhysicsInput& movement = input.movement;
	float move_x = (movement.D_down ? 1.f : 0.f) - (movement.A_down ? 1.f : 0.f) + movement.axis_left_x;
	float move_y = (movement.W_down ? 1.f : 0.f) - (movement.S_down ? 1.f : 0.f) - movement.axis_left_y;
	const float magnitude = std::sqrt(move_x * move_x + move_y * move_y);
	if(magnitude > 1.f) { move_x /= magnitude; move_y /= magnitude; }
	lua_createtable(state, /*num array elems=*/0, /*num non-array elems=*/13);
	lua_pushboolean(state, available); lua_setfield(state, -2, "available");
	lua_pushnumber(state, move_x); lua_setfield(state, -2, "move_x");
	lua_pushnumber(state, move_y); lua_setfield(state, -2, "move_y");
	lua_pushboolean(state, movement.space_down); lua_setfield(state, -2, "jump");
	lua_pushboolean(state, movement.SHIFT_down); lua_setfield(state, -2, "run");
	lua_pushboolean(state, movement.W_down); lua_setfield(state, -2, "w");
	lua_pushboolean(state, movement.A_down); lua_setfield(state, -2, "a");
	lua_pushboolean(state, movement.S_down); lua_setfield(state, -2, "s");
	lua_pushboolean(state, movement.D_down); lua_setfield(state, -2, "d");
	lua_createtable(state, /*num array elems=*/0, (int)input.keys_down.size());
	for(const uint32 key_code : input.keys_down)
	{
		std::string key_name;
		if(key_code >= Protocol::ScriptKey_CharacterBase + 'a' && key_code <= Protocol::ScriptKey_CharacterBase + 'z')
			key_name.push_back((char)(key_code - Protocol::ScriptKey_CharacterBase));
		else if(key_code >= Protocol::ScriptKey_CharacterBase + '0' && key_code <= Protocol::ScriptKey_CharacterBase + '9')
			key_name.push_back((char)(key_code - Protocol::ScriptKey_CharacterBase));
		else switch(key_code)
		{
		case Protocol::ScriptKey_Left: key_name = "left"; break;
		case Protocol::ScriptKey_Right: key_name = "right"; break;
		case Protocol::ScriptKey_Up: key_name = "up"; break;
		case Protocol::ScriptKey_Down: key_name = "down"; break;
		case Protocol::ScriptKey_Space: key_name = "space"; break;
		case Protocol::ScriptKey_Enter: key_name = "enter"; break;
		case Protocol::ScriptKey_Escape: key_name = "escape"; break;
		case Protocol::ScriptKey_W: key_name = "w"; break;
		case Protocol::ScriptKey_A: key_name = "a"; break;
		case Protocol::ScriptKey_S: key_name = "s"; break;
		case Protocol::ScriptKey_D: key_name = "d"; break;
		default: break;
		}
		if(!key_name.empty())
		{
			lua_pushboolean(state, true);
			lua_setfield(state, -2, key_name.c_str());
		}
	}
	lua_setfield(state, -2, "keys");
	if(input.pointer_available)
	{
		lua_createtable(state, /*num array elems=*/0, /*num non-array elems=*/2);
		lua_pushnumber(state, input.pointer_x); lua_setfield(state, -2, "x");
		lua_pushnumber(state, input.pointer_y); lua_setfield(state, -2, "y");
	}
	else lua_pushnil(state);
	lua_setfield(state, -2, "pointer");
	lua_createtable(state, /*num array elems=*/0, /*num non-array elems=*/3);
	lua_pushboolean(state, input.buttons_down.count(Protocol::ScriptKey_MouseLeft) != 0); lua_setfield(state, -2, "left");
	lua_pushboolean(state, input.buttons_down.count(Protocol::ScriptKey_MouseMiddle) != 0); lua_setfield(state, -2, "middle");
	lua_pushboolean(state, input.buttons_down.count(Protocol::ScriptKey_MouseRight) != 0); lua_setfield(state, -2, "right");
	lua_setfield(state, -2, "mouse");
	lua_createtable(state, /*num array elems=*/0, /*num non-array elems=*/21);
	lua_pushboolean(state, input.gamepad_connected); lua_setfield(state, -2, "connected");
	lua_pushnumber(state, input.left_x); lua_setfield(state, -2, "left_x");
	lua_pushnumber(state, input.left_y); lua_setfield(state, -2, "left_y");
	lua_pushnumber(state, input.right_x); lua_setfield(state, -2, "right_x");
	lua_pushnumber(state, input.right_y); lua_setfield(state, -2, "right_y");
	lua_pushnumber(state, input.left_trigger); lua_setfield(state, -2, "left_trigger");
	lua_pushnumber(state, input.right_trigger); lua_setfield(state, -2, "right_trigger");
	const struct GamepadButtonField { const char* name; uint32 code; } gamepad_buttons[] = {
		{"a", Protocol::ScriptKey_GamepadA}, {"b", Protocol::ScriptKey_GamepadB},
		{"x", Protocol::ScriptKey_GamepadX}, {"y", Protocol::ScriptKey_GamepadY},
		{"l1", Protocol::ScriptKey_GamepadL1}, {"r1", Protocol::ScriptKey_GamepadR1},
		{"l3", Protocol::ScriptKey_GamepadL3}, {"r3", Protocol::ScriptKey_GamepadR3},
		{"start", Protocol::ScriptKey_GamepadStart}, {"select", Protocol::ScriptKey_GamepadSelect},
		{"up", Protocol::ScriptKey_GamepadUp}, {"down", Protocol::ScriptKey_GamepadDown},
		{"left", Protocol::ScriptKey_GamepadLeft}, {"right", Protocol::ScriptKey_GamepadRight}
	};
	for(const GamepadButtonField& button : gamepad_buttons)
	{
		lua_pushboolean(state, input.buttons_down.count(button.code) != 0);
		lua_setfield(state, -2, button.name);
	}
	lua_setfield(state, -2, "gamepad");
	return 1;
#elif SERVER
	lua_pushnil(state);
	return 1;
#endif
}


// Cast a short local interaction ray from the current camera. This is a
// presentation query only; gameplay consequences must still be validated by
// the authoritative server.
static int gameRaycast(lua_State* state)
{
	checkNumArgs(state, /*num_args_required=*/1);
	const double raw_max_distance = LuaUtils::getDoubleArg(state, /*index=*/1);
	if(!isFinite(raw_max_distance) || raw_max_distance <= 0.0 || raw_max_distance > 100.0)
		throw glare::Exception("game.raycast(): max distance must be between 0 and 100 metres" + errorContextString(state));

#if GUI_CLIENT
	SubstrataLuaVM* sub_lua_vm = (SubstrataLuaVM*)lua_callbacks(state)->userdata;
	LuaScript* script = (LuaScript*)lua_getthreaddata(state);
	LuaScriptEvaluator* script_evaluator = script ? (LuaScriptEvaluator*)script->userdata : nullptr;
	if(script_evaluator && script_evaluator->world_object && sub_lua_vm->gui_client)
	{
		UID hit_object_uid;
		Vec3d hit_position, hit_normal;
		float hit_distance = 0.f;
		if(sub_lua_vm->gui_client->gameRaycast(script_evaluator->world_object->uid, (float)raw_max_distance,
			hit_object_uid, hit_position, hit_normal, hit_distance))
		{
			lua_createtable(state, /*num array elems=*/0, /*num non-array elems=*/5);
			script_evaluator->pushWorldObjectTableOntoStack(hit_object_uid);
			lua_setfield(state, -2, "object");
			lua_pushnumber(state, (double)hit_object_uid.value());
			lua_setfield(state, -2, "object_uid");
			LuaUtils::pushVec3d(state, hit_position);
			lua_setfield(state, -2, "position");
			LuaUtils::pushVec3d(state, hit_normal);
			lua_setfield(state, -2, "normal");
			lua_pushnumber(state, hit_distance);
			lua_setfield(state, -2, "distance");
			return 1;
		}
	}
#elif SERVER
	(void)raw_max_distance;
#endif
	lua_pushnil(state);
	return 1;
}


static int destroyTimer(lua_State* state)
{
	// arg 1: timer_id : Number

	checkNumArgs(state, /*num_args_required*/1);

	const int timer_id = (int)LuaUtils::getDoubleArg(state, /*index=*/1);
	if(timer_id < 0)
		return 0; // Negative values (including the client-side createGameLoop sentinel) are no-op handles.

	LuaScript* script = (LuaScript*)lua_getthreaddata(state);
	LuaScriptEvaluator* script_evaluator = (LuaScriptEvaluator*)script->userdata;

	for(int i=0; i<LuaScriptEvaluator::MAX_NUM_TIMERS; ++i)
		if(script_evaluator->timers[i].id == timer_id)
		{
			script_evaluator->destroyTimer(/*timer index=*/i);
			break;
		}
	
	return 0; // Count of returned values
}


static int getNumMaterials(lua_State* state)
{
	// arg 1: ob : WorldObject

	checkNumArgs(state, /*num_args_required*/1);

	// Get object UID
	//const UID uid((uint64)LuaUtils::getTableLightUserDataField(state, /*table index=*/1, "uid"));
	const UID ob_uid((uint64)LuaUtils::getTableNumberField(state, /*table index=*/1, "uid"));
	
	LuaScript* script = (LuaScript*)lua_getthreaddata(state); // NOTE: this double pointer-chasing sucks
	LuaScriptEvaluator* script_evaluator = (LuaScriptEvaluator*)script->userdata;

	WorldObject* ob = getWorldObjectForUID(script_evaluator, ob_uid);


	lua_pushnumber(state, (double)ob->materials.size());
	return 1; // Count of returned values
}


static int getMaterial(lua_State* state)
{
	// arg 1: ob : WorldObject
	// arg 2: index : Number

	checkNumArgs(state, /*num_args_required*/2);
	
	// Get object UID
	const UID ob_uid((uint64)LuaUtils::getTableNumberField(state, /*table index=*/1, "uid"));
	const size_t index = (size_t)LuaUtils::getDoubleArg(state, /*index=*/2);

	SubstrataLuaVM* sub_lua_vm = (SubstrataLuaVM*)lua_callbacks(state)->userdata;

	LuaScript* script = (LuaScript*)lua_getthreaddata(state); // NOTE: this double pointer-chasing sucks
	LuaScriptEvaluator* script_evaluator = (LuaScriptEvaluator*)script->userdata;

	WorldObject* ob = getWorldObjectForUID(script_evaluator, ob_uid);

	if(index >= ob->materials.size())
		throw glare::Exception("Invalid material index" + errorContextString(state));

	// Make a material table with object UID and material index
	lua_createtable(state, /*num array elems=*/0, /*num non-array elems=*/2);

	//LuaUtils::setNumberAsTableField(state, "uid", (double)uid.value());
	LuaUtils::setLightUserDataAsTableField(state, "uid", (void*)ob_uid.value());
	//LuaUtils::setNumberAsTableField(state, "idx", (double)index);
	LuaUtils::setLightUserDataAsTableField(state, "idx", (void*)index);

	// Set metatable to worldMaterialClassMetaTable_ref
	lua_getref(state, sub_lua_vm->worldMaterialClassMetaTable_ref); // Pushes worldObjectClassMetaTable_ref onto the stack.
	lua_setmetatable(state, -2); // "Pops a table from the stack and sets it as the new metatable for the value at the given acceptable index."

	return 1; // Count of returned values
}


static int playAudio(lua_State* state)
{
	// arg 1: ob : WorldObject

	checkNumArgs(state, /*num_args_required*/1);

#if GUI_CLIENT
	
	// Get object UID
	const UID ob_uid((uint64)LuaUtils::getTableNumberField(state, /*table index=*/1, "uid"));

	SubstrataLuaVM* sub_lua_vm = (SubstrataLuaVM*)lua_callbacks(state)->userdata;

	LuaScript* script = (LuaScript*)lua_getthreaddata(state); // NOTE: this double pointer-chasing sucks
	LuaScriptEvaluator* script_evaluator = (LuaScriptEvaluator*)script->userdata;

	WorldObject* ob = getWorldObjectForUID(script_evaluator, ob_uid);

	if(ob->audio_source)
		sub_lua_vm->gui_client->audio_engine.seekToStartAndUnpauseAudio(*ob->audio_source);
#endif

	return 0; // Count of returned values
}


static int pauseAudio(lua_State* state)
{
	checkNumArgs(state, /*num_args_required=*/1);
#if GUI_CLIENT
	const UID ob_uid((uint64)LuaUtils::getTableNumberField(state, /*table index=*/1, "uid"));
	SubstrataLuaVM* sub_lua_vm = (SubstrataLuaVM*)lua_callbacks(state)->userdata;
	LuaScript* script = (LuaScript*)lua_getthreaddata(state);
	LuaScriptEvaluator* script_evaluator = script ? (LuaScriptEvaluator*)script->userdata : nullptr;
	if(script_evaluator && script_evaluator->world_object && sub_lua_vm->gui_client)
	{
		WorldObject* ob = getWorldObjectForUID(script_evaluator, ob_uid);
		if(ob->audio_source)
			sub_lua_vm->gui_client->audio_engine.pauseAudio(*ob->audio_source);
	}
#endif
	return 0;
}


static int resumeAudio(lua_State* state)
{
	checkNumArgs(state, /*num_args_required=*/1);
#if GUI_CLIENT
	const UID ob_uid((uint64)LuaUtils::getTableNumberField(state, /*table index=*/1, "uid"));
	SubstrataLuaVM* sub_lua_vm = (SubstrataLuaVM*)lua_callbacks(state)->userdata;
	LuaScript* script = (LuaScript*)lua_getthreaddata(state);
	LuaScriptEvaluator* script_evaluator = script ? (LuaScriptEvaluator*)script->userdata : nullptr;
	if(script_evaluator && script_evaluator->world_object && sub_lua_vm->gui_client)
	{
		WorldObject* ob = getWorldObjectForUID(script_evaluator, ob_uid);
		if(ob->audio_source)
			sub_lua_vm->gui_client->audio_engine.resumeAudio(*ob->audio_source);
	}
#endif
	return 0;
}


static int isPlayingAudio(lua_State* state)
{
	// arg 1: ob : WorldObject

	checkNumArgs(state, /*num_args_required*/1);

	bool is_playing = false;
#if GUI_CLIENT
	
	// Get object UID
	const UID ob_uid((uint64)LuaUtils::getTableNumberField(state, /*table index=*/1, "uid"));

	LuaScript* script = (LuaScript*)lua_getthreaddata(state); // NOTE: this double pointer-chasing sucks
	LuaScriptEvaluator* script_evaluator = (LuaScriptEvaluator*)script->userdata;

	WorldObject* ob = getWorldObjectForUID(script_evaluator, ob_uid);

	if(ob->audio_source)
		is_playing = ob->audio_source->isPlaying();
#endif

	lua_pushboolean(state, is_playing ? 1 : 0);
	return 1; // Count of returned values
}


#if GUI_CLIENT
static WorldObject* getModelWorldObjectFromLuaSelf(lua_State* state)
{
	const UID ob_uid((uint64)LuaUtils::getTableNumberField(state, /*table index=*/1, "uid"));
	LuaScript* script = (LuaScript*)lua_getthreaddata(state);
	LuaScriptEvaluator* script_evaluator = script ? (LuaScriptEvaluator*)script->userdata : nullptr;
	return getWorldObjectForUID(script_evaluator, ob_uid);
}


static const AnimationData* getLoadedModelAnimationData(WorldObject* ob)
{
	return ob && ob->opengl_engine_ob && ob->opengl_engine_ob->mesh_data ?
		&ob->opengl_engine_ob->mesh_data->animation_data : nullptr;
}


static int getModelNodeIndexArg(lua_State* state, const AnimationData& animation_data, const int arg_index)
{
	if(lua_type(state, arg_index) == LUA_TSTRING)
		return animation_data.getNodeIndex(LuaUtils::getStringArg(state, arg_index));
	if(lua_type(state, arg_index) == LUA_TNUMBER)
	{
		const double index = LuaUtils::getDoubleArg(state, arg_index);
		if(std::isfinite(index) && index >= 0 && index < (double)animation_data.nodes.size() && std::floor(index) == index)
			return (int)index;
	}
	return -1;
}


static void pushModelNodeInfo(lua_State* state, const AnimationData& animation_data, const int node_index)
{
	const AnimationNodeData& node = animation_data.nodes[node_index];
	bool is_bone = false;
	for(int joint_node : animation_data.joint_nodes)
		if(joint_node == node_index) { is_bone = true; break; }
	lua_createtable(state, 0, 7);
	LuaUtils::setNumberAsTableField(state, "index", node_index);
	LuaUtils::setStringAsTableField(state, "name", node.name);
	LuaUtils::setNumberAsTableField(state, "parent_index", node.parent_index);
	lua_pushboolean(state, is_bone);
	lua_setfield(state, -2, "is_bone");
	LuaUtils::pushVec3f(state, Vec3f(node.trans));
	lua_setfield(state, -2, "translation");
	lua_createtable(state, 0, 4);
	lua_pushnumber(state, node.rot.v[0]); lua_setfield(state, -2, "x");
	lua_pushnumber(state, node.rot.v[1]); lua_setfield(state, -2, "y");
	lua_pushnumber(state, node.rot.v[2]); lua_setfield(state, -2, "z");
	lua_pushnumber(state, node.rot.v[3]); lua_setfield(state, -2, "w");
	lua_setfield(state, -2, "rotation");
	LuaUtils::pushVec3f(state, Vec3f(node.scale));
	lua_setfield(state, -2, "scale");
}


static int getNodeCount(lua_State* state)
{
	checkNumArgs(state, 1);
	WorldObject* ob = getModelWorldObjectFromLuaSelf(state);
	const AnimationData* data = getLoadedModelAnimationData(ob);
	lua_pushnumber(state, data ? data->nodes.size() : 0);
	return 1;
}


static int getNodeIndex(lua_State* state)
{
	checkNumArgs(state, 2);
	WorldObject* ob = getModelWorldObjectFromLuaSelf(state);
	const AnimationData* data = getLoadedModelAnimationData(ob);
	const int index = data && lua_type(state, 2) == LUA_TSTRING ? data->getNodeIndex(LuaUtils::getStringArg(state, 2)) : -1;
	lua_pushnumber(state, index);
	return 1;
}


static int getNodeInfo(lua_State* state)
{
	checkNumArgs(state, 2);
	WorldObject* ob = getModelWorldObjectFromLuaSelf(state);
	const AnimationData* data = getLoadedModelAnimationData(ob);
	const int node_index = data ? getModelNodeIndexArg(state, *data, 2) : -1;
	if(node_index < 0) lua_pushnil(state);
	else pushModelNodeInfo(state, *data, node_index);
	return 1;
}


static int getBoneCount(lua_State* state)
{
	checkNumArgs(state, 1);
	WorldObject* ob = getModelWorldObjectFromLuaSelf(state);
	const AnimationData* data = getLoadedModelAnimationData(ob);
	lua_pushnumber(state, data ? data->joint_nodes.size() : 0);
	return 1;
}


static int getBoneInfo(lua_State* state)
{
	checkNumArgs(state, 2);
	WorldObject* ob = getModelWorldObjectFromLuaSelf(state);
	const AnimationData* data = getLoadedModelAnimationData(ob);
	const double slot = LuaUtils::getDoubleArg(state, 2);
	if(!data || !std::isfinite(slot) || slot < 0 || slot >= (double)data->joint_nodes.size() || std::floor(slot) != slot)
		lua_pushnil(state);
	else
	{
		const int node_index = data->joint_nodes[(size_t)slot];
		pushModelNodeInfo(state, *data, node_index);
		LuaUtils::setNumberAsTableField(state, "bone_index", slot);
	}
	return 1;
}


static int setNodeTransform(lua_State* state)
{
	checkNumArgs(state, 3);
	WorldObject* ob = getModelWorldObjectFromLuaSelf(state);
	const AnimationData* data = getLoadedModelAnimationData(ob);
	const int node_index = data ? getModelNodeIndexArg(state, *data, 2) : -1;
	if(node_index < 0 || !ob->opengl_engine_ob || data->joint_nodes.empty())
	{
		lua_pushboolean(state, false);
		return 1;
	}
	LuaUtils::checkValueIsTable(state, 3);
	const Vec3f translation = LuaUtils::getTableVec3fFieldWithDefault(state, 3, "translation", Vec3f(0.f));
	const Vec3f scale = LuaUtils::getTableVec3fFieldWithDefault(state, 3, "scale", Vec3f(1.f));
	Quatf rotation = Quatf::identity();
	lua_getfield(state, 3, "rotation");
	if(!lua_isnil(state, -1))
	{
		LuaUtils::checkValueIsTable(state, -1);
		const float x = (float)LuaUtils::getTableNumberField(state, -1, "x");
		const float y = (float)LuaUtils::getTableNumberField(state, -1, "y");
		const float z = (float)LuaUtils::getTableNumberField(state, -1, "z");
		const float w = (float)LuaUtils::getTableNumberField(state, -1, "w");
		const Quatf requested(Vec4f(x, y, z, w));
		if(!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || !std::isfinite(w) || requested.length() < 1.0e-6f)
			throw glare::Exception("setNodeTransform(): rotation must be a finite non-zero quaternion" + errorContextString(state));
		rotation = normalise(requested);
	}
	lua_pop(state, 1);
	if(!translation.isFinite() || !scale.isFinite() || std::fabs(translation[0]) > 10000.f || std::fabs(translation[1]) > 10000.f || std::fabs(translation[2]) > 10000.f ||
		std::fabs(scale[0]) < 0.001f || std::fabs(scale[1]) < 0.001f || std::fabs(scale[2]) < 0.001f || std::fabs(scale[0]) > 1000.f || std::fabs(scale[1]) > 1000.f || std::fabs(scale[2]) > 1000.f)
		throw glare::Exception("setNodeTransform(): translation or scale is outside the supported finite bounds" + errorContextString(state));
	auto& node_data = ob->opengl_engine_ob->anim_node_data;
	if(node_data.size() < data->nodes.size()) node_data.resize(data->nodes.size());
	node_data[(size_t)node_index].procedural_transform = Matrix4f::translationMatrix(translation[0], translation[1], translation[2]) * rotation.toMatrix() * Matrix4f::scaleMatrix(scale[0], scale[1], scale[2]);
	lua_pushboolean(state, true);
	return 1;
}


static int clearNodeTransform(lua_State* state)
{
	checkNumArgs(state, 2);
	WorldObject* ob = getModelWorldObjectFromLuaSelf(state);
	const AnimationData* data = getLoadedModelAnimationData(ob);
	const int node_index = data ? getModelNodeIndexArg(state, *data, 2) : -1;
	if(node_index < 0 || !ob->opengl_engine_ob || data->joint_nodes.empty())
	{
		lua_pushboolean(state, false);
		return 1;
	}
	auto& node_data = ob->opengl_engine_ob->anim_node_data;
	if(node_data.size() < data->nodes.size()) node_data.resize(data->nodes.size());
	node_data[(size_t)node_index].procedural_transform = Matrix4f::identity();
	lua_pushboolean(state, true);
	return 1;
}
#else
static int getNodeCount(lua_State* state) { (void)state; return 0; }
static int getNodeIndex(lua_State* state) { (void)state; return 0; }
static int getNodeInfo(lua_State* state) { (void)state; return 0; }
static int getBoneCount(lua_State* state) { (void)state; return 0; }
static int getBoneInfo(lua_State* state) { (void)state; return 0; }
static int setNodeTransform(lua_State* state) { (void)state; return 0; }
static int clearNodeTransform(lua_State* state) { (void)state; return 0; }
#endif


static int startPlayingAnimation(lua_State* state)
{
	// arg 1: ob : WorldObject
	// arg 2: animation_index : Number or exact name
	// arg 3: optional elapsed animation time, for synchronized game cues

	checkNumArgs(state, /*num_args_required*/2);
	if(lua_gettop(state) > 3)
		throw glare::Exception("startPlayingAnimation() accepts an optional elapsed animation time as its third argument" + errorContextString(state));
	bool started = false;

#if GUI_CLIENT
	
	// Get object UID from arg 1.
	const UID ob_uid((uint64)LuaUtils::getTableNumberField(state, /*table index=*/1, "uid"));

	LuaScript* script = (LuaScript*)lua_getthreaddata(state); // NOTE: this double pointer-chasing sucks
	LuaScriptEvaluator* script_evaluator = (LuaScriptEvaluator*)script->userdata;

	WorldObject* ob = getWorldObjectForUID(script_evaluator, ob_uid);
	
	if(ob->opengl_engine_ob && ob->opengl_engine_ob->mesh_data)
	{
		const size_t animation_count = ob->opengl_engine_ob->mesh_data->animation_data.animations.size();
		int animation_index = -1;
		if(lua_type(state, /*index=*/2) == LUA_TSTRING)
		{
			const std::string animation_name = LuaUtils::getStringArg(state, /*index=*/2);
			animation_index = ob->opengl_engine_ob->mesh_data->animation_data.getAnimationIndex(animation_name);
		}
		else if(lua_type(state, /*index=*/2) == LUA_TNUMBER)
		{
			const double requested_index = LuaUtils::getDoubleArg(state, /*index=*/2);
			if(isFinite(requested_index) && requested_index >= 0.0 &&
				requested_index < (double)animation_count && std::floor(requested_index) == requested_index)
				animation_index = (int)requested_index;
		}
		if(animation_index >= 0 && (size_t)animation_index < animation_count)
		{
			double elapsed_animation_time = 0;
			if(lua_gettop(state) >= 3 && !lua_isnil(state, 3))
			{
				elapsed_animation_time = LuaUtils::getDoubleArg(state, /*index=*/3);
				if(!std::isfinite(elapsed_animation_time) || elapsed_animation_time < 0 || elapsed_animation_time > 3600)
					throw glare::Exception("startPlayingAnimation(): elapsed animation time must be finite and in [0, 3600] seconds" + errorContextString(state));
			}
			if(script_evaluator->substrata_lua_vm->gui_client)
				script_evaluator->substrata_lua_vm->gui_client->setScriptAnimationPaused(ob_uid, false);
			if(ob->opengl_engine_ob->next_anim_i >= 0 && (size_t)ob->opengl_engine_ob->next_anim_i < animation_count)
				ob->opengl_engine_ob->current_anim_i = ob->opengl_engine_ob->next_anim_i;

			ob->opengl_engine_ob->next_anim_i = animation_index;

			const double cur_time = Clock::getTimeSinceInit();
			const double TRANSITION_PERIOD = 0.3; // TODO: set this with a default arg?
			ob->opengl_engine_ob->use_time_offset = elapsed_animation_time - cur_time;

			ob->opengl_engine_ob->transition_start_time = cur_time;
			ob->opengl_engine_ob->transition_end_time   = cur_time + TRANSITION_PERIOD;
			started = true;
		}
	}
#endif

	lua_pushboolean(state, started ? 1 : 0);
	return 1; // Whether a loaded animation clip was selected.
}


static int stopPlayingAnimation(lua_State* state)
{
	checkNumArgs(state, /*num_args_required=*/1);
	bool stopped = false;
#if GUI_CLIENT
	const UID ob_uid((uint64)LuaUtils::getTableNumberField(state, /*table index=*/1, "uid"));
	LuaScript* script = (LuaScript*)lua_getthreaddata(state);
	LuaScriptEvaluator* script_evaluator = script ? (LuaScriptEvaluator*)script->userdata : nullptr;
	if(script_evaluator && script_evaluator->substrata_lua_vm->gui_client)
	{
		WorldObject* ob = getWorldObjectForUID(script_evaluator, ob_uid);
		if(ob->opengl_engine_ob && ob->opengl_engine_ob->mesh_data &&
			!ob->opengl_engine_ob->mesh_data->animation_data.animations.empty())
		{
			script_evaluator->substrata_lua_vm->gui_client->setScriptAnimationPaused(ob_uid, true);
			stopped = true;
		}
	}
#endif
	lua_pushboolean(state, stopped ? 1 : 0);
	return 1;
}


static int resumePlayingAnimation(lua_State* state)
{
	checkNumArgs(state, /*num_args_required=*/1);
	bool resumed = false;
#if GUI_CLIENT
	const UID ob_uid((uint64)LuaUtils::getTableNumberField(state, /*table index=*/1, "uid"));
	LuaScript* script = (LuaScript*)lua_getthreaddata(state);
	LuaScriptEvaluator* script_evaluator = script ? (LuaScriptEvaluator*)script->userdata : nullptr;
	if(script_evaluator && script_evaluator->substrata_lua_vm->gui_client)
	{
		WorldObject* ob = getWorldObjectForUID(script_evaluator, ob_uid);
		if(ob->opengl_engine_ob && ob->opengl_engine_ob->mesh_data &&
			!ob->opengl_engine_ob->mesh_data->animation_data.animations.empty())
		{
			script_evaluator->substrata_lua_vm->gui_client->setScriptAnimationPaused(ob_uid, false);
			resumed = true;
		}
	}
#endif
	lua_pushboolean(state, resumed ? 1 : 0);
	return 1;
}


// Try and find an animation for an object by name.  Return -1 if no such animation found.
static int getAnimationIndex(lua_State* state)
{
	// arg 1: ob : WorldObject
	// arg 2: animation_name: String

	checkNumArgs(state, /*num_args_required*/2);

	int result_anim_index = -1;
#if GUI_CLIENT
	
	// Get object UID
	const UID ob_uid((uint64)LuaUtils::getTableNumberField(state, /*table index=*/1, "uid"));

	const std::string animation_name = LuaUtils::getStringArg(state, /*index=*/2);

	LuaScript* script = (LuaScript*)lua_getthreaddata(state); // NOTE: this double pointer-chasing sucks
	LuaScriptEvaluator* script_evaluator = (LuaScriptEvaluator*)script->userdata;

	WorldObject* ob = getWorldObjectForUID(script_evaluator, ob_uid);
	
	if(ob->opengl_engine_ob && ob->opengl_engine_ob->mesh_data)
		result_anim_index = ob->opengl_engine_ob->mesh_data->animation_data.getAnimationIndex(animation_name);

#endif
	lua_pushnumber(state, result_anim_index);

	return 1; // Count of returned values
}


static int getAnimationCount(lua_State* state)
{
	checkNumArgs(state, 1);
	int count = 0;
#if GUI_CLIENT
	WorldObject* ob = getModelWorldObjectFromLuaSelf(state);
	const AnimationData* data = getLoadedModelAnimationData(ob);
	if(data) count = (int)data->animations.size();
#endif
	lua_pushnumber(state, count);
	return 1;
}


static int getAnimationName(lua_State* state)
{
	checkNumArgs(state, 2);
#if GUI_CLIENT
	WorldObject* ob = getModelWorldObjectFromLuaSelf(state);
	const AnimationData* data = getLoadedModelAnimationData(ob);
	const double index = LuaUtils::getDoubleArg(state, 2);
	if(data && std::isfinite(index) && index >= 0 && index < (double)data->animations.size() && std::floor(index) == index)
		LuaUtils::pushString(state, data->animations[(size_t)index]->name);
	else
		lua_pushnil(state);
#else
	lua_pushnil(state);
#endif
	return 1;
}


#if GUI_CLIENT
static bool getParticleEmitterObjectFromLuaArg(lua_State* state, WorldObject*& ob_out)
{
	ob_out = NULL;
	const UID ob_uid((uint64)LuaUtils::getTableNumberField(state, /*table index=*/1, "uid"));

	LuaScript* script = (LuaScript*)lua_getthreaddata(state);
	LuaScriptEvaluator* script_evaluator = (LuaScriptEvaluator*)script->userdata;
	WorldObject* ob = getWorldObjectForUID(script_evaluator, ob_uid);
	if(!ParticleEmitterSettings::isParticleEmitterContent(ob->content))
		return false;

	ob_out = ob;
	return true;
}
#endif


static int particleEmitterStart(lua_State* state)
{
	checkNumArgs(state, /*num_args_required=*/1);

	bool ok = false;
#if GUI_CLIENT
	WorldObject* ob = NULL;
	ok = getParticleEmitterObjectFromLuaArg(state, ob);
	if(ok)
	{
		SubstrataLuaVM* sub_lua_vm = (SubstrataLuaVM*)lua_callbacks(state)->userdata;
		sub_lua_vm->gui_client->setParticleEmitterRuntimePaused(ob->uid, false);
	}
#endif
	lua_pushboolean(state, ok ? 1 : 0);
	return 1;
}


static int particleEmitterStop(lua_State* state)
{
	checkNumArgs(state, /*num_args_required=*/1);

	bool ok = false;
#if GUI_CLIENT
	WorldObject* ob = NULL;
	ok = getParticleEmitterObjectFromLuaArg(state, ob);
	if(ok)
	{
		SubstrataLuaVM* sub_lua_vm = (SubstrataLuaVM*)lua_callbacks(state)->userdata;
		sub_lua_vm->gui_client->setParticleEmitterRuntimePaused(ob->uid, true);
	}
#endif
	lua_pushboolean(state, ok ? 1 : 0);
	return 1;
}


static int particleEmitterBurst(lua_State* state)
{
	const int num_args = lua_gettop(state);
	if(num_args < 1 || num_args > 2)
		throw glare::Exception("burst(): Expected 1 or 2 arguments" + errorContextString(state));

	bool ok = false;
#if GUI_CLIENT
	WorldObject* ob = NULL;
	ok = getParticleEmitterObjectFromLuaArg(state, ob);
	if(ok)
	{
		ParticleEmitterSettings settings = ParticleEmitterSettings::fromContent(ob->content);
		int burst_count = myMax(settings.burst_count, 1);
		if(num_args >= 2)
			burst_count = myClamp((int)LuaUtils::getDoubleArg(state, /*index=*/2), 1, 512);

		SubstrataLuaVM* sub_lua_vm = (SubstrataLuaVM*)lua_callbacks(state)->userdata;
		sub_lua_vm->gui_client->triggerParticleEmitterBurst(ob->uid, burst_count);
	}
#endif
	lua_pushboolean(state, ok ? 1 : 0);
	return 1;
}


static int particleEmitterClear(lua_State* state)
{
	checkNumArgs(state, /*num_args_required=*/1);

	bool ok = false;
#if GUI_CLIENT
	WorldObject* ob = NULL;
	ok = getParticleEmitterObjectFromLuaArg(state, ob);
	if(ok)
	{
		SubstrataLuaVM* sub_lua_vm = (SubstrataLuaVM*)lua_callbacks(state)->userdata;
		sub_lua_vm->gui_client->clearParticleEmitterParticles(ob->uid);
	}
#endif
	lua_pushboolean(state, ok ? 1 : 0);
	return 1;
}


// C++ implementation of __index for WorldObject class. Used when a WorldObject table field is read from.
static int worldObjectClassIndexMetaMethod(lua_State* state)
{
	// arg 1 is table (WorldObject)
	// arg 2 is the key (method name)

	assert(lua_gettop(state) == 2); // Should be 2 args.
	checkNumArgs(state, 2);

	// Get object UID
	const UID uid((uint64)LuaUtils::getTableNumberField(state, /*table index=*/1, "uid"));

	LuaScript* script = (LuaScript*)lua_getthreaddata(state); // NOTE: this double pointer-chasing sucks
	LuaScriptEvaluator* script_evaluator = (LuaScriptEvaluator*)script->userdata;

	WorldObject* ob = getWorldObjectForUID(script_evaluator, uid);

	int atom = -1;
	const char* key_str = LuaUtils::getStringAndAtom(state, /*index=*/2, atom);
	switch(atom) // NOTE: The switch cases should be in the same order as the Atom enum values to ensure nice code-gen.
	{
	case Atom_model_url:
		assert(stringEqual(key_str, "model_url"));
		LuaUtils::pushString(state, ob->model_url);
		break;
	case Atom_pos:
		assert(stringEqual(key_str, "pos"));
		LuaUtils::pushVec3d(state, ob->pos);
		break;
	case Atom_axis:
		assert(stringEqual(key_str, "axis"));
		LuaUtils::pushVec3f(state, ob->axis);
		break;
	case Atom_angle:
		assert(stringEqual(key_str, "angle"));
		lua_pushnumber(state, ob->angle);
		break;
	case Atom_scale:
		assert(stringEqual(key_str, "scale"));
		LuaUtils::pushVec3f(state, ob->scale);
		break;
	case Atom_collidable:
		assert(stringEqual(key_str, "collidable"));
		lua_pushboolean(state, ob->isCollidable());
		break;
	case Atom_dynamic:
		assert(stringEqual(key_str, "dynamic"));
		lua_pushboolean(state, ob->isDynamic());
		break;
	case Atom_sensor:
		assert(stringEqual(key_str, "sensor"));
		lua_pushboolean(state, ob->isSensor());
		break;
	case Atom_content:
		assert(stringEqual(key_str, "content"));
		LuaUtils::pushString(state, ob->content);
		break;
	case Atom_target_url:
		assert(stringEqual(key_str, "target_url"));
		LuaUtils::pushString(state, ob->target_url);
		break;
	case Atom_video_autoplay:
		assert(stringEqual(key_str, "video_autoplay"));
		lua_pushboolean(state, BitUtils::isBitSet(ob->flags, WorldObject::VIDEO_AUTOPLAY));
		break;
	case Atom_video_loop:
		assert(stringEqual(key_str, "video_loop"));
		lua_pushboolean(state, BitUtils::isBitSet(ob->flags, WorldObject::VIDEO_LOOP));
		break;
	case Atom_video_muted:
		assert(stringEqual(key_str, "video_muted"));
		lua_pushboolean(state, BitUtils::isBitSet(ob->flags, WorldObject::VIDEO_MUTED));
		break;
	case Atom_mass:
		assert(stringEqual(key_str, "mass"));
		lua_pushnumber(state, ob->mass);
		break;
	case Atom_friction:
		assert(stringEqual(key_str, "friction"));
		lua_pushnumber(state, ob->friction);
		break;	
	case Atom_restitution:
		assert(stringEqual(key_str, "restitution"));
		lua_pushnumber(state, ob->restitution);
		break;
	case Atom_centre_of_mass_offset_os:
		assert(stringEqual(key_str, "centre_of_mass_offset_os"));
		LuaUtils::pushVec3f(state, ob->centre_of_mass_offset_os);
		break;
	case Atom_audio_source_url:
		assert(stringEqual(key_str, "audio_source_url"));
		LuaUtils::pushString(state, ob->audio_source_url);
		break;
	case Atom_audio_volume:
		assert(stringEqual(key_str, "audio_volume"));
		lua_pushnumber(state, ob->audio_volume);
		break;
	case Atom_getNumMaterials:
		assert(stringEqual(key_str, "getNumMaterials"));
		lua_pushcfunction(state, getNumMaterials, "getNumMaterials");
		break;
	case Atom_getMaterial:
		assert(stringEqual(key_str, "getMaterial"));
		lua_pushcfunction(state, getMaterial, "getMaterial");
		break;
	case Atom_playAudio:
		assert(stringEqual(key_str, "playAudio"));
		lua_pushcfunction(state, playAudio, "playAudio");
		break;
	case Atom_pauseAudio:
		assert(stringEqual(key_str, "pauseAudio"));
		lua_pushcfunction(state, pauseAudio, "pauseAudio");
		break;
	case Atom_resumeAudio:
		assert(stringEqual(key_str, "resumeAudio"));
		lua_pushcfunction(state, resumeAudio, "resumeAudio");
		break;
	case Atom_isPlayingAudio:
		assert(stringEqual(key_str, "isPlayingAudio"));
		lua_pushcfunction(state, isPlayingAudio, "isPlayingAudio");
		break;
	case Atom_startPlayingAnimation:
		assert(stringEqual(key_str, "startPlayingAnimation"));
		lua_pushcfunction(state, startPlayingAnimation, "startPlayingAnimation");
		break;
	case Atom_stopPlayingAnimation:
		assert(stringEqual(key_str, "stopPlayingAnimation"));
		lua_pushcfunction(state, stopPlayingAnimation, "stopPlayingAnimation");
		break;
	case Atom_resumePlayingAnimation:
		assert(stringEqual(key_str, "resumePlayingAnimation"));
		lua_pushcfunction(state, resumePlayingAnimation, "resumePlayingAnimation");
		break;
	case Atom_getAnimationIndex:
		assert(stringEqual(key_str, "getAnimationIndex"));
		lua_pushcfunction(state, getAnimationIndex, "getAnimationIndex");
		break;
	case Atom_getAnimationCount:
		assert(stringEqual(key_str, "getAnimationCount"));
		lua_pushcfunction(state, getAnimationCount, "getAnimationCount");
		break;
	case Atom_getAnimationName:
		assert(stringEqual(key_str, "getAnimationName"));
		lua_pushcfunction(state, getAnimationName, "getAnimationName");
		break;
	case Atom_getNodeCount:
		assert(stringEqual(key_str, "getNodeCount"));
		lua_pushcfunction(state, getNodeCount, "getNodeCount");
		break;
	case Atom_getNodeIndex:
		assert(stringEqual(key_str, "getNodeIndex"));
		lua_pushcfunction(state, getNodeIndex, "getNodeIndex");
		break;
	case Atom_getNodeInfo:
		assert(stringEqual(key_str, "getNodeInfo"));
		lua_pushcfunction(state, getNodeInfo, "getNodeInfo");
		break;
	case Atom_getBoneCount:
		assert(stringEqual(key_str, "getBoneCount"));
		lua_pushcfunction(state, getBoneCount, "getBoneCount");
		break;
	case Atom_getBoneInfo:
		assert(stringEqual(key_str, "getBoneInfo"));
		lua_pushcfunction(state, getBoneInfo, "getBoneInfo");
		break;
	case Atom_setNodeTransform:
		assert(stringEqual(key_str, "setNodeTransform"));
		lua_pushcfunction(state, setNodeTransform, "setNodeTransform");
		break;
	case Atom_clearNodeTransform:
		assert(stringEqual(key_str, "clearNodeTransform"));
		lua_pushcfunction(state, clearNodeTransform, "clearNodeTransform");
		break;
	case Atom_start:
		assert(stringEqual(key_str, "start"));
		lua_pushcfunction(state, particleEmitterStart, "start");
		break;
	case Atom_stop:
		assert(stringEqual(key_str, "stop"));
		lua_pushcfunction(state, particleEmitterStop, "stop");
		break;
	case Atom_burst:
		assert(stringEqual(key_str, "burst"));
		lua_pushcfunction(state, particleEmitterBurst, "burst");
		break;
	case Atom_clearParticles:
		assert(stringEqual(key_str, "clearParticles"));
		lua_pushcfunction(state, particleEmitterClear, "clearParticles");
		break;
	case Atom_particleStart:
		assert(stringEqual(key_str, "particleStart"));
		lua_pushcfunction(state, particleEmitterStart, "particleStart");
		break;
	case Atom_particleStop:
		assert(stringEqual(key_str, "particleStop"));
		lua_pushcfunction(state, particleEmitterStop, "particleStop");
		break;
	case Atom_particleBurst:
		assert(stringEqual(key_str, "particleBurst"));
		lua_pushcfunction(state, particleEmitterBurst, "particleBurst");
		break;
	case Atom_particleClear:
		assert(stringEqual(key_str, "particleClear"));
		lua_pushcfunction(state, particleEmitterClear, "particleClear");
		break;
	case Atom_audio_loop:
		assert(stringEqual(key_str, "audio_loop"));
		lua_pushboolean(state, BitUtils::isBitSet(ob->flags, WorldObject::AUDIO_LOOP));
		break;

	default:
		throw glare::Exception("Unknown field '" + std::string(key_str) + "'" + errorContextString(state));
	}

	return 1; // Count of returned values
}


#if SERVER
template <class StringType>
static void assignStringWithSizeCheck(lua_State* state, int index, StringType& field, const char* field_name, size_t max_size)
{
	size_t new_len;
	const char* new_string = LuaUtils::getStringPointerAndLen(state, index, new_len);
	if(new_len > max_size)
		throw glare::Exception("New " + std::string(field_name) + " length too long. (string had length " + toString(new_len) + ", max len is " + toString(max_size) + ")" + errorContextString(state));
	
	field.assign(new_string, new_len);
}
#endif


// C++ implementation of __newindex for WorldObject class.  Used when a value is assigned to a WorldObject field
static int worldObjectClassNewIndexMetaMethod(lua_State* state)
{
#if GUI_CLIENT
	return 0; // Count of returned values
#else

	// Arg 1: table 
	// Arg 2: key
	// Arg 3: value

	assert(lua_gettop(state) == 3); // Should be 3 args.
	checkNumArgs(state, 3);
	
	// Get object UID
	const UID uid((uint64)LuaUtils::getTableNumberField(state, /*table index=*/1, "uid"));

	SubstrataLuaVM* sub_lua_vm = (SubstrataLuaVM*)lua_callbacks(state)->userdata;
	LuaScript* script = (LuaScript*)lua_getthreaddata(state); // NOTE: this double pointer-chasing sucks
	LuaScriptEvaluator* script_evaluator = (LuaScriptEvaluator*)script->userdata;

	if(script_evaluator->cur_world_state_lock == nullptr)
	{
		assert(0);
		throw glare::Exception("Internal error: cur_world_state_lock was null");
	}

	WorldObject* ob = getWorldObjectForUID(script_evaluator, uid);

	// Check permissions before we update object.
	// A script has permissions to modify an object if and only if the creator of the script is also the creator of the object.
	if(ob->creator_id != script_evaluator->world_object->creator_id)
		throw glare::Exception("Script does not have permissions to modify object (ob UID: " + uid.toString() + ")");
	if(ob->game_spawn_owner_uid.valid() && (!sub_lua_vm || !sub_lua_vm->server || sub_lua_vm->server->world_state->read_only_mode))
		throw glare::Exception("Game-spawned objects cannot be modified without a writable authoritative server.");
	if(ob->game_spawn_owner_uid.valid() && !script_evaluator->world_state->allowGameObjectPropertyUpdate(
		ob->game_spawn_owner_uid, *script_evaluator->cur_world_state_lock))
		throw glare::Exception("The game reached its object-property update limit (600 per game and 12,000 per world per second).");


	bool transform_changed = false;
	bool other_changed = false;
	const Vec3d old_pos = ob->pos;
	const Vec3f old_axis = ob->axis;
	const float old_angle = ob->angle;
	const Vec3f old_scale = ob->scale;

	int atom = -1;
	const char* key_str = LuaUtils::getStringAndAtom(state, /*index=*/2, atom);
	switch(atom) // NOTE: The switch cases should be in the same order as the Atom enum values to ensure nice code-gen.
	{
	case Atom_model_url:
		assert(stringEqual(key_str, "model_url"));

		assignStringWithSizeCheck(state, /*index=*/3, /*field=*/ob->model_url, /*field name=*/"model_url", /*max size=*/WorldObject::MAX_URL_SIZE);
		ob->from_remote_model_url_dirty = true; // TODO: rename

#if SERVER
		script_evaluator->world_state->getDirtyFromRemoteObjects(*script_evaluator->cur_world_state_lock).insert(ob);
#endif
		break;
	case Atom_pos:
		assert(stringEqual(key_str, "pos"));
		{
			const Vec3d position = LuaUtils::getVec3d(state, /*index=*/3);
			if(!position.isFinite() || std::abs(position.x) > 1.0e7 || std::abs(position.y) > 1.0e7 || std::abs(position.z) > 1.0e7)
				throw glare::Exception("object.pos must be finite and within +/-10,000,000 metres" + errorContextString(state));
			ob->pos = position;
		}
		transform_changed = true;
		break;
	case Atom_axis:
		assert(stringEqual(key_str, "axis"));
		{
			const Vec3f axis = LuaUtils::getVec3f(state, /*index=*/3);
			const float axis_length = axis.length();
			if(!axis.isFinite() || !isFinite(axis_length) || axis_length < 1.0e-6f || axis_length > 10000.f)
				throw glare::Exception("object.axis must be finite and non-zero" + errorContextString(state));
			ob->axis = normalise(axis);
		}
		transform_changed = true;
		break;
	case Atom_angle:
		assert(stringEqual(key_str, "angle"));
		{
			const float angle = LuaUtils::getFloat(state, /*index=*/3);
			if(!isFinite(angle) || std::abs(angle) > 6.2831854f)
			throw glare::Exception("object.angle must be finite and within -2*pi..2*pi radians" + errorContextString(state));
			ob->angle = angle;
		}
		transform_changed = true;
		break;
	case Atom_scale:
		assert(stringEqual(key_str, "scale"));
		{
			const Vec3f scale = LuaUtils::getVec3f(state, /*index=*/3);
			if(!scale.isFinite() || scale.x <= 0 || scale.y <= 0 || scale.z <= 0 || scale.x > 100 || scale.y > 100 || scale.z > 100)
				throw glare::Exception("object.scale components must be finite and between 0 and 100" + errorContextString(state));
			ob->scale = scale;
		}
		transform_changed = true;
		break;
	case Atom_collidable:
		assert(stringEqual(key_str, "collidable"));
		ob->setCollidable(LuaUtils::getBool(state, /*index=*/3));
		other_changed = true;
		break;
	case Atom_dynamic:
		ob->setDynamic(LuaUtils::getBool(state, /*index=*/3));
		other_changed = true;
		break;
	case Atom_sensor:
		ob->setIsSensor(LuaUtils::getBool(state, /*index=*/3));
		other_changed = true;
		break;
	case Atom_content:
		{
		assert(stringEqual(key_str, "content"));

		assignStringWithSizeCheck(state, /*index=*/3, /*field=*/ob->content, /*field name=*/"content", /*max size=*/WorldObject::MAX_CONTENT_SIZE);

		ob->from_remote_content_dirty = true; // TODO: rename
		script_evaluator->world_state->getDirtyFromRemoteObjects(*script_evaluator->cur_world_state_lock).insert(ob);
		break;
		}
	case Atom_target_url:
		{
		assert(stringEqual(key_str, "target_url"));

		assignStringWithSizeCheck(state, /*index=*/3, /*field=*/ob->target_url, /*field name=*/"target_url", /*max size=*/WorldObject::MAX_URL_SIZE);

		ob->from_remote_other_dirty = true; // TODO: rename
		script_evaluator->world_state->getDirtyFromRemoteObjects(*script_evaluator->cur_world_state_lock).insert(ob);
		break;
		}
	case Atom_video_autoplay:
		assert(stringEqual(key_str, "video_autoplay"));
		BitUtils::setOrZeroBit(ob->flags, WorldObject::VIDEO_AUTOPLAY, LuaUtils::getBool(state, /*index=*/3));
		other_changed = true;
		break;
	case Atom_video_loop:
		assert(stringEqual(key_str, "video_loop"));
		BitUtils::setOrZeroBit(ob->flags, WorldObject::VIDEO_LOOP, LuaUtils::getBool(state, /*index=*/3));
		other_changed = true;
		break;
	case Atom_video_muted:
		assert(stringEqual(key_str, "video_muted"));
		BitUtils::setOrZeroBit(ob->flags, WorldObject::VIDEO_MUTED, LuaUtils::getBool(state, /*index=*/3));
		other_changed = true;
		break;
	case Atom_mass:
		assert(stringEqual(key_str, "mass"));
		ob->mass = LuaUtils::getFloat(state, /*index=*/3);
		other_changed = true;
		break;
	case Atom_friction:
		assert(stringEqual(key_str, "friction"));
		ob->friction = LuaUtils::getFloat(state, /*index=*/3);
		other_changed = true;
		break;	
	case Atom_restitution:
		assert(stringEqual(key_str, "restitution"));
		ob->restitution = LuaUtils::getFloat(state, /*index=*/3);
		other_changed = true;
		break;
	case Atom_centre_of_mass_offset_os:
		assert(stringEqual(key_str, "centre_of_mass_offset_os"));
		ob->centre_of_mass_offset_os = LuaUtils::getVec3f(state, /*index=*/3);
		other_changed = true;
		break;
	case Atom_audio_source_url:
		assert(stringEqual(key_str, "audio_source_url"));

		assignStringWithSizeCheck(state, /*index=*/3, /*field=*/ob->audio_source_url, /*field name=*/"audio_source_url", /*max size=*/WorldObject::MAX_URL_SIZE);

		other_changed = true;
		break;
	case Atom_audio_volume:
		assert(stringEqual(key_str, "audio_volume"));
		ob->audio_volume = LuaUtils::getFloat(state, /*index=*/3);
		other_changed = true;
		break;
	case Atom_audio_loop:
		assert(stringEqual(key_str, "audio_loop"));
		BitUtils::setOrZeroBit(ob->flags, WorldObject::AUDIO_LOOP, LuaUtils::getBool(state, /*index=*/3));
		other_changed = true;
		break;
	default:
		throw glare::Exception("Unknown field '" + std::string(key_str) + "'");
	}

	if(transform_changed)
	{
		auto restore_old_transform = [&]() {
			ob->setTransformAndHistory(old_pos, old_axis, old_angle);
			ob->scale = old_scale;
			ob->transformChanged();
		};
		WorldObject candidate;
		candidate.copyNetworkStateFrom(*ob);
		candidate.transformChanged();
		if(!userHasObjectCreationPermissions(candidate, script_evaluator->world_object->creator_id,
			*script_evaluator->world_state, *script_evaluator->cur_world_state_lock))
		{
			restore_old_transform();
			throw glare::Exception("The script owner does not have permission to move this object to that position.");
		}
		if(ob->game_spawn_owner_uid.valid() && !script_evaluator->world_state->allowGameObjectTransformUpdate(
			ob->game_spawn_owner_uid, *script_evaluator->cur_world_state_lock))
		{
			restore_old_transform();
			throw glare::Exception("The game reached its object-transform update limit.");
		}
		ob->setTransformAndHistory(ob->pos, ob->axis, ob->angle);
		ob->transformChanged();
		if(ob->game_spawn_owner_uid.valid())
			script_evaluator->world_state->refreshGameRaySpatialIndexObject(*ob, *script_evaluator->cur_world_state_lock);
		ob->last_transform_update_avatar_uid = std::numeric_limits<uint32>::max();
		ob->from_remote_transform_dirty = true; // TODO: rename
		script_evaluator->world_state->getDirtyFromRemoteObjects(*script_evaluator->cur_world_state_lock).insert(ob);
	}
	else if(other_changed)
	{
		ob->from_remote_other_dirty = true; // TODO: rename
		script_evaluator->world_state->getDirtyFromRemoteObjects(*script_evaluator->cur_world_state_lock).insert(ob);
	}

	// Game-spawned objects are session geometry. Keep their script-driven
	// transform/material edits transient so they are never persisted as world assets.
	if(!ob->game_spawn_owner_uid.valid())
		script_evaluator->world_state->addWorldObjectAsDBDirty(ob, *script_evaluator->cur_world_state_lock);
	sub_lua_vm->server->world_state->markAsChanged();

	return 0; // Count of returned values
#endif
}



// C++ implementation of __index for WorldMaterial class. Used when a WorldMaterial table field is read from.
static int worldMaterialClassIndexMetaMethod(lua_State* state)
{
	// arg 1 is table (WorldMaterial)
	// arg 2 is the key (method name)

	assert(lua_gettop(state) == 2); // Should be 2 args.
	checkNumArgs(state, 2);

	// Get object UID from the world material table
	//const UID uid((uint64)LuaUtils::getTableNumberField(state, /*table index=*/1, "uid"));
	const UID uid((uint64)LuaUtils::getTableLightUserDataField(state, /*table index=*/1, "uid"));

	// Get material index from the world material table
	//const size_t mat_index = (size_t)LuaUtils::getTableNumberField(state, /*table index=*/1, "idx");
	const size_t mat_index = (size_t)LuaUtils::getTableLightUserDataField(state, /*table index=*/1, "idx");

	LuaScript* script = (LuaScript*)lua_getthreaddata(state); // NOTE: this double pointer-chasing sucks
	LuaScriptEvaluator* script_evaluator = (LuaScriptEvaluator*)script->userdata;

	WorldObject* ob = getWorldObjectForUID(script_evaluator, uid);

	if(mat_index >= ob->materials.size())
		throw glare::Exception("Invalid material index" + errorContextString(state));
#if SERVER
	if(ob->game_spawn_owner_uid.valid() && !script_evaluator->world_state->allowGameObjectMaterialUpdate(
		ob->game_spawn_owner_uid, *script_evaluator->cur_world_state_lock))
		throw glare::Exception("The game reached its object-material update limit." + errorContextString(state));
#endif

	WorldMaterial* mat = ob->materials[mat_index].ptr();
	
	int atom = -1;
	const char* key_str = LuaUtils::getStringAndAtom(state, /*index=*/2, atom);
	switch(atom) // NOTE: The switch cases should be in the same order as the Atom enum values to ensure nice code-gen.
	{
	case Atom_colour:
		assert(stringEqual(key_str, "colour"));
		LuaUtils::pushVec3f(state, mat->colour_rgb.toVec3());
		break;
	case Atom_colour_texture_url:
		assert(stringEqual(key_str, "colour_texture_url"));
		LuaUtils::pushString(state, mat->colour_texture_url);
		break;
	case Atom_emission_rgb:
		assert(stringEqual(key_str, "emission_rgb"));
		LuaUtils::pushVec3f(state, mat->emission_rgb.toVec3());
		break;
	case Atom_emission_texture_url:
		assert(stringEqual(key_str, "emission_texture_url"));
		LuaUtils::pushString(state, mat->emission_texture_url);
		break;
	case Atom_normal_map_url:
		assert(stringEqual(key_str, "normal_map_url"));
		LuaUtils::pushString(state, mat->normal_map_url);
		break;
	case Atom_roughness_val:
		assert(stringEqual(key_str, "roughness_val"));
		lua_pushnumber(state, mat->roughness.val);
		break;
	case Atom_roughness_texture_url:
		assert(stringEqual(key_str, "roughness_texture_url"));
		LuaUtils::pushString(state, mat->roughness.texture_url);
		break;
	case Atom_metallic_fraction_val:
		assert(stringEqual(key_str, "metallic_fraction_val"));
		lua_pushnumber(state, mat->metallic_fraction.val);
		break;
	case Atom_opacity_val:
		assert(stringEqual(key_str, "opacity_val"));
		lua_pushnumber(state, mat->opacity.val);
		break;
	case Atom_tex_matrix:
		assert(stringEqual(key_str, "tex_matrix"));
		LuaUtils::pushMatrix2f(state, mat->tex_matrix);
		break;
	case Atom_emission_lum_flux_or_lum:
		assert(stringEqual(key_str, "emission_lum_flux_or_lum"));
		lua_pushnumber(state, mat->emission_lum_flux_or_lum);
		break;
	case Atom_hologram:
		assert(stringEqual(key_str, "hologram"));
		lua_pushboolean(state, BitUtils::isBitSet(mat->flags, WorldMaterial::HOLOGRAM_FLAG));
		break;
	case Atom_double_sided:
		assert(stringEqual(key_str, "double_sided"));
		lua_pushboolean(state, BitUtils::isBitSet(mat->flags, WorldMaterial::DOUBLE_SIDED_FLAG));
		break;
	default:
		throw glare::Exception("Unknown field '" + std::string(key_str) + "'" + errorContextString(state));
	}

	return 1; // Count of returned values
}


// C++ implementation of __newindex for WorldMaterial class.  Used when a value is assigned to a WorldMaterial field
static int worldMaterialClassNewIndexMetaMethod(lua_State* state)
{
#if GUI_CLIENT
	return 0; // Count of returned values
#else

	// Arg 1: WorldMaterial
	// Arg 2: key : string
	// Arg 3: value

	assert(lua_gettop(state) == 3); // Should be 3 args.
	checkNumArgs(state, 3);
	
	// Get object UID from the world material table
	//const UID uid((uint64)LuaUtils::getTableNumberField(state, /*table index=*/1, "uid"));
	const UID uid((uint64)LuaUtils::getTableLightUserDataField(state, /*table index=*/1, "uid"));

	// Get material index from the world material table
	const size_t mat_index = (size_t)LuaUtils::getTableLightUserDataField(state, /*table index=*/1, "idx");

	SubstrataLuaVM* sub_lua_vm = (SubstrataLuaVM*)lua_callbacks(state)->userdata;
	LuaScript* script = (LuaScript*)lua_getthreaddata(state); // NOTE: this double pointer-chasing sucks
	LuaScriptEvaluator* script_evaluator = (LuaScriptEvaluator*)script->userdata;

	WorldObject* ob = getWorldObjectForUID(script_evaluator, uid);

	// Check permissions before we update object
	if(ob->creator_id != script_evaluator->world_object->creator_id)
		throw glare::Exception("Script does not have permissions to modifiy object (ob UID: " + uid.toString() + ")" + errorContextString(state));
	if(ob->game_spawn_owner_uid.valid() && (!sub_lua_vm || !sub_lua_vm->server || sub_lua_vm->server->world_state->read_only_mode))
		throw glare::Exception("Game-spawned object materials cannot be modified without a writable authoritative server." + errorContextString(state));


	if(mat_index >= ob->materials.size())
		throw glare::Exception("Invalid material index" + errorContextString(state));

	WorldMaterial* mat = ob->materials[mat_index].ptr();

	// Read key
	int atom = -1;
	const char* key_str = LuaUtils::getStringAndAtom(state, /*index=*/2, atom);
	switch(atom) // NOTE: The switch cases should be in the same order as the Atom enum values to ensure nice code-gen.
	{
	case Atom_colour:
		assert(stringEqual(key_str, "colour"));
		mat->colour_rgb = Colour3f(LuaUtils::getVec3f(state, /*index=*/3));
		break;
	case Atom_colour_texture_url:
		assert(stringEqual(key_str, "colour_texture_url"));
		assignStringWithSizeCheck(state, /*index=*/3, mat->colour_texture_url, "colour_texture_url", WorldObject::MAX_URL_SIZE);
		break;
	case Atom_emission_rgb:
		assert(stringEqual(key_str, "emission_rgb"));
		mat->emission_rgb = Colour3f(LuaUtils::getVec3f(state, /*index=*/3));
		break;
	case Atom_emission_texture_url:
		assert(stringEqual(key_str, "emission_texture_url"));
		assignStringWithSizeCheck(state, /*index=*/3, mat->emission_texture_url, "emission_texture_url", WorldObject::MAX_URL_SIZE);
		break;
	case Atom_normal_map_url:
		assert(stringEqual(key_str, "normal_map_url"));
		assignStringWithSizeCheck(state, /*index=*/3, mat->normal_map_url, "normal_map_url", WorldObject::MAX_URL_SIZE);
		break;
	case Atom_roughness_val:
		assert(stringEqual(key_str, "roughness_val"));
		mat->roughness.val = LuaUtils::getFloat(state, /*index=*/3);
		break;
	case Atom_roughness_texture_url:
		assert(stringEqual(key_str, "roughness_texture_url"));
		assignStringWithSizeCheck(state, /*index=*/3, mat->roughness.texture_url, "roughness_texture_url", WorldObject::MAX_URL_SIZE);
		break;
	case Atom_metallic_fraction_val:
		assert(stringEqual(key_str, "metallic_fraction_val"));
		mat->metallic_fraction.val = LuaUtils::getFloat(state, /*index=*/3);
		break;
	case Atom_opacity_val:
		assert(stringEqual(key_str, "opacity_val"));
		mat->opacity.val = LuaUtils::getFloat(state, /*index=*/3);
		break;
	case Atom_tex_matrix:
		assert(stringEqual(key_str, "tex_matrix"));
		mat->tex_matrix = LuaUtils::getMatrix2f(state, /*index=*/3);
		break;
	case Atom_emission_lum_flux_or_lum:
		assert(stringEqual(key_str, "emission_lum_flux_or_lum"));
		mat->emission_lum_flux_or_lum = LuaUtils::getFloat(state, /*index=*/3);
		break;
	case Atom_hologram:
		assert(stringEqual(key_str, "hologram"));
		BitUtils::setOrZeroBit(mat->flags, WorldMaterial::HOLOGRAM_FLAG, LuaUtils::getBool(state, /*index=*/3));
		break;
	case Atom_double_sided:
		assert(stringEqual(key_str, "double_sided"));
		BitUtils::setOrZeroBit(mat->flags, WorldMaterial::DOUBLE_SIDED_FLAG, LuaUtils::getBool(state, /*index=*/3));
		break;
	default:
		throw glare::Exception("Unknown field '" + std::string(key_str) + "'" + errorContextString(state));
	}

	// Mark the object as dirty, sending the updated object will send the updated material as well.
	ob->from_remote_other_dirty = true; // TODO: rename
	script_evaluator->world_state->getDirtyFromRemoteObjects(*script_evaluator->cur_world_state_lock).insert(ob);

	if(!ob->game_spawn_owner_uid.valid())
		script_evaluator->world_state->addWorldObjectAsDBDirty(ob, *script_evaluator->cur_world_state_lock);
	sub_lua_vm->server->world_state->markAsChanged();

	return 0; // Count of returned values
#endif
}


// C++ implementation of __index for User class. Used when a User table field is read from.
static int userClassIndexMetaMethod(lua_State* state)
{
	throw glare::Exception("Disabled");

	// arg 1 is table
	// arg 2 is the key (field name)

	assert(lua_gettop(state) == 2); // Should be 2 args.
	checkNumArgs(state, 2);

	const UID uid((uint64)LuaUtils::getTableNumberField(state, /*table index=*/1, "uid"));

#if GUI_CLIENT
	SubstrataLuaVM* sub_lua_vm = (SubstrataLuaVM*)lua_callbacks(state)->userdata;

	//LuaScript* script = (LuaScript*)lua_getthreaddata(state);
	//LuaScriptEvaluator* script_evaluator = (LuaScriptEvaluator*)script->userdata;

	auto res = sub_lua_vm->gui_client->world_state->avatars.find(uid);
	if(res == sub_lua_vm->gui_client->world_state->avatars.end())
		throw glare::Exception("No such avatar with given UID" + errorContextString(state));

	Avatar* avatar = res->second.ptr();

#elif SERVER
	// Get user UID from the user table
	//const UserID uid((uint64)LuaUtils::getTableNumberField(state, /*table index=*/1, "id"));

	//SubstrataLuaVM* sub_lua_vm = (SubstrataLuaVM*)lua_callbacks(state)->userdata;

	//auto res = sub_lua_vm->server->world_state->user_id_to_users.find(uid);
	//if(res == sub_lua_vm->server->world_state->user_id_to_users.end())
	//	throw glare::Exception("No such user with given UID");

	//User* user = res->second.ptr();



//	SubstrataLuaVM* sub_lua_vm = (SubstrataLuaVM*)lua_callbacks(state)->userdata;

	LuaScript* script = (LuaScript*)lua_getthreaddata(state);
	LuaScriptEvaluator* script_evaluator = (LuaScriptEvaluator*)script->userdata;

	if(script_evaluator->cur_world_state_lock == nullptr)
		throw glare::Exception("Internal error: cur_world_state_lock was null");
	const ServerWorldState::AvatarMapType& avatars = script_evaluator->world_state->getAvatars(*script_evaluator->cur_world_state_lock);
	auto res = avatars.find(uid);
	if(res == avatars.end())
		throw glare::Exception("No such avatar with given UID" + errorContextString(state));

	const Avatar* avatar = res->second.ptr();
	//if(res == sub_lua_vm->server->world_state->user_id_to_users.end())
	//	throw glare::Exception("No such user with given UID");

	//User* user = res->second.ptr();
#endif


	const char* key_str = lua_tolstring(state, /*index=*/2, /*stringlen=*/NULL); // May return NULL if not a string
	if(key_str)
	{
		if(stringEqual(key_str, "pos"))
		{
			LuaUtils::pushVec3d(state, avatar->pos);
			return 1;
		}
//#if SERVER
//		else if(stringEqual(key_str, "name"))
//		{
//			LuaUtils::pushString(user->name);
//			return 1;
//		}
//#endif
		else
			throw glare::Exception("User class: Unknown field '" + std::string(key_str) + "'" + errorContextString(state));
	}

	return 1; // Count of returned values
}


// C++ implementation of __newindex.  Used when a value is assigned to a table field.
static int userClassNewIndexMetaMethod(lua_State* state)
{
	// Arg 1: table 
	// Arg 2: key
	// Arg 3: value

	assert(lua_gettop(state) == 3); // Should be 3 args.
	checkNumArgs(state, 3);
	
	// Read key
//	size_t stringlen = 0;
//	const char* key_str = lua_tolstring(state, /*index=*/2, &stringlen); // May return NULL if not a string
//	if(key_str)
//	{
//		
//	}
//
//	// By default, just do the assignment to the original table here.
//	//lua_rawsetfield(state, /*table index=*/1, key_str);
//	lua_rawset(state, /*table index=*/1); // Sets table[key] = value, pops key and value from stack.

	return 0; // Count of returned values
}


// C++ implementation of __index for Avatar class. Used when a Avatar table field is read from.
static int avatarClassIndexMetaMethod(lua_State* state)
{
	// arg 1 is Avatar table
	// arg 2 is the key (field name)

	assert(lua_gettop(state) == 2); // Should be 2 args.
	checkNumArgs(state, 2);

	const UID avatar_uid((uint64)LuaUtils::getTableNumberField(state, /*table index=*/1, "uid"));

	LuaScript* script = (LuaScript*)lua_getthreaddata(state);
	LuaScriptEvaluator* script_evaluator = (LuaScriptEvaluator*)script->userdata;

#if GUI_CLIENT
	SubstrataLuaVM* sub_lua_vm = (SubstrataLuaVM*)lua_callbacks(state)->userdata;
	WorldState* world_state = sub_lua_vm->gui_client->world_state.ptr();
	checkHoldWorldStateMutex(script_evaluator, world_state);

	auto res = world_state->avatars.find(avatar_uid);
	if(res == world_state->avatars.end())
		throw glare::Exception("No such avatar with given UID" + errorContextString(state));

	Avatar* avatar = res->second.ptr();
#elif SERVER

	if(script_evaluator->cur_world_state_lock == nullptr)
		throw glare::Exception("Internal error: cur_world_state_lock was null");
	const ServerWorldState::AvatarMapType& avatars = script_evaluator->world_state->getAvatars(*script_evaluator->cur_world_state_lock);

	auto res = avatars.find(avatar_uid);
	if(res == avatars.end())
		throw glare::Exception("No such avatar with given UID" + errorContextString(state));

	Avatar* avatar = res->second.ptr();
#endif

	// Read key
	int atom = -1;
	const char* key_str = LuaUtils::getStringAndAtom(state, /*index=*/2, atom);
	switch(atom)
	{
	case Atom_pos:
		assert(stringEqual(key_str, "pos"));
		LuaUtils::pushVec3d(state, avatar->pos);
		return 1;
	case Atom_name:
		assert(stringEqual(key_str, "name"));
		LuaUtils::pushString(state, avatar->name);
		return 1;
	case Atom_linear_velocity:
		assert(stringEqual(key_str, "linear_velocity"));
#if GUI_CLIENT
		LuaUtils::pushVec3f(state, Vec3f(sub_lua_vm->gui_client->player_physics.getLinearVel()));
		return 1;
#else // else server:
		LuaUtils::pushVec3f(state, Vec3f(0.f));
		return 1;
#endif
	case Atom_vehicle_inside: // Returns an Object table of the vehicle object the avatar is inside-of/riding, or nil if none.
	{
		assert(stringEqual(key_str, "vehicle_inside"));
#if GUI_CLIENT
		const WorldObject* vehicle_inside;
		if(avatar_uid == sub_lua_vm->gui_client->client_avatar_uid) // If this is our avatar:
			vehicle_inside = sub_lua_vm->gui_client->vehicle_controller_inside ? sub_lua_vm->gui_client->vehicle_controller_inside->getControlledObject() : nullptr;
		else
			vehicle_inside = avatar->entered_vehicle.ptr();

		if(vehicle_inside)
			script_evaluator->pushWorldObjectTableOntoStack(vehicle_inside->uid);
		else
			lua_pushnil(state);
#else // else server:
		if(avatar->vehicle_inside_uid.valid())
			script_evaluator->pushWorldObjectTableOntoStack(avatar->vehicle_inside_uid);
		else
			lua_pushnil(state);
#endif
		return 1;
	}
	default:
		throw glare::Exception("Avatar class: Unknown field '" + std::string(key_str) + "'" + errorContextString(state));
	}
}


// C++ implementation of __newindex.  Used when a value is assigned to a table field.
static int avatarClassNewIndexMetaMethod(lua_State* state)
{
	// Arg 1: table 
	// Arg 2: key
	// Arg 3: value

#if GUI_CLIENT
	LuaScript* script = (LuaScript*)lua_getthreaddata(state);
	LuaScriptEvaluator* script_evaluator = (LuaScriptEvaluator*)script->userdata;
	SubstrataLuaVM* sub_lua_vm = (SubstrataLuaVM*)lua_callbacks(state)->userdata;
	WorldState* world_state = sub_lua_vm->gui_client->world_state.ptr();
	checkHoldWorldStateMutex(script_evaluator, world_state);
#endif

	[[maybe_unused]] const UID avatar_uid = UID((uint64)LuaUtils::getTableNumberField(state, /*table index=*/1, "uid"));

	// Read key
	int atom = -1;
	const char* key_str = LuaUtils::getStringAndAtom(state, /*index=*/2, atom);
	switch(atom) // NOTE: The switch cases should be in the same order as the Atom enum values to ensure nice code-gen.
	{
		case Atom_pos:
		{
			assert(stringEqual(key_str, "pos"));
#if GUI_CLIENT
			if(avatar_uid == sub_lua_vm->gui_client->client_avatar_uid)
			{
				const Vec3d pos = LuaUtils::getVec3d(state, /*index=*/3);
				sub_lua_vm->gui_client->player_physics.setEyePosition(pos);
			}
#endif
			break;
		}
		case Atom_name:
		{
			assert(stringEqual(key_str, "name"));
			throw glare::Exception("Can't set avatar name");
			break;
		}
		case Atom_linear_velocity:
		{
			assert(stringEqual(key_str, "linear_velocity"));
#if GUI_CLIENT
			if(avatar_uid == sub_lua_vm->gui_client->client_avatar_uid)
			{
				const Vec3f new_linear_vel = LuaUtils::getVec3f(state, /*index=*/3);
				sub_lua_vm->gui_client->player_physics.setLinearVel(new_linear_vel.toVec4fVector());
			}
#endif
			break;
		}
	default:
		throw glare::Exception("Avatar class: Unknown field '" + std::string(key_str) + "'" + errorContextString(state));
	}

	return 0; // Count of returned values
}


// "gets called when a string is created; returned atom can be retrieved via tostringatom"
static int16_t glareLuaUserAtom(const char* str, size_t stringlen)
{
	// conPrint("glareLuaUserAtom(): str: " + std::string(str, stringlen));

	for(size_t i=0; i<staticArrayNumElems(string_atoms); ++i)
		if(stringEqual(str, string_atoms[i].str))
			return (int16)string_atoms[i].atom;

	return -1; // or use ATOM_UNDEF here?
}


// NOTE: These values can't change without breaking deserialisation of these serialised objects.
static const uint32 Vec3d_metatable_UID			= 1; // See LuaVM::LuaVM()
static const uint32 WorldObject_metatable_UID	= 100;
static const uint32 WorldMaterial_metatable_UID	= 101;
static const uint32 User_metatable_UID			= 102;
static const uint32 Avatar_metatable_UID		= 103;


SubstrataLuaVM::SubstrataLuaVM(const SubstrataLuaVMArgs& args)
:	metatable_uid_to_ref_map(std::numeric_limits<uint32>::max()),
#if GUI_CLIENT
	gui_client(args.gui_client),
	player_physics(args.player_physics)
#endif
#if SERVER
	server(args.server)
#endif
{
	lua_vm.set(new LuaVM());
	// A Lua VM owns all loaded object scripts for this client or script owner.
	// Keep a hard aggregate bound while leaving room for multiplayer game state.
	lua_vm->max_total_mem_allowed = 128 * 1024 * 1024;

	lua_callbacks(lua_vm->state)->userdata = this;
	lua_callbacks(lua_vm->state)->useratom = glareLuaUserAtom;


	metatable_uid_to_ref_map.insert(std::make_pair(Vec3d_metatable_UID, lua_vm->Vec3dMetaTable_ref));


	// Set some global functions
	lua_pushcfunction(lua_vm->state, luaVec3fConstructor, /*debugname=*/"Vec3f");
	lua_setglobal(lua_vm->state, "Vec3f");
//TEMP DISABLED	lua_pushcfunction(lua_vm->state, createObject, /*debugname=*/"createObject");
//	lua_setglobal(lua_vm->state, "createObject"); // Pops a value from the stack and sets it as the new value of global name.
	
	lua_pushcfunction(lua_vm->state, luaGetWorldObjectForUID, /*debugname=*/"getObjectForUID");
	lua_setglobal(lua_vm->state, "getObjectForUID");
	lua_pushcfunction(lua_vm->state, luaFindWorldObjectForUID, /*debugname=*/"findObjectForUID");
	lua_setglobal(lua_vm->state, "findObjectForUID");
	
	lua_pushcfunction(lua_vm->state, getCurrentTime, /*debugname=*/"getCurrentTime");
	lua_setglobal(lua_vm->state, "getCurrentTime");
	
	lua_pushcfunction(lua_vm->state, showMessageToUser, /*debugname=*/"showMessageToUser");
	lua_setglobal(lua_vm->state, "showMessageToUser");
	lua_pushcfunction(lua_vm->state, gameSetHUDText, /*debugname=*/"gameSetHUDText");
	lua_setglobal(lua_vm->state, "gameSetHUDText");
	lua_pushcfunction(lua_vm->state, gameClearHUD, /*debugname=*/"gameClearHUD");
	lua_setglobal(lua_vm->state, "gameClearHUD");

	// The namespaced surface is the supported game API. Keep the early global
	// HUD bindings above for scripts written during the initial prototype stage.
	lua_createtable(lua_vm->state, /*num array elems=*/0, /*num non-array elems=*/25);
	lua_pushcfunction(lua_vm->state, gameGetPlayers, /*debugname=*/"game.getPlayers");
	lua_setfield(lua_vm->state, -2, "getPlayers");
#if SERVER
	lua_pushcfunction(lua_vm->state, gameRandomInt, /*debugname=*/"game.randomInt");
	lua_setfield(lua_vm->state, -2, "randomInt");
#endif
	lua_pushcfunction(lua_vm->state, gameGetPlayersNear, /*debugname=*/"game.getPlayersNear");
	lua_setfield(lua_vm->state, -2, "getPlayersNear");
	lua_pushcfunction(lua_vm->state, gameGetPlayersOverlapping, /*debugname=*/"game.getPlayersOverlapping");
	lua_setfield(lua_vm->state, -2, "getPlayersOverlapping");
#if SERVER
	lua_pushcfunction(lua_vm->state, gameGetAimTarget, /*debugname=*/"game.getAimTarget");
	lua_setfield(lua_vm->state, -2, "getAimTarget");
	lua_pushcfunction(lua_vm->state, gameGetAimPlayerTarget, /*debugname=*/"game.getAimPlayerTarget");
	lua_setfield(lua_vm->state, -2, "getAimPlayerTarget");
#endif
	lua_pushcfunction(lua_vm->state, gameSweepAABB, /*debugname=*/"game.sweepAABB");
	lua_setfield(lua_vm->state, -2, "sweepAABB");
	lua_pushcfunction(lua_vm->state, gameMoveObjectWithSlide, /*debugname=*/"game.moveObjectWithSlide");
	lua_setfield(lua_vm->state, -2, "moveObjectWithSlide");
	lua_pushcfunction(lua_vm->state, gameSetHUDText, /*debugname=*/"game.setHUDText");
	lua_setfield(lua_vm->state, -2, "setHUDText");
	lua_pushcfunction(lua_vm->state, gameSetInteractionPrompt, /*debugname=*/"setInteractionPrompt");
	lua_setfield(lua_vm->state, -2, "setInteractionPrompt");
	lua_pushcfunction(lua_vm->state, gameClearHUD, /*debugname=*/"game.clearHUD");
	lua_setfield(lua_vm->state, -2, "clearHUD");
	lua_pushcfunction(lua_vm->state, gameSetHUDMenu, /*debugname=*/"game.setHUDMenu");
	lua_setfield(lua_vm->state, -2, "setHUDMenu");
	lua_pushcfunction(lua_vm->state, gameClearHUDMenu, /*debugname=*/"game.clearHUDMenu");
	lua_setfield(lua_vm->state, -2, "clearHUDMenu");
	lua_pushcfunction(lua_vm->state, createGameLoop, /*debugname=*/"game.createLoop");
	lua_setfield(lua_vm->state, -2, "createLoop");
	lua_pushcfunction(lua_vm->state, gameSpawnObject, /*debugname=*/"game.spawnObject");
	lua_setfield(lua_vm->state, -2, "spawnObject");
	lua_pushcfunction(lua_vm->state, gameCreateVoxelObject, /*debugname=*/"game.createVoxelObject");
	lua_setfield(lua_vm->state, -2, "createVoxelObject");
	lua_pushcfunction(lua_vm->state, gameSpawnGroup, /*debugname=*/"game.spawnGroup");
	lua_setfield(lua_vm->state, -2, "spawnGroup");
	lua_pushcfunction(lua_vm->state, gameDestroyObject, /*debugname=*/"game.destroyObject");
	lua_setfield(lua_vm->state, -2, "destroyObject");
	lua_pushcfunction(lua_vm->state, gameClearSpawnedObjects, /*debugname=*/"game.clearSpawnedObjects");
	lua_setfield(lua_vm->state, -2, "clearSpawnedObjects");
	lua_pushcfunction(lua_vm->state, gameSetObjectTransform, /*debugname=*/"game.setObjectTransform");
	lua_setfield(lua_vm->state, -2, "setObjectTransform");
	lua_pushcfunction(lua_vm->state, gameGetOverlaps, /*debugname=*/"game.getOverlaps");
	lua_setfield(lua_vm->state, -2, "getOverlaps");
	lua_pushcfunction(lua_vm->state, gameGetOverlapChanges, /*debugname=*/"game.getOverlapChanges");
	lua_setfield(lua_vm->state, -2, "getOverlapChanges");
	lua_pushcfunction(lua_vm->state, gameGetData, /*debugname=*/"game.getData");
	lua_setfield(lua_vm->state, -2, "getData");
	lua_pushcfunction(lua_vm->state, gameSetData, /*debugname=*/"game.setData");
	lua_setfield(lua_vm->state, -2, "setData");
	lua_pushcfunction(lua_vm->state, gameGetPlayerData, /*debugname=*/"game.getPlayerData");
	lua_setfield(lua_vm->state, -2, "getPlayerData");
	lua_pushcfunction(lua_vm->state, gameSetPlayerData, /*debugname=*/"game.setPlayerData");
	lua_setfield(lua_vm->state, -2, "setPlayerData");
	lua_pushcfunction(lua_vm->state, createClientGameLoop, /*debugname=*/"game.createClientLoop");
	lua_setfield(lua_vm->state, -2, "createClientLoop");
	lua_pushcfunction(lua_vm->state, gameSetCamera, /*debugname=*/"game.setCamera");
	lua_setfield(lua_vm->state, -2, "setCamera");
	lua_pushcfunction(lua_vm->state, gameFollowCamera, /*debugname=*/"game.followCamera");
	lua_setfield(lua_vm->state, -2, "followCamera");
	lua_pushcfunction(lua_vm->state, gameReleaseCamera, /*debugname=*/"game.releaseCamera");
	lua_setfield(lua_vm->state, -2, "releaseCamera");
	lua_pushcfunction(lua_vm->state, gameSetPlayerControl, /*debugname=*/"game.setPlayerControl");
	lua_setfield(lua_vm->state, -2, "setPlayerControl");
	lua_pushcfunction(lua_vm->state, gameGetLocalInput, /*debugname=*/"game.getLocalInput");
	lua_setfield(lua_vm->state, -2, "getLocalInput");
	lua_pushcfunction(lua_vm->state, gameRaycast, /*debugname=*/"game.raycast");
	lua_setfield(lua_vm->state, -2, "raycast");
	lua_pushcfunction(lua_vm->state, gameIsKeyDown, /*debugname=*/"game.isKeyDown");
	lua_setfield(lua_vm->state, -2, "isKeyDown");
	lua_pushcfunction(lua_vm->state, gameBindAction, /*debugname=*/"game.bindAction");
	lua_setfield(lua_vm->state, -2, "bindAction");
	lua_pushcfunction(lua_vm->state, gameIsActionDown, /*debugname=*/"game.isActionDown");
	lua_setfield(lua_vm->state, -2, "isActionDown");
	lua_pushcfunction(lua_vm->state, gameIsLocalActionDown, /*debugname=*/"game.isLocalActionDown");
	lua_setfield(lua_vm->state, -2, "isLocalActionDown");
	lua_pushcfunction(lua_vm->state, gameGetInput, /*debugname=*/"game.getInput");
	lua_setfield(lua_vm->state, -2, "getInput");
#if GUI_CLIENT
	lua_pushcfunction(lua_vm->state, gameSendToServer, /*debugname=*/"game.sendToServer");
	lua_setfield(lua_vm->state, -2, "sendToServer");
#endif
	lua_pushcfunction(lua_vm->state, gameGetState, /*debugname=*/"game.getState");
	lua_setfield(lua_vm->state, -2, "getState");
	lua_pushcfunction(lua_vm->state, gameSetState, /*debugname=*/"game.setState");
	lua_setfield(lua_vm->state, -2, "setState");
#if SERVER
	lua_pushcfunction(lua_vm->state, gamePlayAvatarSpawnEffect, /*debugname=*/"game.playAvatarSpawnEffect");
	lua_setfield(lua_vm->state, -2, "playAvatarSpawnEffect");
	lua_pushcfunction(lua_vm->state, gameEmitEvent, /*debugname=*/"game.emitEvent");
	lua_setfield(lua_vm->state, -2, "emitEvent");
#endif
	lua_setglobal(lua_vm->state, "game");

	lua_pushcfunction(lua_vm->state, createTimer, /*debugname=*/"createTimer");
	lua_setglobal(lua_vm->state, "createTimer");
	lua_pushcfunction(lua_vm->state, createGameLoop, /*debugname=*/"createGameLoop");
	lua_setglobal(lua_vm->state, "createGameLoop");
	
	lua_pushcfunction(lua_vm->state, destroyTimer, /*debugname=*/"destroyTimer");
	lua_setglobal(lua_vm->state, "destroyTimer");
	
	lua_pushcfunction(lua_vm->state, luaAddEventListener, /*debugname=*/"addEventListener");
	lua_setglobal(lua_vm->state, "addEventListener");
	lua_pushcfunction(lua_vm->state, luaRemoveEventListener, /*debugname=*/"removeEventListener");
	lua_setglobal(lua_vm->state, "removeEventListener");
	
	lua_pushcfunction(lua_vm->state, doHTTPGetRequestAsync, /*debugname=*/"doHTTPGetRequestAsync");
	lua_setglobal(lua_vm->state, "doHTTPGetRequestAsync");
	
	lua_pushcfunction(lua_vm->state, doHTTPPostRequestAsync, /*debugname=*/"doHTTPPostRequestAsync");
	lua_setglobal(lua_vm->state, "doHTTPPostRequestAsync");


	lua_pushcfunction(lua_vm->state, getSecret, /*debugname=*/"getSecret");
	lua_setglobal(lua_vm->state, "getSecret");

	lua_pushcfunction(lua_vm->state, parseJSON, /*debugname=*/"parseJSON");
	lua_setglobal(lua_vm->state, "parseJSON");

	lua_createtable(lua_vm->state, /*narr=*/0, /*nrec=*/2);
	lua_vm->setCFunctionAsTableField(objectStorageGetItem, /*debugname=*/"objectStorageGetItem", /*key=*/"getItem");
	lua_vm->setCFunctionAsTableField(objectStorageSetItem, /*debugname=*/"objectStorageSetItem", /*key=*/"setItem");
	lua_setglobal(lua_vm->state, "objectstorage"); // Set table as global name table 'objectstorage'


	//--------------------------- Create metatables for our classes ---------------------------

	//--------------------------- Create WorldObject Metatable ---------------------------
	lua_createtable(lua_vm->state, /*num array elems=*/0, /*num non-array elems=*/2); // Create WorldObject metatable
	
	LuaUtils::setNumberAsTableField(lua_vm->state, "uid", WorldObject_metatable_UID); // Set metatable UID (for serialisation)

	lua_vm->setCFunctionAsTableField(worldObjectClassIndexMetaMethod,    /*debugname=*/"worldObjectClassIndexMetaMethod",    /*key=*/"__index");
	lua_vm->setCFunctionAsTableField(worldObjectClassNewIndexMetaMethod, /*debugname=*/"worldObjectClassNewIndexMetaMethod", /*key=*/"__newindex");

	lua_setreadonly(lua_vm->state, /*index=*/-1, /*enabled=*/1); // Set metatable as read-only.

	worldObjectClassMetaTable_ref = lua_ref(lua_vm->state, /*index=*/-1); // Get reference to WorldObjectMetaTable.  Does not pop.
	lua_pop(lua_vm->state, 1); // Pop WorldObjectMetaTable from stack
	//--------------------------- End create User Metatable ---------------------------


	//--------------------------- Create WorldMaterial Metatable ---------------------------
	lua_createtable(lua_vm->state, /*num array elems=*/0, /*num non-array elems=*/2); // Create WorldMaterial metatable

	LuaUtils::setNumberAsTableField(lua_vm->state, "uid", WorldMaterial_metatable_UID); // Set metatable UID (for serialisation)

	lua_vm->setCFunctionAsTableField(worldMaterialClassIndexMetaMethod,    /*debugname=*/"worldMaterialClassIndexMetaMethod",    /*key=*/"__index");
	lua_vm->setCFunctionAsTableField(worldMaterialClassNewIndexMetaMethod, /*debugname=*/"worldMaterialClassNewIndexMetaMethod", /*key=*/"__newindex");

	lua_setreadonly(lua_vm->state, /*index=*/-1, /*enabled=*/1); // Set metatable as read-only.

	worldMaterialClassMetaTable_ref = lua_ref(lua_vm->state, /*index=*/-1); // Get reference to WorldMaterialMetaTable.  Does not pop.
	lua_pop(lua_vm->state, 1); // Pop WorldMaterialMetaTable from stack
	//--------------------------- End create User Metatable ---------------------------
	

	//--------------------------- Create User Metatable ---------------------------
	lua_createtable(lua_vm->state, /*num array elems=*/0, /*num non-array elems=*/2); // Create User metatable

	LuaUtils::setNumberAsTableField(lua_vm->state, "uid", User_metatable_UID); // Set metatable UID (for serialisation)

	lua_vm->setCFunctionAsTableField(userClassIndexMetaMethod,    /*debugname=*/"userIndexMetaMethod",    /*key=*/"__index");
	lua_vm->setCFunctionAsTableField(userClassNewIndexMetaMethod, /*debugname=*/"userNewIndexMetaMethod", /*key=*/"__newindex");

	lua_setreadonly(lua_vm->state, /*index=*/-1, /*enabled=*/1); // Set metatable as read-only.

	userClassMetaTable_ref = lua_ref(lua_vm->state, /*index=*/-1); // Get reference to UserClassMetaTable.  Does not pop.
	lua_pop(lua_vm->state, 1); // Pop UserClassMetaTable from stack
	//--------------------------- End create User Metatable ---------------------------


	//--------------------------- Create Avatar Metatable ---------------------------
	lua_createtable(lua_vm->state, /*num array elems=*/0, /*num non-array elems=*/2); // Create Avatar metatable

	LuaUtils::setNumberAsTableField(lua_vm->state, "uid", Avatar_metatable_UID); // Set metatable UID (for serialisation)

	lua_vm->setCFunctionAsTableField(avatarClassIndexMetaMethod,    /*debugname=*/"avatarIndexMetaMethod",    /*key=*/"__index");
	lua_vm->setCFunctionAsTableField(avatarClassNewIndexMetaMethod, /*debugname=*/"avatarNewIndexMetaMethod", /*key=*/"__newindex");

	lua_setreadonly(lua_vm->state, /*index=*/-1, /*enabled=*/1); // Set metatable as read-only.

	avatarClassMetaTable_ref = lua_ref(lua_vm->state, /*index=*/-1); // Get reference to AvatarClassMetaTable.  Does not pop.
	lua_pop(lua_vm->state, 1); // Pop AvatarClassMetaTable from stack
	//--------------------------- End create Avatar Metatable ---------------------------

	lua_vm->finishInitAndSandbox();

	// Add items to metatable_uid_to_ref_map for deserialisation
	metatable_uid_to_ref_map.insert(std::make_pair(WorldObject_metatable_UID,   worldObjectClassMetaTable_ref));
	metatable_uid_to_ref_map.insert(std::make_pair(WorldMaterial_metatable_UID, worldMaterialClassMetaTable_ref)); 
	metatable_uid_to_ref_map.insert(std::make_pair(User_metatable_UID,          userClassMetaTable_ref)); 
	metatable_uid_to_ref_map.insert(std::make_pair(Avatar_metatable_UID,        avatarClassMetaTable_ref)); 
}


SubstrataLuaVM::~SubstrataLuaVM()
{
}
