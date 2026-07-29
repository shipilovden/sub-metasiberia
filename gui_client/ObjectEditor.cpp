#include "ObjectEditor.h"
#include "ParticleEmitterSettings.h"
#include "RuntimeTranslation.h"


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
#include "../shared/GaussianSplatAsset.h"
#include "../shared/GaussianSplatData.h"
#include <QtGui/QIcon>
#include <QtGui/QImage>
#include <QtGui/QMouseEvent>
#include <QtGui/QPainter>
#include <QtGui/QPainterPath>
#include <QtGui/QPaintEvent>
#include <QtGui/QPixmap>
#include <QtGui/QDesktopServices>
#include <QtWidgets/QErrorMessage>
#include <QtWidgets/QAbstractItemView>
#include <QtWidgets/QApplication>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDialog>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QFrame>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QInputDialog>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSizePolicy>
#include <QtWidgets/QTextBrowser>
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
#include <cmath>
#include <functional>


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


RuntimeTranslation::UILanguage currentUILanguageForObjectEditor(const QSettings* settings)
{
	QString lang_value;
	if(QApplication::instance())
		lang_value = QApplication::instance()->property("metasiberia.ui_language").toString();

	if(lang_value.isEmpty() && settings)
		lang_value = settings->value("setting/ui_language", settings->value("ui/language", QStringLiteral("en"))).toString();

	const QString lower = lang_value.trimmed().toLower();
	if(lower.startsWith("ru") || lower == "russian")
		return RuntimeTranslation::UILanguage::Russian;

	return RuntimeTranslation::UILanguage::English;
}


QString translateObjectEditorRuntimeText(RuntimeTranslation::UILanguage language, const char* source_text)
{
	if(!source_text)
		return QString();

	const QString source_qstr = QString::fromUtf8(source_text);
	if(language != RuntimeTranslation::UILanguage::Russian)
		return source_qstr;

	static RuntimeTranslation::RuntimeTranslator translator;
	const QString translated = translator.translate("ObjectEditor", source_text, nullptr, -1);
	return translated.isEmpty() ? source_qstr : translated;
}


QString particleCustomPresetNamesKey()
{
	return QStringLiteral("object_editor/particle_custom_preset_names");
}


QString particleCustomPresetContentKey(const QString& name)
{
	return QStringLiteral("object_editor/particle_custom_preset/") + QString::fromLatin1(name.toUtf8().toHex());
}


bool particlePresetDataIsUserPreset(const QString& data)
{
	return data.startsWith(QStringLiteral("user:"));
}


QString particlePresetUserNameFromData(const QString& data)
{
	return particlePresetDataIsUserPreset(data) ? data.mid(5) : QString();
}


float previewCurveValue(const ParticleEmitterSettings::Curve curve, const float t_, const float custom_mid)
{
	const float t = myClamp(t_, 0.f, 1.f);
	switch(curve)
	{
	case ParticleEmitterSettings::Curve_Linear:     return t;
	case ParticleEmitterSettings::Curve_EaseIn:     return t * t;
	case ParticleEmitterSettings::Curve_EaseOut:    return 1.f - (1.f - t) * (1.f - t);
	case ParticleEmitterSettings::Curve_SmoothStep: return t * t * (3.f - 2.f * t);
	case ParticleEmitterSettings::Curve_Custom:
	{
		const float mid = myClamp(custom_mid, 0.f, 1.f);
		return t < 0.5f ? 2.f * mid * t : mid + (1.f - mid) * (2.f * t - 1.f);
	}
	}
	return t;
}


QString curveLabel(const ParticleEmitterSettings::Curve curve)
{
	switch(curve)
	{
	case ParticleEmitterSettings::Curve_Linear:     return QStringLiteral("Linear");
	case ParticleEmitterSettings::Curve_EaseIn:     return QStringLiteral("Ease In");
	case ParticleEmitterSettings::Curve_EaseOut:    return QStringLiteral("Ease Out");
	case ParticleEmitterSettings::Curve_SmoothStep: return QStringLiteral("Smooth");
	case ParticleEmitterSettings::Curve_Custom:     return QStringLiteral("Custom");
	}
	return QStringLiteral("Linear");
}


Colour3f portalColour(float r, float g, float b)
{
	return Colour3f(r, g, b);
}


void setPortalMaterial(WorldMaterialRef& mat_ref, const char* name, const Colour3f& colour, const Colour3f& emission,
	const float roughness, const float metallic, const float opacity, const float luminance,
	const bool hologram, const bool double_sided, const char* colour_texture = NULL, const float texture_scale = 1.f)
{
	if(mat_ref.isNull())
		mat_ref = new WorldMaterial();

	WorldMaterial& mat = *mat_ref;
	mat = WorldMaterial();
	mat.name = name;
	mat.colour_rgb = colour;
	mat.emission_rgb = emission;
	mat.roughness = ScalarVal(roughness);
	mat.metallic_fraction = ScalarVal(metallic);
	mat.opacity = ScalarVal(opacity);
	mat.emission_lum_flux_or_lum = luminance;
	mat.tex_matrix = Matrix2f(texture_scale, 0.f, 0.f, texture_scale);
	if(colour_texture)
		mat.colour_texture_url = colour_texture;
	if(hologram)
		mat.flags |= WorldMaterial::HOLOGRAM_FLAG;
	if(double_sided)
		mat.flags |= WorldMaterial::DOUBLE_SIDED_FLAG;
}


void applyPortalStylePresetToMaterials(const QString& style, std::vector<WorldMaterialRef>& materials)
{
	WorldObject::ensurePortalMaterialsPresent(materials);

	const size_t inner = WorldObject::PORTAL_INNER_RIM_MATERIAL_INDEX;
	const size_t arch = WorldObject::PORTAL_ARCH_MATERIAL_INDEX;
	const size_t frame = WorldObject::PORTAL_FRAME_MATERIAL_INDEX;
	const size_t effect = WorldObject::PORTAL_EFFECT_MATERIAL_INDEX;
	const size_t threshold = WorldObject::PORTAL_THRESHOLD_MATERIAL_INDEX;

	if(style == QStringLiteral("arcane"))
	{
		setPortalMaterial(materials[inner],    "Arcane Inner Rim",    portalColour(0.58f, 0.28f, 1.00f), portalColour(0.95f, 0.42f, 1.00f), 0.18f, 0.85f, 1.00f, 3.0f, false, false);
		setPortalMaterial(materials[arch],     "Arcane Stone Arch",   portalColour(0.32f, 0.27f, 0.42f), portalColour(0.16f, 0.08f, 0.34f), 0.62f, 0.05f, 1.00f, 0.0f, false, false, "carrara1.jpg", 0.045f);
		setPortalMaterial(materials[frame],    "Arcane Outer Edge",   portalColour(0.14f, 0.08f, 0.25f), portalColour(0.70f, 0.28f, 1.00f), 0.22f, 0.50f, 1.00f, 1.2f, false, false);
		setPortalMaterial(materials[effect],   "Arcane Portal Effect",portalColour(0.75f, 0.30f, 1.00f), portalColour(1.00f, 0.36f, 1.00f), 0.10f, 0.00f, 0.82f, 5.0f, true, true);
		setPortalMaterial(materials[threshold],"Arcane Threshold",    portalColour(0.58f, 0.28f, 1.00f), portalColour(0.95f, 0.42f, 1.00f), 0.20f, 0.75f, 1.00f, 2.0f, false, false);
	}
	else if(style == QStringLiteral("stargate"))
	{
		setPortalMaterial(materials[inner],    "Stargate Inner Rim",    portalColour(0.42f, 0.72f, 1.00f), portalColour(0.20f, 0.88f, 1.00f), 0.14f, 0.90f, 1.00f, 4.0f, false, false);
		setPortalMaterial(materials[arch],     "Stargate Alloy Arch",   portalColour(0.62f, 0.68f, 0.70f), portalColour(0.08f, 0.18f, 0.22f), 0.26f, 0.80f, 1.00f, 0.1f, false, false);
		setPortalMaterial(materials[frame],    "Stargate Glyph Edge",   portalColour(0.12f, 0.22f, 0.26f), portalColour(0.22f, 0.90f, 1.00f), 0.18f, 0.70f, 1.00f, 2.0f, false, false);
		setPortalMaterial(materials[effect],   "Stargate Event Horizon",portalColour(0.20f, 0.76f, 1.00f), portalColour(0.25f, 0.90f, 1.00f), 0.05f, 0.00f, 0.88f, 6.0f, true, true);
		setPortalMaterial(materials[threshold],"Stargate Threshold",    portalColour(0.42f, 0.72f, 1.00f), portalColour(0.20f, 0.88f, 1.00f), 0.16f, 0.85f, 1.00f, 2.5f, false, false);
	}
	else if(style == QStringLiteral("void"))
	{
		setPortalMaterial(materials[inner],    "Void Inner Rim",    portalColour(0.05f, 0.03f, 0.08f), portalColour(0.40f, 0.10f, 0.92f), 0.08f, 0.65f, 1.00f, 2.5f, false, false);
		setPortalMaterial(materials[arch],     "Void Obsidian Arch",portalColour(0.015f, 0.015f, 0.020f), portalColour(0.10f, 0.02f, 0.22f), 0.12f, 0.50f, 1.00f, 0.3f, false, false);
		setPortalMaterial(materials[frame],    "Void Outer Edge",   portalColour(0.02f, 0.02f, 0.04f), portalColour(0.62f, 0.10f, 1.00f), 0.06f, 0.85f, 1.00f, 3.0f, false, false);
		setPortalMaterial(materials[effect],   "Void Singularity",  portalColour(0.08f, 0.02f, 0.18f), portalColour(0.80f, 0.08f, 1.00f), 0.03f, 0.00f, 0.72f, 8.0f, true, true);
		setPortalMaterial(materials[threshold],"Void Threshold",    portalColour(0.05f, 0.03f, 0.08f), portalColour(0.40f, 0.10f, 0.92f), 0.08f, 0.65f, 1.00f, 2.0f, false, false);
	}
	else if(style == QStringLiteral("lava"))
	{
		setPortalMaterial(materials[inner],    "Lava Inner Rim",  portalColour(1.00f, 0.42f, 0.08f), portalColour(1.00f, 0.28f, 0.02f), 0.32f, 0.25f, 1.00f, 5.0f, false, false);
		setPortalMaterial(materials[arch],     "Basalt Arch",     portalColour(0.09f, 0.07f, 0.06f), portalColour(0.35f, 0.08f, 0.00f), 0.78f, 0.05f, 1.00f, 0.8f, false, false);
		setPortalMaterial(materials[frame],    "Magma Edge",      portalColour(0.42f, 0.08f, 0.02f), portalColour(1.00f, 0.20f, 0.00f), 0.20f, 0.20f, 1.00f, 4.0f, false, false);
		setPortalMaterial(materials[effect],   "Lava Gate Effect",portalColour(1.00f, 0.24f, 0.02f), portalColour(1.00f, 0.18f, 0.00f), 0.08f, 0.00f, 0.88f, 7.0f, true, true);
		setPortalMaterial(materials[threshold],"Hot Threshold",   portalColour(1.00f, 0.42f, 0.08f), portalColour(1.00f, 0.28f, 0.02f), 0.26f, 0.30f, 1.00f, 4.0f, false, false);
	}
	else if(style == QStringLiteral("ice"))
	{
		setPortalMaterial(materials[inner],    "Ice Inner Rim",    portalColour(0.62f, 0.92f, 1.00f), portalColour(0.36f, 0.86f, 1.00f), 0.05f, 0.12f, 0.78f, 2.4f, true, true);
		setPortalMaterial(materials[arch],     "Frosted Arch",     portalColour(0.80f, 0.94f, 1.00f), portalColour(0.12f, 0.30f, 0.42f), 0.22f, 0.00f, 0.92f, 0.2f, false, false);
		setPortalMaterial(materials[frame],    "Ice Outer Edge",   portalColour(0.36f, 0.70f, 0.96f), portalColour(0.30f, 0.82f, 1.00f), 0.07f, 0.16f, 0.84f, 2.0f, true, true);
		setPortalMaterial(materials[effect],   "Cryo Portal Effect",portalColour(0.42f, 0.84f, 1.00f), portalColour(0.28f, 0.86f, 1.00f), 0.02f, 0.00f, 0.60f, 5.0f, true, true);
		setPortalMaterial(materials[threshold],"Ice Threshold",    portalColour(0.62f, 0.92f, 1.00f), portalColour(0.36f, 0.86f, 1.00f), 0.05f, 0.12f, 0.80f, 1.8f, true, true);
	}
	else if(style == QStringLiteral("forest"))
	{
		setPortalMaterial(materials[inner],    "Forest Inner Rim",  portalColour(0.22f, 0.76f, 0.32f), portalColour(0.18f, 0.72f, 0.24f), 0.42f, 0.05f, 1.00f, 1.6f, false, false);
		setPortalMaterial(materials[arch],     "Moss Stone Arch",   portalColour(0.28f, 0.36f, 0.25f), portalColour(0.04f, 0.18f, 0.05f), 0.84f, 0.00f, 1.00f, 0.0f, false, false, "carrara1.jpg", 0.060f);
		setPortalMaterial(materials[frame],    "Forest Outer Edge", portalColour(0.10f, 0.28f, 0.12f), portalColour(0.22f, 0.82f, 0.20f), 0.55f, 0.05f, 1.00f, 1.4f, false, false);
		setPortalMaterial(materials[effect],   "Forest Portal Effect",portalColour(0.16f, 0.82f, 0.30f), portalColour(0.20f, 0.90f, 0.22f), 0.12f, 0.00f, 0.76f, 3.0f, true, true);
		setPortalMaterial(materials[threshold],"Forest Threshold",  portalColour(0.22f, 0.76f, 0.32f), portalColour(0.18f, 0.72f, 0.24f), 0.42f, 0.05f, 1.00f, 1.2f, false, false);
	}
	else if(style == QStringLiteral("cyber"))
	{
		setPortalMaterial(materials[inner],    "Cyber Inner Rim",    portalColour(0.05f, 0.90f, 1.00f), portalColour(0.02f, 0.95f, 1.00f), 0.12f, 0.90f, 1.00f, 5.0f, false, false);
		setPortalMaterial(materials[arch],     "Cyber Black Arch",   portalColour(0.02f, 0.03f, 0.04f), portalColour(0.00f, 0.18f, 0.20f), 0.24f, 0.65f, 1.00f, 0.3f, false, false);
		setPortalMaterial(materials[frame],    "Cyber Magenta Edge", portalColour(0.85f, 0.05f, 1.00f), portalColour(0.95f, 0.05f, 1.00f), 0.10f, 0.80f, 1.00f, 4.0f, false, false);
		setPortalMaterial(materials[effect],   "Cyber Portal Effect",portalColour(0.04f, 0.95f, 1.00f), portalColour(0.04f, 0.95f, 1.00f), 0.04f, 0.00f, 0.84f, 7.0f, true, true);
		setPortalMaterial(materials[threshold],"Cyber Threshold",    portalColour(0.05f, 0.90f, 1.00f), portalColour(0.02f, 0.95f, 1.00f), 0.12f, 0.90f, 1.00f, 3.0f, false, false);
	}
	else if(style == QStringLiteral("solar"))
	{
		setPortalMaterial(materials[inner],    "Solar Inner Rim",    portalColour(1.00f, 0.84f, 0.24f), portalColour(1.00f, 0.72f, 0.10f), 0.18f, 0.80f, 1.00f, 5.0f, false, false);
		setPortalMaterial(materials[arch],     "Solar Gold Arch",    portalColour(0.86f, 0.62f, 0.18f), portalColour(0.65f, 0.32f, 0.02f), 0.22f, 0.95f, 1.00f, 0.6f, false, false);
		setPortalMaterial(materials[frame],    "Solar Corona Edge",  portalColour(1.00f, 0.48f, 0.08f), portalColour(1.00f, 0.48f, 0.04f), 0.10f, 0.65f, 1.00f, 5.0f, false, false);
		setPortalMaterial(materials[effect],   "Solar Portal Effect",portalColour(1.00f, 0.64f, 0.10f), portalColour(1.00f, 0.48f, 0.04f), 0.04f, 0.00f, 0.86f, 8.0f, true, true);
		setPortalMaterial(materials[threshold],"Solar Threshold",    portalColour(1.00f, 0.84f, 0.24f), portalColour(1.00f, 0.72f, 0.10f), 0.18f, 0.80f, 1.00f, 3.5f, false, false);
	}
	else if(style == QStringLiteral("ghost"))
	{
		setPortalMaterial(materials[inner],    "Ghost Inner Rim",    portalColour(0.76f, 0.92f, 1.00f), portalColour(0.56f, 0.82f, 1.00f), 0.04f, 0.00f, 0.42f, 2.2f, true, true);
		setPortalMaterial(materials[arch],     "Ghost Glass Arch",   portalColour(0.78f, 0.88f, 0.95f), portalColour(0.10f, 0.24f, 0.32f), 0.06f, 0.00f, 0.34f, 0.6f, true, true);
		setPortalMaterial(materials[frame],    "Ghost Outer Edge",   portalColour(0.62f, 0.86f, 1.00f), portalColour(0.50f, 0.82f, 1.00f), 0.04f, 0.00f, 0.48f, 2.0f, true, true);
		setPortalMaterial(materials[effect],   "Ghost Portal Effect",portalColour(0.62f, 0.90f, 1.00f), portalColour(0.46f, 0.86f, 1.00f), 0.02f, 0.00f, 0.48f, 4.0f, true, true);
		setPortalMaterial(materials[threshold],"Ghost Threshold",    portalColour(0.76f, 0.92f, 1.00f), portalColour(0.56f, 0.82f, 1.00f), 0.04f, 0.00f, 0.42f, 1.8f, true, true);
	}
	else
	{
		setPortalMaterial(materials[inner],    "Classic Inner Rim",   portalColour(0.85f, 0.81f, 0.55f), portalColour(0.85f, 0.81f, 0.55f), 0.30f, 1.00f, 1.00f, 1.2f, false, false);
		setPortalMaterial(materials[arch],     "Classic Marble Arch", portalColour(1.00f, 1.00f, 1.00f), portalColour(0.85f, 0.85f, 0.85f), 0.46f, 0.00f, 1.00f, 0.0f, false, false, "carrara1.jpg", 0.050f);
		setPortalMaterial(materials[frame],    "Classic Outer Edge",  portalColour(0.92f, 0.86f, 0.56f), portalColour(0.85f, 0.78f, 0.44f), 0.22f, 0.85f, 1.00f, 0.8f, false, false);
		setPortalMaterial(materials[effect],   "Classic Portal Effect",portalColour(0.80f, 1.00f, 0.92f), portalColour(0.65f, 1.00f, 0.86f), 0.10f, 0.00f, 0.78f, 3.0f, true, true);
		setPortalMaterial(materials[threshold],"Classic Threshold",   portalColour(0.85f, 0.81f, 0.55f), portalColour(0.85f, 0.81f, 0.55f), 0.30f, 1.00f, 1.00f, 1.0f, false, false);
	}
}


} // anonymous namespace


class ParticleCurveWidget : public QWidget
{
public:
	ParticleCurveWidget(QWidget* parent = NULL)
	:	QWidget(parent),
		curve(ParticleEmitterSettings::Curve_Linear),
		custom_mid(0.5f),
		dragging(false)
	{
		setMinimumHeight(54);
		setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
		setCursor(Qt::CrossCursor);
	}

	void setChangedCallback(const std::function<void()>& callback) { changed_callback = callback; }

	void setCurve(ParticleEmitterSettings::Curve new_curve, float new_custom_mid)
	{
		curve = new_curve;
		custom_mid = myClamp(new_custom_mid, 0.f, 1.f);
		update();
	}

	ParticleEmitterSettings::Curve currentCurve() const { return curve; }
	float currentCustomMid() const { return custom_mid; }

protected:
	virtual void paintEvent(QPaintEvent*) override
	{
		QPainter p(this);
		p.setRenderHint(QPainter::Antialiasing, true);

		const QRectF r = rect().adjusted(5, 5, -5, -5);
		p.fillRect(rect(), QColor(28, 28, 28));
		p.setPen(QPen(QColor(58, 58, 58), 1));
		for(int i=1; i<4; ++i)
		{
			const qreal x = r.left() + r.width() * i / 4.0;
			const qreal y = r.top() + r.height() * i / 4.0;
			p.drawLine(QPointF(x, r.top()), QPointF(x, r.bottom()));
			p.drawLine(QPointF(r.left(), y), QPointF(r.right(), y));
		}
		p.setPen(QPen(QColor(88, 88, 88), 1));
		p.drawRect(r);

		QPainterPath path;
		for(int i=0; i<=64; ++i)
		{
			const float t = (float)i / 64.f;
			const float value = previewCurveValue(curve, t, custom_mid);
			const QPointF pt(r.left() + r.width() * t, r.bottom() - r.height() * value);
			if(i == 0) path.moveTo(pt);
			else path.lineTo(pt);
		}
		p.setPen(QPen(QColor(62, 198, 255), 2.2));
		p.drawPath(path);

		const QPointF mid_pt(r.left() + r.width() * 0.5, r.bottom() - r.height() * previewCurveValue(curve, 0.5f, custom_mid));
		p.setBrush(QColor(255, 255, 255));
		p.setPen(QPen(QColor(8, 8, 8), 1));
		p.drawEllipse(mid_pt, 3.8, 3.8);

		p.setPen(QColor(196, 202, 208));
		p.drawText(r.adjusted(6, 2, -6, -2), Qt::AlignRight | Qt::AlignTop, curveLabel(curve));
	}

	virtual void mousePressEvent(QMouseEvent* e) override
	{
		if(e->button() == Qt::LeftButton)
		{
			dragging = true;
			setCustomFromY(e->pos().y());
		}
	}

	virtual void mouseMoveEvent(QMouseEvent* e) override
	{
		if(dragging)
			setCustomFromY(e->pos().y());
	}

	virtual void mouseReleaseEvent(QMouseEvent*) override
	{
		dragging = false;
	}

	virtual void mouseDoubleClickEvent(QMouseEvent* e) override
	{
		if(e->button() != Qt::LeftButton)
			return;

		if(curve == ParticleEmitterSettings::Curve_Linear)
			curve = ParticleEmitterSettings::Curve_EaseIn;
		else if(curve == ParticleEmitterSettings::Curve_EaseIn)
			curve = ParticleEmitterSettings::Curve_EaseOut;
		else if(curve == ParticleEmitterSettings::Curve_EaseOut)
			curve = ParticleEmitterSettings::Curve_SmoothStep;
		else
			curve = ParticleEmitterSettings::Curve_Linear;
		update();
		if(changed_callback)
			changed_callback();
	}

private:
	void setCustomFromY(int y)
	{
		const QRectF r = rect().adjusted(5, 5, -5, -5);
		curve = ParticleEmitterSettings::Curve_Custom;
		custom_mid = myClamp((float)((r.bottom() - (qreal)y) / myMax(r.height(), 1.0)), 0.f, 1.f);
		update();
		if(changed_callback)
			changed_callback();
	}

	ParticleEmitterSettings::Curve curve;
	float custom_mid;
	bool dragging;
	std::function<void()> changed_callback;
};


namespace
{


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


void configureDockComboBox(QComboBox* combo, int min_contents_length = 14, int popup_width = 260)
{
	if(!combo)
		return;

	combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	combo->setMinimumContentsLength(min_contents_length);
	combo->setMaxVisibleItems(18);
	combo->setMinimumWidth(130);
	combo->setMaximumWidth(popup_width);
	combo->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
	if(combo->view())
	{
		combo->view()->setTextElideMode(Qt::ElideRight);
		combo->view()->setMinimumWidth(popup_width);
		combo->view()->setMaximumWidth(popup_width);
	}
}


QString particleKindData(const ParticleEmitterSettings::ParticleKind kind)
{
	switch(kind)
	{
	case ParticleEmitterSettings::ParticleKind_Foam:      return QStringLiteral("foam");
	case ParticleEmitterSettings::ParticleKind_Spark:     return QStringLiteral("spark");
	case ParticleEmitterSettings::ParticleKind_Streak:    return QStringLiteral("streak");
	case ParticleEmitterSettings::ParticleKind_Star:      return QStringLiteral("star");
	case ParticleEmitterSettings::ParticleKind_Ring:      return QStringLiteral("ring");
	case ParticleEmitterSettings::ParticleKind_Nebula:    return QStringLiteral("nebula");
	case ParticleEmitterSettings::ParticleKind_Flame:     return QStringLiteral("flame");
	case ParticleEmitterSettings::ParticleKind_Snowflake: return QStringLiteral("snowflake");
	case ParticleEmitterSettings::ParticleKind_SoftDisc:  return QStringLiteral("soft_disc");
	case ParticleEmitterSettings::ParticleKind_Smoke:
	default:                                              return QStringLiteral("smoke");
	}
}


ParticleEmitterSettings::ParticleKind particleKindFromData(const QString& data)
{
	if(data == QStringLiteral("foam"))      return ParticleEmitterSettings::ParticleKind_Foam;
	if(data == QStringLiteral("spark"))     return ParticleEmitterSettings::ParticleKind_Spark;
	if(data == QStringLiteral("streak"))    return ParticleEmitterSettings::ParticleKind_Streak;
	if(data == QStringLiteral("star"))      return ParticleEmitterSettings::ParticleKind_Star;
	if(data == QStringLiteral("ring"))      return ParticleEmitterSettings::ParticleKind_Ring;
	if(data == QStringLiteral("nebula"))    return ParticleEmitterSettings::ParticleKind_Nebula;
	if(data == QStringLiteral("flame"))     return ParticleEmitterSettings::ParticleKind_Flame;
	if(data == QStringLiteral("snowflake")) return ParticleEmitterSettings::ParticleKind_Snowflake;
	if(data == QStringLiteral("soft_disc")) return ParticleEmitterSettings::ParticleKind_SoftDisc;
	return ParticleEmitterSettings::ParticleKind_Smoke;
}


QString particleShapeData(const ParticleEmitterSettings::Shape shape)
{
	switch(shape)
	{
	case ParticleEmitterSettings::Shape_Point:      return QStringLiteral("point");
	case ParticleEmitterSettings::Shape_Sphere:     return QStringLiteral("sphere");
	case ParticleEmitterSettings::Shape_Box:        return QStringLiteral("box");
	case ParticleEmitterSettings::Shape_Ring:       return QStringLiteral("ring");
	case ParticleEmitterSettings::Shape_Cylinder:   return QStringLiteral("cylinder");
	case ParticleEmitterSettings::Shape_Cone:       return QStringLiteral("cone");
	case ParticleEmitterSettings::Shape_Line:       return QStringLiteral("line");
	case ParticleEmitterSettings::Shape_Hemisphere: return QStringLiteral("hemisphere");
	case ParticleEmitterSettings::Shape_Disc:
	default:                                        return QStringLiteral("disc");
	}
}


ParticleEmitterSettings::Shape particleShapeFromData(const QString& data)
{
	if(data == QStringLiteral("point"))      return ParticleEmitterSettings::Shape_Point;
	if(data == QStringLiteral("sphere"))     return ParticleEmitterSettings::Shape_Sphere;
	if(data == QStringLiteral("box"))        return ParticleEmitterSettings::Shape_Box;
	if(data == QStringLiteral("ring"))       return ParticleEmitterSettings::Shape_Ring;
	if(data == QStringLiteral("cylinder"))   return ParticleEmitterSettings::Shape_Cylinder;
	if(data == QStringLiteral("cone"))       return ParticleEmitterSettings::Shape_Cone;
	if(data == QStringLiteral("line"))       return ParticleEmitterSettings::Shape_Line;
	if(data == QStringLiteral("hemisphere")) return ParticleEmitterSettings::Shape_Hemisphere;
	return ParticleEmitterSettings::Shape_Disc;
}

} // anonymous namespace

