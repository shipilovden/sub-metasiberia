#include "ObjectEditor.h"


#include "ShaderEditorDialog.h"
#include "../dll/include/IndigoMesh.h"
#include "../indigo/TextureServer.h"
#include "../indigo/globals.h"
#include "../graphics/Map2D.h"
#include "../graphics/ImageMap.h"
#include "../graphics/TextRenderer.h"
#include "../maths/vec3.h"
#include "../maths/GeometrySampling.h"
#include "../utils/Lock.h"
#include "../utils/Mutex.h"
#include "../utils/Clock.h"
#include "../utils/Timer.h"
#include "../utils/Platform.h"
#include "../utils/FileUtils.h"
#include "../utils/Reference.h"
#include "../utils/StringUtils.h"
#include "../utils/TaskManager.h"
#include "../qt/SignalBlocker.h"
#include "../qt/QtUtils.h"
#include "../qt/RealControl.h"
#include <QtGui/QIcon>
#include <QtGui/QImage>
#include <QtGui/QMouseEvent>
#include <QtGui/QPixmap>
#include <QtGui/QDesktopServices>
#include <QtWidgets/QErrorMessage>
#include <QtWidgets/QAbstractItemView>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QInputDialog>
#include <QtWidgets/QLabel>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QVBoxLayout>
#include <QtCore/QTimer>
#include <QtCore/QCoreApplication>
#include <QtCore/QSignalBlocker>
#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtWidgets/QColorDialog>
#include <set>
#include <stack>
#include <algorithm>


// NOTE: these max volume levels should be the same as in maxAudioVolumeForObject() in WorkerThread.cpp (runs on server)
static const float DEFAULT_MAX_VOLUME = 4;
static const float DEFAULT_MAX_VIDEO_VOLUME = 4;
static const float SLIDER_MAX_VOLUME = 2;


namespace
{
QString makeFontPreviewText(const QString& font_name)
{
	return QString::fromUtf8("Привет  ") + font_name;
}


QIcon makeFontPreviewIcon(TextRendererRef renderer, const QString& preview_text, const std::string& font_path)
{
	try
	{
		const int preview_width = 300;
		const int font_size_px = 20;
		const int padding_x = 8;
		const int padding_y = 4;

		TextRendererFontFaceSizeSetRef font_set = new TextRendererFontFaceSizeSet(renderer, font_path);
		TextRendererFontFaceRef font = font_set->getFontFaceForSize(font_size_px);

		const std::string preview_text_utf8 = QtUtils::toIndString(preview_text);
		const TextRenderer::SizeInfo size_info = font->getTextSize(preview_text_utf8);
		const int preview_height = std::max(32, size_info.glyphSize().y + padding_y * 2);

		ImageMapUInt8Ref map = new ImageMapUInt8(preview_width, preview_height, 3);
		map->set(255);

		font->drawText(*map, preview_text_utf8, padding_x - size_info.bitmap_left, padding_y + size_info.bitmap_top, Colour3f(0, 0, 0), /*render_SDF=*/false);

		const QImage image(
			(const uchar*)map->getData(),
			(int)map->getWidth(),
			(int)map->getHeight(),
			(int)(map->getWidth() * map->getN()),
			QImage::Format_RGB888
		);
		return QIcon(QPixmap::fromImage(image.copy()));
	}
	catch(...)
	{
		return QIcon();
	}
}


void addFontComboItem(QComboBox* combo, const QString& canonical_name, const QIcon& preview_icon)
{
	combo->addItem(canonical_name, canonical_name);

	const int index = combo->count() - 1;
	if(!preview_icon.isNull())
	{
		combo->setItemIcon(index, preview_icon);
		combo->setItemData(index, QSize(360, 36), Qt::SizeHintRole);
	}

	combo->setItemData(index, canonical_name, Qt::ToolTipRole);
}


QString fontNameForComboIndex(const QComboBox* combo, int index)
{
	if(index < 0 || index >= combo->count())
		return QString();

	QString font_name = combo->itemData(index).toString();
	if(font_name.isEmpty())
		font_name = combo->itemText(index);
	return font_name;
}

} // anonymous namespace

static int remapAudioPlayerMaterialIndexForEditor(int selected_index, size_t material_count);


