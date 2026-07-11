/*=====================================================================
MainWindow.h
------------
Copyright Glare Technologies Limited 2024 -
=====================================================================*/
#pragma once


#include "UIInterface.h"
#include "GUIClient.h"
#include "CredentialManager.h"
#include "RuntimeTranslation.h"
#include <utils/ArgumentParser.h>
#include <utils/Timer.h>
#include <utils/ComObHandle.h>
#include <utils/SocketBufferOutStream.h>
#include <QtCore/QPoint>
#include <QtCore/QDateTime>
#include <QtCore/QJsonArray>
#include <QtCore/QMap>
#include <QtCore/QStringList>
#include <QtWidgets/QMainWindow>
#include <QtWidgets/QDockWidget>
#include <string>
namespace Ui { class MainWindow; }
namespace glare { class TaskManager; }
class QSettings;
class QSettingsStore;
class UserDetailsWidget;
class URLWidget;
class QLabel;
class LogWindow;
class QMimeData;
class QMenu;
class QDialog;
class QTabWidget;
class QComboBox;
class QLineEdit;
class QScrollArea;
class QTabBar;
class QSplitter;
class QVBoxLayout;
class QToolButton;
class QFrame;
class QAction;
class QActionGroup;
struct ID3D11Device;
struct IMFDXGIDeviceManager;
struct _SDL_GameController;
class RenderStatsWidget;
class MiniDmpSender;
class UpdateManager;
class WebcamWindow;
class AvatarSettingsWidget;
class ScientificObjectEditor;


class MainWindow final : public QMainWindow, public PrintOutput, public UIInterface
{
	Q_OBJECT
public:
	MainWindow(const std::string& base_dir_path, const std::string& appdata_path, const ArgumentParser& args, QWidget* parent = 0);
	~MainWindow();

	void initialiseUI();
	void afterGLInitInitialise(); // Called after glWigget and OpenGLEngine has been initialised.

	void logAndConPrintMessage(const std::string& msg); // Print to console, and appends to LogWindow log display.

	// PrintOutput interface
	virtual void print(const std::string& s) override; // Print a message and a newline character.
	virtual void printStr(const std::string& s) override; // Print a message without a newline character.

