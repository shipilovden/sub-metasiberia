/*=====================================================================
GearInventoryUI.cpp
-------------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#include "GearInventoryUI.h"


#include "AvatarPreviewGLUIWidget.h"
#include "GUIClient.h"
#include "../shared/ResourceManager.h"
#include <opengl/ui/GLUIInertWidget.h>
#include <opengl/OpenGLEngine.h>
#include <opengl/FrameBuffer.h>
#include <graphics/SRGBUtils.h>


namespace
{
static const float THUMBNAIL_SIZE_PX = 80.f;
static const int GEAR_GRID_COLS = 4;
static const int GEAR_GRID_ROWS = 6;
static const float INTERIOR_GRID_PADDING_PX = 6.f;
static const float OUTER_GRID_PADDING_PX = 22.f;
static const float DUMMY_ITEM_ALPHA = 0.5f;
static const Colour3f DUMMY_ITEM_COLOUR = Colour3f(0.3f);


static GLUITextViewRef makeHeader(GLUIRef gl_ui, Reference<OpenGLEngine> opengl_engine, const std::string& text)
{
	GLUITextView::CreateArgs args;
	args.background_alpha = 0.f;
	args.font_size_px = 16;
	return new GLUITextView(*gl_ui, opengl_engine, text, Vec2f(0.f), args);
}


static std::string thumbnailPathForGearItem(GUIClient* gui_client, const GearItem& item)
{
	if(!item.preview_image_URL.empty())
	{
		if(gui_client->resource_manager->isFileForURLPresent(item.preview_image_URL))
			return gui_client->resource_manager->pathForURL(item.preview_image_URL);

		DownloadingResourceInfo info;
		info.pos = gui_client->cam_controller.getFirstPersonPosition();
		info.size_factor = 1.f;
		info.used_by_other = true;
		gui_client->startDownloadingResource(item.preview_image_URL, gui_client->cam_controller.getFirstPersonPosition().toVec4fPoint(), 1.f, info);
	}

	return gui_client->resources_dir_path + "/dot.png";
}
}


GearInventoryUI::GearInventoryUI(GUIClient* gui_client_, GLUIRef gl_ui_)
:	gui_client(gui_client_),
	gl_ui(gl_ui_),
	close_soon(false),
	need_rebuild_equipped(false),
	need_rebuild_all_gear(false),
	avatar_pre_ob_to_world_matrix(Matrix4f::identity())
{
	opengl_engine = gui_client_->opengl_engine;
	main_world_scene = opengl_engine->getCurrentScene();

	GLUIGridContainer::CreateArgs outer_args;
	outer_args.background_alpha = 0.f;
	outer_args.cell_x_padding_px = OUTER_GRID_PADDING_PX;
	outer_args.cell_y_padding_px = OUTER_GRID_PADDING_PX * 0.75f;
	outer_grid = new GLUIGridContainer(*gl_ui, opengl_engine, outer_args);

	outer_grid->setCellWidget(/*x=*/0, /*y=*/1, makeHeader(gl_ui, opengl_engine, "Preview"));
	outer_grid->setCellWidget(/*x=*/1, /*y=*/1, makeHeader(gl_ui, opengl_engine, "Equipped Gear"));
	outer_grid->setCellWidget(/*x=*/2, /*y=*/1, makeHeader(gl_ui, opengl_engine, "All Gear"));

	avatar_preview_scene = new OpenGLScene(*opengl_engine);
	avatar_preview_scene->draw_water = false;
	avatar_preview_scene->water_level_z = -10000.0;
	avatar_preview_scene->background_colour = Colour3f(0.2f);
	opengl_engine->addScene(avatar_preview_scene);

	{
		OpenGLSceneRef old_scene = opengl_engine->getCurrentScene();
		opengl_engine->setCurrentScene(avatar_preview_scene);

		OpenGLMaterial env_mat;
		opengl_engine->setEnvMat(env_mat);
		opengl_engine->setSunDir(normalise(Vec4f(1, -1, 1, 0)));

		const float W = 200.f;
		GLObjectRef ob = opengl_engine->allocateObject();
		ob->materials.resize(1);
		ob->materials[0].albedo_linear_rgb = toLinearSRGB(Colour3f(0.8f));
		try
		{
			ob->materials[0].albedo_texture = opengl_engine->getTexture(gui_client->base_dir_path + "/data/resources/obstacle.png");
		}
		catch(glare::Exception& e)
		{
			conPrint("ERROR: " + e.what());
		}
		ob->materials[0].roughness = 0.8f;
		ob->materials[0].fresnel_scale = 0.5f;
		ob->materials[0].tex_matrix = Matrix2f(W, 0, 0, W);
		ob->ob_to_world_matrix = Matrix4f::scaleMatrix(W, W, 1) * Matrix4f::translationMatrix(-0.5f, -0.5f, 0);
		ob->mesh_data = opengl_engine->getUnitQuadMeshData();
		opengl_engine->addObject(ob);

		opengl_engine->setCurrentScene(main_world_scene.nonNull() ? main_world_scene : old_scene);
	}

	avatar_preview_widget = new AvatarPreviewGLUIWidget(*gl_ui);
	outer_grid->setCellWidget(/*x=*/0, /*y=*/0, avatar_preview_widget);

	GLUIGridContainer::CreateArgs grid_args;
	grid_args.background_alpha = 0.f;
	grid_args.cell_x_padding_px = INTERIOR_GRID_PADDING_PX;
	grid_args.cell_y_padding_px = INTERIOR_GRID_PADDING_PX;
	equipped_grid = new GLUIGridContainer(*gl_ui, opengl_engine, grid_args);
	all_gear_grid = new GLUIGridContainer(*gl_ui, opengl_engine, grid_args);
	outer_grid->setCellWidget(/*x=*/1, /*y=*/0, equipped_grid);
	outer_grid->setCellWidget(/*x=*/2, /*y=*/0, all_gear_grid);

	GLUIWindow::CreateArgs window_args;
	window_args.title = "Gear Inventory";
	window_args.background_alpha = 0.9f;
	window_args.background_colour = Colour3f(0.1f);
	window_args.padding_px = 0;
	window_args.z = -0.2f;
	window = new GLUIWindow(*gl_ui, opengl_engine, window_args);
	window->setBodyWidget(outer_grid);
	window->handler = this;
	gl_ui->addWidget(window);

	recreateAvatarPreviewFBO();
	rebuildEquippedGrid();
	rebuildAllGearGrid();
	updateWidgetPositions();
}