ObjectEditor::ObjectEditor(QWidget *parent)
:	QWidget(parent),
	selected_mat_index(0),
	edit_timer(new QTimer(this)),
	shader_editor(NULL),
	settings(NULL),
	selected_font_name("Default"),
	controls_editable(true),
	text_font_feature_supported(true),
	syncing_audio_playlist_widget(false),
	editing_audio_player_webview(false),
	audioShuffleCheckBox(NULL),
	audioActivationDistanceLabel(NULL),
	audioActivationDistanceSpinBox(NULL),
	audioPlaylistGroupBox(NULL),
	audioPlaylistListWidget(NULL),
	audioAddTracksPushButton(NULL),
	audioAddURLPushButton(NULL),
	audioRemoveTrackPushButton(NULL),
	audioMoveTrackUpPushButton(NULL),
	audioMoveTrackDownPushButton(NULL),
	spotlight_col(0.85f)
{
	setupUi(this);

	this->modelFileSelectWidget->force_use_last_dir_setting = true;
	this->videoURLFileSelectWidget->force_use_last_dir_setting = true;
	this->audioFileWidget->force_use_last_dir_setting = true;

	//this->scaleXDoubleSpinBox->setMinimum(0.00001);
	//this->scaleYDoubleSpinBox->setMinimum(0.00001);
	//this->scaleZDoubleSpinBox->setMinimum(0.00001);

	SignalBlocker::setChecked(show3DControlsCheckBox, true); // On by default.

	connect(this->matEditor,				SIGNAL(materialChanged()),			this, SIGNAL(objectChanged()));

	connect(this->modelFileSelectWidget,	SIGNAL(filenameChanged(QString&)),	this, SIGNAL(objectChanged()));
	connect(this->scriptTextEdit,			SIGNAL(textChanged()),				this, SLOT(scriptTextEditChanged()));
	connect(this->contentTextEdit,			SIGNAL(textChanged()),				this, SIGNAL(objectChanged()));
	connect(this->fontComboBox,				SIGNAL(currentIndexChanged(int)),	this, SLOT(onFontChanged(int)));
	
	connect(this->targetURLLineEdit,		SIGNAL(editingFinished()),			this, SIGNAL(objectChanged()));
	connect(this->targetURLLineEdit,		SIGNAL(editingFinished()),			this, SLOT(targetURLChanged()));


	connect(this->audioFileWidget,			SIGNAL(filenameChanged(QString&)),	this, SIGNAL(objectChanged()));
	connect(this->volumeDoubleSpinBox,		SIGNAL(valueChanged(double)),		this, SIGNAL(objectChanged()));

	connect(this->posXDoubleSpinBox,		SIGNAL(valueChanged(double)),		this, SIGNAL(objectTransformChanged()));
	connect(this->posYDoubleSpinBox,		SIGNAL(valueChanged(double)),		this, SIGNAL(objectTransformChanged()));
	connect(this->posZDoubleSpinBox,		SIGNAL(valueChanged(double)),		this, SIGNAL(objectTransformChanged()));

	connect(this->scaleXDoubleSpinBox,		SIGNAL(valueChanged(double)),		this, SLOT(xScaleChanged(double)));
	connect(this->scaleYDoubleSpinBox,		SIGNAL(valueChanged(double)),		this, SLOT(yScaleChanged(double)));
	connect(this->scaleZDoubleSpinBox,		SIGNAL(valueChanged(double)),		this, SLOT(zScaleChanged(double)));
	
	connect(this->rotAxisXDoubleSpinBox,	SIGNAL(valueChanged(double)),		this, SIGNAL(objectTransformChanged()));
	connect(this->rotAxisYDoubleSpinBox,	SIGNAL(valueChanged(double)),		this, SIGNAL(objectTransformChanged()));
	connect(this->rotAxisZDoubleSpinBox,	SIGNAL(valueChanged(double)),		this, SIGNAL(objectTransformChanged()));

	connect(this->collidableCheckBox,		SIGNAL(toggled(bool)),				this, SIGNAL(objectChanged()));
	connect(this->dynamicCheckBox,			SIGNAL(toggled(bool)),				this, SIGNAL(objectChanged()));
	connect(this->sensorCheckBox,			SIGNAL(toggled(bool)),				this, SIGNAL(objectChanged()));

	connect(this->massDoubleSpinBox,		SIGNAL(valueChanged(double)),		this, SIGNAL(objectChanged()));
	connect(this->frictionDoubleSpinBox,	SIGNAL(valueChanged(double)),		this, SIGNAL(objectChanged()));
	connect(this->restitutionDoubleSpinBox,	SIGNAL(valueChanged(double)),		this, SIGNAL(objectChanged()));

	connect(this->COMOffsetXDoubleSpinBox,	SIGNAL(valueChanged(double)),		this, SIGNAL(objectChanged()));
	connect(this->COMOffsetYDoubleSpinBox,	SIGNAL(valueChanged(double)),		this, SIGNAL(objectChanged()));
	connect(this->COMOffsetZDoubleSpinBox,	SIGNAL(valueChanged(double)),		this, SIGNAL(objectChanged()));


	connect(this->luminousFluxDoubleSpinBox,SIGNAL(valueChanged(double)),		this, SIGNAL(objectChanged()));

	connect(this->show3DControlsCheckBox,	SIGNAL(toggled(bool)),				this, SIGNAL(posAndRot3DControlsToggled()));

	connect(this->linkScaleCheckBox,		SIGNAL(toggled(bool)),				this, SLOT(linkScaleCheckBoxToggled(bool)));

	connect(this->videoAutoplayCheckBox,	SIGNAL(toggled(bool)),				this, SIGNAL(objectChanged()));
	connect(this->videoLoopCheckBox,		SIGNAL(toggled(bool)),				this, SIGNAL(objectChanged()));
	connect(this->videoMutedCheckBox,		SIGNAL(toggled(bool)),				this, SIGNAL(objectChanged()));

	connect(this->videoURLFileSelectWidget,	SIGNAL(filenameChanged(QString&)),	this, SIGNAL(objectChanged()));
	connect(this->videoVolumeDoubleSpinBox,	SIGNAL(valueChanged(double)),		this, SIGNAL(objectChanged()));

	connect(this->audioAutoplayCheckBox,	SIGNAL(toggled(bool)),				this, SIGNAL(objectChanged()));
	connect(this->audioLoopCheckBox,		SIGNAL(toggled(bool)),				this, SIGNAL(objectChanged()));

	connect(this->spotlightStartAngleSpinBox,	SIGNAL(valueChanged(double)),	this, SIGNAL(objectChanged()));
	connect(this->spotlightEndAngleSpinBox,		SIGNAL(valueChanged(double)),	this, SIGNAL(objectChanged()));

	connect(this->cameraEnabledCheckBox,			SIGNAL(toggled(bool)),			this, SIGNAL(objectChanged()));
	connect(this->cameraFOVYDoubleSpinBox,			SIGNAL(valueChanged(double)),	this, SIGNAL(objectChanged()));
	connect(this->cameraNearDistDoubleSpinBox,		SIGNAL(valueChanged(double)),	this, SIGNAL(objectChanged()));
	connect(this->cameraFarDistDoubleSpinBox,		SIGNAL(valueChanged(double)),	this, SIGNAL(objectChanged()));
	connect(this->cameraRenderWidthSpinBox,		SIGNAL(valueChanged(int)),		this, SIGNAL(objectChanged()));
	connect(this->cameraRenderHeightSpinBox,		SIGNAL(valueChanged(int)),		this, SIGNAL(objectChanged()));
	connect(this->cameraMaxFPSSpinBox,				SIGNAL(valueChanged(int)),		this, SIGNAL(objectChanged()));

	connect(this->cameraScreenEnabledCheckBox,		SIGNAL(toggled(bool)),			this, SIGNAL(objectChanged()));
	connect(this->cameraScreenSourceUIDLineEdit,	SIGNAL(editingFinished()),		this, SIGNAL(objectChanged()));
	connect(this->cameraScreenMaterialIndexSpinBox,SIGNAL(valueChanged(int)),		this, SIGNAL(objectChanged()));


	this->volumeDoubleSpinBox->setMaximum(DEFAULT_MAX_VOLUME);
	this->volumeDoubleSpinBox->setSliderMaximum(SLIDER_MAX_VOLUME);

	this->videoVolumeDoubleSpinBox->setMaximum(DEFAULT_MAX_VIDEO_VOLUME);
	this->videoVolumeDoubleSpinBox->setSliderMaximum(SLIDER_MAX_VOLUME);

	this->visitURLLabel->hide();

	this->audioShuffleCheckBox = new QCheckBox(QCoreApplication::translate("ObjectEditor", "Shuffle"), this->audioGroupBox);
	this->gridLayout_3->addWidget(this->audioShuffleCheckBox, 0, 2);

	this->audioActivationDistanceLabel = new QLabel(QCoreApplication::translate("ObjectEditor", "Activation Distance"), this->audioGroupBox);
	this->gridLayout_3->addWidget(this->audioActivationDistanceLabel, 1, 0);

	this->audioActivationDistanceSpinBox = new RealControl(this->audioGroupBox);
	this->audioActivationDistanceSpinBox->setMinimum(WorldObject::MIN_AUDIO_PLAYER_ACTIVATION_DISTANCE);
	this->audioActivationDistanceSpinBox->setMaximum(WorldObject::MAX_AUDIO_PLAYER_ACTIVATION_DISTANCE);
	this->audioActivationDistanceSpinBox->setSingleStep(0.5);
	this->audioActivationDistanceSpinBox->setSliderMinimum(WorldObject::MIN_AUDIO_PLAYER_ACTIVATION_DISTANCE);
	this->audioActivationDistanceSpinBox->setSliderMaximum(60.0);
	this->audioActivationDistanceSpinBox->setSliderSteps(240);
	this->audioActivationDistanceSpinBox->setSuffix(QCoreApplication::translate("ObjectEditor", " m"));
	this->gridLayout_3->addWidget(this->audioActivationDistanceSpinBox, 1, 1, 1, 2);

	this->audioPlaylistGroupBox = new QGroupBox(QCoreApplication::translate("ObjectEditor", "Playlist"), this->audioGroupBox);
	QVBoxLayout* audio_playlist_group_layout = new QVBoxLayout(this->audioPlaylistGroupBox);
	audio_playlist_group_layout->setContentsMargins(0, 0, 0, 0);
	audio_playlist_group_layout->setSpacing(6);

	this->audioPlaylistListWidget = new QListWidget(this->audioPlaylistGroupBox);
	this->audioPlaylistListWidget->setSelectionMode(QAbstractItemView::SingleSelection);
	audio_playlist_group_layout->addWidget(this->audioPlaylistListWidget);

	QHBoxLayout* audio_playlist_buttons_layout = new QHBoxLayout();
	audio_playlist_buttons_layout->setContentsMargins(0, 0, 0, 0);
	audio_playlist_buttons_layout->setSpacing(6);

	this->audioAddTracksPushButton = new QPushButton(QCoreApplication::translate("ObjectEditor", "Add Tracks"), this->audioPlaylistGroupBox);
	this->audioAddURLPushButton = new QPushButton(QCoreApplication::translate("ObjectEditor", "Add URL"), this->audioPlaylistGroupBox);
	this->audioRemoveTrackPushButton = new QPushButton(QCoreApplication::translate("ObjectEditor", "Remove"), this->audioPlaylistGroupBox);
	this->audioMoveTrackUpPushButton = new QPushButton(QCoreApplication::translate("ObjectEditor", "Up"), this->audioPlaylistGroupBox);
	this->audioMoveTrackDownPushButton = new QPushButton(QCoreApplication::translate("ObjectEditor", "Down"), this->audioPlaylistGroupBox);

	audio_playlist_buttons_layout->addWidget(this->audioAddTracksPushButton);
	audio_playlist_buttons_layout->addWidget(this->audioAddURLPushButton);
	audio_playlist_buttons_layout->addWidget(this->audioRemoveTrackPushButton);
	audio_playlist_buttons_layout->addWidget(this->audioMoveTrackUpPushButton);
	audio_playlist_buttons_layout->addWidget(this->audioMoveTrackDownPushButton);
	audio_playlist_group_layout->addLayout(audio_playlist_buttons_layout);

	this->verticalLayout_6->addWidget(this->audioPlaylistGroupBox);

	connect(this->audioShuffleCheckBox,		SIGNAL(toggled(bool)),				this, SIGNAL(objectChanged()));
	connect(this->audioActivationDistanceSpinBox, SIGNAL(valueChanged(double)),	this, SIGNAL(objectChanged()));
	connect(this->audioAddTracksPushButton, SIGNAL(clicked(bool)),				this, SLOT(on_audioAddTracksPushButton_clicked(bool)));
	connect(this->audioAddURLPushButton,	SIGNAL(clicked(bool)),				this, SLOT(on_audioAddURLPushButton_clicked(bool)));
	connect(this->audioRemoveTrackPushButton, SIGNAL(clicked(bool)),			this, SLOT(on_audioRemoveTrackPushButton_clicked(bool)));
	connect(this->audioMoveTrackUpPushButton, SIGNAL(clicked(bool)),			this, SLOT(on_audioMoveTrackUpPushButton_clicked(bool)));
	connect(this->audioMoveTrackDownPushButton, SIGNAL(clicked(bool)),		this, SLOT(on_audioMoveTrackDownPushButton_clicked(bool)));
	connect(this->audioPlaylistListWidget,	SIGNAL(itemChanged(QListWidgetItem*)), this, SLOT(audioPlaylistItemChanged(QListWidgetItem*)));
	connect(this->audioPlaylistListWidget,	SIGNAL(currentRowChanged(int)),		this, SLOT(audioPlaylistSelectionChanged()));

	// Set up script edit timer.
	edit_timer->setSingleShot(true);
	edit_timer->setInterval(300);

	connect(edit_timer, SIGNAL(timeout()), this, SLOT(editTimerTimeout()));

	this->fontComboBox->setMinimumContentsLength(22);
	this->fontComboBox->setIconSize(QSize(300, 32));
	if(this->fontComboBox->view())
		this->fontComboBox->view()->setTextElideMode(Qt::ElideNone);

	updateAudioPlaylistButtonsEnabled();
}


void ObjectEditor::init() // settings should be set before this.
{
	show3DControlsCheckBox->setChecked(settings->value("objectEditor/show3DControlsCheckBoxChecked", /*default val=*/true).toBool());
	SignalBlocker::setChecked(linkScaleCheckBox, settings->value("objectEditor/linkScaleCheckBoxChecked", /*default val=*/true).toBool());

	SignalBlocker::setValue(gridSpacingDoubleSpinBox, settings->value("objectEditor/gridSpacing", /*default val=*/1.0).toDouble());
	SignalBlocker::setChecked(snapToGridCheckBox, settings->value("objectEditor/snapToGridCheckBoxChecked", /*default val=*/false).toBool());

	// Initialize font list
	loadAvailableFonts();
}


void ObjectEditor::addAudioPlaylistEntry(const QString& value, bool make_current)
{
	const QString trimmed_value = value.trimmed();
	if(trimmed_value.isEmpty())
		return;

	QListWidgetItem* item = new QListWidgetItem(trimmed_value, this->audioPlaylistListWidget);
	item->setFlags(item->flags() | Qt::ItemIsEditable);
	item->setToolTip(trimmed_value);

	if(make_current)
		this->audioPlaylistListWidget->setCurrentItem(item);
}


