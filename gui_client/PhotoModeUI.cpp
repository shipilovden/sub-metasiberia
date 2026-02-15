/*=====================================================================
PhotoModeUI.cpp
---------------
Copyright Glare Technologies Limited 2025 -
=====================================================================*/
#include "PhotoModeUI.h"


#include "GUIClient.h"
#include "ThreadMessages.h"
#include "../shared/Protocol.h"
#include "../shared/ImageDecoding.h"
#include <graphics/jpegdecoder.h>
#include <graphics/SRGBUtils.h>
#include "HTTPClient.h"
#include <networking/TLSSocket.h>
#include <algorithm>
#include <utils/Clock.h>
#include <utils/StringUtils.h>
#include <utils/PlatformUtils.h>
#include <utils/FileChecksum.h>
#include <utils/MemMappedFile.h>
#include <utils/MessageableThread.h>
#include <utils/JSONParser.h>
#include <webserver/Escaping.h>
#if EMSCRIPTEN
#include <networking/EmscriptenWebSocket.h>
#endif

PhotoModeUI::PhotoModeUI()
:	gui_client(NULL)
{}


PhotoModeUI::~PhotoModeUI()
{}


void PhotoModeUI::makePhotoModeSlider(PhotoModeSlider& slider, const std::string& label, const std::string& tooltip, double min_val, double max_val, double initial_value, double scroll_speed)
{
	GLUITextView::CreateArgs text_view_args;
	text_view_args.background_alpha = 0;
	text_view_args.text_colour = Colour3f(0.9f);
	text_view_args.tooltip = tooltip;

	slider.label = new GLUITextView(*gl_ui, opengl_engine, label, Vec2f(0), text_view_args);
	gl_ui->addWidget(slider.label);

	GLUISlider::CreateArgs args;
	args.tooltip = tooltip;
	args.min_value = min_val;
	args.max_value = max_val;
	args.initial_value = initial_value;
	args.scroll_speed = scroll_speed;
	slider.slider = new GLUISlider(*gl_ui, opengl_engine, Vec2f(0), Vec2f(0.1f), args);
	slider.slider->handler = this;
	gl_ui->addWidget(slider.slider);

	slider.value_view = new GLUITextView(*gl_ui, opengl_engine, doubleToStringMaxNDecimalPlaces(args.initial_value, 2), Vec2f(0), text_view_args);
	gl_ui->addWidget(slider.value_view);
}


static double focusDistForSliderVal(double slider_val)
{
	return 1.0 / (1.0 - slider_val) - 1.0;
}

static double sliderValForFocusDist(double d)
{
	/*
	d = 1 / (1 - v) - 1
	d + 1 = 1 / (1 - v)
	(d + 1) (1 - v) = 1
	(d + 1) - v(d + 1) = 1
	- v(d + 1) = 1 - (d + 1)
	-v = (1 - (d + 1)) / (d + 1)
	v = -(1 - (d + 1)) / (d + 1)
	v = (-1 + (d + 1)) / (d + 1)
	v = ((d + 1) - 1) / (d + 1)
	v = d / (d + 1)
	*/
	return d / (d + 1.0);
}


void PhotoModeUI::create(Reference<OpenGLEngine>& opengl_engine_, GUIClient* gui_client_, GLUIRef gl_ui_, const Reference<SettingsStore>& settings_)
{
	opengl_engine = opengl_engine_;
	gui_client = gui_client_;
	gl_ui = gl_ui_;
	settings = settings_;

	{
		GLUITextButton::CreateArgs args;
		args.tooltip = "The standard camera mode when not in photo mode.";
		standard_cam_button = new GLUITextButton(*gl_ui_, opengl_engine_, "standard camera", Vec2f(0), args);
		standard_cam_button->handler = this;
		gl_ui->addWidget(standard_cam_button);

		standard_cam_button->setToggled(false);
	}
	{
		GLUITextButton::CreateArgs args;
		args.tooltip = "A camera that looks back at the avatar's face.  The avatar will look towards the camera.";
		selfie_cam_button = new GLUITextButton(*gl_ui_, opengl_engine_, "selfie camera", Vec2f(0), args);
		selfie_cam_button->handler = this;
		gl_ui->addWidget(selfie_cam_button);

		selfie_cam_button->setToggled(true);
	}
	{
		GLUITextButton::CreateArgs args;
		args.tooltip = "A camera with a fixed angle relative to the player avatar or vehicle.";
		fixed_angle_cam_button = new GLUITextButton(*gl_ui_, opengl_engine_, "fixed angle camera", Vec2f(0), args);
		fixed_angle_cam_button->handler = this;
		gl_ui->addWidget(fixed_angle_cam_button);
	}
	{
		GLUITextButton::CreateArgs args;
		args.tooltip = "The free camera can move anywhere and look in any direction";
		free_cam_button = new GLUITextButton(*gl_ui_, opengl_engine_, "free camera", Vec2f(0), args);
		free_cam_button->handler = this;
		gl_ui->addWidget(free_cam_button);
	}
	{
		GLUITextButton::CreateArgs args;
		args.tooltip = "A camera with a fixed positon that looks at the avatar";
		tracking_cam_button = new GLUITextButton(*gl_ui_, opengl_engine_, "tracking camera", Vec2f(0), args);
		tracking_cam_button->handler = this;
		gl_ui->addWidget(tracking_cam_button);
	}
	{
		GLUITextButton::CreateArgs args;
		args.tooltip = "Reset photo mode camera settings";
		reset_button = new GLUITextButton(*gl_ui_, opengl_engine_, "Reset", Vec2f(0), args);
		reset_button->handler = this;
		gl_ui->addWidget(reset_button);
	}

	{
		GLUITextView::CreateArgs text_view_args;
		text_view_args.background_alpha = 0;
		text_view_args.text_colour = Colour3f(0.9f);
		text_view_args.tooltip = "Autofocus";

		autofocus_label = new GLUITextView(*gl_ui, opengl_engine, "Autofocus", Vec2f(0), text_view_args);
		gl_ui->addWidget(autofocus_label);

	}

	{
		GLUITextButton::CreateArgs args;
		args.tooltip = "Disable autofocus.  Focus Distance slider is used instead.";
		autofocus_off_button = new GLUITextButton(*gl_ui_, opengl_engine_, "Off", Vec2f(0), args);
		autofocus_off_button->handler = this;
		gl_ui->addWidget(autofocus_off_button);
	}
	{
		GLUITextButton::CreateArgs args;
		args.tooltip = "Automatically focus on the nearest avatar eye.";
		autofocus_eye_button = new GLUITextButton(*gl_ui_, opengl_engine_, "Eye", Vec2f(0), args);
		autofocus_eye_button->handler = this;
		gl_ui->addWidget(autofocus_eye_button);
	}
	{
		GLUIButton::CreateArgs args;
		args.tooltip = "Take photo";
		take_screenshot_button = new GLUIButton(*gl_ui, opengl_engine, gui_client->resources_dir_path + "/buttons/Selfie.png", Vec2f(0), Vec2f(0.1f, 0.1f),args);
		//take_screenshot_button = new GLUIButton(*gl_ui, opengl_engine, gui_client->resources_dir_path + "/buttons/minimap_icon.png", Vec2f(0), Vec2f(0.1f, 0.1f),args);
		take_screenshot_button->handler = this;
		gl_ui->addWidget(take_screenshot_button);
	}
	{
		GLUITextButton::CreateArgs args;
		args.tooltip = "Show photos folder";
		show_screenshots_button = new GLUITextButton(*gl_ui_, opengl_engine_, "Show photos", Vec2f(0), args);
		show_screenshots_button->handler = this;
		gl_ui->addWidget(show_screenshots_button);
	}
	{
		GLUITextButton::CreateArgs args;
		args.tooltip = "Upload photo to Telegram (requires telegram settings)";
		upload_photo_button = new GLUITextButton(*gl_ui_, opengl_engine_, "Upload photo", Vec2f(0), args);
		upload_photo_button->handler = this;
		gl_ui->addWidget(upload_photo_button);
	}
	{
		GLUITextButton::CreateArgs args;
		args.tooltip = "Hide user interface";
		hide_ui_button = new GLUITextButton(*gl_ui_, opengl_engine_, "Hide UI", Vec2f(0), args);
		hide_ui_button->handler = this;
		gl_ui->addWidget(hide_ui_button);
	}

	{
		GLUIInertWidget::CreateArgs args;
		args.background_colour = Colour3f(0.0f);
		args.background_alpha = 0.2f;
		args.z = 0.1f;
		background_ob = new GLUIInertWidget(*gl_ui, opengl_engine, args);
		gl_ui->addWidget(background_ob);
	}

	makePhotoModeSlider(dof_blur_slider, /*label=*/"Depth of field blur", /*tooltip=*/"Depth of field blur strength", 
		/*min val=*/0.0, /*max val=*/1.0, /*initial val=*/opengl_engine->getCurrentScene()->dof_blur_strength, /*scroll speed=*/1.0);

	makePhotoModeSlider(dof_focus_distance_slider, /*label=*/"Focus Distance", /*tooltip=*/"Focus Distance", 
		/*min val=*/0.001, /*max val=*/1.0, /*initial val=*/sliderValForFocusDist(opengl_engine->getCurrentScene()->dof_blur_focus_distance), /*scroll speed=*/1.0);

	makePhotoModeSlider(ev_adjust_slider, /*label=*/"EV adjust", /*tooltip=*/"EV adjust", 
		/*min val=*/-8, /*max val=*/8, /*initial val=*/0, /*scroll speed=*/1.0);

	makePhotoModeSlider(saturation_slider, /*label=*/"Saturation", /*tooltip=*/"Colour saturation", 
		/*min val=*/0, /*max val=*/2, /*initial val=*/1, /*scroll speed=*/1.0);

	makePhotoModeSlider(focal_length_slider, /*label=*/"Focal length", /*tooltip=*/"Camera focal length", 
		/*min val=*/0.010, /*max val=*/1.0, /*initial val=*/0.025, /*scroll speed=*/0.05);

	makePhotoModeSlider(roll_slider, /*label=*/"Roll", /*tooltip=*/"Camera roll angle", 
		/*min val=*/-90, /*max val=*/90, /*initial val=*/0, /*scroll speed=*/1.0);

	updateFocusDistValueString();
	updateFocalLengthValueString();

	updateWidgetPositions();
}

