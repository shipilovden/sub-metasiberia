/*=====================================================================
MiscInfoUI.cpp
--------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#include "MiscInfoUI.h"


#include "GUIClient.h"
#include "URLParser.h"
#if USE_SDL || EMSCRIPTEN
#include "SDLUIInterface.h"
#endif
#include <tracy/Tracy.hpp>
#include <utils/ConPrint.h>
#include <utils/Exception.h>
#include <utils/StringUtils.h>
#include <maths/mathstypes.h>


MiscInfoUI::MiscInfoUI()
:	gui_client(NULL),
	transit_input_visible(false),
	visible(true)
{}


MiscInfoUI::~MiscInfoUI()
{}


void MiscInfoUI::create(Reference<OpenGLEngine>& opengl_engine_, GUIClient* gui_client_, GLUIRef gl_ui_)
{
	ZoneScoped; // Tracy profiler

	opengl_engine = opengl_engine_;
	gui_client = gui_client_;
	gl_ui = gl_ui_;

#if EMSCRIPTEN || USE_SDL // Show login, signup and movement buttons on web and SDL.
	GLUITextButton::CreateArgs login_args;
	login_args.tooltip = "Войти в существующий аккаунт";
	login_args.font_size_px = 12;
	login_button = new GLUITextButton(*gl_ui_, opengl_engine_, "Войти", Vec2f(0.f), login_args);
	login_button->handler = this;
	gl_ui->addWidget(login_button);

	GLUITextButton::CreateArgs signup_args;
	signup_args.tooltip = "Создать новый аккаунт";
	signup_args.font_size_px = 12;
	signup_button = new GLUITextButton(*gl_ui_, opengl_engine_, "Зарегистрироваться", Vec2f(0.f), signup_args);
	signup_button->handler = this;
	gl_ui->addWidget(signup_button);


	movement_button = new GLUIButton(*gl_ui_, opengl_engine_, /*tex path=*/gui_client->resources_dir_path + "/buttons/dir_pad.png", Vec2f(0.f), /*dims=*/Vec2f(0.4f, 0.1f), GLUIButton::CreateArgs());
	movement_button->handler = this;
	gl_ui->addWidget(movement_button);


	{
		GLUIButton::CreateArgs args;
		args.tooltip = "Настройки аватара";
		avatar_button = new GLUIButton(*gl_ui_, opengl_engine_, gui_client->resources_dir_path + "/buttons/avatar.png", Vec2f(0.f), /*dims=*/Vec2f(0.4f, 0.1f), args);
		avatar_button->handler = this;
		gl_ui->addWidget(avatar_button);
	}

	{
		try
		{
			GLUIButton::CreateArgs args;
			args.tooltip = "Транзит";
			transit_button = new GLUIButton(*gl_ui_, opengl_engine_, gui_client->resources_dir_path + "/buttons/transit_icon.png", Vec2f(0.f), /*dims=*/Vec2f(0.4f, 0.1f), args);
			transit_button->handler = this;
			gl_ui->addWidget(transit_button);
		}
		catch(glare::Exception& e)
		{
			conPrint("Warning: Failed to create transit button: " + e.what());
			// transit_button will remain null, which is fine - it will just not be shown
		}
	}

	{
		try
		{
			GLUIButton::CreateArgs args;
			args.tooltip = "Домой";
			home_button = new GLUIButton(*gl_ui_, opengl_engine_, gui_client->resources_dir_path + "/buttons/home.png", Vec2f(0.f), /*dims=*/Vec2f(0.4f, 0.1f), args);
			home_button->handler = this;
			gl_ui->addWidget(home_button);
		}
		catch(glare::Exception& e)
		{
			conPrint("Warning: Failed to create home button: " + e.what());
			// home_button will remain null, which is fine - it will just not be shown
		}
	}

	{
		try
		{
			GLUIButton::CreateArgs args;
			args.tooltip = "Показать участки";
			parcels_button = new GLUIButton(*gl_ui_, opengl_engine_, gui_client->resources_dir_path + "/buttons/parcels.png", Vec2f(0.f), /*dims=*/Vec2f(0.4f, 0.1f), args);
			parcels_button->handler = this;
			gl_ui->addWidget(parcels_button);
		}
		catch(glare::Exception& e)
		{
			conPrint("Warning: Failed to create parcels button: " + e.what());
			// parcels_button will remain null, which is fine - it will just not be shown
		}
		
		// Create anchor button (between home and parcels)
		{
			try
			{
				GLUIButton::CreateArgs args;
				args.tooltip = "Заякориться";
				anchor_button = new GLUIButton(*gl_ui_, opengl_engine_, gui_client->resources_dir_path + "/buttons/anchor.png", Vec2f(0.f), /*dims=*/Vec2f(0.4f, 0.1f), args);
				anchor_button->handler = this;
				gl_ui->addWidget(anchor_button);
			}
			catch(glare::Exception& e)
			{
				conPrint("Warning: Failed to create anchor button: " + e.what());
				// anchor_button will remain null, which is fine - it will just not be shown
			}
		}
	}

	// Create about button (next to parcels button)
	{
		try
		{
			GLUIButton::CreateArgs args;
			args.tooltip = "О программе";
			about_button = new GLUIButton(*gl_ui_, opengl_engine_, gui_client->resources_dir_path + "/buttons/about.png", Vec2f(0.f), /*dims=*/Vec2f(0.4f, 0.1f), args);
			about_button->handler = this;
			gl_ui->addWidget(about_button);
		}
		catch(glare::Exception& e)
		{
			conPrint("Warning: Failed to create about button: " + e.what());
			// about_button will remain null, which is fine - it will just not be shown
		}
	}

    // Create transit input field (like chat input) and thin white outline
	{
		GLUILineEdit::CreateArgs create_args;
		create_args.background_colour = Colour3f(0.1f); // Same gray as chat input
		create_args.background_alpha = 0.8f; // Slightly transparent like chat input
		create_args.font_size_px = 12; // Same as chat
		create_args.width = gl_ui->getUIWidthForDevIndepPixelWidth(200); // Small width
		transit_input_field = new GLUILineEdit(*gl_ui_, opengl_engine_, Vec2f(0.f), create_args);
		
		// Set up Enter key handler to connect to server
		GLUILineEdit* transit_input_field_ptr = transit_input_field.ptr();
		MiscInfoUI* this_ptr = this; // Capture 'this' to access transit_input_visible
		transit_input_field->on_enter_pressed = [transit_input_field_ptr, gui_client_, this_ptr]()
		{
			if(!transit_input_field_ptr->getText().empty())
			{
				std::string url_string = transit_input_field_ptr->getText();
				
				// If URL doesn't start with "sub://", add it
				if(url_string.find("sub://") != 0)
				{
					url_string = "sub://" + url_string;
				}
				
				try
				{
					// Parse the URL
					URLParseResults parse_results = URLParser::parseURL(url_string);
					
					// Schedule connection for timerEvent() to avoid blocking UI thread
					gui_client_->pending_transit_connection = parse_results;
					gui_client_->has_pending_transit_connection = true;
					
					// Clear the input field
					transit_input_field_ptr->clear();
					
					// Hide the input field
					this_ptr->transit_input_visible = false;
					if(this_ptr->transit_input_field)
						this_ptr->transit_input_field->setVisible(false);
				}
				catch(glare::Exception& e)
				{
					// Show error message
					conPrint("Error parsing URL: " + e.what());
					gui_client_->ui_interface->showPlainTextMessageBox("Ошибка разбора URL", e.what());
				}
			}
		};
		
        gl_ui->addWidget(transit_input_field);
		transit_input_field->setVisible(false);
	}