void ObjectEditor::syncAudioPlaylistWidgetFromContent(const std::string& content)
{
	this->syncing_audio_playlist_widget = true;

	this->audioPlaylistListWidget->clear();

	const QStringList lines = QtUtils::toQString(content).split('\n', Qt::KeepEmptyParts);
	for(int i=0; i<lines.size(); ++i)
		addAudioPlaylistEntry(lines[i], false);

	if(this->audioPlaylistListWidget->count() > 0)
		this->audioPlaylistListWidget->setCurrentRow(0);

	this->syncing_audio_playlist_widget = false;
	updateAudioPlaylistButtonsEnabled();
}


void ObjectEditor::syncContentFromAudioPlaylistWidget()
{
	if(this->syncing_audio_playlist_widget)
		return;

	QStringList lines;
	lines.reserve(this->audioPlaylistListWidget->count());
	for(int i=0; i<this->audioPlaylistListWidget->count(); ++i)
	{
		QListWidgetItem* item = this->audioPlaylistListWidget->item(i);
		if(item)
		{
			const QString trimmed_text = item->text().trimmed();
			item->setToolTip(trimmed_text);
			if(!trimmed_text.isEmpty())
				lines.push_back(trimmed_text);
		}
	}

	{
		SignalBlocker blocker(this->contentTextEdit);
		this->contentTextEdit->setPlainText(lines.join("\n"));
	}

	updateAudioPlaylistButtonsEnabled();
}


void ObjectEditor::updateAudioPlaylistButtonsEnabled()
{
	const bool have_selection = this->audioPlaylistListWidget && (this->audioPlaylistListWidget->currentRow() >= 0);
	const int current_row = have_selection ? this->audioPlaylistListWidget->currentRow() : -1;
	const int num_items = this->audioPlaylistListWidget ? this->audioPlaylistListWidget->count() : 0;
	const bool editable = this->controls_editable;

	if(this->audioAddTracksPushButton) this->audioAddTracksPushButton->setEnabled(editable);
	if(this->audioAddURLPushButton) this->audioAddURLPushButton->setEnabled(editable);
	if(this->audioRemoveTrackPushButton) this->audioRemoveTrackPushButton->setEnabled(editable && have_selection);
	if(this->audioMoveTrackUpPushButton) this->audioMoveTrackUpPushButton->setEnabled(editable && have_selection && current_row > 0);
	if(this->audioMoveTrackDownPushButton) this->audioMoveTrackDownPushButton->setEnabled(editable && have_selection && current_row >= 0 && current_row + 1 < num_items);
}


ObjectEditor::~ObjectEditor()
{
	settings->setValue("objectEditor/gridSpacing", gridSpacingDoubleSpinBox->value());
	settings->setValue("objectEditor/snapToGridCheckBoxChecked", snapToGridCheckBox->isChecked());
}


void ObjectEditor::updateInfoLabel(const WorldObject& ob)
{
	const std::string creator_name = !ob.creator_name.empty() ? ob.creator_name :
		(ob.creator_id.valid() ? (QtUtils::toStdString(QCoreApplication::translate("ObjectEditor", "user id: ")) + ob.creator_id.toString()) : QtUtils::toStdString(QCoreApplication::translate("ObjectEditor", "[Unknown]")));

	QString ob_type;
	switch(ob.object_type)
	{
	case WorldObject::ObjectType_Generic: ob_type = QCoreApplication::translate("ObjectEditor", "Generic object"); break;
	case WorldObject::ObjectType_Hypercard: ob_type = QCoreApplication::translate("ObjectEditor", "Hypercard"); break;
	case WorldObject::ObjectType_VoxelGroup: ob_type = QCoreApplication::translate("ObjectEditor", "Voxel Group"); break;
	case WorldObject::ObjectType_Spotlight: ob_type = QCoreApplication::translate("ObjectEditor", "Spotlight"); break;
	case WorldObject::ObjectType_WebView: ob_type = ob.isAudioPlayerWebView() ? QCoreApplication::translate("ObjectEditor", "Audio Player") : QCoreApplication::translate("ObjectEditor", "Web View"); break;
	case WorldObject::ObjectType_Video: ob_type = QCoreApplication::translate("ObjectEditor", "Video"); break;
	case WorldObject::ObjectType_Text: ob_type = QCoreApplication::translate("ObjectEditor", "Text"); break;
	case WorldObject::ObjectType_Portal: ob_type = QCoreApplication::translate("ObjectEditor", "Portal"); break;
	case WorldObject::ObjectType_Seat: ob_type = QCoreApplication::translate("ObjectEditor", "Seat"); break;
	case WorldObject::ObjectType_Camera: ob_type = QCoreApplication::translate("ObjectEditor", "Camera"); break;
	case WorldObject::ObjectType_CameraScreen: ob_type = QCoreApplication::translate("ObjectEditor", "Camera Screen"); break;
	}

	QString info_text = ob_type + " (UID: " + QtUtils::toQString(ob.uid.toString()) + "), \n" +
		QCoreApplication::translate("ObjectEditor", "created by") + " '" + QtUtils::toQString(creator_name) + "' " +
		QtUtils::toQString(ob.created_time.timeAgoDescription());
	
	// Show last-modified time only if it differs from created_time.
	if(ob.created_time.time != ob.last_modified_time.time)
		info_text += ", " + QCoreApplication::translate("ObjectEditor", "last modified") + " " + QtUtils::toQString(ob.last_modified_time.timeAgoDescription());

	this->infoLabel->setText(info_text);
}


