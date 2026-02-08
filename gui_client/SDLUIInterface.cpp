/*=====================================================================
SDLUIInterface.cpp
------------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#include "SDLUIInterface.h"


#include "GUIClient.h"
#include <utils/ConPrint.h>
#include <utils/Base64.h>
#include <utils/BufferOutStream.h>
#include <utils/FileUtils.h>
#include <graphics/ImageMap.h>
#include <graphics/TextRenderer.h>
#include <graphics/jpegdecoder.h>
#include <webserver/Escaping.h>
#include <SDL.h>
#include <iostream>
#if EMSCRIPTEN
#include <emscripten.h>
#endif
#include <opengl/ui/GLUI.h>
#include <opengl/ui/GLUIButton.h>
#include <opengl/ui/GLUITextButton.h>
#include <opengl/ui/GLUITextView.h>
#include <opengl/ui/GLUIInertWidget.h>
#include <graphics/SRGBUtils.h>
#include <maths/mathstypes.h>
#include <AESEncryption.h>
#include <utils/StringUtils.h>
#include <utils/Exception.h>
#include <cstring>




#if EMSCRIPTEN

// Define openURLInNewBrowserTab(const char* URL) function
EM_JS(void, openURLInNewBrowserTab, (const char* URL), {
	window.open(UTF8ToString(URL), "mozillaTab"); // See https://developer.mozilla.org/en-US/docs/Web/API/Window/open
});

// Define navigateToURL(const char* URL) function
EM_JS(void, navigateToURL, (const char* URL), {
	window.location = UTF8ToString(URL);
});


EM_JS(void, downloadFile, (const char* dataUrl_cstr, const char* fileName_cstr), {

	let dataUrl  = UTF8ToString(dataUrl_cstr);
	let fileName = UTF8ToString(fileName_cstr);

	// Create a temporary anchor element
	const link = document.createElement('a');

	// Set the href to the data URL
	link.href = dataUrl;

	// Set the download attribute with filename
	link.download = fileName;

	// Append to body (necessary for Firefox)
	document.body.appendChild(link);

	// Simulate click
	link.click();
	
	// Clean up
	document.body.removeChild(link);
});

#endif


void SDLUIInterface::appendChatMessage(const std::string& msg)
{
}

void SDLUIInterface::clearChatMessages()
{
}

bool SDLUIInterface::isShowParcelsEnabled() const
{
	return show_parcels_enabled;
}

void SDLUIInterface::updateOnlineUsersList()
{
}

void SDLUIInterface::showHTMLMessageBox(const std::string& title, const std::string& msg)
{
	SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, title.c_str(), msg.c_str(), window);
}

void SDLUIInterface::showPlainTextMessageBox(const std::string& title, const std::string& msg)
{
	SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, title.c_str(), msg.c_str(), window);
}


// Print without flushing.
static void doPrint(const std::string& s)
{
	std::cout << s << "\n";
}


void SDLUIInterface::logMessage(const std::string& msg)
{
	doPrint("Log: " + msg); // TEMP
}

void SDLUIInterface::setTextAsNotLoggedIn()
{
	logged_in_username = "";
}

void SDLUIInterface::setTextAsLoggedIn(const std::string& username)
{
	logged_in_username = username;
}

void SDLUIInterface::loginButtonClicked()
{
#if EMSCRIPTEN
	const std::string return_url = gui_client->getCurrentWebClientURLPath();
	const std::string url = "/login?return=" + web::Escaping::URLEscape(return_url);
	navigateToURL(url.c_str());
#endif
}

void SDLUIInterface::signUpButtonClicked()
{
#if EMSCRIPTEN
	const std::string return_url = gui_client->getCurrentWebClientURLPath();
	const std::string url = "/signup?return=" + web::Escaping::URLEscape(return_url);
	navigateToURL(url.c_str());
#endif
}

void SDLUIInterface::loggedInButtonClicked()
{
#if EMSCRIPTEN
	navigateToURL("/account");
#endif
}

void SDLUIInterface::updateWorldSettingsControlsEditable()
{
}

void SDLUIInterface::updateWorldSettingsUIFromWorldSettings()
{
}

bool SDLUIInterface::diagnosticsVisible()
{
	return false;
}

bool SDLUIInterface::showObAABBsEnabled()
{
	return false;
}

bool SDLUIInterface::showPhysicsObOwnershipEnabled()
{
	return false;
}

bool SDLUIInterface::showVehiclePhysicsVisEnabled()
{
	return false;
}

bool SDLUIInterface::showPlayerPhysicsVisEnabled()
{
	return false;
}

bool SDLUIInterface::showLodChunksVisEnabled()
{
	return false;
}

void SDLUIInterface::writeTransformMembersToObject(WorldObject& ob)
{
}

void SDLUIInterface::objectLastModifiedUpdated(const WorldObject& ob)
{
}

void SDLUIInterface::objectModelURLUpdated(const WorldObject& ob)
{
}

void SDLUIInterface::objectLightmapURLUpdated(const WorldObject& ob)
{
}

void SDLUIInterface::showEditorDockWidget()
{
}

void SDLUIInterface::showParcelEditor()
{
}

void SDLUIInterface::setParcelEditorForParcel(const Parcel& parcel)
{
}

void SDLUIInterface::setParcelEditorEnabled(bool enabled)
{
}

void SDLUIInterface::showObjectEditor()
{
}

void SDLUIInterface::setObjectEditorEnabled(bool enabled)
{
}

void SDLUIInterface::setObjectEditorControlsEditable(bool editable)
{
}

void SDLUIInterface::setObjectEditorFromOb(const WorldObject& ob, int selected_mat_index, bool ob_in_editing_users_world)
{
}

int SDLUIInterface::getSelectedMatIndex()
{
	return 0;
}

void SDLUIInterface::objectEditorToObject(WorldObject& ob)
{
}

void SDLUIInterface::objectEditorObjectPickedUp()
{
}

void SDLUIInterface::objectEditorObjectDropped()
{
}

bool SDLUIInterface::snapToGridCheckBoxChecked()
{
	return false;
}

double SDLUIInterface::gridSpacing()
{
	return 1.0;
}

bool SDLUIInterface::posAndRot3DControlsEnabled()
{
	return true;
}

void SDLUIInterface::setUIForSelectedObject()
{
}

void SDLUIInterface::startObEditorTimerIfNotActive()
{
}

void SDLUIInterface::startLightmapFlagTimer()
{
}

void SDLUIInterface::showAvatarSettings() // Show avatar settings dialog.
{
#if EMSCRIPTEN
	EM_ASM({
		showAvatarSettingsWidget();
	});
#else
	// Create SDL avatar settings dialog with two icon buttons
	GLUIRef gl_ui = gui_client->gl_ui;
	Reference<OpenGLEngine> opengl_engine = gui_client->opengl_engine;
	
	// Dialog dimensions
	const float dialog_w = gl_ui->getUIWidthForDevIndepPixelWidth(400);
	const float dialog_h = gl_ui->getUIWidthForDevIndepPixelWidth(300);
	// Center of screen in GLUI coordinates: (0, 0) is center, not (0.5, 0.5)
	const float center_x = 0.f;
	const float center_y = 0.f;
	const Vec2f dialog_min = Vec2f(center_x - dialog_w / 2.f, center_y - dialog_h / 2.f);
	const Vec2f dialog_max = Vec2f(center_x + dialog_w / 2.f, center_y + dialog_h / 2.f);
	
	// Create background panel - use same style as About dialog (light gray)
	if(avatar_settings_dialog_background_panel.isNull())
	{
		GLUIInertWidget::CreateArgs panel_args;
		panel_args.background_colour = Colour3f(0.7f); // Same light gray as About dialog
		panel_args.background_alpha = 0.8f; // Same alpha as About dialog
		panel_args.z = -0.488f; // Background behind text/button, same as About dialog
		avatar_settings_dialog_background_panel = new GLUIInertWidget(*gl_ui, opengl_engine, panel_args);
		avatar_settings_dialog_background_panel->setPosAndDims(dialog_min, Vec2f(dialog_w, dialog_h));
		gl_ui->addWidget(avatar_settings_dialog_background_panel);
	}
	else
	{
		avatar_settings_dialog_background_panel->setPosAndDims(dialog_min, Vec2f(dialog_w, dialog_h));
	}
	
	// Text args for title
	GLUITextView::CreateArgs text_args;
	text_args.font_size_px = 20;
	text_args.padding_px = 5;
	text_args.max_width = dialog_w * 2.0f;
	text_args.z = -0.5f;
	text_args.line_0_x_offset = 0.f;
	
	// Title: "Настройки аватара"
	const float title_y = dialog_max.y - gl_ui->getUIWidthForDevIndepPixelWidth(40);
	const float title_x = dialog_min.x + gl_ui->getUIWidthForDevIndepPixelWidth(20);
	
	if(avatar_settings_dialog_title_text.isNull())
	{
		avatar_settings_dialog_title_text = new GLUITextView(*gl_ui, opengl_engine, "Настройки аватара", Vec2f(title_x, title_y), text_args);
		gl_ui->addWidget(avatar_settings_dialog_title_text);
	}
	else
	{
		avatar_settings_dialog_title_text->setPos(Vec2f(title_x, title_y));
	}
	avatar_settings_dialog_title_text->setVisible(true);
	
	// Button dimensions
	const float button_size = gl_ui->getUIWidthForDevIndepPixelWidth(120);
	const float button_spacing = gl_ui->getUIWidthForDevIndepPixelWidth(30);
	const float buttons_y = center_y - gl_ui->getUIWidthForDevIndepPixelWidth(20);
	const float total_buttons_width = button_size * 2 + button_spacing;
	const float buttons_start_x = center_x - total_buttons_width / 2.f;
	
	// ReadyPlayerMe button - set z to be in front of background panel
	const float readyplayerme_x = buttons_start_x;
	if(avatar_settings_readyplayerme_button.isNull())
	{
		GLUIButton::CreateArgs button_args;
		avatar_settings_readyplayerme_button = new GLUIButton(*gl_ui, opengl_engine, gui_client->resources_dir_path + "/buttons/ReadyPlayerMe.png", 
			Vec2f(readyplayerme_x, buttons_y), Vec2f(button_size, button_size), button_args);
		avatar_settings_readyplayerme_button->handler = this;
		avatar_settings_readyplayerme_button->setZ(-0.5f); // In front of background panel (z = -0.488)
		gl_ui->addWidget(avatar_settings_readyplayerme_button);
	}
	else
	{
		avatar_settings_readyplayerme_button->setPosAndDims(Vec2f(readyplayerme_x, buttons_y), Vec2f(button_size, button_size));
		avatar_settings_readyplayerme_button->setZ(-0.5f); // Ensure z is correct
	}
	avatar_settings_readyplayerme_button->setVisible(true);
	
	// AvaturnMe button - set z to be in front of background panel
	const float avaturnme_x = readyplayerme_x + button_size + button_spacing;
	if(avatar_settings_avaturnme_button.isNull())
	{
		GLUIButton::CreateArgs button_args;
		avatar_settings_avaturnme_button = new GLUIButton(*gl_ui, opengl_engine, gui_client->resources_dir_path + "/buttons/avaturnme.png", 
			Vec2f(avaturnme_x, buttons_y), Vec2f(button_size, button_size), button_args);
		avatar_settings_avaturnme_button->handler = this;
		avatar_settings_avaturnme_button->setZ(-0.5f); // In front of background panel (z = -0.488)
		gl_ui->addWidget(avatar_settings_avaturnme_button);
	}
	else
	{
		avatar_settings_avaturnme_button->setPosAndDims(Vec2f(avaturnme_x, buttons_y), Vec2f(button_size, button_size));
		avatar_settings_avaturnme_button->setZ(-0.5f); // Ensure z is correct
	}
	avatar_settings_avaturnme_button->setVisible(true);
	
	// Close button - use same style as About dialog buttons
	GLUITextButton::CreateArgs close_button_args;
	close_button_args.font_size_px = 18;
	close_button_args.z = -0.5f;
	close_button_args.background_colour = toLinearSRGB(Colour3f(1.f)); // White background (default)
	close_button_args.text_colour = toLinearSRGB(Colour3f(0.1f)); // Dark text (default)
	close_button_args.mouseover_background_colour = toLinearSRGB(Colour3f(0.8f)); // Light gray on hover (default)
	close_button_args.mouseover_text_colour = toLinearSRGB(Colour3f(0.1f)); // Dark text on hover (default)
	
	const float close_button_w = gl_ui->getUIWidthForDevIndepPixelWidth(100);
	const float close_button_h = gl_ui->getUIWidthForDevIndepPixelWidth(30);
	const float close_button_y = dialog_min.y + gl_ui->getUIWidthForDevIndepPixelWidth(20);
	const float close_button_x = dialog_min.x + (dialog_w - close_button_w) / 2.f;
	
	if(avatar_settings_close_button.isNull())
	{
		close_button_args.tooltip = "Закрыть";
		avatar_settings_close_button = new GLUITextButton(*gl_ui, opengl_engine, "Закрыть", Vec2f(close_button_x, close_button_y), close_button_args);
		avatar_settings_close_button->handler = this;
		gl_ui->addWidget(avatar_settings_close_button);
	}
	else
	{
		avatar_settings_close_button->setPos(Vec2f(close_button_x, close_button_y));
	}
	avatar_settings_close_button->setVisible(true);
	
	// Show all widgets
	avatar_settings_dialog_background_panel->setVisible(true);
	avatar_settings_dialog_visible = true;
#endif
}

void SDLUIInterface::hideAvatarSettings()
{
	if(avatar_settings_dialog_background_panel.nonNull())
		avatar_settings_dialog_background_panel->setVisible(false);
	if(avatar_settings_dialog_title_text.nonNull())
		avatar_settings_dialog_title_text->setVisible(false);
	if(avatar_settings_readyplayerme_button.nonNull())
		avatar_settings_readyplayerme_button->setVisible(false);
	if(avatar_settings_avaturnme_button.nonNull())
		avatar_settings_avaturnme_button->setVisible(false);
	if(avatar_settings_close_button.nonNull())
		avatar_settings_close_button->setVisible(false);
	avatar_settings_dialog_visible = false;
}


bool SDLUIInterface::handleAvatarSettingsDialogMousePress(const MouseEvent& e)
{
	if(!avatar_settings_dialog_visible)
		return false;
	
	// Convert mouse coordinates to UI coordinates
	const Vec2f coords = gui_client->gl_ui->UICoordsForOpenGLCoords(e.gl_coords);
	
	// Check if click is outside the dialog panel
	if(avatar_settings_dialog_background_panel.nonNull())
	{
		if(!avatar_settings_dialog_background_panel->rect.inOpenRectangle(coords))
		{
			// Click is outside dialog - close it
			hideAvatarSettings();
			return true; // Event handled
		}
	}
	
	return false; // Event not handled (click was inside dialog)
}


void SDLUIInterface::setCamRotationOnMouseDragEnabled(bool enabled)
{

}

bool SDLUIInterface::isCursorHidden()
{
	return false;
}

void SDLUIInterface::hideCursor()
{
}

void SDLUIInterface::setKeyboardCameraMoveEnabled(bool enabled)
{
}

bool SDLUIInterface::isKeyboardCameraMoveEnabled()
{
	return true;
}

bool SDLUIInterface::hasFocus()
{
	return (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) != 0; // NOTE: maybe want SDL_WINDOW_MOUSE_FOCUS?
}

void SDLUIInterface::setHelpInfoLabelToDefaultText()
{
}

void SDLUIInterface::setHelpInfoLabel(const std::string& text)
{
}

void SDLUIInterface::toggleFlyMode()
{
	gui_client->player_physics.setFlyModeEnabled(!gui_client->player_physics.flyModeEnabled());
}

void SDLUIInterface::enableThirdPersonCamera()
{
	gui_client->thirdPersonCameraToggled(true);
}

void SDLUIInterface::toggleThirdPersonCameraMode()
{
	gui_client->thirdPersonCameraToggled(!gui_client->cam_controller.thirdPersonEnabled());
}

void SDLUIInterface::enableThirdPersonCameraIfNotAlreadyEnabled()
{
	if(!gui_client->cam_controller.thirdPersonEnabled())
		gui_client->thirdPersonCameraToggled(true);
}

void SDLUIInterface::enableFirstPersonCamera()
{
	gui_client->thirdPersonCameraToggled(false);
}


void SDLUIInterface::openURL(const std::string& URL)
{
	conPrint("SDLUIInterface::openURL: URL: " + URL);
#if EMSCRIPTEN
	openURLInNewBrowserTab(URL.c_str());
#else
	SDL_OpenURL(URL.c_str());
#endif
}

Vec2i SDLUIInterface::getMouseCursorWidgetPos() // Get mouse cursor position, relative to gl widget.
{
	Vec2i p(0, 0);
	SDL_GetMouseState(&p.x, &p.y);
	return p;
}

std::string SDLUIInterface::getUsernameForDomain(const std::string& domain)
{
	if(!settings_store.nonNull())
		return std::string();
	
	// Try to find credentials for this domain
	// Credentials are stored as: credentials/0/domain, credentials/0/username, credentials/0/encrypted_password, etc.
	// Or as: credentials/domain/username, credentials/domain/encrypted_password (simpler format)
	
	// First try the simpler format: credentials/domain/username
	std::string username = settings_store->getStringValue("credentials/" + domain + "/username", "");
	if(!username.empty())
		return username;
	
	// Try the array format: credentials/0/domain, credentials/0/username, etc.
	for(int i = 0; i < 100; ++i) // Check up to 100 credentials
	{
		std::string cred_domain = settings_store->getStringValue("credentials/" + std::to_string(i) + "/domain", "");
		if(cred_domain.empty())
			break; // No more credentials
		
		if(cred_domain == domain)
		{
			return settings_store->getStringValue("credentials/" + std::to_string(i) + "/username", "");
		}
	}
	
	return std::string();
}

std::string SDLUIInterface::getDecryptedPasswordForDomain(const std::string& domain)
{
	if(!settings_store.nonNull())
		return std::string();
	
	// Try to find encrypted password for this domain
	std::string encrypted_password;
	
	// First try the simpler format: credentials/domain/encrypted_password
	encrypted_password = settings_store->getStringValue("credentials/" + domain + "/encrypted_password", "");
	
	// If not found, try the array format
	if(encrypted_password.empty())
	{
		for(int i = 0; i < 100; ++i) // Check up to 100 credentials
		{
			std::string cred_domain = settings_store->getStringValue("credentials/" + std::to_string(i) + "/domain", "");
			if(cred_domain.empty())
				break; // No more credentials
			
			if(cred_domain == domain)
			{
				encrypted_password = settings_store->getStringValue("credentials/" + std::to_string(i) + "/encrypted_password", "");
				break;
			}
		}
	}
	
	if(encrypted_password.empty())
		return std::string();
	
	// Decrypt the password using the same logic as CredentialManager
	try
	{
		// Decode base64 to raw bytes
		std::vector<unsigned char> cyphertex_binary;
		Base64::decode(encrypted_password, cyphertex_binary);
		
		// AES decrypt
		const std::string key = "RHJKEF_ZAepxYxYkrL3c6rWD";
		const std::string salt = "P6A3uZ4P";
		AESEncryption aes(key, salt);
		std::vector<unsigned char> plaintext_v = aes.decrypt(cyphertex_binary);
		
		// Convert to std::string
		std::string plaintext(plaintext_v.size(), '\0');
		if(!plaintext_v.empty())
			std::memcpy(&plaintext[0], plaintext_v.data(), plaintext_v.size());
		return plaintext;
	}
	catch(glare::Exception&)
	{
		return "";
	}
}

bool SDLUIInterface::inScreenshotTakingMode()
{
	return false;
}

void SDLUIInterface::takeScreenshot()
{
	gui_client->opengl_engine->getCurrentScene()->draw_overlay_objects = false; // Hide UI

	try
	{
		if(SDL_GL_MakeCurrent(window, gl_context) != 0)
			conPrint("SDL_GL_MakeCurrent failed.");

		ImageMapUInt8Ref map = gui_client->opengl_engine->drawToBufferAndReturnImageMap();
		if(map->hasAlphaChannel())
			map = map->extract3ChannelImage();

		// Write the JPEG to an in-mem buffer, since we will need it as a data URL for the web client.
		BufferOutStream buffer;
		JPEGDecoder::saveToStream(map, JPEGDecoder::SaveOptions(), buffer);

		const std::string filename = "substrata_screenshot_" + toString((uint64)Clock::getSecsSince1970()) + ".jpg";

#if EMSCRIPTEN
		std::string encoded;
		Base64::encode(buffer.buf.data(), buffer.buf.size(), encoded);
		const std::string image_data_url = "data:image/jpeg;base64," + encoded;

		downloadFile(image_data_url.c_str(), filename.c_str()); // Make the browser trigger a file download for the image.

		const std::string path = "/tmp/" + filename;
#else
		const std::string path = this->appdata_path + "/screenshots/" + filename;
		FileUtils::createDirIfDoesNotExist(FileUtils::getDirectory(path));
#endif

		// Write it to disk (so can be used for photo upload)
		FileUtils::writeEntireFile(path, (const char*)buffer.buf.data(), buffer.buf.size());
		settings_store->setStringValue("photo/last_saved_photo_path", path);

#if !EMSCRIPTEN
		gui_client->showInfoNotification("Saved screenshot to " + path);
#endif
	}
	catch(glare::Exception& e)
	{
#if EMSCRIPTEN
		gui_client->showErrorNotification("Error saving photo: " + e.what());
#else
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Error saving photo", e.what().c_str(), window);
#endif
	}

	gui_client->opengl_engine->getCurrentScene()->draw_overlay_objects = true; // Unhide UI.
}


void SDLUIInterface::showScreenshots()
{
#if EMSCRIPTEN
#else
	SDL_OpenURL(("file:///" + this->appdata_path + "/screenshots/").c_str());
#endif
}


void SDLUIInterface::setGLWidgetContextAsCurrent()
{
	if(SDL_GL_MakeCurrent(window, gl_context) != 0)
		conPrint("SDL_GL_MakeCurrent failed.");
}

Vec2i SDLUIInterface::getGlWidgetPosInGlobalSpace()
{
	return Vec2i(0, 0);
}

void SDLUIInterface::webViewDataLinkHovered(const std::string& text)
{
}

bool SDLUIInterface::gamepadAttached()
{
	//return joystick != nullptr;
	return game_controller != nullptr;
}


static float removeDeadZone(float x)
{
	if(std::fabs(x) < (8000.f / 32768.f))
		return 0.f;
	else
		return x;
}

#if 0

float SDLUIInterface::gamepadButtonL2()
{
	const Sint16 val = SDL_JoystickGetAxis(joystick, /*axis=*/4);
	return (float)val / SDL_JOYSTICK_AXIS_MAX;
}

