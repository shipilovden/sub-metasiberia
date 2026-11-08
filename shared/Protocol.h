/*=====================================================================
Protocol.h
----------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#pragma once


#include "utils/Platform.h"

/*
CyberspaceProtocolVersion
20: Added lightmap_url to WorldObject.
21: Added parcel auction stuff
22: Added grid object querying stuff
23: Added ObjectModelURLChanged
24: Added material emission_lum_flux
25: Added AvatarIsHere message, Added anim_state to avatar
26: Added aabb_ws and max_lod_level to WorldObject
27: Added flags to WorldMaterial
28: Added pre_ob_to_world_matrix, materials to Avatar
29: Added AvatarPerformGesture, AvatarStopGesture
30: Added audio_source_url to WorldObject
31: Added length prefixing to messages.
32: Added flags field to Parcel
33: Added spawn_point field to Parcel
34: Added AABB to ObjectTransformUpdate
35: Added emission_rgb and emission_texture_url to WorldMaterial
	Added sending user flags in LoggedInMessage
36: Sending position at start of QueryObjects and QueryObjectsInAABB messages.
37: Not sending aabb_ws with various messages, sending aabb_os in WorldObject.
	Added last_modified_time to WorldObject.
	Added scale to ObjectTransformUpdate message.
38: Use length-prefixed serialisation for WorldMaterial, sending server version to client.
39: Added QueryMapTiles, MapTilesResult
40: Added QueryLODChunksMessage, LODChunkInitialSend, LODChunkUpdatedMessage
41: Added server capabilities uint sent back in initial handshake
42: Added ParcelInitialSendCompressed, client_capabilities
43: Added sending mesh optimisation version to client
44: Added ChangeToDifferentWorld
45: Added Parcel title
46: Added UserMovedNearToAvatar, UserMovedAwayFromAvatar messages.
47: Added UserGestureSettingsChanged message.
48: Added ping+pong messages.
49: Added AvatarSatOnSeat, AvatarGotUpFromSeat messages.
50: Added ObjectType_Camera and ObjectType_CameraScreen.
51: Added optional trailing text_font in WorldObject network serialisation.
52: Added QueryUserGear, UserGearList, GearItemUpdate and CreateGearItem messages.
53: Added CreateChatBot, ChatBotCreated, UpdateChatBot, DeleteChatBot, QueryUserBots, UserBotList, MoveChatBot, TestChatBotGesture messages.
54: Added UserUsedBotMessage; extended bot protocol with use_action_type, use_action_param, api_key, api_endpoint (Block 4).
55: Bot Block 5: movement_type, walk_speed, wander_radius, waypoints, use_actions list.
    Bot Block 6: farewell/walk/talk/interaction gesture slots.
    Bot Block 7: audio_min_distance, audio_start_delay_s, greeting/farewell/interaction audio URLs.
62: Added PrivateChatMessageID.
*/
namespace Protocol
{

const uint32 CyberspaceHello = 1357924680;

const uint32 CyberspaceProtocolVersion = 63;

const uint32 ClientProtocolOK		= 10000;
const uint32 ClientProtocolTooOld	= 10001;
const uint32 ClientProtocolTooNew	= 10002;
const uint32 CyberspaceGoodbye		= 10010;
const uint32 ClientUDPSocketOpen	= 10003;

const uint32 AudioStreamToServerStarted			= 10020;
const uint32 AudioStreamToServerEnded			= 10021;

const uint32 ConnectionTypeUpdates				= 500;
const uint32 ConnectionTypeUploadResource		= 501;
const uint32 ConnectionTypeDownloadResources	= 502;
//const uint32 ConnectionTypeWebsite				= 503; // A connection from the webserver.
const uint32 ConnectionTypeScreenshotBot		= 504; // A connection from the screenshot bot.
const uint32 ConnectionTypeEthBot				= 505; // A connection from the Ethereum bot.
const uint32 ConnectionTypeUploadPhoto			= 506;

const uint32 ChangeToDifferentWorld			= 600;


const uint32 AvatarCreated			= 1000;
const uint32 AvatarDestroyed		= 1001;
const uint32 AvatarTransformUpdate	= 1002;
const uint32 AvatarFullUpdate		= 1003;
const uint32 CreateAvatar			= 1004;
const uint32 AvatarIsHere			= 1005;
const uint32 AvatarPerformGesture	= 1010;
const uint32 AvatarStopGesture		= 1011;

const uint32 AvatarEnteredVehicle	= 1100;
const uint32 AvatarExitedVehicle	= 1101;
const uint32 AvatarSatOnSeat		= 1110; // Avatar sat down on a seat object
const uint32 AvatarGotUpFromSeat	= 1111; // Avatar got up from a seat object


const uint32 UserMovedNearToAvatar		= 1200;
const uint32 UserMovedAwayFromAvatar	= 1201;


const uint32 UserGestureSettingsChanged = 1300;

const uint32 QueryUserGear			= 1400; // Client requests the full list of gear items owned by the logged-in user.
const uint32 UserGearList			= 1401; // Server sends back GearItems for all gear owned by the user.
const uint32 GearItemUpdate			= 1402; // A client has modified a GearItem (e.g. in the gear editor).
const uint32 CreateGearItem			= 1403; // A client wants to create a new GearItem.
const uint32 DeleteGearItem			= 1404; // A client wants to delete an owned GearItem.

const uint32 CreateChatBot			= 1500; // Client wants to create a new chatbot (pos + heading + name).
const uint32 ChatBotCreated			= 1501; // Server confirms creation and sends back the bot id.
const uint32 UpdateChatBot			= 1502; // Client wants to update an existing chatbot (full settings).
const uint32 DeleteChatBot			= 1503; // Client wants to delete a chatbot.
const uint32 QueryUserBots			= 1504; // Client requests list of chatbots it owns.
const uint32 UserBotList			= 1505; // Server sends back bot_id + avatar_uid pairs.
const uint32 MoveChatBot			= 1506; // Client moved bot (pos + heading).
const uint32 TestChatBotGesture		= 1507; // Client asks the server to play one configured bot gesture now.
const uint32 UserUsedBotMessage		= 1508; // Client pressed E on a chatbot avatar.
const uint32 ChatBotDialogChoices		= 1509; // Server sends per-player labeled dialogue choices for a bot.
const uint32 GetBotConversationLog         = 1510; // Client requests recent conversation log for a bot.
const uint32 BotConversationLog            = 1511; // Server sends back log entries.
const uint32 BotSendManualMessage          = 1512; // Admin forces bot to broadcast a message now.
const uint32 GetBotPlayerMemoryList        = 1513; // Admin requests list of all player memory entries.
const uint32 BotPlayerMemoryList           = 1514; // Server sends player memory list.
const uint32 SetBotPlayerMemoryEntry       = 1515; // Admin edits reputation/quest/clears memory for one player.
const uint32 ChatBotDialogChoice		= 1516; // Client selects a server-issued dialogue choice index for a bot.


const uint32 ChatMessageID			= 2000;
const uint32 PrivateChatMessageID	= 2001;

const uint32 ObjectCreated			= 3000;
const uint32 ObjectDestroyed		= 3001;
const uint32 ObjectTransformUpdate	= 3002;
const uint32 ObjectFullUpdate		= 3003;
const uint32 ObjectLightmapURLChanged		= 3010; // The object's lightmap URL changed.
const uint32 ObjectFlagsChanged		= 3011;
const uint32 ObjectModelURLChanged	= 3012;
const uint32 ObjectPhysicsOwnershipTaken	= 3013;
const uint32 ObjectPhysicsTransformUpdate	= 3016;
const uint32 ObjectContentChanged	= 3017;
const uint32 SummonObject			= 3030;

const uint32 CreateObject			= 3004; // Client wants to create an object.
const uint32 DestroyObject			= 3005; // Client wants to destroy an object.
// Available only when the server advertises OBJECT_COPY_SUPPORT. These are new
// framed messages; existing CreateObject payload and protocol versions remain unchanged.
const uint32 CopyObject                  = 3006; // uint32 request_id, string source_world (<=1000), UID source_uid, Vec3d destination_pos.
const uint32 ObjectCopyResult            = 3007; // uint32 request_id, uint32 status (0=success), UID created_uid (invalid on error), string message.

const uint32 QueryObjects			= 3020; // Client wants to query objects in certain grid cells
const uint32 ObjectInitialSend		= 3021;
const uint32 QueryObjectsInAABB		= 3022; // Client wants to query objects in a particular AABB
const uint32 ObjectInitialSendCompressed		= 3023; // A message composed of compressed ObjectInitialSend messages.


const uint32 ParcelCreated			= 3100;
const uint32 ParcelDestroyed		= 3101;
const uint32 ParcelFullUpdate		= 3103;
const uint32 ParcelInitialSendCompressed		= 3104;

const uint32 QueryParcels			= 3150;
const uint32 ParcelList				= 3160;

const uint32 GetAllObjects			= 3600; // Client wants to get all objects from server
const uint32 AllObjectsSent			= 3601; // Server has sent all objects

const uint32 WorldSettingsInitialSendMessage	= 3700;
const uint32 WorldSettingsUpdate	= 3701;

const uint32 WorldDetailsInitialSendMessage	= 3750; // Server is sending ServerWorldState to client.

const uint32 QueryMapTiles			= 3800; // Client wants to query map tile image URLs
const uint32 MapTilesResult			= 3801; // Server is sending back a list of tile image URLs to the client.


const uint32 QueryLODChunksMessage		= 3900;
const uint32 LODChunkInitialSend		= 3901;
const uint32 LODChunkUpdatedMessage		= 3902;



//TEMP HACK move elsewhere
const uint32 GetFile				= 4000;
const uint32 GetFiles				= 4001; // Client wants to download multiple resources from the server.

const uint32 NewResourceOnServer	= 4100; // A file has been uploaded to the server


const uint32 UploadAllowed			= 5100;
const uint32 LogInFailure			= 5101;
const uint32 InvalidFileSize		= 5102;
const uint32 NoWritePermissions		= 5103;
const uint32 ServerIsInReadOnlyMode	= 5104;
const uint32 InvalidFileType		= 5105;


//TEMP HACK move elsewhere
const uint32 UserSelectedObject		= 6000;
const uint32 UserDeselectedObject	= 6001;

const uint32 UserUsedObjectMessage			= 6500;
const uint32 UserTouchedObjectMessage		= 6501;
const uint32 UserPressedObjectKeyMessage	= 6502; // UID object, uint32 ScriptKeyCode; sent only while the client has focused that object.
const uint32 UserReleasedObjectKeyboardMessage = 6503; // No payload; release the per-connection keyboard focus.
const uint32 UserReleasedObjectKeyMessage	= 6504; // UID object, uint32 ScriptKeyCode; release one key in the active game session.
const uint32 UserGameActionMessage			= 6505; // UID object, string action id (<=32); accepted only for an active game HUD action.
const uint32 UserUsedObjectWithKeyMessage	= 6509; // UID object, uint32 ScriptKeyCode; validates the object's configured interaction key.
const uint32 UserGameFormSubmitMessage		= 6508; // UID object, action id, <=4 (field id, value) pairs; validated against current server HUD menu.
const uint32 UserGameInputStateMessage		= 6506; // UID object, flags, gamepad axes, normalised pointer, optional trigger/wheel data and camera ray gated by capabilities.
const uint32 UserGameRPCMessage			= 6507; // UID object, name (<=32), serialized plain Luau value (<=4096 bytes).
const uint32 UserMovedNearToObjectMessage	= 6510;
const uint32 UserMovedAwayFromObjectMessage	= 6511;
const uint32 UserEnteredParcelMessage		= 6512;
const uint32 UserExitedParcelMessage		= 6513;
const uint32 UserClickedScriptedObjectMessage = 6514; // UID object, optional camera-ray origin XYZ, hit position XYZ metres; capability-gated extension.

const uint32 InfoMessageID			= 7001;
const uint32 GameHUDUpdateID			= 7012; // UID game_object, uint32 visible, string text (<=2048); client capability required.
const uint32 GameHUDMenuUpdateID			= 7013; // UID game_object, optional panel ID, visible, title, body, optional layout, <=8 actions, optional <=4 form-field/progress-bar and <=8 positioned text-widget specs/styles.
const uint32 GameStateUpdateID			= 7014; // UID game_object, uint32 version, bounded serialized Luau state; client capability required.
const uint32 GameEventID				= 7015; // UID game_object, uint32 sequence, event name (<=32), optional server world time; client capability required.
const uint32 ScriptInteractionPromptUpdateID = 7016; // UID object, visible, input mode, bounded prompt text; client capability required.
const uint32 AvatarMaterialiseEffectID = 7017; // UID avatar; start the standard client-side materialisation effect.
const uint32 ErrorMessageID			= 7002;
const uint32 ServerAdminMessageID	= 7010; // Allows server to send a message like "Server going down for reboot soon"
// Server -> authenticated superadmin only: avatar UID, user ID, username,
// world name, position (3 doubles), heading in radians (double).
const uint32 AdminWorldEntryMessageID = 7011;

const uint32 LogInMessage			= 8000;
const uint32 LogOutMessage			= 8001;
// SignUpMessage payload: username, email, password, avatar choice string,
// terms-accepted uint32, privacy-accepted uint32.
const uint32 SignUpMessage			= 8002;
const uint32 LoggedInMessageID		= 8003;
const uint32 LoggedOutMessageID		= 8004;
const uint32 SignedUpMessageID		= 8005;
const uint32 GuestModeMessageID		= 8006;

const uint32 RequestPasswordReset	= 8010; // Client wants to reset the password for a given email address.  Obsolete, does nothing.  Use website instead.
const uint32 ChangePasswordWithResetToken = 8011; // Client is sending the password reset token, email address, and the new password.  Obsolete, does nothing.  Use website instead.

const uint32 TimeSyncMessage		= 9000; // Sends the current time

const uint32 ScreenShotRequest		= 11001;
const uint32 ScreenShotSucceeded	= 11002;
const uint32 TileScreenShotRequest	= 11003;
const uint32 GearScreenShotRequest	= 11004;


const uint32 SubmitEthTransactionRequest		= 12001;
const uint32 EthTransactionSubmitted			= 12002;
const uint32 EthTransactionSubmissionFailed		= 12003;

const uint32 KeepAlive				= 13000; // A message that doesn't do anything apart from provide a means for the client or server to check a connection is still working by making a socket call.

const uint32 PhotoUploadSucceeded	= 14000;
const uint32 PhotoUploadFailed		= 14001;




// Client capabilities
const uint32 STREAMING_COMPRESSED_OBJECT_SUPPORT	= 0x1; // Can the client handle ObjectInitialSendCompressed messages?
const uint32 SENDS_USER_MOVED_CHATBOT_MSGS			= 0x2;//  Does the client send UserMovedNearToAvatar and userMovedAwayFromBotAvatar msgs?

const uint32 AUTHORISED_OBJECT_COPY_SUPPORT = 0x4; // Uses source-UID CopyObject for clone/paste.
const uint32 ADMIN_WORLD_ENTRY_SUPPORT = 0x8; // Can receive AdminWorldEntryMessageID. Does not grant permission.
const uint32 SCRIPTED_OBJECT_CLICK_CLIENT_SUPPORT = 0x200000; // Sends bounded world-object clicks with a hit position.
const uint32 SCRIPTED_OBJECT_CLICK_RAY_CLIENT_SUPPORT = 0x400000; // Can include the camera ray origin with scripted-object clicks.
const uint32 SCRIPTED_AVATAR_MATERIALISE_EFFECT_CLIENT_SUPPORT = 0x800000; // Can render server-triggered avatar materialisation effects.

// Server capabilities
const uint32 OBJECT_TEXTURE_BASISU_SUPPORT			= 0x1;
const uint32 TERRAIN_DETAIL_MAPS_BASISU_SUPPORT		= 0x2;
const uint32 OPTIMISED_MESH_SUPPORT					= 0x4;
// The server implements QueryUserGear/UserGearList/GearItemUpdate/CreateGearItem.
// Do not infer this from the protocol version: some Metasiberia v52+ servers
// advertised the message IDs but did not have the corresponding handlers.
const uint32 GEAR_INVENTORY_SUPPORT					= 0x8;
const uint32 OBJECT_COPY_SUPPORT                         = 0x10; // Authoritative source-UID based copy/paste with ObjectCopyResult.
const uint32 SCRIPTED_OBJECT_KEYBOARD_SUPPORT             = 0x20; // Server accepts bounded focused digital input events for scripted games.
const uint32 SCRIPTED_INTERACTION_PROMPT_CLIENT_SUPPORT    = 0x20000000; // Client can receive per-object interaction prompt state.
const uint32 SCRIPTED_INTERACTION_KEY_CLIENT_SUPPORT       = 0x40000000; // Client can receive custom per-object key bindings.
const uint32 SCRIPTED_INTERACTION_PROMPT_SUPPORT           = 0x40000000; // Server can send interaction prompt state.
const uint32 SCRIPTED_OBJECT_CLICK_SUPPORT                 = 0x1000000; // Server validates and dispatches scripted-object clicks.
const uint32 SCRIPTED_OBJECT_CLICK_RAY_SUPPORT             = 0x2000000; // Server validates click occlusion from the client's bounded camera ray.
const uint32 SCRIPTED_AVATAR_MATERIALISE_EFFECT_SUPPORT    = 0x4000000; // Server can broadcast standard avatar materialisation effects.
const uint32 SCRIPTED_INTERACTION_KEY_SUPPORT                = 0x08000000; // Server accepts per-object custom-key interactions and sends key bindings.
const uint32 SCRIPTED_GAME_ANALOG_INPUT_SUPPORT             = 0x40; // Server accepts bounded focused pointer/analog game input snapshots.
const uint32 SCRIPTED_GAME_RPC_SUPPORT                      = 0x80; // Server accepts bounded focused client-to-server game messages.
const uint32 SCRIPTED_GAME_HUD_SUPPORT                     = 0x40; // Server can send targeted game HUD updates to capable clients.
const uint32 SCRIPTED_GAME_HUD_LAYOUT_SUPPORT               = 0x100; // Server appends normalized menu layout floats to GameHUDMenuUpdateID.
const uint32 SCRIPTED_GAME_HUD_CLIENT_SUPPORT               = 0x10; // Client can receive targeted game HUD and interactive menu packets.
const uint32 SCRIPTED_GAME_HUD_LAYOUT_CLIENT_SUPPORT         = 0x20; // Client can receive normalized menu layout floats.
const uint32 SCRIPTED_GAME_HUD_PANELS_SUPPORT                 = 0x200; // Server appends a bounded panel ID to GameHUDMenuUpdateID.
const uint32 SCRIPTED_GAME_HUD_FORM_SUPPORT                    = 0x400; // Server appends bounded field specs to GameHUDMenuUpdateID and accepts form submits.
const uint32 SCRIPTED_GAME_HUD_CHOICES_SUPPORT                 = 0x800; // Server appends bounded select options to GameHUDMenuUpdateID for capable clients.
const uint32 SCRIPTED_GAME_TRIGGER_INPUT_SUPPORT               = 0x1000; // Server accepts two extra trigger axes in focused game input snapshots.
const uint32 SCRIPTED_GAME_HUD_PANELS_CLIENT_SUPPORT          = 0x40; // Client can receive named interactive HUD panels.
const uint32 SCRIPTED_GAME_HUD_FORM_CLIENT_SUPPORT             = 0x80; // Client can render and submit bounded game HUD forms.
const uint32 SCRIPTED_GAME_HUD_CHOICES_CLIENT_SUPPORT          = 0x100; // Client can render and submit bounded select fields.
const uint32 SCRIPTED_GAME_TRIGGER_INPUT_CLIENT_SUPPORT        = 0x200; // Client appends trigger axes to focused game input snapshots.
const uint32 SCRIPTED_GAME_STATE_DELTA_SUPPORT                 = 0x2000; // Server can send prefix/suffix deltas for scripted-game state snapshots.
const uint32 SCRIPTED_GAME_HUD_NUMERIC_SUPPORT                 = 0x4000; // Server can send bounded numeric form fields in game HUD menus.
const uint32 SCRIPTED_GAME_STATE_DELTA_CLIENT_SUPPORT          = 0x400; // Client can reconstruct versioned scripted-game state deltas.
const uint32 SCRIPTED_GAME_HUD_NUMERIC_CLIENT_SUPPORT           = 0x800; // Client can render and submit bounded numeric HUD form fields.
const uint32 SCRIPTED_GAME_HUD_CHECKBOX_SUPPORT                 = 0x8000; // Server can send and validate boolean HUD form fields.
const uint32 SCRIPTED_GAME_HUD_ACTION_LAYOUT_SUPPORT             = 0x10000; // Server appends optional normalized per-action button bounds.
const uint32 SCRIPTED_GAME_HUD_PROGRESS_SUPPORT                  = 0x80000; // Server appends bounded progress-bar specs to GameHUDMenuUpdateID.
const uint32 SCRIPTED_GAME_HUD_TEXT_WIDGETS_SUPPORT               = 0x100000; // Server appends up to eight positioned text widgets to GameHUDMenuUpdateID.
const uint32 SCRIPTED_GAME_HUD_CHECKBOX_CLIENT_SUPPORT          = 0x1000; // Client can render and submit boolean HUD form fields.
const uint32 SCRIPTED_GAME_HUD_ACTION_LAYOUT_CLIENT_SUPPORT      = 0x2000; // Client can receive per-action HUD button bounds.
const uint32 SCRIPTED_GAME_HUD_TEXT_LAYOUT_SUPPORT                 = 0x20000; // Server appends optional normalized text-HUD placement.
const uint32 SCRIPTED_GAME_EVENT_SUPPORT                           = 0x40000; // Server can send bounded reliable game events to focused participants.
const uint32 SCRIPTED_GAME_EVENT_TIME_SUPPORT                      = 0x200000; // Server appends synchronized world time to game events when the client advertises support.
const uint32 SCRIPTED_GAME_HUD_TEXT_LAYOUT_CLIENT_SUPPORT          = 0x4000; // Client can receive normalized text-HUD placement.
const uint32 SCRIPTED_GAME_EVENT_CLIENT_SUPPORT                    = 0x8000; // Client can receive reliable game event packets.
const uint32 SCRIPTED_GAME_EVENT_TIME_CLIENT_SUPPORT               = 0x40000; // Client can receive synchronized world time on game events.
const uint32 SCRIPTED_GAME_HUD_PROGRESS_CLIENT_SUPPORT             = 0x10000; // Client can render progress bars in scripted-game menus.
const uint32 SCRIPTED_GAME_WHEEL_INPUT_SUPPORT                     = 0x80000; // Server accepts bounded focused mouse-wheel deltas in game input snapshots.
const uint32 SCRIPTED_GAME_WHEEL_INPUT_CLIENT_SUPPORT               = 0x10000; // Client appends mouse-wheel deltas to focused game input snapshots.
const uint32 SCRIPTED_GAME_HUD_TEXT_WIDGETS_CLIENT_SUPPORT          = 0x20000; // Client receives positioned text widgets in game HUD menus.
const uint32 SCRIPTED_GAME_AIM_RAY_SUPPORT                          = 0x400000; // Server accepts a bounded camera ray in game input snapshots.
const uint32 SCRIPTED_GAME_HUD_TEXT_STYLE_SUPPORT                   = 0x800000; // Server appends text and card colours to positioned game HUD text widgets.
const uint32 SCRIPTED_GAME_AIM_RAY_CLIENT_SUPPORT                   = 0x80000; // Client appends its cursor or screen-centre view ray to game input snapshots.
const uint32 SCRIPTED_GAME_HUD_TEXT_STYLE_CLIENT_SUPPORT            = 0x100000; // Client receives text and card colours for positioned game HUD text widgets.

// Game input codes are part of UserPressedObjectKeyMessage and must remain
// stable across client/server builds. Letters and digits use CharacterBase +
// lowercase ASCII; input is forwarded only while an object has explicit focus.
enum ScriptKeyCode : uint32
{
	ScriptKey_Left = 1,
	ScriptKey_Right,
	ScriptKey_Up,
	ScriptKey_Down,
	ScriptKey_Space,
	ScriptKey_Enter,
	ScriptKey_Escape,
	ScriptKey_W,
	ScriptKey_A,
	ScriptKey_S,
	ScriptKey_D,
	ScriptKey_GamepadA,
	ScriptKey_GamepadB,
	ScriptKey_GamepadX,
	ScriptKey_GamepadY,
	ScriptKey_MouseLeft,
	ScriptKey_MouseMiddle,
	ScriptKey_MouseRight,
	ScriptKey_GamepadL1,
	ScriptKey_GamepadR1,
	ScriptKey_GamepadL3,
	ScriptKey_GamepadR3,
	ScriptKey_GamepadStart,
	ScriptKey_GamepadSelect,
	ScriptKey_GamepadUp,
	ScriptKey_GamepadDown,
	ScriptKey_GamepadLeft,
	ScriptKey_GamepadRight,
	ScriptKey_CharacterBase = 0x1000,
	ScriptKey_FunctionBase = 0x2000, // F1-F12 use FunctionBase + 1..12.
	ScriptKey_Backspace = 0x2100,
	ScriptKey_Delete,
	ScriptKey_PageUp,
	ScriptKey_PageDown,
	ScriptKey_Home,
	ScriptKey_End,
	ScriptKey_Minus,
	ScriptKey_Equals,
	ScriptKey_Plus,
	ScriptKey_LeftBracket,
	ScriptKey_RightBracket
};

const int OPTIMISED_MESH_VERSION = 3;

} // end namespace Protocol
