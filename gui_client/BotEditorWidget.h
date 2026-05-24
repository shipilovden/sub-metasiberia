/*=====================================================================
BotEditorWidget.h
-----------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#pragma once

#include <QtWidgets/QWidget>
#include "../shared/UID.h"
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
class GUIClient;


class BotEditorWidget : public QWidget
{
	Q_OBJECT
public:
	explicit BotEditorWidget(QWidget* parent = nullptr);
	~BotEditorWidget();

	void init(GUIClient* gui_client_);

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
		uint32 trigger_flags, const std::string& trigger_keywords, double trigger_cooldown);
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
	void sendMoveBot();       // called by debounce timer
	void scheduleMove();      // called by valueChanged signals
	void sendUpdateBot();     // called by Save
	void onAvatarURLChanged();// called when avatar URL editingFinished
	void onBotListCurrentRowChanged(int row);
	void onRefreshBots();

private:
	void buildUI();

	GUIClient*   gui_client = nullptr;
	uint64_t     bot_id     = 0;
	UID          avatar_uid;
	QTimer*      move_timer  = nullptr; // debounce timer for position changes

	QListWidget* bot_list_widget = nullptr;
	QPushButton* refresh_bots_btn = nullptr;
	std::vector<BotListEntry> bot_list_entries;

	QTabWidget*  tab_widget = nullptr;

	// ── Position section (always visible above tabs) ──────────────────
	QDoubleSpinBox* pos_x_spin    = nullptr;
	QDoubleSpinBox* pos_y_spin    = nullptr;
	QDoubleSpinBox* pos_z_spin    = nullptr;
	QDoubleSpinBox* heading_spin  = nullptr;

	// ── Identity tab ─────────────────────────────────────────────────
	QLineEdit*      name_edit        = nullptr;
	QLineEdit*      avatar_url_edit  = nullptr;
	QPushButton*    avatar_browse    = nullptr;

	// Scale
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
	QLabel*         llm_note      = nullptr;

	// ── Behaviour tab ─────────────────────────────────────────────────
	QDoubleSpinBox* greet_dist_spin    = nullptr;  // metres - greet when user within
	QDoubleSpinBox* farewell_dist_spin = nullptr;  // metres - farewell when user beyond
	QDoubleSpinBox* talk_radius_spin   = nullptr;  // metres - respond to chat within
	QCheckBox*      always_face_cb     = nullptr;  // always rotate to face nearest user
	QCheckBox*      stationary_cb      = nullptr;  // don't drift / stay on spawn point
	QCheckBox*      disabled_cb        = nullptr;  // disable all bot logic
	QCheckBox*      trigger_proximity_cb = nullptr;
	QCheckBox*      trigger_chat_cb      = nullptr;
	QCheckBox*      trigger_keywords_cb  = nullptr;
	QCheckBox*      trigger_gesture_cb   = nullptr;
	QCheckBox*      trigger_use_cb       = nullptr;
	QLineEdit*      trigger_keywords_edit = nullptr;
	QDoubleSpinBox* trigger_cooldown_spin = nullptr;

	// ── Animations tab ────────────────────────────────────────────────
	QLineEdit*      greet_name_edit  = nullptr;
	QLineEdit*      greet_url_edit   = nullptr;
	QDoubleSpinBox* greet_cd         = nullptr;
	QPushButton*    greet_test_btn   = nullptr;

	QLineEdit*      idle_name_edit   = nullptr;
	QLineEdit*      idle_url_edit    = nullptr;
	QDoubleSpinBox* idle_int         = nullptr;
	QPushButton*    idle_test_btn    = nullptr;

	QLineEdit*      react_name_edit  = nullptr;
	QLineEdit*      react_url_edit   = nullptr;
	QDoubleSpinBox* react_cd2        = nullptr;
	QPushButton*    react_test_btn   = nullptr;

	// ── Audio tab ────────────────────────────────────────────────────
	QLineEdit*      audio_url_edit   = nullptr;
	QPushButton*    audio_browse     = nullptr;
	QDoubleSpinBox* audio_vol_spin   = nullptr;   // 0.0 – 1.0
	QDoubleSpinBox* audio_radius_spin= nullptr;   // metres
	QDoubleSpinBox* audio_activation_spin= nullptr;
	QDoubleSpinBox* audio_cooldown_spin= nullptr;
	QCheckBox*      audio_autoplay_cb= nullptr;
	QCheckBox*      audio_loop_cb    = nullptr;
	QCheckBox*      audio_spatial_cb = nullptr;

	// ── Buttons ───────────────────────────────────────────────────────
	QPushButton*    save_btn   = nullptr;
	QPushButton*    cancel_btn = nullptr;
	QPushButton*    delete_btn = nullptr;
};