float SDLUIInterface::gamepadButtonR2()
{
	const Sint16 val = SDL_JoystickGetAxis(joystick, /*axis=*/5);
	return (float)val / SDL_JOYSTICK_AXIS_MAX;
}

float SDLUIInterface::gamepadAxisLeftX()
{
	const Sint16 val = SDL_JoystickGetAxis(joystick, /*axis=*/0);
	return removeDeadZone(val / 32768.f); // ((float)val - SDL_JOYSTICK_AXIS_MIN) / (SDL_JOYSTICK_AXIS_MAX - SDL_JOYSTICK_AXIS_MIN);
}

float SDLUIInterface::gamepadAxisLeftY()
{
	const Sint16 val = SDL_JoystickGetAxis(joystick, /*axis=*/1);
	return removeDeadZone(val / 32768.f); // ((float)val - SDL_JOYSTICK_AXIS_MIN) / (SDL_JOYSTICK_AXIS_MAX - SDL_JOYSTICK_AXIS_MIN);
}

float SDLUIInterface::gamepadAxisRightX()
{
	const Sint16 val = SDL_JoystickGetAxis(joystick, /*axis=*/2);
	return removeDeadZone(val / 32768.f); // ((float)val - SDL_JOYSTICK_AXIS_MIN) / (SDL_JOYSTICK_AXIS_MAX - SDL_JOYSTICK_AXIS_MIN);
}

