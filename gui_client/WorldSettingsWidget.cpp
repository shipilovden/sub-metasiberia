/*=====================================================================
WorldSettingsWidget.cpp
-----------------------
Copyright Glare Technologies Limited 2023 -
=====================================================================*/
#include "WorldSettingsWidget.h"


#include "MainWindow.h"
#include "../qt/SignalBlocker.h"
#include "../shared/ResourceManager.h"
#include <qt/QtUtils.h>
#include <FileUtils.h>
#include <FileChecksum.h>
#include <QtCore/QSettings>
#include <QtCore/QEvent>
#include <QtCore/QObject>
#include <QtGui/QMouseEvent>
#include <QtWidgets/QMessageBox>
#include <algorithm>


WorldSettingsWidget::WorldSettingsWidget(QWidget* parent)
:	QWidget(parent),
	main_window(NULL),
	terrain_section_resize_drag_active(false),
	terrain_section_resize_drag_start_global_y(0),
	terrain_section_resize_drag_start_height(0)
{
	setupUi(this);

	connect(this->newTerrainSectionPushButton, SIGNAL(clicked()), this, SLOT(newTerrainSectionPushButtonClicked()));

	connect(this->applyPushButton, SIGNAL(clicked()), this, SLOT(applySettingsSlot()));

	this->waterZDoubleSpinBox->setMinimum(-std::numeric_limits<double>::infinity());
	this->waterZDoubleSpinBox->setMaximum( std::numeric_limits<double>::infinity());

	this->defaultTerrainZDoubleSpinBox->setMinimum(-std::numeric_limits<double>::infinity());
	this->defaultTerrainZDoubleSpinBox->setMaximum( std::numeric_limits<double>::infinity());

	connect(this->sunThetaSettingRealControl,   SIGNAL(valueChanged(double)), this, SLOT(settingsChangedSlot()));
	connect(this->sunPhiSettingRealControl,     SIGNAL(valueChanged(double)), this, SLOT(settingsChangedSlot()));
	connect(this->layer0ASpinBox,               SIGNAL(valueChanged(double)), this, SLOT(settingsChangedSlot()));
	connect(this->layer0HeightScaleSpinBox,     SIGNAL(valueChanged(double)), this, SLOT(settingsChangedSlot()));
	connect(this->layer1ASpinBox,               SIGNAL(valueChanged(double)), this, SLOT(settingsChangedSlot()));
	connect(this->layer1HeightScaleSpinBox,     SIGNAL(valueChanged(double)), this, SLOT(settingsChangedSlot()));

	terrainSectionScrollArea->setMouseTracking(true);
	terrainSectionScrollArea->viewport()->setMouseTracking(true);
	terrainSectionScrollArea->installEventFilter(this);
	terrainSectionScrollArea->viewport()->installEventFilter(this);
}


WorldSettingsWidget::~WorldSettingsWidget()
{}


void WorldSettingsWidget::init(MainWindow* main_window_)
{
	main_window = main_window_;

	updateControlsEditable();
}


void WorldSettingsWidget::retranslateUiText()
{
	retranslateUi(this);

	QLayout* sections_layout = terrainSectionScrollAreaWidgetContents->layout();
	if(!sections_layout)
		return;

	for(int i=0; i<sections_layout->count(); ++i)
	{
		if(QWidget* widget = sections_layout->itemAt(i)->widget())
		{
			TerrainSpecSectionWidget* section_widget = dynamic_cast<TerrainSpecSectionWidget*>(widget);
			if(section_widget)
				section_widget->retranslateUi(section_widget);
		}
	}
}


bool WorldSettingsWidget::shouldStartTerrainSectionResize(const QPoint& pos_in_scroll_area) const
{
	const int resize_hot_zone_px = 6;
	const int h = terrainSectionScrollArea->height();
	return (pos_in_scroll_area.y() >= h - resize_hot_zone_px) && (pos_in_scroll_area.y() <= h + resize_hot_zone_px);
}


void WorldSettingsWidget::setTerrainSectionAreaHeight(int target_height)
{
	const int min_height = 70;
	const int max_height = std::max(min_height, this->height() - 180);
	const int clamped_height = std::max(min_height, std::min(target_height, max_height));

	terrainSectionScrollArea->setMinimumHeight(clamped_height);
	terrainSectionScrollArea->setMaximumHeight(clamped_height);
}


