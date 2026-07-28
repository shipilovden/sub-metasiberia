/*=====================================================================
CulturalObjectEditor.h
----------------------
Copyright Glare Technologies Limited 2026 -
=====================================================================*/
#pragma once


#include "CulturalObjectSettings.h"
#include "../shared/WorldObject.h"
#include <QtWidgets/QWidget>
#include <map>


class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGridLayout;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QTabWidget;


class CulturalObjectEditor : public QWidget
{
	Q_OBJECT

public:
	explicit CulturalObjectEditor(QWidget* parent = 0);

	void setFromObject(const WorldObject& ob, bool ob_in_editing_users_world);
	void setTransformFromObject(const WorldObject& ob);
	void toObject(WorldObject& ob_out);
	void writeTransformMembersToObject(WorldObject& ob_out);
	void objectLastModifiedUpdated(const WorldObject& ob);
	void objectPickedUp();
	void objectDropped();
	void setControlsEnabled(bool enabled);
	void setControlsEditable(bool editable);
	bool posAndRot3DControlsEnabled() const;
	void setPosAndRot3DControlsEnabled(bool enabled);
	bool snapToGridChecked() const;
	double gridSpacing() const;

signals:
	void objectTransformChanged();
	void objectChanged();
	void posAndRot3DControlsToggled();
	void deleteObjectRequested();

private:
	QLineEdit* addLine(QGridLayout* grid, int& row, const char* key, const QString& label, const QString& placeholder = QString());
	QPlainTextEdit* addText(QGridLayout* grid, int& row, const char* key, const QString& label, int height = 74);
	QComboBox* addCombo(QGridLayout* grid, int& row, const char* key, const QString& label, const QStringList& values);
	QCheckBox* addCheck(QGridLayout* grid, int& row, const char* key, const QString& label);
	QDoubleSpinBox* addDouble(QGridLayout* grid, int& row, const char* key, const QString& label, double min, double max, double step, int decimals = 3);
	QWidget* makeTab(const QString& title, QGridLayout** grid_out);
	void connectChangeSignals();
	void setControlsFromSettings(const CulturalObjectSettings& settings);
	CulturalObjectSettings controlsToSettings() const;
	QString line(const char* key) const;
	QString text(const char* key) const;
	QString combo(const char* key) const;
	bool checked(const char* key) const;
	double number(const char* key) const;
	void setStatus(const QString& status, bool error = false);
	void importLocalJson();
	void showRawJson();

	UID editing_ob_uid;
	bool controls_editable;
	bool syncing;
	CulturalObjectSettings current_settings;
	QLabel* info_label;
	QLabel* status_label;
	QTabWidget* tabs;
	std::map<std::string, QLineEdit*> lines;
	std::map<std::string, QPlainTextEdit*> texts;
	std::map<std::string, QComboBox*> combos;
	std::map<std::string, QCheckBox*> checks;
	std::map<std::string, QDoubleSpinBox*> doubles;

	QCheckBox* show_3d_controls;
	QCheckBox* snap_to_grid;
	QDoubleSpinBox* grid_spacing;
	QDoubleSpinBox* pos_x;
	QDoubleSpinBox* pos_y;
	QDoubleSpinBox* pos_z;
	QDoubleSpinBox* scale_x;
	QDoubleSpinBox* scale_y;
	QDoubleSpinBox* scale_z;
	QDoubleSpinBox* rot_x;
	QDoubleSpinBox* rot_y;
	QDoubleSpinBox* rot_z;
};