GearInventoryUI::~GearInventoryUI()
{
	gear_editor_ui = nullptr;

	if(avatar_preview_scene.nonNull())
	{
		OpenGLSceneRef old_scene = opengl_engine->getCurrentScene();
		opengl_engine->setCurrentScene(avatar_preview_scene);

		checkRemoveObAndSetRefToNull(*opengl_engine, avatar_preview_gl_ob);

		opengl_engine->setCurrentScene(main_world_scene.nonNull() ? main_world_scene : old_scene);
		opengl_engine->removeScene(avatar_preview_scene);
	}

	if(window)
		window->removeAllContainedWidgetsFromGLUIAndClear();

	equipped_gear_ui.clear();
	all_gear_ui.clear();

	checkRemoveAndDeleteWidget(gl_ui, window);
}


void GearInventoryUI::think()
{
	if(need_rebuild_equipped)
	{
		rebuildEquippedGrid();
		need_rebuild_equipped = false;
	}

	if(need_rebuild_all_gear)
	{
		rebuildAllGearGrid();
		need_rebuild_all_gear = false;
	}

	if(gear_editor_ui)
	{
		gear_editor_ui->think();
		if(gear_editor_ui->close_soon)
			gear_editor_ui = nullptr;
	}

	if(!gear_editor_ui)
		renderAvatarPreview();

	if(main_world_scene.nonNull() && opengl_engine->getCurrentScene() != main_world_scene.ptr())
		opengl_engine->setCurrentScene(main_world_scene);
}


void GearInventoryUI::viewportResized(int /*w*/, int /*h*/)
{
	recreateAvatarPreviewFBO();
	updateWidgetPositions();

	if(gear_editor_ui)
		gear_editor_ui->viewportResized(0, 0);
}