static void checkRemove(GLUIRef gl_ui, PhotoModeSlider& slider)
{
	checkRemoveAndDeleteWidget(gl_ui, slider.label);
	checkRemoveAndDeleteWidget(gl_ui, slider.slider);
	checkRemoveAndDeleteWidget(gl_ui, slider.value_view);
}

void PhotoModeUI::destroy()
{
	checkRemoveAndDeleteWidget(gl_ui, standard_cam_button);
	checkRemoveAndDeleteWidget(gl_ui, selfie_cam_button);
	checkRemoveAndDeleteWidget(gl_ui, fixed_angle_cam_button);
	checkRemoveAndDeleteWidget(gl_ui, free_cam_button);
	checkRemoveAndDeleteWidget(gl_ui, tracking_cam_button);
	checkRemoveAndDeleteWidget(gl_ui, reset_button);
	checkRemoveAndDeleteWidget(gl_ui, autofocus_label);
	checkRemoveAndDeleteWidget(gl_ui, autofocus_off_button);
	checkRemoveAndDeleteWidget(gl_ui, autofocus_eye_button);
	checkRemoveAndDeleteWidget(gl_ui, take_screenshot_button);
	checkRemoveAndDeleteWidget(gl_ui, show_screenshots_button);
	checkRemoveAndDeleteWidget(gl_ui, upload_photo_button);
	checkRemoveAndDeleteWidget(gl_ui, hide_ui_button);
	checkRemoveAndDeleteWidget(gl_ui, background_ob);

	checkRemoveAndDeleteWidget(gl_ui, upload_background_ob);
	checkRemoveAndDeleteWidget(gl_ui, upload_image_widget);
	checkRemoveAndDeleteWidget(gl_ui, caption_label);
	checkRemoveAndDeleteWidget(gl_ui, caption_line_edit);
	checkRemoveAndDeleteWidget(gl_ui, ok_button);
	checkRemoveAndDeleteWidget(gl_ui, cancel_button);

	checkRemove(gl_ui, dof_blur_slider);
	checkRemove(gl_ui, dof_focus_distance_slider);
	checkRemove(gl_ui, ev_adjust_slider);
	checkRemove(gl_ui, saturation_slider);
	checkRemove(gl_ui, focal_length_slider);
	checkRemove(gl_ui, roll_slider);
	
	gl_ui = NULL;
	gui_client = NULL;
	opengl_engine = NULL;
}


void PhotoModeUI::setVisible(bool visible)
{
	if(standard_cam_button)
	{
		standard_cam_button->setVisible(visible);
		selfie_cam_button->setVisible(visible);
		fixed_angle_cam_button->setVisible(visible);
		free_cam_button->setVisible(visible);
		tracking_cam_button->setVisible(visible);
		reset_button->setVisible(visible);

		autofocus_label->setVisible(visible);
		autofocus_off_button->setVisible(visible);
		autofocus_eye_button->setVisible(visible);

		take_screenshot_button->setVisible(visible);
		show_screenshots_button->setVisible(visible);
		upload_photo_button->setVisible(visible);
		hide_ui_button->setVisible(visible);

		background_ob->setVisible(visible);

		dof_blur_slider.setVisible(visible);
		dof_focus_distance_slider.setVisible(visible);
		ev_adjust_slider.setVisible(visible);
		saturation_slider.setVisible(visible);
		focal_length_slider.setVisible(visible);
		roll_slider.setVisible(visible);
	}
}


bool PhotoModeUI::isVisible() const
{
	if(standard_cam_button) 
		return standard_cam_button->isVisible();
	else
		return false;
}


void PhotoModeUI::enablePhotoModeUI()
{
	setVisible(true);

	updateWidgetPositions();

	resetControlsToPhotoModeDefaults();
}


void PhotoModeUI::disablePhotoModeUI()
{
	// Restore camera settings to the defaults
	resetControlsToNonPhotoModeDefaults();

	// Hide Photo mode UI
	setVisible(false);
}


bool PhotoModeUI::isPhotoModeEnabled()
{
	return standard_cam_button && standard_cam_button->isVisible();
}


void PhotoModeUI::autofocusDistSet(double dist)
{
	dof_focus_distance_slider.slider->setValueNoEvent(sliderValForFocusDist(dist));
	updateFocusDistValueString();
}


//void PhotoModeUI::standardCameraModeSelected()
//{
//	untoggleAllCamModeButtons();
//	standard_cam_button->setToggled(true);
//}


void PhotoModeUI::think()
{
}