	// Semicolon is for intellisense, see http://www.qtsoftware.com/developer/faqs/faq.2007-08-23.5900165993
signals:;
	void resolutionChanged(int, int);

private slots:;
	void on_actionAvatarSettings_triggered();
	void on_actionAddObject_triggered();
	void on_actionAddHypercard_triggered();
	void on_actionAdd_Text_triggered();
	void on_actionAdd_Voxels_triggered();
	void on_actionAdd_Spotlight_triggered();
	void on_actionAdd_Camera_triggered();
	void on_actionAdd_Seat_triggered();
	void on_actionAdd_Portal_triggered();
	void on_actionAdd_to_Favorites_triggered();
	void on_actionAdd_Web_View_triggered();
	void on_actionAdd_Video_triggered();
	void on_actionAdd_Audio_Source_triggered();
	void on_actionAdd_Decal_triggered();
	void on_actionAdd_Particles_triggered();
	void on_actionAddScientificObject_triggered();
	void on_actionCopy_Object_triggered();
	void on_actionPaste_Object_triggered();
	void on_actionCloneObject_triggered();
	void on_actionDeleteObject_triggered();
	void on_actionReset_Layout_triggered();
	void on_actionLogIn_triggered();
	void on_actionSignUp_triggered();
	void on_actionLogOut_triggered();
	void on_actionShow_Parcels_triggered();
	void on_actionFly_Mode_triggered();
	void on_actionThird_Person_Camera_triggered();
	void on_actionGoToMainWorld_triggered();
	void on_actionGoToPersonalWorld_triggered();
	void on_actionGo_to_CryptoVoxels_World_triggered();
	void on_actionGo_to_Substrata_Server_triggered();
	void on_actionGo_to_Metasiberia_Server_triggered();
	void on_actionGo_to_Shki_nvkz_Server_triggered();
	void on_actionGo_to_Map_World_triggered();
	void on_actionGo_to_Parcel_triggered();
	void on_actionGo_to_Position_triggered();
	void on_actionSet_Start_Location_triggered();
	void on_actionGo_To_Start_Location_triggered();
	void on_actionFind_Object_triggered();
	void on_actionList_Objects_Nearby_triggered();
	void on_actionExport_view_to_Indigo_triggered();
	void on_actionTake_Screenshot_triggered();
	void on_actionShow_Screenshot_Folder_triggered();
	void on_actionAbout_Substrata_triggered();
	void on_actionUpdate_triggered();
	void on_actionOptions_triggered();
	void on_actionUndo_triggered();
	void on_actionRedo_triggered();
	void on_actionShow_Log_triggered();
	void on_actionBake_Lightmaps_fast_for_all_objects_in_parcel_triggered();
	void on_actionBake_lightmaps_high_quality_for_all_objects_in_parcel_triggered();
	void on_actionSummon_Bike_triggered();
	void on_actionSummon_Hovercar_triggered();
	void on_actionSummon_Boat_triggered();
	void on_actionSummon_Jet_Ski_triggered();
	void on_actionSummon_Car_triggered();
	void on_actionOpen_Gear_Inventory_triggered();
	void on_actionConvert_Selected_Object_To_Gear_Item_triggered();
	void on_actionAddBot_triggered();
	virtual void openBotSettingsDialog(uint64 bot_id) override;
	virtual void setBotList(const std::vector<UIInterface::BotListEntry>& bots) override;
	virtual void updateBotEditorPosition(double x, double y, double z) override;
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
		uint32 use_action_type, const std::string& use_action_param,
		const std::string& api_key, const std::string& api_endpoint,
		uint32 movement_type, double walk_speed, double wander_radius,
		const std::vector<BotWaypoint>& waypoints_raw,
		const std::vector<BotUseAction>& use_actions_raw,
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
		float conversation_timeout_s, uint32 max_llm_calls_per_hour,
		const std::string& webhook_url,
		uint32 active_hours_start_utc, uint32 active_hours_end_utc,
		const std::vector<BotScriptedResponse>& scripted_responses,
		const std::vector<std::string>& player_whitelist,
		const std::vector<std::string>& player_blacklist,
		const std::vector<BotToolFunctionInfo>& tool_functions,
		// Block 10
		uint32 ai_provider, float top_p, uint32 top_k,
		float frequency_penalty, float presence_penalty, uint32 max_context_messages,
		uint32 dialog_start_node_id, const std::vector<BotDialogNode>& dialog_nodes,
		// Block 11
		bool enable_player_memory, uint32 memory_summary_tokens,
		const std::string& content_filter_patterns, bool jailbreak_guard,
		// Block 12
		uint32 max_llm_calls_per_player_per_hour, bool response_cache_enabled,
		uint32 response_cache_ttl_s, const std::string& fallback_model_id,
		const std::string& fallback_api_key, const std::string& fallback_api_endpoint,
		uint32 llm_max_retries,
		uint32 stats_conversations_24h, uint32 stats_llm_calls_total) override;
	virtual void showBotConversationLog(uint64 bot_id, const std::vector<std::array<std::string,5>>& entries) override;
	virtual void showBotPlayerMemoryList(uint64 bot_id, const std::vector<std::array<std::string,6>>& entries) override;
	virtual void hideBotEditor() override;