void GearInventoryUI::keyPressed(KeyEvent& e)
{
	if(e.key == Key::Key_Escape)
	{
		if(gear_editor_ui)
			gear_editor_ui->close_soon = true;
		else
			close_soon = true;

		e.accepted = true;
		return;
	}

	if(e.key == Key::Key_E)
	{
		const Vec2f mouse_pos = gl_ui->getLastMouseUICoords();

		for(size_t i = 0; i < equipped_gear_ui.size(); ++i)
		{
			if(equipped_gear_ui[i].thumbnail->getRect().inOpenRectangle(mouse_pos))
			{
				openEditorForItem(equipped_gear_ui[i].gear_item);
				e.accepted = true;
				return;
			}
		}

		for(size_t i = 0; i < all_gear_ui.size(); ++i)
		{
			if(all_gear_ui[i].thumbnail->getRect().inOpenRectangle(mouse_pos))
			{
				openEditorForItem(all_gear_ui[i].gear_item);
				e.accepted = true;
				return;
			}
		}

		// Consume E while cursor is over the inventory so world-use action does not fire.
		if(window->getRect().inOpenRectangle(mouse_pos))
			e.accepted = true;
	}
}


void GearInventoryUI::setEquippedGear(const GearItems& equipped_gear_)
{
	equipped_gear = equipped_gear_;
	need_rebuild_equipped = true;
}


void GearInventoryUI::setAllGear(const GearItems& all_gear_)
{
	all_gear = all_gear_;
	need_rebuild_all_gear = true;
}


void GearInventoryUI::setAvatarGLObject(const AvatarGraphics& /*graphics*/, const Reference<GLObject>& avatar_gl_ob, const Matrix4f& pre_ob_to_world_matrix)
{
	avatar_pre_ob_to_world_matrix = pre_ob_to_world_matrix;

	if(avatar_preview_scene.isNull())
		return;

	OpenGLSceneRef old_scene = opengl_engine->getCurrentScene();
	opengl_engine->setCurrentScene(avatar_preview_scene);

	checkRemoveObAndSetRefToNull(*opengl_engine, avatar_preview_gl_ob);

	if(avatar_gl_ob.nonNull())
	{
		avatar_preview_gl_ob = opengl_engine->allocateObject();
		avatar_preview_gl_ob->mesh_data = avatar_gl_ob->mesh_data;
		avatar_preview_gl_ob->materials = avatar_gl_ob->materials;

		// Use the runtime avatar transform (correct model scale/orientation), but keep it centred in preview X/Y.
		Matrix4f preview_ob_to_world = avatar_gl_ob->ob_to_world_matrix;
		const Vec4f preview_translation = preview_ob_to_world.getColumn(3);
		preview_ob_to_world.setColumn(3, Vec4f(0.f, 0.f, preview_translation[2], 1.f));
		avatar_preview_gl_ob->ob_to_world_matrix = preview_ob_to_world;

		opengl_engine->addObject(avatar_preview_gl_ob);
		opengl_engine->updateObjectTransformData(*avatar_preview_gl_ob);

		if(avatar_preview_widget.nonNull())
		{
			avatar_preview_widget->resetCameraFromAvatarTransform(avatar_preview_gl_ob->ob_to_world_matrix);
			avatar_preview_widget->fitCameraToAABB(avatar_preview_gl_ob->aabb_ws);
		}
	}

	opengl_engine->setCurrentScene(main_world_scene.nonNull() ? main_world_scene : old_scene);

	if(gear_editor_ui)
		gear_editor_ui->setAvatarGLObject(avatar_preview_gl_ob, avatar_pre_ob_to_world_matrix);
}


void GearInventoryUI::eventOccurred(GLUICallbackEvent& event)
{
	for(size_t i = 0; i < equipped_gear_ui.size(); ++i)
	{
		if(event.widget == equipped_gear_ui[i].thumbnail.ptr())
		{
			gui_client->equippedGearItemClicked(equipped_gear_ui[i].gear_item);
			event.accepted = true;
			return;
		}
	}

	for(size_t i = 0; i < all_gear_ui.size(); ++i)
	{
		if(event.widget == all_gear_ui[i].thumbnail.ptr())
		{
			gui_client->gearItemClicked(all_gear_ui[i].gear_item);
			event.accepted = true;
			return;
		}
	}
}