static int remapAudioPlayerMaterialIndexForEditor(int selected_index, size_t material_count);


ObjectEditor::ObjectEditor(QWidget *parent)
:	QWidget(parent),
	editing_object_type(WorldObject::ObjectType_Generic),
	selected_mat_index(0),
	edit_timer(new QTimer(this)),
	shader_editor(NULL),
	settings(NULL),
	selected_font_name("Default"),
	controls_editable(true),
	text_font_feature_supported(true),
	syncing_audio_playlist_widget(false),
	editing_audio_player_webview(false),
	editing_particle_emitter(false),
	editing_gaussian_splat(false),
	audioShuffleCheckBox(NULL),
	audioActivationDistanceLabel(NULL),
	audioActivationDistanceSpinBox(NULL),
	audioSoundRadiusLabel(NULL),
	audioSoundRadiusSpinBox(NULL),
	audioDirectionalityEnabledCheckBox(NULL),
	audioDirectivityAlphaLabel(NULL),
	audioDirectivityAlphaSpinBox(NULL),
	audioDirectivityOrderLabel(NULL),
	audioDirectivityOrderSpinBox(NULL),
	audioSpreadDegreesLabel(NULL),
	audioSpreadDegreesSpinBox(NULL),
	audioScheduleEnabledCheckBox(NULL),
	audioScheduleStartHourLabel(NULL),
	audioScheduleStartHourSpinBox(NULL),
	audioScheduleEndHourLabel(NULL),
	audioScheduleEndHourSpinBox(NULL),
	audioPlaylistGroupBox(NULL),
	audioPlaylistListWidget(NULL),
	audioAddTracksPushButton(NULL),
	audioAddURLPushButton(NULL),
	audioRemoveTrackPushButton(NULL),
	audioMoveTrackUpPushButton(NULL),
	audioMoveTrackDownPushButton(NULL),
	portalGroupBox(NULL),
	portalStyleLabel(NULL),
	portalStyleComboBox(NULL),
	portalApplyStylePushButton(NULL),
	portalResetMaterialsPushButton(NULL),
	portalMainWorldPushButton(NULL),
	portalMapWorldPushButton(NULL),
	portalClearTargetPushButton(NULL),
	portalHelpPushButton(NULL),
	portalTipLabel(NULL),
	gaussianSplatGroupBox(NULL),
	gaussianSplatPresetLabel(NULL),
	gaussianSplatPresetComboBox(NULL),
	gaussianSplatResetPushButton(NULL),
	gaussianSplatSHDetailLabel(NULL),
	gaussianSplatSHDetailComboBox(NULL),
	gaussianSplatOpacityLabel(NULL),
	gaussianSplatOpacitySpinBox(NULL),
	gaussianSplatMinimumSourceOpacityLabel(NULL),
	gaussianSplatMinimumSourceOpacitySpinBox(NULL),
	gaussianSplatBrightnessLabel(NULL),
	gaussianSplatBrightnessSpinBox(NULL),
	gaussianSplatRadiusLabel(NULL),
	gaussianSplatRadiusSpinBox(NULL),
	gaussianSplatSaturationLabel(NULL),
	gaussianSplatSaturationSpinBox(NULL),
	gaussianSplatContrastLabel(NULL),
	gaussianSplatContrastSpinBox(NULL),
	gaussianSplatAlphaCutoffLabel(NULL),
	gaussianSplatAlphaCutoffSpinBox(NULL),
	gaussianSplatTipLabel(NULL),
	particleGroupBox(NULL),
	particleHelpPushButton(NULL),
	particlePresetLabel(NULL),
	particlePresetComboBox(NULL),
	particleSavePresetPushButton(NULL),
	particleDeletePresetPushButton(NULL),
	particleEnabledCheckBox(NULL),
	particleKindLabel(NULL),
	particleKindComboBox(NULL),
	particleDirectionLabel(NULL),
	particleDirectionComboBox(NULL),
	particleShapeLabel(NULL),
	particleShapeComboBox(NULL),
	particleRenderModeLabel(NULL),
	particleRenderModeComboBox(NULL),
	particlePreviewLabel(NULL),
	particleSpriteLibraryLabel(NULL),
	particleSpriteLibraryComboBox(NULL),
	particleSpritePathLabel(NULL),
	particleSpritePathLineEdit(NULL),
	particleSpriteBrowsePushButton(NULL),
	particleSpriteClearPushButton(NULL),
	particleAudioEnabledCheckBox(NULL),
	particleAudioURLLabel(NULL),
	particleAudioURLLineEdit(NULL),
	particleAudioBrowsePushButton(NULL),
	particleAudioClearPushButton(NULL),
	particleAudioLoopCheckBox(NULL),
	particleAudioSpatialCheckBox(NULL),
	particleAudioVolumeLabel(NULL),
	particleAudioVolumeSpinBox(NULL),
	particleAudioActivationDistanceLabel(NULL),
	particleAudioActivationDistanceSpinBox(NULL),
	particleAudioMinDistanceLabel(NULL),
	particleAudioMinDistanceSpinBox(NULL),
	particleAudioMaxDistanceLabel(NULL),
	particleAudioMaxDistanceSpinBox(NULL),
	particleAudioFadeInLabel(NULL),
	particleAudioFadeInSpinBox(NULL),
	particleAudioFadeOutLabel(NULL),
	particleAudioFadeOutSpinBox(NULL),
	particleRateLabel(NULL),
	particleRateSpinBox(NULL),
	particleFrameCapLabel(NULL),
	particleFrameCapSpinBox(NULL),
	particleMaxParticlesLabel(NULL),
	particleMaxParticlesSpinBox(NULL),
	particleRadiusLabel(NULL),
	particleRadiusSpinBox(NULL),
	particleSpeedLabel(NULL),
	particleSpeedSpinBox(NULL),
	particleSpeedJitterLabel(NULL),
	particleSpeedJitterSpinBox(NULL),
	particleSpreadLabel(NULL),
	particleSpreadSpinBox(NULL),
	particleTurbulenceLabel(NULL),
	particleTurbulenceSpinBox(NULL),
	particleLifetimeLabel(NULL),
	particleLifetimeSpinBox(NULL),
	particleStartWidthLabel(NULL),
	particleStartWidthSpinBox(NULL),
	particleEndWidthLabel(NULL),
	particleEndWidthSpinBox(NULL),
	particleSizeCurveLabel(NULL),
	particleSizeCurveWidget(NULL),
	particleSizeJitterLabel(NULL),
	particleSizeJitterSpinBox(NULL),
	particleOpacityLabel(NULL),
	particleOpacitySpinBox(NULL),
	particleEndOpacityLabel(NULL),
	particleEndOpacitySpinBox(NULL),
	particleOpacityCurveLabel(NULL),
	particleOpacityCurveWidget(NULL),
	particleOpacityJitterLabel(NULL),
	particleOpacityJitterSpinBox(NULL),
	particleColourLabel(NULL),
	particleColourPushButton(NULL),
	particleEndColourLabel(NULL),
	particleEndColourPushButton(NULL),
	particleColourJitterLabel(NULL),
	particleColourJitterSpinBox(NULL),
	particleTrailLengthLabel(NULL),
	particleTrailLengthSpinBox(NULL),
	particleGlowStrengthLabel(NULL),
	particleGlowStrengthSpinBox(NULL),
	particleRotationLabel(NULL),
	particleRotationSpinBox(NULL),
	particleRotationJitterLabel(NULL),
	particleRotationJitterSpinBox(NULL),
	particleSpinLabel(NULL),
	particleSpinSpinBox(NULL),
	particleSpinJitterLabel(NULL),
	particleSpinJitterSpinBox(NULL),
	particleBurstEnabledCheckBox(NULL),
	particleBurstCountLabel(NULL),
	particleBurstCountSpinBox(NULL),
	particleBurstIntervalLabel(NULL),
	particleBurstIntervalSpinBox(NULL),
	particleMaxDistanceLabel(NULL),
	particleMaxDistanceSpinBox(NULL),
	particleBurstNowPushButton(NULL),
	particleClearParticlesPushButton(NULL),
	particleDiagnosticsLabel(NULL),
	particleDiagnosticsValueLabel(NULL),
	particleWindXLabel(NULL),
	particleWindXSpinBox(NULL),
	particleWindYLabel(NULL),
	particleWindYSpinBox(NULL),
	particleWindZLabel(NULL),
	particleWindZSpinBox(NULL),
	particleVortexStrengthLabel(NULL),
	particleVortexStrengthSpinBox(NULL),
	particleAttractorStrengthLabel(NULL),
	particleAttractorStrengthSpinBox(NULL),
	particleAttractorRadiusLabel(NULL),
	particleAttractorRadiusSpinBox(NULL),
	particleBlackHoleCheckBox(NULL),
	particleEventHorizonLabel(NULL),
	particleEventHorizonSpinBox(NULL),
	particleRadialAccelLabel(NULL),
	particleRadialAccelSpinBox(NULL),
	particleLinearDampingLabel(NULL),
	particleLinearDampingSpinBox(NULL),
	particleBuoyancyLiftLabel(NULL),
	particleBuoyancyLiftSpinBox(NULL),
	particleGravityScaleLabel(NULL),
	particleGravityScaleSpinBox(NULL),
	particleDragAreaLabel(NULL),
	particleDragAreaSpinBox(NULL),
	particleMassLabel(NULL),
	particleMassSpinBox(NULL),
	particleRestitutionLabel(NULL),
	particleRestitutionSpinBox(NULL),
	particleCollisionFrictionLabel(NULL),
	particleCollisionFrictionSpinBox(NULL),
	particleCollideSurfacesCheckBox(NULL),
	particleDieOnSurfaceCheckBox(NULL),
	spotlight_col(0.85f),
	particle_col(0.82f),
	particle_end_col(0.42f)
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

	this->audioSoundRadiusLabel = new QLabel(QCoreApplication::translate("ObjectEditor", "Sound Radius"), this->audioGroupBox);
	this->gridLayout_3->addWidget(this->audioSoundRadiusLabel, 2, 0);

	this->audioSoundRadiusSpinBox = new RealControl(this->audioGroupBox);
	this->audioSoundRadiusSpinBox->setMinimum(WorldObject::MIN_AUDIO_PLAYER_SOUND_RADIUS);
	this->audioSoundRadiusSpinBox->setMaximum(WorldObject::MAX_AUDIO_PLAYER_SOUND_RADIUS);
	this->audioSoundRadiusSpinBox->setSingleStep(0.5);
	this->audioSoundRadiusSpinBox->setSliderMinimum(WorldObject::MIN_AUDIO_PLAYER_SOUND_RADIUS);
	this->audioSoundRadiusSpinBox->setSliderMaximum(120.0);
	this->audioSoundRadiusSpinBox->setSliderSteps(240);
	this->audioSoundRadiusSpinBox->setSuffix(QCoreApplication::translate("ObjectEditor", " m"));
	this->gridLayout_3->addWidget(this->audioSoundRadiusSpinBox, 2, 1, 1, 2);

	this->audioDirectionalityEnabledCheckBox = new QCheckBox(QCoreApplication::translate("ObjectEditor", "3D Directionality"), this->audioGroupBox);
	this->gridLayout_3->addWidget(this->audioDirectionalityEnabledCheckBox, 3, 0, 1, 3);

	this->audioDirectivityAlphaLabel = new QLabel(QCoreApplication::translate("ObjectEditor", "Directionality Focus"), this->audioGroupBox);
	this->gridLayout_3->addWidget(this->audioDirectivityAlphaLabel, 4, 0);

	this->audioDirectivityAlphaSpinBox = new RealControl(this->audioGroupBox);
	this->audioDirectivityAlphaSpinBox->setMinimum(WorldObject::MIN_AUDIO_PLAYER_DIRECTIVITY_ALPHA);
	this->audioDirectivityAlphaSpinBox->setMaximum(WorldObject::MAX_AUDIO_PLAYER_DIRECTIVITY_ALPHA);
	this->audioDirectivityAlphaSpinBox->setSingleStep(0.05);
	this->audioDirectivityAlphaSpinBox->setSliderMinimum(WorldObject::MIN_AUDIO_PLAYER_DIRECTIVITY_ALPHA);
	this->audioDirectivityAlphaSpinBox->setSliderMaximum(WorldObject::MAX_AUDIO_PLAYER_DIRECTIVITY_ALPHA);
	this->audioDirectivityAlphaSpinBox->setSliderSteps(200);
	this->gridLayout_3->addWidget(this->audioDirectivityAlphaSpinBox, 4, 1, 1, 2);

	this->audioDirectivityOrderLabel = new QLabel(QCoreApplication::translate("ObjectEditor", "Directionality Sharpness"), this->audioGroupBox);
	this->gridLayout_3->addWidget(this->audioDirectivityOrderLabel, 5, 0);

	this->audioDirectivityOrderSpinBox = new RealControl(this->audioGroupBox);
	this->audioDirectivityOrderSpinBox->setMinimum(WorldObject::MIN_AUDIO_PLAYER_DIRECTIVITY_ORDER);
	this->audioDirectivityOrderSpinBox->setMaximum(WorldObject::MAX_AUDIO_PLAYER_DIRECTIVITY_ORDER);
	this->audioDirectivityOrderSpinBox->setSingleStep(0.25);
	this->audioDirectivityOrderSpinBox->setSliderMinimum(WorldObject::MIN_AUDIO_PLAYER_DIRECTIVITY_ORDER);
	this->audioDirectivityOrderSpinBox->setSliderMaximum(8.0);
	this->audioDirectivityOrderSpinBox->setSliderSteps(280);
	this->gridLayout_3->addWidget(this->audioDirectivityOrderSpinBox, 5, 1, 1, 2);

	this->audioSpreadDegreesLabel = new QLabel(QCoreApplication::translate("ObjectEditor", "Directional Spread"), this->audioGroupBox);
	this->gridLayout_3->addWidget(this->audioSpreadDegreesLabel, 6, 0);

	this->audioSpreadDegreesSpinBox = new RealControl(this->audioGroupBox);
	this->audioSpreadDegreesSpinBox->setMinimum(WorldObject::MIN_AUDIO_PLAYER_SPREAD_DEGREES);
	this->audioSpreadDegreesSpinBox->setMaximum(WorldObject::MAX_AUDIO_PLAYER_SPREAD_DEGREES);
	this->audioSpreadDegreesSpinBox->setSingleStep(1.0);
	this->audioSpreadDegreesSpinBox->setSliderMinimum(WorldObject::MIN_AUDIO_PLAYER_SPREAD_DEGREES);
	this->audioSpreadDegreesSpinBox->setSliderMaximum(WorldObject::MAX_AUDIO_PLAYER_SPREAD_DEGREES);
	this->audioSpreadDegreesSpinBox->setSliderSteps(360);
	this->audioSpreadDegreesSpinBox->setSuffix(QCoreApplication::translate("ObjectEditor", " deg"));
	this->gridLayout_3->addWidget(this->audioSpreadDegreesSpinBox, 6, 1, 1, 2);

	this->audioScheduleEnabledCheckBox = new QCheckBox(QCoreApplication::translate("ObjectEditor", "Use Daily Schedule"), this->audioGroupBox);
	this->gridLayout_3->addWidget(this->audioScheduleEnabledCheckBox, 7, 0, 1, 3);

	this->audioScheduleStartHourLabel = new QLabel(QCoreApplication::translate("ObjectEditor", "Schedule Start"), this->audioGroupBox);
	this->gridLayout_3->addWidget(this->audioScheduleStartHourLabel, 8, 0);

	this->audioScheduleStartHourSpinBox = new RealControl(this->audioGroupBox);
	this->audioScheduleStartHourSpinBox->setMinimum(WorldObject::MIN_AUDIO_PLAYER_SCHEDULE_HOUR);
	this->audioScheduleStartHourSpinBox->setMaximum(WorldObject::MAX_AUDIO_PLAYER_SCHEDULE_HOUR);
	this->audioScheduleStartHourSpinBox->setSingleStep(0.25);
	this->audioScheduleStartHourSpinBox->setSliderMinimum(WorldObject::MIN_AUDIO_PLAYER_SCHEDULE_HOUR);
	this->audioScheduleStartHourSpinBox->setSliderMaximum(WorldObject::MAX_AUDIO_PLAYER_SCHEDULE_HOUR);
	this->audioScheduleStartHourSpinBox->setSliderSteps(96);
	this->audioScheduleStartHourSpinBox->setSuffix(QCoreApplication::translate("ObjectEditor", " h"));
	this->gridLayout_3->addWidget(this->audioScheduleStartHourSpinBox, 8, 1, 1, 2);

	this->audioScheduleEndHourLabel = new QLabel(QCoreApplication::translate("ObjectEditor", "Schedule End"), this->audioGroupBox);
	this->gridLayout_3->addWidget(this->audioScheduleEndHourLabel, 9, 0);

	this->audioScheduleEndHourSpinBox = new RealControl(this->audioGroupBox);
	this->audioScheduleEndHourSpinBox->setMinimum(WorldObject::MIN_AUDIO_PLAYER_SCHEDULE_HOUR);
	this->audioScheduleEndHourSpinBox->setMaximum(WorldObject::MAX_AUDIO_PLAYER_SCHEDULE_HOUR);
	this->audioScheduleEndHourSpinBox->setSingleStep(0.25);
	this->audioScheduleEndHourSpinBox->setSliderMinimum(WorldObject::MIN_AUDIO_PLAYER_SCHEDULE_HOUR);
	this->audioScheduleEndHourSpinBox->setSliderMaximum(WorldObject::MAX_AUDIO_PLAYER_SCHEDULE_HOUR);
	this->audioScheduleEndHourSpinBox->setSliderSteps(96);
	this->audioScheduleEndHourSpinBox->setSuffix(QCoreApplication::translate("ObjectEditor", " h"));
	this->gridLayout_3->addWidget(this->audioScheduleEndHourSpinBox, 9, 1, 1, 2);

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
	connect(this->audioSoundRadiusSpinBox, SIGNAL(valueChanged(double)),		this, SIGNAL(objectChanged()));
	connect(this->audioDirectionalityEnabledCheckBox, SIGNAL(toggled(bool)),	this, SIGNAL(objectChanged()));
	connect(this->audioDirectionalityEnabledCheckBox, SIGNAL(toggled(bool)),	this, SLOT(audioDirectionalityToggled(bool)));
	connect(this->audioDirectivityAlphaSpinBox, SIGNAL(valueChanged(double)),	this, SIGNAL(objectChanged()));
	connect(this->audioDirectivityOrderSpinBox, SIGNAL(valueChanged(double)),	this, SIGNAL(objectChanged()));
	connect(this->audioSpreadDegreesSpinBox, SIGNAL(valueChanged(double)),		this, SIGNAL(objectChanged()));
	connect(this->audioScheduleEnabledCheckBox, SIGNAL(toggled(bool)),			this, SIGNAL(objectChanged()));
	connect(this->audioScheduleEnabledCheckBox, SIGNAL(toggled(bool)),			this, SLOT(audioScheduleToggled(bool)));
	connect(this->audioScheduleStartHourSpinBox, SIGNAL(valueChanged(double)),	this, SIGNAL(objectChanged()));
	connect(this->audioScheduleEndHourSpinBox, SIGNAL(valueChanged(double)),	this, SIGNAL(objectChanged()));
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

	retranslateDynamicAudioPlayerUI();
	updateAudioPlaylistButtonsEnabled();

	createPortalEditorUI();
	retranslateDynamicPortalUI();

	createGaussianSplatEditorUI();
	retranslateDynamicGaussianSplatUI();

	createParticleEditorUI();
	retranslateDynamicParticleUI();
}


void ObjectEditor::init() // settings should be set before this.
{
	show3DControlsCheckBox->setChecked(settings->value("objectEditor/show3DControlsCheckBoxChecked", /*default val=*/true).toBool());
	SignalBlocker::setChecked(linkScaleCheckBox, settings->value("objectEditor/linkScaleCheckBoxChecked", /*default val=*/true).toBool());

	SignalBlocker::setValue(gridSpacingDoubleSpinBox, settings->value("objectEditor/gridSpacing", /*default val=*/1.0).toDouble());
	SignalBlocker::setChecked(snapToGridCheckBox, settings->value("objectEditor/snapToGridCheckBoxChecked", /*default val=*/false).toBool());

	// Initialize font list
	loadAvailableFonts();
	loadCustomParticlePresets();
	retranslateDynamicPortalUI();
	retranslateDynamicParticleUI();
}


void ObjectEditor::changeEvent(QEvent* event)
{
	if(event->type() == QEvent::LanguageChange)
	{
		this->retranslateUi(this);
		retranslateDynamicAudioPlayerUI();
		retranslateDynamicPortalUI();
		retranslateDynamicGaussianSplatUI();
		retranslateDynamicParticleUI();
	}

	QWidget::changeEvent(event);
}


void ObjectEditor::retranslateDynamicAudioPlayerUI()
{
	if(!this->audioGroupBox)
		return;

	const RuntimeTranslation::UILanguage ui_language = currentUILanguageForObjectEditor(this->settings);
	auto tr_audio = [ui_language](const char* source_text)
	{
		return translateObjectEditorRuntimeText(ui_language, source_text);
	};

	const bool is_audio_player = this->editing_audio_player_webview;
	const bool is_portal = (this->editing_object_type == WorldObject::ObjectType_Portal);
	this->audioGroupBox->setTitle(is_audio_player ? tr_audio("Audio Player") : tr_audio("Audio"));

	this->label_8->setText(tr_audio("Volume"));
	this->label_14->setText(tr_audio("Audio File"));
	this->audioAutoplayCheckBox->setText(tr_audio("Autoplay"));
	this->audioLoopCheckBox->setText(tr_audio("Loop"));
	this->audioShuffleCheckBox->setText(tr_audio("Shuffle"));
	this->audioActivationDistanceLabel->setText(tr_audio("Activation Distance"));
	this->audioSoundRadiusLabel->setText(tr_audio("Sound Radius"));
	this->audioDirectionalityEnabledCheckBox->setText(tr_audio("3D Directionality"));
	this->audioDirectivityAlphaLabel->setText(tr_audio("Directionality Focus"));
	this->audioDirectivityOrderLabel->setText(tr_audio("Directionality Sharpness"));
	this->audioSpreadDegreesLabel->setText(tr_audio("Directional Spread"));
	this->audioScheduleEnabledCheckBox->setText(tr_audio("Use Daily Schedule"));
	this->audioScheduleStartHourLabel->setText(tr_audio("Schedule Start"));
	this->audioScheduleEndHourLabel->setText(tr_audio("Schedule End"));
	this->audioPlaylistGroupBox->setTitle(tr_audio("Playlist"));
	this->audioAddTracksPushButton->setText(tr_audio("Add Tracks"));
	this->audioAddURLPushButton->setText(tr_audio("Add URL"));
	this->audioRemoveTrackPushButton->setText(tr_audio("Remove"));
	this->audioMoveTrackUpPushButton->setText(tr_audio("Up"));
	this->audioMoveTrackDownPushButton->setText(tr_audio("Down"));

	this->audioActivationDistanceSpinBox->setSuffix(tr_audio(" m"));
	this->audioSoundRadiusSpinBox->setSuffix(tr_audio(" m"));
	this->audioSpreadDegreesSpinBox->setSuffix(tr_audio(" deg"));
	this->audioScheduleStartHourSpinBox->setSuffix(tr_audio(" h"));
	this->audioScheduleEndHourSpinBox->setSuffix(tr_audio(" h"));
	this->targetURLLabel->setText(tr_audio(is_portal ? "Portal Target URL" : "Target URL"));
	this->targetURLLineEdit->setToolTip(is_portal ? tr_audio("Walk through the portal to travel to this sub:// destination.") : QString());

	const QString volume_tip = tr_audio("Playback volume for this audio player.");
	const QString autoplay_tip = tr_audio("Enable automatic playback when this object loads.");
	const QString loop_tip = tr_audio("Loop the playlist when it reaches the end.");
	const QString shuffle_tip = tr_audio("Shuffle playlist order.");
	const QString activation_tip = tr_audio("Distance from camera where the player becomes active.");
	const QString sound_radius_tip = tr_audio("Maximum audible distance for this player's sound.");
	const QString directionality_tip = tr_audio("Enable directional 3D sound cone.");
	const QString focus_tip = tr_audio("Forward focus of directional sound pattern.");
	const QString sharpness_tip = tr_audio("Sharpness of directional sound attenuation.");
	const QString spread_tip = tr_audio("Angular spread of directional sound in degrees.");
	const QString schedule_toggle_tip = tr_audio("Restrict playback to a daily time window.");
	const QString schedule_start_tip = tr_audio("Playback start time in local hours.");
	const QString schedule_end_tip = tr_audio("Playback end time in local hours.");
	const QString playlist_tip = tr_audio("Tracks and stream URLs played by this audio player.");

	this->label_8->setToolTip(volume_tip);
	this->volumeDoubleSpinBox->setToolTip(volume_tip);
	this->audioAutoplayCheckBox->setToolTip(autoplay_tip);
	this->audioLoopCheckBox->setToolTip(loop_tip);
	this->audioShuffleCheckBox->setToolTip(shuffle_tip);
	this->audioActivationDistanceLabel->setToolTip(activation_tip);
	this->audioActivationDistanceSpinBox->setToolTip(activation_tip);
	this->audioSoundRadiusLabel->setToolTip(sound_radius_tip);
	this->audioSoundRadiusSpinBox->setToolTip(sound_radius_tip);
	this->audioDirectionalityEnabledCheckBox->setToolTip(directionality_tip);
	this->audioDirectivityAlphaLabel->setToolTip(focus_tip);
	this->audioDirectivityAlphaSpinBox->setToolTip(focus_tip);
	this->audioDirectivityOrderLabel->setToolTip(sharpness_tip);
	this->audioDirectivityOrderSpinBox->setToolTip(sharpness_tip);
	this->audioSpreadDegreesLabel->setToolTip(spread_tip);
	this->audioSpreadDegreesSpinBox->setToolTip(spread_tip);
	this->audioScheduleEnabledCheckBox->setToolTip(schedule_toggle_tip);
	this->audioScheduleStartHourLabel->setToolTip(schedule_start_tip);
	this->audioScheduleStartHourSpinBox->setToolTip(schedule_start_tip);
	this->audioScheduleEndHourLabel->setToolTip(schedule_end_tip);
	this->audioScheduleEndHourSpinBox->setToolTip(schedule_end_tip);
	this->audioPlaylistGroupBox->setToolTip(playlist_tip);
	this->audioPlaylistListWidget->setToolTip(playlist_tip);
}