#endif

	updateWidgetPositions();
}


void MiscInfoUI::destroy()
{
	checkRemoveAndDeleteWidget(gl_ui, movement_button);
	checkRemoveAndDeleteWidget(gl_ui, login_button);
	checkRemoveAndDeleteWidget(gl_ui, signup_button);
	checkRemoveAndDeleteWidget(gl_ui, logged_in_button);
	checkRemoveAndDeleteWidget(gl_ui, avatar_button);
	checkRemoveAndDeleteWidget(gl_ui, transit_button);
	checkRemoveAndDeleteWidget(gl_ui, home_button);
	checkRemoveAndDeleteWidget(gl_ui, anchor_button);
	checkRemoveAndDeleteWidget(gl_ui, parcels_button);
	checkRemoveAndDeleteWidget(gl_ui, about_button);
    checkRemoveAndDeleteWidget(gl_ui, transit_input_field);
	checkRemoveAndDeleteWidget(gl_ui, admin_msg_text_view);
	checkRemoveAndDeleteWidget(gl_ui, unit_string_view);

	for(size_t i=0; i<prebuilt_digits.size(); ++i)
		gl_ui->removeWidget(prebuilt_digits[i]);
	prebuilt_digits.clear();

	gl_ui = NULL;
	opengl_engine = NULL;
}