void ObjectEditor::setFromObject(const WorldObject& ob, int selected_mat_index_, bool ob_in_editing_users_world)
{
	const QSignalBlocker signal_blocker(this);

	this->editing_ob_uid = ob.uid;
	this->editing_audio_player_webview = ob.isAudioPlayerWebView();

	//this->objectTypeLabel->setText(QtUtils::toQString(ob_type + " (UID: " + ob.uid.toString() + ")"));

	if(ob_in_editing_users_world)
	{
		// If the user is logged in, and we are connected to the user's personal world, set a high maximum volume.
		this->volumeDoubleSpinBox->setMaximum(1000);
		this->volumeDoubleSpinBox->setSliderMaximum(SLIDER_MAX_VOLUME);

		this->videoVolumeDoubleSpinBox->setMaximum(1000);
		this->videoVolumeDoubleSpinBox->setSliderMaximum(SLIDER_MAX_VOLUME);
	}
	else
	{
		// Otherwise just use the default max volume values
		this->volumeDoubleSpinBox->setMaximum(DEFAULT_MAX_VOLUME);
		this->volumeDoubleSpinBox->setSliderMaximum(SLIDER_MAX_VOLUME);

		this->videoVolumeDoubleSpinBox->setMaximum(DEFAULT_MAX_VIDEO_VOLUME);
		this->videoVolumeDoubleSpinBox->setSliderMaximum(SLIDER_MAX_VOLUME);
	}

	this->cloned_materials.resize(ob.materials.size());
	for(size_t i=0; i<ob.materials.size(); ++i)
		this->cloned_materials[i] = ob.materials[i]->clone();

	//this->createdByLabel->setText(QtUtils::toQString(creator_name));
	//this->createdTimeLabel->setText(QtUtils::toQString(ob.created_time.timeAgoDescription()));

	updateInfoLabel(ob);

	this->selected_mat_index = selected_mat_index_;

	// The spotlight model has multiple materials, we want to edit material 0 though.
	if(ob.object_type == WorldObject::ObjectType_Spotlight)
		this->selected_mat_index = 0;

	if(this->editing_audio_player_webview)
		this->selected_mat_index = remapAudioPlayerMaterialIndexForEditor(this->selected_mat_index, ob.materials.size());

	this->modelFileSelectWidget->setFilename(QtUtils::toQString(ob.model_url));
	{
		SignalBlocker b(this->scriptTextEdit);
		this->scriptTextEdit->setPlainText(QtUtils::toQString(ob.script));
	}
	{
		SignalBlocker b(this->contentTextEdit);
		this->contentTextEdit->setPlainText(QtUtils::toQString(ob.content));
	}
	syncAudioPlaylistWidgetFromContent(ob.content);
	{
		SignalBlocker b(this->fontComboBox);
		// Set font combobox to the object's font
		const int font_index = this->fontComboBox->findData(QtUtils::toQString(ob.text_font));
		if(font_index >= 0)
			this->fontComboBox->setCurrentIndex(font_index);
		else
			this->fontComboBox->setCurrentIndex(0); // Default to first font if not found

		this->selected_font_name = fontNameForComboIndex(this->fontComboBox, this->fontComboBox->currentIndex());
	}
	{
		SignalBlocker b(this->targetURLLineEdit);
		this->targetURLLineEdit->setText(QtUtils::toQString(ob.target_url));
	}

	this->posXDoubleSpinBox->setEnabled(true);
	this->posYDoubleSpinBox->setEnabled(true);
	this->posZDoubleSpinBox->setEnabled(true);

	setTransformFromObject(ob);

	SignalBlocker::setChecked(this->collidableCheckBox, ob.isCollidable());
	SignalBlocker::setChecked(this->dynamicCheckBox, ob.isDynamic());
	SignalBlocker::setChecked(this->sensorCheckBox, ob.isSensor());
	
	SignalBlocker::setValue(this->massDoubleSpinBox,		ob.mass);
	SignalBlocker::setValue(this->frictionDoubleSpinBox,	ob.friction);
	SignalBlocker::setValue(this->restitutionDoubleSpinBox, ob.restitution);

	SignalBlocker::setValue(COMOffsetXDoubleSpinBox, ob.centre_of_mass_offset_os.x);
	SignalBlocker::setValue(COMOffsetYDoubleSpinBox, ob.centre_of_mass_offset_os.y);
	SignalBlocker::setValue(COMOffsetZDoubleSpinBox, ob.centre_of_mass_offset_os.z);

	
	SignalBlocker::setChecked(this->videoAutoplayCheckBox, BitUtils::isBitSet(ob.flags, WorldObject::VIDEO_AUTOPLAY));
	SignalBlocker::setChecked(this->videoLoopCheckBox,     BitUtils::isBitSet(ob.flags, WorldObject::VIDEO_LOOP));
	SignalBlocker::setChecked(this->videoMutedCheckBox,    BitUtils::isBitSet(ob.flags, WorldObject::VIDEO_MUTED));

	SignalBlocker::setChecked(this->audioAutoplayCheckBox, BitUtils::isBitSet(ob.flags, WorldObject::AUDIO_AUTOPLAY));
	SignalBlocker::setChecked(this->audioLoopCheckBox,     BitUtils::isBitSet(ob.flags, WorldObject::AUDIO_LOOP));
	SignalBlocker::setChecked(this->audioShuffleCheckBox,  BitUtils::isBitSet(ob.flags, WorldObject::AUDIO_SHUFFLE));
	SignalBlocker::setValue(this->audioActivationDistanceSpinBox, myClamp((double)ob.audio_player_activation_distance,
		(double)WorldObject::MIN_AUDIO_PLAYER_ACTIVATION_DISTANCE, (double)WorldObject::MAX_AUDIO_PLAYER_ACTIVATION_DISTANCE));

	this->videoURLFileSelectWidget->setFilename(QtUtils::toQString((!ob.materials.empty()) ? ob.materials[0]->emission_texture_url : ""));

	SignalBlocker::setValue(videoVolumeDoubleSpinBox, ob.audio_volume);
	
	lightmapURLLabel->setText(QtUtils::toQString(ob.lightmap_url));

	WorldMaterialRef selected_mat;
	if(this->selected_mat_index >= 0 && this->selected_mat_index < (int)ob.materials.size())
		selected_mat = ob.materials[this->selected_mat_index];
	else
		selected_mat = new WorldMaterial();

	this->cameraGroupBox->hide();
	this->cameraScreenGroupBox->hide();
	
	if(ob.object_type == WorldObject::ObjectType_Hypercard)
	{
		this->materialsGroupBox->hide();
		this->lightmapGroupBox->hide();
		this->modelLabel->hide();
		this->modelFileSelectWidget->hide();
		this->spotlightGroupBox->hide();
		this->seatGroupBox->hide();
		this->audioGroupBox->show();
		this->physicsSettingsGroupBox->show();
		this->videoGroupBox->hide();
	}
	else if(ob.object_type == WorldObject::ObjectType_VoxelGroup)
	{
		this->materialsGroupBox->show();
		this->lightmapGroupBox->show();
		this->modelLabel->hide();
		this->modelFileSelectWidget->hide();
		this->spotlightGroupBox->hide();
		this->seatGroupBox->hide();
		this->audioGroupBox->show();
		this->physicsSettingsGroupBox->show();
		this->videoGroupBox->hide();
	}
	else if(ob.object_type == WorldObject::ObjectType_Spotlight)
	{
		this->materialsGroupBox->hide();
		this->lightmapGroupBox->hide();
		this->modelLabel->hide();
		this->modelFileSelectWidget->hide();
		this->spotlightGroupBox->show();
		this->seatGroupBox->hide();
		this->audioGroupBox->hide();
		this->physicsSettingsGroupBox->show();
		this->videoGroupBox->hide();
	}
	else if(ob.object_type == WorldObject::ObjectType_Seat)
	{
		this->materialsGroupBox->show();
		this->lightmapGroupBox->hide();
		this->modelLabel->hide();
		this->modelFileSelectWidget->hide();
		this->spotlightGroupBox->hide();
		this->seatGroupBox->show();
		this->audioGroupBox->hide();
		this->physicsSettingsGroupBox->show();
		this->videoGroupBox->hide();
	}
	else if(ob.object_type == WorldObject::ObjectType_Camera)
	{
		this->materialsGroupBox->show();
		this->lightmapGroupBox->hide();
		this->modelLabel->hide();
		this->modelFileSelectWidget->hide();
		this->spotlightGroupBox->hide();
		this->seatGroupBox->hide();
		this->cameraGroupBox->show();
		this->cameraScreenGroupBox->hide();
		this->audioGroupBox->hide();
		this->physicsSettingsGroupBox->show();
		this->videoGroupBox->hide();
	}
	else if(ob.object_type == WorldObject::ObjectType_CameraScreen)
	{
		this->materialsGroupBox->show();
		this->lightmapGroupBox->hide();
		this->modelLabel->hide();
		this->modelFileSelectWidget->hide();
		this->spotlightGroupBox->hide();
		this->seatGroupBox->hide();
		this->cameraGroupBox->hide();
		this->cameraScreenGroupBox->show();
		this->audioGroupBox->hide();
		this->physicsSettingsGroupBox->show();
		this->videoGroupBox->hide();
	}
	else if(ob.object_type == WorldObject::ObjectType_WebView)
	{
		this->materialsGroupBox->show();
		this->lightmapGroupBox->hide();
		this->modelLabel->hide();
		this->modelFileSelectWidget->hide();
		this->spotlightGroupBox->hide();
		this->seatGroupBox->hide();
		this->audioGroupBox->setVisible(ob.isAudioPlayerWebView());
		this->physicsSettingsGroupBox->hide();
		this->videoGroupBox->hide();
	}
	else if(ob.object_type == WorldObject::ObjectType_Video)
	{
		this->materialsGroupBox->hide();
		this->lightmapGroupBox->hide();
		this->modelLabel->hide();
		this->modelFileSelectWidget->hide();
		this->spotlightGroupBox->hide();
		this->seatGroupBox->hide();
		this->audioGroupBox->hide();
		this->physicsSettingsGroupBox->hide();
		this->videoGroupBox->show();
	}
	else if(ob.object_type == WorldObject::ObjectType_Text)
	{
		this->materialsGroupBox->show();
		this->lightmapGroupBox->hide();
		this->modelLabel->hide();
		this->modelFileSelectWidget->hide();
		this->spotlightGroupBox->hide();
		this->seatGroupBox->hide();
		this->audioGroupBox->show();
		this->physicsSettingsGroupBox->show();
		this->videoGroupBox->hide();
	}
	else if(ob.object_type == WorldObject::ObjectType_Portal)
	{
		this->materialsGroupBox->hide();
		this->lightmapGroupBox->hide();
		this->modelLabel->hide();
		this->modelFileSelectWidget->hide();
		this->spotlightGroupBox->hide();
		this->seatGroupBox->hide();
		this->audioGroupBox->show();
		this->physicsSettingsGroupBox->hide();
		this->videoGroupBox->hide();
	}
	else
	{
		this->materialsGroupBox->show();
		this->lightmapGroupBox->show();
		this->modelLabel->show();
		this->modelFileSelectWidget->show();
		this->spotlightGroupBox->hide();
		this->seatGroupBox->hide();
		this->audioGroupBox->show();
		this->physicsSettingsGroupBox->show();
		this->videoGroupBox->hide();
	}

	if(ob.object_type != WorldObject::ObjectType_Hypercard)
	{
		this->matEditor->setFromMaterial(*selected_mat);

		// Set materials combobox
		SignalBlocker blocker(this->materialComboBox);
		this->materialComboBox->clear();
		for(size_t i=0; i<ob.materials.size(); ++i)
		{
			if(this->editing_audio_player_webview && i == 0)
				this->materialComboBox->addItem(QCoreApplication::translate("ObjectEditor", "Material 0 (Player Screen)"), (int)i);
			else if(this->editing_audio_player_webview && i == 1)
				this->materialComboBox->addItem(QCoreApplication::translate("ObjectEditor", "Material 1 (Player Body)"), (int)i);
			else
				this->materialComboBox->addItem(QtUtils::toQString("Material " + toString(i)), (int)i);
		}

		this->materialComboBox->setCurrentIndex(this->selected_mat_index);
	}


	// For spotlight:
	if(ob.object_type == WorldObject::ObjectType_Spotlight)
	{
		SignalBlocker::setValue(this->luminousFluxDoubleSpinBox, selected_mat->emission_lum_flux_or_lum);

		this->spotlight_col = selected_mat->colour_rgb; // Spotlight light colour is in colour_rgb instead of emission_rgb for historical reasons.

		SignalBlocker::setValue(this->spotlightStartAngleSpinBox, ::radToDegree(ob.type_data.spotlight_data.cone_start_angle));
		SignalBlocker::setValue(this->spotlightEndAngleSpinBox,   ::radToDegree(ob.type_data.spotlight_data.cone_end_angle));

		updateSpotlightColourButton();
	}

	// For seat:
	if(ob.object_type == WorldObject::ObjectType_Seat)
	{
		SignalBlocker::setValue(this->upperLegAngleDoubleSpinBox, ob.type_data.seat_data.upper_leg_angle);
		SignalBlocker::setValue(this->lowerLegAngleDoubleSpinBox, ob.type_data.seat_data.lower_leg_angle);
		SignalBlocker::setValue(this->upperArmAngleDoubleSpinBox, ob.type_data.seat_data.upper_arm_angle);
		SignalBlocker::setValue(this->lowerArmAngleDoubleSpinBox, ob.type_data.seat_data.lower_arm_angle);
	}

	if(ob.object_type == WorldObject::ObjectType_Camera)
	{
		SignalBlocker::setChecked(this->cameraEnabledCheckBox, ob.type_data.camera_data.enabled != 0);
		SignalBlocker::setValue(this->cameraFOVYDoubleSpinBox, ::radToDegree(ob.type_data.camera_data.fov_y_rad));
		SignalBlocker::setValue(this->cameraNearDistDoubleSpinBox, ob.type_data.camera_data.near_dist);
		SignalBlocker::setValue(this->cameraFarDistDoubleSpinBox, ob.type_data.camera_data.far_dist);
		SignalBlocker::setValue(this->cameraRenderWidthSpinBox, (int)ob.type_data.camera_data.render_width);
		SignalBlocker::setValue(this->cameraRenderHeightSpinBox, (int)ob.type_data.camera_data.render_height);
		SignalBlocker::setValue(this->cameraMaxFPSSpinBox, (int)ob.type_data.camera_data.max_fps);
	}

	if(ob.object_type == WorldObject::ObjectType_CameraScreen)
	{
		SignalBlocker::setChecked(this->cameraScreenEnabledCheckBox, ob.type_data.camera_screen_data.enabled != 0);
		{
			SignalBlocker b(this->cameraScreenSourceUIDLineEdit);
			this->cameraScreenSourceUIDLineEdit->setText(QtUtils::toQString(toString(ob.type_data.camera_screen_data.source_camera_uid)));
		}
		SignalBlocker::setValue(this->cameraScreenMaterialIndexSpinBox, (int)ob.type_data.camera_screen_data.material_index);
	}


	//this->targetURLLabel->setVisible(ob.object_type == WorldObject::ObjectType_Hypercard);
	//this->targetURLLineEdit->setVisible(ob.object_type == WorldObject::ObjectType_Hypercard);
	const bool is_audio_player = ob.isAudioPlayerWebView();
	this->audioGroupBox->setTitle(is_audio_player ? QCoreApplication::translate("ObjectEditor", "Audio Player") : QCoreApplication::translate("ObjectEditor", "Audio"));
	this->label_9->setVisible(!is_audio_player);
	this->widget_5->setVisible(!is_audio_player);
	this->label_12->setVisible(!is_audio_player);
	this->contentTextEdit->setVisible(!is_audio_player);
	this->fontLabel->setVisible(!is_audio_player);
	this->fontComboBox->setVisible(!is_audio_player);
	this->label_14->setVisible(!is_audio_player);
	this->audioFileWidget->setVisible(!is_audio_player);
	this->targetURLLabel->setVisible(!is_audio_player);
	this->targetURLLineEdit->setVisible(!is_audio_player);
	this->visitURLLabel->setVisible(!is_audio_player && !ob.target_url.empty());
	this->audioShuffleCheckBox->setVisible(is_audio_player);
	this->audioActivationDistanceLabel->setVisible(is_audio_player);
	this->audioActivationDistanceSpinBox->setVisible(is_audio_player);
	this->audioPlaylistGroupBox->setVisible(is_audio_player);
	this->label_12->setText(QCoreApplication::translate("ObjectEditor", "Content"));
	this->contentTextEdit->setPlaceholderText(QString());

	if(ob.lightmap_baking)
	{
		lightmapBakeStatusLabel->setText(QCoreApplication::translate("ObjectEditor", "Lightmap is baking..."));
	}
	else
	{
		lightmapBakeStatusLabel->setText("");
	}

	this->audioFileWidget->setFilename(QtUtils::toQString(ob.audio_source_url));
	SignalBlocker::setValue(volumeDoubleSpinBox, ob.audio_volume);
	updateAudioPlaylistButtonsEnabled();
}


