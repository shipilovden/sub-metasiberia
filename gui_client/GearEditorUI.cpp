/*=====================================================================
GearEditorUI.cpp
----------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#include "GearEditorUI.h"


#include "GUIClient.h"
#include <maths/matrix3.h>
#include <maths/mathstypes.h>
#include <utils/StringUtils.h>


namespace
{
static double parseDoubleOrKeep(const GLUILineEditRef& edit, double fallback)
{
	try
	{
		return stringToDouble(edit->getText());
	}
	catch(glare::Exception&)
	{
		return fallback;
	}
}


static GLUILineEditRef makeLineEdit(GLUIRef gl_ui, Reference<OpenGLEngine> opengl_engine, const std::string& value)
{
	GLUILineEdit::CreateArgs args;
	args.width = gl_ui->getUIWidthForDevIndepPixelWidth(180.f);
	args.padding_px = 6;
	args.font_size_px = 13;
	args.z = -0.21f;
	GLUILineEditRef edit = new GLUILineEdit(*gl_ui, opengl_engine, Vec2f(0.f), args);
	edit->setText(*gl_ui, value);
	return edit;
}
}


GearEditorUI::GearEditorUI(GUIClient* gui_client_, GLUIRef gl_ui_, GearItemRef gear_item_)
:	gui_client(gui_client_),
	gl_ui(gl_ui_),
	gear_item(gear_item_),
	close_soon(false)
{
	opengl_engine = gui_client_->opengl_engine;

	GLUIGridContainer::CreateArgs grid_args;
	grid_args.background_alpha = 0.f;
	grid_args.cell_x_padding_px = 6;
	grid_args.cell_y_padding_px = 4;
	controls_grid = new GLUIGridContainer(*gl_ui, opengl_engine, grid_args);

	int row = 0;
	addLabel(row++, "Numeric gear editor for this branch");
	addLineEdit(row++, "Name", "", name_line_edit);
	addLineEdit(row++, "Description", "", desc_line_edit);
	addLineEdit(row++, "Bone", "", bone_line_edit);
	addLineEdit(row++, "Position X", "0", pos_x_line_edit);
	addLineEdit(row++, "Position Y", "0", pos_y_line_edit);
	addLineEdit(row++, "Position Z", "0", pos_z_line_edit);
	addLineEdit(row++, "Rotation X deg", "0", rot_x_line_edit);
	addLineEdit(row++, "Rotation Y deg", "0", rot_y_line_edit);
	addLineEdit(row++, "Rotation Z deg", "0", rot_z_line_edit);
	addLineEdit(row++, "Scale X", "1", scale_x_line_edit);
	addLineEdit(row++, "Scale Y", "1", scale_y_line_edit);
	addLineEdit(row++, "Scale Z", "1", scale_z_line_edit);

	GLUITextButton::CreateArgs apply_args;
	apply_args.z = -0.21f;
	apply_button = new GLUITextButton(*gl_ui, opengl_engine, "Apply", Vec2f(0.f), apply_args);
	apply_button->handler = this;
	controls_grid->setCellWidget(/*x=*/0, /*y=*/row, apply_button);

	GLUIWindow::CreateArgs window_args;
	window_args.title = "Gear Editor";
	window_args.background_alpha = 0.94f;
	window_args.background_colour = Colour3f(0.11f);
	window_args.padding_px = 10;
	window_args.z = -0.3f;
	window = new GLUIWindow(*gl_ui, opengl_engine, window_args);
	window->setBodyWidget(controls_grid);
	window->handler = this;
	gl_ui->addWidget(window);

	auto bind_enter = [this](GLUILineEditRef& edit)
	{
		edit->on_enter_pressed = [this]() { this->gearItemChanged(); };
	};
	bind_enter(name_line_edit);
	bind_enter(desc_line_edit);
	bind_enter(bone_line_edit);
	bind_enter(pos_x_line_edit);
	bind_enter(pos_y_line_edit);
	bind_enter(pos_z_line_edit);
	bind_enter(rot_x_line_edit);
	bind_enter(rot_y_line_edit);
	bind_enter(rot_z_line_edit);
	bind_enter(scale_x_line_edit);
	bind_enter(scale_y_line_edit);
	bind_enter(scale_z_line_edit);

	refreshFieldsFromGearItem();
	updateWidgetPositions();
}


GearEditorUI::~GearEditorUI()
{
	if(window)
		window->removeAllContainedWidgetsFromGLUIAndClear();

	checkRemoveAndDeleteWidget(gl_ui, window);
}


void GearEditorUI::think()
{
}


void GearEditorUI::viewportResized(int /*w*/, int /*h*/)
{
	updateWidgetPositions();
}


void GearEditorUI::setAvatarGLObject(const Reference<GLObject>& /*avatar_gl_ob*/, const Matrix4f& /*pre_ob_to_world_matrix*/)
{
}


void GearEditorUI::eventOccurred(GLUICallbackEvent& event)
{
	if(event.widget == apply_button.ptr())
	{
		gearItemChanged();
		event.accepted = true;
	}
}


void GearEditorUI::closeWindowEventOccurred(GLUICallbackEvent& /*event*/)
{
	close_soon = true;
}


