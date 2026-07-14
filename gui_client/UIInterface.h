/*=====================================================================
UIInterface.h
-------------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#pragma once


#include "../shared/UID.h"
#include "../server/ChatBot.h"
#include <graphics/ImageMap.h>
#include <maths/vec2.h>
#include <maths/vec3.h>
#include <array>
#include <string>
#include <vector>
class WorldObject;
class Parcel;
class MapTilesResultReceivedMessage;
enum class VoxelToolType;
struct VoxelToolSettings;


/*=====================================================================
UIInterface
-----------
UI Functionality provided by the host platform.
The implementation will be specific to Qt or a web browser etc.
=====================================================================*/
class UIInterface
{
public:
	virtual void appendChatMessage(const std::string& msg) = 0;
	virtual void clearChatMessages() = 0;

	virtual bool isShowParcelsEnabled() const = 0;

	virtual void updateOnlineUsersList() = 0;
	virtual void handleMapTilesResultReceivedMessage(const MapTilesResultReceivedMessage& msg) { (void)msg; }

	virtual void showHTMLMessageBox(const std::string& title, const std::string& msg) = 0;
	virtual void showPlainTextMessageBox(const std::string& title, const std::string& msg) = 0;

	virtual void logMessage(const std::string& msg) = 0;

	// Lua scripting:
	// A lua script created by the logged in user printed something
	virtual void printFromLuaScript(const std::string& msg, UID object_uid) {}
	virtual void luaErrorOccurred(const std::string& msg, UID object_uid) {}

	// UserDetailsWidget:
	virtual void setTextAsNotLoggedIn() = 0;
	virtual void setTextAsLoggedIn(const std::string& username) = 0;

	// Login/signup buttons
	virtual void loginButtonClicked() = 0;
	virtual void signUpButtonClicked() = 0;
	virtual void loggedInButtonClicked() = 0;

	// worldSettingsWidget:
	virtual void updateWorldSettingsControlsEditable() = 0;
	virtual void updateWorldSettingsUIFromWorldSettings() = 0;

	virtual bool diagnosticsVisible() = 0;
	virtual bool showObAABBsEnabled() = 0;
	virtual bool showPhysicsObOwnershipEnabled() = 0;
	virtual bool showVehiclePhysicsVisEnabled() = 0;
	virtual bool showPlayerPhysicsVisEnabled() = 0;
	virtual bool showLodChunksVisEnabled() = 0;

	virtual void writeTransformMembersToObject(WorldObject& ob) = 0; // Get updated transform members from object editor and store in ob.
	virtual void objectLastModifiedUpdated(const WorldObject& ob) = 0; // ob.last_modified_time has been updated, update corresponding UI label.
	virtual void objectModelURLUpdated(const WorldObject& ob) = 0; // Update model URL in UI.
	virtual void objectLightmapURLUpdated(const WorldObject& ob) = 0; // Update lightmap URL in UI.

	
	virtual void showEditorDockWidget() = 0;
	// Parcel editor
	virtual void showParcelEditor() = 0; // Show parcel editor and hide object editor.
	virtual void setParcelEditorForParcel(const Parcel& parcel) = 0;
	virtual void setParcelEditorEnabled(bool enabled) = 0;
	// can_edit_basic_fields: title/description/flags/spawn-point
	// can_edit_owner_and_geometry: owner and parcel bounds (position/scale)
	// can_edit_member_lists: admins and writers
	virtual void setParcelEditorPermissions(bool can_edit_basic_fields, bool can_edit_owner_and_geometry, bool can_edit_member_lists) = 0;
	// Object editor
	virtual void showObjectEditor() = 0; // Show object editor and hide parcel editor.
	virtual void setObjectEditorControlsEditable(bool editable) = 0;
	virtual void setObjectEditorEnabled(bool enabled) = 0;
	virtual void setObjectEditorFromOb(const WorldObject& ob, int selected_mat_index, bool ob_in_editing_users_world) = 0;
	virtual int getSelectedMatIndex() = 0; 
	virtual void objectEditorToObject(WorldObject& ob) = 0; // Sets changed_flags on object as well.
	virtual void objectEditorObjectPickedUp() = 0;
	virtual void objectEditorObjectDropped() = 0;
	virtual bool snapToGridCheckBoxChecked() = 0;
	virtual double gridSpacing() = 0;
	virtual bool posAndRot3DControlsEnabled() = 0;
	virtual void startObEditorTimerIfNotActive() = 0;
	virtual void startLightmapFlagTimer() = 0;