void GearInventoryUI::closeWindowEventOccurred(GLUICallbackEvent& /*event*/)
{
	close_soon = true;
}


void GearInventoryUI::updateWidgetPositions()
{
	if(window.isNull())
		return;

	window->recomputeLayout();

	const float title_bar_h = gl_ui->getUIWidthForDevIndepPixelWidth(28.f);
	const float padding = gl_ui->getUIWidthForDevIndepPixelWidth(0.f);
	const Vec2f body_dims = outer_grid->getDims();
	const Vec2f window_dims(body_dims.x + padding * 2.f, body_dims.y + title_bar_h + padding);

	const float left_margin = myMax(0.f, (2.f - window_dims.x) * 0.5f);
	const float bot_margin = myMax(0.f, (gl_ui->getViewportMinMaxY() * 2.f - window_dims.y) * 0.5f);
	window->setPosAndDims(Vec2f(-1.f + left_margin, -gl_ui->getViewportMinMaxY() + bot_margin), window_dims);
}


void GearInventoryUI::handleUploadedTexture(const OpenGLTextureKey& /*path*/, const URLString& URL, const OpenGLTextureRef& /*opengl_tex*/)
{
	for(size_t i = 0; i < equipped_gear.items.size(); ++i)
	{
		if(equipped_gear.items[i]->preview_image_URL == URL)
		{
			need_rebuild_equipped = true;
			break;
		}
	}

	for(size_t i = 0; i < all_gear.items.size(); ++i)
	{
		if(all_gear.items[i]->preview_image_URL == URL)
		{
			need_rebuild_all_gear = true;
			break;
		}
	}
}


void GearInventoryUI::rebuildEquippedGrid()
{
	equipped_grid->removeAllContainedWidgetsFromGLUIAndClear();
	equipped_gear_ui.clear();

	for(size_t i = 0; i < equipped_gear.items.size(); ++i)
	{
		GLUIButton::CreateArgs args;
		args.sizing_type_x = GLUIWidget::SizingType_FixedSizePx;
		args.sizing_type_y = GLUIWidget::SizingType_FixedSizePx;
		args.fixed_size = Vec2f(THUMBNAIL_SIZE_PX, THUMBNAIL_SIZE_PX);
		args.tooltip = "Click to unequip " + equipped_gear.items[i]->name;
		GLUIButtonRef button = new GLUIButton(*gl_ui, opengl_engine, thumbnailPathForGearItem(gui_client, *equipped_gear.items[i]), args);
		button->handler = this;
		equipped_grid->setCellWidget((int)(i % GEAR_GRID_COLS), (int)(GEAR_GRID_ROWS - 1 - (i / GEAR_GRID_COLS)), button);

		GearItemUI ui;
		ui.thumbnail = button;
		ui.gear_item = equipped_gear.items[i];
		equipped_gear_ui.push_back(ui);
	}

	for(size_t i = equipped_gear.items.size(); i < (size_t)(GEAR_GRID_COLS * GEAR_GRID_ROWS); ++i)
	{
		GLUIInertWidget::CreateArgs args;
		args.background_alpha = DUMMY_ITEM_ALPHA;
		args.background_colour = DUMMY_ITEM_COLOUR;
		GLUIInertWidgetRef dummy = new GLUIInertWidget(*gl_ui, opengl_engine, args);
		dummy->setFixedDimsPx(Vec2f(THUMBNAIL_SIZE_PX, THUMBNAIL_SIZE_PX), *gl_ui);
		equipped_grid->setCellWidget((int)(i % GEAR_GRID_COLS), (int)(GEAR_GRID_ROWS - 1 - (i / GEAR_GRID_COLS)), dummy);
	}

	updateWidgetPositions();
}