void PhotoModeUI::updateWidgetPositions()
{
	if(gl_ui && standard_cam_button)
	{
		const float margin = gl_ui->getUIWidthForDevIndepPixelWidth(12);

		float cur_y = gl_ui->getViewportMinMaxY() - gl_ui->getUIWidthForDevIndepPixelWidth(100);

		cur_y -= standard_cam_button->rect.getWidths().y + margin;
		standard_cam_button->setPos(/*botleft=*/Vec2f(1 - 0.3f, cur_y));
		
		cur_y -= selfie_cam_button->rect.getWidths().y + margin;
		selfie_cam_button->setPos(/*botleft=*/Vec2f(1 - 0.3f, cur_y));
		
		cur_y -= fixed_angle_cam_button->rect.getWidths().y + margin;
		fixed_angle_cam_button->setPos(/*botleft=*/Vec2f(1 - 0.3f, cur_y));
		
		cur_y -= tracking_cam_button->rect.getWidths().y + margin;
		tracking_cam_button->setPos(/*botleft=*/Vec2f(1 - 0.3f, cur_y));
		
		cur_y -= free_cam_button->rect.getWidths().y + margin;
		free_cam_button->setPos(/*botleft=*/Vec2f(1 - 0.3f, cur_y));
	
		// Autofocus label and buttons
		cur_y -= autofocus_off_button->rect.getWidths().y + margin;

		const float label_w  = autofocus_label->rect.getWidths().x;
		autofocus_label->setPos(Vec2f(1 - 0.3f - label_w - margin, cur_y + gl_ui->getUIWidthForDevIndepPixelWidth(8)));

		autofocus_off_button->setPos(/*botleft=*/Vec2f(1 - 0.3f, cur_y));

		autofocus_eye_button->setPos(/*botleft=*/Vec2f(autofocus_off_button->rect.getMax().x + margin, cur_y));
	
		updateSliderPosition(dof_blur_slider, margin, cur_y);
		updateSliderPosition(dof_focus_distance_slider, margin, cur_y);
		updateSliderPosition(ev_adjust_slider, margin, cur_y);
		updateSliderPosition(saturation_slider, margin, cur_y);
		updateSliderPosition(focal_length_slider, margin, cur_y);
		updateSliderPosition(roll_slider, margin, cur_y);		
		
		cur_y -= reset_button->rect.getWidths().y + margin;
		reset_button->setPos(/*botleft=*/Vec2f(1 - 0.3f, cur_y));
		
		static const float BUTTON_W_PIXELS = 50;
		const float BUTTON_W = gl_ui->getUIWidthForDevIndepPixelWidth(BUTTON_W_PIXELS);
		const float button_padding = gl_ui->getUIWidthForDevIndepPixelWidth(10);
		cur_y -= BUTTON_W + margin - button_padding;
		const float selfie_button_x = 1 - 0.3f;
		take_screenshot_button->setPosAndDims(Vec2f(selfie_button_x, cur_y), Vec2f(BUTTON_W, BUTTON_W));
		cur_y += button_padding;

		cur_y -= show_screenshots_button->rect.getWidths().y + margin;
		show_screenshots_button->setPos(/*botleft=*/Vec2f(1 - 0.3f, cur_y));
		
		cur_y -= upload_photo_button->rect.getWidths().y + margin;
		upload_photo_button->setPos(/*botleft=*/Vec2f(1 - 0.3f, cur_y));

		cur_y -= hide_ui_button->rect.getWidths().y + margin;
		hide_ui_button->setPos(/*botleft=*/Vec2f(1 - 0.3f, cur_y));

		// Set the transform of the transparent background quad behind sliders.
		{
			const float back_margin = gl_ui->getUIWidthForDevIndepPixelWidth(10);
			const float background_w = 1.f - roll_slider.label->rect.getMin().x + back_margin;
			const float background_h = dof_blur_slider.label->rect.getMax().y - roll_slider.label->rect.getMin().y + back_margin*2;
			background_ob->setPosAndDims(Vec2f(roll_slider.label->rect.getMin().x -back_margin, roll_slider.label->rect.getMin().y - back_margin), Vec2f(background_w, background_h));
		}

		if(upload_image_widget)
		{
			float exterior_margin = gl_ui->getUIWidthForDevIndepPixelWidth(80);
			float dialog_content_w = 2.f - exterior_margin * 2;
			float upload_dialog_x = -dialog_content_w/2;
			const float dialog_content_top_y = gl_ui->getViewportMinMaxY() - exterior_margin;
			float upload_dialog_y = dialog_content_top_y;

			float max_upload_im_w = dialog_content_w;
			float max_upload_im_h = gl_ui->getViewportMinMaxY() * 1.3f;

			float upload_im_w, upload_im_h;
			if(im_to_upload_aspect_ratio >  (max_upload_im_w / max_upload_im_h)) // If image is wider in aspect ratio than widget:
			{
				upload_im_w = max_upload_im_w;
				upload_im_h = max_upload_im_w / (float)im_to_upload_aspect_ratio;
			}
			else
			{
				upload_im_w = max_upload_im_h * (float)im_to_upload_aspect_ratio;
				upload_im_h = max_upload_im_h;
			}

			upload_dialog_y -= upload_im_h;
			upload_image_widget->setPosAndDims(Vec2f(-1.f + (2.f - upload_im_w) / 2.f, upload_dialog_y), Vec2f(upload_im_w, upload_im_h)); // Centre-align horizontally

			upload_dialog_y -= caption_line_edit->rect.getWidths().y + margin;
			caption_label->setPos(Vec2f(upload_dialog_x, upload_dialog_y + gl_ui->getUIWidthForDevIndepPixelWidth(10)));
			caption_line_edit->setPos(Vec2f(caption_label->rect.getMax().x + margin, upload_dialog_y));
			caption_line_edit->setWidth(dialog_content_w/2 - (caption_label->rect.getMax().x + margin));

			upload_dialog_y -= ok_button->rect.getWidths().y + margin;
			ok_button->setPos(Vec2f(dialog_content_w/2 - (ok_button->rect.getWidths().x + cancel_button->rect.getWidths().x + margin), upload_dialog_y));

			//upload_dialog_y -= cancel_button->rect.getWidths().y + margin;
			cancel_button->setPos(Vec2f(ok_button->rect.getMax().x + margin, upload_dialog_y));


			const float content_h = dialog_content_top_y - upload_dialog_y;

			// Set the transform of the transparent background quad behind dialog.
			const float background_w = dialog_content_w + margin*2;
			const float background_h = content_h + margin*2;
			upload_background_ob->setPosAndDims(Vec2f(upload_dialog_x - margin, upload_dialog_y - margin), Vec2f(background_w, background_h));
		}
	}
}


void PhotoModeUI::updateSliderPosition(PhotoModeSlider& slider, float margin, float& cur_y)
{
	cur_y -= slider.slider->rect.getWidths().y + margin;

	const float label_w  = gl_ui->getUIWidthForDevIndepPixelWidth(160);
	const float slider_w = gl_ui->getUIWidthForDevIndepPixelWidth(180);
	const float value_w  = gl_ui->getUIWidthForDevIndepPixelWidth(60);
	float x_margin = margin;

	float cur_x = 1.f - value_w - slider_w - label_w - x_margin * 3;

	float text_y_offset = gl_ui->getUIWidthForDevIndepPixelWidth(4); // To align with the slider
	slider.label->setPos(Vec2f(cur_x, cur_y + text_y_offset));
	cur_x += label_w + x_margin;

	slider.slider->setPosAndDims(/*botleft=*/Vec2f(cur_x, cur_y), /*dims=*/Vec2f(slider_w, gl_ui->getUIWidthForDevIndepPixelWidth(21)));
	cur_x += slider_w + x_margin;

	slider.value_view->setPos(Vec2f(cur_x, cur_y + text_y_offset));
}


void PhotoModeUI::viewportResized(int w, int h)
{
	if(gl_ui && standard_cam_button)
	{
		standard_cam_button->rebuild();
		selfie_cam_button->rebuild();
		fixed_angle_cam_button->rebuild();
		free_cam_button->rebuild();
		tracking_cam_button->rebuild();
		reset_button->rebuild();
		autofocus_off_button->rebuild();
		autofocus_eye_button->rebuild();
		show_screenshots_button->rebuild();
		upload_photo_button->rebuild();
		hide_ui_button->rebuild();

		updateWidgetPositions();
	}
}


void PhotoModeUI::untoggleAllCamModeButtons()
{
	if(standard_cam_button)
	{
		standard_cam_button->setToggled(false);
		selfie_cam_button->setToggled(false);
		fixed_angle_cam_button->setToggled(false);
		free_cam_button->setToggled(false);
		tracking_cam_button->setToggled(false);
	}
}