bool WorldSettingsWidget::eventFilter(QObject* watched, QEvent* event)
{
	const bool relevant_object = (watched == terrainSectionScrollArea) || (watched == terrainSectionScrollArea->viewport());
	if(!relevant_object)
		return QWidget::eventFilter(watched, event);

	auto toScrollAreaPos = [&](const QPoint& pos_in_watched) -> QPoint
	{
		if(watched == terrainSectionScrollArea)
			return pos_in_watched;
		return terrainSectionScrollArea->viewport()->mapTo(terrainSectionScrollArea, pos_in_watched);
	};

	if(event->type() == QEvent::MouseButtonPress)
	{
		QMouseEvent* mouse_event = static_cast<QMouseEvent*>(event);
		if(mouse_event->button() == Qt::LeftButton)
		{
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
			const QPoint local_pos = mouse_event->position().toPoint();
			const int global_y = mouse_event->globalPosition().toPoint().y();
#else
			const QPoint local_pos = mouse_event->pos();
			const int global_y = mouse_event->globalY();
#endif
			if(shouldStartTerrainSectionResize(toScrollAreaPos(local_pos)))
			{
				terrain_section_resize_drag_active = true;
				terrain_section_resize_drag_start_global_y = global_y;
				terrain_section_resize_drag_start_height = terrainSectionScrollArea->height();
				terrainSectionScrollArea->setCursor(Qt::SizeVerCursor);
				return true;
			}
		}
	}
	else if(event->type() == QEvent::MouseMove)
	{
		QMouseEvent* mouse_event = static_cast<QMouseEvent*>(event);
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
		const QPoint local_pos = mouse_event->position().toPoint();
		const int global_y = mouse_event->globalPosition().toPoint().y();
#else
		const QPoint local_pos = mouse_event->pos();
		const int global_y = mouse_event->globalY();
#endif

		if(terrain_section_resize_drag_active)
		{
			const int delta_y = global_y - terrain_section_resize_drag_start_global_y;
			setTerrainSectionAreaHeight(terrain_section_resize_drag_start_height + delta_y);
			return true;
		}

		if(shouldStartTerrainSectionResize(toScrollAreaPos(local_pos)))
			terrainSectionScrollArea->setCursor(Qt::SizeVerCursor);
		else
			terrainSectionScrollArea->unsetCursor();
	}
	else if(event->type() == QEvent::MouseButtonRelease)
	{
		QMouseEvent* mouse_event = static_cast<QMouseEvent*>(event);
		if(terrain_section_resize_drag_active && (mouse_event->button() == Qt::LeftButton))
		{
			terrain_section_resize_drag_active = false;
			terrainSectionScrollArea->unsetCursor();
			return true;
		}
	}
	else if(event->type() == QEvent::Leave)
	{
		if(!terrain_section_resize_drag_active)
			terrainSectionScrollArea->unsetCursor();
	}

	return QWidget::eventFilter(watched, event);
}


