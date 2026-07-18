/*=====================================================================
AvatarGearPreviewWidget.h
-------------------------
Independent avatar-and-gear preview using the same renderer as the
avatar settings preview.
=====================================================================*/
#pragma once


#include "AvatarPreviewWidget.h"
#include "../shared/Avatar.h"
#include <QtCore/QElapsedTimer>
#include <QtCore/QString>
#include <QtCore/QTimer>
#include <functional>
#include <vector>


class AnimationManager;
class QHideEvent;
class QMouseEvent;
class QPaintEvent;
class QResizeEvent;
class QSettings;
class QShowEvent;
class QWheelEvent;
class ResourceManager;
class TextureServer;


class AvatarGearPreviewWidget final : public AvatarPreviewWidget
{
public:
	enum InteractionMode
	{
		Interaction_Orbit,
		Interaction_Move,
		Interaction_Rotate,
		Interaction_Scale
	};

	enum TransformAxis
	{
		Axis_All,
		Axis_X,
		Axis_Y,
		Axis_Z
	};

	using RestoreMainContextCallback = std::function<void()>;
	using TransformDragCallback = std::function<void(InteractionMode, TransformAxis, const QPoint&, bool)>;
	using TransformHandleClickCallback = std::function<void(InteractionMode, TransformAxis)>;
	using GearSelectionCallback = std::function<void(const UID&)>;

	explicit AvatarGearPreviewWidget(QWidget* parent = nullptr);
	~AvatarGearPreviewWidget();

	void init(
		const std::string& base_dir_path,
		QSettings* settings,
		Reference<ResourceManager> resource_manager,
		AnimationManager* animation_manager,
		RestoreMainContextCallback restore_main_context
	);

	// Uses the server-provided avatar and gear materials.  Model resources are
	// resolved as LOD0 optimised URLs first, with the original URL as fallback.
	void setPreviewData(
		const AvatarSettings& avatar_settings,
		const GearItems& equipped_gear,
		bool use_basis_textures,
		bool use_optimised_meshes,
		int optimised_mesh_version
	);

	// Call after one or more entries returned by missingResourceURLs() have
	// arrived in ResourceManager.  The widget also performs a rate-limited retry
	// while it is visible, so a missed notification cannot leave it stale.
	void resourcesChanged();

	void setSelectedGear(const UID& gear_id);
	void updateGearItemData(const GearItem& gear_item);
	void setInteractionMode(InteractionMode mode, TransformAxis axis);
	void setGizmoVisible(bool visible);
	void setTransformDragCallback(TransformDragCallback callback);
	void setTransformHandleClickCallback(TransformHandleClickCallback callback);
	void setGearSelectionCallback(GearSelectionCallback callback);

	void shutdown();

	bool hasRenderableAvatar() const { return avatar_gl_ob.nonNull() && avatar_added_to_engine; }
	const QString& lastError() const { return last_error; }
	const std::vector<URLString>& missingResourceURLs() const { return missing_resource_urls; }
	bool hasHeightForWidth() const override { return true; }
	int heightForWidth(int width) const override;
	QSize sizeHint() const override;

protected:
	void paintEvent(QPaintEvent* event) override;
	void resizeEvent(QResizeEvent* event) override;
	void paintGL() override;
	void mousePressEvent(QMouseEvent* event) override;
	void mouseDoubleClickEvent(QMouseEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;
	void mouseReleaseEvent(QMouseEvent* event) override;
	void wheelEvent(QWheelEvent* event) override;
	void showEvent(QShowEvent* event) override;
	void hideEvent(QHideEvent* event) override;

private:
	struct PreviewGear
	{
		GearItemRef item;
		GLObjectRef gl_ob;
		std::string bone_name;
		int bone_node_i = -1;
		bool added_to_engine = false;
	};
	struct GizmoSegment
	{
		Vec4f a;
		Vec4f b;
	};

	void renderTick();
	bool anyMissingResourceIsAvailable() const;
	void rebuildSceneIfNeeded();
	void clearPreviewObjects();
	void updateGearAttachmentTransforms();
	PreviewGear* selectedPreviewGear();
	const PreviewGear* selectedPreviewGear() const;
	void ensureGizmoObjects();
	void updateGizmoObjects();
	void removeGizmoObjects();
	void destroyGizmoObjects();
	int gizmoHandleAt(const QPoint& pixel) const;
	bool gearAt(const QPoint& pixel, UID& gear_id_out) const;
	void updateGizmoColours(int hovered_handle);

	std::string resolveModelPath(const URLString& base_url, URLString& desired_url_out);
	std::string resolveResourcePath(const URLString& preferred_url, const URLString& fallback_url);
	void addMissingResourceURL(const URLString& url);
	void applyWorldMaterials(GLObject& ob, const std::vector<WorldMaterialRef>& materials);
	void loadObjectTextures(GLObject& ob);

	std::string base_dir_path;
	Reference<ResourceManager> resource_manager;
	AnimationManager* animation_manager;
	RestoreMainContextCallback restore_main_context;
	TransformDragCallback transform_drag_callback;
	TransformHandleClickCallback transform_handle_click_callback;
	GearSelectionCallback gear_selection_callback;

	Reference<TextureServer> owned_texture_server;
	AvatarSettings preview_avatar_settings;
	GearItems preview_equipped_gear;
	GLObjectRef avatar_gl_ob;
	bool avatar_added_to_engine;
	std::vector<PreviewGear> preview_gear;

	UID selected_gear_id;
	InteractionMode interaction_mode;
	TransformAxis transform_axis;
	QPoint transform_drag_origin;
	bool transform_dragging;

	bool initialised;
	bool shutting_down;
	bool content_dirty;
	bool waiting_for_resources;
	bool use_basis_textures;
	bool use_optimised_meshes;
	int optimised_mesh_version;

	QString last_error;
	std::vector<URLString> missing_resource_urls;
	QTimer render_timer;
	QElapsedTimer resource_retry_timer;
	bool gizmo_visible;
	GLObjectRef gizmo_axis_objects[3];
	GLObjectRef gizmo_rotation_objects[3];
	GizmoSegment gizmo_axis_segments[3];
	std::vector<GizmoSegment> gizmo_rotation_segments[3];
	int hovered_gizmo_handle;
	int applied_gizmo_hover;
	int grabbed_gizmo_handle;
};