void GearInventoryUI::rebuildAllGearGrid()
{
	all_gear_grid->removeAllContainedWidgetsFromGLUIAndClear();
	all_gear_ui.clear();

	for(size_t i = 0; i < all_gear.items.size(); ++i)
	{
		GLUIButton::CreateArgs args;
		args.sizing_type_x = GLUIWidget::SizingType_FixedSizePx;
		args.sizing_type_y = GLUIWidget::SizingType_FixedSizePx;
		args.fixed_size = Vec2f(THUMBNAIL_SIZE_PX, THUMBNAIL_SIZE_PX);
		args.tooltip = "Click to equip " + all_gear.items[i]->name;
		GLUIButtonRef button = new GLUIButton(*gl_ui, opengl_engine, thumbnailPathForGearItem(gui_client, *all_gear.items[i]), args);
		button->handler = this;
		all_gear_grid->setCellWidget((int)(i % GEAR_GRID_COLS), (int)(GEAR_GRID_ROWS - 1 - (i / GEAR_GRID_COLS)), button);

		GearItemUI ui;
		ui.thumbnail = button;
		ui.gear_item = all_gear.items[i];
		all_gear_ui.push_back(ui);
	}

	for(size_t i = all_gear.items.size(); i < (size_t)(GEAR_GRID_COLS * GEAR_GRID_ROWS); ++i)
	{
		GLUIInertWidget::CreateArgs args;
		args.background_alpha = DUMMY_ITEM_ALPHA;
		args.background_colour = DUMMY_ITEM_COLOUR;
		GLUIInertWidgetRef dummy = new GLUIInertWidget(*gl_ui, opengl_engine, args);
		dummy->setFixedDimsPx(Vec2f(THUMBNAIL_SIZE_PX, THUMBNAIL_SIZE_PX), *gl_ui);
		all_gear_grid->setCellWidget((int)(i % GEAR_GRID_COLS), (int)(GEAR_GRID_ROWS - 1 - (i / GEAR_GRID_COLS)), dummy);
	}

	updateWidgetPositions();
}


void GearInventoryUI::openEditorForItem(const GearItemRef& item)
{
	if(gear_editor_ui)
		gear_editor_ui = nullptr;

	gear_editor_ui = new GearEditorUI(gui_client, gl_ui, item);
	gear_editor_ui->setAvatarGLObject(avatar_preview_gl_ob, avatar_pre_ob_to_world_matrix);
}


void GearInventoryUI::renderAvatarPreview()
{
	if(avatar_preview_scene.isNull() || avatar_preview_widget.isNull())
		return;

	OpenGLSceneRef old_scene = opengl_engine->getCurrentScene();
	Reference<FrameBuffer> old_fbo = opengl_engine->getTargetFrameBuffer();

	opengl_engine->setCurrentScene(avatar_preview_scene);
	avatar_preview_widget->renderAvatarPreview();

	opengl_engine->setCurrentScene(main_world_scene.nonNull() ? main_world_scene : old_scene);
	opengl_engine->setTargetFrameBuffer(old_fbo);
}


void GearInventoryUI::recreateAvatarPreviewFBO()
{
	if(avatar_preview_widget.isNull())
		return;

	const float preview_w_dev_ind_px = THUMBNAIL_SIZE_PX * GEAR_GRID_COLS + INTERIOR_GRID_PADDING_PX * 2.f * (GEAR_GRID_COLS - 1);
	const float preview_h_dev_ind_px = THUMBNAIL_SIZE_PX * GEAR_GRID_ROWS + INTERIOR_GRID_PADDING_PX * 2.f * (GEAR_GRID_ROWS - 1);

	const int avatar_preview_w = myMax(16, (int)(gl_ui->getDevicePixelRatio() * preview_w_dev_ind_px));
	const int avatar_preview_h = myMax(16, (int)(gl_ui->getDevicePixelRatio() * preview_h_dev_ind_px));

	avatar_preview_widget->recreateFBO(avatar_preview_w, avatar_preview_h);
	avatar_preview_widget->setFixedDimsPx(Vec2f(preview_w_dev_ind_px, preview_h_dev_ind_px), *gl_ui);

	if(avatar_preview_gl_ob.nonNull())
		avatar_preview_widget->fitCameraToAABB(avatar_preview_gl_ob->aabb_ws);
}