void MiscInfoUI::setVisible(bool visible_)
{
	visible = visible_;

	if(movement_button) 
		movement_button->setVisible(visible);

	if(login_button) 
		login_button->setVisible(visible);

	if(signup_button) 
		signup_button->setVisible(visible);

	if(logged_in_button) 
		logged_in_button->setVisible(visible);
	
	if(avatar_button) 
		avatar_button->setVisible(visible);

	if(transit_button) 
		transit_button->setVisible(visible);

	if(home_button)
		home_button->setVisible(visible);
	
	if(anchor_button)
		anchor_button->setVisible(visible);
	
	if(parcels_button)
		parcels_button->setVisible(visible);

	if(transit_input_field)
		transit_input_field->setVisible(visible && transit_input_visible);

	if(admin_msg_text_view) 
		admin_msg_text_view->setVisible(visible);

	for(size_t i=0; i<prebuilt_digits.size(); ++i)
		prebuilt_digits[i]->setVisible(visible);

	if(unit_string_view) 
		unit_string_view->setVisible(visible);
}


void MiscInfoUI::showLogInAndSignUpButtons()
{
	if(login_button)
		login_button->setVisible(true);

	if(signup_button)
		signup_button->setVisible(true);
	
	if(logged_in_button)
		logged_in_button->setVisible(false);

	updateWidgetPositions();
}


void MiscInfoUI::showLoggedInButton(const std::string& username)
{
	// Hide login and signup buttons
	if(login_button)
		login_button->setVisible(false);

	if(signup_button)
		signup_button->setVisible(false);
	

	// Remove any existing logged_in_button (as text may change)
	if(logged_in_button)
		gl_ui->removeWidget(logged_in_button);
	logged_in_button = NULL;

#if EMSCRIPTEN // Only show on web
	GLUITextButton::CreateArgs logged_in_button_args;
	logged_in_button_args.tooltip = "View user account";
	logged_in_button_args.font_size_px = 12;
	logged_in_button = new GLUITextButton(*gl_ui, opengl_engine, "Logged in as " + username, Vec2f(0.f), logged_in_button_args);
	logged_in_button->handler = this;
	gl_ui->addWidget(logged_in_button);
#endif

	updateWidgetPositions();
}


void MiscInfoUI::showServerAdminMessage(const std::string& msg)
{
	if(msg.empty())
	{
		// Destroy/remove admin_msg_text_view
		if(admin_msg_text_view.nonNull())
		{
			gl_ui->removeWidget(admin_msg_text_view);
			admin_msg_text_view = NULL;
		}
	}
	else
	{
		if(admin_msg_text_view.isNull())
		{
			GLUITextView::CreateArgs create_args;
			admin_msg_text_view = new GLUITextView(*gl_ui, opengl_engine, msg, /*botleft=*/Vec2f(0.1f, 0.9f), create_args); // Create off-screen
			admin_msg_text_view->setTextColour(Colour3f(1.0f, 0.6f, 0.3f));
			gl_ui->addWidget(admin_msg_text_view);
		}

		admin_msg_text_view->setText(*gl_ui, msg);

		updateWidgetPositions();
	}
}

static const int speed_margin_px = 70; // pixels between bottom of viewport and text baseline.
static const int speed_font_size_px = 40;
static const int speed_font_x_advance = 50; // between digits