void ObjectEditor::setTransformFromObject(const WorldObject& ob)
{
	SignalBlocker::setValue(this->posXDoubleSpinBox, ob.pos.x);
	SignalBlocker::setValue(this->posYDoubleSpinBox, ob.pos.y);
	SignalBlocker::setValue(this->posZDoubleSpinBox, ob.pos.z);

	SignalBlocker::setValue(this->scaleXDoubleSpinBox, ob.scale.x);
	SignalBlocker::setValue(this->scaleYDoubleSpinBox, ob.scale.y);
	SignalBlocker::setValue(this->scaleZDoubleSpinBox, ob.scale.z);

	this->last_x_scale_over_z_scale = ob.scale.x / ob.scale.z;
	this->last_x_scale_over_y_scale = ob.scale.x / ob.scale.y;
	this->last_y_scale_over_z_scale = ob.scale.y / ob.scale.z;


	const Matrix3f rot_mat = Matrix3f::rotationMatrix(normalise(ob.axis), ob.angle);

	const Vec3f angles = rot_mat.getAngles();

	SignalBlocker::setValue(this->rotAxisXDoubleSpinBox, angles.x * 360 / Maths::get2Pi<float>());
	SignalBlocker::setValue(this->rotAxisYDoubleSpinBox, angles.y * 360 / Maths::get2Pi<float>());
	SignalBlocker::setValue(this->rotAxisZDoubleSpinBox, angles.z * 360 / Maths::get2Pi<float>());

	updateInfoLabel(ob); // Update info label, which includes last-modified time.
}


template <class StringType>
static void checkStringSize(StringType& s, size_t max_size)
{
	// TODO: throw exception instead?
	if(s.size() > max_size)
		s = s.substr(0, max_size);
}


static bool objectTypeUsesEditableModelURL(WorldObject::ObjectType object_type)
{
	return object_type == WorldObject::ObjectType_Generic;
}


static int remapAudioPlayerMaterialIndexForEditor(int selected_index, size_t material_count)
{
	// Keep material slot 0 reserved for the live browser surface.
	// In editor we treat slot 1 as the primary editable "player body" material.
	if(material_count > 1 && selected_index == 0)
		return 1;
	return selected_index;
}