void ObjectEditor::createPortalEditorUI()
{
	this->portalGroupBox = new QGroupBox(this);
	QVBoxLayout* portal_layout = new QVBoxLayout(this->portalGroupBox);
	portal_layout->setContentsMargins(8, 8, 8, 8);
	portal_layout->setSpacing(6);

	QGridLayout* portal_grid = new QGridLayout();
	portal_grid->setContentsMargins(0, 0, 0, 0);
	portal_grid->setHorizontalSpacing(6);
	portal_grid->setVerticalSpacing(6);

	this->portalStyleLabel = new QLabel(this->portalGroupBox);
	portal_grid->addWidget(this->portalStyleLabel, 0, 0);

	this->portalStyleComboBox = new QComboBox(this->portalGroupBox);
	this->portalStyleComboBox->addItem(QStringLiteral("Classic Marble"), QStringLiteral("classic"));
	this->portalStyleComboBox->addItem(QStringLiteral("Arcane Rift"), QStringLiteral("arcane"));
	this->portalStyleComboBox->addItem(QStringLiteral("Stargate"), QStringLiteral("stargate"));
	this->portalStyleComboBox->addItem(QStringLiteral("Void Singularity"), QStringLiteral("void"));
	this->portalStyleComboBox->addItem(QStringLiteral("Lava Gate"), QStringLiteral("lava"));
	this->portalStyleComboBox->addItem(QStringLiteral("Ice Gate"), QStringLiteral("ice"));
	this->portalStyleComboBox->addItem(QStringLiteral("Forest Gate"), QStringLiteral("forest"));
	this->portalStyleComboBox->addItem(QStringLiteral("Cyber Gate"), QStringLiteral("cyber"));
	this->portalStyleComboBox->addItem(QStringLiteral("Solar Gate"), QStringLiteral("solar"));
	this->portalStyleComboBox->addItem(QStringLiteral("Ghost Gate"), QStringLiteral("ghost"));
	configureDockComboBox(this->portalStyleComboBox, 14, 260);
	portal_grid->addWidget(this->portalStyleComboBox, 0, 1, 1, 3);

	this->portalApplyStylePushButton = new QPushButton(this->portalGroupBox);
	this->portalResetMaterialsPushButton = new QPushButton(this->portalGroupBox);
	this->portalHelpPushButton = new QPushButton(QStringLiteral("?"), this->portalGroupBox);
	this->portalHelpPushButton->setMaximumWidth(32);
	portal_grid->addWidget(this->portalApplyStylePushButton, 1, 1);
	portal_grid->addWidget(this->portalResetMaterialsPushButton, 1, 2);
	portal_grid->addWidget(this->portalHelpPushButton, 1, 3);

	this->portalMainWorldPushButton = new QPushButton(this->portalGroupBox);
	this->portalMapWorldPushButton = new QPushButton(this->portalGroupBox);
	this->portalClearTargetPushButton = new QPushButton(this->portalGroupBox);
	portal_grid->addWidget(this->portalMainWorldPushButton, 2, 1);
	portal_grid->addWidget(this->portalMapWorldPushButton, 2, 2);
	portal_grid->addWidget(this->portalClearTargetPushButton, 2, 3);

	portal_layout->addLayout(portal_grid);

	this->portalTipLabel = new QLabel(this->portalGroupBox);
	this->portalTipLabel->setWordWrap(true);
	this->portalTipLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
	portal_layout->addWidget(this->portalTipLabel);

	this->verticalLayout->addWidget(this->portalGroupBox);
	this->portalGroupBox->hide();

	connect(this->portalApplyStylePushButton, SIGNAL(clicked(bool)), this, SLOT(applyPortalStylePreset()));
	connect(this->portalResetMaterialsPushButton, SIGNAL(clicked(bool)), this, SLOT(resetPortalMaterials()));
	connect(this->portalMainWorldPushButton, SIGNAL(clicked(bool)), this, SLOT(setPortalTargetMainWorld()));
	connect(this->portalMapWorldPushButton, SIGNAL(clicked(bool)), this, SLOT(setPortalTargetMapWorld()));
	connect(this->portalClearTargetPushButton, SIGNAL(clicked(bool)), this, SLOT(clearPortalTarget()));
	connect(this->portalHelpPushButton, SIGNAL(clicked(bool)), this, SLOT(showPortalHelp()));
}


void ObjectEditor::retranslateDynamicPortalUI()
{
	if(!this->portalGroupBox)
		return;

	const RuntimeTranslation::UILanguage ui_language = currentUILanguageForObjectEditor(this->settings);
	auto tr_portal = [ui_language](const char* source_text)
	{
		return translateObjectEditorRuntimeText(ui_language, source_text);
	};

	this->portalGroupBox->setTitle(tr_portal("Portal Editor"));
	this->portalStyleLabel->setText(tr_portal("Portal Style"));
	this->portalStyleComboBox->setItemText(0, tr_portal("Classic Marble"));
	this->portalStyleComboBox->setItemText(1, tr_portal("Arcane Rift"));
	this->portalStyleComboBox->setItemText(2, tr_portal("Stargate"));
	this->portalStyleComboBox->setItemText(3, tr_portal("Void Singularity"));
	this->portalStyleComboBox->setItemText(4, tr_portal("Lava Gate"));
	this->portalStyleComboBox->setItemText(5, tr_portal("Ice Gate"));
	this->portalStyleComboBox->setItemText(6, tr_portal("Forest Gate"));
	this->portalStyleComboBox->setItemText(7, tr_portal("Cyber Gate"));
	this->portalStyleComboBox->setItemText(8, tr_portal("Solar Gate"));
	this->portalStyleComboBox->setItemText(9, tr_portal("Ghost Gate"));
	this->portalApplyStylePushButton->setText(tr_portal("Apply Style"));
	this->portalResetMaterialsPushButton->setText(tr_portal("Reset Materials"));
	this->portalMainWorldPushButton->setText(tr_portal("Main World"));
	this->portalMapWorldPushButton->setText(tr_portal("Map World"));
	this->portalClearTargetPushButton->setText(tr_portal("Clear Target"));
	this->portalTipLabel->setText(tr_portal("A professional portal is built from three things: a clear destination URL, a readable silhouette, and a strong inner effect. Use a style preset first, then tune Portal Effect material for colour and glow."));

	const QString style_tip = tr_portal("Applies a complete visual style to all five portal materials: inner rim, arch body, outer edge, portal effect and threshold.");
	this->portalStyleLabel->setToolTip(style_tip);
	this->portalStyleComboBox->setToolTip(style_tip);
	this->portalApplyStylePushButton->setToolTip(style_tip);
	this->portalResetMaterialsPushButton->setToolTip(tr_portal("Restore the default classic portal material set."));
	this->portalMainWorldPushButton->setToolTip(tr_portal("Set this portal to travel to the main Metasiberia world."));
	this->portalMapWorldPushButton->setToolTip(tr_portal("Set this portal to travel to the map world."));
	this->portalClearTargetPushButton->setToolTip(tr_portal("Clear the destination so the portal is visibly unfinished and cannot mislead players."));
	this->portalHelpPushButton->setToolTip(tr_portal("Open portal design help and a list of missing professional features."));
}


void ObjectEditor::createGaussianSplatEditorUI()
{
	if(this->gaussianSplatGroupBox)
		return;

	this->gaussianSplatGroupBox = new QGroupBox(this);
	QGridLayout* grid = new QGridLayout(this->gaussianSplatGroupBox);
	grid->setContentsMargins(8, 8, 8, 8);
	grid->setHorizontalSpacing(8);
	grid->setVerticalSpacing(6);
	grid->setColumnMinimumWidth(0, 120);
	grid->setColumnStretch(0, 0);
	grid->setColumnStretch(1, 1);

	int row = 0;
	this->gaussianSplatPresetLabel = new QLabel(this->gaussianSplatGroupBox);
	this->gaussianSplatPresetComboBox = new QComboBox(this->gaussianSplatGroupBox);
	this->gaussianSplatPresetComboBox->addItem(QStringLiteral("Original / SuperSplat default"), QStringLiteral("original"));
	this->gaussianSplatPresetComboBox->addItem(QStringLiteral("Clean Haze"), QStringLiteral("clean_haze"));
	this->gaussianSplatPresetComboBox->addItem(QStringLiteral("Metasiberia Clear"), QStringLiteral("metasiberia_clear"));
	configureDockComboBox(this->gaussianSplatPresetComboBox, 12, 220);
	this->gaussianSplatResetPushButton = new QPushButton(this->gaussianSplatGroupBox);
	QHBoxLayout* preset_layout = new QHBoxLayout();
	preset_layout->setContentsMargins(0, 0, 0, 0);
	preset_layout->setSpacing(6);
	preset_layout->addWidget(this->gaussianSplatPresetComboBox, 1);
	preset_layout->addWidget(this->gaussianSplatResetPushButton);
	grid->addWidget(this->gaussianSplatPresetLabel, row, 0);
	grid->addLayout(preset_layout, row++, 1);

	this->gaussianSplatSHDetailLabel = new QLabel(this->gaussianSplatGroupBox);
	this->gaussianSplatSHDetailComboBox = new QComboBox(this->gaussianSplatGroupBox);
	this->gaussianSplatSHDetailComboBox->addItem(QStringLiteral("Auto"), -1);
	this->gaussianSplatSHDetailComboBox->addItem(QStringLiteral("DC only"), 0);
	this->gaussianSplatSHDetailComboBox->addItem(QStringLiteral("Degree 1"), 1);
	this->gaussianSplatSHDetailComboBox->addItem(QStringLiteral("Degree 2"), 2);
	this->gaussianSplatSHDetailComboBox->addItem(QStringLiteral("Degree 3"), 3);
	configureDockComboBox(this->gaussianSplatSHDetailComboBox, 12, 220);
	grid->addWidget(this->gaussianSplatSHDetailLabel, row, 0);
	grid->addWidget(this->gaussianSplatSHDetailComboBox, row++, 1);

	auto add_real_control = [this, grid, &row](QLabel*& label_out, double min_val, double max_val, double step, double slider_min, double slider_max, int slider_steps) -> RealControl*
	{
		label_out = new QLabel(this->gaussianSplatGroupBox);
		RealControl* control = new RealControl(this->gaussianSplatGroupBox);
		control->setMinimum(min_val);
		control->setMaximum(max_val);
		control->setSingleStep(step);
		control->setSliderMinimum(slider_min);
		control->setSliderMaximum(slider_max);
		control->setSliderSteps(slider_steps);
		grid->addWidget(label_out, row, 0);
		grid->addWidget(control, row++, 1);
		connect(control, SIGNAL(valueChanged(double)), this, SIGNAL(objectChanged()));
		return control;
	};

	this->gaussianSplatOpacitySpinBox = add_real_control(this->gaussianSplatOpacityLabel, 0.05, 32.0, 0.05, 0.05, 8.0, 318);
	this->gaussianSplatMinimumSourceOpacitySpinBox = add_real_control(this->gaussianSplatMinimumSourceOpacityLabel, 0.0, 1.0, 0.005, 0.0, 0.25, 100);
	this->gaussianSplatBrightnessSpinBox = add_real_control(this->gaussianSplatBrightnessLabel, 0.05, 4.0, 0.05, 0.05, 2.0, 80);
	this->gaussianSplatRadiusSpinBox = add_real_control(this->gaussianSplatRadiusLabel, 0.10, 4.0, 0.02, 0.10, 2.0, 95);
	this->gaussianSplatSaturationSpinBox = add_real_control(this->gaussianSplatSaturationLabel, 0.0, 3.0, 0.05, 0.0, 2.0, 80);
	this->gaussianSplatContrastSpinBox = add_real_control(this->gaussianSplatContrastLabel, 0.10, 4.0, 0.05, 0.10, 2.5, 96);
	this->gaussianSplatAlphaCutoffSpinBox = add_real_control(this->gaussianSplatAlphaCutoffLabel, 0.0, 0.25, 0.001, 0.0, 0.08, 80);

	this->gaussianSplatTipLabel = new QLabel(this->gaussianSplatGroupBox);
	this->gaussianSplatTipLabel->setWordWrap(true);
	this->gaussianSplatTipLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
	grid->addWidget(this->gaussianSplatTipLabel, row++, 0, 1, 2);

	this->verticalLayout->addWidget(this->gaussianSplatGroupBox);
	this->gaussianSplatGroupBox->hide();

	connect(this->gaussianSplatPresetComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index)
	{
		if(index < 0)
			return;

		applyGaussianSplatPreset(this->gaussianSplatPresetComboBox->itemData(index).toString());
		emit objectChanged();
	});

	connect(this->gaussianSplatResetPushButton, &QPushButton::clicked, this, [this]()
	{
		SignalBlocker::setCurrentIndex(this->gaussianSplatPresetComboBox, 0);
		applyGaussianSplatPreset(QStringLiteral("original"));
		emit objectChanged();
	});

	connect(this->gaussianSplatSHDetailComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int)
	{
		SignalBlocker::setCurrentIndex(this->gaussianSplatPresetComboBox, -1);
		emit objectChanged();
	});

	RealControl* const gaussian_controls[] = {
		this->gaussianSplatOpacitySpinBox,
		this->gaussianSplatMinimumSourceOpacitySpinBox,
		this->gaussianSplatBrightnessSpinBox,
		this->gaussianSplatRadiusSpinBox,
		this->gaussianSplatSaturationSpinBox,
		this->gaussianSplatContrastSpinBox,
		this->gaussianSplatAlphaCutoffSpinBox
	};
	for(RealControl* control : gaussian_controls)
		connect(control, &RealControl::valueChanged, this, [this](double)
		{
			SignalBlocker::setCurrentIndex(this->gaussianSplatPresetComboBox, -1);
		});
}


void ObjectEditor::applyGaussianSplatPreset(const QString& preset)
{
	const bool clean_haze = preset == QStringLiteral("clean_haze");
	const bool metasiberia_clear = preset == QStringLiteral("metasiberia_clear");

	SignalBlocker::setValue(this->gaussianSplatOpacitySpinBox, metasiberia_clear ? 1.25 : 1.0);
	SignalBlocker::setValue(this->gaussianSplatMinimumSourceOpacitySpinBox, (clean_haze || metasiberia_clear) ? 0.08 : 0.0);
	SignalBlocker::setValue(this->gaussianSplatBrightnessSpinBox, 1.0);
	SignalBlocker::setValue(this->gaussianSplatRadiusSpinBox, 1.0);
	SignalBlocker::setValue(this->gaussianSplatSaturationSpinBox, 1.0);
	SignalBlocker::setValue(this->gaussianSplatContrastSpinBox, 1.0);
	SignalBlocker::setValue(this->gaussianSplatAlphaCutoffSpinBox, 1.0 / 255.0);
	SignalBlocker::setCurrentIndex(this->gaussianSplatSHDetailComboBox, 0); // Auto
}


void ObjectEditor::retranslateDynamicGaussianSplatUI()
{
	if(!this->gaussianSplatGroupBox)
		return;

	const RuntimeTranslation::UILanguage ui_language = currentUILanguageForObjectEditor(this->settings);
	auto tr_gaussian = [ui_language](const char* source_text)
	{
		return translateObjectEditorRuntimeText(ui_language, source_text);
	};

	this->gaussianSplatGroupBox->setTitle(tr_gaussian("GaussianSplats Editor"));
	this->gaussianSplatPresetLabel->setText(tr_gaussian("Preset"));
	this->gaussianSplatPresetComboBox->setItemText(0, tr_gaussian("Original / SuperSplat default"));
	this->gaussianSplatPresetComboBox->setItemText(1, tr_gaussian("Clean Haze"));
	this->gaussianSplatPresetComboBox->setItemText(2, tr_gaussian("Metasiberia Clear"));
	this->gaussianSplatResetPushButton->setText(tr_gaussian("Reset to Original"));
	this->gaussianSplatSHDetailLabel->setText(tr_gaussian("SH Detail"));
	this->gaussianSplatSHDetailComboBox->setItemText(0, tr_gaussian("Auto"));
	this->gaussianSplatSHDetailComboBox->setItemText(1, tr_gaussian("DC only"));
	this->gaussianSplatSHDetailComboBox->setItemText(2, tr_gaussian("Degree 1"));
	this->gaussianSplatSHDetailComboBox->setItemText(3, tr_gaussian("Degree 2"));
	this->gaussianSplatSHDetailComboBox->setItemText(4, tr_gaussian("Degree 3"));
	this->gaussianSplatOpacityLabel->setText(tr_gaussian("Density / Opacity"));
	this->gaussianSplatMinimumSourceOpacityLabel->setText(tr_gaussian("Minimum Source Opacity"));
	this->gaussianSplatBrightnessLabel->setText(tr_gaussian("Brightness"));
	this->gaussianSplatRadiusLabel->setText(tr_gaussian("Splat Radius"));
	this->gaussianSplatSaturationLabel->setText(tr_gaussian("Saturation"));
	this->gaussianSplatContrastLabel->setText(tr_gaussian("Contrast"));
	this->gaussianSplatAlphaCutoffLabel->setText(tr_gaussian("Edge Alpha Clip"));
	this->gaussianSplatTipLabel->setText(tr_gaussian("Start with Original to preserve the source. Minimum Source Opacity removes weak source splats and haze; Edge Alpha Clip only trims transparent pixels around each rendered splat. SH Detail limits view-dependent colour detail."));

	const QString preset_tip = tr_gaussian("Choose a safe starting look. Presets only change render controls and never rewrite the source asset.");
	this->gaussianSplatPresetLabel->setToolTip(preset_tip);
	this->gaussianSplatPresetComboBox->setToolTip(preset_tip);
	this->gaussianSplatResetPushButton->setToolTip(tr_gaussian("Restore Original / SuperSplat defaults: density 1, identity colour and radius, no source-opacity filter, automatic SH detail and a 1/255 edge alpha clip."));

	const QString sh_tip = tr_gaussian("Limits spherical-harmonic colour detail. Auto preserves every SH degree available in the source; lower degrees reduce view-dependent detail and can look steadier.");
	this->gaussianSplatSHDetailLabel->setToolTip(sh_tip);
	this->gaussianSplatSHDetailComboBox->setToolTip(sh_tip);

	const QString source_opacity_tip = tr_gaussian("Drops source splats below this original opacity before rendering. Increase it slowly to remove haze and floaters; high values can erase fine geometry.");
	this->gaussianSplatMinimumSourceOpacityLabel->setToolTip(source_opacity_tip);
	this->gaussianSplatMinimumSourceOpacitySpinBox->setToolTip(source_opacity_tip);

	const QString edge_clip_tip = tr_gaussian("Discards very transparent pixels only at the soft edge of each rendered splat. It does not remove source splats; raise it carefully to sharpen silhouettes.");
	this->gaussianSplatAlphaCutoffLabel->setToolTip(edge_clip_tip);
	this->gaussianSplatAlphaCutoffSpinBox->setToolTip(edge_clip_tip);
}


void ObjectEditor::setGaussianSplatControlsFromContent(const std::string& content)
{
	const GaussianSplatRenderSettings render_settings = GaussianSplatRenderSettings::fromContent(content);
	SignalBlocker::setValue(this->gaussianSplatOpacitySpinBox, render_settings.opacity_multiplier);
	SignalBlocker::setValue(this->gaussianSplatMinimumSourceOpacitySpinBox, render_settings.minimum_source_opacity);
	SignalBlocker::setValue(this->gaussianSplatBrightnessSpinBox, render_settings.brightness);
	SignalBlocker::setValue(this->gaussianSplatRadiusSpinBox, render_settings.radius_multiplier);
	SignalBlocker::setValue(this->gaussianSplatSaturationSpinBox, render_settings.saturation);
	SignalBlocker::setValue(this->gaussianSplatContrastSpinBox, render_settings.contrast);
	SignalBlocker::setValue(this->gaussianSplatAlphaCutoffSpinBox, render_settings.alpha_cutoff);
	const int sh_index = this->gaussianSplatSHDetailComboBox->findData(render_settings.sh_degree_override);
	SignalBlocker::setCurrentIndex(this->gaussianSplatSHDetailComboBox, sh_index >= 0 ? sh_index : 0);

	auto approximately_equal = [](float a, float b) { return std::fabs(a - b) < 1.0e-5f; };
	const bool identity_appearance =
		approximately_equal(render_settings.brightness, 1.0f) &&
		approximately_equal(render_settings.radius_multiplier, 1.0f) &&
		approximately_equal(render_settings.saturation, 1.0f) &&
		approximately_equal(render_settings.contrast, 1.0f) &&
		approximately_equal(render_settings.alpha_cutoff, 1.0f / 255.0f) &&
		render_settings.sh_degree_override == -1;
	int preset_index = -1;
	if(identity_appearance && approximately_equal(render_settings.opacity_multiplier, 1.0f) && approximately_equal(render_settings.minimum_source_opacity, 0.0f))
		preset_index = 0;
	else if(identity_appearance && approximately_equal(render_settings.opacity_multiplier, 1.0f) && approximately_equal(render_settings.minimum_source_opacity, 0.08f))
		preset_index = 1;
	else if(identity_appearance && approximately_equal(render_settings.opacity_multiplier, 1.25f) && approximately_equal(render_settings.minimum_source_opacity, 0.08f))
		preset_index = 2;
	SignalBlocker::setCurrentIndex(this->gaussianSplatPresetComboBox, preset_index);
}


GaussianSplatRenderSettings ObjectEditor::gaussianSplatControlsToSettings() const
{
	GaussianSplatRenderSettings render_settings;
	render_settings.opacity_multiplier = (float)this->gaussianSplatOpacitySpinBox->value();
	render_settings.minimum_source_opacity = (float)this->gaussianSplatMinimumSourceOpacitySpinBox->value();
	render_settings.brightness = (float)this->gaussianSplatBrightnessSpinBox->value();
	render_settings.radius_multiplier = (float)this->gaussianSplatRadiusSpinBox->value();
	render_settings.saturation = (float)this->gaussianSplatSaturationSpinBox->value();
	render_settings.contrast = (float)this->gaussianSplatContrastSpinBox->value();
	render_settings.alpha_cutoff = (float)this->gaussianSplatAlphaCutoffSpinBox->value();
	render_settings.sh_degree_override = this->gaussianSplatSHDetailComboBox->currentData().toInt();
	return render_settings;
}