float SDLUIInterface::gamepadAxisRightY()
{
	const Sint16 val = SDL_JoystickGetAxis(joystick, /*axis=*/3);
	return removeDeadZone(val / 32768.f); // ((float)val - SDL_JOYSTICK_AXIS_MIN) / (SDL_JOYSTICK_AXIS_MAX - SDL_JOYSTICK_AXIS_MIN);
}

#else

float SDLUIInterface::gamepadButtonL2()
{
	const Sint16 val = SDL_GameControllerGetAxis(game_controller, /*axis=*/SDL_CONTROLLER_AXIS_TRIGGERLEFT);
	return (float)val / SDL_JOYSTICK_AXIS_MAX;
}

float SDLUIInterface::gamepadButtonR2()
{
	const Sint16 val = SDL_GameControllerGetAxis(game_controller, /*axis=*/SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
	return (float)val / SDL_JOYSTICK_AXIS_MAX;
}

// NOTE: seems to be an issue in SDL that the left axis maps to the left keypad instead of left stick on a Logitech F310 gamepad.
float SDLUIInterface::gamepadAxisLeftX()
{
	const Sint16 val = SDL_GameControllerGetAxis(game_controller, /*axis=*/SDL_CONTROLLER_AXIS_LEFTX);
	return removeDeadZone(val / 32768.f);
}

float SDLUIInterface::gamepadAxisLeftY()
{
	const Sint16 val = SDL_GameControllerGetAxis(game_controller, /*axis=*/SDL_CONTROLLER_AXIS_LEFTY);
	return removeDeadZone(val / 32768.f);
}

float SDLUIInterface::gamepadAxisRightX()
{
	const Sint16 val = SDL_GameControllerGetAxis(game_controller, /*axis=*/SDL_CONTROLLER_AXIS_RIGHTX);
	return removeDeadZone(val / 32768.f);
}

float SDLUIInterface::gamepadAxisRightY()
{
	const Sint16 val = SDL_GameControllerGetAxis(game_controller, /*axis=*/SDL_CONTROLLER_AXIS_RIGHTY);
	return removeDeadZone(val / 32768.f);
}

#endif


bool SDLUIInterface::supportsSharedGLContexts() const
{
	return false;
}


void* SDLUIInterface::makeNewSharedGLContext()
{
	return nullptr;
}


void SDLUIInterface::makeGLContextCurrent(void* context)
{
	assert(0);
}


void* SDLUIInterface::getID3D11Device() const
{
	return d3d11_device;
}


void SDLUIInterface::showAboutDialog()
{
	if(!gui_client || !gui_client->gl_ui || !gui_client->opengl_engine)
		return;
	
	GLUIRef gl_ui = gui_client->gl_ui;
	Reference<OpenGLEngine>& opengl_engine = gui_client->opengl_engine;
	
	// Create background panel - use same style as PhotoModeUI and avatar preview (0.7f background)
	if(about_dialog_background_panel.isNull())
	{
		GLUIInertWidget::CreateArgs args;
		args.background_colour = Colour3f(0.7f); // Same light gray as PhotoModeUI and avatar preview
		args.background_alpha = 0.8f; // Same alpha as working dialogs
		args.z = -0.488f; // Background behind text/button, same as PhotoModeUI upload widget
		about_dialog_background_panel = new GLUIInertWidget(*gl_ui, opengl_engine, args);
		gl_ui->addWidget(about_dialog_background_panel);
	}
	
	// Calculate panel position (centered on screen)
	const float center_x = 0.f;
	const float min_max_y = gl_ui->getViewportMinMaxY();
	const float dialog_w = gl_ui->getUIWidthForDevIndepPixelWidth(650); // Increased from 600 to 650
	const float dialog_h = gl_ui->getUIWidthForDevIndepPixelWidth(480); // Increased from 400 to 480
	// Center vertically: min_max_y is top, -min_max_y is bottom, center is at 0
	const Vec2f dialog_min = Vec2f(center_x - dialog_w/2.f, -dialog_h/2.f);
	
	// Position background panel
	about_dialog_background_panel->setPosAndDims(dialog_min, Vec2f(dialog_w, dialog_h));
	
	// Calculate positions - start from top
	const float text_left_x = dialog_min.x + gl_ui->getUIWidthForDevIndepPixelWidth(40);
	const float line_height = gl_ui->getUIWidthForDevIndepPixelWidth(28); // Font size (18) + spacing (10)
	const float line_spacing = gl_ui->getUIWidthForDevIndepPixelWidth(15); // Space between sections
	float current_y = dialog_min.y + dialog_h - gl_ui->getUIWidthForDevIndepPixelWidth(50);
	
	// Common text args
	GLUITextView::CreateArgs text_args;
	text_args.background_colour = Colour3f(0.f);
	text_args.background_alpha = 0.f;
	text_args.text_colour = toLinearSRGB(Colour3f(0.1f));
	text_args.text_alpha = 1.0f;
	text_args.font_size_px = 18;
	text_args.padding_px = 5;
	text_args.max_width = dialog_w * 2.0f;
	text_args.z = -0.5f;
	text_args.line_0_x_offset = 0.f;
	
	// Common button args - white background with dark text (default GLUITextButton style)
	GLUITextButton::CreateArgs button_args;
	button_args.font_size_px = 18;
	button_args.z = -0.5f;
	// Use default colors: white background, dark text
	button_args.background_colour = Colour3f(1.f); // White background (default)
	button_args.text_colour = toLinearSRGB(Colour3f(0.1f)); // Dark text (default)
	button_args.mouseover_background_colour = toLinearSRGB(Colour3f(0.8f)); // Light gray on hover (default)
	button_args.mouseover_text_colour = toLinearSRGB(Colour3f(0.1f)); // Dark text on hover (default)
	
	// 0. Beta label: "Metasiberia Beta" (centered approximately)
	if(about_dialog_beta_text.isNull())
	{
		GLUITextView::CreateArgs beta_args = text_args;
		beta_args.font_size_px = 20; // Slightly larger
		// Approximate text width: "Metasiberia Beta" is about 16 characters * 12 pixels = ~192 pixels
		// Convert to UI coords and center: dialog_w/2 - text_width/2
		const float estimated_text_width = gl_ui->getUIWidthForDevIndepPixelWidth(180); // Approximate width
		const float beta_x = center_x - estimated_text_width / 2.f;
		beta_args.line_0_x_offset = 0.f; // Start from botleft position
		about_dialog_beta_text = new GLUITextView(*gl_ui, opengl_engine, "Metasiberia Beta", Vec2f(beta_x, current_y), beta_args);
		gl_ui->addWidget(about_dialog_beta_text);
	}
	else
	{
		const float estimated_text_width = gl_ui->getUIWidthForDevIndepPixelWidth(180);
		const float beta_x = center_x - estimated_text_width / 2.f;
		about_dialog_beta_text->setPos(Vec2f(beta_x, current_y));
	}
	about_dialog_beta_text->setVisible(true);
	current_y -= line_height;
	current_y -= line_spacing;
	
	// 1. Title: "Метасибирь - метавселенная из Сибири"
	if(about_dialog_title_text.isNull())
	{
		about_dialog_title_text = new GLUITextView(*gl_ui, opengl_engine, "Метасибирь - метавселенная из Сибири", Vec2f(text_left_x, current_y), text_args);
		gl_ui->addWidget(about_dialog_title_text);
	}
	else
	{
		about_dialog_title_text->setPos(Vec2f(text_left_x, current_y));
	}
	about_dialog_title_text->setVisible(true);
	current_y -= line_height;
	current_y -= line_spacing;
	
	// 2. Links label: "Ссылки:"
	if(about_dialog_links_label_text.isNull())
	{
		about_dialog_links_label_text = new GLUITextView(*gl_ui, opengl_engine, "Ссылки:", Vec2f(text_left_x, current_y), text_args);
		gl_ui->addWidget(about_dialog_links_label_text);
	}
	else
	{
		about_dialog_links_label_text->setPos(Vec2f(text_left_x, current_y));
	}
	about_dialog_links_label_text->setVisible(true);
	current_y -= line_height;
	current_y -= line_spacing;
	
	// 3. Telegram channel and Site buttons in one row (after subtitle)
	const float button_spacing = gl_ui->getUIWidthForDevIndepPixelWidth(20);
	const float button_w = gl_ui->getUIWidthForDevIndepPixelWidth(140);
	
	if(about_dialog_telegram_channel_button.isNull())
	{
		about_dialog_telegram_channel_button = new GLUITextButton(*gl_ui, opengl_engine, "Telegram", Vec2f(text_left_x, current_y), button_args);
		about_dialog_telegram_channel_button->handler = this;
		gl_ui->addWidget(about_dialog_telegram_channel_button);
	}
	else
	{
		about_dialog_telegram_channel_button->setPos(Vec2f(text_left_x, current_y));
	}
	about_dialog_telegram_channel_button->setVisible(true);
	
	if(about_dialog_site_button.isNull())
	{
		about_dialog_site_button = new GLUITextButton(*gl_ui, opengl_engine, "metasiberia.com", Vec2f(text_left_x + button_w + button_spacing, current_y), button_args);
		about_dialog_site_button->handler = this;
		gl_ui->addWidget(about_dialog_site_button);
	}
	else
	{
		about_dialog_site_button->setPos(Vec2f(text_left_x + button_w + button_spacing, current_y));
	}
	about_dialog_site_button->setVisible(true);
	current_y -= line_height;
	current_y -= line_spacing;
	
	// 4. GitHub and VK buttons in one row (reuse button_spacing and button_w from above)
	
	if(about_dialog_github_button.isNull())
	{
		about_dialog_github_button = new GLUITextButton(*gl_ui, opengl_engine, "GitHub", Vec2f(text_left_x, current_y), button_args);
		about_dialog_github_button->handler = this;
		gl_ui->addWidget(about_dialog_github_button);
	}
	else
	{
		about_dialog_github_button->setPos(Vec2f(text_left_x, current_y));
	}
	about_dialog_github_button->setVisible(true);
	
	if(about_dialog_vk_button1.isNull())
	{
		about_dialog_vk_button1 = new GLUITextButton(*gl_ui, opengl_engine, "VK", Vec2f(text_left_x + button_w + button_spacing, current_y), button_args);
		about_dialog_vk_button1->handler = this;
		gl_ui->addWidget(about_dialog_vk_button1);
	}
	else
	{
		about_dialog_vk_button1->setPos(Vec2f(text_left_x + button_w + button_spacing, current_y));
	}
	about_dialog_vk_button1->setVisible(true);
	current_y -= line_height;
	current_y -= line_spacing;
	
	// 5. Author label: "Автор:"
	if(about_dialog_author_label_text.isNull())
	{
		about_dialog_author_label_text = new GLUITextView(*gl_ui, opengl_engine, "Автор:", Vec2f(text_left_x, current_y), text_args);
		gl_ui->addWidget(about_dialog_author_label_text);
	}
	else
	{
		about_dialog_author_label_text->setPos(Vec2f(text_left_x, current_y));
	}
	about_dialog_author_label_text->setVisible(true);
	current_y -= line_height;
	current_y -= line_spacing;
	
	// 5.5. Author name: "Денис Шипилов"
	if(about_dialog_author_name_text.isNull())
	{
		about_dialog_author_name_text = new GLUITextView(*gl_ui, opengl_engine, "Денис Шипилов", Vec2f(text_left_x, current_y), text_args);
		gl_ui->addWidget(about_dialog_author_name_text);
	}
	else
	{
		about_dialog_author_name_text->setPos(Vec2f(text_left_x, current_y));
	}
	about_dialog_author_name_text->setVisible(true);
	current_y -= line_height;
	current_y -= line_spacing;
	
	// 6. VK and Telegram buttons in one row (author's personal)
	if(about_dialog_vk_button2.isNull())
	{
		about_dialog_vk_button2 = new GLUITextButton(*gl_ui, opengl_engine, "VK", Vec2f(text_left_x, current_y), button_args);
		about_dialog_vk_button2->handler = this;
		gl_ui->addWidget(about_dialog_vk_button2);
	}
	else
	{
		about_dialog_vk_button2->setPos(Vec2f(text_left_x, current_y));
	}
	about_dialog_vk_button2->setVisible(true);
	
	if(about_dialog_telegram_button.isNull())
	{
		about_dialog_telegram_button = new GLUITextButton(*gl_ui, opengl_engine, "Telegram", Vec2f(text_left_x + button_w + button_spacing, current_y), button_args);
		about_dialog_telegram_button->handler = this;
		gl_ui->addWidget(about_dialog_telegram_button);
	}
	else
	{
		about_dialog_telegram_button->setPos(Vec2f(text_left_x + button_w + button_spacing, current_y));
	}
	about_dialog_telegram_button->setVisible(true);
	current_y -= line_height;
	current_y -= line_spacing;
	
	// 7. Subtitle: "Основана и создана на glare-core" (moved to bottom)
	if(about_dialog_subtitle_text.isNull())
	{
		about_dialog_subtitle_text = new GLUITextView(*gl_ui, opengl_engine, "Основана и создана на glare-core", Vec2f(text_left_x, current_y), text_args);
		gl_ui->addWidget(about_dialog_subtitle_text);
	}
	else
	{
		about_dialog_subtitle_text->setPos(Vec2f(text_left_x, current_y));
	}
	about_dialog_subtitle_text->setVisible(true);
	current_y -= line_height;
	current_y -= line_spacing;
	
	// Create close button - use same style as other buttons
	if(about_dialog_close_button.isNull())
	{
		GLUITextButton::CreateArgs args = button_args; // Use same args as link buttons
		args.tooltip = "Закрыть";
		// Calculate initial position for button
		const float close_button_w = gl_ui->getUIWidthForDevIndepPixelWidth(120);
		const float close_button_h = gl_ui->getUIWidthForDevIndepPixelWidth(35);
		const float close_button_y = dialog_min.y + gl_ui->getUIWidthForDevIndepPixelWidth(25);
		const float close_button_x = dialog_min.x + (dialog_w - close_button_w) / 2.f;
		about_dialog_close_button = new GLUITextButton(*gl_ui, opengl_engine, "Закрыть", Vec2f(close_button_x, close_button_y), args);
		about_dialog_close_button->handler = this;
		gl_ui->addWidget(about_dialog_close_button);
		about_dialog_close_button->setVisible(true);
	}
	
	// Update button position if it already exists (reposition each time dialog is shown)
	if(about_dialog_close_button.nonNull())
	{
		const float close_button_w = gl_ui->getUIWidthForDevIndepPixelWidth(120);
		const float close_button_h = gl_ui->getUIWidthForDevIndepPixelWidth(35);
		const float close_button_y = dialog_min.y + gl_ui->getUIWidthForDevIndepPixelWidth(25);
		const float close_button_x = dialog_min.x + (dialog_w - close_button_w) / 2.f;
		about_dialog_close_button->setPos(Vec2f(close_button_x, close_button_y));
		about_dialog_close_button->setVisible(true);
	}
	
	// Show all widgets
	about_dialog_background_panel->setVisible(true);
	if(about_dialog_beta_text.nonNull()) about_dialog_beta_text->setVisible(true);
	if(about_dialog_title_text.nonNull()) about_dialog_title_text->setVisible(true);
	if(about_dialog_links_label_text.nonNull()) about_dialog_links_label_text->setVisible(true);
	if(about_dialog_telegram_channel_button.nonNull()) about_dialog_telegram_channel_button->setVisible(true);
	if(about_dialog_site_button.nonNull()) about_dialog_site_button->setVisible(true);
	if(about_dialog_github_button.nonNull()) about_dialog_github_button->setVisible(true);
	if(about_dialog_vk_button1.nonNull()) about_dialog_vk_button1->setVisible(true);
	if(about_dialog_author_label_text.nonNull()) about_dialog_author_label_text->setVisible(true);
	if(about_dialog_author_name_text.nonNull()) about_dialog_author_name_text->setVisible(true);
	if(about_dialog_vk_button2.nonNull()) about_dialog_vk_button2->setVisible(true);
	if(about_dialog_telegram_button.nonNull()) about_dialog_telegram_button->setVisible(true);
	if(about_dialog_subtitle_text.nonNull()) about_dialog_subtitle_text->setVisible(true);
	if(about_dialog_close_button.nonNull()) about_dialog_close_button->setVisible(true);
	
	about_dialog_visible = true;
}


void SDLUIInterface::hideAboutDialog()
{
	if(about_dialog_background_panel.nonNull())
		about_dialog_background_panel->setVisible(false);
	if(about_dialog_beta_text.nonNull())
		about_dialog_beta_text->setVisible(false);
	if(about_dialog_title_text.nonNull())
		about_dialog_title_text->setVisible(false);
	if(about_dialog_subtitle_text.nonNull())
		about_dialog_subtitle_text->setVisible(false);
	if(about_dialog_links_label_text.nonNull())
		about_dialog_links_label_text->setVisible(false);
	if(about_dialog_telegram_channel_button.nonNull())
		about_dialog_telegram_channel_button->setVisible(false);
	if(about_dialog_site_button.nonNull())
		about_dialog_site_button->setVisible(false);
	if(about_dialog_github_button.nonNull())
		about_dialog_github_button->setVisible(false);
	if(about_dialog_vk_button1.nonNull())
		about_dialog_vk_button1->setVisible(false);
	if(about_dialog_author_label_text.nonNull())
		about_dialog_author_label_text->setVisible(false);
	if(about_dialog_author_name_text.nonNull())
		about_dialog_author_name_text->setVisible(false);
	if(about_dialog_vk_button2.nonNull())
		about_dialog_vk_button2->setVisible(false);
	if(about_dialog_telegram_button.nonNull())
		about_dialog_telegram_button->setVisible(false);
	if(about_dialog_close_button.nonNull())
		about_dialog_close_button->setVisible(false);
	about_dialog_visible = false;
}


bool SDLUIInterface::handleAboutDialogMousePress(const MouseEvent& e)
{
	if(!about_dialog_visible)
		return false;
	
	// Convert mouse coordinates to UI coordinates
	const Vec2f coords = gui_client->gl_ui->UICoordsForOpenGLCoords(e.gl_coords);
	
	// Check if click is outside the dialog panel
	if(about_dialog_background_panel.nonNull())
	{
		if(!about_dialog_background_panel->rect.inOpenRectangle(coords))
		{
			// Click is outside dialog - close it
			hideAboutDialog();
			return true; // Event handled
		}
	}
	
	return false; // Event not handled (click was inside dialog)
}


void SDLUIInterface::eventOccurred(GLUICallbackEvent& event)
{
	// Handle clicks on About dialog buttons
	if(event.widget == about_dialog_close_button.ptr())
	{
		event.accepted = true;
		hideAboutDialog();
	}
	else if(event.widget == about_dialog_telegram_channel_button.ptr())
	{
		event.accepted = true;
		openURL("https://t.me/metasiberia");
	}
	else if(event.widget == about_dialog_site_button.ptr())
	{
		event.accepted = true;
		openURL("https://metasiberia.com");
	}
	else if(event.widget == about_dialog_github_button.ptr())
	{
		event.accepted = true;
		openURL("https://github.com/Metasiberia/metasiberia");
	}
	else if(event.widget == about_dialog_vk_button1.ptr())
	{
		event.accepted = true;
		openURL("https://vk.com/metasiberia");
	}
	else if(event.widget == about_dialog_vk_button2.ptr())
	{
		event.accepted = true;
		openURL("https://vk.com/denshipilov");
	}
	else if(event.widget == about_dialog_telegram_button.ptr())
	{
		event.accepted = true;
		openURL("https://t.me/denshipilov");
	}
	// Handle clicks on Avatar Settings dialog buttons
	else if(event.widget == avatar_settings_close_button.ptr())
	{
		event.accepted = true;
		hideAvatarSettings();
	}
	else if(event.widget == avatar_settings_readyplayerme_button.ptr())
	{
		event.accepted = true;
		openURL("https://substrata.readyplayer.me/avatar");
		hideAvatarSettings();
	}
	else if(event.widget == avatar_settings_avaturnme_button.ptr())
	{
		event.accepted = true;
		openURL("https://avaturn.me/");
		hideAvatarSettings();
	}
}