void ObjectEditor::toObject(WorldObject& ob_out)
{
	const bool is_audio_player_webview = ob_out.isAudioPlayerWebView();

	if(is_audio_player_webview)
		syncContentFromAudioPlaylistWidget();

	URLString new_model_url = ob_out.model_url;
	if(objectTypeUsesEditableModelURL((WorldObject::ObjectType)ob_out.object_type))
		new_model_url = toURLString(QtUtils::toIndString(this->modelFileSelectWidget->filename()));
	else if((ob_out.object_type == WorldObject::ObjectType_WebView || ob_out.object_type == WorldObject::ObjectType_Video) && new_model_url.empty())
		new_model_url = "image_cube_5438347426447337425.bmesh";
	if(ob_out.model_url != new_model_url)
		ob_out.changed_flags |= WorldObject::MODEL_URL_CHANGED;
	ob_out.model_url = new_model_url;
	checkStringSize(ob_out.model_url, WorldObject::MAX_URL_SIZE);

	const std::string new_script =  QtUtils::toIndString(this->scriptTextEdit->toPlainText());
	if(ob_out.script != new_script)
		ob_out.changed_flags |= WorldObject::SCRIPT_CHANGED;
	ob_out.script = new_script;
	checkStringSize(ob_out.script, WorldObject::MAX_SCRIPT_SIZE);

	const std::string new_content = QtUtils::toIndString(this->contentTextEdit->toPlainText());
	if(ob_out.content != new_content)
		ob_out.changed_flags |= WorldObject::CONTENT_CHANGED;
	ob_out.content = new_content;
	checkStringSize(ob_out.content, WorldObject::MAX_CONTENT_SIZE);

	QString resolved_font_name = this->selected_font_name;
	if(resolved_font_name.isEmpty())
		resolved_font_name = fontNameForComboIndex(this->fontComboBox, this->fontComboBox->currentIndex());
	const std::string new_text_font = QtUtils::toIndString(resolved_font_name);
	if(text_font_feature_supported)
	{
		if(ob_out.text_font != new_text_font)
			ob_out.changed_flags |= WorldObject::TEXT_FONT_CHANGED;
		ob_out.text_font = new_text_font;
		checkStringSize(ob_out.text_font, WorldObject::MAX_FONT_NAME_SIZE);
	}

	if(is_audio_player_webview)
		ob_out.target_url = WorldObject::audioPlayerTargetURL();
	else
		ob_out.target_url = QtUtils::toIndString(this->targetURLLineEdit->text());
	checkStringSize(ob_out.target_url, WorldObject::MAX_URL_SIZE);

	writeTransformMembersToObject(ob_out); // Set ob_out transform members
	
	ob_out.setCollidable(this->collidableCheckBox->isChecked());
	const bool new_dynamic = this->dynamicCheckBox->isChecked();
	if(new_dynamic != ob_out.isDynamic())
		ob_out.changed_flags |= WorldObject::DYNAMIC_CHANGED;
	ob_out.setDynamic(new_dynamic);

	const bool new_is_sensor = this->sensorCheckBox->isChecked();
	if(new_is_sensor != ob_out.isSensor())
		ob_out.changed_flags |= WorldObject::PHYSICS_VALUE_CHANGED;
	ob_out.setIsSensor(new_is_sensor);

	const float new_mass		= (float)this->massDoubleSpinBox->value();
	const float new_friction	= (float)this->frictionDoubleSpinBox->value();
	const float new_restitution	= (float)this->restitutionDoubleSpinBox->value();
	const Vec3f new_COM_offset(
		(float)this->COMOffsetXDoubleSpinBox->value(),
		(float)this->COMOffsetYDoubleSpinBox->value(),
		(float)this->COMOffsetZDoubleSpinBox->value()
	);

	if(new_mass != ob_out.mass || new_friction != ob_out.friction || new_restitution != ob_out.restitution || new_COM_offset != ob_out.centre_of_mass_offset_os)
		ob_out.changed_flags |= WorldObject::PHYSICS_VALUE_CHANGED;

	ob_out.mass = new_mass;
	ob_out.friction = new_friction;
	ob_out.restitution = new_restitution;
	ob_out.centre_of_mass_offset_os = new_COM_offset;


	BitUtils::setOrZeroBit(ob_out.flags, WorldObject::VIDEO_AUTOPLAY, this->videoAutoplayCheckBox->isChecked());
	BitUtils::setOrZeroBit(ob_out.flags, WorldObject::VIDEO_LOOP,     this->videoLoopCheckBox    ->isChecked());
	BitUtils::setOrZeroBit(ob_out.flags, WorldObject::VIDEO_MUTED,    this->videoMutedCheckBox   ->isChecked());

	BitUtils::setOrZeroBit(ob_out.flags, WorldObject::AUDIO_AUTOPLAY, this->audioAutoplayCheckBox->isChecked());
	BitUtils::setOrZeroBit(ob_out.flags, WorldObject::AUDIO_LOOP,     this->audioLoopCheckBox    ->isChecked());
	BitUtils::setOrZeroBit(ob_out.flags, WorldObject::AUDIO_SHUFFLE,  is_audio_player_webview && this->audioShuffleCheckBox->isChecked());
	if(is_audio_player_webview)
	{
		ob_out.audio_player_activation_distance = myClamp((float)this->audioActivationDistanceSpinBox->value(),
			WorldObject::MIN_AUDIO_PLAYER_ACTIVATION_DISTANCE, WorldObject::MAX_AUDIO_PLAYER_ACTIVATION_DISTANCE);
	}

	if(ob_out.object_type != WorldObject::ObjectType_Hypercard) // Don't store materials for hypercards. (doesn't use them, and matEditor may have old/invalid data)
	{
		int mat_index_for_edit = selected_mat_index;
		if(is_audio_player_webview)
			mat_index_for_edit = remapAudioPlayerMaterialIndexForEditor(mat_index_for_edit, cloned_materials.size());

		if(mat_index_for_edit >= (int)cloned_materials.size())
		{
			cloned_materials.resize(mat_index_for_edit + 1);
			for(size_t i=0; i<cloned_materials.size(); ++i)
				if(cloned_materials[i].isNull())
					cloned_materials[i] = new WorldMaterial();
		}

		this->matEditor->toMaterial(*cloned_materials[mat_index_for_edit]);

		ob_out.materials.resize(cloned_materials.size());
		for(size_t i=0; i<cloned_materials.size(); ++i)
			ob_out.materials[i] = cloned_materials[i]->clone();
	}

	// Set the emission_texture_url from the video URL control.  NOTE: needs to go after setting materials above. 
	if(ob_out.object_type == WorldObject::ObjectType_Video)
	{
		if(ob_out.materials.size() >= 1)
		{
			ob_out.materials[0]->emission_texture_url = QtUtils::toIndString(this->videoURLFileSelectWidget->filename());
			checkStringSize(ob_out.materials[0]->emission_texture_url, WorldObject::MAX_URL_SIZE);
		}
	}

	if(ob_out.object_type == WorldObject::ObjectType_Spotlight) // NOTE: is ob_out.object_type set?
	{
		if(ob_out.materials.size() >= 1)
		{
			ob_out.materials[0]->emission_lum_flux_or_lum = (float)this->luminousFluxDoubleSpinBox->value();

			ob_out.materials[0]->colour_rgb = this->spotlight_col;
			ob_out.materials[0]->emission_rgb = this->spotlight_col;
		}

		updateSpotlightColourButton();

		ob_out.type_data.spotlight_data.cone_start_angle = ::degreeToRad(this->spotlightStartAngleSpinBox->value());
		ob_out.type_data.spotlight_data.cone_end_angle   = ::degreeToRad(this->spotlightEndAngleSpinBox->value());
	}

	// For seat:
	if(ob_out.object_type == WorldObject::ObjectType_Seat)
	{
		ob_out.type_data.seat_data.upper_leg_angle = (float)this->upperLegAngleDoubleSpinBox->value();
		ob_out.type_data.seat_data.lower_leg_angle = (float)this->lowerLegAngleDoubleSpinBox->value();
		ob_out.type_data.seat_data.upper_arm_angle = (float)this->upperArmAngleDoubleSpinBox->value();
		ob_out.type_data.seat_data.lower_arm_angle = (float)this->lowerArmAngleDoubleSpinBox->value();
	}

	if(ob_out.object_type == WorldObject::ObjectType_Camera)
	{
		const float fov_deg = myClamp((float)this->cameraFOVYDoubleSpinBox->value(), 5.f, 175.f);
		const float near_dist = myMax(0.01f, (float)this->cameraNearDistDoubleSpinBox->value());
		const float far_dist = myMax(near_dist + 0.01f, (float)this->cameraFarDistDoubleSpinBox->value());

		ob_out.type_data.camera_data.fov_y_rad = ::degreeToRad(fov_deg);
		ob_out.type_data.camera_data.near_dist = near_dist;
		ob_out.type_data.camera_data.far_dist = far_dist;
		ob_out.type_data.camera_data.render_width = (uint16)myClamp(this->cameraRenderWidthSpinBox->value(), 16, 4096);
		ob_out.type_data.camera_data.render_height = (uint16)myClamp(this->cameraRenderHeightSpinBox->value(), 16, 4096);
		ob_out.type_data.camera_data.max_fps = (uint8)myClamp(this->cameraMaxFPSSpinBox->value(), 1, 120);
		ob_out.type_data.camera_data.enabled = this->cameraEnabledCheckBox->isChecked() ? 1 : 0;
	}

	if(ob_out.object_type == WorldObject::ObjectType_CameraScreen)
	{
		uint64 source_camera_uid = 0;
		const std::string source_uid_text = stripHeadWhitespace(stripTailWhitespace(QtUtils::toIndString(this->cameraScreenSourceUIDLineEdit->text())));
		if(!source_uid_text.empty())
		{
			try
			{
				source_camera_uid = stringToUInt64(source_uid_text);
			}
			catch(StringUtilsExcep&)
			{
				source_camera_uid = 0;
			}
		}

		ob_out.type_data.camera_screen_data.source_camera_uid = source_camera_uid;
		ob_out.type_data.camera_screen_data.material_index = (uint16)myClamp(this->cameraScreenMaterialIndexSpinBox->value(), 0, 65535);
		ob_out.type_data.camera_screen_data.enabled = this->cameraScreenEnabledCheckBox->isChecked() ? 1 : 0;
		ob_out.type_data.camera_screen_data._padding = 0;
	}

	const URLString new_audio_source_url = toURLString(QtUtils::toStdString(this->audioFileWidget->filename()));
	if(ob_out.audio_source_url != new_audio_source_url)
		ob_out.changed_flags |= WorldObject::AUDIO_SOURCE_URL_CHANGED;
	ob_out.audio_source_url = new_audio_source_url;
	checkStringSize(ob_out.audio_source_url, WorldObject::MAX_URL_SIZE);

	if(ob_out.object_type == WorldObject::ObjectType_Video)
		ob_out.audio_volume = videoVolumeDoubleSpinBox->value();
	else
		ob_out.audio_volume = volumeDoubleSpinBox->value();
}


void ObjectEditor::writeTransformMembersToObject(WorldObject& ob_out)
{
	ob_out.pos.x = this->posXDoubleSpinBox->value();
	ob_out.pos.y = this->posYDoubleSpinBox->value();
	ob_out.pos.z = this->posZDoubleSpinBox->value();

	ob_out.scale.x = (float)this->scaleXDoubleSpinBox->value();
	ob_out.scale.y = (float)this->scaleYDoubleSpinBox->value();
	ob_out.scale.z = (float)this->scaleZDoubleSpinBox->value();

	const Vec3f angles(
		(float)(this->rotAxisXDoubleSpinBox->value() / 360 * Maths::get2Pi<double>()),
		(float)(this->rotAxisYDoubleSpinBox->value() / 360 * Maths::get2Pi<double>()),
		(float)(this->rotAxisZDoubleSpinBox->value() / 360 * Maths::get2Pi<double>())
	);

	// Convert angles to rotation matrix, then the rotation matrix to axis-angle.

	const Matrix3f rot_matrix = Matrix3f::fromAngles(angles);

	rot_matrix.rotationMatrixToAxisAngle(/*unit axis out=*/ob_out.axis, /*angle out=*/ob_out.angle);

	if(ob_out.axis.length() < 1.0e-5f)
	{
		ob_out.axis = Vec3f(0,0,1);
		ob_out.angle = 0;
	}
}


void ObjectEditor::objectModelURLUpdated(const WorldObject& ob)
{
	this->modelFileSelectWidget->setFilename(QtUtils::toQString(ob.model_url));

	updateInfoLabel(ob); // Update info label, which includes last-modified time.
}


void ObjectEditor::objectLightmapURLUpdated(const WorldObject& ob)
{
	lightmapURLLabel->setText(QtUtils::toQString(ob.lightmap_url));

	if(ob.lightmap_baking)
	{
		lightmapBakeStatusLabel->setText(QCoreApplication::translate("ObjectEditor", "Lightmap is baking..."));
	}
	else
	{
		lightmapBakeStatusLabel->setText(QCoreApplication::translate("ObjectEditor", "Lightmap baked."));
	}

	updateInfoLabel(ob); // Update info label, which includes last-modified time.
}


void ObjectEditor::objectPickedUp()
{
	this->posXDoubleSpinBox->setEnabled(false);
	this->posYDoubleSpinBox->setEnabled(false);
	this->posZDoubleSpinBox->setEnabled(false);
}


void ObjectEditor::objectDropped()
{
	this->posXDoubleSpinBox->setEnabled(true);
	this->posYDoubleSpinBox->setEnabled(true);
	this->posZDoubleSpinBox->setEnabled(true);
}


void ObjectEditor::setControlsEnabled(bool enabled)
{
	this->setEnabled(enabled);
}