void ObjectEditor::createParticleEditorUI()
{
	if(this->particleGroupBox)
		return;

	this->particleGroupBox = new QGroupBox(this);
	QGridLayout* grid = new QGridLayout(this->particleGroupBox);
	grid->setContentsMargins(8, 8, 8, 8);
	grid->setHorizontalSpacing(8);
	grid->setVerticalSpacing(6);
	grid->setColumnMinimumWidth(0, 120);
	grid->setColumnStretch(0, 0);
	grid->setColumnStretch(1, 1);

	int row = 0;
	this->particleEnabledCheckBox = new QCheckBox(this->particleGroupBox);
	this->particleHelpPushButton = new QPushButton(this->particleGroupBox);
	this->particleHelpPushButton->setFixedSize(28, 24);
	QHBoxLayout* particle_header_layout = new QHBoxLayout();
	particle_header_layout->setContentsMargins(0, 0, 0, 0);
	particle_header_layout->addWidget(this->particleEnabledCheckBox);
	particle_header_layout->addStretch(1);
	particle_header_layout->addWidget(this->particleHelpPushButton);
	grid->addLayout(particle_header_layout, row++, 0, 1, 2);

	this->particlePreviewLabel = new QLabel(this->particleGroupBox);
	this->particlePreviewLabel->setMinimumHeight(74);
	this->particlePreviewLabel->setAlignment(Qt::AlignCenter);
	this->particlePreviewLabel->setFrameShape(QFrame::StyledPanel);
	grid->addWidget(this->particlePreviewLabel, row++, 0, 1, 2);

	this->particlePresetLabel = new QLabel(this->particleGroupBox);
	this->particlePresetComboBox = new QComboBox(this->particleGroupBox);
	this->particlePresetComboBox->addItem(QStringLiteral("Smoke"), QStringLiteral("smoke"));
	this->particlePresetComboBox->addItem(QStringLiteral("Steam"), QStringLiteral("steam"));
	this->particlePresetComboBox->addItem(QStringLiteral("Foam Spray"), QStringLiteral("foam_spray"));
	this->particlePresetComboBox->addItem(QStringLiteral("Fire"), QStringLiteral("fire"));
	this->particlePresetComboBox->addItem(QStringLiteral("Snow"), QStringLiteral("snow"));
	this->particlePresetComboBox->addItem(QStringLiteral("Sparks"), QStringLiteral("sparks"));
	this->particlePresetComboBox->addItem(QStringLiteral("Magic Dust"), QStringLiteral("magic"));
	this->particlePresetComboBox->addItem(QStringLiteral("Embers"), QStringLiteral("embers"));
	this->particlePresetComboBox->addItem(QStringLiteral("Rain"), QStringLiteral("rain"));
	this->particlePresetComboBox->addItem(QStringLiteral("Plasma"), QStringLiteral("plasma"));
	this->particlePresetComboBox->addItem(QStringLiteral("Nebula"), QStringLiteral("nebula"));
	this->particlePresetComboBox->addItem(QStringLiteral("Starfield"), QStringLiteral("starfield"));
	this->particlePresetComboBox->addItem(QStringLiteral("Black Hole"), QStringLiteral("black_hole"));
	this->particlePresetComboBox->addItem(QStringLiteral("Gravity Well"), QStringLiteral("gravity_well"));
	this->particlePresetComboBox->addItem(QStringLiteral("Meteor Shower"), QStringLiteral("meteor_shower"));
	this->particlePresetComboBox->addItem(QStringLiteral("Electric Arc"), QStringLiteral("electric_arc"));
	this->particlePresetComboBox->addItem(QStringLiteral("Fireflies"), QStringLiteral("fireflies"));
	this->particlePresetComboBox->addItem(QStringLiteral("Comet Tail"), QStringLiteral("comet_tail"));
	this->particlePresetComboBox->addItem(QStringLiteral("Galaxy Spiral"), QStringLiteral("galaxy_spiral"));
	this->particlePresetComboBox->addItem(QStringLiteral("Supernova"), QStringLiteral("supernova"));
	this->particlePresetComboBox->addItem(QStringLiteral("Pulsar Beam"), QStringLiteral("pulsar_beam"));
	this->particlePresetComboBox->addItem(QStringLiteral("Solar Wind"), QStringLiteral("solar_wind"));
	this->particlePresetComboBox->addItem(QStringLiteral("Cosmic Dust"), QStringLiteral("cosmic_dust"));
	this->particlePresetComboBox->addItem(QStringLiteral("Wormhole"), QStringLiteral("wormhole"));
	this->particlePresetComboBox->addItem(QStringLiteral("Ion Thruster"), QStringLiteral("ion_thruster"));
	this->particlePresetComboBox->addItem(QStringLiteral("Aurora Curtain"), QStringLiteral("aurora_curtain"));
	this->particlePresetComboBox->addItem(QStringLiteral("Energy Shield"), QStringLiteral("energy_shield"));
	configureDockComboBox(this->particlePresetComboBox, 16, 280);
	this->particleSavePresetPushButton = new QPushButton(this->particleGroupBox);
	this->particleDeletePresetPushButton = new QPushButton(this->particleGroupBox);
	this->particleSavePresetPushButton->setMinimumWidth(72);
	this->particleDeletePresetPushButton->setMinimumWidth(72);
	QHBoxLayout* preset_layout = new QHBoxLayout();
	preset_layout->setContentsMargins(0, 0, 0, 0);
	preset_layout->addWidget(this->particlePresetComboBox, 1);
	preset_layout->addWidget(this->particleSavePresetPushButton);
	preset_layout->addWidget(this->particleDeletePresetPushButton);
	grid->addWidget(this->particlePresetLabel, row, 0);
	grid->addLayout(preset_layout, row++, 1);

	this->particleKindLabel = new QLabel(this->particleGroupBox);
	this->particleKindComboBox = new QComboBox(this->particleGroupBox);
	this->particleKindComboBox->addItem(QStringLiteral("Smoke"), QStringLiteral("smoke"));
	this->particleKindComboBox->addItem(QStringLiteral("Foam"), QStringLiteral("foam"));
	this->particleKindComboBox->addItem(QStringLiteral("Spark"), QStringLiteral("spark"));
	this->particleKindComboBox->addItem(QStringLiteral("Streak"), QStringLiteral("streak"));
	this->particleKindComboBox->addItem(QStringLiteral("Star"), QStringLiteral("star"));
	this->particleKindComboBox->addItem(QStringLiteral("Ring"), QStringLiteral("ring"));
	this->particleKindComboBox->addItem(QStringLiteral("Nebula"), QStringLiteral("nebula"));
	this->particleKindComboBox->addItem(QStringLiteral("Flame"), QStringLiteral("flame"));
	this->particleKindComboBox->addItem(QStringLiteral("Snowflake"), QStringLiteral("snowflake"));
	this->particleKindComboBox->addItem(QStringLiteral("Soft Disc"), QStringLiteral("soft_disc"));
	configureDockComboBox(this->particleKindComboBox, 12, 240);
	grid->addWidget(this->particleKindLabel, row, 0);
	grid->addWidget(this->particleKindComboBox, row++, 1);

	this->particleDirectionLabel = new QLabel(this->particleGroupBox);
	this->particleDirectionComboBox = new QComboBox(this->particleGroupBox);
	this->particleDirectionComboBox->addItem(QStringLiteral("Up"), QStringLiteral("up"));
	this->particleDirectionComboBox->addItem(QStringLiteral("Forward"), QStringLiteral("forward"));
	this->particleDirectionComboBox->addItem(QStringLiteral("Down"), QStringLiteral("down"));
	this->particleDirectionComboBox->addItem(QStringLiteral("Random"), QStringLiteral("random"));
	this->particleDirectionComboBox->addItem(QStringLiteral("Custom"), QStringLiteral("custom"));
	configureDockComboBox(this->particleDirectionComboBox, 12, 220);
	grid->addWidget(this->particleDirectionLabel, row, 0);
	grid->addWidget(this->particleDirectionComboBox, row++, 1);

	this->particleShapeLabel = new QLabel(this->particleGroupBox);
	this->particleShapeComboBox = new QComboBox(this->particleGroupBox);
	this->particleShapeComboBox->addItem(QStringLiteral("Point"), QStringLiteral("point"));
	this->particleShapeComboBox->addItem(QStringLiteral("Disc"), QStringLiteral("disc"));
	this->particleShapeComboBox->addItem(QStringLiteral("Sphere"), QStringLiteral("sphere"));
	this->particleShapeComboBox->addItem(QStringLiteral("Box"), QStringLiteral("box"));
	this->particleShapeComboBox->addItem(QStringLiteral("Ring"), QStringLiteral("ring"));
	this->particleShapeComboBox->addItem(QStringLiteral("Cylinder"), QStringLiteral("cylinder"));
	this->particleShapeComboBox->addItem(QStringLiteral("Cone"), QStringLiteral("cone"));
	this->particleShapeComboBox->addItem(QStringLiteral("Line"), QStringLiteral("line"));
	this->particleShapeComboBox->addItem(QStringLiteral("Hemisphere"), QStringLiteral("hemisphere"));
	configureDockComboBox(this->particleShapeComboBox, 12, 240);
	grid->addWidget(this->particleShapeLabel, row, 0);
	grid->addWidget(this->particleShapeComboBox, row++, 1);

	this->particleRenderModeLabel = new QLabel(this->particleGroupBox);
	this->particleRenderModeComboBox = new QComboBox(this->particleGroupBox);
	this->particleRenderModeComboBox->addItem(QStringLiteral("Soft Smoke"), QStringLiteral("soft"));
	this->particleRenderModeComboBox->addItem(QStringLiteral("Additive Glow"), QStringLiteral("additive_glow"));
	configureDockComboBox(this->particleRenderModeComboBox, 14, 240);
	grid->addWidget(this->particleRenderModeLabel, row, 0);
	grid->addWidget(this->particleRenderModeComboBox, row++, 1);

	this->particleSpriteLibraryLabel = new QLabel(this->particleGroupBox);
	this->particleSpriteLibraryComboBox = new QComboBox(this->particleGroupBox);
	this->particleSpriteLibraryComboBox->addItem(QStringLiteral("Built-in Default"), QStringLiteral(""));
	this->particleSpriteLibraryComboBox->addItem(QStringLiteral("Soft Disc"), QStringLiteral("builtin:soft_disc"));
	this->particleSpriteLibraryComboBox->addItem(QStringLiteral("Spark"), QStringLiteral("builtin:spark"));
	this->particleSpriteLibraryComboBox->addItem(QStringLiteral("Star"), QStringLiteral("builtin:star"));
	this->particleSpriteLibraryComboBox->addItem(QStringLiteral("Ring"), QStringLiteral("builtin:ring"));
	this->particleSpriteLibraryComboBox->addItem(QStringLiteral("Nebula"), QStringLiteral("builtin:nebula"));
	this->particleSpriteLibraryComboBox->addItem(QStringLiteral("Streak"), QStringLiteral("builtin:streak"));
	this->particleSpriteLibraryComboBox->addItem(QStringLiteral("Flame"), QStringLiteral("builtin:flame"));
	this->particleSpriteLibraryComboBox->addItem(QStringLiteral("Foam"), QStringLiteral("builtin:foam"));
	this->particleSpriteLibraryComboBox->addItem(QStringLiteral("Snowflake"), QStringLiteral("builtin:snowflake"));
	this->particleSpriteLibraryComboBox->addItem(QStringLiteral("Comet"), QStringLiteral("builtin:comet"));
	this->particleSpriteLibraryComboBox->addItem(QStringLiteral("Galaxy"), QStringLiteral("builtin:galaxy"));
	this->particleSpriteLibraryComboBox->addItem(QStringLiteral("Flare"), QStringLiteral("builtin:flare"));
	this->particleSpriteLibraryComboBox->addItem(QStringLiteral("Beam"), QStringLiteral("builtin:beam"));
	this->particleSpriteLibraryComboBox->addItem(QStringLiteral("Cosmic Dust"), QStringLiteral("builtin:dust"));
	this->particleSpriteLibraryComboBox->addItem(QStringLiteral("Spiral"), QStringLiteral("builtin:spiral"));
	this->particleSpriteLibraryComboBox->addItem(QStringLiteral("Aurora"), QStringLiteral("builtin:aurora"));
	this->particleSpriteLibraryComboBox->addItem(QStringLiteral("Bubble"), QStringLiteral("builtin:bubble"));
	this->particleSpriteLibraryComboBox->addItem(QStringLiteral("Shard"), QStringLiteral("builtin:shard"));
	this->particleSpriteLibraryComboBox->addItem(QStringLiteral("Smoke Wisp"), QStringLiteral("builtin:smoke_wisp"));
	configureDockComboBox(this->particleSpriteLibraryComboBox, 14, 260);
	grid->addWidget(this->particleSpriteLibraryLabel, row, 0);
	grid->addWidget(this->particleSpriteLibraryComboBox, row++, 1);

	this->particleSpritePathLabel = new QLabel(this->particleGroupBox);
	this->particleSpritePathLineEdit = new QLineEdit(this->particleGroupBox);
	this->particleSpriteBrowsePushButton = new QPushButton(this->particleGroupBox);
	this->particleSpriteClearPushButton = new QPushButton(this->particleGroupBox);
	this->particleSpriteBrowsePushButton->setFixedWidth(34);
	this->particleSpriteClearPushButton->setMinimumWidth(64);
	QHBoxLayout* sprite_layout = new QHBoxLayout();
	sprite_layout->setContentsMargins(0, 0, 0, 0);
	sprite_layout->addWidget(this->particleSpritePathLineEdit, 1);
	sprite_layout->addWidget(this->particleSpriteBrowsePushButton);
	sprite_layout->addWidget(this->particleSpriteClearPushButton);
	grid->addWidget(this->particleSpritePathLabel, row, 0);
	grid->addLayout(sprite_layout, row++, 1);

	auto add_real_control = [this, grid, &row](QLabel*& label_out, const char* label_text, double min_val, double max_val, double step, double slider_min, double slider_max, int slider_steps, const char* suffix) -> RealControl*
	{
		label_out = new QLabel(QCoreApplication::translate("ObjectEditor", label_text), this->particleGroupBox);
		RealControl* control = new RealControl(this->particleGroupBox);
		control->setMinimum(min_val);
		control->setMaximum(max_val);
		control->setSingleStep(step);
		control->setSliderMinimum(slider_min);
		control->setSliderMaximum(slider_max);
		control->setSliderSteps(slider_steps);
		control->setSuffix(QCoreApplication::translate("ObjectEditor", suffix));
		grid->addWidget(label_out, row, 0);
		grid->addWidget(control, row++, 1);
		connect(control, SIGNAL(valueChanged(double)), this, SIGNAL(objectChanged()));
		connect(control, SIGNAL(valueChanged(double)), this, SLOT(updateParticlePreviewThumbnail()));
		return control;
	};

	this->particleAudioEnabledCheckBox = new QCheckBox(this->particleGroupBox);
	grid->addWidget(this->particleAudioEnabledCheckBox, row++, 0, 1, 2);

	this->particleAudioURLLabel = new QLabel(this->particleGroupBox);
	this->particleAudioURLLineEdit = new QLineEdit(this->particleGroupBox);
	this->particleAudioBrowsePushButton = new QPushButton(this->particleGroupBox);
	this->particleAudioClearPushButton = new QPushButton(this->particleGroupBox);
	this->particleAudioBrowsePushButton->setFixedWidth(34);
	this->particleAudioClearPushButton->setMinimumWidth(64);
	QHBoxLayout* particle_audio_layout = new QHBoxLayout();
	particle_audio_layout->setContentsMargins(0, 0, 0, 0);
	particle_audio_layout->addWidget(this->particleAudioURLLineEdit, 1);
	particle_audio_layout->addWidget(this->particleAudioBrowsePushButton);
	particle_audio_layout->addWidget(this->particleAudioClearPushButton);
	grid->addWidget(this->particleAudioURLLabel, row, 0);
	grid->addLayout(particle_audio_layout, row++, 1);

	this->particleAudioLoopCheckBox = new QCheckBox(this->particleGroupBox);
	this->particleAudioSpatialCheckBox = new QCheckBox(this->particleGroupBox);
	QHBoxLayout* particle_audio_flags_layout = new QHBoxLayout();
	particle_audio_flags_layout->setContentsMargins(0, 0, 0, 0);
	particle_audio_flags_layout->addWidget(this->particleAudioLoopCheckBox);
	particle_audio_flags_layout->addWidget(this->particleAudioSpatialCheckBox);
	particle_audio_flags_layout->addStretch(1);
	grid->addLayout(particle_audio_flags_layout, row++, 0, 1, 2);

	this->particleAudioVolumeSpinBox = add_real_control(this->particleAudioVolumeLabel, "Sound Volume", 0.0, 10.0, 0.05, 0.0, 2.0, 200, "");
	this->particleAudioActivationDistanceSpinBox = add_real_control(this->particleAudioActivationDistanceLabel, "Sound Activation Distance", 0.0, 1000.0, 0.5, 0.0, 100.0, 200, " m");
	this->particleAudioMinDistanceSpinBox = add_real_control(this->particleAudioMinDistanceLabel, "Sound Near Distance", 0.1, 100.0, 0.1, 0.1, 20.0, 200, " m");
	this->particleAudioMaxDistanceSpinBox = add_real_control(this->particleAudioMaxDistanceLabel, "Sound Radius", 1.0, 1000.0, 0.5, 1.0, 120.0, 238, " m");
	this->particleAudioFadeInSpinBox = add_real_control(this->particleAudioFadeInLabel, "Sound Fade In", 0.0, 30.0, 0.05, 0.0, 5.0, 100, " s");
	this->particleAudioFadeOutSpinBox = add_real_control(this->particleAudioFadeOutLabel, "Sound Fade Out", 0.0, 30.0, 0.05, 0.0, 5.0, 100, " s");

	this->particleRateSpinBox        = add_real_control(this->particleRateLabel,        "Emission Rate",   0.0,   400.0, 1.0,      0.0,    100.0, 200, " /s");
	this->particleFrameCapSpinBox    = add_real_control(this->particleFrameCapLabel,    "Frame Cap",       1.0,   256.0, 1.0,      1.0,    128.0, 127, "");
	this->particleMaxParticlesSpinBox = add_real_control(this->particleMaxParticlesLabel, "Max Particles",  1.0,  2048.0, 1.0,     1.0,    512.0, 511, "");
	this->particleRadiusSpinBox      = add_real_control(this->particleRadiusLabel,      "Emitter Radius",  0.0,   100.0, 0.05,     0.0,      5.0, 200, " m");
	this->particleSpeedSpinBox       = add_real_control(this->particleSpeedLabel,       "Speed",           0.0,   200.0, 0.1,      0.0,     20.0, 200, " m/s");
	this->particleSpeedJitterSpinBox = add_real_control(this->particleSpeedJitterLabel, "Speed Jitter",    0.0,     1.0, 0.05,     0.0,      1.0, 100, "");
	this->particleSpreadSpinBox      = add_real_control(this->particleSpreadLabel,      "Spread",          0.0,   180.0, 1.0,      0.0,    180.0, 180, " deg");
	this->particleTurbulenceSpinBox  = add_real_control(this->particleTurbulenceLabel,  "Turbulence",      0.0,    50.0, 0.05,     0.0,      8.0, 200, " m/s^2");
	this->particleLifetimeSpinBox    = add_real_control(this->particleLifetimeLabel,    "Lifetime",        0.05,  120.0, 0.1,      0.05,    20.0, 200, " s");
	this->particleStartWidthSpinBox  = add_real_control(this->particleStartWidthLabel,  "Start Size",      0.005, 100.0, 0.05,     0.005,    5.0, 200, " m");
	this->particleEndWidthSpinBox    = add_real_control(this->particleEndWidthLabel,    "End Size",        0.005, 100.0, 0.05,     0.005,    5.0, 200, " m");

	auto add_curve_control = [this, grid, &row](QLabel*& label_out) -> ParticleCurveWidget*
	{
		label_out = new QLabel(this->particleGroupBox);
		ParticleCurveWidget* curve_widget = new ParticleCurveWidget(this->particleGroupBox);
		curve_widget->setChangedCallback([this]() { updateParticlePreviewThumbnail(); emit objectChanged(); });
		grid->addWidget(label_out, row, 0);
		grid->addWidget(curve_widget, row++, 1);
		return curve_widget;
	};

	this->particleSizeCurveWidget = add_curve_control(this->particleSizeCurveLabel);
	this->particleSizeJitterSpinBox  = add_real_control(this->particleSizeJitterLabel,  "Size Jitter",     0.0,     1.0, 0.05,     0.0,      1.0, 100, "");
	this->particleOpacitySpinBox     = add_real_control(this->particleOpacityLabel,     "Opacity",         0.0,     1.0, 0.05,     0.0,      1.0, 100, "");
	this->particleEndOpacitySpinBox  = add_real_control(this->particleEndOpacityLabel,  "End Opacity",     0.0,     1.0, 0.05,     0.0,      1.0, 100, "");
	this->particleOpacityCurveWidget = add_curve_control(this->particleOpacityCurveLabel);
	this->particleOpacityJitterSpinBox = add_real_control(this->particleOpacityJitterLabel, "Opacity Jitter", 0.0,   1.0, 0.05,     0.0,      1.0, 100, "");

	this->particleColourLabel = new QLabel(this->particleGroupBox);
	this->particleColourPushButton = new QPushButton(this->particleGroupBox);
	this->particleColourPushButton->setFixedWidth(42);
	grid->addWidget(this->particleColourLabel, row, 0);
	grid->addWidget(this->particleColourPushButton, row++, 1, Qt::AlignLeft);
	this->particleEndColourLabel = new QLabel(this->particleGroupBox);
	this->particleEndColourPushButton = new QPushButton(this->particleGroupBox);
	this->particleEndColourPushButton->setFixedWidth(42);
	grid->addWidget(this->particleEndColourLabel, row, 0);
	grid->addWidget(this->particleEndColourPushButton, row++, 1, Qt::AlignLeft);
	this->particleColourJitterSpinBox = add_real_control(this->particleColourJitterLabel, "Colour Jitter", 0.0, 1.0, 0.05, 0.0, 1.0, 100, "");
	this->particleTrailLengthSpinBox = add_real_control(this->particleTrailLengthLabel, "Trail Length", 0.0, 100.0, 0.05, 0.0, 10.0, 200, " m");
	this->particleGlowStrengthSpinBox = add_real_control(this->particleGlowStrengthLabel, "Glow Strength", 0.2, 8.0, 0.1, 0.2, 5.0, 160, "");
	this->particleRotationSpinBox = add_real_control(this->particleRotationLabel, "Initial Rotation", -360.0, 360.0, 1.0, -180.0, 180.0, 360, " deg");
	this->particleRotationJitterSpinBox = add_real_control(this->particleRotationJitterLabel, "Rotation Jitter", 0.0, 360.0, 1.0, 0.0, 360.0, 360, " deg");
	this->particleSpinSpinBox = add_real_control(this->particleSpinLabel, "Spin", -720.0, 720.0, 1.0, -360.0, 360.0, 720, " deg/s");
	this->particleSpinJitterSpinBox = add_real_control(this->particleSpinJitterLabel, "Spin Jitter", 0.0, 720.0, 1.0, 0.0, 360.0, 360, " deg/s");

	this->particleBurstEnabledCheckBox = new QCheckBox(this->particleGroupBox);
	grid->addWidget(this->particleBurstEnabledCheckBox, row++, 0, 1, 2);
	this->particleBurstCountSpinBox    = add_real_control(this->particleBurstCountLabel,    "Burst Count",    1.0,   512.0, 1.0,  1.0, 128.0, 127, "");
	this->particleBurstIntervalSpinBox = add_real_control(this->particleBurstIntervalLabel, "Burst Interval", 0.05,  120.0, 0.05, 0.05, 10.0, 200, " s");
	this->particleMaxDistanceSpinBox   = add_real_control(this->particleMaxDistanceLabel,   "View Distance",  0.0, 10000.0, 1.0,  0.0, 300.0, 300, " m");

	this->particleBurstNowPushButton = new QPushButton(this->particleGroupBox);
	this->particleClearParticlesPushButton = new QPushButton(this->particleGroupBox);
	QHBoxLayout* live_buttons_layout = new QHBoxLayout();
	live_buttons_layout->setContentsMargins(0, 0, 0, 0);
	live_buttons_layout->addWidget(this->particleBurstNowPushButton);
	live_buttons_layout->addWidget(this->particleClearParticlesPushButton);
	grid->addLayout(live_buttons_layout, row++, 0, 1, 2);

	this->particleDiagnosticsLabel = new QLabel(this->particleGroupBox);
	this->particleDiagnosticsValueLabel = new QLabel(QStringLiteral("-"), this->particleGroupBox);
	grid->addWidget(this->particleDiagnosticsLabel, row, 0);
	grid->addWidget(this->particleDiagnosticsValueLabel, row++, 1);

	this->particleWindXSpinBox = add_real_control(this->particleWindXLabel, "Wind X", -50.0, 50.0, 0.05, -5.0, 5.0, 200, " m/s^2");
	this->particleWindYSpinBox = add_real_control(this->particleWindYLabel, "Wind Y", -50.0, 50.0, 0.05, -5.0, 5.0, 200, " m/s^2");
	this->particleWindZSpinBox = add_real_control(this->particleWindZLabel, "Wind Z", -50.0, 50.0, 0.05, -5.0, 5.0, 200, " m/s^2");
	this->particleVortexStrengthSpinBox = add_real_control(this->particleVortexStrengthLabel, "Vortex", -50.0, 50.0, 0.05, -5.0, 5.0, 200, " m/s^2");
	this->particleAttractorStrengthSpinBox = add_real_control(this->particleAttractorStrengthLabel, "Attractor", -50.0, 50.0, 0.05, -5.0, 5.0, 200, " m/s^2");
	this->particleAttractorRadiusSpinBox = add_real_control(this->particleAttractorRadiusLabel, "Attractor Radius", 0.0, 100.0, 0.05, 0.0, 10.0, 200, " m");
	this->particleBlackHoleCheckBox = new QCheckBox(this->particleGroupBox);
	grid->addWidget(this->particleBlackHoleCheckBox, row++, 0, 1, 2);
	this->particleEventHorizonSpinBox = add_real_control(this->particleEventHorizonLabel, "Event Horizon", 0.0, 20.0, 0.02, 0.0, 3.0, 150, " m");
	this->particleRadialAccelSpinBox = add_real_control(this->particleRadialAccelLabel, "Radial Force", -50.0, 50.0, 0.05, -5.0, 5.0, 200, " m/s^2");
	this->particleLinearDampingSpinBox = add_real_control(this->particleLinearDampingLabel, "Damping", 0.0, 10.0, 0.02, 0.0, 2.0, 200, " /s");
	this->particleBuoyancyLiftSpinBox = add_real_control(this->particleBuoyancyLiftLabel, "Buoyancy Lift", -5.0, 5.0, 0.05, -1.0, 2.0, 300, "");

	this->particleGravityScaleSpinBox = add_real_control(this->particleGravityScaleLabel, "Gravity Scale", -5.0, 5.0, 0.05, -1.0, 2.0, 300, "");
	this->particleDragAreaSpinBox     = add_real_control(this->particleDragAreaLabel,     "Drag Area",      0.0, 1.0, 0.0001, 0.0, 0.01, 200, " m^2");
	this->particleMassSpinBox         = add_real_control(this->particleMassLabel,         "Particle Mass",  1.0e-9, 10.0, 0.000001, 1.0e-9, 0.001, 200, " kg");
	this->particleRestitutionSpinBox  = add_real_control(this->particleRestitutionLabel,  "Bounce",         0.0, 1.0, 0.05, 0.0, 1.0, 100, "");
	this->particleCollisionFrictionSpinBox = add_real_control(this->particleCollisionFrictionLabel, "Collision Friction", 0.0, 1.0, 0.05, 0.0, 1.0, 100, "");

	this->particleCollideSurfacesCheckBox = new QCheckBox(this->particleGroupBox);
	this->particleDieOnSurfaceCheckBox = new QCheckBox(this->particleGroupBox);
	grid->addWidget(this->particleCollideSurfacesCheckBox, row++, 0, 1, 2);
	grid->addWidget(this->particleDieOnSurfaceCheckBox, row++, 0, 1, 2);

	connect(this->particleEnabledCheckBox, SIGNAL(toggled(bool)), this, SIGNAL(objectChanged()));
	connect(this->particleEnabledCheckBox, SIGNAL(toggled(bool)), this, SLOT(updateParticlePreviewThumbnail()));
	connect(this->particleHelpPushButton, SIGNAL(clicked(bool)), this, SLOT(showParticleHelp()));
	connect(this->particlePresetComboBox, SIGNAL(currentIndexChanged(int)), this, SLOT(particlePresetChanged(int)));
	connect(this->particleSavePresetPushButton, SIGNAL(clicked(bool)), this, SLOT(saveParticlePreset()));
	connect(this->particleDeletePresetPushButton, SIGNAL(clicked(bool)), this, SLOT(deleteParticlePreset()));
	connect(this->particleKindComboBox, SIGNAL(currentIndexChanged(int)), this, SIGNAL(objectChanged()));
	connect(this->particleDirectionComboBox, SIGNAL(currentIndexChanged(int)), this, SIGNAL(objectChanged()));
	connect(this->particleShapeComboBox, SIGNAL(currentIndexChanged(int)), this, SIGNAL(objectChanged()));
	connect(this->particleRenderModeComboBox, SIGNAL(currentIndexChanged(int)), this, SIGNAL(objectChanged()));
	connect(this->particleKindComboBox, SIGNAL(currentIndexChanged(int)), this, SLOT(updateParticlePreviewThumbnail()));
	connect(this->particleDirectionComboBox, SIGNAL(currentIndexChanged(int)), this, SLOT(updateParticlePreviewThumbnail()));
	connect(this->particleShapeComboBox, SIGNAL(currentIndexChanged(int)), this, SLOT(updateParticlePreviewThumbnail()));
	connect(this->particleRenderModeComboBox, SIGNAL(currentIndexChanged(int)), this, SLOT(updateParticlePreviewThumbnail()));
	connect(this->particleSpriteLibraryComboBox, SIGNAL(currentIndexChanged(int)), this, SLOT(particleSpriteLibraryChanged(int)));
	connect(this->particleSpritePathLineEdit, SIGNAL(textChanged(QString)), this, SIGNAL(objectChanged()));
	connect(this->particleSpritePathLineEdit, SIGNAL(textChanged(QString)), this, SLOT(updateParticlePreviewThumbnail()));
	connect(this->particleSpriteBrowsePushButton, SIGNAL(clicked(bool)), this, SLOT(browseParticleSprite()));
	connect(this->particleSpriteClearPushButton, SIGNAL(clicked(bool)), this, SLOT(clearParticleSprite()));
	connect(this->particleAudioEnabledCheckBox, SIGNAL(toggled(bool)), this, SIGNAL(objectChanged()));
	connect(this->particleAudioURLLineEdit, SIGNAL(textChanged(QString)), this, SIGNAL(objectChanged()));
	connect(this->particleAudioBrowsePushButton, SIGNAL(clicked(bool)), this, SLOT(browseParticleAudio()));
	connect(this->particleAudioClearPushButton, SIGNAL(clicked(bool)), this, SLOT(clearParticleAudio()));
	connect(this->particleAudioLoopCheckBox, SIGNAL(toggled(bool)), this, SIGNAL(objectChanged()));
	connect(this->particleAudioSpatialCheckBox, SIGNAL(toggled(bool)), this, SIGNAL(objectChanged()));
	connect(this->particleColourPushButton, SIGNAL(clicked(bool)), this, SLOT(on_particleColourPushButton_clicked(bool)));
	connect(this->particleEndColourPushButton, SIGNAL(clicked(bool)), this, SLOT(on_particleEndColourPushButton_clicked(bool)));
	connect(this->particleBurstEnabledCheckBox, SIGNAL(toggled(bool)), this, SIGNAL(objectChanged()));
	connect(this->particleBurstEnabledCheckBox, SIGNAL(toggled(bool)), this, SLOT(updateParticlePreviewThumbnail()));
	connect(this->particleBurstNowPushButton, SIGNAL(clicked(bool)), this, SIGNAL(particleBurstNowSignal()));
	connect(this->particleClearParticlesPushButton, SIGNAL(clicked(bool)), this, SIGNAL(particleClearParticlesSignal()));
	connect(this->particleBlackHoleCheckBox, SIGNAL(toggled(bool)), this, SIGNAL(objectChanged()));
	connect(this->particleBlackHoleCheckBox, SIGNAL(toggled(bool)), this, SLOT(updateParticlePreviewThumbnail()));
	connect(this->particleCollideSurfacesCheckBox, SIGNAL(toggled(bool)), this, SIGNAL(objectChanged()));
	connect(this->particleDieOnSurfaceCheckBox, SIGNAL(toggled(bool)), this, SIGNAL(objectChanged()));

	this->verticalLayout->insertWidget(2, this->particleGroupBox);
	this->particleGroupBox->hide();
}


