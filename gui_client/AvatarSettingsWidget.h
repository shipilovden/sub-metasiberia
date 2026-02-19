#pragma once

#include "ui_AvatarSettingsWidget.h"

#include "../shared/WorldMaterial.h"
#include <graphics/BatchedMesh.h>
#include <utils/Reference.h>
#include <maths/Matrix4f.h>

#include <QtWidgets/QWidget>
#include <QtCore/QTimer>
#include <functional>

class GUIClient;
class AnimationManager;
class ResourceManager;
class TextureServer;
class VRoidAuthFlow;
namespace Indigo { class Mesh; }
struct GLObject;


// Dock-friendly avatar settings UI (Qt). Replaces the old modal AvatarSettingsDialog workflow.
class AvatarSettingsWidget : public QWidget, private Ui_AvatarSettingsWidget
{
	Q_OBJECT

public:
	AvatarSettingsWidget(
		QWidget* parent,
		const std::string& base_dir_path_,
		QSettings* settings_,
		Reference<ResourceManager> resource_manager_,
		AnimationManager* anim_manager_,
		GUIClient* gui_client_,
		std::function<void()> make_main_gl_context_current_
	);

	~AvatarSettingsWidget();

	void shutdownGL();

signals:
	void requestClose();

private slots:
	void avatarFilenameChanged(QString& filename);
	void animationComboBoxIndexChanged(int index);
	void onApplyClicked();
	void onCloseClicked();
	void onTick();

private:
	void loadModelIntoPreview(const std::string& local_path, bool show_error_dialogs);

	std::string base_dir_path;
	QSettings* settings;
	Reference<ResourceManager> resource_manager;
	AnimationManager* anim_manager;
	GUIClient* gui_client;
	std::function<void()> make_main_gl_context_current;

	Reference<TextureServer> texture_server;
	Reference<GLObject> preview_gl_ob;
	VRoidAuthFlow* vroid_auth_flow;

	bool done_initial_load;
	QTimer tick_timer;

public:
	// Last successfully loaded model data (used for Apply).
	BatchedMeshRef loaded_mesh;
	std::vector<WorldMaterialRef> loaded_materials;
	std::string result_path;
	Matrix4f pre_ob_to_world_matrix;
};