	// Optional native voxel-editor bridge.  The SDL/web UI keeps the default
	// no-op implementation and therefore retains the legacy Ctrl/Alt tools.
	virtual bool getVoxelEditorToolState(VoxelToolType& tool_out, VoxelToolSettings& settings_out) const
	{
		(void)tool_out;
		(void)settings_out;
		return false;
	}
	virtual void voxelEditorMaterialPicked(int material_index) { (void)material_index; }
	virtual void voxelEditorObjectDataChanged(const WorldObject& ob) { (void)ob; }

	virtual void showAvatarSettings() = 0; // Show avatar settings dialog.
	virtual bool isAvatarSettingsDialogVisible() const { return false; } // Check if avatar settings dialog is visible (SDL only)
	
	virtual void setWebcamWindowVisible(bool visible) {} // Show/hide webcam window (Qt only)
	
	virtual void setUIForSelectedObject() = 0; // Enable/disable delete object action etc. based on if there is a selected object or not.

	
	virtual bool isCursorHidden() = 0;
	virtual void hideCursor() = 0;

	virtual void setCamRotationOnMouseDragEnabled(bool enabled) = 0; // Do we want mouse click + dragging to move the camera?

	virtual void setKeyboardCameraMoveEnabled(bool enabled) = 0; // Do we want WASD keys etc. to move the camera?  We don't want this while e.g. we enter text into a webview.
	virtual bool isKeyboardCameraMoveEnabled() = 0;

	virtual bool hasFocus() = 0; // Does OpenGL widget have focus?

	virtual void setHelpInfoLabelToDefaultText() = 0;
	virtual void setHelpInfoLabel(const std::string& text) = 0;

	virtual void toggleFlyMode() = 0;

	// TODO: simplify this interface
	virtual void enableThirdPersonCamera() = 0;
	virtual void enableThirdPersonCameraIfNotAlreadyEnabled() = 0;
	virtual void toggleThirdPersonCameraMode() = 0;
	virtual void enableFirstPersonCamera() = 0;

	virtual void openURL(const std::string& URL) = 0;

	virtual Vec2i getMouseCursorWidgetPos() = 0; // Get mouse cursor position, relative to gl widget.

	// Credential manager
	virtual std::string getUsernameForDomain(const std::string& domain) = 0; // Returns empty string if no stored username for domain
	virtual std::string getDecryptedPasswordForDomain(const std::string& domain) = 0; // Returns empty string if no stored password for domain

	virtual bool inScreenshotTakingMode() = 0;
	virtual void takeScreenshot() = 0;
	virtual void showScreenshots() = 0;

	virtual void setGLWidgetContextAsCurrent() = 0;

	virtual Vec2i getGlWidgetPosInGlobalSpace() = 0; // Get top left of the GLWidget in global screen coordinates.

	virtual void webViewDataLinkHovered(const std::string& text) = 0;

	// Gamepad
	virtual bool gamepadAttached() = 0;
	virtual float gamepadButtonL2() = 0;
	virtual float gamepadButtonR2() = 0;
	virtual float gamepadAxisLeftX() = 0;
	virtual float gamepadAxisLeftY() = 0;
	virtual float gamepadAxisRightX() = 0;
	virtual float gamepadAxisRightY() = 0;
	

	// OpenGL
	virtual bool supportsSharedGLContexts() const = 0;
	virtual void* makeNewSharedGLContext() = 0;
	virtual void makeGLContextCurrent(void* context) = 0;

	virtual	void* getID3D11Device() const = 0;


	// File selection
	struct FileTypeFilter
	{
		std::string description; // e.g. "Images"
		std::vector<std::string> file_types; // e.g. "png", "jpg"
	};
	virtual std::string showOpenFileDialog(const std::string& caption, const std::vector<FileTypeFilter>& file_type_filters, const std::string& settings_key) = 0; // Returns path to file selected or empty string if cancelled.