void ObjectEditor::retranslateDynamicParticleUI()
{
	if(!this->particleGroupBox)
		return;

	const RuntimeTranslation::UILanguage ui_language = currentUILanguageForObjectEditor(this->settings);
	auto tr_particle = [ui_language](const char* source_text)
	{
		return translateObjectEditorRuntimeText(ui_language, source_text);
	};

	this->particleGroupBox->setTitle(tr_particle("Particle Editor"));
	this->particleEnabledCheckBox->setText(tr_particle("Enabled"));
	this->particleHelpPushButton->setText(tr_particle("?"));
	this->particlePresetLabel->setText(tr_particle("Preset"));
	this->particlePresetComboBox->setItemText(0, tr_particle("Smoke"));
	this->particlePresetComboBox->setItemText(1, tr_particle("Steam"));
	this->particlePresetComboBox->setItemText(2, tr_particle("Foam Spray"));
	this->particlePresetComboBox->setItemText(3, tr_particle("Fire"));
	this->particlePresetComboBox->setItemText(4, tr_particle("Snow"));
	this->particlePresetComboBox->setItemText(5, tr_particle("Sparks"));
	this->particlePresetComboBox->setItemText(6, tr_particle("Magic Dust"));
	this->particlePresetComboBox->setItemText(7, tr_particle("Embers"));
	this->particlePresetComboBox->setItemText(8, tr_particle("Rain"));
	this->particlePresetComboBox->setItemText(9, tr_particle("Plasma"));
	this->particlePresetComboBox->setItemText(10, tr_particle("Nebula"));
	this->particlePresetComboBox->setItemText(11, tr_particle("Starfield"));
	this->particlePresetComboBox->setItemText(12, tr_particle("Black Hole"));
	this->particlePresetComboBox->setItemText(13, tr_particle("Gravity Well"));
	this->particlePresetComboBox->setItemText(14, tr_particle("Meteor Shower"));
	this->particlePresetComboBox->setItemText(15, tr_particle("Electric Arc"));
	this->particlePresetComboBox->setItemText(16, tr_particle("Fireflies"));
	this->particlePresetComboBox->setItemText(17, tr_particle("Comet Tail"));
	this->particlePresetComboBox->setItemText(18, tr_particle("Galaxy Spiral"));
	this->particlePresetComboBox->setItemText(19, tr_particle("Supernova"));
	this->particlePresetComboBox->setItemText(20, tr_particle("Pulsar Beam"));
	this->particlePresetComboBox->setItemText(21, tr_particle("Solar Wind"));
	this->particlePresetComboBox->setItemText(22, tr_particle("Cosmic Dust"));
	this->particlePresetComboBox->setItemText(23, tr_particle("Wormhole"));
	this->particlePresetComboBox->setItemText(24, tr_particle("Ion Thruster"));
	this->particlePresetComboBox->setItemText(25, tr_particle("Aurora Curtain"));
	this->particlePresetComboBox->setItemText(26, tr_particle("Energy Shield"));
	this->particleSavePresetPushButton->setText(tr_particle("Save"));
	this->particleDeletePresetPushButton->setText(tr_particle("Delete"));
	this->particleKindLabel->setText(tr_particle("Particle Type"));
	this->particleKindComboBox->setItemText(0, tr_particle("Smoke"));
	this->particleKindComboBox->setItemText(1, tr_particle("Foam"));
	this->particleKindComboBox->setItemText(2, tr_particle("Spark"));
	this->particleKindComboBox->setItemText(3, tr_particle("Streak"));
	this->particleKindComboBox->setItemText(4, tr_particle("Star"));
	this->particleKindComboBox->setItemText(5, tr_particle("Ring"));
	this->particleKindComboBox->setItemText(6, tr_particle("Nebula"));
	this->particleKindComboBox->setItemText(7, tr_particle("Flame"));
	this->particleKindComboBox->setItemText(8, tr_particle("Snowflake"));
	this->particleKindComboBox->setItemText(9, tr_particle("Soft Disc"));
	this->particleDirectionLabel->setText(tr_particle("Direction"));
	this->particleDirectionComboBox->setItemText(0, tr_particle("Up"));
	this->particleDirectionComboBox->setItemText(1, tr_particle("Forward"));
	this->particleDirectionComboBox->setItemText(2, tr_particle("Down"));
	this->particleDirectionComboBox->setItemText(3, tr_particle("Random"));
	this->particleDirectionComboBox->setItemText(4, tr_particle("Custom"));
	this->particleShapeLabel->setText(tr_particle("Emitter Shape"));
	this->particleShapeComboBox->setItemText(0, tr_particle("Point"));
	this->particleShapeComboBox->setItemText(1, tr_particle("Disc"));
	this->particleShapeComboBox->setItemText(2, tr_particle("Sphere"));
	this->particleShapeComboBox->setItemText(3, tr_particle("Box"));
	this->particleShapeComboBox->setItemText(4, tr_particle("Ring"));
	this->particleShapeComboBox->setItemText(5, tr_particle("Cylinder"));
	this->particleShapeComboBox->setItemText(6, tr_particle("Cone"));
	this->particleShapeComboBox->setItemText(7, tr_particle("Line"));
	this->particleShapeComboBox->setItemText(8, tr_particle("Hemisphere"));
	this->particleRenderModeLabel->setText(tr_particle("Render Mode"));
	this->particleRenderModeComboBox->setItemText(0, tr_particle("Soft Smoke"));
	this->particleRenderModeComboBox->setItemText(1, tr_particle("Additive Glow"));
	this->particleSpriteLibraryLabel->setText(tr_particle("Sprite Library"));
	this->particleSpriteLibraryComboBox->setItemText(0, tr_particle("Built-in Default"));
	this->particleSpriteLibraryComboBox->setItemText(1, tr_particle("Soft Disc"));
	this->particleSpriteLibraryComboBox->setItemText(2, tr_particle("Spark"));
	this->particleSpriteLibraryComboBox->setItemText(3, tr_particle("Star"));
	this->particleSpriteLibraryComboBox->setItemText(4, tr_particle("Ring"));
	this->particleSpriteLibraryComboBox->setItemText(5, tr_particle("Nebula"));
	this->particleSpriteLibraryComboBox->setItemText(6, tr_particle("Streak"));
	this->particleSpriteLibraryComboBox->setItemText(7, tr_particle("Flame"));
	this->particleSpriteLibraryComboBox->setItemText(8, tr_particle("Foam"));
	this->particleSpriteLibraryComboBox->setItemText(9, tr_particle("Snowflake"));
	this->particleSpriteLibraryComboBox->setItemText(10, tr_particle("Comet"));
	this->particleSpriteLibraryComboBox->setItemText(11, tr_particle("Galaxy"));
	this->particleSpriteLibraryComboBox->setItemText(12, tr_particle("Flare"));
	this->particleSpriteLibraryComboBox->setItemText(13, tr_particle("Beam"));
	this->particleSpriteLibraryComboBox->setItemText(14, tr_particle("Cosmic Dust"));
	this->particleSpriteLibraryComboBox->setItemText(15, tr_particle("Spiral"));
	this->particleSpriteLibraryComboBox->setItemText(16, tr_particle("Aurora"));
	this->particleSpriteLibraryComboBox->setItemText(17, tr_particle("Bubble"));
	this->particleSpriteLibraryComboBox->setItemText(18, tr_particle("Shard"));
	this->particleSpriteLibraryComboBox->setItemText(19, tr_particle("Smoke Wisp"));
	this->particleSpritePathLabel->setText(tr_particle("Sprite Texture"));
	this->particleSpriteBrowsePushButton->setText(tr_particle("..."));
	this->particleSpriteClearPushButton->setText(tr_particle("Clear"));
	this->particleAudioEnabledCheckBox->setText(tr_particle("Enable particle sound"));
	this->particleAudioURLLabel->setText(tr_particle("Sound File"));
	this->particleAudioBrowsePushButton->setText(tr_particle("..."));
	this->particleAudioClearPushButton->setText(tr_particle("Clear"));
	this->particleAudioLoopCheckBox->setText(tr_particle("Loop sound"));
	this->particleAudioSpatialCheckBox->setText(tr_particle("3D spatial sound"));
	this->particleAudioVolumeLabel->setText(tr_particle("Sound Volume"));
	this->particleAudioActivationDistanceLabel->setText(tr_particle("Sound Activation Distance"));
	this->particleAudioMinDistanceLabel->setText(tr_particle("Sound Near Distance"));
	this->particleAudioMaxDistanceLabel->setText(tr_particle("Sound Radius"));
	this->particleAudioFadeInLabel->setText(tr_particle("Sound Fade In"));
	this->particleAudioFadeOutLabel->setText(tr_particle("Sound Fade Out"));
	this->particleRateLabel->setText(tr_particle("Emission Rate"));
	this->particleFrameCapLabel->setText(tr_particle("Frame Cap"));
	this->particleMaxParticlesLabel->setText(tr_particle("Max Particles"));
	this->particleRadiusLabel->setText(tr_particle("Emitter Radius"));
	this->particleSpeedLabel->setText(tr_particle("Speed"));
	this->particleSpeedJitterLabel->setText(tr_particle("Speed Jitter"));
	this->particleSpreadLabel->setText(tr_particle("Spread"));
	this->particleTurbulenceLabel->setText(tr_particle("Turbulence"));
	this->particleLifetimeLabel->setText(tr_particle("Lifetime"));
	this->particleStartWidthLabel->setText(tr_particle("Start Size"));
	this->particleEndWidthLabel->setText(tr_particle("End Size"));
	this->particleSizeCurveLabel->setText(tr_particle("Size Curve"));
	this->particleSizeJitterLabel->setText(tr_particle("Size Jitter"));
	this->particleOpacityLabel->setText(tr_particle("Opacity"));
	this->particleEndOpacityLabel->setText(tr_particle("End Opacity"));
	this->particleOpacityCurveLabel->setText(tr_particle("Opacity Curve"));
	this->particleOpacityJitterLabel->setText(tr_particle("Opacity Jitter"));
	this->particleColourLabel->setText(tr_particle("Birth Colour"));
	this->particleEndColourLabel->setText(tr_particle("Death Colour"));
	this->particleColourJitterLabel->setText(tr_particle("Colour Jitter"));
	this->particleTrailLengthLabel->setText(tr_particle("Trail Length"));
	this->particleGlowStrengthLabel->setText(tr_particle("Glow Strength"));
	this->particleRotationLabel->setText(tr_particle("Initial Rotation"));
	this->particleRotationJitterLabel->setText(tr_particle("Rotation Jitter"));
	this->particleSpinLabel->setText(tr_particle("Spin"));
	this->particleSpinJitterLabel->setText(tr_particle("Spin Jitter"));
	this->particleBurstEnabledCheckBox->setText(tr_particle("Looping Burst"));
	this->particleBurstCountLabel->setText(tr_particle("Burst Count"));
	this->particleBurstIntervalLabel->setText(tr_particle("Burst Interval"));
	this->particleMaxDistanceLabel->setText(tr_particle("View Distance"));
	this->particleBurstNowPushButton->setText(tr_particle("Burst Now"));
	this->particleClearParticlesPushButton->setText(tr_particle("Clear Particles"));
	this->particleDiagnosticsLabel->setText(tr_particle("Diagnostics"));
	this->particleWindXLabel->setText(tr_particle("Wind X"));
	this->particleWindYLabel->setText(tr_particle("Wind Y"));
	this->particleWindZLabel->setText(tr_particle("Wind Z"));
	this->particleVortexStrengthLabel->setText(tr_particle("Vortex"));
	this->particleAttractorStrengthLabel->setText(tr_particle("Attractor"));
	this->particleAttractorRadiusLabel->setText(tr_particle("Attractor Radius"));
	this->particleBlackHoleCheckBox->setText(tr_particle("Black hole gravity"));
	this->particleEventHorizonLabel->setText(tr_particle("Event Horizon"));
	this->particleRadialAccelLabel->setText(tr_particle("Radial Force"));
	this->particleLinearDampingLabel->setText(tr_particle("Damping"));
	this->particleBuoyancyLiftLabel->setText(tr_particle("Buoyancy Lift"));
	this->particleGravityScaleLabel->setText(tr_particle("Gravity Scale"));
	this->particleDragAreaLabel->setText(tr_particle("Drag Area"));
	this->particleMassLabel->setText(tr_particle("Particle Mass"));
	this->particleRestitutionLabel->setText(tr_particle("Bounce"));
	this->particleCollisionFrictionLabel->setText(tr_particle("Collision Friction"));
	this->particleCollideSurfacesCheckBox->setText(tr_particle("Collide with surfaces"));
	this->particleDieOnSurfaceCheckBox->setText(tr_particle("Die on first surface hit"));

	this->particleRateSpinBox->setSuffix(tr_particle(" /s"));
	this->particleRadiusSpinBox->setSuffix(tr_particle(" m"));
	this->particleSpeedSpinBox->setSuffix(tr_particle(" m/s"));
	this->particleSpreadSpinBox->setSuffix(tr_particle(" deg"));
	this->particleTurbulenceSpinBox->setSuffix(tr_particle(" m/s^2"));
	this->particleLifetimeSpinBox->setSuffix(tr_particle(" s"));
	this->particleStartWidthSpinBox->setSuffix(tr_particle(" m"));
	this->particleEndWidthSpinBox->setSuffix(tr_particle(" m"));
	this->particleTrailLengthSpinBox->setSuffix(tr_particle(" m"));
	this->particleAudioActivationDistanceSpinBox->setSuffix(tr_particle(" m"));
	this->particleAudioMinDistanceSpinBox->setSuffix(tr_particle(" m"));
	this->particleAudioMaxDistanceSpinBox->setSuffix(tr_particle(" m"));
	this->particleAudioFadeInSpinBox->setSuffix(tr_particle(" s"));
	this->particleAudioFadeOutSpinBox->setSuffix(tr_particle(" s"));
	this->particleRotationSpinBox->setSuffix(tr_particle(" deg"));
	this->particleRotationJitterSpinBox->setSuffix(tr_particle(" deg"));
	this->particleSpinSpinBox->setSuffix(tr_particle(" deg/s"));
	this->particleSpinJitterSpinBox->setSuffix(tr_particle(" deg/s"));
	this->particleBurstIntervalSpinBox->setSuffix(tr_particle(" s"));
	this->particleMaxDistanceSpinBox->setSuffix(tr_particle(" m"));
	this->particleWindXSpinBox->setSuffix(tr_particle(" m/s^2"));
	this->particleWindYSpinBox->setSuffix(tr_particle(" m/s^2"));
	this->particleWindZSpinBox->setSuffix(tr_particle(" m/s^2"));
	this->particleVortexStrengthSpinBox->setSuffix(tr_particle(" m/s^2"));
	this->particleAttractorStrengthSpinBox->setSuffix(tr_particle(" m/s^2"));
	this->particleAttractorRadiusSpinBox->setSuffix(tr_particle(" m"));
	this->particleEventHorizonSpinBox->setSuffix(tr_particle(" m"));
	this->particleRadialAccelSpinBox->setSuffix(tr_particle(" m/s^2"));
	this->particleLinearDampingSpinBox->setSuffix(tr_particle(" /s"));
	this->particleDragAreaSpinBox->setSuffix(tr_particle(" m^2"));
	this->particleMassSpinBox->setSuffix(tr_particle(" kg"));

	auto set_tooltip = [&tr_particle](QWidget* target_widget, const char* source_text)
	{
		if(target_widget)
			target_widget->setToolTip(tr_particle(source_text));
	};
	auto set_pair_tooltip = [&set_tooltip](QWidget* label_widget, QWidget* control_widget, const char* source_text)
	{
		set_tooltip(label_widget, source_text);
		set_tooltip(control_widget, source_text);
	};

	set_tooltip(this->particleGroupBox, "Professional controls for particle emission, sprite visuals, motion, physics, collisions and live preview.");
	set_tooltip(this->particleEnabledCheckBox, "Enable or disable this particle emitter without deleting its settings.");
	set_tooltip(this->particleHelpPushButton, "Open particle editor help with practical recipes.");
	set_pair_tooltip(this->particlePresetLabel, this->particlePresetComboBox, "Apply a ready-made particle setup or a user preset saved in this editor.");
	set_tooltip(this->particleSavePresetPushButton, "Save the current particle settings as a reusable local preset.");
	set_tooltip(this->particleDeletePresetPushButton, "Delete the selected user preset. Built-in presets cannot be removed.");
	set_pair_tooltip(this->particleKindLabel, this->particleKindComboBox, "Choose the particle sprite family and default visual style.");
	set_pair_tooltip(this->particleDirectionLabel, this->particleDirectionComboBox, "Choose the emission direction. Custom direction can be adjusted with the blue 3D handle.");
	set_pair_tooltip(this->particleShapeLabel, this->particleShapeComboBox, "Choose the volume or surface where new particles are spawned.");
	set_pair_tooltip(this->particleRenderModeLabel, this->particleRenderModeComboBox, "Choose soft alpha blending or additive glow rendering.");
	set_pair_tooltip(this->particleSpriteLibraryLabel, this->particleSpriteLibraryComboBox, "Pick a built-in procedural sprite texture.");
	set_pair_tooltip(this->particleSpritePathLabel, this->particleSpritePathLineEdit, "Use a custom sprite path or shared resource URL. Leave empty to use the particle type default.");
	set_tooltip(this->particleSpriteBrowsePushButton, "Select a local image to use as the particle sprite.");
	set_tooltip(this->particleSpriteClearPushButton, "Clear the custom sprite and return to the selected built-in/default sprite.");
	set_tooltip(this->particleAudioEnabledCheckBox, "Attach looping or one-shot sound to this particle emitter.");
	set_pair_tooltip(this->particleAudioURLLabel, this->particleAudioURLLineEdit, "Audio resource URL or local MP3/WAV file. Local files are uploaded when the object is saved.");
	set_tooltip(this->particleAudioBrowsePushButton, "Select a local audio file for this particle effect.");
	set_tooltip(this->particleAudioClearPushButton, "Remove sound from this particle effect.");
	set_tooltip(this->particleAudioLoopCheckBox, "Loop the sound while the emitter is active and the listener is inside the activation distance.");
	set_tooltip(this->particleAudioSpatialCheckBox, "Use 3D spatial audio positioned at the emitter. Turn off for non-spatial ambience.");
	set_pair_tooltip(this->particleAudioVolumeLabel, this->particleAudioVolumeSpinBox, "Base sound volume before distance attenuation.");
	set_pair_tooltip(this->particleAudioActivationDistanceLabel, this->particleAudioActivationDistanceSpinBox, "Distance from the player where the sound source is created and starts playing.");
	set_pair_tooltip(this->particleAudioMinDistanceLabel, this->particleAudioMinDistanceSpinBox, "Distance where spatial sound is still full volume before rolloff begins.");
	set_pair_tooltip(this->particleAudioMaxDistanceLabel, this->particleAudioMaxDistanceSpinBox, "Maximum audible radius used by the 3D distance model.");
	set_pair_tooltip(this->particleAudioFadeInLabel, this->particleAudioFadeInSpinBox, "Soft unmute time when entering the activation distance.");
	set_pair_tooltip(this->particleAudioFadeOutLabel, this->particleAudioFadeOutSpinBox, "Soft mute time when leaving the activation distance.");
	set_pair_tooltip(this->particleRateLabel, this->particleRateSpinBox, "Particles emitted per second while the emitter is active.");
	set_pair_tooltip(this->particleFrameCapLabel, this->particleFrameCapSpinBox, "Maximum particles that may spawn in one frame to avoid spikes.");
	set_pair_tooltip(this->particleMaxParticlesLabel, this->particleMaxParticlesSpinBox, "Maximum live particles kept by this emitter.");
	set_pair_tooltip(this->particleRadiusLabel, this->particleRadiusSpinBox, "Size of the emitter shape in metres. It can also be adjusted with the 3D radius handle.");
	set_pair_tooltip(this->particleSpeedLabel, this->particleSpeedSpinBox, "Initial particle speed in metres per second.");
	set_pair_tooltip(this->particleSpeedJitterLabel, this->particleSpeedJitterSpinBox, "Random variation applied to initial particle speed.");
	set_pair_tooltip(this->particleSpreadLabel, this->particleSpreadSpinBox, "Cone angle used to spread particles away from the emission direction.");
	set_pair_tooltip(this->particleTurbulenceLabel, this->particleTurbulenceSpinBox, "Random acceleration that gives particles noisy living motion.");
	set_pair_tooltip(this->particleLifetimeLabel, this->particleLifetimeSpinBox, "How long each particle lives before it fades out.");
	set_pair_tooltip(this->particleStartWidthLabel, this->particleStartWidthSpinBox, "Particle size at birth.");
	set_pair_tooltip(this->particleEndWidthLabel, this->particleEndWidthSpinBox, "Particle size at the end of its lifetime.");
	set_pair_tooltip(this->particleSizeCurveLabel, this->particleSizeCurveWidget, "Edit how size changes over particle lifetime.");
	set_pair_tooltip(this->particleSizeJitterLabel, this->particleSizeJitterSpinBox, "Random variation applied to particle size.");
	set_pair_tooltip(this->particleOpacityLabel, this->particleOpacitySpinBox, "Particle opacity at birth.");
	set_pair_tooltip(this->particleEndOpacityLabel, this->particleEndOpacitySpinBox, "Particle opacity at the end of its lifetime.");
	set_pair_tooltip(this->particleOpacityCurveLabel, this->particleOpacityCurveWidget, "Edit how opacity changes over particle lifetime.");
	set_pair_tooltip(this->particleOpacityJitterLabel, this->particleOpacityJitterSpinBox, "Random variation applied to particle opacity.");
	set_pair_tooltip(this->particleColourLabel, this->particleColourPushButton, "Particle tint at birth. Fire should start white-yellow, engines should start white-cyan.");
	set_pair_tooltip(this->particleEndColourLabel, this->particleEndColourPushButton, "Particle tint at death. Use red/grey for cooling fire, deep blue for ion trails, or transparent-looking dark colours for smoke.");
	set_pair_tooltip(this->particleColourJitterLabel, this->particleColourJitterSpinBox, "Random brightness variation applied to birth and death colours.");
	set_pair_tooltip(this->particleTrailLengthLabel, this->particleTrailLengthSpinBox, "Initial distance used to distribute particles along a visible trail, useful for comets, meteors, rain streaks and thrusters.");
	set_pair_tooltip(this->particleGlowStrengthLabel, this->particleGlowStrengthSpinBox, "Brightness multiplier for additive/glowing particles.");
	set_pair_tooltip(this->particleRotationLabel, this->particleRotationSpinBox, "Initial sprite rotation in degrees.");
	set_pair_tooltip(this->particleRotationJitterLabel, this->particleRotationJitterSpinBox, "Random variation applied to initial sprite rotation.");
	set_pair_tooltip(this->particleSpinLabel, this->particleSpinSpinBox, "Sprite rotation speed in degrees per second.");
	set_pair_tooltip(this->particleSpinJitterLabel, this->particleSpinJitterSpinBox, "Random variation applied to spin speed.");
	set_tooltip(this->particleBurstEnabledCheckBox, "Emit particles in repeating bursts instead of a continuous stream.");
	set_pair_tooltip(this->particleBurstCountLabel, this->particleBurstCountSpinBox, "Number of particles emitted by each burst.");
	set_pair_tooltip(this->particleBurstIntervalLabel, this->particleBurstIntervalSpinBox, "Time between automatic bursts.");
	set_pair_tooltip(this->particleMaxDistanceLabel, this->particleMaxDistanceSpinBox, "Maximum camera distance where the emitter keeps spawning particles.");
	set_tooltip(this->particleBurstNowPushButton, "Trigger one burst immediately.");
	set_tooltip(this->particleClearParticlesPushButton, "Remove all currently live particles from this emitter.");
	set_pair_tooltip(this->particleDiagnosticsLabel, this->particleDiagnosticsValueLabel, "Live performance and particle-count diagnostics for this emitter.");
	set_pair_tooltip(this->particleWindXLabel, this->particleWindXSpinBox, "Constant wind acceleration on the world X axis.");
	set_pair_tooltip(this->particleWindYLabel, this->particleWindYSpinBox, "Constant wind acceleration on the world Y axis.");
	set_pair_tooltip(this->particleWindZLabel, this->particleWindZSpinBox, "Constant wind acceleration on the world Z axis.");
	set_pair_tooltip(this->particleVortexStrengthLabel, this->particleVortexStrengthSpinBox, "Tangential force that makes particles orbit around the emitter.");
	set_pair_tooltip(this->particleAttractorStrengthLabel, this->particleAttractorStrengthSpinBox, "Force pulling particles toward or pushing them away from the emitter.");
	set_pair_tooltip(this->particleAttractorRadiusLabel, this->particleAttractorRadiusSpinBox, "Radius of the attractor/vortex field shown by the 3D handle.");
	set_tooltip(this->particleBlackHoleCheckBox, "Enable black-hole style gravity with a visible event horizon.");
	set_pair_tooltip(this->particleEventHorizonLabel, this->particleEventHorizonSpinBox, "Particles disappear inside this black-hole radius.");
	set_pair_tooltip(this->particleRadialAccelLabel, this->particleRadialAccelSpinBox, "Extra radial acceleration from the emitter centre; negative values pull inward.");
	set_pair_tooltip(this->particleLinearDampingLabel, this->particleLinearDampingSpinBox, "Velocity damping that slows particles over time.");
	set_pair_tooltip(this->particleBuoyancyLiftLabel, this->particleBuoyancyLiftSpinBox, "Vertical lift added to particles, useful for smoke, steam and bubbles.");
	set_pair_tooltip(this->particleGravityScaleLabel, this->particleGravityScaleSpinBox, "Multiplier for world gravity; negative values make particles rise.");
	set_pair_tooltip(this->particleDragAreaLabel, this->particleDragAreaSpinBox, "Aerodynamic drag area used by the physics integration.");
	set_pair_tooltip(this->particleMassLabel, this->particleMassSpinBox, "Particle mass used with gravity, drag and collision response.");
	set_pair_tooltip(this->particleRestitutionLabel, this->particleRestitutionSpinBox, "How much velocity is kept after a surface collision.");
	set_pair_tooltip(this->particleCollisionFrictionLabel, this->particleCollisionFrictionSpinBox, "Tangential velocity lost when particles collide with a surface.");
	set_tooltip(this->particleCollideSurfacesCheckBox, "Let particles collide with world surfaces.");
	set_tooltip(this->particleDieOnSurfaceCheckBox, "Destroy particles on their first surface hit instead of bouncing.");
}