void ObjectEditor::setControlsEditable(bool editable)
{
	this->controls_editable = editable;
	this->modelFileSelectWidget->setReadOnly(!editable);
	this->scriptTextEdit->setReadOnly(!editable);
	this->contentTextEdit->setReadOnly(!editable);
	this->fontComboBox->setEnabled(editable && text_font_feature_supported);
	this->targetURLLineEdit->setReadOnly(!editable);

	this->posXDoubleSpinBox->setReadOnly(!editable);
	this->posYDoubleSpinBox->setReadOnly(!editable);
	this->posZDoubleSpinBox->setReadOnly(!editable);

	this->scaleXDoubleSpinBox->setReadOnly(!editable);
	this->scaleYDoubleSpinBox->setReadOnly(!editable);
	this->scaleZDoubleSpinBox->setReadOnly(!editable);

	this->rotAxisXDoubleSpinBox->setReadOnly(!editable);
	this->rotAxisYDoubleSpinBox->setReadOnly(!editable);
	this->rotAxisZDoubleSpinBox->setReadOnly(!editable);

	this->collidableCheckBox->setEnabled(editable);
	this->dynamicCheckBox->setEnabled(editable);
	this->sensorCheckBox->setEnabled(editable);

	this->matEditor->setControlsEditable(editable);

	this->editScriptPushButton->setEnabled(editable);
	this->bakeLightmapPushButton->setEnabled(editable);
	this->bakeLightmapHighQualPushButton->setEnabled(editable);
	this->removeLightmapPushButton->setEnabled(editable);

	this->audioFileWidget->setReadOnly(!editable);
	this->volumeDoubleSpinBox->setReadOnly(!editable);
	this->audioAutoplayCheckBox->setEnabled(editable);
	this->audioLoopCheckBox->setEnabled(editable);
	this->audioShuffleCheckBox->setEnabled(editable);
	this->audioActivationDistanceSpinBox->setReadOnly(!editable);
	this->audioPlaylistListWidget->setEnabled(editable);
	this->audioPlaylistListWidget->setEditTriggers(editable ? (QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed | QAbstractItemView::SelectedClicked) : QAbstractItemView::NoEditTriggers);
	updateAudioPlaylistButtonsEnabled();

	this->cameraEnabledCheckBox->setEnabled(editable);
	this->cameraFOVYDoubleSpinBox->setReadOnly(!editable);
	this->cameraNearDistDoubleSpinBox->setReadOnly(!editable);
	this->cameraFarDistDoubleSpinBox->setReadOnly(!editable);
	this->cameraRenderWidthSpinBox->setReadOnly(!editable);
	this->cameraRenderHeightSpinBox->setReadOnly(!editable);
	this->cameraMaxFPSSpinBox->setReadOnly(!editable);

	this->cameraScreenEnabledCheckBox->setEnabled(editable);
	this->cameraScreenSourceUIDLineEdit->setReadOnly(!editable);
	this->cameraScreenMaterialIndexSpinBox->setReadOnly(!editable);
}


void ObjectEditor::setTextFontFeatureSupported(bool supported)
{
	this->text_font_feature_supported = supported;
	this->fontComboBox->setEnabled(this->controls_editable && supported);

	const QString tooltip = supported ?
		QString() :
		QCoreApplication::translate("ObjectEditor", "This server does not support text font selection yet. Requires server protocol version 51 or newer.");

	this->fontComboBox->setToolTip(tooltip);
	this->fontLabel->setToolTip(tooltip);
}


void ObjectEditor::on_audioAddTracksPushButton_clicked(bool)
{
	if(!this->controls_editable)
		return;

	const QString last_audio_dir = this->settings ? this->settings->value("mainwindow/lastAudioFileDir").toString() : QString();
	const QStringList selected_filenames = QFileDialog::getOpenFileNames(
		this,
		QCoreApplication::translate("ObjectEditor", "Select audio file(s)..."),
		last_audio_dir,
		QCoreApplication::translate("ObjectEditor", "Audio file (*.mp3 *.wav *.aac *.m4a *.ogg *.opus *.flac)")
	);

	if(selected_filenames.isEmpty())
		return;

	if(this->settings)
		this->settings->setValue("mainwindow/lastAudioFileDir", QtUtils::toQString(FileUtils::getDirectory(QtUtils::toIndString(selected_filenames[0]))));

	this->syncing_audio_playlist_widget = true;
	for(int i=0; i<selected_filenames.size(); ++i)
		addAudioPlaylistEntry(selected_filenames[i], i + 1 == selected_filenames.size());
	this->syncing_audio_playlist_widget = false;

	syncContentFromAudioPlaylistWidget();
	emit objectChanged();
}


void ObjectEditor::on_audioAddURLPushButton_clicked(bool)
{
	if(!this->controls_editable)
		return;

	bool ok = false;
	const QString value = QInputDialog::getText(
		this,
		QCoreApplication::translate("ObjectEditor", "Add Playlist Entry"),
		QCoreApplication::translate("ObjectEditor", "Audio/Radio stream URL or local path:"),
		QLineEdit::Normal,
		QString(),
		&ok
	);
	if(!ok)
		return;

	this->syncing_audio_playlist_widget = true;
	addAudioPlaylistEntry(value, true);
	this->syncing_audio_playlist_widget = false;

	syncContentFromAudioPlaylistWidget();
	emit objectChanged();
}


void ObjectEditor::on_audioRemoveTrackPushButton_clicked(bool)
{
	if(!this->controls_editable)
		return;

	const int current_row = this->audioPlaylistListWidget->currentRow();
	if(current_row < 0)
		return;

	delete this->audioPlaylistListWidget->takeItem(current_row);

	if(current_row < this->audioPlaylistListWidget->count())
		this->audioPlaylistListWidget->setCurrentRow(current_row);
	else if(this->audioPlaylistListWidget->count() > 0)
		this->audioPlaylistListWidget->setCurrentRow(this->audioPlaylistListWidget->count() - 1);

	syncContentFromAudioPlaylistWidget();
	emit objectChanged();
}


void ObjectEditor::on_audioMoveTrackUpPushButton_clicked(bool)
{
	if(!this->controls_editable)
		return;

	const int current_row = this->audioPlaylistListWidget->currentRow();
	if(current_row <= 0)
		return;

	QListWidgetItem* item = this->audioPlaylistListWidget->takeItem(current_row);
	this->audioPlaylistListWidget->insertItem(current_row - 1, item);
	this->audioPlaylistListWidget->setCurrentRow(current_row - 1);

	syncContentFromAudioPlaylistWidget();
	emit objectChanged();
}


void ObjectEditor::on_audioMoveTrackDownPushButton_clicked(bool)
{
	if(!this->controls_editable)
		return;

	const int current_row = this->audioPlaylistListWidget->currentRow();
	if(current_row < 0 || current_row + 1 >= this->audioPlaylistListWidget->count())
		return;

	QListWidgetItem* item = this->audioPlaylistListWidget->takeItem(current_row);
	this->audioPlaylistListWidget->insertItem(current_row + 1, item);
	this->audioPlaylistListWidget->setCurrentRow(current_row + 1);

	syncContentFromAudioPlaylistWidget();
	emit objectChanged();
}


void ObjectEditor::audioPlaylistItemChanged(QListWidgetItem*)
{
	syncContentFromAudioPlaylistWidget();
	emit objectChanged();
}


void ObjectEditor::audioPlaylistSelectionChanged()
{
	updateAudioPlaylistButtonsEnabled();
}


void ObjectEditor::on_visitURLLabel_linkActivated(const QString&)
{
	std::string url = QtUtils::toStdString(this->targetURLLineEdit->text());
	if(StringUtils::containsString(url, "://"))
	{
		// URL already has protocol prefix
		const std::string protocol = url.substr(0, url.find("://", 0));
		if(protocol == "http" || protocol == "https")
		{
			QDesktopServices::openUrl(QtUtils::toQString(url));
		}
		else
		{
			// Don't open this URL, might be something potentially unsafe like a file on disk
			QErrorMessage m;
			m.showMessage("This URL is potentially unsafe and will not be opened.");
			m.exec();
		}
	}
	else
	{
		url = "http://" + url;
		QDesktopServices::openUrl(QtUtils::toQString(url));
	}
}


void ObjectEditor::on_materialComboBox_currentIndexChanged(int index)
{
	this->selected_mat_index = index;

	int editor_mat_index = index;
	if(this->editing_audio_player_webview)
		editor_mat_index = remapAudioPlayerMaterialIndexForEditor(index, this->cloned_materials.size());

	if(editor_mat_index < (int)this->cloned_materials.size())
		this->matEditor->setFromMaterial(*this->cloned_materials[editor_mat_index]);
}


void ObjectEditor::on_newMaterialPushButton_clicked(bool checked)
{
	this->selected_mat_index = this->materialComboBox->count();

	this->materialComboBox->addItem(QtUtils::toQString("Material " + toString(selected_mat_index)), selected_mat_index);

	{
		SignalBlocker blocker(this->materialComboBox);
		this->materialComboBox->setCurrentIndex(this->selected_mat_index);
	}

	this->cloned_materials.push_back(new WorldMaterial());
	this->matEditor->setFromMaterial(*this->cloned_materials.back());

	emit objectChanged();
}


void ObjectEditor::on_editScriptPushButton_clicked(bool checked)
{
	if(!shader_editor)
	{
		shader_editor = new ShaderEditorDialog(this, base_dir_path);

		shader_editor->setWindowTitle("Script Editor");

		QObject::connect(shader_editor, SIGNAL(shaderChanged()), SLOT(scriptChangedFromEditor()));

		QObject::connect(shader_editor, SIGNAL(openServerScriptLogSignal()), this, SIGNAL(openServerScriptLogSignal()));
	}

	shader_editor->initialise(QtUtils::toIndString(this->scriptTextEdit->toPlainText()));

	shader_editor->show();
	shader_editor->raise();
}


void ObjectEditor::on_bakeLightmapPushButton_clicked(bool checked)
{
	lightmapBakeStatusLabel->setText(QCoreApplication::translate("ObjectEditor", "Lightmap is baking..."));

	emit bakeObjectLightmap();
}


void ObjectEditor::on_bakeLightmapHighQualPushButton_clicked(bool checked)
{
	lightmapBakeStatusLabel->setText(QCoreApplication::translate("ObjectEditor", "Lightmap is baking..."));

	emit bakeObjectLightmapHighQual();
}