void PhotoModeUI::eventOccurred(GLUICallbackEvent& event)
{
	if(gui_client)
	{
		if(event.widget == standard_cam_button.ptr())
		{
			gui_client->cam_controller.standardCameraModeSelected();

			untoggleAllCamModeButtons();
			standard_cam_button->setToggled(true);

			event.accepted = true;
		}
		else if(event.widget == selfie_cam_button.ptr())
		{
			gui_client->cam_controller.selfieCameraModeSelected();

			untoggleAllCamModeButtons();
			selfie_cam_button->setToggled(true);

			event.accepted = true;
		}
		else if(event.widget == fixed_angle_cam_button.ptr())
		{
			gui_client->cam_controller.fixedAngleCameraModeSelected();

			untoggleAllCamModeButtons();
			fixed_angle_cam_button->setToggled(true);

			event.accepted = true;
		}
		else if(event.widget == free_cam_button.ptr())
		{
			gui_client->cam_controller.freeCameraModeSelected();

			untoggleAllCamModeButtons();
			free_cam_button->setToggled(true);

			event.accepted = true;
		}
		else if(event.widget == tracking_cam_button.ptr())
		{
			gui_client->cam_controller.trackingCameraModeSelected();

			untoggleAllCamModeButtons();
			tracking_cam_button->setToggled(true);

			event.accepted = true;
		}
		else if(event.widget == reset_button.ptr())
		{
			resetControlsToPhotoModeDefaults();

			event.accepted = true;
		}
		else if(event.widget == autofocus_off_button.ptr())
		{
			gui_client->cam_controller.setAutofocusMode(CameraController::AutofocusMode_Off);
			autofocus_off_button->setToggled(true);
			autofocus_eye_button->setToggled(false);

			event.accepted = true;
		}
		else if(event.widget == autofocus_eye_button.ptr())
		{
			gui_client->cam_controller.setAutofocusMode(CameraController::AutofocusMode_Eye);
			autofocus_off_button->setToggled(false);
			autofocus_eye_button->setToggled(true);

			event.accepted = true;
		}
		else if(event.widget == take_screenshot_button.ptr())
		{
			gui_client->ui_interface->takeScreenshot();

			event.accepted = true;
		}
		else if(event.widget == show_screenshots_button.ptr())
		{
			gui_client->ui_interface->showScreenshots();

			event.accepted = true;
		}
		else if(event.widget == upload_photo_button.ptr())
		{
			if(!gui_client->logged_in_user_id.valid())
			{
				gui_client->showErrorNotification("You must be logged in to upload a photo.");
			}
			else
			{
				showUploadPhotoWidget();
			}

			event.accepted = true;
		}
		else if(event.widget == hide_ui_button.ptr())
		{
			gui_client->hideUI();

			event.accepted = true;
		}
		else if(event.widget == ok_button.ptr())
		{
			this->last_caption = caption_line_edit->getText();

			hideUploadPhotoWidget();

			uploadPhoto();

			event.accepted = true;
		}
		else if(event.widget == cancel_button.ptr())
		{
			hideUploadPhotoWidget();

			event.accepted = true;
		}
	}
}


void PhotoModeUI::sliderValueChangedEventOccurred(GLUISliderValueChangedEvent& event)
{
	if(event.widget == dof_blur_slider.slider.ptr())
	{
		dof_blur_slider.value_view->setText(*gl_ui, doubleToStringMaxNDecimalPlaces(event.value, 2)); // Update value text view

		opengl_engine->getCurrentScene()->dof_blur_strength = (float)event.value;
	}
	else if(event.widget == dof_focus_distance_slider.slider.ptr())
	{
		// User has manually set focus distance

		// Disable autofocus if it was enabled
		if(!autofocus_off_button->isToggled())
		{
			gui_client->cam_controller.setAutofocusMode(CameraController::AutofocusMode_Off);
			autofocus_off_button->setToggled(true);
			autofocus_eye_button->setToggled(false);
		}

		updateFocusDistValueString();

		opengl_engine->getCurrentScene()->dof_blur_focus_distance = (float)focusDistForSliderVal(dof_focus_distance_slider.slider->getValue());
	}
	else if(event.widget == ev_adjust_slider.slider.ptr())
	{
		ev_adjust_slider.value_view->setText(*gl_ui, doubleToStringMaxNDecimalPlaces(event.value, 2)); // Update value text view

		opengl_engine->getCurrentScene()->exposure_factor = (float)std::exp2(event.value);
	}
	else if(event.widget == saturation_slider.slider.ptr())
	{
		saturation_slider.value_view->setText(*gl_ui, doubleToStringMaxNDecimalPlaces(event.value, 2)); // Update value text view

		opengl_engine->getCurrentScene()->saturation_multiplier = (float)event.value;
	}
	else if(event.widget == focal_length_slider.slider.ptr())
	{
		updateFocalLengthValueString();

		gui_client->cam_controller.lens_sensor_dist = (float)event.value;
	}
	else if(event.widget == roll_slider.slider.ptr())
	{
		roll_slider.value_view->setText(*gl_ui, doubleToStringMaxNDecimalPlaces(event.value, 2)); // Update value text view

		Vec3d angles = gui_client->cam_controller.getAngles();
		angles.z = ::degreeToRad(event.value);
		gui_client->cam_controller.setAngles(angles);
	}
}


void PhotoModeUI::updateFocusDistValueString()
{
	const double focus_dist = focusDistForSliderVal(dof_focus_distance_slider.slider->getValue());
	dof_focus_distance_slider.value_view->setText(*gl_ui, doubleToStringMaxNDecimalPlaces(focus_dist, 2) + " m"); // Update value text view
}


void PhotoModeUI::updateFocalLengthValueString()
{
	focal_length_slider.value_view->setText(*gl_ui, doubleToStringMaxNDecimalPlaces(focal_length_slider.slider->getValue() * 1000, 0) + " mm"); // Update value text view
}


void PhotoModeUI::resetControlsToPhotoModeDefaults()
{
	gui_client->cam_controller.standardCameraModeSelected();
	//gui_client->cam_controller.selfieCameraModeSelected();
	untoggleAllCamModeButtons();
	//selfie_cam_button->setToggled(true);
	standard_cam_button->setToggled(true);

	gui_client->cam_controller.setAutofocusMode(CameraController::AutofocusMode_Eye);
	autofocus_off_button->setToggled(false);
	autofocus_eye_button->setToggled(true);

	// Reset sliders, will reset actual values in OpenGLEngine as well via sliderValueChangedEventOccurred().
	dof_blur_slider.setValue(0.0, gl_ui);
	dof_focus_distance_slider.setValueNoEvent(sliderValForFocusDist(1.0), gl_ui); // setValueNoEvent so don't disable autofocus
	ev_adjust_slider.setValue(0.0, gl_ui);
	saturation_slider.setValue(1.0, gl_ui);
	focal_length_slider.setValue(0.025, gl_ui);
	roll_slider.setValue(0.0, gl_ui);

	opengl_engine->getCurrentScene()->dof_blur_strength = 0.0f; // Should already be set to zero but make sure.
}


void PhotoModeUI::resetControlsToNonPhotoModeDefaults()
{
	gui_client->cam_controller.standardCameraModeSelected();
	untoggleAllCamModeButtons();
	standard_cam_button->setToggled(true);

	gui_client->cam_controller.setAutofocusMode(CameraController::AutofocusMode_Off);
	//autofocus_off_button->setToggled(false);
	//autofocus_eye_button->setToggled(false);

	// Reset sliders, will reset actual values in OpenGLEngine as well via sliderValueChangedEventOccurred().
	dof_blur_slider.setValue(0.0, gl_ui);
	dof_focus_distance_slider.setValue(sliderValForFocusDist(1.0), gl_ui);
	ev_adjust_slider.setValue(0.0, gl_ui);
	saturation_slider.setValue(1.0, gl_ui);
	focal_length_slider.setValue(0.025, gl_ui);
	roll_slider.setValue(0.0, gl_ui);

	opengl_engine->getCurrentScene()->dof_blur_strength = 0.0f; // Should already be set to zero but make sure.
}