void MiscInfoUI::showVehicleSpeed(float speed_km_per_h)
{
	const float text_y = -gl_ui->getViewportMinMaxY() + gl_ui->getUIWidthForDevIndepPixelWidth(speed_margin_px);

	// The approach we will take here is to pre-create the digits 0-9 in the ones, tens and hundreds places, and then only make the digits corresponding to the current speed visible.
	// This will avoid any runtime allocs.
	// prebuilt_digits will be laid out like:
	// 0, 0, 0, 1, 1, 1, 2, 2, 2, 3, 3, 3, ..
	// |   \   \
	// |   tens  ones
	// hundreds

	if(prebuilt_digits.empty())
	{
		prebuilt_digits.resize(30);
		for(int i=0; i<30; ++i)
		{
			const int digit_val = i / 3;
			const int digit_place = i % 3;
			GLUITextView::CreateArgs create_args;
			create_args.font_size_px = speed_font_size_px;
			create_args.background_alpha = 0;
			create_args.text_selectable = false;
			prebuilt_digits[i] = new GLUITextView(*gl_ui, opengl_engine, toString(digit_val), Vec2f(0.f + (-3 + digit_place) * gl_ui->getUIWidthForDevIndepPixelWidth(speed_font_x_advance), text_y), create_args);
		}
	}

	const int speed_int = (int)speed_km_per_h;

	for(int i=0; i<3; ++i) // For each digit place:
	{
		const int place_1_val = (i == 0) ? 100 : ((i == 1) ? 10 : 1);
		const int digit_val = (speed_int / place_1_val) % 10;

		for(int z=0; z<10; ++z) // For digit val z at digit place i:
		{
			const bool should_draw = (digit_val == z) && ((speed_int >= place_1_val) || (i == 2 && speed_int == 0)); // Don't show leading zeroes.  But if speed = 0, we do need to show one zero.
			prebuilt_digits[z*3 + i]->setVisible(should_draw && visible);
		}
	}

	if(unit_string_view.isNull())
	{
		const std::string msg = " km/h";
		GLUITextView::CreateArgs create_args;
		create_args.font_size_px = speed_font_size_px;
		create_args.background_alpha = 0;
		create_args.text_selectable = false;
		unit_string_view = new GLUITextView(*gl_ui, opengl_engine, msg, /*botleft=*/Vec2f(gl_ui->getUIWidthForDevIndepPixelWidth(speed_font_x_advance) * 0, text_y), create_args); // Create off-screen
		unit_string_view->setTextColour(Colour3f(1.0f, 1.0f, 1.0f));
		gl_ui->addWidget(unit_string_view);
	}
	else
		unit_string_view->setVisible(true && visible);

	updateWidgetPositions();
}


void MiscInfoUI::showVehicleInfo(const std::string& msg)
{
	updateWidgetPositions();
}



void MiscInfoUI::hideVehicleSpeed()
{
	// Destroy/remove speed_text_view

	for(size_t i=0; i<prebuilt_digits.size(); ++i)
		prebuilt_digits[i]->setVisible(false);

	if(unit_string_view.nonNull())
		unit_string_view->setVisible(false);
}


void MiscInfoUI::think()
{
	if(gl_ui.nonNull())
	{
	}
}