private slots:
	void onBotEditorSave();
	void onBotEditorDelete();
	void onBotEditorCancel();
	void onBotEditorBotSelected(uint64 bot_id, const UID& avatar_uid);
	void on_actionMute_Audio_toggled(bool checked);
	void on_actionSave_Object_To_Disk_triggered();
	void on_actionSave_Parcel_Objects_To_Disk_triggered();
	void on_actionLoad_Objects_From_Disk_triggered();
	void on_actionDelete_All_Parcel_Objects_triggered();
	void on_actionEnter_Fullscreen_triggered();
	void on_actionLanguage_English_triggered();
	void on_actionLanguage_Russian_triggered();
	void on_actionGo_Back_triggered();
	void openCurrentLocationInBrowserSlot();

	void diagnosticsWidgetChanged();
	void diagnosticsReloadTerrain();
	void sendChatMessageSlot();
	void sendLightmapNeededFlagsSlot();

	void glWidgetMousePressed(QMouseEvent* e);
	void glWidgetMouseReleased(QMouseEvent* e);
	void glWidgetMouseDoubleClicked(QMouseEvent* e);
	void glWidgetMouseMoved(QMouseEvent* e);
	void glWidgetKeyPressed(QKeyEvent* e);
	void glWidgetkeyReleased(QKeyEvent* e);
	void glWidgetFocusOut();
	void glWidgetMouseWheelEvent(QWheelEvent* e);
	void gamepadButtonXChanged(bool pressed);
	void gamepadButtonAChanged(bool pressed);
	void glWidgetViewportResized(int w, int h);
	void onIndigoViewDockWidgetVisibilityChanged(bool v);
	void glWidgetCutShortcutTriggered();
	void glWidgetCopyShortcutTriggered();
	void glWidgetPasteShortcutTriggered();
	void onUpdateCheckFinished();
	void onUpdateAvailabilityChanged(bool available);

	void enterFullScreenMode();
	void exitFromFullScreenMode();

	void objectTransformEditedSlot();
	void objectEditedSlot();
	void scriptChangedFromEditorSlot();
	void particleBurstNowSlot();
	void particleClearParticlesSlot();
	void parcelEditedSlot();
	void worldSettingsChangedSlot();
	void environmentSettingChangedSlot();
	void bakeObjectLightmapSlot(); // Bake the currently selected object lightmap
	void bakeObjectLightmapHighQualSlot(); // Bake the currently selected object lightmap
	void removeLightmapSignalSlot();
	void posAndRot3DControlsToggledSlot();
	void URLChangedSlot();
	void materialSelectedInBrowser(const std::string& path);
	void updateObjectEditorObTransformSlot();
	void handleURL(const QUrl& url);
	void openServerScriptLogSlot();
	void on_webcamEnableCheckBox_toggled(bool checked);
	void updateFavoritesMenu();
	void toggleChatEmojiPopup();
public:
	bool connectedToUsersWorldOrGodUser();
	void webViewMouseDoubleClicked(QMouseEvent* e);
private:
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
	bool nativeEvent(const QByteArray& event_type, void* message, qintptr* result);
#else
	bool nativeEvent(const QByteArray& event_type, void* message, long* result) override;
#endif
	virtual void closeEvent(QCloseEvent* event) override;
	virtual void timerEvent(QTimerEvent* event) override;
	virtual void changeEvent(QEvent *event) override;
	virtual bool eventFilter(QObject* obj, QEvent* event) override;
	void rebuildChatEmojiPopupContents();
	void sendChatOrEmojiMessage(const std::string& message);
	void setupChatPlayerControls();
	void refreshChatPlayerControls();
	void rebuildChatUserRows();
	void rebuildPrivateDialogRows(const QString& filter);
	void refreshPrivateChatUnreadCount();
	void rebuildChatGroupRows();
	void createChatGroup();
	void showChatGroupSettings(const QString& group_id);
	void appendChatMessageWidget(const QString& html, bool private_message);
	void appendLocalChatMessage(const QString& html, bool private_message, bool remember_in_history = true);
	void appendLocalChatAttachmentMessage(const QStringList& selected_filenames);
	QString chatHistoryFilePath() const;
	void loadChatHistoryFromDisk();
	void saveChatHistoryToDisk() const;
	void rememberChatHistoryEntry(const QString& html, bool private_message);
	void clearChatMessageWidgets();
	void clearPersistentChatHistory();
	void applyChatMessageDisplaySettings();
	void applyChatThemeStylesheet();
	void updateChatMessageVisibility();
	void updateChatTabText();
	void updatePrivateChatTabText();
	void setChatSettingsVisible(bool visible);
	void updateChatLayoutForCurrentTab();
	void addReactionToChatMessage(QFrame* message_row, const QString& reaction);
	void showChatMessageContextMenu(QFrame* message_row, const QString& plain_text, QToolButton* anchor_button);
	void deleteChatMessageRow(QFrame* message_row, bool delete_for_everyone);
	std::string selectedChatRecipientName() const;
	UID selectedChatRecipientUID() const;
	void startPrivateChatWithUser(UID avatar_uid);
	void openPrivateChatDialog(const QString& peer_name, UID peer_uid = UID::invalidUID());
	void notePrivateChatDialogFromPlainText(const QString& plain_text, bool incoming_message, bool count_unread);
	void showChatUserProfile(UID avatar_uid);
	void showChatUserContextMenu(UID avatar_uid, const QPoint& global_pos);
	void showChatAttachmentMenu();
	void showChatMoreSendMenu();
	void sendPrivateChatMessageToSelectedUser();
	void teleportNearSelectedChatUser();
	void startMainTimer();
	void visitSubURL(const std::string& URL); // Visit a substrata 'sub://' URL.  Checks hostname and only reconnects if the hostname is different from the current one.
	void doObjectSelectionTraceForMouseEvent(QMouseEvent* e);