void PhotoModeUI::showUploadPhotoWidget()
{
	checkRemoveAndDeleteWidget(gl_ui, upload_background_ob);
	checkRemoveAndDeleteWidget(gl_ui, upload_image_widget);
	checkRemoveAndDeleteWidget(gl_ui, caption_label);
	checkRemoveAndDeleteWidget(gl_ui, caption_line_edit);
	checkRemoveAndDeleteWidget(gl_ui, ok_button);
	checkRemoveAndDeleteWidget(gl_ui, cancel_button);
	/*if(upload_background_overlay_ob)
	{
		opengl_engine->removeOverlayObject(upload_background_overlay_ob);
		upload_background_overlay_ob = NULL;
	}*/

	{
		std::string jpeg_path;
		try
		{
			// Get last image
			const std::string last_saved_photo_path = settings->getStringValue("photo/last_saved_photo_path", "");

			if(last_saved_photo_path.empty())
			{
				gui_client->showErrorNotification("Take a photo first!");
				return;
			}

			const uint64 hash = FileChecksum::fileChecksum(last_saved_photo_path);
		
			Reference<Map2D> im = ImageDecoding::decodeImage(".", last_saved_photo_path);

			if(!im.isType<ImageMapUInt8>())
				throw glare::Exception("not ImageMapUInt8");

			jpeg_path = PlatformUtils::getTempDirPath() + "/upload_photo_temp_" + toString(hash) + ".jpg";

			JPEGDecoder::save(im.downcast<ImageMapUInt8>(), jpeg_path, JPEGDecoder::SaveOptions());

			upload_image_widget = new GLUIImage(*gl_ui, opengl_engine, jpeg_path, Vec2f(0.f), Vec2f(0.1f), "photo to upload");
			gl_ui->addWidget(upload_image_widget);

			im_to_upload_aspect_ratio = (double)im->getMapWidth() / im->getMapHeight();

			this->upload_image_jpeg_path = jpeg_path;
		}
		catch(glare::Exception& e)
		{
			gui_client->showErrorNotification("Failed to load or convert photo for upload: " + e.what());
			return;
		}

		
	}

	{
		GLUITextView::CreateArgs args;
		args.background_alpha = 0;
		args.text_colour = toLinearSRGB(Colour3f(0.1f));
		args.tooltip = "Caption";
		args.z = -0.5f;
		caption_label = new GLUITextView(*gl_ui, opengl_engine, "Caption:", Vec2f(0.f), args);
		gl_ui->addWidget(caption_label);
	}

	{
		GLUILineEdit::CreateArgs args;
		args.width = 1.4f;
		args.z = -0.5f;
		args.text_colour = toLinearSRGB(Colour3f(0.1f));
		args.background_colour = Colour3f(0.7f);
		args.mouseover_background_colour = Colour3f(0.8f);
		caption_line_edit = new GLUILineEdit(*gl_ui, opengl_engine, Vec2f(0.f), args);
		gl_ui->addWidget(caption_line_edit);
	}

	{
		GLUITextButton::CreateArgs args;
		args.z = -0.5f;
		ok_button = new GLUITextButton(*gl_ui, opengl_engine, "Upload", Vec2f(0), args);
		ok_button->handler = this;
		gl_ui->addWidget(ok_button);
	}
	{
		GLUITextButton::CreateArgs args;
		args.z = -0.5f;
		cancel_button = new GLUITextButton(*gl_ui, opengl_engine, "Cancel", Vec2f(0), args);
		cancel_button->handler = this;
		gl_ui->addWidget(cancel_button);
	}

	/*{
		upload_background_overlay_ob = new OverlayObject();
		upload_background_overlay_ob->mesh_data = opengl_engine->getUnitQuadMeshData();
		upload_background_overlay_ob->material.albedo_linear_rgb = Colour3f(0.7f);
		upload_background_overlay_ob->material.alpha = 0.8f;

		upload_background_overlay_ob->ob_to_world_matrix = Matrix4f::identity();

		opengl_engine->addOverlayObject(upload_background_overlay_ob);
	}*/
	{
		GLUIInertWidget::CreateArgs args;
		args.background_colour = Colour3f(0.7f);
		args.background_alpha = 0.8f;
		args.z = -0.488f;
		upload_background_ob = new GLUIInertWidget(*gl_ui, opengl_engine, args);
		gl_ui->addWidget(upload_background_ob);
	}

	//showing_upload_widget = true;

	updateWidgetPositions();
}


void PhotoModeUI::hideUploadPhotoWidget()
{
	checkRemoveAndDeleteWidget(gl_ui, upload_background_ob);
	checkRemoveAndDeleteWidget(gl_ui, upload_image_widget);
	checkRemoveAndDeleteWidget(gl_ui, caption_label);
	checkRemoveAndDeleteWidget(gl_ui, caption_line_edit);
	checkRemoveAndDeleteWidget(gl_ui, ok_button);
	checkRemoveAndDeleteWidget(gl_ui, cancel_button);

	/*if(upload_background_overlay_ob)
	{
		opengl_engine->removeOverlayObject(upload_background_overlay_ob);
		upload_background_overlay_ob = NULL;
	}*/
}


class UploadPhotoThread : public MessageableThread
{
public:
	void doRun() override
	{
		try
		{
			conPrint("UploadPhotoThread: Connecting to " + hostname + ":" + toString(port) + "...");

			socket = nullptr;
#if EMSCRIPTEN
			socket = new EmscriptenWebSocket();

			const bool use_TLS = hostname != "localhost"; // Don't use TLS on localhost for now, for testing.
			const std::string protocol = use_TLS ? "wss" : "ws";
			socket.downcast<EmscriptenWebSocket>()->connect(protocol, hostname, /*port=*/use_TLS ? 443 : 80);
#else

			MySocketRef plain_socket = new MySocket(hostname, port);
			plain_socket->setUseNetworkByteOrder(false);

			socket = new TLSSocket(plain_socket, config, hostname);
#endif

			conPrint("UploadPhotoThread: Connected to " + hostname + ":" + toString(port) + "!");

			socket->writeUInt32(Protocol::CyberspaceHello); // Write hello
			socket->writeUInt32(Protocol::CyberspaceProtocolVersion); // Write protocol version
			socket->writeUInt32(Protocol::ConnectionTypeUploadPhoto); // Write connection type
			socket->flush();

			conPrint("UploadPhotoThread: reading hello...");
			// Read hello response from server
			const uint32 hello_response = socket->readUInt32();
			if(hello_response != Protocol::CyberspaceHello)
				throw glare::Exception("Invalid hello from server: " + toString(hello_response));

			conPrint("UploadPhotoThread: read hello_response.");

			// Read protocol version response from server
			const uint32 protocol_response = socket->readUInt32();
			if(protocol_response == Protocol::ClientProtocolTooOld)
			{
				const std::string msg = socket->readStringLengthFirst(10000);
				throw glare::Exception(msg);
			}
			else if(protocol_response == Protocol::ClientProtocolOK)
			{
			}
			else
				throw glare::Exception("Invalid protocol version response from server: " + toString(protocol_response));

			// Read server protocol version
			const uint32 server_protocol_version = socket->readUInt32();

			// Read server capabilities
			[[maybe_unused]] uint32 server_capabilities = 0;
			if(server_protocol_version >= 41)
				server_capabilities = socket->readUInt32();

			// Read server_mesh_optimisation_version
			[[maybe_unused]] int server_mesh_optimisation_version = 1;
			if(server_protocol_version >= 43)
				server_mesh_optimisation_version = socket->readInt32();

			// Send login details
			socket->writeStringLengthFirst(username);
			socket->writeStringLengthFirst(password);

			// Write world name
			socket->writeStringLengthFirst(world_name);

			// Write parcel id
			writeToStream(parcel_id, *socket);

			// Write cam_pos, cam_angles.
			socket->writeData(&cam_pos, sizeof(cam_pos));
			socket->writeData(&cam_angles, sizeof(cam_angles));

			// Write caption
			socket->writeStringLengthFirst(caption);

			// Load resource from disk
			MemMappedFile file(upload_image_jpeg_path);

			socket->writeUInt64(file.fileSize()); // Write file size

			// Send the photo
			socket->writeData(file.fileData(), file.fileSize());
			conPrint("UploadPhotoThread: Sent file '" + upload_image_jpeg_path + "' (" + toString(file.fileSize()) + " B)");

			// Read result: PhotoUploadSucceeded or PhotoUploadFailed
			const uint32 upload_result = socket->readUInt32();
			if(upload_result == Protocol::PhotoUploadSucceeded)
			{
				out_msg_queue->enqueue(new InfoMessage("Photo Uploaded"));
			}
			else if(upload_result == Protocol::PhotoUploadFailed)
			{
				const std::string msg = socket->readStringLengthFirst(10000);
				out_msg_queue->enqueue(new InfoMessage("Photo upload failed: " + msg));
			}
			else
				throw glare::Exception("Expected to read PhotoUploadSucceeded or PhotoUploadFailed");
		}
		catch(glare::Exception& e)
		{
			conPrint("UploadPhotoThread glare::Exception: " + e.what());

			out_msg_queue->enqueue(new ErrorMessage("Photo upload failed: " + e.what()));
		}
	}

