/*=====================================================================
BotEditorWidget.h
-----------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#pragma once

#include <QtWidgets/QWidget>
#include "../shared/UID.h"
#include "../shared/Avatar.h"
#include "../server/ChatBot.h"
#include <maths/vec3.h>
#include <stdint.h>
#include <string>
#include <vector>

class QLineEdit;
class QPlainTextEdit;
class QDoubleSpinBox;
class QSpinBox;
class QCheckBox;
class QComboBox;
class QPushButton;
class QTabWidget;
class QLabel;
class QTimer;
class QListWidget;
class QTableWidget;
class GUIClient;


class BotEditorWidget : public QWidget
{
	Q_OBJECT
public:
	explicit BotEditorWidget(QWidget* parent = nullptr);
	~BotEditorWidget();

	void init(GUIClient* gui_client_);

	// Animation slot indices (public so .cpp file-scope arrays can use them)
	enum : int {
		AS_GREETING    = 0,
		AS_IDLE        = 1,
		AS_REACTIVE    = 2,
		AS_SURPRISE    = 3,
		AS_ACKNOWLEDGE = 4,
		AS_FAREWELL    = 5,
		AS_WALK        = 6,
		AS_TALK        = 7,
		AS_INTERACTION = 8,
		NUM_ANIM_SLOTS = 9
	};

	void setBot(uint64_t bot_id_, const UID& avatar_uid_,
		const std::string& name, const std::string& avatar_url,
		const std::string& prompt,
		double pos_x, double pos_y, double pos_z, double heading_deg,
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
		uint32 use_action_type, const std::string& use_action_param,
		const std::string& api_key, const std::string& api_endpoint,
		uint32 movement_type, double walk_speed, double wander_radius,
		const std::vector<BotWaypoint>& waypoints,
		const std::vector<BotUseAction>& use_actions,
		const std::string& farewell_gesture_name, const std::string& farewell_gesture_url,
		uint32 farewell_gesture_flags, double farewell_gesture_cooldown,
		const std::string& walk_gesture_name, const std::string& walk_gesture_url, uint32 walk_gesture_flags,
		const std::string& talk_gesture_name, const std::string& talk_gesture_url, uint32 talk_gesture_flags,
		const std::string& interaction_gesture_name, const std::string& interaction_gesture_url,
		uint32 interaction_gesture_flags, double interaction_gesture_cooldown,
		double audio_min_distance, double audio_start_delay,
		const std::string& greeting_audio_url, const std::string& farewell_audio_url2,
		const std::string& interaction_audio_url,
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
		uint32 dialog_start_node_id = 0, const std::vector<BotDialogNode>& dialog_nodes_in = {},
		// Block 11: memory + content safety
		bool enable_player_memory = false, uint32 memory_summary_tokens = 150,
		const std::string& content_filter_patterns = "", bool jailbreak_guard = true,
		// Block 12: per-player rate, cache, fallback, retry, stats
		uint32 max_llm_calls_per_player_per_hour = 0, bool response_cache_enabled = false,
		uint32 response_cache_ttl_s = 300, const std::string& fallback_model_id = "",
		const std::string& fallback_api_key = "", const std::string& fallback_api_endpoint = "",
		uint32 llm_max_retries = 0,
		uint32 stats_conversations_24h = 0, uint32 stats_llm_calls_total = 0);

	void showConversationLog(const std::vector<std::array<std::string,5>>& entries);
	void showPlayerMemoryList(const std::vector<std::array<std::string,6>>& entries);

	struct BotListEntry
	{
		uint64_t bot_id = 0;
		UID avatar_uid;
		std::string name;
	};
	void setBotList(const std::vector<BotListEntry>& bots);

	void clear();
	void updatePosition(double x, double y, double z);

	uint64_t currentBotId()    const { return bot_id; }
	UID      currentAvatarUID() const { return avatar_uid; }

signals:
	void saveClicked();
	void deleteClicked();
	void cancelClicked();
	void botSelected(uint64_t bot_id, const UID& avatar_uid);

private slots:
	void onSave();
	void onDelete();
	void onCancel();
	void onBrowseAvatar();
	void sendMoveBot();
	void scheduleMove();
	void sendUpdateBot();
	void onAvatarURLChanged();
	void onBotListCurrentRowChanged(int row);
	void onRefreshBots();
	void onWaypointAdd();
	void onWaypointRemove();
	void onWaypointTakePos();
	void onUseActionAdd();
	void onUseActionRemove();
	void onScriptedResponseAdd();
	void onScriptedResponseRemove();
	void onRefreshPromptPreview();
	void onDuplicateBot();
	void onExportJson();
	void onImportJson();
	void onApplyTemplate(int index);
	void onTeleportToBot();
	void onRequestConversationLog();
	void onSendManualMessage();
	void onRequestPlayerMemoryList();
	void onEditPlayerMemory();
	void onClearPlayerMemory();
	void onValidateDialogTree();
	void onBotListSearchChanged(const QString& text);
	void onDialogNodeAdd();
	void onDialogNodeRemove();
	void onDialogNodeSelectionChanged();
	void onDialogChoiceAdd();
	void onDialogChoiceRemove();
	void onToolFunctionAdd();
	void onToolFunctionRemove();
	void onWhitelistAdd();
	void onWhitelistRemove();
	void onBlacklistAdd();
	void onBlacklistRemove();

private:
	void buildUI();
	void callUpdateBot(const AvatarSettings& av, const std::string& audio_url,
		uint32 flags, uint32 trigger_flags, const Vec3f& model_scale);
	void addUseActionRow(uint32 type = 0, const QString& label = {}, const QString& param = {}, const QString& uuid_filter = {});

	GUIClient*   gui_client = nullptr;
	uint64_t     bot_id     = 0;
	UID          avatar_uid;
	QTimer*      move_timer  = nullptr;

	QListWidget* bot_list_widget = nullptr;
	QPushButton* refresh_bots_btn = nullptr;
	std::vector<BotListEntry> bot_list_entries;

	QTabWidget*  tab_widget = nullptr;

	// ── Position section ──────────────────────────────────────────────
	QDoubleSpinBox* pos_x_spin    = nullptr;
	QDoubleSpinBox* pos_y_spin    = nullptr;
	QDoubleSpinBox* pos_z_spin    = nullptr;
	QDoubleSpinBox* heading_spin  = nullptr;

	// ── Identity tab ──────────────────────────────────────────────────
	QLineEdit*      name_edit        = nullptr;
	QLineEdit*      avatar_url_edit  = nullptr;
	QPushButton*    avatar_browse    = nullptr;
	QDoubleSpinBox* scale_x_spin  = nullptr;
	QDoubleSpinBox* scale_y_spin  = nullptr;
	QDoubleSpinBox* scale_z_spin  = nullptr;

	// ── AI / Prompt tab ───────────────────────────────────────────────
	QPlainTextEdit* prompt_edit   = nullptr;
	QLineEdit*      ai_model_edit = nullptr;
	QLineEdit*      ai_preset_edit = nullptr;
	QPlainTextEdit* ai_knowledge_edit = nullptr;
	QDoubleSpinBox* ai_temperature_spin = nullptr;
	QSpinBox*       ai_max_tokens_spin = nullptr;
	QLineEdit*      fallback_msg_edit = nullptr;
	QLabel*         llm_note      = nullptr;
	QLineEdit*      api_key_edit      = nullptr;
	QLineEdit*      api_endpoint_edit = nullptr;

	// ── Behaviour tab ─────────────────────────────────────────────────
	QDoubleSpinBox* greet_dist_spin    = nullptr;
	QDoubleSpinBox* farewell_dist_spin = nullptr;
	QDoubleSpinBox* talk_radius_spin   = nullptr;
	QCheckBox*      always_face_cb     = nullptr;
	QCheckBox*      stationary_cb      = nullptr;
	QCheckBox*      disabled_cb        = nullptr;
	QCheckBox*      trigger_proximity_cb = nullptr;
	QCheckBox*      trigger_chat_cb      = nullptr;
	QCheckBox*      trigger_keywords_cb  = nullptr;
	QCheckBox*      trigger_gesture_cb   = nullptr;
	QCheckBox*      trigger_use_cb       = nullptr;
	QLineEdit*      trigger_keywords_edit = nullptr;
	QDoubleSpinBox* trigger_cooldown_spin = nullptr;

	// ── Behaviour tab — multiple use-actions (block 5) ────────────────
	QTableWidget*   use_actions_table    = nullptr;
	QPushButton*    ua_add_btn           = nullptr;
	QPushButton*    ua_remove_btn        = nullptr;

	// ── Behaviour tab — movement (block 5) ───────────────────────────
	QComboBox*      movement_type_combo  = nullptr;
	QDoubleSpinBox* walk_speed_spin      = nullptr;
	QDoubleSpinBox* wander_radius_spin   = nullptr;
	QTableWidget*   waypoints_table      = nullptr;
	QPushButton*    wp_add_btn           = nullptr;
	QPushButton*    wp_remove_btn        = nullptr;
	QPushButton*    wp_take_pos_btn      = nullptr;

	// ── Animations tab — compact table ───────────────────────────────
	struct AnimSlot {
		QLineEdit*      name_edit = nullptr;
		QLineEdit*      url_edit  = nullptr;
		QDoubleSpinBox* cd_spin   = nullptr; // nullptr for WALK and TALK
		QCheckBox*      loop_cb   = nullptr;
		QCheckBox*      head_cb   = nullptr;
	};
	AnimSlot        anim_slots[NUM_ANIM_SLOTS];
	QTableWidget*   anim_table            = nullptr;

	// ── Audio tab ─────────────────────────────────────────────────────
	QLineEdit*      audio_url_edit   = nullptr;
	QPushButton*    audio_browse     = nullptr;
	QDoubleSpinBox* audio_vol_spin   = nullptr;
	QDoubleSpinBox* audio_radius_spin= nullptr;
	QDoubleSpinBox* audio_activation_spin= nullptr;
	QDoubleSpinBox* audio_cooldown_spin= nullptr;
	QCheckBox*      audio_autoplay_cb= nullptr;
	QCheckBox*      audio_loop_cb    = nullptr;
	QCheckBox*      audio_spatial_cb = nullptr;
	// Extended sound (block 7)
	QDoubleSpinBox* audio_min_dist_spin    = nullptr;
	QDoubleSpinBox* audio_start_delay_spin = nullptr;
	QLineEdit*      greeting_audio_url_edit     = nullptr;
	QLineEdit*      farewell_audio_url_edit     = nullptr;
	QLineEdit*      interaction_audio_url_edit  = nullptr;

	// ── Extended AI tab (Block 10) ───────────────────────────────────
	QComboBox*      ai_provider_combo        = nullptr;
	QDoubleSpinBox* top_p_spin               = nullptr;
	QSpinBox*       top_k_spin               = nullptr;
	QDoubleSpinBox* freq_penalty_spin        = nullptr;
	QDoubleSpinBox* pres_penalty_spin        = nullptr;
	QSpinBox*       max_ctx_msgs_spin        = nullptr;
	QCheckBox*      stream_llm_cb            = nullptr;
	QCheckBox*      listens_global_chat_cb   = nullptr;
	QCheckBox*      react_to_mention_cb      = nullptr;
	QCheckBox*      react_to_bots_cb         = nullptr;
	QCheckBox*      use_dialog_cb            = nullptr;

	// ── Scenario tab (dialog tree) ────────────────────────────────────
	QTableWidget*   dialog_nodes_table       = nullptr;
	QPushButton*    dn_add_btn               = nullptr;
	QPushButton*    dn_remove_btn            = nullptr;
	QTableWidget*   dialog_choices_table     = nullptr;
	QPushButton*    dc_add_btn               = nullptr;
	QPushButton*    dc_remove_btn            = nullptr;
	QSpinBox*       dialog_start_spin        = nullptr;
	std::vector<BotDialogNode> m_dialog_nodes; // In-memory dialog tree state

	// ── Block 12 controls ────────────────────────────────────────────
	QSpinBox*       player_rate_spin          = nullptr;
	QCheckBox*      cache_enable_cb           = nullptr;
	QSpinBox*       cache_ttl_spin            = nullptr;
	QLineEdit*      fallback_model_edit       = nullptr;
	QLineEdit*      fallback_key_edit         = nullptr;
	QLineEdit*      fallback_ep_edit          = nullptr;
	QSpinBox*       retries_spin              = nullptr;
	// Stats (read-only)
	QLabel*         stats_label               = nullptr;

	// ── Bot list search ───────────────────────────────────────────────
	QLineEdit*      bot_list_search_edit      = nullptr;

	// ── Conversation log tab + manual trigger ────────────────────────
	QTableWidget*   conv_log_table            = nullptr;
	QPushButton*    conv_log_refresh_btn      = nullptr;
	QLineEdit*      manual_msg_edit           = nullptr;
	QPushButton*    manual_msg_send_btn       = nullptr;

	// ── Player CRM tab ────────────────────────────────────────────────
	QTableWidget*   crm_table                 = nullptr;
	QPushButton*    crm_load_btn              = nullptr;
	QPushButton*    crm_edit_btn              = nullptr;
	QPushButton*    crm_clear_btn             = nullptr;

	// ── Statistics tab ────────────────────────────────────────────────
	QLabel*         stats_detail_label        = nullptr;

	// ── Memory + Safety controls (Block 11) ──────────────────────────
	QCheckBox*      memory_enable_cb        = nullptr;
	QSpinBox*       memory_tokens_spin      = nullptr;
	QPlainTextEdit* filter_patterns_edit    = nullptr;
	QCheckBox*      jailbreak_guard_cb      = nullptr;

	// ── Test tab (prompt preview) ─────────────────────────────────────
	QLineEdit*      test_player_name_edit   = nullptr;
	QPlainTextEdit* test_prompt_preview     = nullptr;
	QPushButton*    test_refresh_btn        = nullptr;

	// ── Advanced tab (Block 9) ────────────────────────────────────────
	QDoubleSpinBox* conv_timeout_spin       = nullptr;
	QSpinBox*       max_llm_calls_spin      = nullptr;
	QLineEdit*      webhook_url_edit        = nullptr;
	QCheckBox*      active_hours_cb         = nullptr;
	QSpinBox*       active_hours_start_spin = nullptr;
	QSpinBox*       active_hours_end_spin   = nullptr;
	// Scripted responses
	QTableWidget*   scripted_resp_table     = nullptr;
	QPushButton*    sr_add_btn              = nullptr;
	QPushButton*    sr_remove_btn          = nullptr;
	// Player whitelist / blacklist
	QTableWidget*   whitelist_table         = nullptr;
	QPushButton*    wl_add_btn              = nullptr;
	QPushButton*    wl_remove_btn           = nullptr;
	QTableWidget*   blacklist_table         = nullptr;
	QPushButton*    bl_add_btn              = nullptr;
	QPushButton*    bl_remove_btn           = nullptr;
	// Tool functions tab
	QTableWidget*   tool_func_table         = nullptr;
	QPushButton*    tf_add_btn              = nullptr;
	QPushButton*    tf_remove_btn           = nullptr;

	// ── Buttons ───────────────────────────────────────────────────────
	QPushButton*    save_btn      = nullptr;
	QPushButton*    cancel_btn    = nullptr;
	QPushButton*    delete_btn    = nullptr;
	QPushButton*    duplicate_btn = nullptr;
	QPushButton*    teleport_btn  = nullptr;
	QComboBox*      template_combo = nullptr;
};