void WorldSettingsWidget::setFromWorldSettings(const WorldSettings& world_settings)
{
	QSignalBlocker blocker(this);

	QtUtils::ClearLayout(terrainSectionScrollAreaWidgetContents->layout(), /*delete widgets=*/true);

	for(size_t i=0; i<world_settings.terrain_spec.section_specs.size(); ++i)
	{
		const TerrainSpecSection& section_spec = world_settings.terrain_spec.section_specs[i];

		TerrainSpecSectionWidget* new_section_widget = new TerrainSpecSectionWidget(this);
		new_section_widget->xSpinBox->setValue(section_spec.x);
		new_section_widget->ySpinBox->setValue(section_spec.y);
		new_section_widget->heightmapURLFileSelectWidget->setFilename(QtUtils::toQString(section_spec.heightmap_URL));
		new_section_widget->maskMapURLFileSelectWidget->setFilename(QtUtils::toQString(section_spec.mask_map_URL));
		new_section_widget->treeMaskMapURLFileSelectWidget->setFilename(QtUtils::toQString(section_spec.tree_mask_map_URL));

		const bool editable = main_window->connectedToUsersWorldOrGodUser();
		new_section_widget->updateControlsEditable(editable);

		terrainSectionScrollAreaWidgetContents->layout()->addWidget(new_section_widget);
		connect(new_section_widget, SIGNAL(removeButtonClickedSignal()), this, SLOT(removeTerrainSectionButtonClickedSlot()));
	}

	detailColMapURLs0FileSelectWidget->setFilename(QtUtils::toQString(world_settings.terrain_spec.detail_col_map_URLs[0]));
	detailColMapURLs1FileSelectWidget->setFilename(QtUtils::toQString(world_settings.terrain_spec.detail_col_map_URLs[1]));
	detailColMapURLs2FileSelectWidget->setFilename(QtUtils::toQString(world_settings.terrain_spec.detail_col_map_URLs[2]));
	detailColMapURLs3FileSelectWidget->setFilename(QtUtils::toQString(world_settings.terrain_spec.detail_col_map_URLs[3]));

	detailHeightMapURLs0FileSelectWidget->setFilename(QtUtils::toQString(world_settings.terrain_spec.detail_height_map_URLs[0]));

	terrainSectionWidthDoubleSpinBox->setValue(world_settings.terrain_spec.terrain_section_width_m);
	terrainHeightScaleDoubleSpinBox->setValue(world_settings.terrain_spec.terrain_height_scale);
	defaultTerrainZDoubleSpinBox->setValue(world_settings.terrain_spec.default_terrain_z);
	waterZDoubleSpinBox->setValue(world_settings.terrain_spec.water_z);
	waterCheckBox->setChecked(BitUtils::isBitSet(world_settings.terrain_spec.flags, TerrainSpec::WATER_ENABLED_FLAG));

	SignalBlocker::setValue(this->sunThetaSettingRealControl, ::radToDegree(world_settings.sun_theta));
	SignalBlocker::setValue(this->sunPhiSettingRealControl,   ::radToDegree(world_settings.sun_phi));
	SignalBlocker::setValue(layer0ASpinBox,           world_settings.fog_settings.layer_0_A);
	SignalBlocker::setValue(layer0HeightScaleSpinBox, world_settings.fog_settings.layer_0_scale_height);
	SignalBlocker::setValue(layer1ASpinBox,           world_settings.fog_settings.layer_1_A);
	SignalBlocker::setValue(layer1HeightScaleSpinBox, world_settings.fog_settings.layer_1_scale_height);
}


URLString WorldSettingsWidget::getURLForFileSelectWidget(FileSelectWidget* widget)
{
	std::string current_URL_or_path = QtUtils::toStdString(widget->filename());

	// Copy all dependencies into resource directory if they are not there already.
	if(FileUtils::fileExists(current_URL_or_path)) // If this was a local path:
	{
		const std::string local_path = current_URL_or_path;
		const URLString URL = ResourceManager::URLForPathAndHash(local_path, FileChecksum::fileChecksum(local_path));

		// Copy model to local resources dir.
		main_window->gui_client.resource_manager->copyLocalFileToResourceDir(local_path, URL);

		return URL;
	}
	else
	{
		return toURLString(current_URL_or_path);
	}
}


void WorldSettingsWidget::toWorldSettings(WorldSettings& world_settings_out)
{
	world_settings_out.terrain_spec.section_specs.resize(0);
	for(int i=0; i<terrainSectionScrollAreaWidgetContents->layout()->count(); ++i)
	{
		QWidget* widget = terrainSectionScrollAreaWidgetContents->layout()->itemAt(i)->widget();
		TerrainSpecSectionWidget* section_widget = dynamic_cast<TerrainSpecSectionWidget*>(widget);
		if(section_widget)
		{
			TerrainSpecSection section;
			section.x = section_widget->xSpinBox->value();
			section.y = section_widget->ySpinBox->value();
			section.heightmap_URL = getURLForFileSelectWidget(section_widget->heightmapURLFileSelectWidget);
			section.mask_map_URL = getURLForFileSelectWidget(section_widget->maskMapURLFileSelectWidget);
			section.tree_mask_map_URL = getURLForFileSelectWidget(section_widget->treeMaskMapURLFileSelectWidget);

			world_settings_out.terrain_spec.section_specs.push_back(section);
		}
	}

	world_settings_out.terrain_spec.detail_col_map_URLs[0] = getURLForFileSelectWidget(detailColMapURLs0FileSelectWidget);
	world_settings_out.terrain_spec.detail_col_map_URLs[1] = getURLForFileSelectWidget(detailColMapURLs1FileSelectWidget);
	world_settings_out.terrain_spec.detail_col_map_URLs[2] = getURLForFileSelectWidget(detailColMapURLs2FileSelectWidget);
	world_settings_out.terrain_spec.detail_col_map_URLs[3] = getURLForFileSelectWidget(detailColMapURLs3FileSelectWidget);

	world_settings_out.terrain_spec.detail_height_map_URLs[0] = getURLForFileSelectWidget(detailHeightMapURLs0FileSelectWidget);

	world_settings_out.terrain_spec.terrain_section_width_m = (float)terrainSectionWidthDoubleSpinBox->value();
	world_settings_out.terrain_spec.terrain_height_scale = (float)terrainHeightScaleDoubleSpinBox->value();
	world_settings_out.terrain_spec.default_terrain_z = (float)defaultTerrainZDoubleSpinBox->value();
	world_settings_out.terrain_spec.water_z = (float)waterZDoubleSpinBox->value();
	world_settings_out.terrain_spec.flags = (waterCheckBox->isChecked() ? TerrainSpec::WATER_ENABLED_FLAG : 0);

	world_settings_out.sun_theta = ::degreeToRad(this->sunThetaSettingRealControl->value());
	world_settings_out.sun_phi   = ::degreeToRad(this->sunPhiSettingRealControl  ->value());
	world_settings_out.fog_settings.layer_0_A            = (float)layer0ASpinBox->value();
	world_settings_out.fog_settings.layer_0_scale_height = (float)layer0HeightScaleSpinBox->value();
	world_settings_out.fog_settings.layer_1_A            = (float)layer1ASpinBox->value();
	world_settings_out.fog_settings.layer_1_scale_height = (float)layer1HeightScaleSpinBox->value();
}