	std::string upload_image_jpeg_path;
	std::string world_name;
	ParcelID parcel_id;
	Vec3d cam_pos, cam_angles;
	std::string caption;
	std::string hostname;
	int port;
	std::string username, password;
	glare::AtomicInt should_die;
	struct tls_config* config;
	SocketInterfaceRef socket;
	ThreadSafeQueue<Reference<ThreadMessage> >* out_msg_queue;
};


static std::string getOptionalEnvVar(const char* name)
{
	try
	{
		if(PlatformUtils::isEnvironmentVariableDefined(name))
			return PlatformUtils::getEnvironmentVariable(name);
	}
	catch(...) {}
	return std::string();
}


class UploadPhotoToTelegramThread : public MessageableThread
{
public:
	void doRun() override
	{
		try
		{
			if(bot_token.empty() || chat_id.empty())
				throw glare::Exception("Telegram upload is not configured.");

			// Load resource from disk
			MemMappedFile file(upload_image_jpeg_path);

			const std::string boundary =
				"------------------------metasiberia_" + toString((uint64)Clock::getSecsSince1970()) + "_" + toString((uint64)(uintptr_t)this);

			std::string body;
			body.reserve(1024 + caption.size() + (size_t)file.fileSize());

			auto appendField = [&](const char* name, const std::string& value)
			{
				body += "--" + boundary + "\r\n";
				body += "Content-Disposition: form-data; name=\"";
				body += name;
				body += "\"\r\n\r\n";
				body += value;
				body += "\r\n";
			};

			appendField("chat_id", chat_id);
			if(!caption.empty())
				appendField("caption", caption);

			// Photo part
			body += "--" + boundary + "\r\n";
			body += "Content-Disposition: form-data; name=\"photo\"; filename=\"photo.jpg\"\r\n";
			body += "Content-Type: image/jpeg\r\n\r\n";
			body.append((const char*)file.fileData(), (size_t)file.fileSize());
			body += "\r\n--" + boundary + "--\r\n";

			HTTPClientRef client = new HTTPClient();
			client->additional_headers.push_back("User-Agent: Metasiberia client");

			const std::string url = "https://api.telegram.org/bot" + bot_token + "/sendPhoto";

			std::string response_body;
			HTTPClient::ResponseInfo response = client->sendPost(
				url,
				body,
				"multipart/form-data; boundary=" + boundary,
				response_body
			);

			if(response.response_code != 200 || (response_body.find("\"ok\":true") == std::string::npos))
			{
				std::string snippet = response_body;
				if(snippet.size() > 4000)
					snippet = snippet.substr(0, 4000);
				throw glare::Exception("Telegram API returned HTTP " + toString(response.response_code) + ". Response: " + snippet);
			}

			out_msg_queue->enqueue(new InfoMessage("Photo posted to Telegram"));
		}
		catch(glare::Exception& e)
		{
			conPrint("UploadPhotoToTelegramThread glare::Exception: " + e.what());
			out_msg_queue->enqueue(new ErrorMessage("Telegram upload failed: " + e.what()));
		}
	}

	std::string upload_image_jpeg_path;
	std::string caption;
	std::string bot_token;
	std::string chat_id;
	ThreadSafeQueue<Reference<ThreadMessage> >* out_msg_queue = nullptr;
};


static std::string safeSnippet(const std::string& s, size_t max_len)
{
	if(s.size() <= max_len)
		return s;
	return s.substr(0, max_len);
}


static void throwVKErrorIfPresent(const JSONParser& parser, const JSONNode& root)
{
	if(!root.hasChild("error"))
		return;

	const JSONNode& err_obj = root.getChildObject(parser, "error");
	const std::string msg = err_obj.getChildStringValueWithDefaultVal(parser, "error_msg", "VK API error");
	const int code = err_obj.getChildIntValueWithDefaultVal(parser, "error_code", -1);

	// Common pitfall: using a community (group) token for methods that require a user token.
	// VK returns error 27 for "method is unavailable with group auth."
	if(code == 27)
		throw glare::Exception("VK API error 27: " + msg + "  This typically means you are using a community (group) token. For posting photos to a community wall, VK requires a USER access token (standalone app / implicit flow) with scopes like photos, wall, groups.");

	throw glare::Exception("VK API error " + toString(code) + ": " + msg);
}


static std::string makeVKMethodURL(const std::string& api_host, const char* method_name)
{
	if(api_host.empty())
		return std::string("https://api.vk.ru/method/") + method_name;

	// Allow passing full base URL like "https://api.vk.ru" just in case.
	const bool has_scheme = (api_host.find("://") != std::string::npos);
	const std::string base = has_scheme ? api_host : ("https://" + api_host);
	return base + "/method/" + method_name;
}

static std::string makeFormBody(const std::vector<std::pair<std::string, std::string>>& params)
{
	std::string body;
	body.reserve(params.size() * 32);

	for(size_t i=0; i<params.size(); ++i)
	{
		if(i > 0)
			body += "&";
		body += web::Escaping::URLEscape(params[i].first);
		body += "=";
		body += web::Escaping::URLEscape(params[i].second);
	}

	return body;
}

static std::string vkCallPOST(HTTPClient& client, const std::string& api_host, const char* method_name, const std::vector<std::pair<std::string, std::string>>& params)
{
	const std::string url = makeVKMethodURL(api_host, method_name);
	const std::string body = makeFormBody(params);

	std::string response_body;
	HTTPClient::ResponseInfo resp = client.sendPost(url, body, "application/x-www-form-urlencoded", response_body);
	if(resp.response_code != 200)
		throw glare::Exception(std::string("VK API ") + method_name + " returned HTTP " + toString(resp.response_code) + ". Response: " + safeSnippet(response_body, 2000));

	return response_body;
}


static uint64 parseVKGroupIdFromScreenName(HTTPClient& client, const std::string& api_host, const std::string& api_version, const std::string& screen_name)
{
	// Resolve group id via groups.getById.  Requires a valid access token (supplied via Authorization header).
	const std::string response_body = vkCallPOST(
		client,
		api_host,
		"groups.getById",
		{
			{"group_id", screen_name},
			{"v", api_version},
		}
	);

	JSONParser parser;
	parser.parseBuffer(response_body.c_str(), response_body.size());
	const JSONNode& root = parser.nodes[0];
	throwVKErrorIfPresent(parser, root);

	// VK can return either:
	// - {"response":[{...}]} (classic)
	// - {"response":{"groups":[{...}], "profiles":[...]}} (newer / some endpoints)
	const JSONNode& resp_node = root.getChildNode(parser, "response");
	if(resp_node.type == JSONNode::Type_Array)
	{
		if(resp_node.child_indices.empty())
			throw glare::Exception("VK API groups.getById returned empty response array.");

		const JSONNode& group_obj = parser.nodes[resp_node.child_indices[0]];
		return (uint64)group_obj.getChildUIntValue(parser, "id");
	}
	else if(resp_node.type == JSONNode::Type_Object)
	{
		const JSONNode& groups_arr = resp_node.getChildArray(parser, "groups");
		if(groups_arr.child_indices.empty())
			throw glare::Exception("VK API groups.getById returned empty response.groups array.");

		const JSONNode& group_obj = parser.nodes[groups_arr.child_indices[0]];
		return (uint64)group_obj.getChildUIntValue(parser, "id");
	}
	else
	{
		throw glare::Exception("Unexpected VK API groups.getById response type: " + JSONNode::typeString(resp_node.type));
	}
}