	virtual void openBotSettingsDialog(uint64 bot_id) {}
	// Show bot editor in left panel
	struct BotListEntry
	{
		uint64 bot_id = 0;
		UID avatar_uid;
		std::string name;
	};
	virtual void setBotList(const std::vector<BotListEntry>& bots) {}
	virtual void updateBotEditorPosition(double x, double y, double z) {}
	virtual void showBotEditor(uint64 bot_id, const UID& avatar_uid,
		const std::string& name, const std::string& avatar_url, const std::string& prompt,
		double px, double py, double pz, double heading_deg,
		const std::string& greeting_name, const std::string& greeting_url, double greeting_cooldown,
		const std::string& idle_name, const std::string& idle_url, double idle_interval,
		const std::string& reactive_name, const std::string& reactive_url, double reactive_cooldown,
		uint32 flags, double greeting_distance, double farewell_distance, double chat_radius,
		const Vec3f& model_scale,
		const std::string& ai_model_id, const std::string& ai_personality_preset, const std::string& ai_knowledge, double ai_temperature, uint32 ai_max_tokens,
		const std::string& audio_url, double audio_volume, double audio_radius, double audio_activation_distance, double audio_cooldown,
		uint32 trigger_flags, const std::string& trigger_keywords, double trigger_cooldown,
		uint32 greeting_gesture_flags, uint32 idle_gesture_flags, uint32 reactive_gesture_flags,
		const std::string& fallback_message,
		const std::string& surprise_name, const std::string& surprise_url, uint32 surprise_flags, double surprise_cooldown,
		const std::string& acknowledge_name, const std::string& acknowledge_url, uint32 acknowledge_flags, double acknowledge_cooldown,
		uint32 use_action_type = 0, const std::string& use_action_param = "",
		const std::string& api_key = "", const std::string& api_endpoint = "",
		// Block 5
		uint32 movement_type = 0, double walk_speed = 1.4, double wander_radius = 5.0,
		const std::vector<BotWaypoint>& waypoints_raw = {},
		const std::vector<BotUseAction>& use_actions_raw = {},
		// Block 6
		const std::string& farewell_gesture_name = "", const std::string& farewell_gesture_url = "",
		uint32 farewell_gesture_flags = 0, double farewell_gesture_cooldown = 8.0,
		const std::string& walk_gesture_name = "", const std::string& walk_gesture_url = "", uint32 walk_gesture_flags = 0,
		const std::string& talk_gesture_name = "", const std::string& talk_gesture_url = "", uint32 talk_gesture_flags = 0,
		const std::string& interaction_gesture_name = "", const std::string& interaction_gesture_url = "",
		uint32 interaction_gesture_flags = 0, double interaction_gesture_cooldown = 3.0,
		// Block 7
		double audio_min_distance = 1.0, double audio_start_delay = 0.0,
		const std::string& greeting_audio_url = "", const std::string& farewell_audio_url2 = "",
		const std::string& interaction_audio_url = "",
		// Block 9: advanced settings
		float conversation_timeout_s = 0.f, uint32 max_llm_calls_per_hour = 0,
		const std::string& webhook_url = "",
		uint32 active_hours_start_utc = 8, uint32 active_hours_end_utc = 22,
		const std::vector<BotScriptedResponse>& scripted_responses = {},
		const std::vector<std::string>& player_whitelist = {},
		const std::vector<std::string>& player_blacklist = {},
		const std::vector<BotToolFunctionInfo>& tool_functions = {},
		// Block 10: extended AI + dialog
		uint32 ai_provider = 0, float top_p = 0.f, uint32 top_k = 0,
		float frequency_penalty = 0.f, float presence_penalty = 0.f, uint32 max_context_messages = 0,
		uint32 dialog_start_node_id = 0, const std::vector<BotDialogNode>& dialog_nodes = {},
		// Block 11
		bool enable_player_memory = false, uint32 memory_summary_tokens = 150,
		const std::string& content_filter_patterns = "", bool jailbreak_guard = true,
		// Block 12
		uint32 max_llm_calls_per_player_per_hour = 0, bool response_cache_enabled = false,
		uint32 response_cache_ttl_s = 300, const std::string& fallback_model_id = "",
		const std::string& fallback_api_key = "", const std::string& fallback_api_endpoint = "",
		uint32 llm_max_retries = 0,
		uint32 stats_conversations_24h = 0, uint32 stats_llm_calls_total = 0) {}
	virtual void showBotConversationLog(uint64 bot_id, const std::vector<std::array<std::string,5>>& entries) {}
	virtual void showBotPlayerMemoryList(uint64 bot_id, const std::vector<std::array<std::string,6>>& entries) {}
	virtual void hideBotEditor() {}
};