void WorldSettingsWidget::updateControlsEditable()
{
	const bool editable = main_window && main_window->connectedToUsersWorldOrGodUser();

	for(int i=0; i<terrainSectionScrollAreaWidgetContents->layout()->count(); ++i)
	{
		QWidget* widget = terrainSectionScrollAreaWidgetContents->layout()->itemAt(i)->widget();
		TerrainSpecSectionWidget* section_widget = dynamic_cast<TerrainSpecSectionWidget*>(widget);
		if(section_widget)
			section_widget->updateControlsEditable(editable);
	}

	newTerrainSectionPushButton->setEnabled(editable);

	detailColMapURLs0FileSelectWidget->setReadOnly(!editable);
	detailColMapURLs1FileSelectWidget->setReadOnly(!editable);
	detailColMapURLs2FileSelectWidget->setReadOnly(!editable);
	detailColMapURLs3FileSelectWidget->setReadOnly(!editable);
	
	detailHeightMapURLs0FileSelectWidget->setReadOnly(!editable);

	terrainSectionWidthDoubleSpinBox->setReadOnly(!editable);
	terrainHeightScaleDoubleSpinBox->setReadOnly(!editable);
	defaultTerrainZDoubleSpinBox->setReadOnly(!editable);
	waterZDoubleSpinBox->setReadOnly(!editable);
	waterCheckBox->setEnabled(editable);

	sunThetaSettingRealControl->setEnabled(editable);
	sunPhiSettingRealControl  ->setEnabled(editable);
	layer0ASpinBox->setReadOnly(!editable);
	layer0HeightScaleSpinBox->setReadOnly(!editable);
	layer1ASpinBox->setReadOnly(!editable);
	layer1HeightScaleSpinBox->setReadOnly(!editable);

	applyPushButton->setEnabled(editable);
}


void WorldSettingsWidget::newTerrainSectionPushButtonClicked()
{
	TerrainSpecSectionWidget* new_section_widget = new TerrainSpecSectionWidget(this);

	terrainSectionScrollAreaWidgetContents->layout()->addWidget(new_section_widget);

	connect(new_section_widget, SIGNAL(removeButtonClickedSignal()), this, SLOT(removeTerrainSectionButtonClickedSlot()));
}


void WorldSettingsWidget::removeTerrainSectionButtonClickedSlot()
{
	QObject* sender_ob = QObject::sender();

	terrainSectionScrollAreaWidgetContents->layout()->removeWidget((QWidget*)sender_ob);

	sender_ob->deleteLater();
}


void WorldSettingsWidget::applySettingsSlot()
{
	settingsChangedSlot();
}


void WorldSettingsWidget::settingsChangedSlot()
{
	try
	{
		emit settingsChangedSignal();
	}
	catch(glare::Exception& e)
	{
		QMessageBox msgBox;
		msgBox.setWindowTitle("Error");
		msgBox.setText(QtUtils::toQString(e.what()));
		msgBox.exec();
	}
}