class UploadPhotoToVKThread : public MessageableThread
{
public:
	void doRun() override
	{
		try
		{
			if(access_token.empty())
				throw glare::Exception("VK upload is not configured.");

			const std::string use_api_host = api_host.empty() ? "api.vk.ru" : api_host;
			const std::string use_api_version = api_version.empty() ? "5.199" : api_version;

			uint64 use_group_id = group_id;

			HTTPClientRef client = new HTTPClient();
			client->additional_headers.push_back("User-Agent: Metasiberia client");
			client->additional_headers.push_back("Authorization: Bearer " + access_token);

			if(use_group_id == 0 && !group_screen_name.empty())
				use_group_id = parseVKGroupIdFromScreenName(*client, use_api_host, use_api_version, group_screen_name);
			if(use_group_id == 0)
				throw glare::Exception("VK upload is not configured (missing vk/group_id or vk/group_screen_name).");

			// 1) Get upload server
			{
				const std::string response_body = vkCallPOST(
					*client,
					use_api_host,
					"photos.getWallUploadServer",
					{
						{"group_id", toString(use_group_id)},
						{"v", use_api_version},
					}
				);

				JSONParser parser;
				parser.parseBuffer(response_body.c_str(), response_body.size());
				const JSONNode& root = parser.nodes[0];
				throwVKErrorIfPresent(parser, root);
				const JSONNode& resp_obj = root.getChildObject(parser, "response");
				upload_url = resp_obj.getChildStringValue(parser, "upload_url");
			}

			// Load resource from disk
			MemMappedFile file(upload_image_jpeg_path);

			// 2) Upload photo to upload_url
			int server = -1;
			std::string photo;
			std::string hash;
			{
				const std::string boundary =
					"------------------------metasiberia_vk_" + toString((uint64)Clock::getSecsSince1970()) + "_" + toString((uint64)(uintptr_t)this);

				std::string body;
				body.reserve(512 + (size_t)file.fileSize());

				body += "--" + boundary + "\r\n";
				body += "Content-Disposition: form-data; name=\"photo\"; filename=\"photo.jpg\"\r\n";
				body += "Content-Type: image/jpeg\r\n\r\n";
				body.append((const char*)file.fileData(), (size_t)file.fileSize());
				body += "\r\n--" + boundary + "--\r\n";

				std::string response_body;
				HTTPClient::ResponseInfo resp = client->sendPost(
					upload_url,
					body,
					"multipart/form-data; boundary=" + boundary,
					response_body
				);

				if(resp.response_code != 200)
					throw glare::Exception("VK upload server returned HTTP " + toString(resp.response_code) + ". Response: " + safeSnippet(response_body, 2000));

				JSONParser parser;
				parser.parseBuffer(response_body.c_str(), response_body.size());
				const JSONNode& root = parser.nodes[0];
				// Upload server response uses error fields too sometimes.
				if(root.hasChild("error"))
				{
					const std::string msg = root.getChildStringValueWithDefaultVal(parser, "error", "VK upload error");
					throw glare::Exception("VK upload error: " + msg);
				}

				server = root.getChildIntValue(parser, "server");
				photo = root.getChildStringValue(parser, "photo");
				hash = root.getChildStringValue(parser, "hash");
			}

			// 3) Save wall photo
			std::string attachment;
			{
				const std::string response_body = vkCallPOST(
					*client,
					use_api_host,
					"photos.saveWallPhoto",
					{
						{"group_id", toString(use_group_id)},
						{"server", toString(server)},
						{"photo", photo},
						{"hash", hash},
						{"v", use_api_version},
					}
				);

				JSONParser parser;
				parser.parseBuffer(response_body.c_str(), response_body.size());
				const JSONNode& root = parser.nodes[0];
				throwVKErrorIfPresent(parser, root);

				const JSONNode& arr = root.getChildArray(parser, "response");
				if(arr.child_indices.empty())
					throw glare::Exception("VK API photos.saveWallPhoto returned empty response array.");

				const JSONNode& photo_obj = parser.nodes[arr.child_indices[0]];
				const int owner_id = photo_obj.getChildIntValue(parser, "owner_id");
				const int id = photo_obj.getChildIntValue(parser, "id");
				const std::string access_key = photo_obj.getChildStringValueWithDefaultVal(parser, "access_key", "");

				attachment = "photo" + toString(owner_id) + "_" + toString(id);
				if(!access_key.empty())
					attachment += "_" + access_key;
			}

			// 4) Post to wall
			{
				const int owner_id = -(int)use_group_id;
				const std::string response_body = vkCallPOST(
					*client,
					use_api_host,
					"wall.post",
					{
						{"owner_id", toString(owner_id)},
						{"from_group", "1"},
						{"message", message},
						{"attachments", attachment},
						{"v", use_api_version},
					}
				);

				JSONParser parser;
				parser.parseBuffer(response_body.c_str(), response_body.size());
				const JSONNode& root = parser.nodes[0];
				throwVKErrorIfPresent(parser, root);
			}

			out_msg_queue->enqueue(new InfoMessage("Photo posted to VK"));
		}
		catch(glare::Exception& e)
		{
			conPrint("UploadPhotoToVKThread glare::Exception: " + e.what());
			out_msg_queue->enqueue(new ErrorMessage("VK upload failed: " + e.what()));
		}
	}

	std::string upload_image_jpeg_path;
	std::string message;
	std::string access_token;
	uint64 group_id = 0;
	std::string group_screen_name;
	std::string api_host;
	std::string api_version;
	ThreadSafeQueue<Reference<ThreadMessage> >* out_msg_queue = nullptr;

private:
	std::string upload_url;
};


static std::string trimAndCollapseWhitespace(const std::string& s, size_t max_len)
{
	std::string out;
	out.reserve(std::min(max_len, s.size()));

	bool last_was_space = false;
	for(size_t i=0; i<s.size() && out.size() < max_len; ++i)
	{
		const unsigned char c = (unsigned char)s[i];
		const bool is_space = (c == ' ' || c == '\t' || c == '\n' || c == '\r');
		if(is_space)
		{
			if(!out.empty() && !last_was_space)
				out.push_back(' ');
			last_was_space = true;
		}
		else
		{
			out.push_back((char)c);
			last_was_space = false;
		}
	}

	// Trim trailing space
	while(!out.empty() && out.back() == ' ')
		out.pop_back();

	return out;
}


static std::string getDefaultTelegramHashtags()
{
	return "#metasiberia #metasiberiabeta #screenshot #metaversefromsiberia";
}


static std::string buildSocialCaption(
	const GUIClient& gui_client,
	const Reference<SettingsStore>& settings,
	const char* title_key,
	const char* hashtags_key,
	const std::string& default_title,
	const std::string& default_hashtags,
	size_t max_caption_len,
	const std::string& user_caption,
	const std::string& username,
	const ParcelID& in_parcel_id)
{
	const std::string title = settings->getStringValue(title_key, default_title);
	std::string hashtags = settings->getStringValue(hashtags_key, "");
	if(hashtags.empty())
		hashtags = default_hashtags;

	const std::string safe_user_caption = trimAndCollapseWhitespace(user_caption, /*max_len=*/400);

	const std::string sub_url = gui_client.getCurrentURL();

	const std::string hostname = gui_client.server_hostname.empty() ? std::string("substrata.info") : gui_client.server_hostname;
	const bool use_http = (hostname == "localhost") || (hostname == "127.0.0.1");
	const std::string web_url = (use_http ? "http://" : "https://") + hostname + gui_client.getCurrentWebClientURLPath();

	std::string caption;
	caption.reserve(512);

	caption += title + "\n";

	if(!safe_user_caption.empty())
		caption += safe_user_caption + "\n";

	if(!username.empty())
		caption += "By: " + username + "\n";

	if(!gui_client.server_worldname.empty())
		caption += "World: " + gui_client.server_worldname + "\n";

	if(in_parcel_id.valid())
		caption += "Parcel: " + in_parcel_id.toString() + "\n";

	caption += "Location (client): " + sub_url + "\n";
	caption += "Location (web): " + web_url + "\n\n";

	caption += hashtags;

	if(caption.size() > max_caption_len)
	{
		// Keep the tail (hashtags) where possible.
		const std::string suffix = "\n\n" + hashtags;
		const size_t keep_prefix_len = (max_caption_len > suffix.size() + 3) ? (max_caption_len - suffix.size() - 3) : 0;
		caption = caption.substr(0, keep_prefix_len) + "..." + suffix;
		if(caption.size() > max_caption_len)
			caption.resize(max_caption_len);
	}

	return caption;
}