private:
	void initialiseThemesMenu();
	void initialiseLanguageMenu();
	void applyUILanguage(RuntimeTranslation::UILanguage language, bool persist_setting);
	void refreshTranslatedUiText();
	void configureEditAddSubmenu();
	void refreshEditMenuActionIcons();
	void configureMainToolbarButtons();
	void applyMainChromeThemeStylesheet();
	void updateMenuTooltips();
	void refreshMapDockText();
	void updateMapDockState();
	bool applyNamedQtTheme(const std::string& theme_name, bool persist_setting);
	void applyDefaultQtTheme(bool persist_setting);
	void updateThemesMenuCheckedState(const std::string& active_theme_name);

	void updateStatusBar();
	void updateDiagnostics();
	void runScreenshotCode();

	virtual void dragEnterEvent(QDragEnterEvent* event) override;
	virtual void dropEvent(QDropEvent* event) override;

	void handlePasteOrDropMimeData(const QMimeData* mime_data);

public:
	void showErrorNotification(const std::string& msg);
	void showInfoNotification(const std::string& msg);

	//------------------------------------------------- UIInterface -----------------------------------------------------------
	virtual void appendChatMessage(const std::string& msg) override;
	virtual void clearChatMessages() override;
	virtual bool isShowParcelsEnabled() const override;
	virtual void updateOnlineUsersList() override; // Works off world state avatars.
	virtual void handleMapTilesResultReceivedMessage(const MapTilesResultReceivedMessage& msg) override;
	virtual void showHTMLMessageBox(const std::string& title, const std::string& msg) override;
	virtual void showPlainTextMessageBox(const std::string& title, const std::string& msg) override;

	virtual void logMessage(const std::string& msg) override; // Appends to LogWindow log display.

	// Lua scripting:
	// A lua script created by the logged in user printed something
	virtual void printFromLuaScript(const std::string& msg, UID object_uid) override;
	virtual void luaErrorOccurred(const std::string& msg, UID object_uid) override;

	// UserDetailsWidget:
	virtual void setTextAsNotLoggedIn() override;
	virtual void setTextAsLoggedIn(const std::string& username) override;

	// Login/signup buttons
	virtual void loginButtonClicked() override;
	virtual void signUpButtonClicked() override;
	virtual void loggedInButtonClicked() override;

	// worldSettingsWidget:
	virtual void updateWorldSettingsControlsEditable() override;

	virtual void updateWorldSettingsUIFromWorldSettings() override;

	virtual bool diagnosticsVisible() override;
	virtual bool showObAABBsEnabled() override;
	virtual bool showPhysicsObOwnershipEnabled() override;
	virtual bool showVehiclePhysicsVisEnabled() override;
	virtual bool showPlayerPhysicsVisEnabled() override;
	virtual bool showLodChunksVisEnabled() override;

	virtual void writeTransformMembersToObject(WorldObject& ob) override;
	virtual void objectLastModifiedUpdated(const WorldObject& ob) override;
	virtual void objectModelURLUpdated(const WorldObject& ob) override;
	virtual void objectLightmapURLUpdated(const WorldObject& ob) override; // Update lightmap URL in UI if we have selected the object.

	virtual void showEditorDockWidget() override;

	// Parcel editor
	virtual void showParcelEditor() override;
	virtual void setParcelEditorForParcel(const Parcel& parcel) override;
	virtual void setParcelEditorEnabled(bool enabled) override;
	virtual void setParcelEditorPermissions(bool can_edit_basic_fields, bool can_edit_owner_and_geometry, bool can_edit_member_lists) override;

	// Object editor
	virtual void showObjectEditor() override;
	virtual void setObjectEditorEnabled(bool enabled) override;
	virtual void setObjectEditorControlsEditable(bool editable) override;
	virtual void setObjectEditorFromOb(const WorldObject& ob, int selected_mat_index, bool ob_in_editing_users_world) override; // 
	virtual int getSelectedMatIndex() override; //
	virtual void objectEditorToObject(WorldObject& ob) override;
	virtual void objectEditorObjectPickedUp() override;
	virtual void objectEditorObjectDropped() override;
	virtual bool snapToGridCheckBoxChecked() override;
	virtual double gridSpacing() override;
	virtual bool posAndRot3DControlsEnabled() override;
	virtual void setUIForSelectedObject() override; // Enable/disable delete object action etc..
	virtual void startObEditorTimerIfNotActive() override;
	virtual void startLightmapFlagTimer() override;

	virtual void showAvatarSettings() override;

	virtual void setCamRotationOnMouseDragEnabled(bool enabled) override;
	virtual bool isCursorHidden() override;
	virtual void hideCursor() override;

	virtual void setKeyboardCameraMoveEnabled(bool enabled) override; // 
	virtual bool isKeyboardCameraMoveEnabled() override;

	virtual bool hasFocus() override;

	virtual void setHelpInfoLabelToDefaultText() override;
	virtual void setHelpInfoLabel(const std::string& text) override;

	
	virtual void enableThirdPersonCamera() override;
	virtual void toggleFlyMode() override;
	virtual void toggleThirdPersonCameraMode() override;
	virtual void enableThirdPersonCameraIfNotAlreadyEnabled() override;
	virtual void enableFirstPersonCamera() override;

	virtual void openURL(const std::string& URL) override;

	virtual Vec2i getMouseCursorWidgetPos() override;

	// Credential manager
	virtual std::string getUsernameForDomain(const std::string& domain) override; // Returns empty string if no stored username for domain
	virtual std::string getDecryptedPasswordForDomain(const std::string& domain) override; // Returns empty string if no stored password for domain

	virtual bool inScreenshotTakingMode() override;
	virtual void takeScreenshot() override;
	virtual void showScreenshots() override;

	virtual void setGLWidgetContextAsCurrent() override;

	virtual Vec2i getGlWidgetPosInGlobalSpace() override; // Get top left of the GLWidget in global screen coordinates.

	virtual void webViewDataLinkHovered(const std::string& text) override;

	// Gamepad
	virtual bool gamepadAttached() override;
	virtual float gamepadButtonL2() override;
	virtual float gamepadButtonR2() override;
	virtual float gamepadAxisLeftX() override;
	virtual float gamepadAxisLeftY() override;
	virtual float gamepadAxisRightX() override;
	virtual float gamepadAxisRightY() override;


	// OpenGL
	virtual bool supportsSharedGLContexts() const override;
	virtual void* makeNewSharedGLContext()  override;
	virtual void makeGLContextCurrent(void* context) override;

	virtual void* getID3D11Device() const override;

	// File selection
	virtual std::string showOpenFileDialog(const std::string& caption, const std::vector<FileTypeFilter>& file_type_filters, const std::string& settings_key) override; // Returns path to file selected or empty string if cancelled.

	// Webcam (Qt only)
	virtual void setWebcamWindowVisible(bool visible) override;
	//------------------------------------------------- End UIInterface -----------------------------------------------------------

