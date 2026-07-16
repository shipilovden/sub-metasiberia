/*=====================================================================
GearInventoryPanel.h
--------------------
Native Qt gear inventory/editor dock for Metasiberia.
=====================================================================*/
#pragma once


#include "../shared/GearItem.h"
#include <QtCore/QString>
#include <QtWidgets/QWidget>
#include <functional>
#include <string>


class AnimationManager;
class AvatarGearPreviewWidget;
class GUIClient;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QCheckBox;
class QLineEdit;
class QPushButton;
class QSettings;
class QTabWidget;
class QToolButton;
class QVBoxLayout;
class ResourceManager;


class GearInventoryPanel final : public QWidget
{
public:
	explicit GearInventoryPanel(QWidget* parent = nullptr);
	~GearInventoryPanel();

	void initPreview(const std::string& base_dir_path, QSettings* settings, Reference<ResourceManager> resource_manager,
		AnimationManager* animation_manager, std::function<void()> restore_main_context);
	void setClient(GUIClient* client);
	void setIconDirectory(const QString& directory);
	void refreshFromClient();
	void shutdownPreview();

private:
	enum TransformMode { Mode_Orbit, Mode_Move, Mode_Rotate, Mode_Scale };
	enum TransformAxis { Axis_All, Axis_X, Axis_Y, Axis_Z };

	void rebuildCards();
	void clearCardLayout(QVBoxLayout* layout);
	QWidget* makeCard(const GearItemRef& item, bool equipped);
	void selectItem(const GearItemRef& item);
	void updateEditorFromSelection();
	void updateSelectionFromEditor(bool send_to_server);
	void applyPreviewDrag(const QPoint& total_delta, bool finished);
	bool selectedItemIsEquipped() const;
	bool canSynchroniseInventory() const;
	void setTransformMode(TransformMode mode);
	void setTransformAxis(TransformAxis axis);
	void updateToolButtonStates();
	QString previewPathForItem(const GearItem& item) const;
	QString boneDisplayName(const QString& bone_name) const;
	QString boneDataName() const;
	void setHoverHelp(QWidget* widget, const QString& text);

	GUIClient* gui_client;
	QString icon_directory;
	GearItemRef selected_item;
	TransformMode transform_mode;
	TransformAxis transform_axis;
	Vec3f drag_start_translation;
	Vec3f drag_start_scale;
	Vec3f drag_start_axis;
	float drag_start_angle;
	bool updating_editor;

	AvatarGearPreviewWidget* preview;
	QLabel* status_label;
	QLabel* selection_label;
	QTabWidget* tabs;
	QVBoxLayout* equipped_cards_layout;
	QVBoxLayout* all_cards_layout;
	QComboBox* bone_combo;
	QLineEdit* name_edit;
	QDoubleSpinBox* translation_spins[3];
	QDoubleSpinBox* rotation_axis_spins[3];
	QDoubleSpinBox* rotation_angle_spin;
	QDoubleSpinBox* scale_spins[3];
	QToolButton* orbit_button;
	QToolButton* refresh_button;
	QToolButton* move_button;
	QToolButton* rotate_button;
	QToolButton* scale_button;
	QToolButton* axis_buttons[4];
	QPushButton* apply_button;
	QPushButton* delete_button;
	QCheckBox* gizmo_button;
};