static std::string buildTelegramCaption(
	const GUIClient& gui_client,
	const Reference<SettingsStore>& settings,
	const std::string& user_caption,
	const std::string& username,
	const ParcelID& in_parcel_id)
{
	// Telegram photo caption limit is 1024 chars.
	const size_t MAX_CAPTION_LEN = 1024;
	return buildSocialCaption(
		gui_client,
		settings,
		/*title_key=*/"telegram/photo_caption_title",
		/*hashtags_key=*/"telegram/photo_hashtags",
		/*default_title=*/"Metasiberia Beta",
		/*default_hashtags=*/getDefaultTelegramHashtags(),
		/*max_caption_len=*/MAX_CAPTION_LEN,
		user_caption,
		username,
		in_parcel_id
	);
}


static std::string buildVKMessage(
	const GUIClient& gui_client,
	const Reference<SettingsStore>& settings,
	const std::string& user_caption,
	const std::string& username,
	const ParcelID& in_parcel_id)
{
	// VK message limit is larger than Telegram caption; keep it conservative.
	const size_t MAX_MSG_LEN = 3800;
	return buildSocialCaption(
		gui_client,
		settings,
		/*title_key=*/"vk/photo_caption_title",
		/*hashtags_key=*/"vk/photo_hashtags",
		/*default_title=*/"Metasiberia Beta",
		/*default_hashtags=*/getDefaultTelegramHashtags(),
		/*max_caption_len=*/MAX_MSG_LEN,
		user_caption,
		username,
		in_parcel_id
	);
}


void PhotoModeUI::uploadPhoto()
{
	const bool telegram_enabled = settings->getBoolValue("telegram/upload_photo_enabled", /*default=*/true);
	const bool vk_enabled = settings->getBoolValue("vk/upload_photo_enabled", /*default=*/true);
	const bool any_external_requested = telegram_enabled || vk_enabled;

	const std::string username_for_server = gui_client->ui_interface->getUsernameForDomain(gui_client->server_hostname);

	// Find out which parcel we are in, if any.
	// NOTE: use avatar position or camera position? Or focus target position?
	ParcelID in_parcel_id = ParcelID::invalidParcelID();
	{
		Lock lock(gui_client->world_state->mutex);
		const Parcel* cam_parcel = gui_client->world_state->getParcelPointIsIn(gui_client->cam_controller.getPosition());
		if(cam_parcel)
			in_parcel_id = cam_parcel->id;
	}

	std::string bot_token = settings->getStringValue("telegram/bot_token", "");
	if(bot_token.empty())
		bot_token = getOptionalEnvVar("METASIBERIA_TELEGRAM_BOT_TOKEN");

	std::string chat_id = settings->getStringValue("telegram/photo_upload_chat_id", "");
	if(chat_id.empty())
		chat_id = getOptionalEnvVar("METASIBERIA_TELEGRAM_PHOTO_CHAT_ID");

	bool started_any_external_upload = false;
	bool telegram_missing_config = false;
	bool vk_missing_config = false;

	if(telegram_enabled)
	{
		if(bot_token.empty() || chat_id.empty())
		{
			telegram_missing_config = true;
		}
		else
		{
			Reference<UploadPhotoToTelegramThread> thread = new UploadPhotoToTelegramThread();
			thread->caption = buildTelegramCaption(*gui_client, settings, last_caption, username_for_server, in_parcel_id);
			thread->upload_image_jpeg_path = upload_image_jpeg_path;
			thread->bot_token = bot_token;
			thread->chat_id = chat_id;
			thread->out_msg_queue = &gui_client->msg_queue;

			upload_thread_manager.addThread(thread);
			started_any_external_upload = true;
		}
	}

	std::string vk_token = settings->getStringValue("vk/access_token", "");
	if(vk_token.empty())
		vk_token = getOptionalEnvVar("METASIBERIA_VK_ACCESS_TOKEN");

	uint64 vk_group_id = (uint64)settings->getIntValue("vk/group_id", 0);
	if(vk_group_id == 0)
	{
		const std::string env_group_id = getOptionalEnvVar("METASIBERIA_VK_GROUP_ID");
		if(!env_group_id.empty())
		{
			try { vk_group_id = stringToUInt64(env_group_id); } catch(...) {}
		}
	}

	std::string vk_group_screen_name = settings->getStringValue("vk/group_screen_name", "metasiberia_official");
	if(vk_group_screen_name.empty())
		vk_group_screen_name = getOptionalEnvVar("METASIBERIA_VK_GROUP_SCREEN_NAME");

	std::string vk_api_host = settings->getStringValue("vk/api_host", "api.vk.ru");
	if(vk_api_host.empty())
		vk_api_host = getOptionalEnvVar("METASIBERIA_VK_API_HOST");

	std::string vk_api_version = settings->getStringValue("vk/api_version", "5.199");
	if(vk_api_version.empty())
		vk_api_version = getOptionalEnvVar("METASIBERIA_VK_API_VERSION");

	if(vk_enabled)
	{
		if(vk_token.empty())
		{
			vk_missing_config = true;
		}
		else if(vk_group_id == 0 && vk_group_screen_name.empty())
		{
			vk_missing_config = true;
		}
		else
		{
			Reference<UploadPhotoToVKThread> thread = new UploadPhotoToVKThread();
			thread->message = buildVKMessage(*gui_client, settings, last_caption, username_for_server, in_parcel_id);
			thread->upload_image_jpeg_path = upload_image_jpeg_path;
			thread->access_token = vk_token;
			thread->group_id = vk_group_id;
			thread->group_screen_name = vk_group_screen_name;
			thread->api_host = vk_api_host;
			thread->api_version = vk_api_version;
			thread->out_msg_queue = &gui_client->msg_queue;

			upload_thread_manager.addThread(thread);
			started_any_external_upload = true;
		}
	}

	if(started_any_external_upload)
	{
		// If at least one destination started (e.g. Telegram), keep UX clean and don't spam notifications
		// about other destinations being unconfigured.
		return;
	}

	// If user requested external uploads but none were started (misconfiguration), don't fall back to legacy upload.
	if(any_external_requested)
	{
		// Provide a single actionable error message.
		if(telegram_missing_config && vk_missing_config)
			gui_client->showErrorNotification("Upload photo is not configured. Configure Telegram (METASIBERIA_TELEGRAM_BOT_TOKEN + METASIBERIA_TELEGRAM_PHOTO_CHAT_ID) and/or VK (METASIBERIA_VK_ACCESS_TOKEN).");
		else if(telegram_missing_config)
			gui_client->showErrorNotification("Telegram upload is not configured. Set telegram/bot_token + telegram/photo_upload_chat_id (or env vars METASIBERIA_TELEGRAM_BOT_TOKEN + METASIBERIA_TELEGRAM_PHOTO_CHAT_ID).");
		else if(vk_missing_config)
			gui_client->showErrorNotification("VK upload is not configured. Set vk/access_token (or env var METASIBERIA_VK_ACCESS_TOKEN).");
		return;
	}

	// Legacy upload-to-website path (Substrata protocol).  Kept for compatibility/tests.
	const std::string username = gui_client->ui_interface->getUsernameForDomain(gui_client->server_hostname);
	const std::string password = gui_client->ui_interface->getDecryptedPasswordForDomain(gui_client->server_hostname);

	Reference<UploadPhotoThread> thread = new UploadPhotoThread();
	thread->caption = last_caption.substr(0, 10000);
	thread->world_name = gui_client->server_worldname;
	thread->parcel_id = in_parcel_id;
	thread->cam_pos = gui_client->cam_controller.getPosition();
	thread->cam_angles = gui_client->cam_controller.getAngles();
	thread->upload_image_jpeg_path = upload_image_jpeg_path;
	thread->hostname = gui_client->server_hostname;
	thread->port = gui_client->server_port;
	thread->username = username;
	thread->password = password;
	thread->config = gui_client->client_tls_config;
	thread->out_msg_queue = &gui_client->msg_queue;

	upload_thread_manager.addThread(thread);
}


void PhotoModeSlider::setVisible(bool visible)
{
	label->setVisible(visible);
	slider->setVisible(visible);
	value_view->setVisible(visible);
}


void PhotoModeSlider::setValue(double value, GLUIRef gl_ui)
{
	slider->setValue(value);
}


void PhotoModeSlider::setValueNoEvent(double value, GLUIRef gl_ui)
{
	slider->setValueNoEvent(value);
}