public:

	std::string base_dir_path;
	std::string appdata_path;
private:
	ArgumentParser parsed_args;

	Timer total_timer;

public:
	Ui::MainWindow* ui;

	SocketBufferOutStream scratch_packet;
	
	std::string screenshot_output_path;
	bool run_as_screenshot_slave;
	Reference<MySocket> screenshot_command_socket;
	Reference<MySocket> screenshot_command_listener;
	bool taking_map_screenshot;
	bool test_screenshot_taking;
	int screenshot_highlight_parcel_id;
	int screenshot_width_px;
	float screenshot_ortho_sensor_width_m;
	Vec3d screenshot_campos;
	Vec3d screenshot_camangles;
	bool screenshot_target_worldname_set;
	std::string screenshot_target_worldname;

	bool taking_gear_screenshot;
	bool screenshot_gear_model_load_started;
	Reference<GearItem> screenshot_gear_item;
	Reference<WorldObject> screenshot_gear_world_ob;
	Timer screenshot_loading_timer;

	Timer time_since_last_screenshot;
	Timer time_since_last_waiting_msg;

	
	QSettings* settings;
	Reference<QSettingsStore> settings_store;

	struct ChatPrivateDialogState
	{
		QString peer_key;
		QString peer_display_name;
		QString last_message;
		QDateTime last_time;
		int unread_count = 0;
		UID peer_uid = UID::invalidUID();
	};

	struct ChatGroupState
	{
		QString id;
		QString name;
		QString description;
		QStringList members;
		bool notifications_enabled = true;
		bool invite_only = true;
	};

	UserDetailsWidget* user_details;
	URLWidget* url_widget;
	UpdateManager* update_manager;
	QDialog* chat_emoji_popup;
	QTabWidget* chat_emoji_tab_widget;
	QComboBox* chat_user_combo;
	QToolButton* chat_private_button;
	QToolButton* chat_goto_user_button;
	QTabBar* chat_tabs_bar;
	QVBoxLayout* chat_users_list_layout;
	QLineEdit* chat_player_search_edit;
	QLabel* chat_online_count_label;
	QScrollArea* chat_messages_scroll_area;
	QVBoxLayout* chat_messages_list_layout;
	QWidget* chat_users_panel;
	QWidget* chat_main_panel;
	QWidget* chat_input_panel;
	QSplitter* chat_body_splitter;
	QWidget* chat_messages_page;
	QWidget* chat_groups_page;
	QVBoxLayout* chat_groups_list_layout;
	QWidget* chat_settings_page;
	bool chat_notifications_enabled;
	bool chat_show_timestamps;
	bool chat_compact_message_view;
	bool chat_network_private_messages_enabled;
	bool chat_showing_private_messages;
	bool chat_private_conversation_open;
	bool chat_switching_to_private_conversation;
	int chat_message_counter;
	int chat_unread_count;
	int chat_private_unread_count;
	QStringList chat_pending_attachment_echoes;
	bool chat_loading_history;
	QJsonArray chat_history_entries;
	QMap<QString, ChatPrivateDialogState> chat_private_dialogs;
	QMap<QString, ChatGroupState> chat_groups;
	UID chat_private_recipient_uid;
	std::string chat_private_recipient_name;
	QActionGroup* theme_action_group;
	QActionGroup* language_action_group;
	RuntimeTranslation::RuntimeTranslator* runtime_translator;
	RuntimeTranslation::UILanguage current_ui_language;

	double last_timerEvent_CPU_work_elapsed;
	double last_updateGL_time;
	double last_xr_companion_update_time;
	bool default_qt_style_name_set;
	std::string default_qt_style_name;