void GearEditorUI::updateWidgetPositions()
{
	if(window.isNull())
		return;

	window->recomputeLayout();

	const float title_bar_h = gl_ui->getUIWidthForDevIndepPixelWidth(28.f);
	const float padding = gl_ui->getUIWidthForDevIndepPixelWidth(10.f);
	const Vec2f body_dims = controls_grid->getDims();
	const Vec2f window_dims(body_dims.x + padding * 2.f, body_dims.y + title_bar_h + padding * 2.f);

	const float left_margin = myMax(0.f, (2.f - window_dims.x) * 0.5f);
	const float bot_margin = myMax(0.f, (gl_ui->getViewportMinMaxY() * 2.f - window_dims.y) * 0.5f);
	window->setPosAndDims(Vec2f(-1.f + left_margin, -gl_ui->getViewportMinMaxY() + bot_margin), window_dims);
}


void GearEditorUI::handleUploadedTexture(const OpenGLTextureKey& /*path*/, const URLString& /*URL*/, const OpenGLTextureRef& /*opengl_tex*/)
{
}


void GearEditorUI::addLabel(int row, const std::string& text)
{
	GLUITextView::CreateArgs args;
	args.background_alpha = 0.f;
	args.font_size_px = 14;
	args.max_width = gl_ui->getUIWidthForDevIndepPixelWidth(280.f);
	GLUITextViewRef label = new GLUITextView(*gl_ui, opengl_engine, text, Vec2f(0.f), args);
	controls_grid->setCellWidget(/*x=*/0, /*y=*/row, label);
}


void GearEditorUI::addLineEdit(int row, const std::string& label, const std::string& value, GLUILineEditRef& edit_out)
{
	GLUITextView::CreateArgs label_args;
	label_args.background_alpha = 0.f;
	label_args.font_size_px = 13;
	GLUITextViewRef label_widget = new GLUITextView(*gl_ui, opengl_engine, label, Vec2f(0.f), label_args);
	controls_grid->setCellWidget(/*x=*/0, /*y=*/row, label_widget);

	edit_out = makeLineEdit(gl_ui, opengl_engine, value);
	controls_grid->setCellWidget(/*x=*/1, /*y=*/row, edit_out);
}


void GearEditorUI::refreshFieldsFromGearItem()
{
	name_line_edit->setText(*gl_ui, gear_item->name);
	desc_line_edit->setText(*gl_ui, gear_item->description);
	bone_line_edit->setText(*gl_ui, gear_item->bone_name);
	pos_x_line_edit->setText(*gl_ui, doubleToStringNDecimalPlaces(gear_item->translation.x, 4));
	pos_y_line_edit->setText(*gl_ui, doubleToStringNDecimalPlaces(gear_item->translation.y, 4));
	pos_z_line_edit->setText(*gl_ui, doubleToStringNDecimalPlaces(gear_item->translation.z, 4));
	scale_x_line_edit->setText(*gl_ui, doubleToStringNDecimalPlaces(gear_item->scale.x, 4));
	scale_y_line_edit->setText(*gl_ui, doubleToStringNDecimalPlaces(gear_item->scale.y, 4));
	scale_z_line_edit->setText(*gl_ui, doubleToStringNDecimalPlaces(gear_item->scale.z, 4));

	Vec3f angles(0.f);
	if(gear_item->axis.length2() > 1.0e-12f)
	{
		const Matrix3f rot_mat = Matrix3f::rotationMatrix(normalise(gear_item->axis), gear_item->angle);
		angles = rot_mat.getAngles();
	}

	rot_x_line_edit->setText(*gl_ui, doubleToStringNDecimalPlaces(radToDegree(angles.x), 3));
	rot_y_line_edit->setText(*gl_ui, doubleToStringNDecimalPlaces(radToDegree(angles.y), 3));
	rot_z_line_edit->setText(*gl_ui, doubleToStringNDecimalPlaces(radToDegree(angles.z), 3));
}


void GearEditorUI::gearItemChanged()
{
	gear_item->name = name_line_edit->getText();
	gear_item->description = desc_line_edit->getText();
	gear_item->bone_name = bone_line_edit->getText();

	gear_item->translation = Vec3f(
		(float)parseDoubleOrKeep(pos_x_line_edit, gear_item->translation.x),
		(float)parseDoubleOrKeep(pos_y_line_edit, gear_item->translation.y),
		(float)parseDoubleOrKeep(pos_z_line_edit, gear_item->translation.z)
	);
	gear_item->scale = Vec3f(
		(float)parseDoubleOrKeep(scale_x_line_edit, gear_item->scale.x),
		(float)parseDoubleOrKeep(scale_y_line_edit, gear_item->scale.y),
		(float)parseDoubleOrKeep(scale_z_line_edit, gear_item->scale.z)
	);

	const Vec3f angles(
		(float)degreeToRad(parseDoubleOrKeep(rot_x_line_edit, 0.0)),
		(float)degreeToRad(parseDoubleOrKeep(rot_y_line_edit, 0.0)),
		(float)degreeToRad(parseDoubleOrKeep(rot_z_line_edit, 0.0))
	);

	const Matrix3f rot_matrix = Matrix3f::fromAngles(angles);
	rot_matrix.rotationMatrixToAxisAngle(gear_item->axis, gear_item->angle);
	if(gear_item->axis.length2() < 1.0e-12f)
	{
		gear_item->axis = Vec3f(0, 0, 1);
		gear_item->angle = 0;
	}

	gui_client->gearItemChangedOnOurAvatar(gear_item.ptr());
	refreshFieldsFromGearItem();
}
