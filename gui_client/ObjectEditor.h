#pragma once


#include "../shared/WorldObject.h"
#include "../utils/IncludeWindows.h" // This needs to go first for NOMINMAX.
#include "../opengl/OpenGLEngine.h"
#include "../maths/vec2.h"
#include "../maths/vec3.h"
#include "../utils/Timer.h"
#include "../utils/Reference.h"
#include "../utils/RefCounted.h"
#include "ui_ObjectEditor.h"
#include <QtCore/QEvent>
#include <map>


namespace Indigo { class Mesh; }
class TextureServer;
class EnvEmitter;
class ShaderEditorDialog;
class QCheckBox;
class QComboBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class RealControl;
class ParticleCurveWidget;


class ObjectEditor : public QWidget, public Ui::ObjectEditor
{
	Q_OBJECT        // must include this if you use Qt signals/slots

public:
	ObjectEditor(QWidget *parent = 0);
	~ObjectEditor();

	void init(); // settings should be set before this.

	void setFromObject(const WorldObject& ob, int selected_mat_index, bool ob_in_editing_users_world);
	void setTransformFromObject(const WorldObject& ob);

	void objectLastModifiedUpdated(const WorldObject& ob) { updateInfoLabel(ob); }

	void toObject(WorldObject& ob_out);
	void writeTransformMembersToObject(WorldObject& ob_out);

	// Object details were updated from outside of the editor, for example due to an update message from the server.
	void objectModelURLUpdated(const WorldObject& ob);
	void objectLightmapURLUpdated(const WorldObject& ob);

	void objectPickedUp();
	void objectDropped();

	void setControlsEnabled(bool enabled);

	void setControlsEditable(bool editable);
	void setTextFontFeatureSupported(bool supported);

	int getSelectedMatIndex() const { return selected_mat_index; }

	void materialSelectedInBrowser(const std::string& path);

	bool posAndRot3DControlsEnabled() { return show3DControlsCheckBox->isChecked(); }

	void printFromLuaScript(const std::string& msg, UID object_uid);
	void luaErrorOccurred(const std::string& msg, UID object_uid);
	void setParticleDiagnostics(size_t selected_emitter_particle_count, size_t total_particle_count);

	QSettings* settings;
	std::string base_dir_path;
protected:
	virtual void changeEvent(QEvent* event) override;

signals:;
	void objectTransformChanged();
	void objectChanged();
	void scriptChangedFromEditorSignal();
	void particleBurstNowSignal();
	void particleClearParticlesSignal();
	void bakeObjectLightmap();
	void bakeObjectLightmapHighQual();
	void removeLightmapSignal();
	void posAndRot3DControlsToggled();
	void openServerScriptLogSignal();
	
private slots:
	void on_visitURLLabel_linkActivated(const QString& link);
	void on_materialComboBox_currentIndexChanged(int index);
	void on_newMaterialPushButton_clicked(bool checked);
	void on_audioAddTracksPushButton_clicked(bool checked);
	void on_audioAddURLPushButton_clicked(bool checked);
	void on_audioRemoveTrackPushButton_clicked(bool checked);
	void on_audioMoveTrackUpPushButton_clicked(bool checked);
	void on_audioMoveTrackDownPushButton_clicked(bool checked);
	void targetURLChanged();
	void scriptTextEditChanged();
	void scriptChangedFromEditor();
	void on_editScriptPushButton_clicked(bool checked);
	void on_bakeLightmapPushButton_clicked(bool checked);
	void on_bakeLightmapHighQualPushButton_clicked(bool checked);
	void on_removeLightmapPushButton_clicked(bool checked);
	void editTimerTimeout();
	void xScaleChanged(double val);
	void yScaleChanged(double val);
	void zScaleChanged(double val);
	void linkScaleCheckBoxToggled(bool val);
	void on_spotlightColourPushButton_clicked(bool checked);
	void onFontChanged(int index);
	void audioPlaylistItemChanged(QListWidgetItem* item);
	void audioPlaylistSelectionChanged();
	void audioDirectionalityToggled(bool checked);
	void audioScheduleToggled(bool checked);
	void on_particleColourPushButton_clicked(bool checked);
	void on_particleEndColourPushButton_clicked(bool checked);
	void particlePresetChanged(int index);
	void saveParticlePreset();
	void deleteParticlePreset();
	void browseParticleSprite();
	void clearParticleSprite();
	void particleSpriteLibraryChanged(int index);
	void browseParticleAudio();
	void clearParticleAudio();
	void showParticleHelp();
	void updateParticlePreviewThumbnail();
	void applyPortalStylePreset();
	void resetPortalMaterials();
	void setPortalTargetMainWorld();
	void setPortalTargetMapWorld();
	void clearPortalTarget();
	void showPortalHelp();

private:
	void updateInfoLabel(const WorldObject& ob);
	void updateSpotlightColourButton();
	void updateParticleColourButton();
	void loadAvailableFonts();
	void syncAudioPlaylistWidgetFromContent(const std::string& content);
	void syncContentFromAudioPlaylistWidget();
	void addAudioPlaylistEntry(const QString& value, bool make_current);
	void updateAudioPlaylistButtonsEnabled();
	void retranslateDynamicAudioPlayerUI();
	void createPortalEditorUI();
	void retranslateDynamicPortalUI();
	void createParticleEditorUI();
	void retranslateDynamicParticleUI();
	void loadCustomParticlePresets();
	void setParticleControlsFromContent(const std::string& content);
	void setParticleControlsFromSettings(const struct ParticleEmitterSettings& settings);
	struct ParticleEmitterSettings particleControlsToSettings() const;
	// Store a cloned copy of the materials.
	// The reason for having this is so if the user selected another material,
	// we can display it, without needing to hang on to a reference to the original world object.
	std::vector<WorldMaterialRef> cloned_materials;