void ObjectEditor::on_removeLightmapPushButton_clicked(bool checked)
{
	this->lightmapURLLabel->clear();
	emit removeLightmapSignal();
}


void ObjectEditor::targetURLChanged()
{
	this->visitURLLabel->setVisible(!this->targetURLLineEdit->text().isEmpty());
}


void ObjectEditor::scriptTextEditChanged()
{
	edit_timer->start();

	if(shader_editor)
		shader_editor->update(QtUtils::toIndString(scriptTextEdit->toPlainText()));
}


void ObjectEditor::scriptChangedFromEditor()
{
	{
		SignalBlocker b(this->scriptTextEdit);
		this->scriptTextEdit->setPlainText(shader_editor->getShaderText());
	}

	emit scriptChangedFromEditorSignal(); // objectChanged();
}


void ObjectEditor::editTimerTimeout()
{
	emit objectChanged();
}


void ObjectEditor::materialSelectedInBrowser(const std::string& path)
{
	// Load material
	try
	{
		WorldMaterialRef mat = WorldMaterial::loadFromXMLOnDisk(path, /*convert_rel_paths_to_abs_disk_paths=*/true);

		if(selected_mat_index >= 0 && selected_mat_index < (int)this->cloned_materials.size())
		{
			this->cloned_materials[this->selected_mat_index] = mat;
			this->matEditor->setFromMaterial(*mat);

			emit objectChanged();
		}
	}
	catch(glare::Exception& e)
	{
		QErrorMessage m;
		m.showMessage("Error while opening material: " + QtUtils::toQString(e.what()));
		m.exec();
	}
}


void ObjectEditor::printFromLuaScript(const std::string& msg, UID object_uid)
{
	if((this->editing_ob_uid == object_uid) && shader_editor)
		shader_editor->printFromLuaScript(msg);
}


void ObjectEditor::luaErrorOccurred(const std::string& msg, UID object_uid)
{
	if((this->editing_ob_uid == object_uid) && shader_editor)
		shader_editor->luaErrorOccurred(msg);
}


void ObjectEditor::xScaleChanged(double new_x)
{
	if(this->linkScaleCheckBox->isChecked())
	{
		// Update y and z scales to maintain the previous ratios between scales.
		
		// we want new_y / new_x = old_y / old_x
		// new_y = (old_y / old_x) * new_x
		// new_y = new_x * old_y / old_x = new_x * (old_y / old_x) = new_x / (old_x / old_y)
		double new_y = new_x / last_x_scale_over_y_scale;

		// new_z = (old_z / old_x) * new_x = new_x / (old_x / old_z)
		double new_z = new_x / last_x_scale_over_z_scale;

		SignalBlocker::setValue(scaleYDoubleSpinBox, new_y);
		SignalBlocker::setValue(scaleZDoubleSpinBox, new_z);
	}
	else
	{
		// x value has changed, so update ratios.
		this->last_x_scale_over_z_scale = new_x / scaleZDoubleSpinBox->value();
		this->last_x_scale_over_y_scale = new_x / scaleYDoubleSpinBox->value();
	}

	emit objectTransformChanged();
}


void ObjectEditor::yScaleChanged(double new_y)
{
	if(this->linkScaleCheckBox->isChecked())
	{
		// Update x and z scales to maintain the previous ratios between scales.

		// we want new_x / new_y = old_x / old_y
		// new_x = (old_x / old_y) * new_y
		double new_x = last_x_scale_over_y_scale * new_y;

		// we want new_z / new_y = old_z / old_y
		// new_z = (old_z / old_y) * new_y = new_y / (old_y / old_z)
		double new_z = new_y / last_y_scale_over_z_scale;

		SignalBlocker::setValue(scaleXDoubleSpinBox, new_x);
		SignalBlocker::setValue(scaleZDoubleSpinBox, new_z);
	}
	else
	{
		// y value has changed, so update ratios.
		this->last_x_scale_over_y_scale = scaleXDoubleSpinBox->value() / new_y;
		this->last_y_scale_over_z_scale = new_y / scaleZDoubleSpinBox->value();
	}

	emit objectTransformChanged();
}


void ObjectEditor::zScaleChanged(double new_z)
{
	if(this->linkScaleCheckBox->isChecked())
	{
		// Update x and y scales to maintain the previous ratios between scales.
		
		// we want new_x / new_z = old_x / old_z
		// new_x = (old_x / old_z) * new_z
		double new_x = last_x_scale_over_z_scale * new_z;

		// we want new_y / new_z = old_y / old_z
		// new_y = (old_y / old_z) * new_z
		double new_y = last_y_scale_over_z_scale * new_z;

		// Set x and y scales
		SignalBlocker::setValue(scaleXDoubleSpinBox, new_x);
		SignalBlocker::setValue(scaleYDoubleSpinBox, new_y);
	}
	else
	{
		// z value has changed, so update ratios.
		this->last_x_scale_over_z_scale = scaleXDoubleSpinBox->value() / new_z;
		this->last_y_scale_over_z_scale = scaleYDoubleSpinBox->value() / new_z;
	}

	emit objectTransformChanged();
}


void ObjectEditor::linkScaleCheckBoxToggled(bool val)
{
	assert(settings);
	if(settings)
		settings->setValue("objectEditor/linkScaleCheckBoxChecked", this->linkScaleCheckBox->isChecked());
}


void ObjectEditor::onFontChanged(int index)
{
	if(!text_font_feature_supported)
		return;

	this->selected_font_name = fontNameForComboIndex(this->fontComboBox, index);

	// Defer the object update until the ComboBox has fully committed the new current item.
	QTimer::singleShot(0, this, SLOT(editTimerTimeout()));
}


void ObjectEditor::updateSpotlightColourButton()
{
	const int COLOUR_BUTTON_W = 30;
	QImage image(COLOUR_BUTTON_W, COLOUR_BUTTON_W, QImage::Format_RGB32);
	image.fill(QColor(qRgba(
		(int)(this->spotlight_col.r * 255),
		(int)(this->spotlight_col.g * 255),
		(int)(this->spotlight_col.b * 255),
		255
	)));
	QIcon icon;
	QPixmap pixmap = QPixmap::fromImage(image);
	icon.addPixmap(pixmap);
	this->spotlightColourPushButton->setIcon(icon);
	this->spotlightColourPushButton->setIconSize(QSize(COLOUR_BUTTON_W, COLOUR_BUTTON_W));
}


void ObjectEditor::on_spotlightColourPushButton_clicked(bool checked)
{
	const QColor initial_col(qRgba(
		(int)(spotlight_col.r * 255),
		(int)(spotlight_col.g * 255),
		(int)(spotlight_col.b * 255),
		255
	));

	QColorDialog d(initial_col, this);
	const int res = d.exec();
	if(res == QDialog::Accepted)
	{
		const QColor new_col = d.currentColor();

		this->spotlight_col.r = new_col.red()   / 255.f;
		this->spotlight_col.g = new_col.green() / 255.f;
		this->spotlight_col.b = new_col.blue()  / 255.f;

		updateSpotlightColourButton();

		emit objectChanged();
	}
}


void ObjectEditor::loadAvailableFonts()
{
	// Clear existing items
	this->fontComboBox->clear();

	// Add a default font option
	addFontComboItem(this->fontComboBox, "Default", QIcon());
	this->selected_font_name = "Default";

	// Try to load fonts from the packaged font directory first.
	// Installed builds stage fonts under data/resources/fonts, while some dev setups still use resources/fonts.
	std::vector<std::string> possible_paths;
	
	// Try 1: packaged paths relative to the resolved app base dir.
	if(!base_dir_path.empty())
	{
		possible_paths.push_back(base_dir_path + "/data/resources/fonts");
		possible_paths.push_back(base_dir_path + "/resources/fonts");
	}
	
	// Try 2: packaged paths relative to the current working directory.
	possible_paths.push_back("./data/resources/fonts");
	possible_paths.push_back("data/resources/fonts");

	// Try 3: legacy/current working directory fallbacks.
	possible_paths.push_back("./resources/fonts");
	possible_paths.push_back("resources/fonts");
	
	// Try 4: relative to executable / older layouts
	possible_paths.push_back("../data/resources/fonts");
	possible_paths.push_back("../../data/resources/fonts");
	possible_paths.push_back("../resources/fonts");
	possible_paths.push_back("../../resources/fonts");
	
	// Try 5: absolute development path fallback.
	possible_paths.push_back("C:/programming/substrata/resources/fonts");
	
	for(const auto& fonts_dir : possible_paths)
	{
		try
		{
			if(FileUtils::fileExists(fonts_dir))
			{
				// Use Qt to list files
				QDir font_dir(QtUtils::toQString(fonts_dir));
				QStringList font_filters;
				font_filters << "*.ttf" << "*.otf" << "*.fon" << "*.woff";
				
				QFileInfoList files = font_dir.entryInfoList(font_filters, QDir::Files | QDir::NoDotAndDotDot);
				
				if(files.size() > 0)
				{
					TextRendererRef preview_renderer = new TextRenderer();

					// Sort files by name
					std::sort(files.begin(), files.end(), [](const QFileInfo& a, const QFileInfo& b) {
						return a.baseName() < b.baseName();
					});
					
					for(const QFileInfo& file_info : files)
					{
						const QString font_name = file_info.baseName();
						const QIcon preview_icon = makeFontPreviewIcon(preview_renderer, makeFontPreviewText(font_name), QtUtils::toIndString(file_info.absoluteFilePath()));
						addFontComboItem(this->fontComboBox, font_name, preview_icon);
					}
					
					if(this->fontComboBox->view())
						this->fontComboBox->view()->setMinimumWidth(520);

					// Successfully loaded fonts, break out of loop
					break;
				}
			}
		}
		catch(const std::exception&)
		{
			// Continue to next path
		}
	}
}
