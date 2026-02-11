/*=====================================================================
SDLUIInterface.h
----------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#pragma once


#include "UIInterface.h"
#include <settings/SettingsStore.h>
#include <SDL.h>
#include <opengl/ui/GLUITextButton.h>
#include <opengl/ui/GLUIButton.h>
#include <opengl/ui/GLUICallbackHandler.h>
#include <opengl/ui/GLUITextView.h>
#include <opengl/ui/GLUIInertWidget.h>
struct SDL_Window;
class GUIClient;
class TextRendererFontFace;
class OpenGLEngine;
class OpenGLScene;
template <class T> class Reference;
class OpenGLTexture;


class SDLUIInterface final : public UIInterface, public GLUICallbackHandler
{
public:
	virtual void appendChatMessage(const std::string& msg) override;
	virtual void clearChatMessages() override;
	virtual bool isShowParcelsEnabled() const override;
	virtual void updateOnlineUsersList() override; // Works off world state avatars.
	virtual void showHTMLMessageBox(const std::string& title, const std::string& msg) override;
	virtual void showPlainTextMessageBox(const std::string& title, const std::string& msg) override;

	virtual void logMessage(const std::string& msg) override; // Appends to LogWindow log display.

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

	virtual void showAvatarSettings() override; // Show avatar settings dialog.
	virtual bool isAvatarSettingsDialogVisible() const override { return avatar_settings_dialog_visible; }
	void hideAvatarSettings();
	bool handleAvatarSettingsDialogMousePress(const MouseEvent& e); // Check if click is outside dialog

	virtual void setCamRotationOnMouseDragEnabled(bool enabled) override;
	virtual bool isCursorHidden() override;
	virtual void hideCursor() override;

	virtual void setKeyboardCameraMoveEnabled(bool enabled) override; // Do we want WASD keys etc. to move the camera?  We don't want this while e.g. we enter text into a webview.
	virtual bool isKeyboardCameraMoveEnabled() override;

	virtual bool hasFocus() override;

	virtual void setHelpInfoLabelToDefaultText() override;
	virtual void setHelpInfoLabel(const std::string& text) override;

	
	virtual void toggleFlyMode() override;
	virtual void enableThirdPersonCamera() override;
	virtual void toggleThirdPersonCameraMode() override;
	virtual void enableThirdPersonCameraIfNotAlreadyEnabled() override;
	virtual void enableFirstPersonCamera() override;

	virtual void openURL(const std::string& URL) override;

	virtual Vec2i getMouseCursorWidgetPos() override; // Get mouse cursor position, relative to gl widget.

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
	virtual void* makeNewSharedGLContext() override;
	virtual void makeGLContextCurrent(void* context) override;

	virtual	void* getID3D11Device() const override;


	SDL_Window* window;
	SDL_GLContext gl_context;
	std::string logged_in_username;
	GUIClient* gui_client;
	//SDL_Joystick* joystick;
	SDL_GameController* game_controller;
	std::string appdata_path;

	Reference<SettingsStore> settings_store;
	void* d3d11_device;
	
	bool show_parcels_enabled;
	
	// About dialog
	void showAboutDialog();
	void hideAboutDialog();
	bool isAboutDialogVisible() const { return about_dialog_visible; }
	bool handleAboutDialogMousePress(const MouseEvent& e); // Check if click is outside dialog
	
	// GLUICallbackHandler
	virtual void eventOccurred(GLUICallbackEvent& event) override;
	
private:
	GLUIInertWidgetRef about_dialog_background_panel;
	GLUITextViewRef about_dialog_beta_text;
	GLUITextViewRef about_dialog_title_text;
	GLUITextViewRef about_dialog_subtitle_text;
	GLUITextViewRef about_dialog_links_label_text;
	GLUITextButtonRef about_dialog_telegram_channel_button;
	GLUITextButtonRef about_dialog_site_button;
	GLUITextButtonRef about_dialog_github_button;
	GLUITextButtonRef about_dialog_vk_button1;
	GLUITextViewRef about_dialog_author_label_text;
	GLUITextViewRef about_dialog_author_name_text;
	GLUITextButtonRef about_dialog_vk_button2;
	GLUITextButtonRef about_dialog_telegram_button;
	GLUITextButtonRef about_dialog_close_button;
	bool about_dialog_visible = false;
	
	// Avatar Settings dialog
	GLUIInertWidgetRef avatar_settings_dialog_background_panel;
	GLUITextViewRef avatar_settings_dialog_title_text;
	GLUIButtonRef avatar_settings_readyplayerme_button;
	GLUIButtonRef avatar_settings_avaturnme_button;
	GLUITextButtonRef avatar_settings_close_button;
	bool avatar_settings_dialog_visible = false;

	//Reference<TextRendererFontFace> font;
};