	UID editing_ob_uid;
	WorldObject::ObjectType editing_object_type;
	int selected_mat_index;

	QTimer* edit_timer;

	ShaderEditorDialog* shader_editor;

	// Store the ratios between the scale components, used when link-scales is enabled.
	// Store ratios instead of individual values, as this allows us to preserve the rations if the scales are set to zero at some point.
	double last_x_scale_over_z_scale;
	double last_x_scale_over_y_scale;
	double last_y_scale_over_z_scale;
	QString selected_font_name;
	bool controls_editable;
	bool text_font_feature_supported;
	bool syncing_audio_playlist_widget;
	bool editing_audio_player_webview;
	bool editing_particle_emitter;

	QCheckBox* audioShuffleCheckBox;
	QLabel* audioActivationDistanceLabel;
	RealControl* audioActivationDistanceSpinBox;
	QLabel* audioSoundRadiusLabel;
	RealControl* audioSoundRadiusSpinBox;
	QCheckBox* audioDirectionalityEnabledCheckBox;
	QLabel* audioDirectivityAlphaLabel;
	RealControl* audioDirectivityAlphaSpinBox;
	QLabel* audioDirectivityOrderLabel;
	RealControl* audioDirectivityOrderSpinBox;
	QLabel* audioSpreadDegreesLabel;
	RealControl* audioSpreadDegreesSpinBox;
	QCheckBox* audioScheduleEnabledCheckBox;
	QLabel* audioScheduleStartHourLabel;
	RealControl* audioScheduleStartHourSpinBox;
	QLabel* audioScheduleEndHourLabel;
	RealControl* audioScheduleEndHourSpinBox;
	QGroupBox* audioPlaylistGroupBox;
	QListWidget* audioPlaylistListWidget;
	QPushButton* audioAddTracksPushButton;
	QPushButton* audioAddURLPushButton;
	QPushButton* audioRemoveTrackPushButton;
	QPushButton* audioMoveTrackUpPushButton;
	QPushButton* audioMoveTrackDownPushButton;

	QGroupBox* portalGroupBox;
	QLabel* portalStyleLabel;
	QComboBox* portalStyleComboBox;
	QPushButton* portalApplyStylePushButton;
	QPushButton* portalResetMaterialsPushButton;
	QPushButton* portalMainWorldPushButton;
	QPushButton* portalMapWorldPushButton;
	QPushButton* portalClearTargetPushButton;
	QPushButton* portalHelpPushButton;
	QLabel* portalTipLabel;