void ObjectEditor::loadCustomParticlePresets()
{
	if(!this->settings || !this->particlePresetComboBox)
		return;

	{
		SignalBlocker blocker(this->particlePresetComboBox);
		for(int i=this->particlePresetComboBox->count() - 1; i >= 0; --i)
		{
			if(particlePresetDataIsUserPreset(this->particlePresetComboBox->itemData(i).toString()))
				this->particlePresetComboBox->removeItem(i);
		}

		QStringList names = this->settings->value(particleCustomPresetNamesKey()).toStringList();
		names.removeDuplicates();
		names.sort(Qt::CaseInsensitive);
		for(int i=0; i<names.size(); ++i)
		{
			const QString name = names[i].trimmed();
			if(!name.isEmpty())
				this->particlePresetComboBox->addItem(name, QStringLiteral("user:") + name);
		}
	}
}


void ObjectEditor::setParticleControlsFromContent(const std::string& content)
{
	const ParticleEmitterSettings emitter_settings = ParticleEmitterSettings::fromContent(content);
	setParticleControlsFromSettings(emitter_settings);
}


void ObjectEditor::setParticleControlsFromSettings(const ParticleEmitterSettings& emitter_settings)
{
	SignalBlocker::setChecked(this->particleEnabledCheckBox, emitter_settings.enabled);
	{
		SignalBlocker blocker(this->particlePresetComboBox);
		const int index = this->particlePresetComboBox->findData(QtUtils::toQString(emitter_settings.preset_name));
		this->particlePresetComboBox->setCurrentIndex(index >= 0 ? index : 0);
	}
	{
		SignalBlocker blocker(this->particleKindComboBox);
		const QString item_data = particleKindData(emitter_settings.kind);
		const int index = this->particleKindComboBox->findData(item_data);
		this->particleKindComboBox->setCurrentIndex(index >= 0 ? index : 0);
	}
	{
		SignalBlocker blocker(this->particleDirectionComboBox);
		QString item_data = emitter_settings.direction == ParticleEmitterSettings::Direction_Forward ? QStringLiteral("forward") : QStringLiteral("up");
		if(emitter_settings.direction == ParticleEmitterSettings::Direction_Down)
			item_data = QStringLiteral("down");
		else if(emitter_settings.direction == ParticleEmitterSettings::Direction_Random)
			item_data = QStringLiteral("random");
		else if(emitter_settings.direction == ParticleEmitterSettings::Direction_Custom)
			item_data = QStringLiteral("custom");
		const int index = this->particleDirectionComboBox->findData(item_data);
		this->particleDirectionComboBox->setCurrentIndex(index >= 0 ? index : 0);
	}
	{
		SignalBlocker blocker(this->particleShapeComboBox);
		const QString item_data = particleShapeData(emitter_settings.shape);
		const int index = this->particleShapeComboBox->findData(item_data);
		this->particleShapeComboBox->setCurrentIndex(index >= 0 ? index : 1);
	}
	{
		SignalBlocker blocker(this->particleRenderModeComboBox);
		const QString item_data = emitter_settings.render_mode == ParticleEmitterSettings::RenderMode_AdditiveGlow ? QStringLiteral("additive_glow") : QStringLiteral("soft");
		const int index = this->particleRenderModeComboBox->findData(item_data);
		this->particleRenderModeComboBox->setCurrentIndex(index >= 0 ? index : 0);
	}
	{
		QSignalBlocker blocker(this->particleSpritePathLineEdit);
		this->particleSpritePathLineEdit->setText(QtUtils::toQString(emitter_settings.sprite_path));
	}
	{
		SignalBlocker blocker(this->particleSpriteLibraryComboBox);
		const int index = this->particleSpriteLibraryComboBox->findData(QtUtils::toQString(emitter_settings.sprite_path));
		this->particleSpriteLibraryComboBox->setCurrentIndex(index >= 0 ? index : 0);
	}
	SignalBlocker::setChecked(this->particleAudioEnabledCheckBox, emitter_settings.audio_enabled);
	{
		QSignalBlocker blocker(this->particleAudioURLLineEdit);
		this->particleAudioURLLineEdit->setText(QtUtils::toQString(emitter_settings.audio_url));
	}
	SignalBlocker::setChecked(this->particleAudioLoopCheckBox, emitter_settings.audio_loop);
	SignalBlocker::setChecked(this->particleAudioSpatialCheckBox, emitter_settings.audio_spatial);
	SignalBlocker::setValue(this->particleAudioVolumeSpinBox, emitter_settings.audio_volume);
	SignalBlocker::setValue(this->particleAudioActivationDistanceSpinBox, emitter_settings.audio_activation_distance);
	SignalBlocker::setValue(this->particleAudioMinDistanceSpinBox, emitter_settings.audio_min_distance);
	SignalBlocker::setValue(this->particleAudioMaxDistanceSpinBox, emitter_settings.audio_max_distance);
	SignalBlocker::setValue(this->particleAudioFadeInSpinBox, emitter_settings.audio_fade_in_s);
	SignalBlocker::setValue(this->particleAudioFadeOutSpinBox, emitter_settings.audio_fade_out_s);
	SignalBlocker::setValue(this->particleRateSpinBox, emitter_settings.rate_per_sec);
	SignalBlocker::setValue(this->particleFrameCapSpinBox, emitter_settings.max_spawn_per_frame);
	SignalBlocker::setValue(this->particleMaxParticlesSpinBox, emitter_settings.max_particles);
	SignalBlocker::setValue(this->particleRadiusSpinBox, emitter_settings.emitter_radius);
	SignalBlocker::setValue(this->particleSpeedSpinBox, emitter_settings.speed);
	SignalBlocker::setValue(this->particleSpeedJitterSpinBox, emitter_settings.speed_jitter);
	SignalBlocker::setValue(this->particleSpreadSpinBox, emitter_settings.spread_deg);
	SignalBlocker::setValue(this->particleTurbulenceSpinBox, emitter_settings.turbulence_strength);
	SignalBlocker::setValue(this->particleLifetimeSpinBox, emitter_settings.lifetime_s);
	SignalBlocker::setValue(this->particleStartWidthSpinBox, emitter_settings.start_width);
	SignalBlocker::setValue(this->particleEndWidthSpinBox, emitter_settings.end_width);
	this->particleSizeCurveWidget->setCurve(emitter_settings.size_curve, emitter_settings.size_curve_mid);
	SignalBlocker::setValue(this->particleSizeJitterSpinBox, emitter_settings.size_jitter);
	SignalBlocker::setValue(this->particleOpacitySpinBox, emitter_settings.opacity);
	SignalBlocker::setValue(this->particleEndOpacitySpinBox, emitter_settings.end_opacity);
	this->particleOpacityCurveWidget->setCurve(emitter_settings.opacity_curve, emitter_settings.opacity_curve_mid);
	SignalBlocker::setValue(this->particleOpacityJitterSpinBox, emitter_settings.opacity_jitter);
	SignalBlocker::setValue(this->particleColourJitterSpinBox, emitter_settings.colour_jitter);
	SignalBlocker::setValue(this->particleTrailLengthSpinBox, emitter_settings.trail_length);
	SignalBlocker::setValue(this->particleGlowStrengthSpinBox, emitter_settings.glow_strength);
	SignalBlocker::setValue(this->particleRotationSpinBox, emitter_settings.rotation_deg);
	SignalBlocker::setValue(this->particleRotationJitterSpinBox, emitter_settings.rotation_jitter_deg);
	SignalBlocker::setValue(this->particleSpinSpinBox, emitter_settings.spin_deg_per_sec);
	SignalBlocker::setValue(this->particleSpinJitterSpinBox, emitter_settings.spin_jitter_deg_per_sec);
	SignalBlocker::setChecked(this->particleBurstEnabledCheckBox, emitter_settings.burst_enabled);
	SignalBlocker::setValue(this->particleBurstCountSpinBox, emitter_settings.burst_count);
	SignalBlocker::setValue(this->particleBurstIntervalSpinBox, emitter_settings.burst_interval_s);
	SignalBlocker::setValue(this->particleMaxDistanceSpinBox, emitter_settings.max_spawn_distance);
	SignalBlocker::setValue(this->particleWindXSpinBox, emitter_settings.wind_accel_x);
	SignalBlocker::setValue(this->particleWindYSpinBox, emitter_settings.wind_accel_y);
	SignalBlocker::setValue(this->particleWindZSpinBox, emitter_settings.wind_accel_z);
	SignalBlocker::setValue(this->particleVortexStrengthSpinBox, emitter_settings.vortex_strength);
	SignalBlocker::setValue(this->particleAttractorStrengthSpinBox, emitter_settings.attractor_strength);
	SignalBlocker::setValue(this->particleAttractorRadiusSpinBox, emitter_settings.attractor_radius);
	SignalBlocker::setChecked(this->particleBlackHoleCheckBox, emitter_settings.black_hole_mode);
	SignalBlocker::setValue(this->particleEventHorizonSpinBox, emitter_settings.event_horizon_radius);
	SignalBlocker::setValue(this->particleRadialAccelSpinBox, emitter_settings.radial_accel);
	SignalBlocker::setValue(this->particleLinearDampingSpinBox, emitter_settings.linear_damping);
	SignalBlocker::setValue(this->particleBuoyancyLiftSpinBox, emitter_settings.buoyancy_lift);
	SignalBlocker::setValue(this->particleGravityScaleSpinBox, emitter_settings.gravity_scale);
	SignalBlocker::setValue(this->particleDragAreaSpinBox, emitter_settings.drag_area);
	SignalBlocker::setValue(this->particleMassSpinBox, emitter_settings.mass);
	SignalBlocker::setValue(this->particleRestitutionSpinBox, emitter_settings.restitution);
	SignalBlocker::setValue(this->particleCollisionFrictionSpinBox, emitter_settings.collision_friction);
	SignalBlocker::setChecked(this->particleCollideSurfacesCheckBox, emitter_settings.collide_surfaces);
	SignalBlocker::setChecked(this->particleDieOnSurfaceCheckBox, emitter_settings.die_when_hit_surface);

	this->particle_col = emitter_settings.start_colour;
	this->particle_end_col = emitter_settings.end_colour;
	updateParticleColourButton();
	updateParticlePreviewThumbnail();
}