void MiscInfoUI::updateWidgetPositions()
{
	if(gl_ui.nonNull())
	{
		const float min_max_y = gl_ui->getViewportMinMaxY();
		const float margin = gl_ui->getUIWidthForDevIndepPixelWidth(18);

		float cur_x = -1 + margin;
		if(login_button && signup_button)
		{
			const Vec2f login_button_dims  = login_button->rect.getWidths();
			const Vec2f signup_button_dims = signup_button->rect.getWidths();
			
			const float x_spacing = margin;//gl_ui->getUIWidthForDevIndepPixelWidth(10);
			//const float total_w = login_button_dims.x + x_spacing + signup_button_dims.x;

			if(login_button->isVisible())
			{
				login_button ->setPos(/*botleft=*/Vec2f(cur_x                                  , min_max_y - login_button_dims.y  - margin));
				signup_button->setPos(/*botleft=*/Vec2f(cur_x + login_button_dims.x + x_spacing, min_max_y - signup_button_dims.y - margin));

				cur_x = cur_x + login_button_dims.x + x_spacing + signup_button_dims.x + x_spacing;
			}
		}

		if(logged_in_button)
		{
			const Vec2f button_dims = logged_in_button->rect.getWidths();

			if(logged_in_button->isVisible())
			{
				logged_in_button->setPos(/*botleft=*/Vec2f(/*-1 + margin*/cur_x, min_max_y - button_dims.y + - margin));
				cur_x = cur_x + button_dims.x + margin;
			}
		}

		if(avatar_button)
		{
			const float button_size = gl_ui->getUIWidthForDevIndepPixelWidth(40); // Size for both avatar and transit buttons

			// Adjust position slightly differently depending on if we have login/logged-in buttons or not.
			float avatar_x;
			if(login_button || logged_in_button)
				avatar_x = cur_x - margin * 0.4f;
			else
				avatar_x = cur_x - margin * 0.1f;
			
			const float avatar_y = min_max_y - button_size * 0.82f - margin;
			const Vec2f button_dims = Vec2f(button_size, button_size);
			avatar_button->setPosAndDims(/*botleft=*/Vec2f(avatar_x, avatar_y), button_dims);
			
			// Position transit button right next to avatar button - use EXACTLY the same size
			if(transit_button)
			{
				const float button_spacing = margin * 0.3f; // Small spacing between buttons
				const float transit_x = avatar_x + button_size + button_spacing;
				transit_button->setPosAndDims(/*botleft=*/Vec2f(transit_x, avatar_y), button_dims);
				
				// Position home button right next to transit button - use EXACTLY the same size
				if(home_button)
				{
					const float home_x = transit_x + button_size + button_spacing;
					home_button->setPosAndDims(/*botleft=*/Vec2f(home_x, avatar_y), button_dims);
					
					// Position anchor button between home and parcels
					if(anchor_button)
					{
						const float anchor_x = home_x + button_size + button_spacing;
						anchor_button->setPosAndDims(/*botleft=*/Vec2f(anchor_x, avatar_y), button_dims);
					}
					
					// Position parcels button right next to anchor button (or home if no anchor) - use EXACTLY the same size
					if(parcels_button)
					{
						const float parcels_x = anchor_button ? (anchor_button->rect.getMax().x + button_spacing) : (home_x + button_size + button_spacing);
						parcels_button->setPosAndDims(/*botleft=*/Vec2f(parcels_x, avatar_y), button_dims);
					}
					
					// Position about button right next to parcels button - use EXACTLY the same size
					if(about_button)
					{
						const float about_x = parcels_button ? (parcels_button->rect.getMax().x + button_spacing) : (anchor_button ? (anchor_button->rect.getMax().x + button_spacing) : (home_x + button_size + button_spacing));
						about_button->setPosAndDims(/*botleft=*/Vec2f(about_x, avatar_y), button_dims);
					}
				}
				
                // Position transit input field centered on screen, slightly above cursor line
                if(transit_input_field)
                {
                    const float input_field_width = gl_ui->getUIWidthForDevIndepPixelWidth(500); // Wider input
                    const float center_x = 0.f;
                    const float input_field_x = center_x - input_field_width / 2.f;
                    const float input_field_y = gl_ui->getViewportMinMaxY() * 0.2f; // raise above center ~20% of half-height
                    transit_input_field->setWidth(input_field_width);
                    transit_input_field->setPos(Vec2f(input_field_x, input_field_y));

                }
			}
		}
		else if(transit_button)
		{
			// If avatar_button doesn't exist, position transit_button independently
			const float button_w = gl_ui->getUIWidthForDevIndepPixelWidth(40); // Same size as avatar button would be
			float transit_x;
			if(login_button || logged_in_button)
				transit_x = cur_x - margin * 0.4f;
			else
				transit_x = cur_x - margin * 0.1f;
			const float transit_y = min_max_y - button_w * 0.82f - margin;
			transit_button->setPosAndDims(/*botleft=*/Vec2f(transit_x, transit_y), Vec2f(button_w, button_w));
			
			// Position home button right next to transit button - use EXACTLY the same size
			if(home_button)
			{
				const float button_spacing = margin * 0.3f; // Small spacing between buttons
				const float home_x = transit_x + button_w + button_spacing;
				home_button->setPosAndDims(/*botleft=*/Vec2f(home_x, transit_y), Vec2f(button_w, button_w));
				
				// Position anchor button between home and parcels
				if(anchor_button)
				{
					const float anchor_x = home_x + button_w + button_spacing;
					anchor_button->setPosAndDims(/*botleft=*/Vec2f(anchor_x, transit_y), Vec2f(button_w, button_w));
				}
				
				// Position parcels button right next to anchor button (or home if no anchor) - use EXACTLY the same size
				if(parcels_button)
				{
					const float parcels_x = anchor_button ? (anchor_button->rect.getMax().x + button_spacing) : (home_x + button_w + button_spacing);
					parcels_button->setPosAndDims(/*botleft=*/Vec2f(parcels_x, transit_y), Vec2f(button_w, button_w));
				}
			}
			
            // Position transit input field centered on screen, slightly above cursor line
            if(transit_input_field)
            {
                const float input_field_width = gl_ui->getUIWidthForDevIndepPixelWidth(500); // Wider input
                const float center_x = 0.f;
                const float input_field_x = center_x - input_field_width / 2.f;
                const float input_field_y = gl_ui->getViewportMinMaxY() * 0.2f; // raise above center ~20% of half-height
                transit_input_field->setWidth(input_field_width);
                transit_input_field->setPos(Vec2f(input_field_x, input_field_y));

            }
		}


		if(admin_msg_text_view.nonNull())
		{
			const Vec2f text_dims = admin_msg_text_view->getRect().getWidths();

			const float vert_margin = 50.f / opengl_engine->getViewPortWidth(); // 50 pixels

			admin_msg_text_view->setPos(/*botleft=*/Vec2f(-0.4f, min_max_y - text_dims.y - vert_margin));
		}
		

		const float text_y = -gl_ui->getViewportMinMaxY() + gl_ui->getUIWidthForDevIndepPixelWidth(speed_margin_px);

		// 0, 0, 0, 1, 1, 1, 2, 2, 2
		for(int i=0; i<(int)prebuilt_digits.size(); ++i)
		{
			const int digit_place = i % 3;
			prebuilt_digits[i]->setPos(Vec2f(0.f + (-3 + digit_place) * gl_ui->getUIWidthForDevIndepPixelWidth(speed_font_x_advance), text_y));
		}
		if(unit_string_view.nonNull())
			unit_string_view->setPos(/*botleft=*/Vec2f(gl_ui->getUIWidthForDevIndepPixelWidth(speed_font_x_advance) * 0, text_y));


		if(movement_button)
		{
			// Keep joystick closer to bottom edge.
			const float spacing = gl_ui->getUIWidthForDevIndepPixelWidth(8);

			// Joystick size: 33px
			const float button_w = myMax(gl_ui->getUIWidthForDevIndepPixelWidth(33), 0.05f);
			const float button_h = button_w;
			// Shift joystick right to avoid overlap with enlarged chat button (chat is now 60px instead of 40px)
			// Original position was -1.f + 0.4f, shift it further right
			const Vec2f pos = Vec2f(-1.f + 0.55f, -min_max_y + spacing);

			movement_button->setPosAndDims(pos, Vec2f(button_w, button_h));
		}
	}
}