	QGroupBox* particleGroupBox;
	QPushButton* particleHelpPushButton;
	QLabel* particlePresetLabel;
	QComboBox* particlePresetComboBox;
	QPushButton* particleSavePresetPushButton;
	QPushButton* particleDeletePresetPushButton;
	QCheckBox* particleEnabledCheckBox;
	QLabel* particleKindLabel;
	QComboBox* particleKindComboBox;
	QLabel* particleDirectionLabel;
	QComboBox* particleDirectionComboBox;
	QLabel* particleShapeLabel;
	QComboBox* particleShapeComboBox;
	QLabel* particleRenderModeLabel;
	QComboBox* particleRenderModeComboBox;
	QLabel* particlePreviewLabel;
	QLabel* particleSpriteLibraryLabel;
	QComboBox* particleSpriteLibraryComboBox;
	QLabel* particleSpritePathLabel;
	QLineEdit* particleSpritePathLineEdit;
	QPushButton* particleSpriteBrowsePushButton;
	QPushButton* particleSpriteClearPushButton;
	QCheckBox* particleAudioEnabledCheckBox;
	QLabel* particleAudioURLLabel;
	QLineEdit* particleAudioURLLineEdit;
	QPushButton* particleAudioBrowsePushButton;
	QPushButton* particleAudioClearPushButton;
	QCheckBox* particleAudioLoopCheckBox;
	QCheckBox* particleAudioSpatialCheckBox;
	QLabel* particleAudioVolumeLabel;
	RealControl* particleAudioVolumeSpinBox;
	QLabel* particleAudioActivationDistanceLabel;
	RealControl* particleAudioActivationDistanceSpinBox;
	QLabel* particleAudioMinDistanceLabel;
	RealControl* particleAudioMinDistanceSpinBox;
	QLabel* particleAudioMaxDistanceLabel;
	RealControl* particleAudioMaxDistanceSpinBox;
	QLabel* particleAudioFadeInLabel;
	RealControl* particleAudioFadeInSpinBox;
	QLabel* particleAudioFadeOutLabel;
	RealControl* particleAudioFadeOutSpinBox;
	QLabel* particleRateLabel;
	RealControl* particleRateSpinBox;
	QLabel* particleFrameCapLabel;
	RealControl* particleFrameCapSpinBox;
	QLabel* particleMaxParticlesLabel;
	RealControl* particleMaxParticlesSpinBox;
	QLabel* particleRadiusLabel;
	RealControl* particleRadiusSpinBox;
	QLabel* particleSpeedLabel;
	RealControl* particleSpeedSpinBox;
	QLabel* particleSpeedJitterLabel;
	RealControl* particleSpeedJitterSpinBox;
	QLabel* particleSpreadLabel;
	RealControl* particleSpreadSpinBox;
	QLabel* particleTurbulenceLabel;
	RealControl* particleTurbulenceSpinBox;
	QLabel* particleLifetimeLabel;
	RealControl* particleLifetimeSpinBox;
	QLabel* particleStartWidthLabel;
	RealControl* particleStartWidthSpinBox;
	QLabel* particleEndWidthLabel;
	RealControl* particleEndWidthSpinBox;
	QLabel* particleSizeCurveLabel;
	ParticleCurveWidget* particleSizeCurveWidget;
	QLabel* particleSizeJitterLabel;
	RealControl* particleSizeJitterSpinBox;
	QLabel* particleOpacityLabel;
	RealControl* particleOpacitySpinBox;
	QLabel* particleEndOpacityLabel;
	RealControl* particleEndOpacitySpinBox;
	QLabel* particleOpacityCurveLabel;
	ParticleCurveWidget* particleOpacityCurveWidget;
	QLabel* particleOpacityJitterLabel;
	RealControl* particleOpacityJitterSpinBox;
	QLabel* particleColourLabel;
	QPushButton* particleColourPushButton;
	QLabel* particleEndColourLabel;
	QPushButton* particleEndColourPushButton;
	QLabel* particleColourJitterLabel;
	RealControl* particleColourJitterSpinBox;
	QLabel* particleTrailLengthLabel;
	RealControl* particleTrailLengthSpinBox;
	QLabel* particleGlowStrengthLabel;
	RealControl* particleGlowStrengthSpinBox;
	QLabel* particleRotationLabel;
	RealControl* particleRotationSpinBox;
	QLabel* particleRotationJitterLabel;
	RealControl* particleRotationJitterSpinBox;
	QLabel* particleSpinLabel;
	RealControl* particleSpinSpinBox;
	QLabel* particleSpinJitterLabel;
	RealControl* particleSpinJitterSpinBox;
	QCheckBox* particleBurstEnabledCheckBox;
	QLabel* particleBurstCountLabel;
	RealControl* particleBurstCountSpinBox;
	QLabel* particleBurstIntervalLabel;
	RealControl* particleBurstIntervalSpinBox;
	QLabel* particleMaxDistanceLabel;
	RealControl* particleMaxDistanceSpinBox;
	QPushButton* particleBurstNowPushButton;
	QPushButton* particleClearParticlesPushButton;
	QLabel* particleDiagnosticsLabel;
	QLabel* particleDiagnosticsValueLabel;
	QLabel* particleWindXLabel;
	RealControl* particleWindXSpinBox;
	QLabel* particleWindYLabel;
	RealControl* particleWindYSpinBox;
	QLabel* particleWindZLabel;
	RealControl* particleWindZSpinBox;
	QLabel* particleVortexStrengthLabel;
	RealControl* particleVortexStrengthSpinBox;
	QLabel* particleAttractorStrengthLabel;
	RealControl* particleAttractorStrengthSpinBox;
	QLabel* particleAttractorRadiusLabel;
	RealControl* particleAttractorRadiusSpinBox;
	QCheckBox* particleBlackHoleCheckBox;
	QLabel* particleEventHorizonLabel;
	RealControl* particleEventHorizonSpinBox;
	QLabel* particleRadialAccelLabel;
	RealControl* particleRadialAccelSpinBox;
	QLabel* particleLinearDampingLabel;
	RealControl* particleLinearDampingSpinBox;
	QLabel* particleBuoyancyLiftLabel;
	RealControl* particleBuoyancyLiftSpinBox;
	QLabel* particleGravityScaleLabel;
	RealControl* particleGravityScaleSpinBox;
	QLabel* particleDragAreaLabel;
	RealControl* particleDragAreaSpinBox;
	QLabel* particleMassLabel;
	RealControl* particleMassSpinBox;
	QLabel* particleRestitutionLabel;
	RealControl* particleRestitutionSpinBox;
	QLabel* particleCollisionFrictionLabel;
	RealControl* particleCollisionFrictionSpinBox;
	QCheckBox* particleCollideSurfacesCheckBox;
	QCheckBox* particleDieOnSurfaceCheckBox;

	Colour3f spotlight_col;
	Colour3f particle_col;
	Colour3f particle_end_col;
};