ParticleEmitterSettings ObjectEditor::particleControlsToSettings() const
{
	const std::string current_content = QtUtils::toStdString(this->contentTextEdit->toPlainText());
	ParticleEmitterSettings emitter_settings = ParticleEmitterSettings::isParticleEmitterContent(current_content) ? ParticleEmitterSettings::fromContent(current_content) : ParticleEmitterSettings::defaultSmoke();

	emitter_settings.preset_name = QtUtils::toStdString(this->particlePresetComboBox->currentData().toString());
	emitter_settings.enabled = this->particleEnabledCheckBox->isChecked();
	const QString kind = this->particleKindComboBox->currentData().toString();
	emitter_settings.kind = particleKindFromData(kind);
	const QString direction = this->particleDirectionComboBox->currentData().toString();
	if(direction == QStringLiteral("forward"))
		emitter_settings.direction = ParticleEmitterSettings::Direction_Forward;
	else if(direction == QStringLiteral("down"))
		emitter_settings.direction = ParticleEmitterSettings::Direction_Down;
	else if(direction == QStringLiteral("random"))
		emitter_settings.direction = ParticleEmitterSettings::Direction_Random;
	else if(direction == QStringLiteral("custom"))
		emitter_settings.direction = ParticleEmitterSettings::Direction_Custom;
	else
		emitter_settings.direction = ParticleEmitterSettings::Direction_Up;
	const QString shape = this->particleShapeComboBox->currentData().toString();
	emitter_settings.shape = particleShapeFromData(shape);
	const QString render_mode = this->particleRenderModeComboBox->currentData().toString();
	emitter_settings.render_mode = (render_mode == QStringLiteral("additive_glow")) ? ParticleEmitterSettings::RenderMode_AdditiveGlow : ParticleEmitterSettings::RenderMode_Soft;
	emitter_settings.sprite_path = QtUtils::toStdString(this->particleSpritePathLineEdit->text().trimmed());
	emitter_settings.audio_enabled = this->particleAudioEnabledCheckBox->isChecked();
	emitter_settings.audio_url = QtUtils::toStdString(this->particleAudioURLLineEdit->text().trimmed());
	emitter_settings.audio_loop = this->particleAudioLoopCheckBox->isChecked();
	emitter_settings.audio_spatial = this->particleAudioSpatialCheckBox->isChecked();
	emitter_settings.audio_volume = (float)this->particleAudioVolumeSpinBox->value();
	emitter_settings.audio_activation_distance = (float)this->particleAudioActivationDistanceSpinBox->value();
	emitter_settings.audio_min_distance = (float)this->particleAudioMinDistanceSpinBox->value();
	emitter_settings.audio_max_distance = (float)this->particleAudioMaxDistanceSpinBox->value();
	emitter_settings.audio_fade_in_s = (float)this->particleAudioFadeInSpinBox->value();
	emitter_settings.audio_fade_out_s = (float)this->particleAudioFadeOutSpinBox->value();
	emitter_settings.rate_per_sec = (float)this->particleRateSpinBox->value();
	emitter_settings.max_spawn_per_frame = (int)std::round(this->particleFrameCapSpinBox->value());
	emitter_settings.max_particles = (int)std::round(this->particleMaxParticlesSpinBox->value());
	emitter_settings.emitter_radius = (float)this->particleRadiusSpinBox->value();
	emitter_settings.speed = (float)this->particleSpeedSpinBox->value();
	emitter_settings.speed_jitter = (float)this->particleSpeedJitterSpinBox->value();
	emitter_settings.spread_deg = (float)this->particleSpreadSpinBox->value();
	emitter_settings.turbulence_strength = (float)this->particleTurbulenceSpinBox->value();
	emitter_settings.lifetime_s = (float)this->particleLifetimeSpinBox->value();
	emitter_settings.start_width = (float)this->particleStartWidthSpinBox->value();
	emitter_settings.end_width = (float)this->particleEndWidthSpinBox->value();
	emitter_settings.size_curve = this->particleSizeCurveWidget->currentCurve();
	emitter_settings.size_curve_mid = this->particleSizeCurveWidget->currentCustomMid();
	emitter_settings.size_jitter = (float)this->particleSizeJitterSpinBox->value();
	emitter_settings.opacity = (float)this->particleOpacitySpinBox->value();
	emitter_settings.end_opacity = (float)this->particleEndOpacitySpinBox->value();
	emitter_settings.opacity_curve = this->particleOpacityCurveWidget->currentCurve();
	emitter_settings.opacity_curve_mid = this->particleOpacityCurveWidget->currentCustomMid();
	emitter_settings.opacity_jitter = (float)this->particleOpacityJitterSpinBox->value();
	emitter_settings.colour = this->particle_col;
	emitter_settings.start_colour = this->particle_col;
	emitter_settings.end_colour = this->particle_end_col;
	emitter_settings.colour_jitter = (float)this->particleColourJitterSpinBox->value();
	emitter_settings.trail_length = (float)this->particleTrailLengthSpinBox->value();
	emitter_settings.glow_strength = (float)this->particleGlowStrengthSpinBox->value();
	emitter_settings.rotation_deg = (float)this->particleRotationSpinBox->value();
	emitter_settings.rotation_jitter_deg = (float)this->particleRotationJitterSpinBox->value();
	emitter_settings.spin_deg_per_sec = (float)this->particleSpinSpinBox->value();
	emitter_settings.spin_jitter_deg_per_sec = (float)this->particleSpinJitterSpinBox->value();
	emitter_settings.burst_enabled = this->particleBurstEnabledCheckBox->isChecked();
	emitter_settings.burst_count = (int)std::round(this->particleBurstCountSpinBox->value());
	emitter_settings.burst_interval_s = (float)this->particleBurstIntervalSpinBox->value();
	emitter_settings.max_spawn_distance = (float)this->particleMaxDistanceSpinBox->value();
	emitter_settings.wind_accel_x = (float)this->particleWindXSpinBox->value();
	emitter_settings.wind_accel_y = (float)this->particleWindYSpinBox->value();
	emitter_settings.wind_accel_z = (float)this->particleWindZSpinBox->value();
	emitter_settings.vortex_strength = (float)this->particleVortexStrengthSpinBox->value();
	emitter_settings.attractor_strength = (float)this->particleAttractorStrengthSpinBox->value();
	emitter_settings.attractor_radius = (float)this->particleAttractorRadiusSpinBox->value();
	emitter_settings.black_hole_mode = this->particleBlackHoleCheckBox->isChecked();
	emitter_settings.event_horizon_radius = (float)this->particleEventHorizonSpinBox->value();
	emitter_settings.radial_accel = (float)this->particleRadialAccelSpinBox->value();
	emitter_settings.linear_damping = (float)this->particleLinearDampingSpinBox->value();
	emitter_settings.buoyancy_lift = (float)this->particleBuoyancyLiftSpinBox->value();
	emitter_settings.gravity_scale = (float)this->particleGravityScaleSpinBox->value();
	emitter_settings.drag_area = (float)this->particleDragAreaSpinBox->value();
	emitter_settings.mass = (float)this->particleMassSpinBox->value();
	emitter_settings.restitution = (float)this->particleRestitutionSpinBox->value();
	emitter_settings.collision_friction = (float)this->particleCollisionFrictionSpinBox->value();
	emitter_settings.collide_surfaces = this->particleCollideSurfacesCheckBox->isChecked();
	emitter_settings.die_when_hit_surface = this->particleDieOnSurfaceCheckBox->isChecked();

	return emitter_settings;
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
	case WorldObject::ObjectType_Generic: ob_type = ParticleEmitterSettings::isParticleEmitterContent(ob.content) ? QCoreApplication::translate("ObjectEditor", "Particle Emitter") : QCoreApplication::translate("ObjectEditor", "Generic object"); break;
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
	this->editing_object_type = ob.object_type;
	this->editing_audio_player_webview = ob.isAudioPlayerWebView();
	this->editing_particle_emitter = (ob.object_type == WorldObject::ObjectType_Generic) && ParticleEmitterSettings::isParticleEmitterContent(ob.content);
	this->editing_gaussian_splat = (ob.object_type == WorldObject::ObjectType_Generic) && GaussianSplatAsset::hasSupportedExtension(ob.model_url);
	const RuntimeTranslation::UILanguage ui_language = currentUILanguageForObjectEditor(this->settings);

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
	if(ob.isPortal())
		WorldObject::ensurePortalMaterialsPresent(this->cloned_materials);

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
	if(this->editing_particle_emitter)
		setParticleControlsFromContent(ob.content);
	if(this->editing_gaussian_splat)
		setGaussianSplatControlsFromContent(ob.content);
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
	SignalBlocker::setValue(this->audioSoundRadiusSpinBox, myClamp((double)ob.audio_player_sound_radius,
		(double)WorldObject::MIN_AUDIO_PLAYER_SOUND_RADIUS, (double)WorldObject::MAX_AUDIO_PLAYER_SOUND_RADIUS));
	SignalBlocker::setChecked(this->audioDirectionalityEnabledCheckBox, ob.audio_player_directionality_enabled);
	SignalBlocker::setValue(this->audioDirectivityAlphaSpinBox, myClamp((double)ob.audio_player_directivity_alpha,
		(double)WorldObject::MIN_AUDIO_PLAYER_DIRECTIVITY_ALPHA, (double)WorldObject::MAX_AUDIO_PLAYER_DIRECTIVITY_ALPHA));
	SignalBlocker::setValue(this->audioDirectivityOrderSpinBox, myClamp((double)ob.audio_player_directivity_order,
		(double)WorldObject::MIN_AUDIO_PLAYER_DIRECTIVITY_ORDER, (double)WorldObject::MAX_AUDIO_PLAYER_DIRECTIVITY_ORDER));
	SignalBlocker::setValue(this->audioSpreadDegreesSpinBox, myClamp((double)ob.audio_player_spread_degrees,
		(double)WorldObject::MIN_AUDIO_PLAYER_SPREAD_DEGREES, (double)WorldObject::MAX_AUDIO_PLAYER_SPREAD_DEGREES));
	SignalBlocker::setChecked(this->audioScheduleEnabledCheckBox, ob.audio_player_schedule_enabled);
	SignalBlocker::setValue(this->audioScheduleStartHourSpinBox, myClamp((double)ob.audio_player_schedule_start_hour,
		(double)WorldObject::MIN_AUDIO_PLAYER_SCHEDULE_HOUR, (double)WorldObject::MAX_AUDIO_PLAYER_SCHEDULE_HOUR));
	SignalBlocker::setValue(this->audioScheduleEndHourSpinBox, myClamp((double)ob.audio_player_schedule_end_hour,
		(double)WorldObject::MIN_AUDIO_PLAYER_SCHEDULE_HOUR, (double)WorldObject::MAX_AUDIO_PLAYER_SCHEDULE_HOUR));

	this->videoURLFileSelectWidget->setFilename(QtUtils::toQString((!ob.materials.empty()) ? ob.materials[0]->emission_texture_url : ""));

	SignalBlocker::setValue(videoVolumeDoubleSpinBox, ob.audio_volume);
	
	lightmapURLLabel->setText(QtUtils::toQString(ob.lightmap_url));

	WorldMaterialRef selected_mat;
	if(this->selected_mat_index >= 0 && this->selected_mat_index < (int)this->cloned_materials.size())
		selected_mat = this->cloned_materials[this->selected_mat_index];
	else
		selected_mat = new WorldMaterial();

	this->cameraGroupBox->hide();
	this->cameraScreenGroupBox->hide();
	
	if(this->editing_gaussian_splat)
	{
		this->materialsGroupBox->hide();
		this->lightmapGroupBox->hide();
		this->modelLabel->show();
		this->modelFileSelectWidget->show();
		this->spotlightGroupBox->hide();
		this->seatGroupBox->hide();
		this->audioGroupBox->hide();
		this->physicsSettingsGroupBox->show();
		this->videoGroupBox->hide();
	}
	else if(this->editing_particle_emitter)
	{
		this->materialsGroupBox->show();
		this->lightmapGroupBox->hide();
		this->modelLabel->show();
		this->modelFileSelectWidget->show();
		this->spotlightGroupBox->hide();
		this->seatGroupBox->hide();
		this->audioGroupBox->hide();
		this->physicsSettingsGroupBox->show();
		this->videoGroupBox->hide();
	}
	else if(ob.object_type == WorldObject::ObjectType_Hypercard)
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
		this->materialsGroupBox->show();
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

	this->particleGroupBox->setVisible(this->editing_particle_emitter);
	this->gaussianSplatGroupBox->setVisible(this->editing_gaussian_splat);
	this->portalGroupBox->setVisible(ob.object_type == WorldObject::ObjectType_Portal);

	if(ob.object_type != WorldObject::ObjectType_Hypercard)
	{
		this->matEditor->setFromMaterial(*selected_mat);

		// Set materials combobox
		SignalBlocker blocker(this->materialComboBox);
		this->materialComboBox->clear();
		for(size_t i=0; i<this->cloned_materials.size(); ++i)
		{
			if(this->editing_audio_player_webview && i == 0)
				this->materialComboBox->addItem(QCoreApplication::translate("ObjectEditor", "Material 0 (Player Screen)"), (int)i);
			else if(this->editing_audio_player_webview && i == 1)
				this->materialComboBox->addItem(QCoreApplication::translate("ObjectEditor", "Material 1 (Player Body)"), (int)i);
			else if(ob.isPortal() && i == WorldObject::PORTAL_INNER_RIM_MATERIAL_INDEX)
				this->materialComboBox->addItem(translateObjectEditorRuntimeText(ui_language, "Material 0 (Inner Rim)"), (int)i);
			else if(ob.isPortal() && i == WorldObject::PORTAL_ARCH_MATERIAL_INDEX)
				this->materialComboBox->addItem(translateObjectEditorRuntimeText(ui_language, "Material 1 (Arch Body)"), (int)i);
			else if(ob.isPortal() && i == WorldObject::PORTAL_FRAME_MATERIAL_INDEX)
				this->materialComboBox->addItem(translateObjectEditorRuntimeText(ui_language, "Material 2 (Outer Edge)"), (int)i);
			else if(ob.isPortal() && i == WorldObject::PORTAL_EFFECT_MATERIAL_INDEX)
				this->materialComboBox->addItem(translateObjectEditorRuntimeText(ui_language, "Material 3 (Portal Effect)"), (int)i);
			else if(ob.isPortal() && i == WorldObject::PORTAL_THRESHOLD_MATERIAL_INDEX)
				this->materialComboBox->addItem(translateObjectEditorRuntimeText(ui_language, "Material 4 (Threshold)"), (int)i);
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
	const bool is_portal = ob.isPortal();
	const bool is_particle_emitter = this->editing_particle_emitter;
	const bool is_gaussian_splat = this->editing_gaussian_splat;
	auto tr_object_editor = [ui_language](const char* source_text)
	{
		return translateObjectEditorRuntimeText(ui_language, source_text);
	};
	this->audioGroupBox->setTitle(is_audio_player ? tr_object_editor("Audio Player") : tr_object_editor("Audio"));
	this->label_9->setVisible(!is_audio_player);
	this->widget_5->setVisible(!is_audio_player);
	this->label_12->setVisible(!is_audio_player && !is_particle_emitter && !is_gaussian_splat);
	this->contentTextEdit->setVisible(!is_audio_player && !is_particle_emitter && !is_gaussian_splat);
	this->fontLabel->setVisible(!is_audio_player && !is_portal && !is_particle_emitter && !is_gaussian_splat);
	this->fontComboBox->setVisible(!is_audio_player && !is_portal && !is_particle_emitter && !is_gaussian_splat);
	this->label_14->setVisible(!is_audio_player);
	this->audioFileWidget->setVisible(!is_audio_player);
	this->targetURLLabel->setVisible(!is_audio_player && !is_particle_emitter && !is_gaussian_splat);
	this->targetURLLineEdit->setVisible(!is_audio_player && !is_particle_emitter && !is_gaussian_splat);
	this->visitURLLabel->setVisible(!is_audio_player && !is_portal && !is_particle_emitter && !is_gaussian_splat && !ob.target_url.empty());
	this->audioShuffleCheckBox->setVisible(is_audio_player);
	this->audioActivationDistanceLabel->setVisible(is_audio_player);
	this->audioActivationDistanceSpinBox->setVisible(is_audio_player);
	this->audioSoundRadiusLabel->setVisible(is_audio_player);
	this->audioSoundRadiusSpinBox->setVisible(is_audio_player);
	this->audioDirectionalityEnabledCheckBox->setVisible(is_audio_player);
	this->audioDirectivityAlphaLabel->setVisible(is_audio_player);
	this->audioDirectivityAlphaSpinBox->setVisible(is_audio_player);
	this->audioDirectivityOrderLabel->setVisible(is_audio_player);
	this->audioDirectivityOrderSpinBox->setVisible(is_audio_player);
	this->audioSpreadDegreesLabel->setVisible(is_audio_player);
	this->audioSpreadDegreesSpinBox->setVisible(is_audio_player);
	this->audioScheduleEnabledCheckBox->setVisible(is_audio_player);
	this->audioScheduleStartHourLabel->setVisible(is_audio_player);
	this->audioScheduleStartHourSpinBox->setVisible(is_audio_player);
	this->audioScheduleEndHourLabel->setVisible(is_audio_player);
	this->audioScheduleEndHourSpinBox->setVisible(is_audio_player);
	this->audioPlaylistGroupBox->setVisible(is_audio_player);
	const bool directionality_controls_enabled = is_audio_player && this->audioDirectionalityEnabledCheckBox->isChecked();
	this->audioDirectivityAlphaSpinBox->setEnabled(directionality_controls_enabled);
	this->audioDirectivityOrderSpinBox->setEnabled(directionality_controls_enabled);
	this->audioSpreadDegreesSpinBox->setEnabled(directionality_controls_enabled);
	const bool schedule_controls_enabled = is_audio_player && this->audioScheduleEnabledCheckBox->isChecked();
	this->audioScheduleStartHourSpinBox->setEnabled(schedule_controls_enabled);
	this->audioScheduleEndHourSpinBox->setEnabled(schedule_controls_enabled);
	this->label_12->setText(tr_object_editor("Content"));
	this->contentTextEdit->setPlaceholderText(QString());
	this->targetURLLabel->setText(tr_object_editor(is_portal ? "Portal Target URL" : "Target URL"));
	this->targetURLLineEdit->setPlaceholderText(is_portal ? QStringLiteral("sub://vr.metasiberia.com/World?x=0&y=0&z=0&heading=0") : QString());
	this->targetURLLineEdit->setToolTip(is_portal ? tr_object_editor("Walk through the portal to travel to this sub:// destination.") : QString());
	this->newMaterialPushButton->setVisible(!is_portal);

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


void ObjectEditor::setContentForSpecialisedEditor(const std::string& content)
{
	SignalBlocker blocker(this->contentTextEdit);
	this->contentTextEdit->setPlainText(QtUtils::toQString(content));
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
	const bool is_particle_emitter = this->editing_particle_emitter || ((ob_out.object_type == WorldObject::ObjectType_Generic) && ParticleEmitterSettings::isParticleEmitterContent(ob_out.content));

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

	const bool is_gaussian_splat = this->editing_gaussian_splat || ((ob_out.object_type == WorldObject::ObjectType_Generic) && GaussianSplatAsset::hasSupportedExtension(ob_out.model_url));

	const std::string new_script =  QtUtils::toIndString(this->scriptTextEdit->toPlainText());
	if(ob_out.script != new_script)
		ob_out.changed_flags |= WorldObject::SCRIPT_CHANGED;
	ob_out.script = new_script;
	checkStringSize(ob_out.script, WorldObject::MAX_SCRIPT_SIZE);

	const std::string new_content = is_gaussian_splat ? GaussianSplatRenderSettings::serialiseToContent(gaussianSplatControlsToSettings()) :
		(is_particle_emitter ? ParticleEmitterSettings::serialiseToContent(particleControlsToSettings()) : QtUtils::toIndString(this->contentTextEdit->toPlainText()));
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
		ob_out.audio_player_sound_radius = myClamp((float)this->audioSoundRadiusSpinBox->value(),
			WorldObject::MIN_AUDIO_PLAYER_SOUND_RADIUS, WorldObject::MAX_AUDIO_PLAYER_SOUND_RADIUS);
		ob_out.audio_player_directionality_enabled = this->audioDirectionalityEnabledCheckBox->isChecked();
		ob_out.audio_player_directivity_alpha = myClamp((float)this->audioDirectivityAlphaSpinBox->value(),
			WorldObject::MIN_AUDIO_PLAYER_DIRECTIVITY_ALPHA, WorldObject::MAX_AUDIO_PLAYER_DIRECTIVITY_ALPHA);
		ob_out.audio_player_directivity_order = myClamp((float)this->audioDirectivityOrderSpinBox->value(),
			WorldObject::MIN_AUDIO_PLAYER_DIRECTIVITY_ORDER, WorldObject::MAX_AUDIO_PLAYER_DIRECTIVITY_ORDER);
		ob_out.audio_player_spread_degrees = myClamp((float)this->audioSpreadDegreesSpinBox->value(),
			WorldObject::MIN_AUDIO_PLAYER_SPREAD_DEGREES, WorldObject::MAX_AUDIO_PLAYER_SPREAD_DEGREES);
		ob_out.audio_player_schedule_enabled = this->audioScheduleEnabledCheckBox->isChecked();
		ob_out.audio_player_schedule_start_hour = myClamp((float)this->audioScheduleStartHourSpinBox->value(),
			WorldObject::MIN_AUDIO_PLAYER_SCHEDULE_HOUR, WorldObject::MAX_AUDIO_PLAYER_SCHEDULE_HOUR);
		ob_out.audio_player_schedule_end_hour = myClamp((float)this->audioScheduleEndHourSpinBox->value(),
			WorldObject::MIN_AUDIO_PLAYER_SCHEDULE_HOUR, WorldObject::MAX_AUDIO_PLAYER_SCHEDULE_HOUR);
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
	this->audioSoundRadiusSpinBox->setReadOnly(!editable);
	this->audioDirectionalityEnabledCheckBox->setEnabled(editable);
	this->audioDirectivityAlphaSpinBox->setReadOnly(!editable);
	this->audioDirectivityOrderSpinBox->setReadOnly(!editable);
	this->audioSpreadDegreesSpinBox->setReadOnly(!editable);
	this->audioScheduleEnabledCheckBox->setEnabled(editable);
	this->audioScheduleStartHourSpinBox->setReadOnly(!editable);
	this->audioScheduleEndHourSpinBox->setReadOnly(!editable);
	this->audioPlaylistListWidget->setEnabled(editable);
	this->audioPlaylistListWidget->setEditTriggers(editable ? (QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed | QAbstractItemView::SelectedClicked) : QAbstractItemView::NoEditTriggers);
	updateAudioPlaylistButtonsEnabled();

	this->portalStyleComboBox->setEnabled(editable);
	this->portalApplyStylePushButton->setEnabled(editable);
	this->portalResetMaterialsPushButton->setEnabled(editable);
	this->portalMainWorldPushButton->setEnabled(editable);
	this->portalMapWorldPushButton->setEnabled(editable);
	this->portalClearTargetPushButton->setEnabled(editable);

	this->gaussianSplatPresetComboBox->setEnabled(editable);
	this->gaussianSplatResetPushButton->setEnabled(editable);
	this->gaussianSplatSHDetailComboBox->setEnabled(editable);
	this->gaussianSplatOpacitySpinBox->setReadOnly(!editable);
	this->gaussianSplatMinimumSourceOpacitySpinBox->setReadOnly(!editable);
	this->gaussianSplatBrightnessSpinBox->setReadOnly(!editable);
	this->gaussianSplatRadiusSpinBox->setReadOnly(!editable);
	this->gaussianSplatSaturationSpinBox->setReadOnly(!editable);
	this->gaussianSplatContrastSpinBox->setReadOnly(!editable);
	this->gaussianSplatAlphaCutoffSpinBox->setReadOnly(!editable);

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

	this->particleEnabledCheckBox->setEnabled(editable);
	this->particlePresetComboBox->setEnabled(editable);
	this->particleSavePresetPushButton->setEnabled(editable);
	this->particleDeletePresetPushButton->setEnabled(editable);
	this->particleKindComboBox->setEnabled(editable);
	this->particleDirectionComboBox->setEnabled(editable);
	this->particleShapeComboBox->setEnabled(editable);
	this->particleRenderModeComboBox->setEnabled(editable);
	this->particleSpriteLibraryComboBox->setEnabled(editable);
	this->particleSpritePathLineEdit->setReadOnly(!editable);
	this->particleSpriteBrowsePushButton->setEnabled(editable);
	this->particleSpriteClearPushButton->setEnabled(editable);
	this->particleAudioEnabledCheckBox->setEnabled(editable);
	this->particleAudioURLLineEdit->setReadOnly(!editable);
	this->particleAudioBrowsePushButton->setEnabled(editable);
	this->particleAudioClearPushButton->setEnabled(editable);
	this->particleAudioLoopCheckBox->setEnabled(editable);
	this->particleAudioSpatialCheckBox->setEnabled(editable);
	this->particleAudioVolumeSpinBox->setReadOnly(!editable);
	this->particleAudioActivationDistanceSpinBox->setReadOnly(!editable);
	this->particleAudioMinDistanceSpinBox->setReadOnly(!editable);
	this->particleAudioMaxDistanceSpinBox->setReadOnly(!editable);
	this->particleAudioFadeInSpinBox->setReadOnly(!editable);
	this->particleAudioFadeOutSpinBox->setReadOnly(!editable);
	this->particleRateSpinBox->setReadOnly(!editable);
	this->particleFrameCapSpinBox->setReadOnly(!editable);
	this->particleMaxParticlesSpinBox->setReadOnly(!editable);
	this->particleRadiusSpinBox->setReadOnly(!editable);
	this->particleSpeedSpinBox->setReadOnly(!editable);
	this->particleSpeedJitterSpinBox->setReadOnly(!editable);
	this->particleSpreadSpinBox->setReadOnly(!editable);
	this->particleTurbulenceSpinBox->setReadOnly(!editable);
	this->particleLifetimeSpinBox->setReadOnly(!editable);
	this->particleStartWidthSpinBox->setReadOnly(!editable);
	this->particleEndWidthSpinBox->setReadOnly(!editable);
	this->particleSizeCurveWidget->setEnabled(editable);
	this->particleSizeJitterSpinBox->setReadOnly(!editable);
	this->particleOpacitySpinBox->setReadOnly(!editable);
	this->particleEndOpacitySpinBox->setReadOnly(!editable);
	this->particleOpacityCurveWidget->setEnabled(editable);
	this->particleOpacityJitterSpinBox->setReadOnly(!editable);
	this->particleColourPushButton->setEnabled(editable);
	this->particleEndColourPushButton->setEnabled(editable);
	this->particleColourJitterSpinBox->setReadOnly(!editable);
	this->particleTrailLengthSpinBox->setReadOnly(!editable);
	this->particleGlowStrengthSpinBox->setReadOnly(!editable);
	this->particleRotationSpinBox->setReadOnly(!editable);
	this->particleRotationJitterSpinBox->setReadOnly(!editable);
	this->particleSpinSpinBox->setReadOnly(!editable);
	this->particleSpinJitterSpinBox->setReadOnly(!editable);
	this->particleBurstEnabledCheckBox->setEnabled(editable);
	this->particleBurstCountSpinBox->setReadOnly(!editable);
	this->particleBurstIntervalSpinBox->setReadOnly(!editable);
	this->particleMaxDistanceSpinBox->setReadOnly(!editable);
	this->particleBurstNowPushButton->setEnabled(editable);
	this->particleClearParticlesPushButton->setEnabled(editable);
	this->particleWindXSpinBox->setReadOnly(!editable);
	this->particleWindYSpinBox->setReadOnly(!editable);
	this->particleWindZSpinBox->setReadOnly(!editable);
	this->particleVortexStrengthSpinBox->setReadOnly(!editable);
	this->particleAttractorStrengthSpinBox->setReadOnly(!editable);
	this->particleAttractorRadiusSpinBox->setReadOnly(!editable);
	this->particleBlackHoleCheckBox->setEnabled(editable);
	this->particleEventHorizonSpinBox->setReadOnly(!editable);
	this->particleRadialAccelSpinBox->setReadOnly(!editable);
	this->particleLinearDampingSpinBox->setReadOnly(!editable);
	this->particleBuoyancyLiftSpinBox->setReadOnly(!editable);
	this->particleGravityScaleSpinBox->setReadOnly(!editable);
	this->particleDragAreaSpinBox->setReadOnly(!editable);
	this->particleMassSpinBox->setReadOnly(!editable);
	this->particleRestitutionSpinBox->setReadOnly(!editable);
	this->particleCollisionFrictionSpinBox->setReadOnly(!editable);
	this->particleCollideSurfacesCheckBox->setEnabled(editable);
	this->particleDieOnSurfaceCheckBox->setEnabled(editable);
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


void ObjectEditor::audioDirectionalityToggled(bool checked)
{
	this->audioDirectivityAlphaSpinBox->setEnabled(checked);
	this->audioDirectivityOrderSpinBox->setEnabled(checked);
	this->audioSpreadDegreesSpinBox->setEnabled(checked);
}


void ObjectEditor::audioScheduleToggled(bool checked)
{
	this->audioScheduleStartHourSpinBox->setEnabled(checked);
	this->audioScheduleEndHourSpinBox->setEnabled(checked);
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
	this->visitURLLabel->setVisible(
		(this->editing_object_type != WorldObject::ObjectType_Portal) &&
		!this->editing_audio_player_webview &&
		!this->editing_particle_emitter &&
		!this->targetURLLineEdit->text().isEmpty()
	);
}


void ObjectEditor::applyPortalStylePreset()
{
	if(this->editing_object_type != WorldObject::ObjectType_Portal || !this->portalStyleComboBox)
		return;

	const QString style = this->portalStyleComboBox->currentData().toString();
	applyPortalStylePresetToMaterials(style, this->cloned_materials);

	if(this->selected_mat_index >= 0 && this->selected_mat_index < (int)this->cloned_materials.size())
		this->matEditor->setFromMaterial(*this->cloned_materials[this->selected_mat_index]);

	emit objectChanged();
}


void ObjectEditor::resetPortalMaterials()
{
	if(this->editing_object_type != WorldObject::ObjectType_Portal || !this->portalStyleComboBox)
		return;

	const int classic_index = this->portalStyleComboBox->findData(QStringLiteral("classic"));
	if(classic_index >= 0)
		this->portalStyleComboBox->setCurrentIndex(classic_index);

	applyPortalStylePreset();
}


void ObjectEditor::setPortalTargetMainWorld()
{
	if(this->editing_object_type != WorldObject::ObjectType_Portal)
		return;

	this->targetURLLineEdit->setText(QStringLiteral("sub://vr.metasiberia.com/"));
	targetURLChanged();
	emit objectChanged();
}


void ObjectEditor::setPortalTargetMapWorld()
{
	if(this->editing_object_type != WorldObject::ObjectType_Portal)
		return;

	this->targetURLLineEdit->setText(QStringLiteral("sub://vr.metasiberia.com/map"));
	targetURLChanged();
	emit objectChanged();
}


void ObjectEditor::clearPortalTarget()
{
	if(this->editing_object_type != WorldObject::ObjectType_Portal)
		return;

	this->targetURLLineEdit->clear();
	targetURLChanged();
	emit objectChanged();
}


void ObjectEditor::showPortalHelp()
{
	const RuntimeTranslation::UILanguage ui_language = currentUILanguageForObjectEditor(this->settings);
	auto tr_portal = [ui_language](const char* source_text)
	{
		return translateObjectEditorRuntimeText(ui_language, source_text);
	};

	QDialog dialog(this);
	dialog.setWindowTitle(tr_portal("Portal Editor Help"));
	dialog.resize(700, 720);

	QVBoxLayout* main_layout = new QVBoxLayout(&dialog);
	main_layout->setContentsMargins(12, 12, 12, 12);
	main_layout->setSpacing(10);

	QTextBrowser* browser = new QTextBrowser(&dialog);
	browser->setOpenExternalLinks(false);
	browser->setMinimumSize(560, 420);
	const QString html =
		QStringLiteral("<h2>") + tr_portal("Portal Editor Help") + QStringLiteral("</h2>") +
		QStringLiteral("<p>") + tr_portal("A portal is both navigation and stage lighting. Players must instantly understand where it goes, whether it is active, and what kind of world waits behind it.") + QStringLiteral("</p>") +
		QStringLiteral("<h3>") + tr_portal("Fast workflow") + QStringLiteral("</h3><ol>") +
		QStringLiteral("<li>") + tr_portal("Set Portal Target URL first. Use sub:// links for in-world travel, for example sub://vr.metasiberia.com/map.") + QStringLiteral("</li>") +
		QStringLiteral("<li>") + tr_portal("Pick a Portal Style or press a style button below. The preset changes all five portal materials at once.") + QStringLiteral("</li>") +
		QStringLiteral("<li>") + tr_portal("Tune Material 3 (Portal Effect) for the inner colour and glow, then tune rim/frame materials for silhouette readability.") + QStringLiteral("</li>") +
		QStringLiteral("<li>") + tr_portal("Scale the portal so the player can read it from a distance, then rotate it so the front faces the intended approach direction.") + QStringLiteral("</li></ol>") +
		QStringLiteral("<h3>") + tr_portal("Material slots") + QStringLiteral("</h3><ul>") +
		QStringLiteral("<li>") + tr_portal("Material 0 is the inner rim: use it as the brightest readable outline.") + QStringLiteral("</li>") +
		QStringLiteral("<li>") + tr_portal("Material 1 is the arch body: stone, metal, glass or dark support material.") + QStringLiteral("</li>") +
		QStringLiteral("<li>") + tr_portal("Material 2 is the outer edge: use contrast here so the portal silhouette stays visible.") + QStringLiteral("</li>") +
		QStringLiteral("<li>") + tr_portal("Material 3 is the portal effect: this tints the procedural inner shader.") + QStringLiteral("</li>") +
		QStringLiteral("<li>") + tr_portal("Material 4 is the threshold: make it match the rim so players see where to step.") + QStringLiteral("</li></ul>") +
		QStringLiteral("<h3>") + tr_portal("Space portals") + QStringLiteral("</h3><p>") +
		tr_portal("For space themes use Stargate, Void Singularity, Solar Gate or Ghost Gate. Strong blue/cyan reads as sci-fi travel, violet reads as deep-space anomaly, orange reads as solar/plasma, pale transparent blue reads as cryo or ghost-space. Add a nearby particle emitter with Black Hole, Wormhole, Galaxy Spiral, Starfield or Comet Tail for a professional layered effect.") + QStringLiteral("</p>") +
		QStringLiteral("<h3>") + tr_portal("Professional composition") + QStringLiteral("</h3><p>") +
		tr_portal("A strong portal usually has three layers: solid frame, glowing threshold, moving inner effect. Add particles outside the arch only after the target URL and base style are correct. Keep the portal entrance unobstructed and avoid very low opacity on the frame, otherwise players miss the doorway.") + QStringLiteral("</p>") +
		QStringLiteral("<h3>") + tr_portal("What is still missing") + QStringLiteral("</h3><p>") +
		tr_portal("Not yet implemented: portal mesh shape variants, inner effect variants, animated shader parameters, destination picker from favorites/parcels/worlds, URL validation, paired two-way portal creation, spawn offset/orientation controls, access rules, cooldown/sound controls, portal preview thumbnails and portal-specific particle attachment.") + QStringLiteral("</p>");
	browser->setHtml(html);
	main_layout->addWidget(browser, 1);

	QLabel* style_label = new QLabel(QStringLiteral("<b>") + tr_portal("Apply a complete portal style") + QStringLiteral("</b>"), &dialog);
	main_layout->addWidget(style_label);

	QGridLayout* style_grid = new QGridLayout();
	style_grid->setContentsMargins(0, 0, 0, 0);
	style_grid->setHorizontalSpacing(6);
	style_grid->setVerticalSpacing(6);
	auto add_style_button = [&](const char* button_label, const QString& style_data, int row, int col)
	{
		QPushButton* button = new QPushButton(tr_portal(button_label), &dialog);
		style_grid->addWidget(button, row, col);
		connect(button, &QPushButton::clicked, &dialog, [this, style_data, &dialog]()
		{
			const int index = this->portalStyleComboBox ? this->portalStyleComboBox->findData(style_data) : -1;
			if(index >= 0)
				this->portalStyleComboBox->setCurrentIndex(index);
			applyPortalStylePreset();
			dialog.accept();
		});
	};
	add_style_button("Classic Marble", QStringLiteral("classic"), 0, 0);
	add_style_button("Arcane Rift", QStringLiteral("arcane"), 0, 1);
	add_style_button("Stargate", QStringLiteral("stargate"), 0, 2);
	add_style_button("Void Singularity", QStringLiteral("void"), 0, 3);
	add_style_button("Lava Gate", QStringLiteral("lava"), 1, 0);
	add_style_button("Ice Gate", QStringLiteral("ice"), 1, 1);
	add_style_button("Forest Gate", QStringLiteral("forest"), 1, 2);
	add_style_button("Cyber Gate", QStringLiteral("cyber"), 1, 3);
	add_style_button("Solar Gate", QStringLiteral("solar"), 2, 0);
	add_style_button("Ghost Gate", QStringLiteral("ghost"), 2, 1);
	main_layout->addLayout(style_grid);

	QHBoxLayout* bottom_layout = new QHBoxLayout();
	QPushButton* main_world_button = new QPushButton(tr_portal("Main World"), &dialog);
	QPushButton* map_world_button = new QPushButton(tr_portal("Map World"), &dialog);
	QPushButton* clear_target_button = new QPushButton(tr_portal("Clear Target"), &dialog);
	QPushButton* close_button = new QPushButton(tr_portal("Close"), &dialog);
	bottom_layout->addWidget(main_world_button);
	bottom_layout->addWidget(map_world_button);
	bottom_layout->addWidget(clear_target_button);
	bottom_layout->addStretch(1);
	bottom_layout->addWidget(close_button);
	main_layout->addLayout(bottom_layout);

	connect(main_world_button, &QPushButton::clicked, this, [this, &dialog]() { setPortalTargetMainWorld(); dialog.accept(); });
	connect(map_world_button, &QPushButton::clicked, this, [this, &dialog]() { setPortalTargetMapWorld(); dialog.accept(); });
	connect(clear_target_button, &QPushButton::clicked, this, [this, &dialog]() { clearPortalTarget(); dialog.accept(); });
	connect(close_button, &QPushButton::clicked, &dialog, &QDialog::accept);

	dialog.exec();
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


void ObjectEditor::updateParticleColourButton()
{
	const int COLOUR_BUTTON_W = 30;
	auto set_button_icon = [COLOUR_BUTTON_W](QPushButton* button, const Colour3f& colour)
	{
		if(!button)
			return;
		QImage image(COLOUR_BUTTON_W, COLOUR_BUTTON_W, QImage::Format_RGB32);
		image.fill(QColor(qRgba(
			(int)(colour.r * 255),
			(int)(colour.g * 255),
			(int)(colour.b * 255),
			255
		)));
		QIcon icon;
		QPixmap pixmap = QPixmap::fromImage(image);
		icon.addPixmap(pixmap);
		button->setIcon(icon);
		button->setIconSize(QSize(COLOUR_BUTTON_W, COLOUR_BUTTON_W));
	};
	set_button_icon(this->particleColourPushButton, this->particle_col);
	set_button_icon(this->particleEndColourPushButton, this->particle_end_col);
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


void ObjectEditor::on_particleColourPushButton_clicked(bool checked)
{
	const QColor initial_col(qRgba(
		(int)(particle_col.r * 255),
		(int)(particle_col.g * 255),
		(int)(particle_col.b * 255),
		255
	));

	QColorDialog d(initial_col, this);
	const int res = d.exec();
	if(res == QDialog::Accepted)
	{
		const QColor new_col = d.currentColor();

		this->particle_col.r = new_col.red()   / 255.f;
		this->particle_col.g = new_col.green() / 255.f;
		this->particle_col.b = new_col.blue()  / 255.f;

		updateParticleColourButton();
		updateParticlePreviewThumbnail();

		emit objectChanged();
	}
}


void ObjectEditor::on_particleEndColourPushButton_clicked(bool checked)
{
	const QColor initial_col(qRgba(
		(int)(particle_end_col.r * 255),
		(int)(particle_end_col.g * 255),
		(int)(particle_end_col.b * 255),
		255
	));

	QColorDialog d(initial_col, this);
	const int res = d.exec();
	if(res == QDialog::Accepted)
	{
		const QColor new_col = d.currentColor();

		this->particle_end_col.r = new_col.red()   / 255.f;
		this->particle_end_col.g = new_col.green() / 255.f;
		this->particle_end_col.b = new_col.blue()  / 255.f;

		updateParticleColourButton();
		updateParticlePreviewThumbnail();

		emit objectChanged();
	}
}


void ObjectEditor::particlePresetChanged(int index)
{
	if(index < 0 || !this->particlePresetComboBox)
		return;

	const QString preset_data = this->particlePresetComboBox->itemData(index).toString();
	ParticleEmitterSettings preset_settings;
	if(particlePresetDataIsUserPreset(preset_data) && this->settings)
	{
		const QString preset_name = particlePresetUserNameFromData(preset_data);
		const QString content = this->settings->value(particleCustomPresetContentKey(preset_name)).toString();
		preset_settings = ParticleEmitterSettings::fromContent(QtUtils::toStdString(content));
		preset_settings.preset_name = QtUtils::toStdString(preset_data);
	}
	else
	{
		const std::string preset_name = QtUtils::toStdString(preset_data);
		preset_settings = ParticleEmitterSettings::presetSettings(preset_name);
	}

	preset_settings.enabled = this->particleEnabledCheckBox->isChecked();
	setParticleControlsFromSettings(preset_settings);

	emit objectChanged();
	emit particleClearParticlesSignal();
	emit particleBurstNowSignal();
}


void ObjectEditor::showParticleHelp()
{
	const RuntimeTranslation::UILanguage ui_language = currentUILanguageForObjectEditor(this->settings);
	auto tr_particle = [ui_language](const char* source_text)
	{
		return translateObjectEditorRuntimeText(ui_language, source_text);
	};

	QDialog dialog(this);
	dialog.setWindowTitle(tr_particle("Particle Editor Help"));
	dialog.resize(680, 720);

	QVBoxLayout* main_layout = new QVBoxLayout(&dialog);
	main_layout->setContentsMargins(12, 12, 12, 12);
	main_layout->setSpacing(10);

	QTextBrowser* browser = new QTextBrowser(&dialog);
	browser->setOpenExternalLinks(false);
	browser->setMinimumSize(560, 420);
	const QString html =
		QStringLiteral("<h2>") + tr_particle("Particle Editor Help") + QStringLiteral("</h2>") +
		QStringLiteral("<p>") + tr_particle("Particle emitters are live objects: choose a preset, tune the controls, then judge the result in the 3D world. The preview strip shows lifetime curves, not the final world lighting.") + QStringLiteral("</p>") +
		QStringLiteral("<h3>") + tr_particle("Fast workflow") + QStringLiteral("</h3><ol>") +
		QStringLiteral("<li>") + tr_particle("Choose a preset or press one of the recipe buttons below.") + QStringLiteral("</li>") +
		QStringLiteral("<li>") + tr_particle("Move the emitter above the ground and point the arrow in the direction of travel.") + QStringLiteral("</li>") +
		QStringLiteral("<li>") + tr_particle("Press Burst Now to verify visibility, then tune Emission Rate, Start Size, End Size, Lifetime and Opacity.") + QStringLiteral("</li>") +
		QStringLiteral("<li>") + tr_particle("Use Save to store your own local preset after the effect looks right.") + QStringLiteral("</li></ol>") +
		QStringLiteral("<h3>") + tr_particle("Cinematic recipes") + QStringLiteral("</h3><ul>") +
		QStringLiteral("<li>") + tr_particle("Campfire: press Fire, keep the emitter radius small, use Up direction, short lifetime, Birth Colour white/yellow, Death Colour red/dark, then place an Embers emitter slightly above it.") + QStringLiteral("</li>") +
		QStringLiteral("<li>") + tr_particle("Explosion flash: press Supernova, lower Lifetime, increase Radial Force, use Additive Glow, then press Burst Now instead of relying on continuous emission.") + QStringLiteral("</li>") +
		QStringLiteral("<li>") + tr_particle("Magic pickup: press Magic Dust or Fireflies, use Sphere shape, low speed, soft glow, mild Vortex and no gravity.") + QStringLiteral("</li>") +
		QStringLiteral("<li>") + tr_particle("Storm rain: press Rain, use Box shape above the area, Down direction, high speed, Collide with surfaces and Die on first hit.") + QStringLiteral("</li></ul>") +
		QStringLiteral("<h3>") + tr_particle("Realistic smoke") + QStringLiteral("</h3><p>") +
		tr_particle("Use Smoke, Steam or Smoke Wisp. Keep speed low, lifetime long, end size large, opacity fading to zero, Soft Smoke render mode, slight Turbulence, negative Gravity Scale or Buoyancy Lift.") + QStringLiteral("</p>") +
		QStringLiteral("<h3>") + tr_particle("Realistic fire") + QStringLiteral("</h3><p>") +
		tr_particle("Use Fire or Embers. Good fire is a lifetime gradient: white/yellow Birth Colour, red/dark Death Colour, short Lifetime, modest End Size, Additive Glow, Flame sprite, upward direction, turbulence and small emitter radius. Add Sparks as a second emitter for hotter fire.") + QStringLiteral("</p>") +
		QStringLiteral("<h3>") + tr_particle("Space effects") + QStringLiteral("</h3><p>") +
		tr_particle("Use Nebula, Starfield, Black Hole, Gravity Well, Comet Tail, Galaxy Spiral, Supernova, Pulsar Beam, Solar Wind, Cosmic Dust or Wormhole. Most space effects want Sphere or Ring shape, Random direction, zero gravity, Additive Glow, Vortex and Attractor radius.") + QStringLiteral("</p>") +
		QStringLiteral("<h3>") + tr_particle("Space cookbook") + QStringLiteral("</h3><ul>") +
		QStringLiteral("<li>") + tr_particle("Black hole: press Black Hole, use Ring or Sphere, enable Black Hole mode, increase Attractor Strength and Event Horizon, keep End Opacity near zero, add Galaxy Spiral nearby for an accretion disc.") + QStringLiteral("</li>") +
		QStringLiteral("<li>") + tr_particle("Comet: press Comet Tail, point the emitter opposite the travel direction, use long Trail Length, pale Birth Colour, blue Death Colour, long Lifetime, low gravity and moderate turbulence.") + QStringLiteral("</li>") +
		QStringLiteral("<li>") + tr_particle("Wormhole: press Wormhole, use Ring shape, high Vortex, Additive Glow, Spiral sprite and enough Max Particles for a continuous tunnel.") + QStringLiteral("</li>") +
		QStringLiteral("<li>") + tr_particle("Spaceship engine: press Ion Thruster, use Custom direction, Cone shape, Beam sprite, high speed, short Lifetime, long Trail Length, white/cyan Birth Colour and deep blue Death Colour.") + QStringLiteral("</li>") +
		QStringLiteral("<li>") + tr_particle("Deep nebula: press Nebula or Cosmic Dust, use Sphere, very low speed, long lifetime, low opacity, large end size, no collisions and soft coloured glow.") + QStringLiteral("</li></ul>") +
		QStringLiteral("<h3>") + tr_particle("Physics and collisions") + QStringLiteral("</h3><p>") +
		tr_particle("Use Wind for directional drift, Vortex for orbiting motion, Attractor for gravity wells, Radial Force for explosions, Damping for slowing particles, and Collision Friction/Bounce for sparks, meteors and rain.") + QStringLiteral("</p>") +
		QStringLiteral("<h3>") + tr_particle("If the effect is invisible") + QStringLiteral("</h3><ul>") +
		QStringLiteral("<li>") + tr_particle("Increase Opacity, Start Size, End Size and Glow Strength.") + QStringLiteral("</li>") +
		QStringLiteral("<li>") + tr_particle("Increase Emission Rate or press Burst Now.") + QStringLiteral("</li>") +
		QStringLiteral("<li>") + tr_particle("Check View Distance and Max Particles.") + QStringLiteral("</li>") +
		QStringLiteral("<li>") + tr_particle("Clear a broken custom sprite path or choose a built-in sprite.") + QStringLiteral("</li>") +
		QStringLiteral("<li>") + tr_particle("Disable Collide with surfaces if particles die immediately on the ground.") + QStringLiteral("</li></ul>") +
		QStringLiteral("<h3>") + tr_particle("Still missing") + QStringLiteral("</h3><p>") +
		tr_particle("Not yet implemented: node graph authoring, timeline/keyframes, GPU simulation, particle-to-particle collisions, true volumetric light scattering, animated sprite atlases, export/import preset packs and per-effect LOD budgets.") + QStringLiteral("</p>");
	browser->setHtml(html);
	main_layout->addWidget(browser, 1);

	QLabel* recipes_label = new QLabel(QStringLiteral("<b>") + tr_particle("Quick recipes") + QStringLiteral("</b>"), &dialog);
	main_layout->addWidget(recipes_label);

	QGridLayout* recipe_grid = new QGridLayout();
	recipe_grid->setContentsMargins(0, 0, 0, 0);
	recipe_grid->setHorizontalSpacing(6);
	recipe_grid->setVerticalSpacing(6);
	auto add_preset_button = [&](const char* button_label, const QString& preset_data, int row, int col)
	{
		QPushButton* button = new QPushButton(tr_particle(button_label), &dialog);
		recipe_grid->addWidget(button, row, col);
		connect(button, &QPushButton::clicked, &dialog, [this, preset_data]()
		{
			const int index = this->particlePresetComboBox ? this->particlePresetComboBox->findData(preset_data) : -1;
			if(index >= 0)
			{
				QSignalBlocker blocker(this->particlePresetComboBox);
				this->particlePresetComboBox->setCurrentIndex(index);
				particlePresetChanged(index);
			}
		});
	};
	add_preset_button("Smoke", QStringLiteral("smoke"), 0, 0);
	add_preset_button("Fire", QStringLiteral("fire"), 0, 1);
	add_preset_button("Sparks", QStringLiteral("sparks"), 0, 2);
	add_preset_button("Nebula", QStringLiteral("nebula"), 0, 3);
	add_preset_button("Steam", QStringLiteral("steam"), 1, 0);
	add_preset_button("Embers", QStringLiteral("embers"), 1, 1);
	add_preset_button("Rain", QStringLiteral("rain"), 1, 2);
	add_preset_button("Plasma", QStringLiteral("plasma"), 1, 3);
	add_preset_button("Black Hole", QStringLiteral("black_hole"), 2, 0);
	add_preset_button("Gravity Well", QStringLiteral("gravity_well"), 2, 1);
	add_preset_button("Comet Tail", QStringLiteral("comet_tail"), 2, 2);
	add_preset_button("Galaxy Spiral", QStringLiteral("galaxy_spiral"), 2, 3);
	add_preset_button("Supernova", QStringLiteral("supernova"), 3, 0);
	add_preset_button("Pulsar Beam", QStringLiteral("pulsar_beam"), 3, 1);
	add_preset_button("Wormhole", QStringLiteral("wormhole"), 3, 2);
	add_preset_button("Ion Thruster", QStringLiteral("ion_thruster"), 3, 3);
	add_preset_button("Solar Wind", QStringLiteral("solar_wind"), 4, 0);
	add_preset_button("Cosmic Dust", QStringLiteral("cosmic_dust"), 4, 1);
	add_preset_button("Energy Shield", QStringLiteral("energy_shield"), 4, 2);
	add_preset_button("Fireflies", QStringLiteral("fireflies"), 4, 3);
	main_layout->addLayout(recipe_grid);

	QHBoxLayout* bottom_layout = new QHBoxLayout();
	QPushButton* burst_button = new QPushButton(tr_particle("Burst Now"), &dialog);
	QPushButton* clear_button = new QPushButton(tr_particle("Clear Particles"), &dialog);
	QPushButton* close_button = new QPushButton(tr_particle("Close"), &dialog);
	bottom_layout->addWidget(burst_button);
	bottom_layout->addWidget(clear_button);
	bottom_layout->addStretch(1);
	bottom_layout->addWidget(close_button);
	main_layout->addLayout(bottom_layout);

	connect(burst_button, &QPushButton::clicked, this, [this]() { emit particleBurstNowSignal(); });
	connect(clear_button, &QPushButton::clicked, this, [this]() { emit particleClearParticlesSignal(); });
	connect(close_button, &QPushButton::clicked, &dialog, &QDialog::accept);

	dialog.exec();
}


void ObjectEditor::saveParticlePreset()
{
	if(!this->settings)
		return;

	QString default_name;
	const QString cur_data = this->particlePresetComboBox->currentData().toString();
	if(particlePresetDataIsUserPreset(cur_data))
		default_name = particlePresetUserNameFromData(cur_data);

	bool ok = false;
	QString name = QInputDialog::getText(
		this,
		QCoreApplication::translate("ObjectEditor", "Save Particle Preset"),
		QCoreApplication::translate("ObjectEditor", "Preset name:"),
		QLineEdit::Normal,
		default_name,
		&ok
	).trimmed();

	if(!ok || name.isEmpty())
		return;

	name.replace(QStringLiteral("\""), QStringLiteral("'"));
	name.replace(QStringLiteral("\\"), QStringLiteral("_"));
	name.replace(QStringLiteral("/"), QStringLiteral("_"));

	ParticleEmitterSettings preset_settings = particleControlsToSettings();
	const QString preset_data = QStringLiteral("user:") + name;
	preset_settings.preset_name = QtUtils::toStdString(preset_data);

	this->settings->setValue(particleCustomPresetContentKey(name), QtUtils::toQString(ParticleEmitterSettings::serialiseToContent(preset_settings)));

	QStringList names = this->settings->value(particleCustomPresetNamesKey()).toStringList();
	if(!names.contains(name, Qt::CaseInsensitive))
		names.push_back(name);
	names.removeDuplicates();
	names.sort(Qt::CaseInsensitive);
	this->settings->setValue(particleCustomPresetNamesKey(), names);
	this->settings->sync();

	loadCustomParticlePresets();
	const int new_index = this->particlePresetComboBox->findData(preset_data);
	if(new_index >= 0)
		this->particlePresetComboBox->setCurrentIndex(new_index);

	emit objectChanged();
}


void ObjectEditor::deleteParticlePreset()
{
	if(!this->settings || !this->particlePresetComboBox)
		return;

	const QString preset_data = this->particlePresetComboBox->currentData().toString();
	if(!particlePresetDataIsUserPreset(preset_data))
		return;

	const QString name = particlePresetUserNameFromData(preset_data);
	this->settings->remove(particleCustomPresetContentKey(name));

	QStringList names = this->settings->value(particleCustomPresetNamesKey()).toStringList();
	names.removeAll(name);
	this->settings->setValue(particleCustomPresetNamesKey(), names);
	this->settings->sync();

	loadCustomParticlePresets();
	const int smoke_index = this->particlePresetComboBox->findData(QStringLiteral("smoke"));
	this->particlePresetComboBox->setCurrentIndex(smoke_index >= 0 ? smoke_index : 0);

	emit objectChanged();
}


void ObjectEditor::browseParticleSprite()
{
	const QString data_dir = QtUtils::toQString(this->base_dir_path + "/data");
	const QString start_dir = QDir(data_dir + QStringLiteral("/resources/sprites")).exists() ? data_dir + QStringLiteral("/resources/sprites") : data_dir;
	const QString filename = QFileDialog::getOpenFileName(
		this,
		QCoreApplication::translate("ObjectEditor", "Select Particle Sprite"),
		start_dir,
		QCoreApplication::translate("ObjectEditor", "Images (*.png *.jpg *.jpeg *.webp *.basis);;All files (*.*)")
	);

	if(filename.isEmpty())
		return;

	QString path = filename;
	if(!data_dir.isEmpty())
	{
		const QString rel = QDir(data_dir).relativeFilePath(filename).replace(QStringLiteral("\\"), QStringLiteral("/"));
		if(!rel.startsWith(QStringLiteral("../")) && !QDir::isAbsolutePath(rel))
			path = QStringLiteral("/") + rel;
	}
	path.replace(QStringLiteral("\\"), QStringLiteral("/"));

	this->particleSpritePathLineEdit->setText(path);
	{
		SignalBlocker blocker(this->particleSpriteLibraryComboBox);
		this->particleSpriteLibraryComboBox->setCurrentIndex(0);
	}
	emit objectChanged();
}


void ObjectEditor::clearParticleSprite()
{
	this->particleSpritePathLineEdit->clear();
	{
		SignalBlocker blocker(this->particleSpriteLibraryComboBox);
		this->particleSpriteLibraryComboBox->setCurrentIndex(0);
	}
	updateParticlePreviewThumbnail();
	emit objectChanged();
}


void ObjectEditor::browseParticleAudio()
{
	const QString data_dir = QtUtils::toQString(this->base_dir_path + "/data");
	const QString start_dir = QDir(data_dir + QStringLiteral("/resources/sounds")).exists() ? data_dir + QStringLiteral("/resources/sounds") : data_dir;
	const QString filename = QFileDialog::getOpenFileName(
		this,
		QCoreApplication::translate("ObjectEditor", "Select Particle Sound"),
		start_dir,
		QCoreApplication::translate("ObjectEditor", "Audio (*.mp3 *.wav);;All files (*.*)")
	);

	if(filename.isEmpty())
		return;

	QString path = filename;
	if(!data_dir.isEmpty())
	{
		const QString rel = QDir(data_dir).relativeFilePath(filename).replace(QStringLiteral("\\"), QStringLiteral("/"));
		if(!rel.startsWith(QStringLiteral("../")) && !QDir::isAbsolutePath(rel))
			path = QStringLiteral("/") + rel;
	}
	path.replace(QStringLiteral("\\"), QStringLiteral("/"));

	this->particleAudioURLLineEdit->setText(path);
	SignalBlocker::setChecked(this->particleAudioEnabledCheckBox, true);
	emit objectChanged();
}


void ObjectEditor::clearParticleAudio()
{
	this->particleAudioURLLineEdit->clear();
	SignalBlocker::setChecked(this->particleAudioEnabledCheckBox, false);
	emit objectChanged();
}


void ObjectEditor::particleSpriteLibraryChanged(int index)
{
	if(index < 0 || !this->particleSpriteLibraryComboBox || !this->particleSpritePathLineEdit)
		return;

	const QString sprite_path = this->particleSpriteLibraryComboBox->itemData(index).toString();
	{
		QSignalBlocker blocker(this->particleSpritePathLineEdit);
		this->particleSpritePathLineEdit->setText(sprite_path);
	}
	updateParticlePreviewThumbnail();
	emit objectChanged();
}


void ObjectEditor::updateParticlePreviewThumbnail()
{
	if(!this->particlePreviewLabel || !this->particleRateSpinBox || !this->particleSizeCurveWidget || !this->particleOpacityCurveWidget)
		return;

	if(!this->editing_particle_emitter)
	{
		this->particlePreviewLabel->clear();
		return;
	}

	const ParticleEmitterSettings s = particleControlsToSettings();
	const int W = myMax(180, this->particlePreviewLabel->width() > 0 ? this->particlePreviewLabel->width() : 220);
	const int H = 76;
	QPixmap pix(W, H);
	pix.fill(QColor(18, 20, 23));

	QPainter p(&pix);
	p.setRenderHint(QPainter::Antialiasing, true);
	p.fillRect(pix.rect(), QColor(18, 20, 23));
	p.setPen(QPen(QColor(42, 46, 52), 1));
	for(int x=0; x<W; x += 24)
		p.drawLine(x, 0, x, H);
	for(int y=0; y<H; y += 24)
		p.drawLine(0, y, W, y);

	if(s.black_hole_mode)
	{
		const QPointF c(W * 0.52, H * 0.5);
		QRadialGradient g(c, H * 0.45);
		g.setColorAt(0.0, QColor(0, 0, 0, 255));
		g.setColorAt(0.34, QColor(0, 0, 0, 230));
		g.setColorAt(0.42, QColor(255, 166, 62, 210));
		g.setColorAt(0.60, QColor(80, 125, 255, 72));
		g.setColorAt(1.0, QColor(0, 0, 0, 0));
		p.setBrush(g);
		p.setPen(Qt::NoPen);
		p.drawEllipse(c, H * 0.42, H * 0.42);
		p.setBrush(QColor(0, 0, 0, 255));
		p.drawEllipse(c, myMax(4.0, s.event_horizon_radius * 16.0), myMax(4.0, s.event_horizon_radius * 16.0));
	}

	const int samples = 34;
	for(int i=0; i<samples; ++i)
	{
		const float t = (float)i / (float)(samples - 1);
		const float size_t = previewCurveValue(s.size_curve, t, s.size_curve_mid);
		const float opacity_t = previewCurveValue(s.opacity_curve, t, s.opacity_curve_mid);
		const float width = Maths::lerp(s.start_width, s.end_width, size_t);
		const float alpha = Maths::lerp(s.opacity, s.end_opacity, opacity_t);
		const float x = 16.f + (W - 32.f) * t;
		const float flow = s.direction == ParticleEmitterSettings::Direction_Down ? 1.f : -1.f;
		const float y = H * 0.55f + flow * (H * 0.28f) * t + std::sin((float)i * 1.7f) * myMin(12.f, s.turbulence_strength * 2.2f);
		const float radius = myClamp(width * 10.f, 2.0f, 18.0f);
		QColor col(
			(int)myClamp(Maths::lerp(s.start_colour.r, s.end_colour.r, t) * 255.f, 0.f, 255.f),
			(int)myClamp(Maths::lerp(s.start_colour.g, s.end_colour.g, t) * 255.f, 0.f, 255.f),
			(int)myClamp(Maths::lerp(s.start_colour.b, s.end_colour.b, t) * 255.f, 0.f, 255.f)
		);
		col.setAlpha((int)myClamp(alpha * 220.f, 0.f, 245.f));
		if(s.trail_length > 0.f && i > 0)
		{
			QColor trail_col = col;
			trail_col.setAlpha((int)myClamp(alpha * 90.f, 0.f, 160.f));
			p.setPen(QPen(trail_col, myClamp(radius * 0.65f, 1.0f, 8.0f), Qt::SolidLine, Qt::RoundCap));
			const float tail_x = x - myMin(48.f, s.trail_length * 10.f);
			p.drawLine(QPointF(tail_x, y), QPointF(x, y));
		}
		if(s.render_mode == ParticleEmitterSettings::RenderMode_AdditiveGlow)
		{
			QRadialGradient glow(QPointF(x, y), radius * 1.8f);
			QColor glow_col = col;
			glow_col.setAlpha((int)myClamp(alpha * s.glow_strength * 42.f, 0.f, 180.f));
			glow.setColorAt(0.0, glow_col);
			glow.setColorAt(1.0, QColor(glow_col.red(), glow_col.green(), glow_col.blue(), 0));
			p.setBrush(glow);
			p.setPen(Qt::NoPen);
			p.drawEllipse(QPointF(x, y), radius * 1.8f, radius * 1.8f);
		}
		p.setBrush(col);
		p.setPen(Qt::NoPen);
		p.drawEllipse(QPointF(x, y), radius, radius);
	}

	p.setPen(QPen(QColor(82, 92, 102), 1));
	p.drawRect(pix.rect().adjusted(0, 0, -1, -1));
	this->particlePreviewLabel->setPixmap(pix);
}


void ObjectEditor::setParticleDiagnostics(size_t selected_emitter_particle_count, size_t total_particle_count)
{
	if(!this->particleDiagnosticsValueLabel)
		return;

	if(!this->editing_particle_emitter)
		this->particleDiagnosticsValueLabel->setText(QStringLiteral("-"));
	else
	{
		const int budget = this->particleMaxParticlesSpinBox ? (int)std::round(this->particleMaxParticlesSpinBox->value()) : 0;
		const double rate = this->particleRateSpinBox ? this->particleRateSpinBox->value() : 0.0;
		const QString sprite = this->particleSpritePathLineEdit ? this->particleSpritePathLineEdit->text().trimmed() : QString();
		QString sprite_status = QCoreApplication::translate("ObjectEditor", "built-in sprite");
		if(!sprite.isEmpty())
		{
			if(sprite.startsWith(QStringLiteral("builtin:")))
				sprite_status = QCoreApplication::translate("ObjectEditor", "built-in sprite");
			else if(sprite.startsWith(QStringLiteral("/")) || sprite.contains(QStringLiteral(":/")) || sprite.contains(QStringLiteral(":\\")))
				sprite_status = QCoreApplication::translate("ObjectEditor", "local sprite");
			else
				sprite_status = QCoreApplication::translate("ObjectEditor", "shared sprite");
		}

		this->particleDiagnosticsValueLabel->setText(QStringLiteral("%1 / %2 | %3 total | %4/s | %5")
			.arg((qulonglong)selected_emitter_particle_count)
			.arg(budget)
			.arg((qulonglong)total_particle_count)
			.arg(rate, 0, 'f', 1)
			.arg(sprite_status));
	}

	updateParticlePreviewThumbnail();
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