void MiscInfoUI::viewportResized(int w, int h)
{
	if(gl_ui.nonNull())
	{
		if(login_button) login_button->rebuild();
		if(signup_button) signup_button->rebuild();
		if(logged_in_button) logged_in_button->rebuild();

		updateWidgetPositions();
	}
}


void MiscInfoUI::eventOccurred(GLUICallbackEvent& event)
{
	if(gui_client)
	{
		if(event.widget == admin_msg_text_view.ptr())
		{
			//GLUIButton* button = static_cast<GLUIButton*>(event.widget);

			event.accepted = true;

			//conPrint("Clicked on text view!");
			//gui_client->setSelfieModeEnabled(selfie_button->toggled);
		}
		else if(event.widget == login_button.ptr())
		{
			gui_client->loginButtonClicked();

			event.accepted = true;
		}
		else if(event.widget == signup_button.ptr())
		{
			gui_client->signupButtonClicked();

			event.accepted = true;
		}
		else if(event.widget == logged_in_button.ptr())
		{
			gui_client->loggedInButtonClicked();

			event.accepted = true;
		}
		else if(event.widget == movement_button.ptr())
		{
			event.accepted = true;
		}
		else if(event.widget == avatar_button.ptr())
		{
			gui_client->ui_interface->showAvatarSettings();
			event.accepted = true;
		}
		else if(event.widget == transit_button.ptr())
		{
            // Toggle transit input field visibility
			transit_input_visible = !transit_input_visible;
			if(transit_input_field)
			{
				transit_input_field->setVisible(transit_input_visible);
				updateWidgetPositions(); // Update position
			}
			event.accepted = true;
		}
		else if(event.widget == home_button.ptr())
		{
			// Always connect to sub://vr.metasiberia.com/Admin
			try
			{
				URLParseResults parse_results = URLParser::parseURL("sub://vr.metasiberia.com/Admin");
				
				// Schedule connection for timerEvent() to avoid blocking UI thread
				gui_client->pending_transit_connection = parse_results;
				gui_client->has_pending_transit_connection = true;
			}
			catch(glare::Exception& e)
			{
				conPrint("Error parsing home URL: " + e.what());
				gui_client->ui_interface->showPlainTextMessageBox("Ошибка разбора URL", e.what());
			}
			
			event.accepted = true;
		}
		else if(event.widget == anchor_button.ptr())
		{
			// Save current position as start location
#if USE_SDL || EMSCRIPTEN
			if(gui_client && gui_client->ui_interface)
			{
				SDLUIInterface* sdl_ui = dynamic_cast<SDLUIInterface*>(gui_client->ui_interface);
				if(sdl_ui && sdl_ui->settings_store.nonNull())
				{
					try
					{
						// Get current position
						const Vec3d pos = gui_client->cam_controller.getFirstPersonPosition();
						const double heading_deg = Maths::doubleMod(::radToDegree(gui_client->cam_controller.getAngles().x), 360.0);
						
						// Build URL string with current position
						std::string url;
						url.reserve(128);
						url += "sub://";
						url += gui_client->server_hostname;
						url += "/";
						url += gui_client->server_worldname;
						url += "?x=";
						url += doubleToStringNDecimalPlaces(pos.x, 1);
						url += "&y=";
						url += doubleToStringNDecimalPlaces(pos.y, 1);
						url += "&z=";
						url += doubleToStringNDecimalPlaces(pos.z, 2);
						url += "&heading=";
						url += doubleToStringNDecimalPlaces(heading_deg, 1);
						
						// Save to settings
						sdl_ui->settings_store->setStringValue("setting/start_location_URL", url);
						
						conPrint("Заякорились: сохранено местоположение " + url);
					}
					catch(glare::Exception& e)
					{
						conPrint("Ошибка при сохранении местоположения: " + e.what());
					}
				}
			}
#endif
			event.accepted = true;
		}
		else if(event.widget == parcels_button.ptr())
		{
			// Toggle show parcels
#if USE_SDL || EMSCRIPTEN
			SDLUIInterface* sdl_ui_interface = dynamic_cast<SDLUIInterface*>(gui_client->ui_interface);
			if(sdl_ui_interface)
			{
				sdl_ui_interface->show_parcels_enabled = !sdl_ui_interface->show_parcels_enabled;
				
				if(sdl_ui_interface->show_parcels_enabled)
				{
					gui_client->addParcelObjects();
				}
				else
				{
					gui_client->removeParcelObjects();
				}
			}
#endif
			
			event.accepted = true;
		}
		else if(event.widget == about_button.ptr())
		{
			// Toggle about dialog (show if hidden, hide if visible)
			event.accepted = true;
#if USE_SDL || EMSCRIPTEN
			if(gui_client && gui_client->ui_interface)
			{
				// Try to cast to SDLUIInterface to call showAboutDialog
				SDLUIInterface* sdl_ui = dynamic_cast<SDLUIInterface*>(gui_client->ui_interface);
				if(sdl_ui)
				{
					if(sdl_ui->isAboutDialogVisible())
						sdl_ui->hideAboutDialog();
					else
						sdl_ui->showAboutDialog();
				}
				else
				{
					conPrint("О программе: SDLUIInterface недоступен");
				}
			}
#endif
		}
	}
}


bool MiscInfoUI::handleTransitInputMousePress(const MouseEvent& e)
{
#if !EMSCRIPTEN
    if(!transit_input_visible || transit_input_field.isNull() || !gl_ui.nonNull())
        return false;

    // Convert mouse coordinates to UI coordinates
    const Vec2f coords = gl_ui->UICoordsForOpenGLCoords(e.gl_coords);

    // If click is outside input field rect, hide it
    if(!transit_input_field->rect.inOpenRectangle(coords))
    {
        transit_input_visible = false;
        transit_input_field->setVisible(false);
        return true;
    }
#endif
    return false;
}