private:
	bool need_help_info_dock_widget_position; // We may need to position the Help info dock widget to the bottom right of the GL view.
	// But we need to wait until the gl view has been resized before we do this, so set this flag to do in a timer event.
	
	QTimer* update_ob_editor_transform_timer;
	QTimer* lightmap_flag_timer;
	int main_timer_id; // ID of Main QT timer.

public:
#if defined(_WIN32)
	ComObHandle<ID3D11Device> d3d_device;
	ComObHandle<IMFDXGIDeviceManager> device_manager;
#endif

	LogWindow* log_window;

	bool in_CEF_message_loop;
	bool should_close;
	bool closing; // Timer events keep firing after closeEvent(), annoyingly, so keep track of if we are closing the Window, in which case we can early-out of timerEvent().
	bool running_destructor;

	Reference<OpenGLEngine> opengl_engine;
	
	GUIClient gui_client;

	CredentialManager credential_manager;

	glare::TaskManager* main_task_manager;
	glare::TaskManager* high_priority_task_manager;

	Reference<glare::Allocator> main_mem_allocator;

	//struct _SDL_GameController* game_controller;
	QByteArray pre_fullscreen_window_state;

	Reference<RenderStatsWidget> CPU_render_stats_widget;
	Reference<RenderStatsWidget> GPU_render_stats_widget;

	MiniDmpSender* minidump_sender;
	WebcamWindow* webcam_window;

	QDockWidget* avatar_dock_widget;
	AvatarSettingsWidget* avatar_settings_widget;
	QDockWidget* map_dock_widget;
	QWidget* map_dock_map_widget;
	ScientificObjectEditor* scientific_object_editor;
	QAction* action_add_scientific_object;
	enum ActiveEditorKind
	{
		ActiveEditor_Object,
		ActiveEditor_Scientific
	};
	ActiveEditorKind active_editor_kind;
};
