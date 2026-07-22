/*=====================================================================
MainWindow.cpp
--------------
Copyright Glare Technologies Limited 2024 -
=====================================================================*/


#ifdef _MSC_VER // Qt headers suppress some warnings on Windows, make sure the warning suppression doesn't propagate to our code. See https://bugreports.qt.io/browse/QTBUG-26877
#pragma warning(push, 0) // Disable warnings
#endif
#include "MainWindow.h"
#include "ui_MainWindow.h"
#include "AboutDialog.h"
#include "UpdateDialog.h"
#include "UpdateManager.h"
#include "CreateObjectsDialog.h"
#include "webcam/WebcamWindow.h"
#include "ClientThread.h"
#include "GoToPositionDialog.h"
#include "LogWindow.h"
#include "UserDetailsWidget.h"
#include "AvatarSettingsDialog.h"
#include "AvatarSettingsWidget.h"
#include "AddObjectDialog.h"
#include "AddVideoDialog.h"
#include "MainOptionsDialog.h"
#include "FindObjectDialog.h"
#include "ListObjectsNearbyDialog.h"
#include "ModelLoading.h"
#include "EmojiUtils.h"
#include "TestSuite.h"
#include "TerrainSystem.h"
#include "GuiClientApplication.h"
#include "LoginDialog.h"
#include "SignUpDialog.h"
#include "GoToParcelDialog.h"
#include "LoadItemQueue.h"
#include <settings/QSettingsStore.h>
#include "URLWidget.h"
#include "URLWhitelist.h"
#include "URLParser.h"
#include "CEF.h"
#include "ThreadMessages.h"
#include "MeshBuilding.h"
#include "MiniMap.h"
#include "MapWorldUtils.h"
#include "HTTPClient.h"
#include "GearInventoryUI.h"
#include "GearInventoryPanel.h"
#include "ModelLoading.h"
#include "BotEditorWidget.h"
#include "PlayerPhysics.h"
#include "ParticleEmitterSettings.h"
#include "TreeEditorPanel.h"
#include "TreeGenerator.h"
#include "TreeObject.h"
#include "TreePresets.h"
#include "TreeSerialization.h"
#include "LucideIconUtils.h"
#include "VoxelEditorData.h"
#include "VoxelEditorPanel.h"
#include "VoxelTools.h"
#include "UploadResourceThread.h"
#include "ScientificObjectEditor.h"
#include "ScientificObjectSettings.h"
#include "CulturalObjectEditor.h"
#include "CulturalObjectSettings.h"
#include "AnimationEditorPanel.h"
#include "PhotoVideoSettingsPanel.h"
#include "DocumentEditorPanel.h"
#include "../qt/FlowLayout.h"
#include "../shared/Protocol.h"
#include "../shared/Version.h"
#include "../shared/LODGeneration.h"
#include "../shared/ImageDecoding.h"
#include "../shared/MessageUtils.h"
#include <QtCore/QMimeData>
#include <QtCore/QByteArray>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QDateTime>
#include <QtCore/QRandomGenerator>
#include <utils/FileChecksum.h>
#include <QtCore/QHash>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonParseError>
#include <QtCore/QIODevice>
#include <QtCore/QSettings>
#include <QtCore/QUuid>
#include <QtCore/QSignalBlocker>
#include <QtCore/QSet>
#include <QtCore/QThread>
#include <QtCore/QLoggingCategory>
#include <QtCore/QPointer>
#include <QtCore/QTimer>
#include <QtCore/QUrl>
#include <QtCore/QUrlQuery>
#include <QtCore/QVariant>
#include <QtCore/QVector>
#include <QtGui/QContextMenuEvent>
#include <QtGui/QCursor>
#include <QtGui/QIcon>
#include <QtGui/QImage>
#include <QtGui/QMouseEvent>
#include <QtGui/QPainter>
#include <QtGui/QPen>
#include <QtGui/QPixmap>
#include <QtGui/QPolygonF>
#include <QtCore/QRegularExpression>
#include <QtGui/QTextDocument>
#include <QtGui/QWheelEvent>
#include <QtWidgets/QAction>
#include <QtWidgets/QActionGroup>
#include <QtWidgets/QApplication>
#include <QtWidgets/QAbstractButton>
#include <QtGui/QClipboard>
#include <QtGui/QDesktopServices>
#include <QtGui/QFont>
#include <QtGui/QGuiApplication>
#include <QtGui/QHelpEvent>
#include <QtGui/QImageReader>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QMenu>
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QErrorMessage>
#include <QtWidgets/QInputDialog>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QDialog>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QFrame>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLayout>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QScrollArea>
#include <QtWidgets/QScrollBar>
#include <QtWidgets/QSizePolicy>
#include <QtWidgets/QSplitter>
#include <QtWidgets/QTabWidget>
#include <QtWidgets/QTabBar>
#include <QtWidgets/QToolButton>
#include <QtWidgets/QToolTip>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>
#include <QtWidgets/QStyle>
#include <QtWidgets/QStyleFactory>
#include <QtGui/QScreen>
#if SUBSTRATA_USE_QT_GAMEPAD
#include <QtGamepad/QGamepadManager>
#include <QtGamepad/QGamepad>
#endif
#include "../qt/QtUtils.h"
#ifdef _MSC_VER
#pragma warning(pop) // Re-enable warnings
#endif
#include "../maths/Quat.h"
#include "../maths/GeometrySampling.h"
#include "../utils/Clock.h"
#include "../utils/Timer.h"
#include "../utils/PlatformUtils.h"
#include "../utils/ConPrint.h"
#include "../utils/Exception.h"
#include "../utils/TaskManager.h"
#include "../utils/SocketBufferOutStream.h"
#include "../utils/StringUtils.h"
#include "../utils/FileUtils.h"
#include "../utils/FileChecksum.h"
#include "../utils/FileOutStream.h"
#include "../utils/BufferOutStream.h"
#include <algorithm>
#include <cstdlib>
#include <functional>
#include "../utils/IndigoXMLDoc.h"
#include "../utils/LimitedAllocator.h"
#include <Escaping.h>
#include "../networking/MySocket.h"
#include "../graphics/ImageMap.h"
#include "../graphics/FormatDecoderGLTF.h"
#include "../dll/IndigoStringUtils.h"
#include "../dll/include/IndigoException.h"
#include "../indigo/TextureServer.h"
#include "../graphics/PNGDecoder.h"
#include "../graphics/jpegdecoder.h"
#include "../opengl/RenderStatsWidget.h"
#if defined(_WIN32)
#include "../video/WMFVideoReader.h"
#endif
#include "../direct3d/Direct3DUtils.h"
#include "superluminal/PerformanceAPI.h"
#if BUGSPLAT_SUPPORT
#include <BugSplat.h>
#endif
#include <map>
#include <set>
#include <unordered_set>


#ifdef _WIN32
#include <d3d11.h>
#include <d3d11_4.h>
#endif

#if defined(_WIN32) || defined(_WIN64)
#else
#include <signal.h>
#endif
#include <OpenGLEngineTests.h>

#include <tracy/Tracy.hpp>


// If we are building on Windows, and we are not in Release mode (e.g. BUILD_TESTS is enabled), then make sure the console window is shown.
#if defined(_WIN32) && defined(BUILD_TESTS)
#pragma comment(linker, "/SUBSYSTEM:CONSOLE")
#endif


static const Colour4f PARCEL_OUTLINE_COLOUR    = Colour4f::fromHTMLHexString("f09a13"); // orange

static std::vector<std::string> qt_debug_msgs;

static FileOutStream* log_file = nullptr;
static const double XR_COMPANION_UPDATE_PERIOD_S = 1.0 / 20.0;
static const char* const QT_THEME_SETTINGS_KEY = "setting/qt_theme_name";
static const char* const UI_LANGUAGE_SETTINGS_KEY = "setting/ui_language";
static const char* const LEGACY_UI_LANGUAGE_SETTINGS_KEY = "ui/language";
static const char* const UI_LANGUAGE_APP_PROPERTY_KEY = "metasiberia.ui_language";
static const char* const QT_THEME_DIR_REL_PATH = "/data/resources/qt_themes";

static bool tryParseMapLatLonInput(const std::string& input, double& lat_out, double& lon_out);
static std::string makeMetasiberiaMapLatLonURL(double lat, double lon, double z, double heading_deg);
static bool lookupMetasiberiaMapPlaceName(const std::string& query, double& lat_out, double& lon_out, std::string& label_out, std::string& error_out);


namespace
{
struct QtThemeColors
{
	QColor primary;
	QColor secondary;

	QColor text;
	QColor overlay2;
	QColor overlay1;
	QColor overlay0;
	QColor surface2;
	QColor surface1;
	QColor surface0;
	QColor base;
	QColor mantle;
	QColor crust;
};


static bool parseThemeColor(const QJsonObject& json_obj, const char* key, QColor& colour_out, std::string& error_out)
{
	const QJsonValue value = json_obj.value(QLatin1String(key));
	if(!value.isString())
	{
		error_out = std::string("Theme key '") + key + "' is missing or not a string.";
		return false;
	}

	const QColor colour(value.toString());
	if(!colour.isValid())
	{
		error_out = std::string("Theme key '") + key + "' has an invalid colour value.";
		return false;
	}

	colour_out = colour;
	return true;
}


static bool loadQtThemeColorsFromFile(const QString& file_path, QtThemeColors& theme_out, std::string& error_out)
{
	QFile file(file_path);
	if(!file.open(QIODevice::ReadOnly))
	{
		error_out = "Failed to open theme file '" + QtUtils::toIndString(file_path) + "'.";
		return false;
	}

	QJsonParseError parse_error;
	const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parse_error);
	if(parse_error.error != QJsonParseError::NoError)
	{
		error_out = "Failed to parse theme JSON '" + QtUtils::toIndString(file_path) + "': " + QtUtils::toIndString(parse_error.errorString()) + ".";
		return false;
	}

	if(!doc.isObject())
	{
		error_out = "Theme JSON root is not an object for '" + QtUtils::toIndString(file_path) + "'.";
		return false;
	}

	const QJsonObject obj = doc.object();
	return
		parseThemeColor(obj, "primary", theme_out.primary, error_out) &&
		parseThemeColor(obj, "secondary", theme_out.secondary, error_out) &&
		parseThemeColor(obj, "text", theme_out.text, error_out) &&
		parseThemeColor(obj, "overlay2", theme_out.overlay2, error_out) &&
		parseThemeColor(obj, "overlay1", theme_out.overlay1, error_out) &&
		parseThemeColor(obj, "overlay0", theme_out.overlay0, error_out) &&
		parseThemeColor(obj, "surface2", theme_out.surface2, error_out) &&
		parseThemeColor(obj, "surface1", theme_out.surface1, error_out) &&
		parseThemeColor(obj, "surface0", theme_out.surface0, error_out) &&
		parseThemeColor(obj, "base", theme_out.base, error_out) &&
		parseThemeColor(obj, "mantle", theme_out.mantle, error_out) &&
		parseThemeColor(obj, "crust", theme_out.crust, error_out);
}


static QString makeThemeDisplayName(const QString& internal_name)
{
	QStringList parts = internal_name.split('_', Qt::SkipEmptyParts);
	for(int i = 0; i < parts.size(); ++i)
	{
		if(!parts[i].isEmpty())
			parts[i][0] = parts[i][0].toUpper();
	}
	return parts.join(' ');
}


static void applyQtThemePalette(const QtThemeColors& theme)
{
	// Use Fusion so the palette roles are applied consistently to Qt widgets.
	if(QStyle* fusion_style = QStyleFactory::create("Fusion"))
		QApplication::setStyle(fusion_style);

	const QColor highlighted_colour = theme.primary;
	const int highlighted_gray = qGray(highlighted_colour.rgb());
	const QColor highlighted_text_colour =
		(std::abs(qGray(theme.text.rgb()) - highlighted_gray) >= std::abs(qGray(theme.mantle.rgb()) - highlighted_gray)) ?
		theme.text : theme.mantle;

	qreal h = 0, s = 0, v = 0, a = 1;
	theme.text.getHsvF(&h, &s, &v, &a);
	const QColor bright_text_colour = QColor::fromHsvF(h, s, 1.0 - v, a);

	const bool dark_theme = theme.text.value() > theme.base.value();

	QPalette palette;

	// Normal
	if(dark_theme)
	{
		palette.setColor(QPalette::Base, theme.mantle);
		palette.setColor(QPalette::AlternateBase, theme.base);
	}
	else
	{
		palette.setColor(QPalette::Base, theme.crust);
		palette.setColor(QPalette::AlternateBase, theme.mantle);
	}
	palette.setColor(QPalette::Window, theme.base);
	palette.setColor(QPalette::WindowText, theme.text);
	palette.setColor(QPalette::PlaceholderText, theme.overlay1);
	palette.setColor(QPalette::Text, theme.text);
	palette.setColor(QPalette::Button, theme.base);
	palette.setColor(QPalette::ButtonText, theme.text);
	palette.setColor(QPalette::BrightText, bright_text_colour);
	palette.setColor(QPalette::ToolTipBase, theme.mantle);
	palette.setColor(QPalette::ToolTipText, theme.overlay2);

	palette.setColor(QPalette::Highlight, highlighted_colour);
	palette.setColor(QPalette::HighlightedText, highlighted_text_colour);
	palette.setColor(QPalette::Link, theme.secondary);
	palette.setColor(QPalette::LinkVisited, theme.secondary);

	palette.setColor(QPalette::Light, theme.crust);
	palette.setColor(QPalette::Midlight, theme.mantle);
	palette.setColor(QPalette::Mid, theme.surface0);
	palette.setColor(QPalette::Dark, theme.surface1);
	palette.setColor(QPalette::Shadow, theme.overlay0);

	// Inactive
	palette.setColor(QPalette::Inactive, QPalette::Highlight, theme.surface1);
	palette.setColor(QPalette::Inactive, QPalette::Link, theme.surface1);
	palette.setColor(QPalette::Inactive, QPalette::LinkVisited, theme.surface1);

	// Disabled
	palette.setColor(QPalette::Disabled, QPalette::WindowText, theme.overlay1);
	palette.setColor(QPalette::Disabled, QPalette::Base, theme.base);
	palette.setColor(QPalette::Disabled, QPalette::AlternateBase, theme.base);
	palette.setColor(QPalette::Disabled, QPalette::Text, theme.overlay1);
	palette.setColor(QPalette::Disabled, QPalette::PlaceholderText, theme.overlay1);
	palette.setColor(QPalette::Disabled, QPalette::Button, theme.base);
	palette.setColor(QPalette::Disabled, QPalette::ButtonText, theme.overlay1);
	palette.setColor(QPalette::Disabled, QPalette::BrightText, theme.mantle);
	palette.setColor(QPalette::Disabled, QPalette::Highlight, theme.surface2);
	palette.setColor(QPalette::Disabled, QPalette::HighlightedText, theme.surface0);
	palette.setColor(QPalette::Disabled, QPalette::Link, theme.surface0);
	palette.setColor(QPalette::Disabled, QPalette::LinkVisited, theme.surface0);

#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
	palette.setColor(QPalette::Accent, theme.secondary);
	palette.setColor(QPalette::Inactive, QPalette::Accent, theme.surface1);
	palette.setColor(QPalette::Disabled, QPalette::Accent, theme.surface2);
#endif

	QApplication::setPalette(palette);
}

#if defined(_WIN32)
typedef HRESULT (WINAPI *DwmSetWindowAttributeFn)(HWND hwnd, DWORD dwAttribute, LPCVOID pvAttribute, DWORD cbAttribute);

static DwmSetWindowAttributeFn getDwmSetWindowAttributeFn()
{
	static DwmSetWindowAttributeFn fn = nullptr;
	static bool attempted_load = false;
	if(!attempted_load)
	{
		attempted_load = true;
		if(HMODULE dwmapi_module = ::LoadLibraryA("dwmapi.dll"))
			fn = reinterpret_cast<DwmSetWindowAttributeFn>(::GetProcAddress(dwmapi_module, "DwmSetWindowAttribute"));
	}
	return fn;
}


static bool setDwmWindowAttribute(HWND hwnd, DWORD attribute, const void* value, DWORD value_size)
{
	DwmSetWindowAttributeFn fn = getDwmSetWindowAttributeFn();
	return fn && (fn(hwnd, attribute, value, value_size) == S_OK);
}


static DWORD toColorRef(const QColor& colour)
{
	return RGB(colour.red(), colour.green(), colour.blue());
}


static void applyNativeWindowCaptionTheme(QWidget* widget, const QtThemeColors* theme)
{
	if(!widget)
		return;

	const HWND hwnd = reinterpret_cast<HWND>(widget->winId());
	if(!hwnd)
		return;

	// DWM attributes are available on modern Windows builds. If an attribute is unsupported, the call fails and we ignore it.
	static const DWORD DWMWA_USE_IMMERSIVE_DARK_MODE_NEW = 20;
	static const DWORD DWMWA_USE_IMMERSIVE_DARK_MODE_OLD = 19;
	static const DWORD DWMWA_BORDER_COLOR = 34;
	static const DWORD DWMWA_CAPTION_COLOR = 35;
	static const DWORD DWMWA_TEXT_COLOR = 36;
	static const DWORD DWM_COLOR_DEFAULT = 0xFFFFFFFFu;

	if(theme)
	{
		const bool dark_caption = theme->base.value() < 128;
		const BOOL immersive_dark = dark_caption ? TRUE : FALSE;
		if(!setDwmWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE_NEW, &immersive_dark, sizeof(immersive_dark)))
			(void)setDwmWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE_OLD, &immersive_dark, sizeof(immersive_dark));

		const DWORD caption_colour = toColorRef(theme->base);
		const DWORD border_colour = toColorRef(theme->surface1);
		const DWORD text_colour = toColorRef(theme->text);
		(void)setDwmWindowAttribute(hwnd, DWMWA_CAPTION_COLOR, &caption_colour, sizeof(caption_colour));
		(void)setDwmWindowAttribute(hwnd, DWMWA_BORDER_COLOR, &border_colour, sizeof(border_colour));
		(void)setDwmWindowAttribute(hwnd, DWMWA_TEXT_COLOR, &text_colour, sizeof(text_colour));
	}
	else
	{
		const BOOL immersive_dark = FALSE;
		if(!setDwmWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE_NEW, &immersive_dark, sizeof(immersive_dark)))
			(void)setDwmWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE_OLD, &immersive_dark, sizeof(immersive_dark));
		(void)setDwmWindowAttribute(hwnd, DWMWA_CAPTION_COLOR, &DWM_COLOR_DEFAULT, sizeof(DWM_COLOR_DEFAULT));
		(void)setDwmWindowAttribute(hwnd, DWMWA_BORDER_COLOR, &DWM_COLOR_DEFAULT, sizeof(DWM_COLOR_DEFAULT));
		(void)setDwmWindowAttribute(hwnd, DWMWA_TEXT_COLOR, &DWM_COLOR_DEFAULT, sizeof(DWM_COLOR_DEFAULT));
	}
}


static void applyNativeWindowCaptionThemeDeferred(QWidget* owner, const QtThemeColors* theme)
{
	QtThemeColors theme_copy;
	const bool has_theme = theme != nullptr;
	if(has_theme)
		theme_copy = *theme;

	const auto apply_to_windows = [has_theme, theme_copy]()
	{
		const QtThemeColors* use_theme = has_theme ? &theme_copy : nullptr;
		const QList<QWidget*> top_level_widgets = QApplication::topLevelWidgets();
		for(QWidget* widget : top_level_widgets)
			if(widget && widget->isWindow())
				applyNativeWindowCaptionTheme(widget, use_theme);
	};

	apply_to_windows();
	if(owner)
	{
		QTimer::singleShot(0, owner, apply_to_windows);
		QTimer::singleShot(250, owner, apply_to_windows);
	}
}
#endif


class MetasiberiaMapDockWidget : public QWidget
{
public:
	MetasiberiaMapDockWidget(GUIClient* gui_client_, QWidget* parent)
	: QWidget(parent),
	  gui_client(gui_client_),
	  refresh_timer(new QTimer(this)),
	  current_pos(0.0),
	  drag_start_pos(0.0),
	  current_heading_rad(0.0),
	  osm_zoom(16),
	  server_map_width_ws(500.f),
	  active(false),
	  follow_avatar(true),
	  dragging(false),
	  has_current_pos(false),
	  mode(TileMode_ServerWorld),
	  last_requested_tile_z(-1000),
	  next_tile_refresh_query_time(0.0),
	  last_requested_campos(Vec3d(-1000000.0)),
	  updating_coordinate_edit(false),
	  scratch_packet(SocketBufferOutStream::DontUseNetworkByteOrder)
	{
		setMinimumSize(280, 240);
		setMouseTracking(true);
		setFocusPolicy(Qt::WheelFocus);
		setAutoFillBackground(false);

		zoom_out_button = makeToolButton("-", tr("Уменьшить масштаб"));
		zoom_in_button = makeToolButton("+", tr("Увеличить масштаб"));
		reset_view_button = makeToolButton("1", tr("Сбросить масштаб"));
		recenter_button = makeToolButton("⌖", tr("Определить местоположение"));
		coordinate_edit = makeLineEdit(tr("Широта, долгота"), tr("Введите широту и долготу через запятую и нажмите Enter."));
		search_edit = makeLineEdit(tr("Поиск места"), tr("Введите название места и нажмите Enter."));
		search_button = makeToolButton("⌕", tr("Найти место"));
		connect(zoom_out_button, &QToolButton::clicked, this, [this]() { adjustZoom(-1); });
		connect(zoom_in_button, &QToolButton::clicked, this, [this]() { adjustZoom(1); });
		connect(reset_view_button, &QToolButton::clicked, this, [this]() { resetMapView(); });
		connect(recenter_button, &QToolButton::clicked, this, [this]() { recenterOnAvatar(); });
		connect(coordinate_edit, &QLineEdit::returnPressed, this, [this]() { visitCoordinateEditPosition(); });
		connect(search_edit, &QLineEdit::returnPressed, this, [this]() { searchAndVisitPlace(); });
		connect(search_button, &QToolButton::clicked, this, [this]() { searchAndVisitPlace(); });

		connect(refresh_timer, &QTimer::timeout, this, [this]() {
			refreshFromClient();
		});
		refresh_timer->start(160);

		positionToolButtons();
		positionBottomControls();
		updateInputControlVisibility();
	}

	void setMapActive(bool active_)
	{
		active = active_;
		updateInputControlVisibility();
		if(active)
			refreshFromClient();
		else
			update();
	}

	void handleMapTilesResultReceivedMessage(const MapTilesResultReceivedMessage& msg)
	{
		if(!active || mode != TileMode_ServerWorld)
			return;

		if(msg.tile_indices.size() != msg.tile_URLS.size())
			return;
		for(size_t i = 0; i < msg.tile_indices.size(); ++i)
		{
			const URLString& URL = msg.tile_URLS[i];
			if(!URL.empty())
			{
				server_tile_URLs[msg.tile_indices[i]] = URL;
				requestResourceForURL(URL, mapTileWorldPos(msg.tile_indices[i]), serverTileWidthWSForZ(msg.tile_indices[i].z), dockTileDownloadPriority(msg.tile_indices[i].z));
			}
		}
		update();
	}

protected:
	void paintEvent(QPaintEvent*) override
	{
		QPainter painter(this);
		painter.setRenderHint(QPainter::Antialiasing, true);
		painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
		painter.fillRect(rect(), QColor(244, 244, 240));

		if(!active || !gui_client || !gui_client->usesEmbeddedMapDock())
		{
			painter.setPen(QColor(60, 68, 78));
			painter.drawText(rect().adjusted(12, 12, -12, -12), Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
				tr("Карта доступна на vr.metasiberia.com."));
			return;
		}

		requestVisibleTiles();
		const bool drew_any_tile = (mode == TileMode_OSM) ? drawOSMTiles(painter) : drawServerWorldTiles(painter);
		if(!drew_any_tile)
		{
			painter.setPen(QColor(88, 94, 104));
			painter.drawText(rect(), Qt::AlignCenter, tr("Загрузка карты..."));
		}

		drawAvatarDots(painter);
		drawAvatarMarker(painter);
		drawHUD(painter);
		updateInputControlVisibility();
	}

	void wheelEvent(QWheelEvent* event) override
	{
		if(!active)
		{
			event->ignore();
			return;
		}

		if(event->angleDelta().y() != 0)
			adjustZoom(event->angleDelta().y() > 0 ? 1 : -1);
		event->accept();
	}

	void mousePressEvent(QMouseEvent* event) override
	{
		if(!active || event->button() != Qt::LeftButton)
		{
			QWidget::mousePressEvent(event);
			return;
		}

		dragging = true;
		drag_start_mouse_pos = event->pos();
		drag_start_pos = current_pos;
		follow_avatar = false;
		setCursor(Qt::ClosedHandCursor);
		event->accept();
	}

	void mouseMoveEvent(QMouseEvent* event) override
	{
		if(!dragging)
		{
			updateAvatarHoverTooltip(event);
			QWidget::mouseMoveEvent(event);
			return;
		}

		const QPoint delta = event->pos() - drag_start_mouse_pos;
		const double scale_px_per_m = pixelsPerWorldMetre();
		if(scale_px_per_m > 0.0)
		{
			current_pos.x = drag_start_pos.x - (double)delta.x() / scale_px_per_m;
			current_pos.y = drag_start_pos.y + (double)delta.y() / scale_px_per_m;
			requestVisibleTiles();
			updateCoordinateEditText();
			update();
		}
		event->accept();
	}

	void mouseReleaseEvent(QMouseEvent* event) override
	{
		if(dragging && event->button() == Qt::LeftButton)
		{
			dragging = false;
			unsetCursor();
			event->accept();
			return;
		}

		QWidget::mouseReleaseEvent(event);
	}

	void leaveEvent(QEvent* event) override
	{
		QToolTip::hideText();
		QWidget::leaveEvent(event);
	}

	void resizeEvent(QResizeEvent*) override
	{
		positionToolButtons();
		positionBottomControls();
	}

private:
	enum TileMode
	{
		TileMode_ServerWorld,
		TileMode_OSM
	};

	QToolButton* makeToolButton(const QString& text, const QString& tooltip)
	{
		QToolButton* button = new QToolButton(this);
		button->setText(text);
		button->setToolTip(tooltip);
		button->setAutoRaise(true);
		button->setFixedSize(24, 24);
		button->setStyleSheet(
			"QToolButton { background: rgba(255,255,255,230); border: 1px solid #aeb5c0; border-radius: 2px; color: #1f2937; font-weight: 600; }"
			"QToolButton:hover { background: #e7f1ff; border-color: #6fa5df; }"
			"QToolButton:pressed { background: #cfe4ff; }");
		button->raise();
		return button;
	}

	QLineEdit* makeLineEdit(const QString& placeholder, const QString& tooltip)
	{
		QLineEdit* edit = new QLineEdit(this);
		edit->setPlaceholderText(placeholder);
		edit->setToolTip(tooltip);
		edit->setFixedHeight(24);
		edit->setClearButtonEnabled(true);
		edit->setStyleSheet(
			"QLineEdit { background: rgba(255,255,255,235); border: 1px solid #c2c8d0; border-radius: 2px; color: #1f2937; padding: 2px 6px; }"
			"QLineEdit:focus { border-color: #4d90d9; background: #ffffff; }");
		edit->raise();
		return edit;
	}

	void positionToolButtons()
	{
		if(!zoom_in_button || !zoom_out_button || !reset_view_button || !recenter_button || !search_button)
			return;

		const int margin = 8;
		const int gap = 4;
		const int button_w = zoom_in_button->width();
		const int y = margin;
		int x = width() - margin - button_w;
		recenter_button->move(x, y);
		x -= button_w + gap;
		reset_view_button->move(x, y);
		x -= button_w + gap;
		zoom_in_button->move(x, y);
		x -= button_w + gap;
		zoom_out_button->move(x, y);

		zoom_out_button->raise();
		zoom_in_button->raise();
		reset_view_button->raise();
		recenter_button->raise();
	}

	void positionBottomControls()
	{
		if(!coordinate_edit || !search_edit || !search_button)
			return;

		const int margin = 8;
		const int gap = 4;
		const int control_h = coordinate_edit->height();
		const int y = height() - margin - control_h;
		const int search_button_w = search_button->width();
		const int available_w = myMax(0, width() - margin * 2 - gap * 2 - search_button_w);
		const int coord_w = myClamp((int)(available_w * 0.48), 118, myMax(118, available_w - 90));
		const int search_w = myMax(80, available_w - coord_w);

		int x = margin;
		coordinate_edit->setGeometry(x, y, coord_w, control_h);
		x += coord_w + gap;
		search_edit->setGeometry(x, y, search_w, control_h);
		x += search_w + gap;
		search_button->move(x, y);

		coordinate_edit->raise();
		search_edit->raise();
		search_button->raise();
	}

	void updateInputControlVisibility()
	{
		const bool show_map_inputs = active && (mode == TileMode_OSM) && gui_client && gui_client->usesEmbeddedMapDock();
		if(coordinate_edit)
			coordinate_edit->setVisible(show_map_inputs);
		if(search_edit)
			search_edit->setVisible(show_map_inputs);
		if(search_button)
			search_button->setVisible(show_map_inputs);
	}

	static int wrappedTileX(int tile_x, int tile_z)
	{
		const int n = 1 << tile_z;
		return Maths::intMod(tile_x, n);
	}

	static int clampedTileY(int tile_y, int tile_z)
	{
		const int n = 1 << tile_z;
		return myClamp(tile_y, 0, n - 1);
	}

	static int serverTileZForMapWidthWS(float map_width_ws)
	{
		return myClamp((int)std::log2(2 * 5120 / map_width_ws), 0, 6);
	}

	static float serverTileWidthWSForZ(int tile_z)
	{
		return 5120.f / (1 << tile_z);
	}

	static int dockTileDownloadPriority(int tile_z)
	{
		return 400 + tile_z;
	}

	static Vec3d mapTileWorldPos(const Vec3i& tile)
	{
		if(tile.z < 0)
			return Vec3d(0.0);

		const double tile_w = serverTileWidthWSForZ(tile.z);
		return Vec3d(((double)tile.x + 0.5) * tile_w, ((double)tile.y + 0.5) * tile_w, 0.0);
	}

	URLString osmTileURL(int tile_x, int tile_y, int tile_z) const
	{
		return MapWorldUtils::makeOSMTileURL(
			gui_client ? gui_client->server_hostname : std::string("vr.metasiberia.com"),
			wrappedTileX(tile_x, tile_z),
			clampedTileY(tile_y, tile_z),
			tile_z);
	}

	void refreshFromClient()
	{
		if(!gui_client || !gui_client->usesEmbeddedMapDock())
		{
			if(active)
			{
				active = false;
				update();
			}
			return;
		}

		const TileMode new_mode = gui_client->isMetasiberiaMapWorld() ? TileMode_OSM : TileMode_ServerWorld;
		if(new_mode != mode)
		{
			mode = new_mode;
			clearTransientMapState();
			updateInputControlVisibility();
		}

		active = true;
		if(follow_avatar || !has_current_pos)
		{
			current_pos = gui_client->cam_controller.getFirstPersonPosition();
			has_current_pos = true;
		}
		current_heading_rad = gui_client->cam_controller.getAngles().x;
		updateCoordinateEditText();
		requestVisibleTiles();
		update();
	}

	void clearTransientMapState()
	{
		tile_pixmaps.clear();
		requested_resource_URLs.clear();
		server_tile_URLs.clear();
		queried_server_tile_coords.clear();
		last_requested_tile_z = -1000;
		next_tile_refresh_query_time = 0.0;
		last_requested_campos = Vec3d(-1000000.0);
		follow_avatar = true;
		has_current_pos = false;
	}

	void recenterOnAvatar()
	{
		follow_avatar = true;
		has_current_pos = false;
		refreshFromClient();
	}

	void resetMapView()
	{
		if(mode == TileMode_OSM)
			osm_zoom = 16;
		else
		{
			server_map_width_ws = 500.f;
			last_requested_tile_z = -1000;
		}

		recenterOnAvatar();
	}

	void visitMapLatLon(double lat, double lon, const std::string& notification_suffix)
	{
		if(!gui_client)
			return;

		const Vec3d player_pos = gui_client->cam_controller.getFirstPersonPosition();
		const double target_z = player_pos.z > 0.1 ? player_pos.z : PlayerPhysics::getEyeHeight();
		const double heading_deg = Maths::doubleMod(::radToDegree(gui_client->cam_controller.getAngles().x), 360.0);
		gui_client->visitSubURL(makeMetasiberiaMapLatLonURL(lat, lon, target_z, heading_deg));
		if(!notification_suffix.empty())
			gui_client->showInfoNotification(notification_suffix);
		follow_avatar = true;
		has_current_pos = false;
		refreshFromClient();
	}

	void visitCoordinateEditPosition()
	{
		if(!coordinate_edit || updating_coordinate_edit)
			return;

		double lat = 0.0;
		double lon = 0.0;
		if(!tryParseMapLatLonInput(QtUtils::toIndString(coordinate_edit->text()), lat, lon))
		{
			if(gui_client)
				gui_client->showErrorNotification("Введите координаты в формате: 53.69171, 87.43290");
			return;
		}

		visitMapLatLon(lat, lon, "Переход к координатам карты.");
	}

	void searchAndVisitPlace()
	{
		if(!search_edit)
			return;

		const std::string query = stripHeadAndTailWhitespace(QtUtils::toIndString(search_edit->text()));
		if(query.empty())
			return;

		double lat = 0.0;
		double lon = 0.0;
		std::string label;
		std::string error;
		if(!lookupMetasiberiaMapPlaceName(query, lat, lon, label, error))
		{
			if(gui_client)
				gui_client->showErrorNotification(error.empty() ? "Место не найдено." : error);
			return;
		}

		visitMapLatLon(lat, lon, label.empty() ? ("Найдено: " + query) : ("Найдено: " + label));
	}

	void adjustZoom(int delta)
	{
		if(!active)
			return;

		if(mode == TileMode_OSM)
		{
			const int old_zoom = osm_zoom;
			osm_zoom = myClamp(osm_zoom + delta, 8, 18);
			if(osm_zoom == old_zoom)
				return;
		}
		else
		{
			const float old_width = server_map_width_ws;
			server_map_width_ws = myClamp(server_map_width_ws * (delta > 0 ? 0.5f : 2.0f), 80.f, 5120.f);
			if(server_map_width_ws == old_width)
				return;
			last_requested_tile_z = -1000;
		}

		requestVisibleTiles();
		update();
	}

	double pixelsPerWorldMetre() const
	{
		if(mode == TileMode_OSM)
			return 256.0 / MapWorldUtils::getOSMTileWidthWSForTileZ(osm_zoom);
		else
			return (double)myMin(width(), height()) / (double)server_map_width_ws;
	}

	void requestVisibleTiles()
	{
		if(!active || width() <= 0 || height() <= 0)
			return;

		if(mode == TileMode_OSM)
			requestVisibleOSMTiles();
		else
			requestVisibleServerWorldTiles();
	}

	void requestResourceForURL(const URLString& URL, const Vec3d& pos, float tile_width_ws, int priority)
	{
		if(URL.empty() || !gui_client || gui_client->resource_manager.isNull())
			return;

		const QString key = QString::fromStdString(toStdString(URL));
		if(tile_pixmaps.contains(key))
			return;

		ResourceRef resource = gui_client->resource_manager->getExistingResourceForURL(URL);
		if(resource.nonNull() && resource->getState() == Resource::State_Present)
			return;

		if(requested_resource_URLs.count(URL) != 0)
			return;

		DownloadingResourceInfo downloading_info;
		downloading_info.pos = pos;
		downloading_info.size_factor = LoadItemQueueItem::sizeFactorForAABBWS(tile_width_ws, /*importance_factor=*/1.f);
		downloading_info.used_by_other = true;
		downloading_info.net_download_priority = priority;
		gui_client->startDownloadingResource(URL, pos.toVec4fPoint(), tile_width_ws, downloading_info);
		requested_resource_URLs.insert(URL);
	}

	static int clampColourChannel(double value)
	{
		return myClamp((int)std::floor(value + 0.5), 0, 255);
	}

	static QPixmap enhancedMapPixmap(const QPixmap& source_pixmap, bool real_map_tiles)
	{
		if(source_pixmap.isNull())
			return source_pixmap;

		QImage image = source_pixmap.toImage().convertToFormat(QImage::Format_ARGB32);
		const double contrast = real_map_tiles ? 1.22 : 1.12;
		const double saturation = real_map_tiles ? 1.18 : 1.08;
		for(int y = 0; y < image.height(); ++y)
		{
			QRgb* pixels = reinterpret_cast<QRgb*>(image.scanLine(y));
			for(int x = 0; x < image.width(); ++x)
			{
				const QRgb pixel = pixels[x];
				const int alpha = qAlpha(pixel);
				const double source_r = qRed(pixel);
				const double source_g = qGreen(pixel);
				const double source_b = qBlue(pixel);
				const double source_luma = source_r * 0.299 + source_g * 0.587 + source_b * 0.114;

				double r = (source_r - 128.0) * contrast + 128.0;
				double g = (source_g - 128.0) * contrast + 128.0;
				double b = (source_b - 128.0) * contrast + 128.0;

				const double luma = r * 0.299 + g * 0.587 + b * 0.114;
				r = luma + (r - luma) * saturation;
				g = luma + (g - luma) * saturation;
				b = luma + (b - luma) * saturation;

				if(real_map_tiles && source_luma < 248.0)
				{
					const double darken = (248.0 - source_luma) * 0.08;
					r -= darken;
					g -= darken;
					b -= darken;
				}

				pixels[x] = qRgba(clampColourChannel(r), clampColourChannel(g), clampColourChannel(b), alpha);
			}
		}

		return QPixmap::fromImage(image);
	}

	QPixmap pixmapForURL(const URLString& URL)
	{
		if(URL.empty() || !gui_client || gui_client->resource_manager.isNull())
			return QPixmap();

		const QString key = QString::fromStdString(toStdString(URL));
		const QPixmap cached = tile_pixmaps.value(key);
		if(!cached.isNull())
			return cached;

		ResourceRef resource = gui_client->resource_manager->getExistingResourceForURL(URL);
		if(resource.isNull() || resource->getState() != Resource::State_Present)
			return QPixmap();

		const std::string local_path = gui_client->resource_manager->getLocalAbsPathForResource(*resource);
		QPixmap pixmap(QtUtils::toQString(local_path));
		if(!pixmap.isNull())
		{
			pixmap = enhancedMapPixmap(pixmap, mode == TileMode_OSM);
			if(tile_pixmaps.size() > 900)
				tile_pixmaps.clear();
			tile_pixmaps.insert(key, pixmap);
		}
		return pixmap;
	}

	void requestOSMTileAndParents(int tile_x, int tile_y, int tile_z, std::set<Vec3i>& requested_tile_coords)
	{
		const int min_z = myMax(0, tile_z - 3);
		for(int z = tile_z; z >= min_z; --z)
		{
			tile_x = wrappedTileX(tile_x, z);
			tile_y = clampedTileY(tile_y, z);
			if(!MapWorldUtils::isValidOSMTileCoord(tile_x, tile_y, z))
				return;

			const Vec3i tile_coords(tile_x, tile_y, z);
			if(requested_tile_coords.insert(tile_coords).second)
			{
				const URLString URL = osmTileURL(tile_x, tile_y, z);
				const Vec2d tile_centre = MapWorldUtils::getOSMTileCentreLocalCoords(tile_x, tile_y, z);
				requestResourceForURL(URL, Vec3d(tile_centre.x, tile_centre.y, 0.0), MapWorldUtils::getOSMTileWidthWSForTileZ(z), dockTileDownloadPriority(z));
			}

			tile_x = Maths::divideByTwoRoundedDown(tile_x);
			tile_y = Maths::divideByTwoRoundedDown(tile_y);
		}
	}

	void requestVisibleOSMTiles()
	{
		const int centre_tile_x = MapWorldUtils::getOSMTileXForLocalX(current_pos.x, osm_zoom);
		const int centre_tile_y = MapWorldUtils::getOSMTileYForLocalY(current_pos.y, osm_zoom);
		const int radius_x = myClamp(width() / 256 / 2 + 2, 2, 6);
		const int radius_y = myClamp(height() / 256 / 2 + 2, 2, 6);
		std::set<Vec3i> requested_tile_coords;

		for(int dy = -radius_y; dy <= radius_y; ++dy)
		for(int dx = -radius_x; dx <= radius_x; ++dx)
		{
			const int tile_x = wrappedTileX(centre_tile_x + dx, osm_zoom);
			const int tile_y = clampedTileY(centre_tile_y + dy, osm_zoom);
			if(!MapWorldUtils::isValidOSMTileCoord(tile_x, tile_y, osm_zoom))
				continue;

			requestOSMTileAndParents(tile_x, tile_y, osm_zoom, requested_tile_coords);
		}
	}

	void requestVisibleServerWorldTiles()
	{
		if(!gui_client || !gui_client->client_thread || gui_client->server_protocol_version < 39)
			return;

		const Vec3d campos = current_pos;
		const int tile_z = serverTileZForMapWidthWS(server_map_width_ws);
		const float tile_w_ws = serverTileWidthWSForZ(tile_z);
		const int centre_tile_x = Maths::floorToInt((float)campos.x / tile_w_ws);
		const int centre_tile_y = Maths::floorToInt((float)campos.y / tile_w_ws);
		const double now = Clock::getTimeSinceInit();
		const bool force_refresh_query = now >= next_tile_refresh_query_time;

		const double tile_px = tile_w_ws * pixelsPerWorldMetre();
		const int radius_x = myClamp((int)std::ceil(width() / myMax(tile_px, 1.0) * 0.5) + 2, 2, 8);
		const int radius_y = myClamp((int)std::ceil(height() / myMax(tile_px, 1.0) * 0.5) + 2, 2, 8);

		if((campos.getDist(last_requested_campos) <= tile_w_ws * 0.25) && (tile_z == last_requested_tile_z) && !force_refresh_query)
			return;

		std::vector<Vec3i> query_indices;
		query_indices.reserve((size_t)(radius_x * 2 + 1) * (size_t)(radius_y * 2 + 1) * 4);

		for(int y = centre_tile_y - radius_y; y <= centre_tile_y + radius_y; ++y)
		for(int x = centre_tile_x - radius_x; x <= centre_tile_x + radius_x; ++x)
		{
			Vec3i tile_coords(x, y, tile_z);
			while(tile_coords.z >= 0)
			{
				if(force_refresh_query || queried_server_tile_coords.count(tile_coords) == 0)
				{
					query_indices.push_back(tile_coords);
					queried_server_tile_coords.insert(tile_coords);
				}

				tile_coords.x = Maths::divideByTwoRoundedDown(tile_coords.x);
				tile_coords.y = Maths::divideByTwoRoundedDown(tile_coords.y);
				tile_coords.z--;
			}
		}

		if(!query_indices.empty())
		{
			MessageUtils::initPacket(scratch_packet, Protocol::QueryMapTiles);
			scratch_packet.writeUInt32((uint32)query_indices.size());
			scratch_packet.writeData(query_indices.data(), query_indices.size() * sizeof(Vec3i));
			MessageUtils::updatePacketLengthField(scratch_packet);
			gui_client->client_thread->enqueueDataToSend(scratch_packet.buf);
		}

		last_requested_campos = campos;
		last_requested_tile_z = tile_z;
		if(force_refresh_query)
			next_tile_refresh_query_time = now + 10.0;
	}

	bool findOSMTilePixmap(int tile_x, int tile_y, int tile_z, QPixmap& pixmap_out, QRectF& source_rect_out)
	{
		Vec2d source_top_left(0.0);
		double source_scale = 1.0;

		const int min_z = myMax(0, tile_z - 3);
		for(int z = tile_z; z >= min_z; --z)
		{
			tile_x = wrappedTileX(tile_x, z);
			tile_y = clampedTileY(tile_y, z);
			if(!MapWorldUtils::isValidOSMTileCoord(tile_x, tile_y, z))
				return false;

			const URLString URL = osmTileURL(tile_x, tile_y, z);
			const QPixmap pixmap = pixmapForURL(URL);
			if(!pixmap.isNull())
			{
				pixmap_out = pixmap;
				source_rect_out = QRectF(
					source_top_left.x * pixmap.width(),
					source_top_left.y * pixmap.height(),
					source_scale * pixmap.width(),
					source_scale * pixmap.height());
				return true;
			}

			const Vec2d tile_centre = MapWorldUtils::getOSMTileCentreLocalCoords(tile_x, tile_y, z);
			requestResourceForURL(URL, Vec3d(tile_centre.x, tile_centre.y, 0.0), MapWorldUtils::getOSMTileWidthWSForTileZ(z), dockTileDownloadPriority(z));

			source_top_left.x = (double)Maths::intMod(tile_x, 2) * 0.5 + source_top_left.x * 0.5;
			source_top_left.y = (double)Maths::intMod(tile_y, 2) * 0.5 + source_top_left.y * 0.5;
			source_scale *= 0.5;
			tile_x = Maths::divideByTwoRoundedDown(tile_x);
			tile_y = Maths::divideByTwoRoundedDown(tile_y);
		}

		return false;
	}

	bool drawOSMTiles(QPainter& painter)
	{
		const int centre_tile_x = MapWorldUtils::getOSMTileXForLocalX(current_pos.x, osm_zoom);
		const int centre_tile_y = MapWorldUtils::getOSMTileYForLocalY(current_pos.y, osm_zoom);
		const int radius_x = myClamp(width() / 256 / 2 + 2, 2, 6);
		const int radius_y = myClamp(height() / 256 / 2 + 2, 2, 6);
		const double tile_width_ws = MapWorldUtils::getOSMTileWidthWSForTileZ(osm_zoom);

		bool drew_any_tile = false;
		for(int dy = -radius_y; dy <= radius_y; ++dy)
		for(int dx = -radius_x; dx <= radius_x; ++dx)
		{
			const int tile_x = wrappedTileX(centre_tile_x + dx, osm_zoom);
			const int tile_y = clampedTileY(centre_tile_y + dy, osm_zoom);
			if(!MapWorldUtils::isValidOSMTileCoord(tile_x, tile_y, osm_zoom))
				continue;

			QPixmap pixmap;
			QRectF source_rect;
			if(!findOSMTilePixmap(tile_x, tile_y, osm_zoom, pixmap, source_rect))
				continue;

			const Vec2d tile_centre = MapWorldUtils::getOSMTileCentreLocalCoords(tile_x, tile_y, osm_zoom);
			const double draw_x = width()  * 0.5 + ((tile_centre.x - current_pos.x) / tile_width_ws) * 256.0 - 128.0;
			const double draw_y = height() * 0.5 - ((tile_centre.y - current_pos.y) / tile_width_ws) * 256.0 - 128.0;
			painter.drawPixmap(QRectF(draw_x, draw_y, 256.0, 256.0), pixmap, source_rect);
			drew_any_tile = true;
		}

		return drew_any_tile;
	}

	bool findServerTilePixmap(int tile_x, int tile_y, int tile_z, QPixmap& pixmap_out, QRectF& source_rect_out)
	{
		Vec2f lower_left_coords(0.f);
		float scale = 1.f;

		for(int z = tile_z; z >= 0; --z)
		{
			const Vec3i indices(tile_x, tile_y, z);
			auto res = server_tile_URLs.find(indices);
			if(res != server_tile_URLs.end() && !res->second.empty())
			{
				const URLString URL = res->second;
				const QPixmap pixmap = pixmapForURL(URL);
				if(!pixmap.isNull())
				{
					const double sx = lower_left_coords.x * pixmap.width();
					const double sy = (1.0 - lower_left_coords.y - scale) * pixmap.height();
					const double sw = scale * pixmap.width();
					const double sh = scale * pixmap.height();
					pixmap_out = pixmap;
					source_rect_out = QRectF(sx, sy, sw, sh);
					return true;
				}

				requestResourceForURL(URL, mapTileWorldPos(indices), serverTileWidthWSForZ(z), dockTileDownloadPriority(z));
			}

			lower_left_coords *= 0.5f;
			lower_left_coords.x += (float)Maths::intMod(tile_x, 2) * 0.5f;
			lower_left_coords.y += (float)Maths::intMod(tile_y, 2) * 0.5f;
			tile_x = Maths::divideByTwoRoundedDown(tile_x);
			tile_y = Maths::divideByTwoRoundedDown(tile_y);
			scale *= 0.5f;
		}

		return false;
	}

	bool drawServerWorldTiles(QPainter& painter)
	{
		const int tile_z = serverTileZForMapWidthWS(server_map_width_ws);
		const float tile_w_ws = serverTileWidthWSForZ(tile_z);
		const double scale_px_per_m = pixelsPerWorldMetre();
		const double tile_px = tile_w_ws * scale_px_per_m;
		const int centre_tile_x = Maths::floorToInt((float)current_pos.x / tile_w_ws);
		const int centre_tile_y = Maths::floorToInt((float)current_pos.y / tile_w_ws);
		const int radius_x = myClamp((int)std::ceil(width() / myMax(tile_px, 1.0) * 0.5) + 2, 2, 8);
		const int radius_y = myClamp((int)std::ceil(height() / myMax(tile_px, 1.0) * 0.5) + 2, 2, 8);

		bool drew_any_tile = false;
		for(int y = centre_tile_y - radius_y; y <= centre_tile_y + radius_y; ++y)
		for(int x = centre_tile_x - radius_x; x <= centre_tile_x + radius_x; ++x)
		{
			QPixmap pixmap;
			QRectF source_rect;
			if(!findServerTilePixmap(x, y, tile_z, pixmap, source_rect))
				continue;

			const double tile_centre_x = ((double)x + 0.5) * tile_w_ws;
			const double tile_centre_y = ((double)y + 0.5) * tile_w_ws;
			const double draw_x = width()  * 0.5 + (tile_centre_x - current_pos.x) * scale_px_per_m - tile_px * 0.5;
			const double draw_y = height() * 0.5 - (tile_centre_y - current_pos.y) * scale_px_per_m - tile_px * 0.5;
			painter.drawPixmap(QRectF(draw_x, draw_y, tile_px, tile_px), pixmap, source_rect);
			drew_any_tile = true;
		}

		return drew_any_tile;
	}

	void drawAvatarMarker(QPainter& painter)
	{
		// Keep the player marker locked to the map viewport; dragging moves the map underneath it.
		const Vec3d forward = gui_client ? gui_client->cam_controller.getForwardsVec() : Vec3d(0, 1, 0);
		const double forward_len_xy = std::sqrt(forward.x * forward.x + forward.y * forward.y);
		const double marker_rotation_deg = (forward_len_xy > 1.0e-6) ? std::atan2(forward.x, forward.y) * 180.0 / 3.14159265358979323846 : 0.0;

		painter.save();
		painter.translate(width() * 0.5, height() * 0.5);
		painter.rotate(marker_rotation_deg);

		QPolygonF arrow;
		arrow << QPointF(0, -12) << QPointF(7, 9) << QPointF(0, 5) << QPointF(-7, 9);
		painter.setPen(QPen(QColor(255, 255, 255), 2));
		painter.setBrush(QColor(34, 100, 210));
		painter.drawPolygon(arrow);
		painter.restore();
	}

	void drawAvatarDots(QPainter& painter)
	{
		if(!gui_client || gui_client->world_state.isNull())
			return;

		const double scale_px_per_m = pixelsPerWorldMetre();
		if(scale_px_per_m <= 0.0)
			return;

		Lock lock(gui_client->world_state->mutex);
		for(auto it = gui_client->world_state->avatars.begin(); it != gui_client->world_state->avatars.end(); ++it)
		{
			const AvatarRef& avatar = it->second;
			if(avatar.isNull() || avatar->uid == gui_client->client_avatar_uid)
				continue;

			const double x = width()  * 0.5 + (avatar->pos.x - current_pos.x) * scale_px_per_m;
			const double y = height() * 0.5 - (avatar->pos.y - current_pos.y) * scale_px_per_m;
			if(x < -8.0 || x > width() + 8.0 || y < -8.0 || y > height() + 8.0)
				continue;

			const bool is_bot = avatar->isChatBotAvatar();
			painter.setPen(QPen(QColor(28, 32, 38, 210), 1));
			painter.setBrush(is_bot ? QColor(255, 255, 255) : QColor(230, 38, 38));
			painter.drawEllipse(QPointF(x, y), is_bot ? 4.2 : 4.6, is_bot ? 4.2 : 4.6);
		}
	}

	QPointF avatarMapPoint(const Vec3d& avatar_pos) const
	{
		const double scale_px_per_m = pixelsPerWorldMetre();
		return QPointF(
			width()  * 0.5 + (avatar_pos.x - current_pos.x) * scale_px_per_m,
			height() * 0.5 - (avatar_pos.y - current_pos.y) * scale_px_per_m);
	}

	bool avatarNameAtMapPoint(const QPoint& pos, QString& name_out) const
	{
		if(!gui_client || gui_client->world_state.isNull())
			return false;

		const double scale_px_per_m = pixelsPerWorldMetre();
		if(scale_px_per_m <= 0.0)
			return false;

		const QPointF cursor_pos(pos);
		const double hit_radius_px = 9.0;
		double best_dist2 = hit_radius_px * hit_radius_px;
		bool found = false;

		Lock lock(gui_client->world_state->mutex);
		for(auto it = gui_client->world_state->avatars.begin(); it != gui_client->world_state->avatars.end(); ++it)
		{
			const AvatarRef& avatar = it->second;
			if(avatar.isNull() || avatar->uid == gui_client->client_avatar_uid)
				continue;

			const QPointF avatar_pos = avatarMapPoint(avatar->pos);
			const double dx = avatar_pos.x() - cursor_pos.x();
			const double dy = avatar_pos.y() - cursor_pos.y();
			const double dist2 = dx * dx + dy * dy;
			if(dist2 <= best_dist2)
			{
				best_dist2 = dist2;
				name_out = QtUtils::toQString(avatar->getUseName());
				found = true;
			}
		}

		return found;
	}

	void updateAvatarHoverTooltip(QMouseEvent* event)
	{
		if(!active)
		{
			QToolTip::hideText();
			return;
		}

		QString avatar_name;
		if(avatarNameAtMapPoint(event->pos(), avatar_name))
			QToolTip::showText(event->globalPos(), avatar_name, this);
		else
			QToolTip::hideText();
	}

	void drawHUD(QPainter& painter)
	{
		if(mode == TileMode_OSM)
		{
			updateCoordinateEditText();
			return;
		}

		QString text;
		text = QString("z%1  x %2  y %3")
			.arg(serverTileZForMapWidthWS(server_map_width_ws))
			.arg(current_pos.x, 0, 'f', 1)
			.arg(current_pos.y, 0, 'f', 1);

		const QRect label_rect = QRect(8, height() - 28, width() - 16, 20);
		painter.fillRect(label_rect.adjusted(-4, -2, 4, 2), QColor(255, 255, 255, 210));
		painter.setPen(QColor(43, 51, 64));
		painter.drawText(label_rect, Qt::AlignLeft | Qt::AlignVCenter, text);
	}

	void updateCoordinateEditText()
	{
		if(!coordinate_edit || coordinate_edit->hasFocus())
			return;

		double lat = 0.0;
		double lon = 0.0;
		MapWorldUtils::localCoordsToLatLon(current_pos.x, current_pos.y, lat, lon);
		updating_coordinate_edit = true;
		coordinate_edit->setText(QString("%1, %2").arg(lat, 0, 'f', 5).arg(lon, 0, 'f', 5));
		updating_coordinate_edit = false;
	}

	GUIClient* gui_client;
	QTimer* refresh_timer;
	QToolButton* zoom_out_button;
	QToolButton* zoom_in_button;
	QToolButton* reset_view_button;
	QToolButton* recenter_button;
	QLineEdit* coordinate_edit;
	QLineEdit* search_edit;
	QToolButton* search_button;
	QHash<QString, QPixmap> tile_pixmaps;
	std::unordered_set<URLString, URLStringHasher> requested_resource_URLs;
	std::map<Vec3i, URLString> server_tile_URLs;
	std::set<Vec3i> queried_server_tile_coords;
	Vec3d current_pos;
	Vec3d drag_start_pos;
	QPoint drag_start_mouse_pos;
	double current_heading_rad;
	int osm_zoom;
	float server_map_width_ws;
	bool active;
	bool follow_avatar;
	bool dragging;
	bool has_current_pos;
	TileMode mode;
	int last_requested_tile_z;
	double next_tile_refresh_query_time;
	Vec3d last_requested_campos;
	bool updating_coordinate_edit;
	SocketBufferOutStream scratch_packet;
};


static void installEditorDockTitleBar(QDockWidget* dock_widget)
{
	if(!dock_widget)
		return;

	QWidget* title_bar = new QWidget(dock_widget);
	title_bar->setObjectName(QStringLiteral("editorDockTitleBar"));
	title_bar->setAttribute(Qt::WA_StyledBackground, true);
	title_bar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

	QHBoxLayout* layout = new QHBoxLayout(title_bar);
	layout->setContentsMargins(6, 2, 3, 2);
	layout->setSpacing(2);

	QLabel* title_label = new QLabel(dock_widget->windowTitle(), title_bar);
	title_label->setObjectName(QStringLiteral("editorDockTitleLabel"));
	title_label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
	title_label->setAttribute(Qt::WA_TransparentForMouseEvents);
	layout->addWidget(title_label, 1);

	QToolButton* float_button = new QToolButton(title_bar);
	float_button->setObjectName(QStringLiteral("editorDockFloatButton"));
	float_button->setAutoRaise(true);
	float_button->setFixedSize(18, 18);
	float_button->setIconSize(QSize(12, 12));
	float_button->setIcon(dock_widget->style()->standardIcon(QStyle::SP_TitleBarNormalButton));
	float_button->setToolTip(QCoreApplication::translate("MainWindow", "Dock or undock this panel"));
	layout->addWidget(float_button);

	QToolButton* close_button = new QToolButton(title_bar);
	close_button->setObjectName(QStringLiteral("editorDockCloseButton"));
	close_button->setAutoRaise(true);
	close_button->setFixedSize(18, 18);
	close_button->setIconSize(QSize(12, 12));
	close_button->setIcon(dock_widget->style()->standardIcon(QStyle::SP_TitleBarCloseButton));
	close_button->setToolTip(QCoreApplication::translate("MainWindow", "Close"));
	layout->addWidget(close_button);

	QObject::connect(dock_widget, &QDockWidget::windowTitleChanged, title_label, &QLabel::setText);
	QObject::connect(float_button, &QToolButton::clicked, dock_widget, [dock_widget]() {
		dock_widget->setFloating(!dock_widget->isFloating());
	});
	QObject::connect(close_button, &QToolButton::clicked, dock_widget, [dock_widget]() { dock_widget->close(); });

	dock_widget->setTitleBarWidget(title_bar);
}


static void dockMetasiberiaMapLikeChat(QMainWindow* main_window, QDockWidget* map_dock_widget, QDockWidget* chat_dock_widget)
{
	if(!main_window || !map_dock_widget)
		return;

	map_dock_widget->setFloating(false);
	if(main_window->dockWidgetArea(map_dock_widget) != Qt::RightDockWidgetArea)
		main_window->addDockWidget(Qt::RightDockWidgetArea, map_dock_widget, Qt::Vertical);

	if(chat_dock_widget && !chat_dock_widget->isFloating())
	{
		if(main_window->dockWidgetArea(chat_dock_widget) != Qt::RightDockWidgetArea)
			main_window->addDockWidget(Qt::RightDockWidgetArea, chat_dock_widget, Qt::Vertical);

		main_window->splitDockWidget(map_dock_widget, chat_dock_widget, Qt::Vertical);
	}
}
} // namespace


MainWindow::MainWindow(const std::string& base_dir_path_, const std::string& appdata_path_, const ArgumentParser& args, QWidget* parent)
:	base_dir_path(base_dir_path_),
	appdata_path(appdata_path_),
	parsed_args(args),
	QMainWindow(parent),
	theme_action_group(NULL),
	language_action_group(NULL),
	runtime_translator(NULL),
	current_ui_language(RuntimeTranslation::UILanguage::English),
	last_timerEvent_CPU_work_elapsed(0.0),
	last_updateGL_time(0.0),
	last_xr_companion_update_time(-1.0),
	default_qt_style_name_set(false),
	need_help_info_dock_widget_position(false),
	log_window(NULL),
	in_CEF_message_loop(false),
	should_close(false),
	closing(false),
	main_timer_id(0),
	gui_client(base_dir_path_, appdata_path_, args),
	run_as_screenshot_slave(false),
	taking_map_screenshot(false),
	taking_gear_screenshot(false),
	screenshot_gear_model_load_started(false),
	test_screenshot_taking(false),
	screenshot_target_worldname_set(false),
	running_destructor(false),
	scratch_packet(SocketBufferOutStream::DontUseNetworkByteOrder),
	settings(NULL),
	user_details(NULL),
	url_widget(NULL),
	ui(NULL),
	minidump_sender(NULL),
	update_manager(NULL),
	chat_emoji_popup(NULL),
	chat_emoji_tab_widget(NULL),
	chat_user_combo(NULL),
	chat_private_button(NULL),
	chat_goto_user_button(NULL),
	chat_tabs_bar(NULL),
	chat_users_list_layout(NULL),
	chat_player_search_edit(NULL),
	chat_online_count_label(NULL),
	chat_messages_scroll_area(NULL),
	chat_messages_list_layout(NULL),
	chat_users_panel(NULL),
	chat_main_panel(NULL),
	chat_input_panel(NULL),
	chat_body_splitter(NULL),
	chat_messages_page(NULL),
	chat_groups_page(NULL),
	chat_groups_list_layout(NULL),
	chat_settings_page(NULL),
	chat_notifications_enabled(true),
	chat_show_timestamps(true),
	chat_compact_message_view(false),
	chat_network_private_messages_enabled(true),
	chat_showing_private_messages(false),
	chat_private_conversation_open(false),
	chat_switching_to_private_conversation(false),
	chat_message_counter(0),
	chat_unread_count(0),
	chat_private_unread_count(0),
	chat_loading_history(false),
	chat_private_recipient_uid(UID::invalidUID()),
	webcam_window(NULL)
	,avatar_dock_widget(NULL)
	,avatar_settings_widget(NULL)
	,map_dock_widget(NULL)
	,map_dock_map_widget(NULL)
	,animation_editor_dock_widget(NULL)
	,animation_editor_panel(NULL)
	,photo_video_dock_widget(NULL)
	,photo_video_settings_panel(NULL)
	,native_photo_video_gl_ready(false)
	,document_editor_dock_widget(NULL)
	,document_editor_panel(NULL)
	,scientific_object_editor(NULL)
	,cultural_object_editor(NULL)
	,tree_editor_panel(NULL)
	,voxel_editor_panel(NULL)
	,gear_inventory_panel(NULL)
	,gear_inventory_refresh_pending(false)
	,action_add_tree(NULL)
	,action_add_scientific_object(NULL)
	,action_add_cultural_object(NULL)
	,action_add_document(NULL)
	,active_editor_kind(ActiveEditor_Object)
	//game_controller(NULL)
{
	ZoneScoped; // Tracy profiler

	settings = new QSettings("Glare Technologies", "Cyberspace");

	credential_manager.loadFromSettings(*settings);

	// Create main task manager.
	// This is for doing work like texture compression and EXR loading, that will be created by LoadTextureTasks etc.
	// Alloc these on the heap as Emscripten may have issues with stack-allocated objects before the emscripten_set_main_loop() call.
	const size_t main_task_manager_num_threads = myClamp<size_t>(PlatformUtils::getNumLogicalProcessors(), 1, 512);
	main_task_manager = new glare::TaskManager("main task manager", main_task_manager_num_threads);
	main_task_manager->setThreadPriorities(MyThread::Priority_Lowest);


	// Create high-priority task manager.
	// For short, processor intensive tasks that the main thread depends on, such as computing animation data for the current frame, or executing Jolt physics tasks.
	const size_t high_priority_task_manager_num_threads = myClamp<size_t>(PlatformUtils::getNumLogicalProcessors(), 1, 512);
	high_priority_task_manager = new glare::TaskManager("high_priority_task_manager", high_priority_task_manager_num_threads);


	main_mem_allocator = new glare::MallocAllocator(); // TEMP TODO: use something better
	//main_mem_allocator = new glare::LimitedAllocator(10'000'000'000ull); // TEMP TODO: use something better



	std::string cache_dir = appdata_path;
	if(settings->value(MainOptionsDialog::useCustomCacheDirKey(), /*default value=*/false).toBool())
	{
		const std::string custom_cache_dir = QtUtils::toStdString(settings->value(MainOptionsDialog::customCacheDirKey()).toString());
		if(!custom_cache_dir.empty()) // Don't use custom cache dir if it's the empty string (e.g. not set to something valid)
			cache_dir = custom_cache_dir;
	}

	settings_store = new QSettingsStore(settings);

	Reference<glare::Allocator> worker_allocator = new glare::LimitedAllocator(/*max_size_B=*/2048 * 1024 * 1024ull);

	gui_client.preConnectInitialise(cache_dir, settings_store, this, high_priority_task_manager, /*worker allocator=*/worker_allocator);
}


static std::string computeWindowTitle()
{
	return "Metasiberia Beta v" + ::cyberspace_version;
}


static std::string canonicalHostForMetasiberia(const std::string& host)
{
	if(host == "87.103.196.229" || host == "185.182.110.184" || host == "89.104.70.23")
		return "vr.metasiberia.com";

	return host;
}


static std::string canonicaliseHostPortForMetasiberia(const std::string& host_port)
{
	std::string host = host_port;
	std::string port_suffix;

	const size_t colon_pos = host_port.find(':');
	if(colon_pos != std::string::npos)
	{
		host = host_port.substr(0, colon_pos);
		port_suffix = host_port.substr(colon_pos);
	}

	return canonicalHostForMetasiberia(host) + port_suffix;
}


static bool hostIsLocalForWebMode(const std::string& host_port)
{
	const size_t colon_pos = host_port.find(':');
	const std::string host = (colon_pos == std::string::npos) ? host_port : host_port.substr(0, colon_pos);
	return (host == "localhost") || (host == "127.0.0.1");
}


static std::string canonicaliseMetasiberiaSubURLHost(const std::string& url)
{
	if(!hasPrefix(url, "sub://"))
		return url;

	const size_t host_start = 6; // After "sub://"
	const size_t host_end = url.find_first_of("/?", host_start);
	const std::string host_port = (host_end == std::string::npos) ? url.substr(host_start) : url.substr(host_start, host_end - host_start);

	std::string host = host_port;
	std::string port_suffix;

	const size_t colon_pos = host_port.find(':');
	if(colon_pos != std::string::npos)
	{
		host = host_port.substr(0, colon_pos);
		port_suffix = host_port.substr(colon_pos);
	}

	const std::string canonical_host = canonicalHostForMetasiberia(host);
	if(canonical_host == host)
		return url;

	const std::string tail = (host_end == std::string::npos) ? std::string() : url.substr(host_end);
	return std::string("sub://") + canonical_host + port_suffix + tail;
}


static std::string makeMetasiberiaWorldSubURL(const std::string& host, const std::string& world_name)
{
	std::string URL = "sub://" + canonicalHostForMetasiberia(host.empty() ? std::string("vr.metasiberia.com") : host) + "/";
	if(!world_name.empty())
		URL += web::Escaping::URLEscape(world_name);
	return URL;
}


static bool isMetasiberiaMapURLForMainWindow(const URLParseResults& parse_res)
{
	return canonicalHostForMetasiberia(parse_res.hostname) == "vr.metasiberia.com" && parse_res.worldname == "map";
}


static bool tryParseMapLatLonInput(const std::string& input, double& lat_out, double& lon_out)
{
	std::string s = stripHeadAndTailWhitespace(input);
	for(char& c : s)
		if(c == ',' || c == ';' || c == '\t')
			c = ' ';

	std::vector<std::string> parts = split(s, ' ');
	std::vector<std::string> values;
	for(size_t i = 0; i < parts.size(); ++i)
	{
		const std::string part = stripHeadAndTailWhitespace(parts[i]);
		if(!part.empty())
			values.push_back(part);
	}

	if(values.size() != 2)
		return false;

	try
	{
		const double lat = stringToDouble(values[0]);
		const double lon = stringToDouble(values[1]);
		if(lat < -85.05112878 || lat > 85.05112878 || lon < -180.0 || lon > 180.0)
			return false;

		lat_out = lat;
		lon_out = lon;
		return true;
	}
	catch(StringUtilsExcep&)
	{
		return false;
	}
}


static std::string makeMetasiberiaMapLatLonURL(double lat, double lon, double z, double heading_deg)
{
	return "sub://vr.metasiberia.com/map?lat=" + doubleToStringNDecimalPlaces(lat, 7) +
		"&lon=" + doubleToStringNDecimalPlaces(lon, 7) +
		"&z=" + doubleToStringNDecimalPlaces(z, 2) +
		"&heading=" + doubleToStringNDecimalPlaces(heading_deg, 1);
}


static bool lookupMetasiberiaMapPlaceName(const std::string& query, double& lat_out, double& lon_out, std::string& label_out, std::string& error_out)
{
	static qint64 last_request_ms = 0;

	const qint64 now_ms = QDateTime::currentMSecsSinceEpoch();
	if(last_request_ms > 0 && now_ms - last_request_ms < 1000)
		QThread::msleep((unsigned long)(1000 - (now_ms - last_request_ms)));
	last_request_ms = QDateTime::currentMSecsSinceEpoch();

	QUrl url("https://nominatim.openstreetmap.org/search");
	QUrlQuery url_query;
	url_query.addQueryItem("format", "jsonv2");
	url_query.addQueryItem("limit", "1");
	url_query.addQueryItem("q", QtUtils::toQString(query));
	url.setQuery(url_query);

	std::string body;
	try
	{
		HTTPClient client;
		client.additional_headers.push_back("User-Agent: Metasiberia/0.0.21 (https://metasiberia.com/)");
		client.additional_headers.push_back("Accept: application/json");
		client.max_data_size = 2 * 1024 * 1024;
		client.max_socket_buffer_size = 2 * 1024 * 1024;

		const HTTPClient::ResponseInfo response = client.downloadFile(QtUtils::toStdString(url.toString(QUrl::FullyEncoded)), body);
		if(response.response_code != 200)
		{
			error_out = "Ошибка поиска места: HTTP " + toString(response.response_code) + " " + response.response_message;
			return false;
		}
	}
	catch(glare::Exception& e)
	{
		error_out = "Ошибка поиска места: " + std::string(e.what());
		return false;
	}

	QJsonParseError parse_error;
	const QJsonDocument doc = QJsonDocument::fromJson(QByteArray(body.data(), (int)body.size()), &parse_error);
	if(parse_error.error != QJsonParseError::NoError || !doc.isArray())
	{
		error_out = "Поиск места вернул некорректный ответ.";
		return false;
	}

	const QJsonArray results = doc.array();
	if(results.isEmpty() || !results[0].isObject())
	{
		error_out = "Место не найдено: " + query;
		return false;
	}

	const QJsonObject first = results[0].toObject();
	bool lat_ok = false;
	bool lon_ok = false;
	const double lat = first.value("lat").toString().toDouble(&lat_ok);
	const double lon = first.value("lon").toString().toDouble(&lon_ok);
	if(!lat_ok || !lon_ok)
	{
		error_out = "В результате поиска нет корректных координат.";
		return false;
	}

	lat_out = lat;
	lon_out = lon;
	label_out = QtUtils::toStdString(first.value("display_name").toString());
	return true;
}


static bool resolveMetasiberiaMapAddressInput(const std::string& input, const GUIClient& gui_client, std::string& resolved_url_out, std::string& notification_out, std::string& error_out)
{
	const std::string trimmed_input = stripHeadAndTailWhitespace(input);
	if(trimmed_input.empty())
		return false;

	const double current_heading_deg = Maths::doubleMod(::radToDegree(gui_client.cam_controller.getAngles().x), 360.0);
	const double map_spawn_z = PlayerPhysics::getEyeHeight();

	if(!hasPrefix(trimmed_input, "sub://") && gui_client.isMetasiberiaMapWorld())
	{
		double lat = 0;
		double lon = 0;
		if(tryParseMapLatLonInput(trimmed_input, lat, lon))
		{
			resolved_url_out = makeMetasiberiaMapLatLonURL(lat, lon, map_spawn_z, current_heading_deg);
			notification_out = "Переход к координатам карты.";
			return true;
		}

		std::string label;
		if(!lookupMetasiberiaMapPlaceName(trimmed_input, lat, lon, label, error_out))
			return true;

		resolved_url_out = makeMetasiberiaMapLatLonURL(lat, lon, map_spawn_z, current_heading_deg);
		notification_out = label.empty() ? ("Переход к месту: " + trimmed_input) : ("Переход к месту: " + label);
		return true;
	}

	if(!hasPrefix(trimmed_input, "sub://"))
		return false;

	URLParseResults parse_res;
	try
	{
		parse_res = URLParser::parseURL(canonicaliseMetasiberiaSubURLHost(trimmed_input));
	}
	catch(glare::Exception&)
	{
		return false;
	}

	if(!isMetasiberiaMapURLForMainWindow(parse_res) || !parse_res.parsed_map_query || (parse_res.parsed_lat && parse_res.parsed_lon))
		return false;

	double lat = 0;
	double lon = 0;
	std::string label;
	if(!lookupMetasiberiaMapPlaceName(parse_res.map_query, lat, lon, label, error_out))
		return true;

	const double use_z = parse_res.parsed_z ? parse_res.z : map_spawn_z;
	const double use_heading = parse_res.parsed_heading ? parse_res.heading : current_heading_deg;
	resolved_url_out = makeMetasiberiaMapLatLonURL(lat, lon, use_z, use_heading);
	notification_out = label.empty() ? ("Переход к месту: " + parse_res.map_query) : ("Переход к месту: " + label);
	return true;
}


static QString defaultHelpInfoMessageText()
{
	return
		QCoreApplication::translate("MainWindow", "Use the W/A/S/D keys and arrow keys to move and look around.\n") +
		QCoreApplication::translate("MainWindow", "Click and drag the mouse on the 3D view to look around.\n") +
		QCoreApplication::translate("MainWindow", "Space key: jump\n") +
		QCoreApplication::translate("MainWindow", "Double-click an object to select it.");
}


static RuntimeTranslation::UILanguage uiLanguageFromSettingsValue(const QString& value)
{
	const QString lower = value.trimmed().toLower();
	if(lower.startsWith("ru") || lower == "russian")
		return RuntimeTranslation::UILanguage::Russian;

	return RuntimeTranslation::UILanguage::English;
}


static QString uiLanguageToSettingsValue(RuntimeTranslation::UILanguage language)
{
	return (language == RuntimeTranslation::UILanguage::Russian) ? "ru" : "en";
}


static QIcon makeMenuGlyphIcon(const QString& glyph)
{
	const int icon_size = 22;
	QPixmap pixmap(icon_size, icon_size);
	pixmap.fill(Qt::transparent);

	QPainter painter(&pixmap);
	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.setRenderHint(QPainter::TextAntialiasing, true);
	painter.setPen(QApplication::palette().color(QPalette::Text));

	QFont font = QApplication::font("QMenu");
	font.setPixelSize(16);
	if(glyph == QStringLiteral("T") || glyph == QStringLiteral("+"))
		font.setBold(true);
	painter.setFont(font);
	painter.drawText(pixmap.rect(), Qt::AlignCenter, glyph);
	return QIcon(pixmap);
}


static void setMenuActionGlyphIcon(QAction* action, const QString& glyph)
{
	if(!action)
		return;
	action->setIcon(makeMenuGlyphIcon(glyph));
	action->setIconVisibleInMenu(true);
}


void MainWindow::startMainTimer()
{
	// Stop previous timer, if it exists.
	if(main_timer_id != 0)
		killTimer(main_timer_id);

	int use_interval = 1; // in milliseconds
	const bool limit_FPS = settings->value(MainOptionsDialog::limitFPSKey(), /*default val=*/false).toBool();
	if(limit_FPS)
	{
		const int max_FPS = myClamp(settings->value(MainOptionsDialog::FPSLimitKey(), /*default val=*/60).toInt(), 15, 1000);
		use_interval = (int)(1000.0 / max_FPS);
	}

#ifdef OSX
	// Set to at least 17ms due to this issue on Mac OS: https://bugreports.qt.io/browse/QTBUG-60346
	use_interval = myMax(use_interval, 17);
#endif

	main_timer_id = startTimer(use_interval);
}


void MainWindow::initialiseUI()
{
	ZoneScoped; // Tracy profiler

#if SUBSTRATA_USE_QT_GAMEPAD
	QGamepadManager::instance(); // Creating the instance here before any windows are created is required for querying gamepads to work.
#endif

	const QString saved_ui_language = settings->value(
		UI_LANGUAGE_SETTINGS_KEY,
		settings->value(LEGACY_UI_LANGUAGE_SETTINGS_KEY, QStringLiteral("en"))
	).toString();
	const RuntimeTranslation::UILanguage startup_language = uiLanguageFromSettingsValue(
		saved_ui_language
	);
	applyUILanguage(startup_language, /*persist_setting=*/false);

	{
		ZoneScopedN("setupUi"); // Tracy profiler
		ui = new Ui::MainWindow();
		ui->setupUi(this);
	}
	ui->menubar->setAttribute(Qt::WA_AlwaysShowToolTips, true);
	ui->menubar->setMouseTracking(true);
	ui->menubar->installEventFilter(this);

	action_add_scientific_object = new QAction(this);
	action_add_scientific_object->setObjectName(QStringLiteral("actionAddScientificObject"));
	connect(action_add_scientific_object, SIGNAL(triggered(bool)), this, SLOT(on_actionAddScientificObject_triggered()));
	action_add_cultural_object = new QAction(this);
	action_add_cultural_object->setObjectName(QStringLiteral("actionAddCulturalObject"));
	connect(action_add_cultural_object, SIGNAL(triggered(bool)), this, SLOT(on_actionAddCulturalObject_triggered()));
	action_add_document = new QAction(this);
	action_add_document->setObjectName(QStringLiteral("actionAddDocument"));
	connect(action_add_document, SIGNAL(triggered(bool)), this, SLOT(on_actionAddDocument_triggered()));
	action_add_tree = new QAction(this);
	action_add_tree->setObjectName(QStringLiteral("actionAddTree"));
	connect(action_add_tree, SIGNAL(triggered(bool)), this, SLOT(on_actionAddTree_triggered()));

	initialiseLanguageMenu();
	configureEditAddSubmenu();

	// Keep favorites menu up to date (actions are rebuilt on-demand when the menu opens).
	if(ui->menuGo_to_Favorites)
	{
		connect(ui->menuGo_to_Favorites, &QMenu::aboutToShow, this, &MainWindow::updateFavoritesMenu);
		ui->menuGo_to_Favorites->installEventFilter(this); // For right-click context menu (rename/delete).
	}

	// Replace webcam dock content with full WebcamWindow (camera list, settings, etc.) before restoreState.
	// If it fails for any reason, keep the existing simple webcam UI (label + checkbox) as a fallback.
	webcam_window = NULL;
	try
	{
		webcam_window = new WebcamWindow(this);
		ui->webcamDockWidget->setWidget(webcam_window);
	}
	catch(glare::Exception& e)
	{
		logMessage("Webcam window init failed: " + std::string(e.what()) + " (using simple webcam UI)");
		webcam_window = NULL;
	}
	catch(...)
	{
		logMessage("Webcam window init failed (unknown exception) (using simple webcam UI)");
		webcam_window = NULL;
	}

	setAcceptDrops(true);

	// --- Update manager (GitHub Releases) ---
	update_manager = new UpdateManager(this);
	connect(update_manager, &UpdateManager::updateCheckFinished, this, &MainWindow::onUpdateCheckFinished);
	connect(update_manager, &UpdateManager::updateAvailabilityChanged, this, &MainWindow::onUpdateAvailabilityChanged);

	// Kick off an async update check once the event loop is running.
	QTimer::singleShot(1500, this, [this]() {
		if(update_manager)
			update_manager->checkForUpdatesAsync(/*force=*/true);
	});

	update_ob_editor_transform_timer = new QTimer(this);
	update_ob_editor_transform_timer->setSingleShot(true);
	connect(update_ob_editor_transform_timer, SIGNAL(timeout()), this, SLOT(updateObjectEditorObTransformSlot()));

	// Avatar settings dock (replaces the old modal dialog).
	avatar_dock_widget = new QDockWidget(tr("Avatar Settings"), this);
	avatar_dock_widget->setObjectName("avatarDockWidget");
	avatar_dock_widget->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

	avatar_settings_widget = new AvatarSettingsWidget(
		avatar_dock_widget,
		this->base_dir_path,
		this->settings,
		gui_client.resource_manager,
		&gui_client.animation_manager,
		&gui_client,
		[this]() { ui->glWidget->makeCurrent(); }
	);
	avatar_dock_widget->setWidget(avatar_settings_widget);
	addDockWidget(Qt::LeftDockWidgetArea, avatar_dock_widget);
	avatar_dock_widget->hide();
	connect(avatar_settings_widget, SIGNAL(requestClose()), avatar_dock_widget, SLOT(hide()));

	map_dock_widget = new QDockWidget(tr("Map"), this);
	map_dock_widget->setObjectName("mapDockWidget");
	map_dock_widget->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
	map_dock_widget->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
	map_dock_map_widget = new MetasiberiaMapDockWidget(&gui_client, map_dock_widget);
	map_dock_widget->setWidget(map_dock_map_widget);
	map_dock_widget->resize(360, 420);
	dockMetasiberiaMapLikeChat(this, map_dock_widget, ui->chatDockWidget);
	map_dock_widget->hide();

	animation_editor_dock_widget = new QDockWidget(tr("Animation Editor"), this);
	animation_editor_dock_widget->setObjectName(QStringLiteral("animationEditorDockWidget"));
	animation_editor_dock_widget->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
	animation_editor_dock_widget->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
	animation_editor_panel = new AnimationEditorPanel(settings, animation_editor_dock_widget);
	animation_editor_panel->setIconDirectory(LucideIconUtils::directoryForBasePath(base_dir_path));
	animation_editor_dock_widget->setWidget(animation_editor_panel);
	animation_editor_dock_widget->setMinimumWidth(440);
	addDockWidget(Qt::RightDockWidgetArea, animation_editor_dock_widget);
	animation_editor_dock_widget->hide();

	QVector<AnimationEditorItem> initial_animations;
	initial_animations << AnimationEditorItem{QStringLiteral("idle_default"), tr("Idle Default"), tr("System"), tr("Built-in"), 0.0, true}
		<< AnimationEditorItem{QStringLiteral("walk_default"), tr("Walk Default"), tr("Movement"), tr("Built-in"), 0.0, true}
		<< AnimationEditorItem{QStringLiteral("run_default"), tr("Run Default"), tr("Movement"), tr("Built-in"), 0.0, false}
		<< AnimationEditorItem{QStringLiteral("jump_start"), tr("Jump Start"), tr("Movement"), tr("Built-in"), 0.0, false}
		<< AnimationEditorItem{QStringLiteral("falling"), tr("Falling"), tr("Movement"), tr("Built-in"), 0.0, false}
		<< AnimationEditorItem{QStringLiteral("landing"), tr("Landing"), tr("Movement"), tr("Built-in"), 0.0, false};
	animation_editor_panel->setAnimations(initial_animations);
	connect(animation_editor_panel, &AnimationEditorPanel::applyProfileRequested, this,
		[this](const QString& profile_name, const QVariantMap&) {
			showInfoNotification("Animation profile saved: " + QtUtils::toStdString(profile_name));
		});

	photo_video_dock_widget = new QDockWidget(tr("Photo and Video Settings"), this);
	photo_video_dock_widget->setObjectName(QStringLiteral("photoVideoSettingsDockWidget"));
	photo_video_dock_widget->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
	photo_video_dock_widget->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
	photo_video_settings_panel = new PhotoVideoSettingsPanel(settings, photo_video_dock_widget);
	photo_video_settings_panel->setIconDirectory(LucideIconUtils::directoryForBasePath(base_dir_path));
	photo_video_dock_widget->setWidget(photo_video_settings_panel);
	photo_video_dock_widget->setMinimumWidth(400);
	addDockWidget(Qt::RightDockWidgetArea, photo_video_dock_widget);
	photo_video_dock_widget->hide();

	const auto apply_photo_video_camera_mode = [this](const QString& mode) {
		if(mode == QStringLiteral("selfie")) gui_client.cam_controller.selfieCameraModeSelected();
		else if(mode == QStringLiteral("fixed_angle")) gui_client.cam_controller.fixedAngleCameraModeSelected();
		else if(mode == QStringLiteral("free")) gui_client.cam_controller.freeCameraModeSelected();
		else if(mode == QStringLiteral("tracking")) gui_client.cam_controller.trackingCameraModeSelected();
		else gui_client.cam_controller.standardCameraModeSelected();
	};
	const auto apply_photo_video_autofocus_mode = [this](const QString& mode) {
		gui_client.cam_controller.setAutofocusMode(mode == QStringLiteral("eye") ? CameraController::AutofocusMode_Eye : CameraController::AutofocusMode_Off);
	};
	const auto apply_photo_video_settings = [this](const QVariantMap& values) {
		if(ui->glWidget->opengl_engine.nonNull() && ui->glWidget->opengl_engine->getCurrentScene())
		{
			auto* scene = ui->glWidget->opengl_engine->getCurrentScene();
			scene->dof_blur_strength = (float)values.value(QStringLiteral("dof_blur"), 0.0).toDouble();
			scene->dof_blur_focus_distance = (float)values.value(QStringLiteral("focus_distance"), 3.0).toDouble();
			scene->exposure_factor = (float)std::exp2(values.value(QStringLiteral("ev"), 0.0).toDouble());
			scene->saturation_multiplier = (float)values.value(QStringLiteral("saturation"), 1.0).toDouble();
		}
		gui_client.cam_controller.lens_sensor_dist = (float)(values.value(QStringLiteral("focal_length_mm"), 25.0).toDouble() * 0.001);
		Vec3d angles = gui_client.cam_controller.getAngles();
		angles.z = ::degreeToRad(values.value(QStringLiteral("roll_degrees"), 0.0).toDouble());
		gui_client.cam_controller.setAngles(angles);
	};
	connect(photo_video_dock_widget, &QDockWidget::visibilityChanged, this,
		[this, apply_photo_video_camera_mode, apply_photo_video_autofocus_mode, apply_photo_video_settings](const bool visible) {
			if(!native_photo_video_gl_ready)
				return;
			gui_client.setPhotoModeEnabled(visible);
			if(visible)
			{
				gui_client.photo_mode_ui.setVisible(false); // Native Qt dock replaces the legacy GL overlay.
				const QVariantMap restored_state = photo_video_settings_panel->currentSettings();
				apply_photo_video_camera_mode(restored_state.value(QStringLiteral("camera_mode"), QStringLiteral("standard")).toString());
				apply_photo_video_autofocus_mode(restored_state.value(QStringLiteral("autofocus_mode"), QStringLiteral("off")).toString());
				apply_photo_video_settings(restored_state);
			}
		});
	connect(photo_video_settings_panel, &PhotoVideoSettingsPanel::cameraModeChanged, this, apply_photo_video_camera_mode);
	connect(photo_video_settings_panel, &PhotoVideoSettingsPanel::autofocusModeChanged, this, apply_photo_video_autofocus_mode);
	connect(photo_video_settings_panel, &PhotoVideoSettingsPanel::settingsChanged, this, apply_photo_video_settings);
	connect(photo_video_settings_panel, &PhotoVideoSettingsPanel::capturePhotoRequested, this,
		[this](const QVariantMap&) { takeScreenshot(); });
	connect(photo_video_settings_panel, &PhotoVideoSettingsPanel::browseGalleryRequested, this, [this]() { showScreenshots(); });
	connect(photo_video_settings_panel, &PhotoVideoSettingsPanel::recordingChanged, this,
		[this](const bool recording, const QVariantMap&) {
			if(recording)
			{
				showErrorNotification("Video recording backend is not available in this build yet; the recording profile was saved.");
				photo_video_settings_panel->setRecording(false);
			}
		});

	document_editor_dock_widget = new QDockWidget(tr("Documents"), this);
	document_editor_dock_widget->setObjectName(QStringLiteral("documentEditorDockWidget"));
	document_editor_dock_widget->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
	document_editor_dock_widget->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
	document_editor_panel = new DocumentEditorPanel(document_editor_dock_widget);
	document_editor_panel->setIconDirectory(LucideIconUtils::directoryForBasePath(base_dir_path));
	document_editor_dock_widget->setWidget(document_editor_panel);
	document_editor_dock_widget->setMinimumWidth(430);
	addDockWidget(Qt::RightDockWidgetArea, document_editor_dock_widget);
	document_editor_dock_widget->hide();
	connect(document_editor_panel, &DocumentEditorPanel::errorOccurred, this,
		[this](const QString& message) { showErrorNotification(QtUtils::toStdString(message)); });
	connect(document_editor_panel, &DocumentEditorPanel::statusMessageChanged, this,
		[this](const QString& message) { statusBar()->showMessage(message, 5000); });
	connect(document_editor_panel, &DocumentEditorPanel::pdfPageTextureRequested, this,
		[this](const QString&, int) { showErrorNotification("PDF page-to-texture export requires the Qt PDF module in this build."); });

	// Add dock widgets to Window menu
	ui->menuWindow->addSeparator();
	ui->menuWindow->addAction(ui->editorDockWidget->toggleViewAction());
	ui->menuWindow->addAction(avatar_dock_widget->toggleViewAction());
	ui->menuWindow->addAction(ui->materialBrowserDockWidget->toggleViewAction());
	ui->menuWindow->addAction(ui->environmentDockWidget->toggleViewAction());
	ui->menuWindow->addAction(ui->worldSettingsDockWidget->toggleViewAction());
	ui->menuWindow->addAction(map_dock_widget->toggleViewAction());
	ui->menuWindow->addAction(animation_editor_dock_widget->toggleViewAction());
	ui->menuWindow->addAction(photo_video_dock_widget->toggleViewAction());
	ui->menuWindow->addAction(document_editor_dock_widget->toggleViewAction());
	ui->menuWindow->addAction(ui->chatDockWidget->toggleViewAction());
	ui->menuWindow->addAction(ui->helpInfoDockWidget->toggleViewAction());
	ui->menuWindow->addAction(ui->webcamDockWidget->toggleViewAction());
#if INDIGO_SUPPORT
	ui->menuWindow->addAction(ui->indigoViewDockWidget->toggleViewAction());
#endif
	ui->menuWindow->addAction(ui->diagnosticsDockWidget->toggleViewAction());
	updateMenuTooltips();


	// Always disable MDI for now, seems to be slower in general in Substrata
	//
	// 	-u sub://substrata.info/?x=-1.3&y=-5.1&z=1.67&heading=85.3
	// -------------------------------------------------------------
	// 1.67 ms CPU, 6.06 ms GPU
	//
	// -u sub://substrata.info/?x=-1.3&y=-5.1&z=1.67&heading=85.3  --no_MDI
	// -------------------------------------------------------------
	// 2.18 ms CPU, 4.53 ms GPU
	//
	ui->glWidget->allow_multi_draw_indirect = false;

	//if(args.isArgPresent("--no_MDI"))
	//	ui->glWidget->allow_multi_draw_indirect = false;
	if(parsed_args.isArgPresent("--no_bindless"))
		ui->glWidget->allow_bindless_textures = false;

	ui->glWidget->setBaseDir(base_dir_path, /*print output=*/this, settings);
	ui->objectEditor->base_dir_path = base_dir_path;
	ui->objectEditor->settings = settings;
	ui->editorDockWidget->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
	installEditorDockTitleBar(ui->editorDockWidget);
	ui->editorDockWidget->setMinimumWidth(360);
	ui->scrollArea->setWidgetResizable(true);
	ui->scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
	ui->scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
	ui->objectEditor->setMinimumWidth(340);
	ui->objectEditor->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
	scientific_object_editor = new ScientificObjectEditor(ui->scrollAreaWidgetContents);
	scientific_object_editor->setObjectName(QStringLiteral("scientificObjectEditor"));
	scientific_object_editor->setMinimumWidth(360);
	scientific_object_editor->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
	ui->verticalLayout_4->addWidget(scientific_object_editor);
	scientific_object_editor->hide();
	cultural_object_editor = new CulturalObjectEditor(ui->scrollAreaWidgetContents);
	cultural_object_editor->setObjectName(QStringLiteral("culturalObjectEditor"));
	cultural_object_editor->setPosAndRot3DControlsEnabled(settings->value("culturalObjectEditor/show3DControls", true).toBool());
	cultural_object_editor->setMinimumWidth(360);
	cultural_object_editor->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
	ui->verticalLayout_4->addWidget(cultural_object_editor);
	cultural_object_editor->hide();
	tree_editor_panel = new TreeEditorPanel(ui->scrollAreaWidgetContents);
	tree_editor_panel->setObjectName(QStringLiteral("treeEditorPanel"));
	tree_editor_panel->setMinimumWidth(360);
	tree_editor_panel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
	ui->verticalLayout_4->addWidget(tree_editor_panel);
	tree_editor_panel->hide();
	voxel_editor_panel = new VoxelEditorPanel(ui->scrollAreaWidgetContents);
	voxel_editor_panel->setObjectName(QStringLiteral("voxelEditorPanel"));
	voxel_editor_panel->setMinimumWidth(360);
	voxel_editor_panel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
	voxel_editor_panel->setIconDirectory(LucideIconUtils::directoryForBasePath(base_dir_path));
	// Keep the specialised voxel controls at the top of the dock so the editor
	// is immediately visible after creation; generic transform controls remain
	// available below it.
	ui->verticalLayout_4->insertWidget(0, voxel_editor_panel);
	voxel_editor_panel->hide();
	gear_inventory_panel = new GearInventoryPanel(ui->scrollAreaWidgetContents);
	gear_inventory_panel->setObjectName(QStringLiteral("gearInventoryPanel"));
	gear_inventory_panel->setMinimumWidth(360);
	gear_inventory_panel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
	gear_inventory_panel->initPreview(
		this->base_dir_path,
		this->settings,
		gui_client.resource_manager,
		&gui_client.animation_manager,
		[main_gl_widget = QPointer<GlWidget>(ui->glWidget)]() {
			if(main_gl_widget)
				main_gl_widget->makeCurrent();
		}
	);
	gear_inventory_panel->setClient(&gui_client);
	gear_inventory_panel->setIconDirectory(LucideIconUtils::directoryForBasePath(base_dir_path));
	ui->verticalLayout_4->insertWidget(0, gear_inventory_panel);
	gear_inventory_panel->hide();

	// Add a spacer to right-align the UserDetailsWidget (see http://www.setnode.com/blog/right-aligning-a-button-in-a-qtoolbar/)
	QWidget* spacer = new QWidget();
	spacer->setMinimumWidth(60);
	spacer->setMaximumWidth(60);
	//spacer->setGeometry(QRect()
	//spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	ui->toolBar->addWidget(spacer);

	url_widget = new URLWidget(this);
	const int button_W = 24;
	url_widget->backPushButton->setFixedSize(button_W, button_W);
	url_widget->backPushButton->setText(QString());
	url_widget->browserPushButton->setFixedSize(button_W, button_W);
	url_widget->browserPushButton->setText(QString());
	url_widget->browserPushButton->setToolTip(tr("Open Current Location In Browser"));
	url_widget->favoritePushButton->setFixedSize(button_W, button_W);
	url_widget->favoritePushButton->setText(QString());
	url_widget->favoritePushButton->setToolTip(tr("Add to Favorites"));
	refreshNavigationButtonIcons();

	ui->toolBar->addWidget(url_widget);

	user_details = new UserDetailsWidget(this);
	ui->toolBar->addWidget(user_details);

	connect(url_widget->backPushButton, SIGNAL(clicked(bool)), this, SLOT(on_actionGo_Back_triggered()));
	connect(url_widget->browserPushButton, SIGNAL(clicked(bool)), this, SLOT(openCurrentLocationInBrowserSlot()));
	connect(url_widget->favoritePushButton, SIGNAL(clicked(bool)), this, SLOT(on_actionAdd_to_Favorites_triggered()));



	// Create the LogWindow early so we can log stuff to it.
	log_window = new LogWindow(this, settings);
	connect(log_window, SIGNAL(openServerScriptLogSignal()), this, SLOT(openServerScriptLogSlot()));

	initialiseThemesMenu();

	logMessage("Qt version: " + std::string(qVersion()));
	logMessage("CEF version: " + CEF::CEFVersionString());

	// Since we use a perspective projection matrix with infinite far distance, use a large max drawing distance.
	ui->glWidget->max_draw_dist = 100000;

	ui->glWidget->main_task_manager = main_task_manager;
	ui->glWidget->high_priority_task_manager = high_priority_task_manager;
	ui->glWidget->main_mem_allocator = main_mem_allocator;


	// Restore main window geometry and state
	this->restoreGeometry(settings->value("mainwindow/geometry").toByteArray());

	if(!this->restoreState(settings->value("mainwindow/windowState").toByteArray()))
	{
		// State was not restored.  This will be the case for new Substrata installs.
		// Hide some dock widgets to provide a slightly simpler user experience.
		// Hide everything but chat and help-info dock widgets.

		// NOTE: to test this code-path, can delete the windowState Registry key in "Computer\HKEY_CURRENT_USER\SOFTWARE\Glare Technologies\Cyberspace\mainwindow" with regedit.
		this->ui->chatDockWidget->hide();
		this->ui->editorDockWidget->hide();
		this->ui->materialBrowserDockWidget->hide();
		this->ui->environmentDockWidget->hide();
		this->ui->worldSettingsWidget->hide();
		this->ui->diagnosticsDockWidget->hide();
		this->ui->worldSettingsDockWidget->hide();
	}

	// Make it so the toolbar can't be hidden, as it's confusing for users when it disappears.
	ui->toolBar->toggleViewAction()->setEnabled(false);
	ui->toolBar->setVisible(true); // Toolbar should always be visible.  Somehow it can be made invisible with the 'right' mainwindow/windowState setting.
	configureMainToolbarButtons();
	applyMainChromeThemeStylesheet();


	ui->worldSettingsWidget->init(this);

	ui->objectEditor->init();
	scientific_object_editor->init(settings);
	tree_editor_panel->init(settings, TreeObject::findBundledAssetRoot(base_dir_path));

	ui->diagnosticsWidget->init(settings);
	connect(ui->diagnosticsWidget, SIGNAL(settingsChangedSignal()), this, SLOT(diagnosticsWidgetChanged()));
	connect(ui->diagnosticsWidget, SIGNAL(reloadTerrainSignal()), this, SLOT(diagnosticsReloadTerrain()));

	ui->environmentOptionsWidget->init(settings);
	connect(ui->environmentOptionsWidget, SIGNAL(settingChanged()), this, SLOT(environmentSettingChangedSlot()));

	// Apply initial Northern Lights checkbox state to the active Qt scene.
	if(ui->glWidget->opengl_engine.nonNull() && ui->glWidget->opengl_engine->getCurrentScene())
	{
		const bool northern_lights_enabled = ui->environmentOptionsWidget->getNorthernLightsEnabled();
		ui->glWidget->opengl_engine->getCurrentScene()->draw_aurora = northern_lights_enabled;
	}

	if(ui->chatEmojiButton)
	{
		QFont emoji_font = ui->chatEmojiButton->font();
#if defined(_WIN32)
		emoji_font.setFamily("Segoe UI Emoji");
#endif
		emoji_font.setPixelSize(22);
		ui->chatEmojiButton->setFont(emoji_font);
		ui->chatEmojiButton->setText(QtUtils::toQString(EmojiUtils::pickerButtonLabel()));

		chat_emoji_popup = new QDialog(this, Qt::Popup);
		chat_emoji_popup->setWindowTitle(QtUtils::toQString(std::string("\xD0\xAD\xD0\xBC\xD0\xBE\xD0\xB4\xD0\xB7\xD0\xB8")));
		chat_emoji_popup->setObjectName("chatEmojiPopup");
		chat_emoji_popup->setStyleSheet(
			"QDialog#chatEmojiPopup { background: #f3f5f8; border: 1px solid #d7dde5; }"
			"QTabWidget::pane { border: 1px solid #d7dde5; background: #f3f5f8; top: -1px; }"
			"QTabBar::tab { background: #e8edf4; color: #2b3442; padding: 10px 12px; margin-right: 2px; border: 1px solid #d7dde5; border-bottom: none; min-width: 92px; }"
			"QTabBar::tab:selected { background: #ffffff; color: #111827; }"
			"QScrollArea { border: none; background: transparent; }"
		);
		QVBoxLayout* popup_layout = new QVBoxLayout(chat_emoji_popup);
		chat_emoji_popup->setWindowTitle(QtUtils::toQString(std::string("\xD0\xAD\xD0\xBC\xD0\xBE\xD0\xB4\xD0\xB7\xD0\xB8")));
		popup_layout->setContentsMargins(12, 12, 12, 12);
		popup_layout->setSpacing(10);

		chat_emoji_tab_widget = new QTabWidget(chat_emoji_popup);
		chat_emoji_tab_widget->setDocumentMode(true);
		chat_emoji_tab_widget->tabBar()->setExpanding(true);
		chat_emoji_tab_widget->tabBar()->setUsesScrollButtons(false);
		chat_emoji_tab_widget->setMinimumSize(900, 700);
		popup_layout->addWidget(chat_emoji_tab_widget);

		rebuildChatEmojiPopupContents();
		chat_emoji_popup->setMinimumSize(940, 780);
		chat_emoji_popup->resize(940, 780);
		connect(ui->chatEmojiButton, &QToolButton::clicked, this, &MainWindow::toggleChatEmojiPopup);
	}

	setupChatPlayerControls();

	connect(ui->chatPushButton, SIGNAL(clicked()), this, SLOT(sendChatMessageSlot()));
	connect(ui->chatMessageLineEdit, SIGNAL(returnPressed()), this, SLOT(sendChatMessageSlot()));
	connect(ui->glWidget, SIGNAL(mousePressed(QMouseEvent*)), this, SLOT(glWidgetMousePressed(QMouseEvent*)));
	connect(ui->glWidget, SIGNAL(mouseReleased(QMouseEvent*)), this, SLOT(glWidgetMouseReleased(QMouseEvent*)));
	connect(ui->glWidget, SIGNAL(mouseDoubleClickedSignal(QMouseEvent*)), this, SLOT(glWidgetMouseDoubleClicked(QMouseEvent*)));
	connect(ui->glWidget, SIGNAL(mouseMoved(QMouseEvent*)), this, SLOT(glWidgetMouseMoved(QMouseEvent*)));
	connect(ui->glWidget, SIGNAL(keyPressed(QKeyEvent*)), this, SLOT(glWidgetKeyPressed(QKeyEvent*)));
	connect(ui->glWidget, SIGNAL(keyReleased(QKeyEvent*)), this, SLOT(glWidgetkeyReleased(QKeyEvent*)));
	connect(ui->glWidget, SIGNAL(focusOutSignal()), this, SLOT(glWidgetFocusOut()));
	connect(ui->glWidget, SIGNAL(mouseWheelSignal(QWheelEvent*)), this, SLOT(glWidgetMouseWheelEvent(QWheelEvent*)));
#if SUBSTRATA_USE_QT_GAMEPAD
	connect(ui->glWidget, SIGNAL(gamepadButtonXChangedSignal(bool)), this, SLOT(gamepadButtonXChanged(bool)));
	connect(ui->glWidget, SIGNAL(gamepadButtonAChangedSignal(bool)), this, SLOT(gamepadButtonAChanged(bool)));
#endif
	connect(ui->glWidget, SIGNAL(viewportResizedSignal(int, int)), this, SLOT(glWidgetViewportResized(int, int)));
	connect(ui->glWidget, SIGNAL(cutShortcutActivated()), this, SLOT(glWidgetCutShortcutTriggered()));
	connect(ui->glWidget, SIGNAL(copyShortcutActivated()), this, SLOT(glWidgetCopyShortcutTriggered()));
	connect(ui->glWidget, SIGNAL(pasteShortcutActivated()), this, SLOT(glWidgetPasteShortcutTriggered()));
	connect(ui->objectEditor, SIGNAL(objectTransformChanged()), this, SLOT(objectTransformEditedSlot()));
	connect(ui->objectEditor, SIGNAL(objectChanged()), this, SLOT(objectEditedSlot()));
	connect(ui->objectEditor, SIGNAL(scriptChangedFromEditorSignal()), this, SLOT(scriptChangedFromEditorSlot()));
	connect(ui->objectEditor, SIGNAL(particleBurstNowSignal()), this, SLOT(particleBurstNowSlot()));
	connect(ui->objectEditor, SIGNAL(particleClearParticlesSignal()), this, SLOT(particleClearParticlesSlot()));
	connect(ui->objectEditor, SIGNAL(bakeObjectLightmap()), this, SLOT(bakeObjectLightmapSlot()));
	connect(ui->objectEditor, SIGNAL(bakeObjectLightmapHighQual()), this, SLOT(bakeObjectLightmapHighQualSlot()));
	connect(ui->objectEditor, SIGNAL(removeLightmapSignal()), this, SLOT(removeLightmapSignalSlot()));
	connect(ui->objectEditor, SIGNAL(posAndRot3DControlsToggled()), this, SLOT(posAndRot3DControlsToggledSlot()));
	connect(ui->objectEditor, SIGNAL(openServerScriptLogSignal()), this, SLOT(openServerScriptLogSlot()));
	connect(scientific_object_editor, SIGNAL(objectTransformChanged()), this, SLOT(objectTransformEditedSlot()));
	connect(scientific_object_editor, SIGNAL(objectChanged()), this, SLOT(objectEditedSlot()));
	connect(scientific_object_editor, SIGNAL(posAndRot3DControlsToggled()), this, SLOT(posAndRot3DControlsToggledSlot()));
	connect(scientific_object_editor, SIGNAL(deleteObjectRequested()), this, SLOT(on_actionDeleteObject_triggered()));
	connect(cultural_object_editor, SIGNAL(objectTransformChanged()), this, SLOT(objectTransformEditedSlot()));
	connect(cultural_object_editor, SIGNAL(objectChanged()), this, SLOT(objectEditedSlot()));
	connect(cultural_object_editor, SIGNAL(posAndRot3DControlsToggled()), this, SLOT(posAndRot3DControlsToggledSlot()));
	connect(cultural_object_editor, SIGNAL(deleteObjectRequested()), this, SLOT(on_actionDeleteObject_triggered()));
	connect(tree_editor_panel, SIGNAL(objectTransformChanged()), this, SLOT(objectTransformEditedSlot()));
	connect(tree_editor_panel, SIGNAL(objectChanged()), this, SLOT(objectEditedSlot()));
	connect(tree_editor_panel, SIGNAL(posAndRot3DControlsToggled()), this, SLOT(posAndRot3DControlsToggledSlot()));
	connect(tree_editor_panel, SIGNAL(deleteObjectRequested()), this, SLOT(on_actionDeleteObject_triggered()));
	connect(voxel_editor_panel, &VoxelEditorPanel::objectMetadataChanged, this, [this]() { objectEditedSlot(); });
	connect(voxel_editor_panel, &VoxelEditorPanel::meshRebuildRequested, this, [this]()
	{
		objectEditedSlot();
		gui_client.rebuildSelectedVoxelObject();
	});
	connect(voxel_editor_panel, &VoxelEditorPanel::toolStateChanged, this, [this]() { gui_client.cancelVoxelShapeTool(); });
	connect(voxel_editor_panel, &VoxelEditorPanel::proceduralGenerationRequested, this, [this](const int type_value)
	{
		const VoxelProceduralType type = static_cast<VoxelProceduralType>(type_value);
		gui_client.applyVoxelProceduralGenerator(type, voxel_editor_panel->proceduralParams(type));
	});
	connect(voxel_editor_panel, &VoxelEditorPanel::selectionCopyRequested, this, [this]() { gui_client.copyVoxelSelection(); });
	connect(voxel_editor_panel, &VoxelEditorPanel::selectionPasteRequested, this, [this]() { gui_client.pasteVoxelSelection(voxel_editor_panel->selectionOffset()); });
	connect(voxel_editor_panel, &VoxelEditorPanel::selectionDeleteRequested, this, [this]() { gui_client.deleteVoxelSelection(); });
	connect(voxel_editor_panel, &VoxelEditorPanel::selectionDuplicateRequested, this, [this]() { gui_client.duplicateVoxelSelection(voxel_editor_panel->selectionOffset()); });
	connect(voxel_editor_panel, &VoxelEditorPanel::selectionMoveRequested, this, [this]() { gui_client.moveVoxelSelection(voxel_editor_panel->selectionOffset()); });
	connect(voxel_editor_panel, &VoxelEditorPanel::selectionClearRequested, this, [this]() { gui_client.clearVoxelSelection(); });
	connect(ui->parcelEditor, SIGNAL(parcelChanged()), this, SLOT(parcelEditedSlot()));
	connect(ui->worldSettingsWidget, SIGNAL(settingsChangedSignal()), this, SLOT(worldSettingsChangedSlot()));
	connect(user_details, SIGNAL(logInClicked()), this, SLOT(on_actionLogIn_triggered()));
	connect(user_details, SIGNAL(logOutClicked()), this, SLOT(on_actionLogOut_triggered()));
	connect(user_details, SIGNAL(signUpClicked()), this, SLOT(on_actionSignUp_triggered()));
	connect(url_widget, SIGNAL(URLChanged()), this, SLOT(URLChangedSlot()));


#if !defined(_WIN32)
	// On Windows, Windows will execute substrata.exe with the -linku argument, so we don't need this technique.
	QDesktopServices::setUrlHandler("sub", /*receiver=*/this, /*method=*/"handleURL");
#endif


	setWindowTitle(QtUtils::toQString(computeWindowTitle()));

	ui->materialBrowserDockWidgetContents->init(this, this->base_dir_path, this->appdata_path, /*print output=*/this);
	connect(ui->materialBrowserDockWidgetContents, SIGNAL(materialSelected(const std::string&)), this, SLOT(materialSelectedInBrowser(const std::string&)));

	ui->objectEditor->setControlsEnabled(false);
	scientific_object_editor->setControlsEnabled(false);
	scientific_object_editor->hide();
	cultural_object_editor->setControlsEnabled(false);
	cultural_object_editor->hide();
	tree_editor_panel->setControlsEnabled(false);
	tree_editor_panel->hide();
	voxel_editor_panel->setEditable(false);
	voxel_editor_panel->hide();
	ui->parcelEditor->hide();
	ui->botEditorWidget->hide();

	refreshMapDockText();
	if(animation_editor_dock_widget)
		animation_editor_dock_widget->setWindowTitle(tr("Animation Editor"));
	if(photo_video_dock_widget)
		photo_video_dock_widget->setWindowTitle(tr("Photo and Video Settings"));
	if(document_editor_dock_widget)
		document_editor_dock_widget->setWindowTitle(tr("Documents"));
	updateMapDockState();

	startMainTimer();

	ui->infoDockWidget->setTitleBarWidget(new QWidget());
	ui->infoDockWidget->hide();

	setUIForSelectedObject();

	// Update help text
	this->ui->helpInfoLabel->setText(defaultHelpInfoMessageText());
	if(ui->chatPushButton)
	{
		ui->chatPushButton->setText(QString::fromUtf8("\xE2\x8F\x8E"));
		ui->chatPushButton->setToolTip(tr("Отправить сообщение"));
	}

	if(!settings->contains("mainwindow/geometry"))
		need_help_info_dock_widget_position = true;

	connect(this->ui->indigoViewDockWidget, SIGNAL(visibilityChanged(bool)), this, SLOT(onIndigoViewDockWidgetVisibilityChanged(bool)));

#if INDIGO_SUPPORT
#else
	this->ui->indigoViewDockWidget->hide();
#endif

	lightmap_flag_timer = new QTimer(this);
	lightmap_flag_timer->setSingleShot(true);
	connect(lightmap_flag_timer, SIGNAL(timeout()), this, SLOT(sendLightmapNeededFlagsSlot()));



#ifdef _WIN32
	// Create a GPU device.  Needed to get hardware accelerated video decoding and for hardware texture sharing for CEF.
	Direct3DUtils::createGPUDeviceAndMFDeviceManager(d3d_device, device_manager);
	gui_client.device_manager = device_manager.ptr;
	gui_client.d3d_device = d3d_device.ptr;

	// Log the adapter (GPU) that was chosen:
	{
		ComObHandle<IDXGIDevice1> device1 = d3d_device.getInterface<IDXGIDevice1>("IDXGIDevice1");
		ComObHandle<IDXGIAdapter> adapter;
		HRESULT hr = device1->GetAdapter(&adapter.ptr);
		if(hr != S_OK)
			throw glare::Exception("GetAdapter failed: " + PlatformUtils::COMErrorString(hr));
		DXGI_ADAPTER_DESC desc;
		adapter->GetDesc(&desc);
		logMessage("Direct3D device adapter: " + StringUtils::PlatformToUTF8UnicodeEncoding(desc.Description) + ", LUID: {" + toString((uint32)desc.AdapterLuid.LowPart) + ", " + toString(desc.AdapterLuid.HighPart) + "}");
	}
#endif //_WIN32


	if(run_as_screenshot_slave)
	{
		conPrint("Waiting for screenshot command connection...");
		screenshot_command_listener = new MySocket();
		screenshot_command_listener->bindAndListen(34534);

		screenshot_command_socket = screenshot_command_listener->acceptConnection(); // Blocks for the initial controller.
		screenshot_command_socket->setUseNetworkByteOrder(false);
		conPrint("Got screenshot command connection.");
	}



	//================================= SDL gamepad support =================================
#if 0
	logMessage("Initialising SDL...");
	if(SDL_Init(SDL_INIT_GAMECONTROLLER) < 0)
		logMessage("Failed to init SDL: " + std::string(SDL_GetError()));
	else
		logMessage("SDL successfully initialised.");

	// Check for joysticks
	logMessage("SDL_NumJoysticks: " + toString(SDL_NumJoysticks()));
	if(SDL_NumJoysticks() < 1)
	{
		logMessage("No joysticks / gamepads connected according to SDL!\n");
	}
	else
	{
		logMessage("Opening controller '" + (SDL_GameControllerNameForIndex(0) ? std::string(SDL_GameControllerNameForIndex(0)) : std::string("[unknown]")) + "'...");
		// Load joystick
		game_controller = SDL_GameControllerOpen(/*device index=*/0);
		if(!game_controller)
			logMessage("Warning: Unable to open game controller! SDL Error: " + std::string(SDL_GetError()));
		else
			logMessage("Successfully opened game controller with SDL.");
	}
#endif
}


void MainWindow::initialiseThemesMenu()
{
	if(!ui || !ui->menuThemes)
		return;

	if(!default_qt_style_name_set && QApplication::style())
	{
		default_qt_style_name = QtUtils::toIndString(QApplication::style()->objectName());
		default_qt_style_name_set = !default_qt_style_name.empty();
	}

	ui->menuThemes->clear();

	if(theme_action_group)
	{
		delete theme_action_group;
		theme_action_group = NULL;
	}

	theme_action_group = new QActionGroup(this);
	theme_action_group->setExclusive(true);

	QAction* default_theme_action = ui->menuThemes->addAction(tr("Default"));
	default_theme_action->setCheckable(true);
	default_theme_action->setData(QString());
	theme_action_group->addAction(default_theme_action);
	ui->menuThemes->addSeparator();

	const QString themes_dir_path = QtUtils::toQString(base_dir_path + QT_THEME_DIR_REL_PATH);
	QDir themes_dir(themes_dir_path);
	const QStringList theme_files = themes_dir.entryList(QStringList() << "*.json", QDir::Files, QDir::Name);
	for(int i = 0; i < theme_files.size(); ++i)
	{
		const QString theme_name = QFileInfo(theme_files[i]).completeBaseName();
		QAction* action = ui->menuThemes->addAction(makeThemeDisplayName(theme_name));
		action->setCheckable(true);
		action->setData(theme_name);
		theme_action_group->addAction(action);
	}

	connect(theme_action_group, &QActionGroup::triggered, this, [this](QAction* action)
	{
		if(!action)
			return;

		const std::string theme_name = QtUtils::toStdString(action->data().toString());
		if(theme_name.empty())
			applyDefaultQtTheme(/*persist_setting=*/true);
		else if(!applyNamedQtTheme(theme_name, /*persist_setting=*/true))
			applyDefaultQtTheme(/*persist_setting=*/true);
	});

	const std::string saved_theme = QtUtils::toStdString(settings->value(QT_THEME_SETTINGS_KEY, QString()).toString());
	if(saved_theme.empty())
	{
		default_theme_action->setChecked(true);
		applyDefaultQtTheme(/*persist_setting=*/false);
		return;
	}

	if(!applyNamedQtTheme(saved_theme, /*persist_setting=*/false))
		applyDefaultQtTheme(/*persist_setting=*/false);
}


bool MainWindow::applyNamedQtTheme(const std::string& theme_name, bool persist_setting)
{
	if(theme_name.empty())
	{
		applyDefaultQtTheme(persist_setting);
		return true;
	}

	const QString themes_dir_path = QtUtils::toQString(base_dir_path + QT_THEME_DIR_REL_PATH);
	const QString theme_file_path = QDir(themes_dir_path).filePath(QtUtils::toQString(theme_name + ".json"));

	QtThemeColors theme_colours;
	std::string error_message;
	if(!loadQtThemeColorsFromFile(theme_file_path, theme_colours, error_message))
	{
		logMessage("[Theme] " + error_message);
		if(persist_setting && settings)
			settings->remove(QT_THEME_SETTINGS_KEY);
		return false;
	}

	applyQtThemePalette(theme_colours);
	configureMainToolbarButtons();
	applyMainChromeThemeStylesheet();
	applyChatThemeStylesheet();
#if defined(_WIN32)
	applyNativeWindowCaptionThemeDeferred(this, &theme_colours);
#endif
	if(persist_setting && settings)
		settings->setValue(QT_THEME_SETTINGS_KEY, QtUtils::toQString(theme_name));

	updateThemesMenuCheckedState(theme_name);
	logMessage("[Theme] Applied theme '" + theme_name + "'.");
	return true;
}


void MainWindow::applyDefaultQtTheme(bool persist_setting)
{
	if(default_qt_style_name_set)
	{
		if(QStyle* default_style = QStyleFactory::create(QtUtils::toQString(default_qt_style_name)))
			QApplication::setStyle(default_style);
	}

	QApplication::setPalette(QPalette());
	configureMainToolbarButtons();
	applyMainChromeThemeStylesheet();
	applyChatThemeStylesheet();
#if defined(_WIN32)
	applyNativeWindowCaptionThemeDeferred(this, nullptr);
#endif

	if(persist_setting && settings)
		settings->remove(QT_THEME_SETTINGS_KEY);

	updateThemesMenuCheckedState(std::string());
	logMessage("[Theme] Applied default Qt theme.");
}


void MainWindow::updateThemesMenuCheckedState(const std::string& active_theme_name)
{
	if(!theme_action_group)
		return;

	const QString active_name = QtUtils::toQString(active_theme_name);
	QList<QAction*> actions = theme_action_group->actions();
	for(int i = 0; i < actions.size(); ++i)
	{
		QAction* action = actions[i];
		if(action)
			action->setChecked(action->data().toString() == active_name);
	}

	// QMenu styles commonly replace the native check indicator with the action
	// icon.  Refresh after changing checked state so the active theme gets an
	// explicit Lucide circle-check marker.
	refreshMainMenuActionIcons();
}


void MainWindow::initialiseLanguageMenu()
{
	if(!ui)
		return;

	if(!language_action_group)
	{
		language_action_group = new QActionGroup(this);
		language_action_group->setExclusive(true);
	}

	language_action_group->addAction(ui->actionLanguage_English);
	language_action_group->addAction(ui->actionLanguage_Russian);

	applyUILanguage(current_ui_language, /*persist_setting=*/false);

	connect(ui->actionLanguage_English, &QAction::toggled, this, [this](bool checked)
	{
		if(checked)
			applyUILanguage(RuntimeTranslation::UILanguage::English, /*persist_setting=*/true);
	}, Qt::UniqueConnection);
	connect(ui->actionLanguage_Russian, &QAction::toggled, this, [this](bool checked)
	{
		if(checked)
			applyUILanguage(RuntimeTranslation::UILanguage::Russian, /*persist_setting=*/true);
	}, Qt::UniqueConnection);

	updateMenuTooltips();
}


void MainWindow::applyUILanguage(RuntimeTranslation::UILanguage language, bool persist_setting)
{
	current_ui_language = language;

	if(!runtime_translator)
		runtime_translator = new RuntimeTranslation::RuntimeTranslator(this);

	if(QApplication::instance())
	{
		QApplication::instance()->removeTranslator(runtime_translator);
		if(language == RuntimeTranslation::UILanguage::Russian)
			QApplication::instance()->installTranslator(runtime_translator);
		QApplication::instance()->setProperty(UI_LANGUAGE_APP_PROPERTY_KEY, uiLanguageToSettingsValue(language));
	}

	if(persist_setting && settings)
	{
		const QString ui_language_value = uiLanguageToSettingsValue(language);
		settings->setValue(UI_LANGUAGE_SETTINGS_KEY, ui_language_value);
		settings->setValue(LEGACY_UI_LANGUAGE_SETTINGS_KEY, ui_language_value);
	}

	if(ui)
	{
		const QSignalBlocker block_english(ui->actionLanguage_English);
		const QSignalBlocker block_russian(ui->actionLanguage_Russian);
		ui->actionLanguage_English->setChecked(language == RuntimeTranslation::UILanguage::English);
		ui->actionLanguage_Russian->setChecked(language == RuntimeTranslation::UILanguage::Russian);
		refreshTranslatedUiText();
	}
}


void MainWindow::refreshTranslatedUiText()
{
	if(!ui)
		return;

	ui->retranslateUi(this);
	ui->worldSettingsWidget->retranslateUiText();
	ui->environmentOptionsWidget->retranslateUi(ui->environmentOptionsWidget);
	setWindowTitle(QtUtils::toQString(computeWindowTitle()));

	this->ui->helpInfoLabel->setText(defaultHelpInfoMessageText());

	if(url_widget)
	{
		url_widget->browserPushButton->setToolTip(tr("Open Current Location In Browser"));
		url_widget->favoritePushButton->setToolTip(tr("Add to Favorites"));
	}

	refreshMapDockText();
	if(animation_editor_dock_widget)
		animation_editor_dock_widget->setWindowTitle(tr("Animation Editor"));
	if(photo_video_dock_widget)
		photo_video_dock_widget->setWindowTitle(tr("Photo and Video Settings"));
	if(document_editor_dock_widget)
		document_editor_dock_widget->setWindowTitle(tr("Documents"));

	if(gui_client.gear_inventory_ui)
		gui_client.gear_inventory_ui->refreshText(current_ui_language == RuntimeTranslation::UILanguage::Russian);

	if(theme_action_group)
		initialiseThemesMenu();

	configureEditAddSubmenu();
	configureMainToolbarButtons();
	applyMainChromeThemeStylesheet();

	if(ui->environmentDockWidget && ui->environmentDockWidget->toggleViewAction())
	{
		const QString title = ui->environmentDockWidget->windowTitle();
		ui->environmentDockWidget->toggleViewAction()->setText(title);
	}

	updateMenuTooltips();
}


void MainWindow::configureEditAddSubmenu()
{
	if(!ui || !ui->menuEdit)
		return;

	QMenu* add_menu = ui->menuEdit->findChild<QMenu*>("menuEditAdd", Qt::FindDirectChildrenOnly);
	if(!add_menu)
	{
		add_menu = new QMenu(ui->menuEdit);
		add_menu->setObjectName("menuEditAdd");
		add_menu->setToolTipsVisible(true);
	}

	add_menu->setTitle(tr("Add"));
	if(action_add_scientific_object)
	{
		action_add_scientific_object->setText(tr("Add Scientific Object"));
		action_add_scientific_object->setToolTip(tr("Add Scientific Object"));
		action_add_scientific_object->setStatusTip(tr("Add Scientific Object"));
	}
	if(action_add_cultural_object)
	{
		action_add_cultural_object->setText(tr("Add Cultural Object"));
		action_add_cultural_object->setToolTip(tr("Create a cultural object and open the Cultural Object Editor"));
		action_add_cultural_object->setStatusTip(tr("Add Cultural Object"));
	}
	if(action_add_document)
	{
		action_add_document->setText(tr("Add Document"));
		action_add_document->setToolTip(tr("Open the document editor for PDF, Markdown, HTML or text"));
		action_add_document->setStatusTip(tr("Add Document"));
	}
	if(action_add_tree)
	{
		action_add_tree->setText(tr("Add Tree"));
		action_add_tree->setToolTip(tr("Add Tree"));
		action_add_tree->setStatusTip(tr("Add procedural tree"));
	}

	const QList<QAction*> add_actions = {
		ui->actionAddObject,
		ui->actionAddHypercard,
		ui->actionAdd_Text,
		ui->actionAdd_Spotlight,
		ui->actionAdd_Particles,
		ui->actionAdd_Voxels,
		action_add_tree,
		action_add_scientific_object,
		action_add_cultural_object,
		action_add_document,
		ui->actionAdd_Camera,
		ui->actionAdd_Seat,
		ui->actionAdd_Audio_Source,
		ui->actionAdd_Web_View,
		ui->actionAdd_Video,
		ui->actionAdd_Decal,
		ui->actionAdd_Portal,
		ui->actionAddBot
	};

	const bool add_menu_in_edit = ui->menuEdit->actions().contains(add_menu->menuAction());
	if(!add_menu_in_edit)
	{
		QAction* insert_before = ui->menuEdit->actions().contains(ui->actionAddObject) ? ui->actionAddObject : ui->actionAdd_to_Favorites;
		if(insert_before && ui->menuEdit->actions().contains(insert_before))
			ui->menuEdit->insertMenu(insert_before, add_menu);
		else
			ui->menuEdit->addMenu(add_menu);
	}

	add_menu->clear();
	for(QAction* action : add_actions)
	{
		if(!action)
			continue;
		ui->menuEdit->removeAction(action);
		add_menu->addAction(action);
	}

	refreshMainMenuActionIcons();
}


void MainWindow::refreshMainMenuActionIcons()
{
	if(!ui)
		return;

	const QString lucide_dir = LucideIconUtils::directoryForBasePath(base_dir_path);
	const QPalette icon_palette = QApplication::palette();
	const QColor foreground = icon_palette.color(QPalette::WindowText);
	const auto themed = [&icon_palette](const QColor& colour)
	{
		return LucideIconUtils::themeAwareColour(colour, icon_palette, QPalette::WindowText, QPalette::Window);
	};
	const auto set_lucide_size = [&lucide_dir](QAction* action, const char* name, const QColor& colour, const QString& fallback, const int logical_size)
	{
		if(!LucideIconUtils::setActionIcon(action, lucide_dir, QString::fromLatin1(name), colour, logical_size))
			setMenuActionGlyphIcon(action, fallback);
	};
	const auto set_lucide = [&set_lucide_size](QAction* action, const char* name, const QColor& colour, const QString& fallback)
	{
		set_lucide_size(action, name, colour, fallback, 20);
	};
	const auto set_plain = [&set_lucide, &foreground](QAction* action, const char* name, const QString& fallback)
	{
		set_lucide(action, name, foreground, fallback);
	};
	const auto set_accent = [&set_lucide, &themed](QAction* action, const char* name, const char* colour, const QString& fallback)
	{
		set_lucide(action, name, themed(QColor(QString::fromLatin1(colour))), fallback);
	};
	const auto set_top_icon = [&lucide_dir](QAction* action, const char* name, const QColor& colour, const QString& fallback)
	{
		// QMenuBar asks QIcon for the platform small-icon metric, so merely
		// requesting a smaller pixmap is scaled back up.  Keep a 20 px canvas but
		// draw a 16 px glyph inside it, matching the visible size of toolbar icons.
		if(!LucideIconUtils::setPaddedActionIcon(action, lucide_dir, QString::fromLatin1(name), colour, 20, 16))
			setMenuActionGlyphIcon(action, fallback);
	};
	const auto set_top_plain = [&set_top_icon, &foreground](QAction* action, const char* name, const QString& fallback)
	{
		set_top_icon(action, name, foreground, fallback);
	};
	const auto set_top_accent = [&set_top_icon, &themed](QAction* action, const char* name, const char* colour, const QString& fallback)
	{
		set_top_icon(action, name, themed(QColor(QString::fromLatin1(colour))), fallback);
	};

	// Top-level menu bar: keep the main navigation calm and theme-coloured.
	set_top_plain(ui->menuEdit->menuAction(), "pencil", QString::fromUtf8("✎"));
	set_top_plain(ui->menuMovement->menuAction(), "move", QString::fromUtf8("↔"));
	set_top_plain(ui->menuAvatar->menuAction(), "user-round", QString::fromUtf8("●"));
	set_top_plain(ui->menuVehicles->menuAction(), "car-front", QString::fromUtf8("▰"));
	set_top_plain(ui->menuGear->menuAction(), "backpack", QString::fromUtf8("▣"));
	set_top_plain(ui->menuView->menuAction(), "eye", QString::fromUtf8("◉"));
	set_top_plain(ui->menuGo->menuAction(), "navigation", QString::fromUtf8("➤"));
	set_top_plain(ui->menuTools->menuAction(), "wrench", QString::fromUtf8("⚒"));
	set_top_plain(ui->menuWindow->menuAction(), "layout-template", QString::fromUtf8("▦"));
	set_top_plain(ui->menuThemes->menuAction(), "palette", QString::fromUtf8("◐"));
	set_top_plain(ui->actionShow_Parcels, "land-plot", QString::fromUtf8("▱"));
	set_top_plain(ui->actionAbout_Substrata, "info", QStringLiteral("i"));
	set_top_accent(ui->actionUpdate, "circle-arrow-up", "#22C55E", QString::fromUtf8("↑"));
	if(animation_editor_dock_widget)
		set_plain(animation_editor_dock_widget->toggleViewAction(), "activity", QString::fromUtf8("A"));
	if(photo_video_dock_widget)
		set_plain(photo_video_dock_widget->toggleViewAction(), "camera", QString::fromUtf8("◉"));
	if(document_editor_dock_widget)
		set_plain(document_editor_dock_widget->toggleViewAction(), "file-text", QString::fromUtf8("▤"));

	QMenu* add_menu = ui->menuEdit ? ui->menuEdit->findChild<QMenu*>("menuEditAdd", Qt::FindDirectChildrenOnly) : NULL;
	if(add_menu)
		set_accent(add_menu->menuAction(), "plus", "#60A5FA", QStringLiteral("+"));

	// Edit and creation commands use semantic accents; ordinary commands stay monochrome.
	set_plain(ui->actionUndo, "undo-2", QString::fromUtf8("↶"));
	set_plain(ui->actionRedo, "redo-2", QString::fromUtf8("↷"));
	set_accent(ui->actionAddObject, "image-plus", "#60A5FA", QString::fromUtf8("□"));
	set_accent(ui->actionAddHypercard, "panels-top-left", "#A78BFA", QString::fromUtf8("▣"));
	set_plain(ui->actionAdd_Text, "type", QStringLiteral("T"));
	set_accent(ui->actionAdd_Spotlight, "spotlight", "#FACC15", QString::fromUtf8("⌁"));
	set_accent(ui->actionAdd_Particles, "sparkles", "#C084FC", QString::fromUtf8("*"));
	set_accent(ui->actionAdd_Voxels, "boxes", "#F97316", QString::fromUtf8("▦"));
	set_accent(action_add_tree, "trees", "#4ADE80", QString::fromUtf8("♣"));
	set_accent(action_add_scientific_object, "atom", "#22D3EE", QString::fromUtf8("⚛"));
	set_accent(action_add_cultural_object, "palette", "#F59E0B", QString::fromUtf8("◆"));
	set_accent(action_add_document, "file-text", "#60A5FA", QString::fromUtf8("▤"));
	set_accent(ui->actionAdd_Camera, "camera", "#93C5FD", QString::fromUtf8("◉"));
	set_accent(ui->actionAdd_Seat, "armchair", "#D6B98C", QString::fromUtf8("╚"));
	set_accent(ui->actionAdd_Audio_Source, "audio-lines", "#F472B6", QString::fromUtf8("♫"));
	set_accent(ui->actionAdd_Web_View, "globe", "#38BDF8", QString::fromUtf8("◎"));
	set_accent(ui->actionAdd_Video, "video", "#FB7185", QString::fromUtf8("▶"));
	set_accent(ui->actionAdd_Decal, "sticker", "#F59E0B", QString::fromUtf8("▤"));
	set_accent(ui->actionAdd_Portal, "door-open", "#818CF8", QString::fromUtf8("↻"));
	set_accent(ui->actionAddBot, "bot", "#2DD4BF", QString::fromUtf8("☻"));
	set_accent(ui->actionAdd_to_Favorites, "star", "#F59E0B", QString::fromUtf8("★"));
	set_plain(ui->actionCopy_Object, "copy", QString::fromUtf8("⧉"));
	set_plain(ui->actionPaste_Object, "clipboard-paste", QString::fromUtf8("▣"));
	set_plain(ui->actionCloneObject, "copy-plus", QString::fromUtf8("⧉"));
	set_accent(ui->actionDeleteObject, "trash-2", "#FB7185", QString::fromUtf8("✕"));
	set_plain(ui->actionFind_Object, "search", QString::fromUtf8("⌕"));
	set_plain(ui->actionList_Objects_Nearby, "radar", QString::fromUtf8("⊙"));
	set_accent(ui->menuLightmaps->menuAction(), "sun", "#FACC15", QString::fromUtf8("☼"));
	set_accent(ui->actionBake_Lightmaps_fast_for_all_objects_in_parcel, "gauge", "#FACC15", QString::fromUtf8("◌"));
	set_accent(ui->actionBake_lightmaps_high_quality_for_all_objects_in_parcel, "sun-medium", "#FACC15", QString::fromUtf8("◎"));
	set_plain(ui->actionSave_Object_To_Disk, "save", QString::fromUtf8("▣"));
	set_plain(ui->actionSave_Parcel_Objects_To_Disk, "package", QString::fromUtf8("▤"));
	set_plain(ui->actionLoad_Objects_From_Disk, "folder-open", QString::fromUtf8("⇪"));

	// Movement, avatar, transport, gear and camera.
	set_plain(ui->actionFly_Mode, "plane", QString::fromUtf8("➤"));
	set_plain(ui->actionAvatarSettings, "user-round-cog", QString::fromUtf8("●"));
	set_plain(ui->actionSummon_Bike, "bike", QString::fromUtf8("◌"));
	set_plain(ui->actionSummon_Hovercar, "rocket", QString::fromUtf8("▲"));
	set_plain(ui->actionSummon_Boat, "ship", QString::fromUtf8("▱"));
	set_plain(ui->actionSummon_Jet_Ski, "waves-horizontal", QString::fromUtf8("≈"));
	set_plain(ui->actionSummon_Car, "car-front", QString::fromUtf8("▰"));
	set_plain(ui->actionOpen_Gear_Inventory, "backpack", QString::fromUtf8("▣"));
	set_plain(ui->actionConvert_Selected_Object_To_Gear_Item, "package-plus", QString::fromUtf8("+"));
	set_plain(ui->actionThird_Person_Camera, "camera", QString::fromUtf8("◉"));

	// World navigation.
	set_plain(ui->actionGo_Back, "arrow-left", QString::fromUtf8("←"));
	set_plain(ui->actionGo_to_Position, "crosshair", QString::fromUtf8("⊕"));
	set_plain(ui->actionGo_to_Parcel, "land-plot", QString::fromUtf8("▱"));
	set_plain(ui->actionGo_To_Start_Location, "house", QString::fromUtf8("⌂"));
	set_plain(ui->actionGoToMainWorld, "globe", QString::fromUtf8("◎"));
	set_plain(ui->actionGoToPersonalWorld, "user-round", QString::fromUtf8("●"));
	set_plain(ui->actionGo_to_CryptoVoxels_World, "boxes", QString::fromUtf8("▦"));
	set_plain(ui->actionGo_to_Substrata_Server, "server", QString::fromUtf8("▤"));
	set_accent(ui->actionGo_to_Metasiberia_Server, "snowflake", "#38BDF8", QString::fromUtf8("✣"));
	set_plain(ui->actionGo_to_Shki_nvkz_Server, "radio-tower", QString::fromUtf8("⌁"));
	set_plain(ui->actionGo_to_Map_World, "map", QString::fromUtf8("▱"));
	set_accent(ui->menuGo_to_Favorites->menuAction(), "star", "#F59E0B", QString::fromUtf8("★"));
	set_plain(ui->actionSet_Start_Location, "map-pin-house", QString::fromUtf8("⌂"));

	// Tools, windows, language, themes and help.
	set_plain(ui->actionTake_Screenshot, "camera", QString::fromUtf8("◉"));
	set_plain(ui->actionShow_Screenshot_Folder, "folder-open", QString::fromUtf8("▣"));
	set_plain(ui->actionShow_Log, "file-text", QString::fromUtf8("▤"));
	set_plain(ui->actionExport_view_to_Indigo, "file-output", QString::fromUtf8("⇱"));
	set_plain(ui->actionMute_Audio, "volume-x", QString::fromUtf8("×"));
	set_plain(ui->actionOptions, "settings", QString::fromUtf8("⚙"));
	set_plain(ui->actionReset_Layout, "layout-template", QString::fromUtf8("▦"));
	set_plain(ui->actionEnter_Fullscreen, "maximize", QString::fromUtf8("□"));
	set_plain(ui->menuLanguage->menuAction(), "languages", QString::fromUtf8("A"));
	set_plain(ui->actionLanguage_English, "languages", QStringLiteral("A"));
	set_plain(ui->actionLanguage_Russian, "languages", QString::fromUtf8("Я"));
	if(theme_action_group)
	{
		for(QAction* theme_action : theme_action_group->actions())
			if(theme_action)
			{
				if(theme_action->isChecked())
					set_accent(theme_action, "circle-check", "#22C55E", QString::fromUtf8("✓"));
				else
					set_plain(theme_action, theme_action->data().toString().isEmpty() ? "monitor" : "palette", QString::fromUtf8("◐"));
			}
	}

	set_plain(ui->editorDockWidget->toggleViewAction(), "pencil-ruler", QString::fromUtf8("✎"));
	if(avatar_dock_widget)
		set_plain(avatar_dock_widget->toggleViewAction(), "user-round", QString::fromUtf8("●"));
	set_plain(ui->materialBrowserDockWidget->toggleViewAction(), "palette", QString::fromUtf8("◐"));
	set_plain(ui->environmentDockWidget->toggleViewAction(), "cloud-sun", QString::fromUtf8("☼"));
	set_plain(ui->worldSettingsDockWidget->toggleViewAction(), "globe", QString::fromUtf8("◎"));
	if(map_dock_widget)
		set_plain(map_dock_widget->toggleViewAction(), "map", QString::fromUtf8("▱"));
	set_plain(ui->chatDockWidget->toggleViewAction(), "messages-square", QString::fromUtf8("▣"));
	set_accent(ui->helpInfoDockWidget->toggleViewAction(), "circle-question-mark", "#60A5FA", QStringLiteral("?"));
	set_plain(ui->webcamDockWidget->toggleViewAction(), "video", QString::fromUtf8("▶"));
	set_plain(ui->indigoViewDockWidget->toggleViewAction(), "sparkles", QString::fromUtf8("*"));
	set_plain(ui->diagnosticsDockWidget->toggleViewAction(), "activity", QString::fromUtf8("⌁"));
}


void MainWindow::refreshNavigationButtonIcons()
{
	if(!url_widget)
		return;

	const QString lucide_dir = LucideIconUtils::directoryForBasePath(base_dir_path);
	const QPalette icon_palette = url_widget->palette();
	const QColor foreground = icon_palette.color(QPalette::ButtonText);
	const QColor favorite = LucideIconUtils::themeAwareColour(
		QColor(QStringLiteral("#F59E0B")), icon_palette, QPalette::ButtonText, QPalette::Button);

	url_widget->backPushButton->setText(QString());
	url_widget->browserPushButton->setText(QString());
	url_widget->favoritePushButton->setText(QString());
	if(!LucideIconUtils::setButtonIcon(url_widget->backPushButton, lucide_dir, QStringLiteral("arrow-left"), foreground))
		url_widget->backPushButton->setText(QString::fromUtf8("←"));
	if(!LucideIconUtils::setButtonIcon(url_widget->browserPushButton, lucide_dir, QStringLiteral("external-link"), foreground))
		url_widget->browserPushButton->setText(QString::fromUtf8("↗"));
	if(!LucideIconUtils::setButtonIcon(url_widget->favoritePushButton, lucide_dir, QStringLiteral("star"), favorite))
		url_widget->favoritePushButton->setText(QString::fromUtf8("★"));
}


void MainWindow::configureMainToolbarButtons()
{
	if(!ui || !ui->toolBar)
		return;

	refreshMainMenuActionIcons();

	const QList<QAction*> add_toolbar_actions = {
		ui->actionAddObject,
		ui->actionAdd_Video,
		ui->actionAddHypercard,
		ui->actionAdd_Web_View,
		ui->actionAdd_Voxels
	};

	for(QAction* action : add_toolbar_actions)
	{
		if(action && ui->toolBar->actions().contains(action))
			ui->toolBar->removeAction(action);
	}

	QAction* insert_before = ui->toolBar->actions().isEmpty() ? NULL : ui->toolBar->actions().first();
	for(QAction* action : add_toolbar_actions)
	{
		if(!action)
			continue;

		if(insert_before)
			ui->toolBar->insertAction(insert_before, action);
		else
			ui->toolBar->addAction(action);

		QString tooltip = action->text();
		tooltip.remove('&');
		action->setToolTip(tooltip);
		action->setStatusTip(tooltip);
	}

	ui->toolBar->setToolButtonStyle(Qt::ToolButtonIconOnly);
	ui->toolBar->setIconSize(QSize(18, 18));
	ui->toolBar->setMovable(false);
	ui->toolBar->setFloatable(false);

	for(QAction* action : add_toolbar_actions)
	{
		if(!action)
			continue;

		if(QToolButton* button = qobject_cast<QToolButton*>(ui->toolBar->widgetForAction(action)))
		{
			button->setAutoRaise(false);
			button->setFixedSize(28, 28);
			button->setIconSize(QSize(18, 18));
			button->setToolButtonStyle(Qt::ToolButtonIconOnly);
		}
	}

	if(url_widget)
	{
		const int button_w = 28;
		url_widget->backPushButton->setFixedSize(button_w, button_w);
		url_widget->backPushButton->setIconSize(QSize(18, 18));
		url_widget->browserPushButton->setFixedSize(button_w, button_w);
		url_widget->browserPushButton->setIconSize(QSize(18, 18));
		url_widget->favoritePushButton->setFixedSize(button_w, button_w);
		url_widget->favoritePushButton->setIconSize(QSize(18, 18));
		refreshNavigationButtonIcons();
	}

	if(voxel_editor_panel)
		voxel_editor_panel->setIconDirectory(LucideIconUtils::directoryForBasePath(base_dir_path));
	if(gear_inventory_panel)
		gear_inventory_panel->setIconDirectory(LucideIconUtils::directoryForBasePath(base_dir_path));
}


void MainWindow::applyMainChromeThemeStylesheet()
{
	if(!ui)
		return;

	const QPalette palette = QApplication::palette();
	const QColor window = palette.color(QPalette::Window);
	const QColor base = palette.color(QPalette::Base);
	const QColor alternate_base = palette.color(QPalette::AlternateBase);
	const QColor text = palette.color(QPalette::WindowText);
	const QColor button = palette.color(QPalette::Button);
	const QColor border = palette.color(QPalette::Mid);
	const QColor highlight = palette.color(QPalette::Highlight);
	const QColor highlighted_text = palette.color(QPalette::HighlightedText);
	const bool dark_theme = text.lightness() > window.lightness();
	const QColor hover = dark_theme ? window.lighter(132) : window.darker(104);
	const QColor pressed = dark_theme ? window.lighter(152) : window.darker(110);
	const QColor menu_background = dark_theme ? base : window;
	const QColor tooltip_background = dark_theme ? alternate_base : QColor(255, 253, 231);

	const auto css = [](const QColor& colour) { return colour.name(QColor::HexRgb); };

	const QString menu_bar_style = QString(
		"QMenuBar#menubar { background: %1; color: %2; border: none; border-bottom: 1px solid %3; spacing: 4px; padding: 2px 5px; }"
		"QMenuBar#menubar::item { background: %7; color: %2; border: 1px solid %3; padding: 3px; border-radius: 4px; }"
		"QMenuBar#menubar::item:selected { background: %4; color: %2; border-color: %5; }"
		"QMenuBar#menubar::item:pressed { background: %8; color: %2; border-color: %5; }")
		.arg(css(window), css(text), css(border), css(hover), css(highlight), css(highlighted_text), css(button), css(pressed));

	const QString menu_style = QString(
		"QMenu { background: %1; color: %2; border: 1px solid %3; padding: 5px; }"
		"QMenu::item { padding: 6px 28px 6px 26px; border-radius: 4px; }"
		"QMenu::item:selected { background: %4; color: %5; }"
		"QMenu::item:disabled { color: %6; }"
		"QMenu::separator { height: 1px; background: %3; margin: 5px 4px; }"
		"QMenu::right-arrow { width: 8px; height: 8px; }")
		.arg(css(menu_background), css(text), css(border), css(highlight), css(highlighted_text), css(palette.color(QPalette::Disabled, QPalette::Text)));

	const QString toolbar_style = QString(
		"QToolBar#toolBar { background: %1; border: none; border-bottom: 1px solid %3; spacing: 4px; padding: 3px 6px; }"
		"QToolBar#toolBar QToolButton, QToolBar#toolBar QPushButton { background: %7; color: %2; border: 1px solid %3; border-radius: 4px; padding: 0px; min-width: 28px; min-height: 28px; max-width: 28px; max-height: 28px; }"
		"QToolBar#toolBar QToolButton:hover, QToolBar#toolBar QPushButton:hover { background: %4; border-color: %5; }"
		"QToolBar#toolBar QToolButton:pressed, QToolBar#toolBar QPushButton:pressed { background: %6; }"
		"QToolBar#toolBar QLineEdit { background: %8; color: %2; border: 1px solid %3; border-radius: 4px; padding: 2px 6px; selection-background-color: %5; selection-color: %9; }"
		"QToolBar#toolBar QLabel { color: %2; }")
		.arg(css(window), css(text), css(border), css(hover), css(highlight), css(pressed), css(button), css(base), css(highlighted_text));

	const QString main_style = QString(
		"QMainWindow#MainWindow { background: %1; color: %2; }"
		"QDockWidget { background: %1; color: %2; }"
		"QDockWidget::title { background: %1; color: %2; border-bottom: 1px solid %3; padding: 3px 6px; text-align: left; }"
		"QWidget#editorDockTitleBar { background: %1; border: none; }"
		"QLabel#editorDockTitleLabel { background: transparent; color: %2; border: none; }"
		"QWidget#editorDockTitleBar QToolButton { background: transparent; color: %2; border: none; border-radius: 3px; padding: 1px; }"
		"QWidget#editorDockTitleBar QToolButton:hover { background: %4; }"
		"QStatusBar { background: %1; color: %2; border-top: 1px solid %3; }")
		.arg(css(window), css(text), css(border), css(hover));

	const QString tooltip_style = QString(
		"QToolTip { color: %1; background-color: %2; border: 1px solid %3; font-size: 9pt; padding: 6px; border-radius: 3px; }")
		.arg(css(text), css(tooltip_background), css(border));

	setStyleSheet(main_style);
	qApp->setStyleSheet(tooltip_style);

	if(ui->menubar)
	{
		ui->menubar->setStyleSheet(menu_bar_style);
		// Let QMenuBar derive its height from the styled 28 px items.  A fixed
		// 28 px bar is too short once its own padding and bottom border are added;
		// Qt then moves every action into the overflow chevron.
		ui->menubar->ensurePolished();
		ui->menubar->updateGeometry();
		const QList<QMenu*> menus = ui->menubar->findChildren<QMenu*>();
		for(QMenu* menu : menus)
			if(menu)
				menu->setStyleSheet(menu_style);
	}

	if(ui->toolBar)
		ui->toolBar->setStyleSheet(toolbar_style);
	if(ui->statusbar)
		ui->statusbar->setStyleSheet(main_style);
}


void MainWindow::refreshMapDockText()
{
	if(map_dock_widget)
		map_dock_widget->setWindowTitle(tr("Map"));

	if(MetasiberiaMapDockWidget* map_widget = dynamic_cast<MetasiberiaMapDockWidget*>(map_dock_map_widget))
		map_widget->setMapActive(gui_client.usesEmbeddedMapDock());

	if(map_dock_widget && map_dock_widget->toggleViewAction())
		map_dock_widget->toggleViewAction()->setText(map_dock_widget->windowTitle());
}


void MainWindow::updateMapDockState()
{
	if(!map_dock_widget)
		return;

	const bool map_world_active = gui_client.usesEmbeddedMapDock();

	if(!map_world_active && map_dock_widget->isVisible())
		map_dock_widget->hide();
	else if(map_world_active && map_dock_widget->isVisible() && (map_dock_widget->isFloating() || dockWidgetArea(map_dock_widget) != Qt::RightDockWidgetArea))
		dockMetasiberiaMapLikeChat(this, map_dock_widget, ui->chatDockWidget);

	if(MetasiberiaMapDockWidget* map_widget = dynamic_cast<MetasiberiaMapDockWidget*>(map_dock_map_widget))
		map_widget->setMapActive(map_world_active);

	map_dock_widget->setEnabled(map_world_active);
	if(map_dock_widget->toggleViewAction())
	{
		map_dock_widget->toggleViewAction()->setEnabled(map_world_active);
		map_dock_widget->toggleViewAction()->setToolTip(
			map_world_active ?
				tr("Open map") :
				tr("Map is available only on vr.metasiberia.com")
		);
	}
}


void MainWindow::updateMenuTooltips()
{
	if(!ui || !menuBar())
		return;

	std::function<void(QMenu*)> apply_menu_tooltips_recursive;
	apply_menu_tooltips_recursive = [&](QMenu* menu)
	{
		if(!menu)
			return;

		menu->setToolTipsVisible(true);

		for(QAction* action : menu->actions())
		{
			if(!action)
				continue;

			const QString action_text = QString(action->text()).remove('&');
			action->setToolTip(action_text);
			action->setStatusTip(action_text);

			if(QMenu* sub_menu = action->menu())
				apply_menu_tooltips_recursive(sub_menu);
		}
	};

	for(QAction* top_level_action : menuBar()->actions())
	{
		if(!top_level_action)
			continue;

		const QString action_text = QString(top_level_action->text()).remove('&');
		top_level_action->setToolTip(action_text);
		top_level_action->setStatusTip(action_text);

		if(QMenu* menu = top_level_action->menu())
			apply_menu_tooltips_recursive(menu);
	}
}


class MainWindowGLUICallbacks : public GLUICallbacks
{
public:
	MainWindowGLUICallbacks() {}

	virtual void startTextInput()
	{}

	virtual void stopTextInput()
	{}

	virtual void setMouseCursor(MouseCursor cursor)
	{
		if(cursor == MouseCursor_Arrow)
		{
			main_window->ui->glWidget->setCursor(Qt::ArrowCursor);
		}
		else if(cursor == MouseCursor_IBeam)
		{
			main_window->ui->glWidget->setCursor(Qt::IBeamCursor);
		}
		else
			assert(0);
	}

	MainWindow* main_window;
};


 // Called after glWigget and OpenGLEngine has been initialised.
void MainWindow::afterGLInitInitialise()
{
	ZoneScoped; // Tracy profiler

	// Ensure main GL context is current while creating GL resources (UI textures, etc).
	// If resources are created against the wrong Qt GL context, rendering can corrupt into black quads.
	ui->glWidget->makeCurrent();
	struct DoneCurrent
	{
		GlWidget* w;
		~DoneCurrent() { if(w) w->doneCurrent(); }
	} done_current{ ui->glWidget };


	if(settings->value("mainwindow/flyMode", QVariant(false)).toBool())
	{
		ui->actionFly_Mode->setChecked(true);
		gui_client.player_physics.setFlyModeEnabled(true);
	}

	gui_client.cam_controller.setThirdPersonEnabled(settings->value("mainwindow/thirdPersonCamera", /*default val=*/false).toBool());
	ui->actionThird_Person_Camera->setChecked(settings->value("mainwindow/thirdPersonCamera", /*default val=*/false).toBool());

	// OpenGLEngineTests::doTextureLoadingTests(*ui->glWidget->opengl_engine);

	// NOTE: this code is also in SDLClient.cpp
#if defined(_WIN32)
		const std::string font_path       = PlatformUtils::getFontsDirPath() + "/Segoeui.ttf"; // SegoeUI is shipped with Windows 7 onwards: https://learn.microsoft.com/en-us/typography/fonts/windows_7_font_list
		const std::string emoji_font_path = PlatformUtils::getFontsDirPath() + "/Seguiemj.ttf";
#elif defined(__APPLE__)
		const std::string font_path       = "/System/Library/Fonts/SFNS.ttf";
		const std::string emoji_font_path = "/System/Library/Fonts/SFNS.ttf";
#else
		// Linux:
		const std::string font_path       = base_dir_path + "/data/resources/TruenoLight-E2pg.otf";
		const std::string emoji_font_path = base_dir_path + "/data/resources/TruenoLight-E2pg.otf";
#endif

	TextRendererRef text_renderer = new TextRenderer();

	TextRendererFontFaceSizeSetRef fonts       = new TextRendererFontFaceSizeSet(text_renderer, font_path);
	TextRendererFontFaceSizeSetRef emoji_fonts = new TextRendererFontFaceSizeSet(text_renderer, emoji_font_path);


	const auto device_pixel_ratio = ui->glWidget->devicePixelRatio(); // For retina screens this is 2, meaning the gl viewport width is in physical pixels, which have twice the density of qt pixel coordinates.

	gui_client.afterGLInitInitialise((double)device_pixel_ratio, ui->glWidget->opengl_engine, fonts, emoji_fonts);
	native_photo_video_gl_ready = true;
	if(photo_video_dock_widget && photo_video_dock_widget->isVisible())
	{
		gui_client.setPhotoModeEnabled(true);
		gui_client.photo_mode_ui.setVisible(false);
	}


	if(settings->value("mainwindow/showParcels", QVariant(false)).toBool())
	{
		ui->actionShow_Parcels->setChecked(true);
		gui_client.addParcelObjects();
	}


	MainWindowGLUICallbacks* glui_callbacks = new MainWindowGLUICallbacks();
	glui_callbacks->main_window = this;
	gui_client.gl_ui->callbacks = glui_callbacks;


	// Do auto-setting of graphics options, if they have not been set.  Otherwise apply MSAA setting.
	if(!settings->contains(MainOptionsDialog::MSAAKey())) // If the MSAA key has not been set:
	{
		const bool is_retina = device_pixel_ratio > 1;

		// We won't use MSAA by default in two cases:
		// 1) Intel drivers - which implies an integrated Intel GPU which is probably not very powerful.
		// 2) A retina monitor - the majority of which correspond to Mac laptops, which will look hopefully look alright without MSAA, and run slowly with MSAA.
		const bool is_Intel = ui->glWidget->opengl_engine->openglDriverVendorIsIntel();
		const bool no_MSAA = is_Intel || is_retina;
		const bool MSAA = !no_MSAA;
		//ui->glWidget->opengl_engine->setMSAAEnabled(MSAA);

		settings->setValue(MainOptionsDialog::MSAAKey(), MSAA); // Save MSAA setting

		settings->setValue(MainOptionsDialog::BloomKey(), MSAA); // Use the same decision for bloom

		logMessage("Auto-setting MSAA: is_retina: " + boolToString(is_retina) + ", is_Intel: " + boolToString(is_Intel) + ", MSAA: " + boolToString(MSAA));
	}
	else
	{
		// Else MSAA setting is present.
		const bool MSAA = settings->value(MainOptionsDialog::MSAAKey(), /*default=*/true).toBool();
		logMessage("Setting MSAA to " + boolToString(MSAA));
		//ui->glWidget->opengl_engine->setMSAAEnabled(MSAA);
	}


	if(ui->diagnosticsWidget->showFrameTimeGraphsCheckBox->isChecked())
	{
		opengl_engine->setProfilingEnabled(true);

		CPU_render_stats_widget = new RenderStatsWidget(opengl_engine, gui_client.gl_ui, /*widget index=*/0);
		GPU_render_stats_widget = new RenderStatsWidget(opengl_engine, gui_client.gl_ui, /*widget index=*/1);
	}
}


MainWindow::~MainWindow()
{
	running_destructor = true; // Set this to not append log messages during destruction, causes assert failure in Qt.
	native_photo_video_gl_ready = false;
	if(photo_video_dock_widget)
		photo_video_dock_widget->blockSignals(true);
	// QObject children outlive the generated Ui wrapper.  Stop the preview while
	// its restore target still exists, so no timer/destructor callback can touch
	// a destroyed main GL widget.
	if(gear_inventory_panel)
		gear_inventory_panel->shutdownPreview();

	if(runtime_translator && QApplication::instance())
		QApplication::instance()->removeTranslator(runtime_translator);

	delete main_task_manager;
	delete high_priority_task_manager;

#if !defined(_WIN32)
	QDesktopServices::unsetUrlHandler("sub"); // Remove 'this' as an URL handler.
#endif

	//ui->glWidget->makeCurrent(); // This crashes on Mac

	// Free direct3d device and device manager
#ifdef _WIN32
	device_manager.release();
	d3d_device.release();
#endif

	delete ui;
	ui = nullptr;

	settings_store = nullptr;
	// NOTE: can't delete settings here as some widget destructors access it after here.
}


void MainWindow::closeEvent(QCloseEvent* event)
{
	// Don't try and close everything down while we're in the message loop in the chromium embedded framework (CEF) code,
	// because that will try and close CEF down, which leads to problems.
	// Instead set a flag (should_close), and close the mainwindow when we're back in the main message loop and not the CEF loop.
	if(in_CEF_message_loop)
	{
		should_close = true;
		event->ignore();
		return;
	}

	ui->glWidget->makeCurrent();
	if(gear_inventory_panel)
		gear_inventory_panel->shutdownPreview();

	// If we are in fullscreen mode, exit it before we save the window state.  This is because we want to start next time not in fullscreen mode.
	if(this->isFullScreen())
		exitFromFullScreenMode();

	// Save main window geometry and state.  See http://doc.qt.io/archives/qt-4.8/qmainwindow.html#saveState
	settings->setValue("mainwindow/geometry", saveGeometry());
	settings->setValue("mainwindow/windowState", saveState());


	gui_client.shutdown();

	CPU_render_stats_widget = nullptr;
	GPU_render_stats_widget = nullptr;


	this->opengl_engine = NULL;
	ui->glWidget->shutdown(); // Shuts down OpenGL Engine.

	if(log_window) log_window->close();

	in_CEF_message_loop = true;
	CEF::shutdownCEF();
	in_CEF_message_loop = false;

	this->closing = true;
	QMainWindow::closeEvent(event);
}


void MainWindow::onIndigoViewDockWidgetVisibilityChanged(bool visible)
{
	conPrint("--------------------------------------- MainWindow::onIndigoViewDockWidgetVisibilityChanged (visible: " + boolToString(visible) + ") --------------");
	if(visible)
	{
		this->ui->indigoView->initialise(this->base_dir_path);

		if(gui_client.world_state)
		{
			Lock lock(gui_client.world_state->mutex);
			this->ui->indigoView->addExistingObjects(*gui_client.world_state, *gui_client.resource_manager);
		}
	}
	else
	{
		this->ui->indigoView->shutdown();
	}
}


// Some resources, such as MP4 videos, shouldn't be downloaded fully before displaying, but instead can be streamed and displayed when only part of the stream is downloaded.
/*static bool shouldStreamResourceViaHTTP(const std::string& url)
{
	// On Windows, use WMF's http reading to stream videos (until we get the custom byte stream working)
#ifdef _WIN32
	return ::hasExtension(url, "mp4");
#else
	return false; // On Mac/linux, we'll use the ResourceIODeviceWrapper for QMediaPlayer, so we don't need to use http.
#endif
}*/


//bool MainWindow::isAudioProcessed(const std::string& url) const
//{
//	Lock lock(audio_processed_mutex);
//	return audio_processed.count(url) > 0;
//}


void MainWindow::logMessage(const std::string& msg) // Append to LogWindow log display
{
	const std::string timestamped_msg = doubleToStringNDecimalPlaces(Clock::getTimeSinceInit(), 3) + " s:   " + msg;

	if(this->log_window && !running_destructor)
		this->log_window->appendLine(timestamped_msg);

	if(log_file)
	{
		log_file->getFileStream() << timestamped_msg;
		log_file->getFileStream() << "\n";
	}
}


void MainWindow::printFromLuaScript(const std::string& msg, UID object_uid)
{
	ui->objectEditor->printFromLuaScript(msg, object_uid);
}


void MainWindow::luaErrorOccurred(const std::string& msg, UID object_uid)
{
	ui->objectEditor->luaErrorOccurred(msg, object_uid);
}


void MainWindow::logAndConPrintMessage(const std::string& msg) // Print to stdout and append to LogWindow log display
{
	conPrint(msg);

	logMessage(msg);
}


void MainWindow::print(const std::string& s) // Print a message and a newline character.
{
	logMessage(s);
}


void MainWindow::printStr(const std::string& s) // Print a message without a newline character.
{
	logMessage(s);
}


void MainWindow::showErrorNotification(const std::string& message)
{
	gui_client.showErrorNotification(message);
}


void MainWindow::showInfoNotification(const std::string& message)
{
	gui_client.showInfoNotification(message);
}


void MainWindow::setTextAsNotLoggedIn()
{
	if(user_details)
		user_details->setTextAsNotLoggedIn();
}


void MainWindow::setTextAsLoggedIn(const std::string& username)
{
	if(user_details)
		user_details->setTextAsLoggedIn(username);

#if BUGSPLAT_SUPPORT
	if(minidump_sender)
		minidump_sender->setDefaultUserName(StringUtils::UTF8ToPlatformUnicodeEncoding(username).c_str());
#endif
}


void MainWindow::loginButtonClicked()
{
	on_actionLogIn_triggered();
}


void MainWindow::signUpButtonClicked()
{
	on_actionSignUp_triggered();
}


void MainWindow::loggedInButtonClicked()
{
	//on_actionSignUp_triggered();
}


void MainWindow::updateWorldSettingsControlsEditable()
{
	if(ui)
		ui->worldSettingsWidget->updateControlsEditable();
}


void MainWindow::updateWorldSettingsUIFromWorldSettings()
{
	if(ui)
		this->ui->worldSettingsWidget->setFromWorldSettings(gui_client.connected_world_settings); // Update UI
}


bool MainWindow::diagnosticsVisible()
{
	return ui->diagnosticsDockWidget->isVisible();
}


bool MainWindow::showObAABBsEnabled()
{
	 return ui->diagnosticsWidget->showObAABBsCheckBox->isChecked();
}


bool MainWindow::showPhysicsObOwnershipEnabled()
{
	return ui->diagnosticsWidget->showPhysicsObOwnershipCheckBox->isChecked();
}


bool MainWindow::showVehiclePhysicsVisEnabled()
{
	return ui->diagnosticsWidget->showVehiclePhysicsVisCheckBox->isChecked();
}


bool MainWindow::showPlayerPhysicsVisEnabled()
{
	return ui->diagnosticsWidget->showPlayerPhysicsVisCheckBox->isChecked();
}

bool MainWindow::showLodChunksVisEnabled()
{
	return ui->diagnosticsWidget->showLodChunkVisCheckBox->isChecked();
}


void MainWindow::writeTransformMembersToObject(WorldObject& ob)
{
	if(ScientificObjectSettings::isScientificObjectContent(ob.content) && scientific_object_editor)
		scientific_object_editor->writeTransformMembersToObject(ob);
	else if(CulturalObjectSettings::isCulturalObjectContent(ob.content) && cultural_object_editor)
		cultural_object_editor->writeTransformMembersToObject(ob);
	else
		ui->objectEditor->writeTransformMembersToObject(ob);
}


void MainWindow::objectLastModifiedUpdated(const WorldObject& ob)
{
	if(ScientificObjectSettings::isScientificObjectContent(ob.content) && scientific_object_editor)
		scientific_object_editor->objectLastModifiedUpdated(ob);
	else if(CulturalObjectSettings::isCulturalObjectContent(ob.content) && cultural_object_editor)
		cultural_object_editor->objectLastModifiedUpdated(ob);
	else
		ui->objectEditor->objectLastModifiedUpdated(ob);
}


void MainWindow::objectModelURLUpdated(const WorldObject& ob)
{
	ui->objectEditor->objectModelURLUpdated(ob);
}


void MainWindow::objectLightmapURLUpdated(const WorldObject& ob)
{
	ui->objectEditor->objectLightmapURLUpdated(ob);
}


void MainWindow::showEditorDockWidget()
{
	ui->editorDockWidget->show(); // Show the object editor dock widget if it is hidden.
}


void MainWindow::setObjectEditorControlsEditable(bool editable)
{
	if(active_editor_kind == ActiveEditor_Scientific && scientific_object_editor)
		scientific_object_editor->setControlsEditable(editable);
	else if(active_editor_kind == ActiveEditor_Cultural && cultural_object_editor)
		cultural_object_editor->setControlsEditable(editable);
	else if(active_editor_kind == ActiveEditor_Tree && tree_editor_panel)
	{
		ui->objectEditor->setTextFontFeatureSupported(gui_client.server_protocol_version >= 51);
		ui->objectEditor->setControlsEditable(editable);
		tree_editor_panel->setControlsEditable(editable);
	}
	else if(active_editor_kind == ActiveEditor_Voxel && voxel_editor_panel)
	{
		ui->objectEditor->setTextFontFeatureSupported(gui_client.server_protocol_version >= 51);
		ui->objectEditor->setControlsEditable(editable);
		voxel_editor_panel->setEditable(editable);
	}
	else
	{
		ui->objectEditor->setTextFontFeatureSupported(gui_client.server_protocol_version >= 51);
		ui->objectEditor->setControlsEditable(editable);
	}
}


void MainWindow::setObjectEditorFromOb(const WorldObject& ob, int selected_mat_index, bool ob_in_editing_users_world)
{
	const bool is_scientific_editor = (ob.object_type == WorldObject::ObjectType_Generic) && ScientificObjectSettings::isScientificObjectContent(ob.content);
	const bool is_cultural_editor = (ob.object_type == WorldObject::ObjectType_Generic) && CulturalObjectSettings::isCulturalObjectContent(ob.content);
	const bool is_tree_editor = TreeObject::isTreeObject(ob);
	const bool is_voxel_editor = ob.object_type == WorldObject::ObjectType_VoxelGroup;
	const bool is_particle_editor = (ob.object_type == WorldObject::ObjectType_Generic) && ParticleEmitterSettings::isParticleEmitterContent(ob.content);
	const bool is_portal_editor = ob.object_type == WorldObject::ObjectType_Portal;

	if(is_scientific_editor && scientific_object_editor)
	{
		active_editor_kind = ActiveEditor_Scientific;
		scientific_object_editor->setFromObject(ob, ob_in_editing_users_world);
	}
	else if(is_cultural_editor && cultural_object_editor)
	{
		active_editor_kind = ActiveEditor_Cultural;
		cultural_object_editor->setFromObject(ob, ob_in_editing_users_world);
	}
	else if(is_tree_editor && tree_editor_panel)
	{
		active_editor_kind = ActiveEditor_Tree;
		ui->objectEditor->setTextFontFeatureSupported(gui_client.server_protocol_version >= 51);
		ui->objectEditor->setFromObject(ob, selected_mat_index, ob_in_editing_users_world);
		tree_editor_panel->setFromObject(ob, ob_in_editing_users_world);
	}
	else if(is_voxel_editor && voxel_editor_panel)
	{
		active_editor_kind = ActiveEditor_Voxel;
		ui->objectEditor->setTextFontFeatureSupported(gui_client.server_protocol_version >= 51);
		voxel_editor_panel->setFromObject(ob);
		ui->objectEditor->setFromObject(ob, selected_mat_index, ob_in_editing_users_world);
		ui->objectEditor->setContentForSpecialisedEditor(voxel_editor_panel->legacyContent());
	}
	else
	{
		active_editor_kind = ActiveEditor_Object;
		ui->objectEditor->setTextFontFeatureSupported(gui_client.server_protocol_version >= 51);
		ui->objectEditor->setFromObject(ob, selected_mat_index, ob_in_editing_users_world);
	}

	ui->editorDockWidget->setWindowTitle(is_scientific_editor ? tr("Scientific Object Editor") : (is_cultural_editor ? tr("Cultural Object Editor") : (is_tree_editor ? tr("Tree Editor") : (is_voxel_editor ? tr("Voxel Editor") : (is_particle_editor ? tr("Particle Editor") : (is_portal_editor ? tr("Portal Editor") : tr("Editor")))))));
	if(ui->editorDockWidget->toggleViewAction())
		ui->editorDockWidget->toggleViewAction()->setText(ui->editorDockWidget->windowTitle());
}


int MainWindow::getSelectedMatIndex()
{
	if(active_editor_kind == ActiveEditor_Scientific)
		return 0;
	if(active_editor_kind == ActiveEditor_Cultural)
		return 0;
	if(active_editor_kind == ActiveEditor_Tree)
		return 0;
	if(active_editor_kind == ActiveEditor_Voxel && voxel_editor_panel)
		return voxel_editor_panel->currentMaterialIndex();
	return ui->objectEditor->getSelectedMatIndex();
}


void MainWindow::objectEditorToObject(WorldObject& ob)
{
	if((active_editor_kind == ActiveEditor_Scientific || ScientificObjectSettings::isScientificObjectContent(ob.content)) && scientific_object_editor)
		scientific_object_editor->toObject(ob);
	else if((active_editor_kind == ActiveEditor_Cultural || CulturalObjectSettings::isCulturalObjectContent(ob.content)) && cultural_object_editor)
		cultural_object_editor->toObject(ob);
	else if((active_editor_kind == ActiveEditor_Tree || TreeObject::isTreeObject(ob)) && tree_editor_panel)
	{
		ui->objectEditor->writeTransformMembersToObject(ob);
		tree_editor_panel->toObject(ob);
	}
	else if((active_editor_kind == ActiveEditor_Voxel || ob.object_type == WorldObject::ObjectType_VoxelGroup) && voxel_editor_panel)
	{
		// Preserve the existing voxel object's generic material/physics/audio/script
		// controls transactionally, then let the specialised panel apply layer
		// metadata and palette ownership.  Voxel metadata uses WorldObject::content,
		// so the generic content editor reads/writes the preserved legacy sidecar.
		WorldObjectRef edited_ob = new WorldObject();
		edited_ob->copyNetworkStateFrom(ob);
		edited_ob->uid = ob.uid;
		edited_ob->changed_flags = ob.changed_flags;
		ui->objectEditor->toObject(*edited_ob);
		voxel_editor_panel->setLegacyContent(edited_ob->content);
		std::string error;
		if(!voxel_editor_panel->applyToObject(*edited_ob, error))
		{
			voxel_editor_panel->setFromObject(ob);
			ui->objectEditor->setFromObject(ob, voxel_editor_panel->currentMaterialIndex(), connectedToUsersWorldOrGodUser());
			ui->objectEditor->setContentForSpecialisedEditor(voxel_editor_panel->legacyContent());
			throw glare::Exception(error.empty() ? "Could not apply voxel editor state." : error);
		}
		const uint32 edited_changed_flags = edited_ob->changed_flags;
		ob.copyNetworkStateFrom(*edited_ob);
		ob.setCompressedVoxels(edited_ob->getCompressedVoxels());
		ob.changed_flags = edited_changed_flags;
		ob.decompressVoxels();
		voxel_editor_panel->setFromObject(ob);
		ui->objectEditor->setFromObject(ob, voxel_editor_panel->currentMaterialIndex(), connectedToUsersWorldOrGodUser());
		ui->objectEditor->setContentForSpecialisedEditor(voxel_editor_panel->legacyContent());
	}
	else
		ui->objectEditor->toObject(ob); // Sets changed_flags on object as well.
}


void MainWindow::objectEditorObjectPickedUp()
{
	if(active_editor_kind == ActiveEditor_Scientific && scientific_object_editor)
		scientific_object_editor->objectPickedUp();
	else if(active_editor_kind == ActiveEditor_Cultural && cultural_object_editor)
		cultural_object_editor->objectPickedUp();
	else if(active_editor_kind == ActiveEditor_Tree && tree_editor_panel)
		ui->objectEditor->objectPickedUp();
	else
		ui->objectEditor->objectPickedUp();
}


void MainWindow::objectEditorObjectDropped()
{
	if(active_editor_kind == ActiveEditor_Scientific && scientific_object_editor)
		scientific_object_editor->objectDropped();
	else if(active_editor_kind == ActiveEditor_Cultural && cultural_object_editor)
		cultural_object_editor->objectDropped();
	else if(active_editor_kind == ActiveEditor_Tree && tree_editor_panel)
		ui->objectEditor->objectDropped();
	else
		ui->objectEditor->objectDropped();
}


bool MainWindow::snapToGridCheckBoxChecked()
{
	if(active_editor_kind == ActiveEditor_Scientific && scientific_object_editor)
		return scientific_object_editor->snapToGridChecked();
	if(active_editor_kind == ActiveEditor_Cultural && cultural_object_editor)
		return cultural_object_editor->snapToGridChecked();
	if(active_editor_kind == ActiveEditor_Tree && tree_editor_panel)
		return ui->objectEditor->snapToGridCheckBox->isChecked();
	return ui->objectEditor->snapToGridCheckBox->isChecked();
}


double MainWindow::gridSpacing()
{
	if(active_editor_kind == ActiveEditor_Scientific && scientific_object_editor)
		return scientific_object_editor->gridSpacing();
	if(active_editor_kind == ActiveEditor_Cultural && cultural_object_editor)
		return cultural_object_editor->gridSpacing();
	if(active_editor_kind == ActiveEditor_Tree && tree_editor_panel)
		return ui->objectEditor->gridSpacingDoubleSpinBox->value();
	return ui->objectEditor->gridSpacingDoubleSpinBox->value();
}


bool MainWindow::posAndRot3DControlsEnabled()
{
	if(active_editor_kind == ActiveEditor_Scientific && scientific_object_editor)
		return scientific_object_editor->posAndRot3DControlsEnabled();
	if(active_editor_kind == ActiveEditor_Cultural && cultural_object_editor)
		return cultural_object_editor->posAndRot3DControlsEnabled();
	if(active_editor_kind == ActiveEditor_Tree && tree_editor_panel)
		return ui->objectEditor->posAndRot3DControlsEnabled();
	return ui->objectEditor->posAndRot3DControlsEnabled();
}


void MainWindow::showObjectEditor()
{
	ui->parcelEditor->hide();
	ui->botEditorWidget->hide();
	if(gear_inventory_panel)
		gear_inventory_panel->hide();
	if(active_editor_kind == ActiveEditor_Scientific && scientific_object_editor)
	{
		ui->objectEditor->hide();
		if(cultural_object_editor)
			cultural_object_editor->hide();
		if(tree_editor_panel)
			tree_editor_panel->hide();
		if(voxel_editor_panel)
			voxel_editor_panel->hide();
		scientific_object_editor->show();
	}
	else if(active_editor_kind == ActiveEditor_Cultural && cultural_object_editor)
	{
		ui->objectEditor->hide();
		if(scientific_object_editor)
			scientific_object_editor->hide();
		if(tree_editor_panel)
			tree_editor_panel->hide();
		if(voxel_editor_panel)
			voxel_editor_panel->hide();
		cultural_object_editor->show();
	}
	else if(active_editor_kind == ActiveEditor_Tree && tree_editor_panel)
	{
		if(scientific_object_editor)
			scientific_object_editor->hide();
		if(cultural_object_editor)
			cultural_object_editor->hide();
		if(voxel_editor_panel)
			voxel_editor_panel->hide();
		ui->objectEditor->show();
		tree_editor_panel->show();
	}
	else if(active_editor_kind == ActiveEditor_Voxel && voxel_editor_panel)
	{
		if(scientific_object_editor)
			scientific_object_editor->hide();
		if(cultural_object_editor)
			cultural_object_editor->hide();
		if(tree_editor_panel)
			tree_editor_panel->hide();
		ui->objectEditor->show();
		voxel_editor_panel->show();
	}
	else
	{
		if(scientific_object_editor)
			scientific_object_editor->hide();
		if(cultural_object_editor)
			cultural_object_editor->hide();
		if(tree_editor_panel)
			tree_editor_panel->hide();
		if(voxel_editor_panel)
			voxel_editor_panel->hide();
		ui->objectEditor->show();
	}
}


void MainWindow::setObjectEditorEnabled(bool enabled)
{
	if(active_editor_kind == ActiveEditor_Scientific && scientific_object_editor)
		scientific_object_editor->setControlsEnabled(enabled);
	else if(active_editor_kind == ActiveEditor_Cultural && cultural_object_editor)
		cultural_object_editor->setControlsEnabled(enabled);
	else if(active_editor_kind == ActiveEditor_Tree && tree_editor_panel)
	{
		ui->objectEditor->setEnabled(enabled);
		tree_editor_panel->setControlsEnabled(enabled);
	}
	else if(active_editor_kind == ActiveEditor_Voxel && voxel_editor_panel)
	{
		ui->objectEditor->setEnabled(enabled);
		voxel_editor_panel->setEditable(enabled);
	}
	else
		ui->objectEditor->setEnabled(enabled);
}


struct AvatarNameInfo
{
	std::string name;
	std::string route_name;
	Colour3f colour;
	UID avatar_uid;
	bool is_self;

	inline bool operator < (const AvatarNameInfo& other) const { return name < other.name; }
};


static void detachLayoutItems(QLayout* layout)
{
	if(!layout)
		return;

	while(QLayoutItem* item = layout->takeAt(0))
		delete item;
}


static void clearLayoutAndDeleteWidgets(QLayout* layout)
{
	if(!layout)
		return;

	while(QLayoutItem* item = layout->takeAt(0))
	{
		if(QWidget* widget = item->widget())
			delete widget;
		if(QLayout* child_layout = item->layout())
			clearLayoutAndDeleteWidgets(child_layout);
		delete item;
	}
}


static QString chatCssColour(const QColor& colour)
{
	return QString("rgb(%1, %2, %3)").arg(colour.red()).arg(colour.green()).arg(colour.blue());
}


static QString chatCssColour(const Colour3f& colour)
{
	const int r = qBound(0, (int)std::floor(colour.r * 255.f + 0.5f), 255);
	const int g = qBound(0, (int)std::floor(colour.g * 255.f + 0.5f), 255);
	const int b = qBound(0, (int)std::floor(colour.b * 255.f + 0.5f), 255);
	return QString("rgb(%1, %2, %3)").arg(r).arg(g).arg(b);
}


static QColor chatAvatarBackgroundColour(const AvatarNameInfo& info)
{
	const uint64 uid_value = info.avatar_uid.valid() ? info.avatar_uid.value() : 1;
	const int hue = (int)((uid_value * 47 + 218) % 360);
	return QColor::fromHsv(hue, 92, 222);
}


static QString chatInitialsForName(const std::string& name)
{
	const QString qname = QtUtils::toQString(name).trimmed();
	QString initials;
	for(int i = 0; i < qname.size() && initials.size() < 2; ++i)
	{
		const QChar ch = qname[i];
		if(ch.isLetterOrNumber())
			initials.append(ch.toUpper());
	}
	return initials.isEmpty() ? QString("?") : initials;
}


static QString chatRoleBadgeForName(const std::string& name)
{
	const std::string lower_name = toLowerCase(name);
	if(lower_name.find("admin") != std::string::npos)
		return QString::fromUtf8("♛");
	if(lower_name.find("moder") != std::string::npos)
		return QString::fromUtf8("◆");
	if(lower_name.find("bot") != std::string::npos)
		return QString::fromUtf8("◎");
	if(lower_name.find("dev") != std::string::npos)
		return QString::fromUtf8("◇");
	return QString();
}


static QString chatRoleLabelForName(const std::string& name)
{
	const std::string lower_name = toLowerCase(name);
	if(lower_name.find("admin") != std::string::npos)
		return QString::fromUtf8("Администратор");
	if(lower_name.find("moder") != std::string::npos)
		return QString::fromUtf8("Модератор");
	if(lower_name.find("bot") != std::string::npos)
		return QString::fromUtf8("Бот");
	if(lower_name.find("dev") != std::string::npos)
		return QString::fromUtf8("Разработчик");
	return QString::fromUtf8("Игрок");
}


static QToolButton* makeChatToolButton(QWidget* parent, const QString& text, const QString& tooltip, const char* kind)
{
	QToolButton* button = new QToolButton(parent);
	button->setText(text);
	button->setToolTip(tooltip);
	button->setCursor(Qt::PointingHandCursor);
	button->setProperty("chatKind", kind);
	button->setToolButtonStyle(Qt::ToolButtonTextOnly);
	return button;
}


static QString plainTextFromChatHtml(const QString& html)
{
	QTextDocument doc;
	doc.setHtml(html);
	return doc.toPlainText().trimmed();
}


static QToolButton* makeCompactChatActionButton(QWidget* parent, const QString& text, const QString& tooltip)
{
	QToolButton* button = makeChatToolButton(parent, text, tooltip, "quick");
	button->setFixedSize(28, 28);
	button->setToolButtonStyle(Qt::ToolButtonTextOnly);
	button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
	return button;
}


static bool isChatImageAttachmentPath(const QString& path)
{
	const QString lower = QFileInfo(path).suffix().toLower();
	return lower == "png" || lower == "jpg" || lower == "jpeg" || lower == "webp" || lower == "bmp" || lower == "gif";
}


static QStringList extractChatFileURLTokens(const QString& plain_text)
{
	QStringList result;
	static const QRegularExpression file_url_re(QStringLiteral("file://\\S+"));
	QRegularExpressionMatchIterator it = file_url_re.globalMatch(plain_text);
	while(it.hasNext())
		result << it.next().captured(0);
	return result;
}


static QString localPathFromChatFileURL(const QString& file_url)
{
	const QUrl url(file_url);
	QString local_path = url.toLocalFile();
	if(local_path.isEmpty() && file_url.startsWith(QStringLiteral("file://"), Qt::CaseInsensitive))
	{
		local_path = QUrl::fromPercentEncoding(file_url.mid(7).toUtf8());
		if(local_path.startsWith('/') && local_path.size() > 2 && local_path[2] == ':')
			local_path.remove(0, 1);
	}
	return QDir::toNativeSeparators(local_path);
}


struct ChatAttachmentRef
{
	QString token;
	QString display_name;
	QString local_path;
	QString resource_url;
};


static QString metasiberiaChatPercentEncodeQt(const QString& value)
{
	return QString::fromLatin1(QUrl::toPercentEncoding(value));
}


static QString metasiberiaChatPercentDecodeQt(const QString& value)
{
	return QUrl::fromPercentEncoding(value.toUtf8());
}


static QString makeMetasiberiaChatAttachmentMarker(const QString& resource_url, const QString& display_name)
{
	return QStringLiteral("[[ms_file_v1|url=%1|name=%2]]")
		.arg(metasiberiaChatPercentEncodeQt(resource_url), metasiberiaChatPercentEncodeQt(display_name));
}


static QList<ChatAttachmentRef> extractChatAttachmentRefs(const QString& plain_text, GUIClient& gui_client)
{
	QList<ChatAttachmentRef> refs;

	const QStringList file_urls = extractChatFileURLTokens(plain_text);
	for(const QString& file_url : file_urls)
	{
		ChatAttachmentRef ref;
		ref.token = file_url;
		ref.local_path = localPathFromChatFileURL(file_url);
		ref.display_name = QFileInfo(ref.local_path).fileName();
		if(ref.display_name.isEmpty())
			ref.display_name = file_url;
		refs.push_back(ref);
	}

	static const QRegularExpression marker_re(QStringLiteral("\\[\\[ms_file_v1\\|url=([^\\]|]+)(?:\\|name=([^\\]]*))?\\]\\]"));
	QRegularExpressionMatchIterator it = marker_re.globalMatch(plain_text);
	while(it.hasNext())
	{
		const QRegularExpressionMatch match = it.next();
		const QString resource_url = metasiberiaChatPercentDecodeQt(match.captured(1));
		if(resource_url.isEmpty())
			continue;

		ChatAttachmentRef ref;
		ref.token = match.captured(0);
		ref.resource_url = resource_url;
		ref.display_name = metasiberiaChatPercentDecodeQt(match.captured(2));
		if(ref.display_name.isEmpty())
			ref.display_name = QFileInfo(resource_url).fileName();
		if(ref.display_name.isEmpty())
			ref.display_name = resource_url;

		try
		{
			const URLString url(QtUtils::toStdString(resource_url));
			if(gui_client.resource_manager.nonNull() && ResourceManager::isValidURL(url) && gui_client.resource_manager->isFileForURLPresent(url))
				ref.local_path = QDir::toNativeSeparators(QtUtils::toQString(gui_client.resource_manager->pathForURL(url)));
		}
		catch(glare::Exception&)
		{}

		refs.push_back(ref);
	}

	return refs;
}


static QColor mixChatColour(const QColor& a, const QColor& b, double b_weight)
{
	const double t = myClamp(b_weight, 0.0, 1.0);
	return QColor(
		(int)(a.red()   * (1.0 - t) + b.red()   * t),
		(int)(a.green() * (1.0 - t) + b.green() * t),
		(int)(a.blue()  * (1.0 - t) + b.blue()  * t),
		255
	);
}


static QString cssChatColour(const QColor& colour)
{
	return colour.name(QColor::HexRgb);
}


static QString chatMessageRowStyle(bool private_message, bool attachment_message, bool system_message, bool reply_message)
{
	const QPalette palette = QApplication::palette();
	const QColor base = palette.color(QPalette::Base);
	const QColor text = palette.color(QPalette::Text);
	const QColor mid = palette.color(QPalette::Mid);
	const QColor highlight = palette.color(QPalette::Highlight);
	const QColor link = palette.color(QPalette::Link);

	QColor background = base;
	QColor border = mid;
	if(system_message)
	{
		background = mixChatColour(base, QColor(52, 199, 89), 0.14);
		border = mixChatColour(mid, QColor(52, 199, 89), 0.55);
	}
	if(attachment_message)
	{
		background = mixChatColour(base, QColor(255, 204, 0), 0.16);
		border = mixChatColour(mid, QColor(255, 204, 0), 0.55);
	}
	if(private_message)
	{
		background = mixChatColour(base, QColor(124, 58, 237), 0.16);
		border = mixChatColour(mid, QColor(124, 58, 237), 0.6);
	}
	if(reply_message)
	{
		background = mixChatColour(background, highlight, 0.10);
		border = mixChatColour(border, link, 0.55);
	}

	return QString(
		"QFrame#chatMessageRow { background: %1; color: %2; border: 1px solid %3; border-left: 4px solid %3; border-radius: 7px; }"
		"QFrame#chatMessageRow:hover { border-color: %4; }"
	).arg(cssChatColour(background), cssChatColour(text), cssChatColour(border), cssChatColour(highlight));
}


static QString makeNetworkSafeChatMessage(const QString& raw_text)
{
	QString result = raw_text.trimmed();
	const QStringList file_urls = extractChatFileURLTokens(result);
	QStringList filenames;
	for(const QString& file_url : file_urls)
	{
		const QFileInfo info(QUrl(file_url).toLocalFile());
		filenames << (info.fileName().isEmpty() ? QString::fromUtf8("файл") : info.fileName());
		result.replace(file_url, QString());
	}
	result = result.simplified();
	if(!filenames.isEmpty())
	{
		if(!result.isEmpty())
			result += " ";
		result += QString::fromUtf8("[вложение: %1]").arg(filenames.join(", "));
	}
	if(result.size() > 900)
		result = result.left(900) + QString::fromUtf8("...");
	return result;
}


static bool isPrivateChatMessageHtml(const QString& html)
{
	const QString plain = plainTextFromChatHtml(html);
	if(plain.startsWith(QStringLiteral("Private from ")) || plain.startsWith(QStringLiteral("Private to ")))
		return true;
	return plain.startsWith(QString::fromUtf8("Лично от ")) || plain.startsWith(QString::fromUtf8("Лично для "));
}


static bool isIncomingPrivateChatMessageHtml(const QString& html)
{
	const QString plain = plainTextFromChatHtml(html);
	return plain.startsWith(QStringLiteral("Private from ")) || plain.startsWith(QString::fromUtf8("Лично от "));
}


static QString privateChatPeerFromPlainText(const QString& plain_text)
{
	for(const QString& prefix : {
		QStringLiteral("Private to "),
		QStringLiteral("Private from "),
		QString::fromUtf8("Лично для "),
		QString::fromUtf8("Лично от ")
	})
	{
		if(plain_text.startsWith(prefix))
		{
			QString peer = plain_text.mid(prefix.size()).trimmed();
			const int colon_pos = peer.indexOf(':');
			if(colon_pos >= 0)
				peer = peer.left(colon_pos).trimmed();
			return peer;
		}
	}
	return QString();
}


static bool chatPeerMatches(const QString& a, const QString& b)
{
	return !a.isEmpty() && !b.isEmpty() && QString::compare(a.trimmed(), b.trimmed(), Qt::CaseInsensitive) == 0;
}


static QString chatPeerKey(const QString& peer)
{
	return peer.trimmed().toLower();
}


static QString privateChatMessageBodyFromPlainText(const QString& plain_text)
{
	const int colon_pos = plain_text.indexOf(':');
	if(colon_pos < 0)
		return plain_text.trimmed();
	return plain_text.mid(colon_pos + 1).trimmed();
}


static std::vector<AvatarNameInfo> collectChatAvatarNameInfos(GUIClient& gui_client, bool include_self)
{
	std::vector<AvatarNameInfo> names;
	if(gui_client.world_state.isNull())
		return names;

	Lock lock(gui_client.world_state->mutex);
	for(auto entry : gui_client.world_state->avatars)
	{
		AvatarNameInfo info;
		info.name       = entry.second->getUseName();
		info.route_name = entry.second->name;
		info.colour     = entry.second->name_colour;
		info.avatar_uid = entry.second->uid;
		info.is_self    = entry.second->uid == gui_client.client_avatar_uid;
		if(include_self || !info.is_self)
			names.push_back(info);
	}

	std::sort(names.begin(), names.end());
	return names;
}


static bool getChatAvatarNameInfo(GUIClient& gui_client, UID avatar_uid, AvatarNameInfo& info_out)
{
	if(!avatar_uid.valid() || gui_client.world_state.isNull())
		return false;

	Lock lock(gui_client.world_state->mutex);
	auto res = gui_client.world_state->avatars.find(avatar_uid);
	if(res == gui_client.world_state->avatars.end())
		return false;

	info_out.name       = res->second->getUseName();
	info_out.route_name = res->second->name;
	info_out.colour     = res->second->name_colour;
	info_out.avatar_uid = res->second->uid;
	info_out.is_self    = res->second->uid == gui_client.client_avatar_uid;
	return true;
}


void MainWindow::applyChatThemeStylesheet()
{
	if(!ui || !ui->chatWidget)
		return;

	const QPalette palette = QApplication::palette();
	const QColor window = palette.color(QPalette::Window);
	const QColor base = palette.color(QPalette::Base);
	const QColor alt_base = palette.color(QPalette::AlternateBase);
	const QColor text = palette.color(QPalette::Text);
	const QColor muted = palette.color(QPalette::PlaceholderText);
	const QColor mid = palette.color(QPalette::Mid);
	const QColor highlight = palette.color(QPalette::Highlight);
	const QColor highlighted_text = palette.color(QPalette::HighlightedText);
	const QColor input_background = mixChatColour(base, window, 0.25);
	const QColor hover_background = mixChatColour(base, highlight, 0.12);
	const QColor messages_background = mixChatColour(base, QColor(52, 199, 89), 0.08);

	ui->chatWidget->setStyleSheet(QString(
		"QWidget#chatWidget { background: %1; color: %2; }"
		"QFrame#chatRoot { background: %1; }"
		"QTabBar::tab { background: %3; color: %2; border: 1px solid %4; border-bottom-color: %4; border-top-left-radius: 7px; border-top-right-radius: 7px; padding: 5px 8px; margin-right: 2px; }"
		"QTabBar::tab:selected { background: %5; color: %2; border-bottom-color: %5; }"
		"QTabBar::tab:hover { background: %6; }"
		"QFrame#chatUsersPanel, QFrame#chatMainPanel, QFrame#chatSettingsPage, QFrame#chatGroupsPage { background: %5; border: 1px solid %4; border-radius: 7px; }"
		"QFrame#chatInputPanel { background: %8; border: 1px solid %4; border-radius: 17px; }"
		"QFrame#chatUserRow { background: %5; border: 1px solid transparent; border-radius: 6px; }"
		"QFrame#chatUserRow:hover { background: %6; border-color: %7; }"
		"QFrame#chatMessagesPage { background: %9; border: 1px solid %4; border-radius: 7px; }"
		"QLabel#chatMessageBody { color: %2; }"
		"QLabel#chatReplyPreview { color: %7; background: %6; border-left: 3px solid %7; border-radius: 5px; padding: 4px 7px; }"
		"QLabel#chatMessageTimeLabel { color: %10; font-size: 11px; }"
		"QLabel#chatOnlineDot { color: #34c759; font-size: 18px; }"
		"QLabel#chatMutedText { color: %10; }"
		"QTextEdit#chatMessagesTextEdit { background: %5; border: 1px solid %4; border-radius: 6px; padding: 10px; color: %2; }"
		"QLineEdit#chatMessageLineEdit { background: transparent; border: none; padding: 6px 4px; color: %2; min-height: 24px; }"
		"QLineEdit#chatMessageLineEdit:focus { border: none; }"
		"QLineEdit, QComboBox { background: %8; border: 1px solid %4; border-radius: 5px; padding: 5px 7px; color: %2; }"
		"QLineEdit:focus, QComboBox:focus { border-color: %7; }"
		"QScrollArea { border: none; background: transparent; }"
		"QScrollBar:vertical { background: %3; width: 9px; margin: 0px; border-radius: 4px; }"
		"QScrollBar::handle:vertical { background: %4; min-height: 34px; border-radius: 4px; }"
		"QToolButton[chatKind=\"toolbar\"], QPushButton[chatKind=\"toolbar\"] { background: %5; border: 1px solid %4; border-radius: 5px; padding: 0px; color: %2; min-width: 26px; min-height: 26px; }"
		"QToolButton[chatKind=\"toolbar\"]:hover, QPushButton[chatKind=\"toolbar\"]:hover { background: %6; border-color: %7; }"
		"QToolButton[chatKind=\"icon\"] { background: %5; border: 1px solid %4; border-radius: 5px; padding: 4px; color: %2; min-width: 24px; min-height: 24px; }"
		"QToolButton[chatKind=\"icon\"]:hover { background: %6; border-color: %7; }"
		"QToolButton[chatKind=\"inputIcon\"] { background: transparent; border: none; border-radius: 13px; padding: 0px; color: %10; min-width: 26px; min-height: 26px; }"
		"QToolButton[chatKind=\"inputIcon\"]:hover { background: %6; color: %2; }"
		"QToolButton[chatKind=\"quick\"] { background: %5; border: 1px solid %4; border-radius: 7px; padding: 0px; color: %2; font-size: 17px; }"
		"QToolButton[chatKind=\"quick\"]:hover { background: %6; border-color: %7; }"
		"QPushButton[chatKind=\"primary\"] { background: %5; color: %2; border: 1px solid %4; border-radius: 15px; padding: 0px; font-size: 15px; font-weight: 700; }"
		"QPushButton[chatKind=\"primary\"]:hover { background: %6; border-color: %7; }"
		"QMenu { background: %5; border: 1px solid %4; padding: 6px; color: %2; }"
		"QMenu::item { padding: 7px 28px 7px 12px; border-radius: 4px; }"
		"QMenu::item:selected { background: %7; color: %11; }"
	)
	.arg(cssChatColour(window))
	.arg(cssChatColour(text))
	.arg(cssChatColour(alt_base))
	.arg(cssChatColour(mid))
	.arg(cssChatColour(base))
	.arg(cssChatColour(hover_background))
	.arg(cssChatColour(highlight))
	.arg(cssChatColour(input_background))
	.arg(cssChatColour(messages_background))
	.arg(cssChatColour(muted))
	.arg(cssChatColour(highlighted_text))
	.arg(cssChatColour(mixChatColour(highlight, text, 0.12))));

	applyChatMessageDisplaySettings();
}


void MainWindow::setupChatPlayerControls()
{
	if(!ui || !ui->verticalLayout_2)
		return;

	ui->chatDockWidget->setWindowTitle(tr("Чат"));
	ui->chatDockWidget->setMinimumSize(170, 150);
	ui->chatDockWidget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
	ui->chatDockWidgetContents->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	ui->chatWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	ui->chatDockWidget->resize(360, 360);
	ui->chatWidget->setObjectName("chatWidget");
	ui->chatWidget->setAutoFillBackground(true);
	applyChatThemeStylesheet();

	detachLayoutItems(ui->verticalLayout_2);
	detachLayoutItems(ui->horizontalLayout);
	if(ui->label)
		ui->label->hide();
	if(ui->onlineUsersTextEdit)
		ui->onlineUsersTextEdit->hide();
	if(ui->widget)
		ui->widget->hide();

	QFrame* chat_root = new QFrame(ui->chatWidget);
	chat_root->setObjectName("chatRoot");
	QVBoxLayout* root_layout = new QVBoxLayout(chat_root);
	root_layout->setContentsMargins(8, 8, 8, 8);
	root_layout->setSpacing(6);

	chat_tabs_bar = new QTabBar(chat_root);
	chat_tabs_bar->setExpanding(false);
	chat_tabs_bar->setDrawBase(false);
	chat_tabs_bar->setUsesScrollButtons(true);
	chat_tabs_bar->setElideMode(Qt::ElideRight);
	chat_tabs_bar->addTab(tr("Игроки"));
	chat_tabs_bar->addTab(tr("Чат"));
	chat_tabs_bar->addTab(tr("Личные"));
	chat_tabs_bar->addTab(tr("Группы"));
	chat_tabs_bar->addTab(QString::fromUtf8("🔔"));
	chat_tabs_bar->setTabToolTip(4, tr("Уведомления"));
	chat_tabs_bar->addTab(QString::fromUtf8("⚙"));
	chat_tabs_bar->setTabToolTip(5, tr("Настройки"));
	root_layout->addWidget(chat_tabs_bar);

	QSplitter* body_splitter = new QSplitter(Qt::Horizontal, chat_root);
	chat_body_splitter = body_splitter;
	body_splitter->setChildrenCollapsible(true);
	body_splitter->setHandleWidth(5);
	body_splitter->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	root_layout->addWidget(body_splitter, 1);

	QFrame* users_panel = new QFrame(body_splitter);
	chat_users_panel = users_panel;
	users_panel->setObjectName("chatUsersPanel");
	users_panel->setMinimumWidth(52);
	users_panel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
	QVBoxLayout* users_layout = new QVBoxLayout(users_panel);
	users_layout->setContentsMargins(5, 5, 5, 5);
	users_layout->setSpacing(5);

	QHBoxLayout* users_header_layout = new QHBoxLayout();
	users_header_layout->setContentsMargins(0, 0, 0, 0);
	users_header_layout->setSpacing(6);
	QLabel* online_dot = new QLabel(QString::fromUtf8("●"), users_panel);
	online_dot->setObjectName("chatOnlineDot");
	chat_online_count_label = new QLabel(tr("Онлайн (0)"), users_panel);
	chat_online_count_label->setStyleSheet("QLabel { font-weight: 700; }");
	chat_online_count_label->setMinimumWidth(0);
	chat_online_count_label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	QToolButton* refresh_users_button = makeChatToolButton(users_panel, QString::fromUtf8("↻"), tr("Обновить список игроков"), "icon");
	QToolButton* users_options_button = makeChatToolButton(users_panel, QString::fromUtf8("⌄"), tr("Параметры списка игроков"), "icon");
	users_header_layout->addWidget(online_dot);
	users_header_layout->addWidget(chat_online_count_label, 1);
	users_header_layout->addWidget(refresh_users_button);
	users_header_layout->addWidget(users_options_button);
	users_layout->addLayout(users_header_layout);

	chat_player_search_edit = new QLineEdit(users_panel);
	chat_player_search_edit->setPlaceholderText(tr("Поиск игрока..."));
	chat_player_search_edit->setClearButtonEnabled(true);
	chat_player_search_edit->setToolTip(tr("Фильтр списка игроков по нику"));
	users_layout->addWidget(chat_player_search_edit);

	QScrollArea* users_scroll = new QScrollArea(users_panel);
	users_scroll->setWidgetResizable(true);
	users_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	users_scroll->setMinimumWidth(0);
	users_scroll->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
	QWidget* users_scroll_contents = new QWidget(users_scroll);
	chat_users_list_layout = new QVBoxLayout(users_scroll_contents);
	chat_users_list_layout->setContentsMargins(0, 0, 0, 0);
	chat_users_list_layout->setSpacing(8);
	users_scroll->setWidget(users_scroll_contents);
	users_layout->addWidget(users_scroll, 1);

	QPushButton* update_users_button = new QPushButton(QString::fromUtf8("↻"), users_panel);
	update_users_button->setProperty("chatKind", "toolbar");
	update_users_button->setToolTip(tr("Обновить список игроков"));
	update_users_button->setCursor(Qt::PointingHandCursor);
	update_users_button->setFixedHeight(28);
	users_layout->addWidget(update_users_button);

	QFrame* chat_panel = new QFrame(body_splitter);
	chat_main_panel = chat_panel;
	chat_panel->setObjectName("chatMainPanel");
	QVBoxLayout* chat_layout = new QVBoxLayout(chat_panel);
	chat_layout->setContentsMargins(10, 10, 10, 10);
	chat_layout->setSpacing(10);

	QHBoxLayout* toolbar_layout = new QHBoxLayout();
	toolbar_layout->setContentsMargins(0, 0, 0, 0);
	toolbar_layout->setSpacing(8);
	QToolButton* filter_button = makeChatToolButton(chat_panel, QString::fromUtf8("⌕"), tr("Фильтр сообщений"), "toolbar");
	QToolButton* clear_button = makeChatToolButton(chat_panel, QString::fromUtf8("⌫"), tr("Очистить текущую историю сообщений"), "toolbar");
	for(QToolButton* button : { filter_button, clear_button })
		button->setFixedSize(28, 28);
	toolbar_layout->addWidget(filter_button);
	toolbar_layout->addStretch(1);
	toolbar_layout->addWidget(clear_button);
	chat_layout->addLayout(toolbar_layout);

	ui->chatMessagesTextEdit->setParent(chat_panel);
	ui->chatMessagesTextEdit->setReadOnly(true);
	ui->chatMessagesTextEdit->setFrameShape(QFrame::NoFrame);
	ui->chatMessagesTextEdit->setAcceptRichText(true);
	ui->chatMessagesTextEdit->setMinimumHeight(230);
	ui->chatMessagesTextEdit->hide();

	chat_messages_page = new QFrame(chat_panel);
	chat_messages_page->setObjectName("chatMessagesPage");
	QVBoxLayout* messages_page_layout = new QVBoxLayout(chat_messages_page);
	messages_page_layout->setContentsMargins(8, 8, 8, 8);
	messages_page_layout->setSpacing(8);
	chat_messages_scroll_area = new QScrollArea(chat_messages_page);
	chat_messages_scroll_area->setWidgetResizable(true);
	chat_messages_scroll_area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	QWidget* messages_scroll_contents = new QWidget(chat_messages_scroll_area);
	chat_messages_list_layout = new QVBoxLayout(messages_scroll_contents);
	chat_messages_list_layout->setContentsMargins(8, 8, 8, 8);
	chat_messages_list_layout->setSpacing(8);
	chat_messages_list_layout->addStretch(1);
	chat_messages_scroll_area->setWidget(messages_scroll_contents);
	messages_page_layout->addWidget(chat_messages_scroll_area);
	chat_layout->addWidget(chat_messages_page, 1);

	chat_groups_page = new QFrame(chat_panel);
	chat_groups_page->setObjectName("chatGroupsPage");
	QVBoxLayout* groups_page_layout = new QVBoxLayout(chat_groups_page);
	groups_page_layout->setContentsMargins(8, 8, 8, 8);
	groups_page_layout->setSpacing(8);
	QHBoxLayout* groups_header_layout = new QHBoxLayout();
	groups_header_layout->setContentsMargins(0, 0, 0, 0);
	groups_header_layout->setSpacing(6);
	QLabel* groups_title_label = new QLabel(tr("Группы"), chat_groups_page);
	groups_title_label->setStyleSheet("QLabel { font-weight: 700; }");
	QToolButton* create_group_button = makeChatToolButton(chat_groups_page, QString::fromUtf8("+"), tr("Создать группу"), "toolbar");
	create_group_button->setFixedSize(28, 28);
	groups_header_layout->addWidget(groups_title_label, 1);
	groups_header_layout->addWidget(create_group_button);
	groups_page_layout->addLayout(groups_header_layout);

	QScrollArea* groups_scroll_area = new QScrollArea(chat_groups_page);
	groups_scroll_area->setWidgetResizable(true);
	groups_scroll_area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	QWidget* groups_scroll_contents = new QWidget(groups_scroll_area);
	chat_groups_list_layout = new QVBoxLayout(groups_scroll_contents);
	chat_groups_list_layout->setContentsMargins(0, 0, 0, 0);
	chat_groups_list_layout->setSpacing(8);
	groups_scroll_area->setWidget(groups_scroll_contents);
	groups_page_layout->addWidget(groups_scroll_area, 1);
	chat_layout->addWidget(chat_groups_page, 1);
	chat_groups_page->hide();

	chat_settings_page = new QFrame(chat_panel);
	chat_settings_page->setObjectName("chatSettingsPage");
	QVBoxLayout* settings_layout = new QVBoxLayout(chat_settings_page);
	settings_layout->setContentsMargins(12, 12, 12, 12);
	settings_layout->setSpacing(8);
	QCheckBox* sound_enabled_cb = new QCheckBox(tr("Звук включен"), chat_settings_page);
	sound_enabled_cb->setChecked(ui->actionMute_Audio ? !ui->actionMute_Audio->isChecked() : true);
	QCheckBox* notifications_cb = new QCheckBox(tr("Уведомления включены"), chat_settings_page);
	notifications_cb->setChecked(settings ? settings->value("chat/notifications_enabled", true).toBool() : true);
	QCheckBox* timestamps_cb = new QCheckBox(tr("Показывать время сообщений"), chat_settings_page);
	timestamps_cb->setChecked(settings ? settings->value("chat/show_timestamps", true).toBool() : true);
	QCheckBox* compact_cb = new QCheckBox(tr("Компактная лента сообщений"), chat_settings_page);
	compact_cb->setChecked(settings ? settings->value("chat/compact_messages", false).toBool() : false);
	QCheckBox* network_private_cb = new QCheckBox(tr("Сетевые личные сообщения"), chat_settings_page);
	network_private_cb->setToolTip(tr("Включайте только если сервер обновлён и поддерживает личные сообщения."));
	network_private_cb->setChecked(settings ? settings->value("chat/network_private_messages_enabled_v2", true).toBool() : true);
	QPushButton* clear_history_button = new QPushButton(tr("Очистить историю чата"), chat_settings_page);
	clear_history_button->setProperty("chatKind", "toolbar");
	QPushButton* export_history_button = new QPushButton(tr("Экспортировать историю HTML"), chat_settings_page);
	export_history_button->setProperty("chatKind", "toolbar");
	QPushButton* blocked_players_button = new QPushButton(tr("Заблокированные игроки"), chat_settings_page);
	blocked_players_button->setProperty("chatKind", "toolbar");
	QPushButton* report_button = new QPushButton(tr("Пожаловаться"), chat_settings_page);
	report_button->setProperty("chatKind", "toolbar");
	QPushButton* pin_chat_button = new QPushButton(tr("Закрепить чат"), chat_settings_page);
	pin_chat_button->setProperty("chatKind", "toolbar");
	QPushButton* help_button = new QPushButton(tr("Помощь"), chat_settings_page);
	help_button->setProperty("chatKind", "toolbar");
	settings_layout->addWidget(sound_enabled_cb);
	settings_layout->addWidget(notifications_cb);
	settings_layout->addWidget(timestamps_cb);
	settings_layout->addWidget(compact_cb);
	settings_layout->addWidget(network_private_cb);
	settings_layout->addSpacing(6);
	settings_layout->addWidget(blocked_players_button);
	settings_layout->addWidget(report_button);
	settings_layout->addWidget(pin_chat_button);
	settings_layout->addWidget(help_button);
	settings_layout->addSpacing(6);
	settings_layout->addWidget(clear_history_button);
	settings_layout->addWidget(export_history_button);
	settings_layout->addStretch(1);
	chat_layout->addWidget(chat_settings_page, 1);
	chat_settings_page->hide();

	chat_notifications_enabled = notifications_cb->isChecked();
	chat_show_timestamps = timestamps_cb->isChecked();
	chat_compact_message_view = compact_cb->isChecked();
	chat_network_private_messages_enabled = network_private_cb->isChecked();

	chat_user_combo = NULL;
	chat_private_button = NULL;
	chat_goto_user_button = NULL;

	QToolButton* quick_attach_button = makeCompactChatActionButton(chat_panel, QString::fromUtf8("📎"), tr("Прикрепить файл"));
	QToolButton* quick_emoji_button = makeCompactChatActionButton(chat_panel, QString::fromUtf8("☺"), tr("Эмодзи"));
	QToolButton* quick_more_button = makeCompactChatActionButton(chat_panel, QString::fromUtf8("⋯"), tr("Дополнительные способы отправки"));
	for(QToolButton* button : { quick_attach_button, quick_emoji_button, quick_more_button })
	{
		button->setProperty("chatKind", "inputIcon");
		button->setFixedSize(26, 26);
	}

	QFrame* input_panel = new QFrame(chat_panel);
	chat_input_panel = input_panel;
	input_panel->setObjectName("chatInputPanel");
	QHBoxLayout* input_layout = new QHBoxLayout(input_panel);
	input_layout->setContentsMargins(6, 4, 6, 4);
	input_layout->setSpacing(4);

	ui->chatEmojiButton->setParent(input_panel);
	ui->chatEmojiButton->setProperty("chatKind", "icon");
	ui->chatEmojiButton->setToolTip(tr("Эмодзи"));
	ui->chatEmojiButton->hide();
	ui->chatMessageLineEdit->setParent(input_panel);
	ui->chatMessageLineEdit->setPlaceholderText(tr("Введите сообщение..."));
	ui->chatPushButton->setParent(input_panel);
	ui->chatPushButton->setText(QString::fromUtf8("\xE2\x8F\x8E"));
	ui->chatPushButton->setToolTip(tr("Отправить сообщение"));
	ui->chatPushButton->setProperty("chatKind", "primary");
	ui->chatPushButton->setCursor(Qt::PointingHandCursor);
	ui->chatPushButton->setFixedSize(34, 30);

	input_layout->addWidget(quick_attach_button);
	input_layout->addWidget(ui->chatMessageLineEdit, 1);
	input_layout->addWidget(quick_emoji_button);
	input_layout->addWidget(quick_more_button);
	input_layout->addWidget(ui->chatPushButton);
	chat_layout->addWidget(input_panel);
	body_splitter->addWidget(users_panel);
	body_splitter->addWidget(chat_panel);
	body_splitter->setStretchFactor(0, 0);
	body_splitter->setStretchFactor(1, 1);
	body_splitter->setSizes(QList<int>() << 110 << 260);
	ui->verticalLayout_2->setContentsMargins(0, 0, 0, 0);
	ui->verticalLayout_2->setSpacing(0);
	ui->verticalLayout_2->addWidget(chat_root);

	connect(chat_player_search_edit, &QLineEdit::textChanged, this, [this]() { rebuildChatUserRows(); });
	connect(refresh_users_button, &QToolButton::clicked, this, [this]() { refreshChatPlayerControls(); showInfoNotification("Список игроков обновлен."); });
	connect(update_users_button, &QPushButton::clicked, this, [this]() { refreshChatPlayerControls(); showInfoNotification("Список игроков обновлен."); });
	connect(users_options_button, &QToolButton::clicked, this, [this, users_options_button]() {
		QMenu menu(this);
		QAction* sort_action = menu.addAction(tr("Сортировка по нику"));
		sort_action->setCheckable(true);
		sort_action->setChecked(true);
		QAction* show_offline = menu.addAction(tr("Показывать offline"));
		show_offline->setEnabled(false);
		menu.addSeparator();
		menu.addAction(tr("Поиск игроков"))->setEnabled(false);
		menu.exec(users_options_button->mapToGlobal(QPoint(0, users_options_button->height())));
	});
	connect(filter_button, &QToolButton::clicked, this, [this]() {
		if(chat_tabs_bar && chat_tabs_bar->currentIndex() == 2 && chat_private_conversation_open)
		{
			chat_private_conversation_open = false;
			updateChatLayoutForCurrentTab();
			return;
		}
		if(chat_tabs_bar)
			chat_tabs_bar->setCurrentIndex(0);
		if(chat_player_search_edit)
			chat_player_search_edit->setFocus();
	});
	connect(clear_button, &QToolButton::clicked, this, [this]() { clearChatMessageWidgets(); });
	connect(create_group_button, &QToolButton::clicked, this, &MainWindow::createChatGroup);
	connect(quick_attach_button, &QToolButton::clicked, this, &MainWindow::showChatAttachmentMenu);
	connect(quick_emoji_button, &QToolButton::clicked, this, &MainWindow::toggleChatEmojiPopup);
	connect(quick_more_button, &QToolButton::clicked, this, &MainWindow::showChatMoreSendMenu);
	connect(blocked_players_button, &QPushButton::clicked, this, [this]() { showInfoNotification("Список заблокированных игроков пока пуст."); });
	connect(report_button, &QPushButton::clicked, this, [this]() { showInfoNotification("Жалоба будет отправлена модераторам после выбора сообщения или игрока."); });
	connect(pin_chat_button, &QPushButton::clicked, this, [this]() { showInfoNotification("Чат закреплен в текущей области."); });
	connect(help_button, &QPushButton::clicked, this, [this]() {
		if(ui && ui->helpInfoDockWidget)
		{
			ui->helpInfoDockWidget->show();
			ui->helpInfoDockWidget->raise();
		}
	});

	connect(sound_enabled_cb, &QCheckBox::toggled, this, [this](bool enabled) {
		if(ui && ui->actionMute_Audio && ui->actionMute_Audio->isChecked() == enabled)
			ui->actionMute_Audio->setChecked(!enabled);
		if(settings)
			settings->setValue("chat/sound_enabled", enabled);
	});
	if(ui && ui->actionMute_Audio)
	{
		connect(ui->actionMute_Audio, &QAction::toggled, sound_enabled_cb, [sound_enabled_cb](bool muted) {
			sound_enabled_cb->setChecked(!muted);
		});
	}
	connect(notifications_cb, &QCheckBox::toggled, this, [this](bool enabled) {
		chat_notifications_enabled = enabled;
		if(settings)
			settings->setValue("chat/notifications_enabled", enabled);
		showInfoNotification(enabled ? "Уведомления чата включены." : "Уведомления чата выключены.");
	});
	connect(timestamps_cb, &QCheckBox::toggled, this, [this](bool enabled) {
		chat_show_timestamps = enabled;
		if(settings)
			settings->setValue("chat/show_timestamps", enabled);
		applyChatMessageDisplaySettings();
	});
	connect(compact_cb, &QCheckBox::toggled, this, [this](bool enabled) {
		chat_compact_message_view = enabled;
		if(settings)
			settings->setValue("chat/compact_messages", enabled);
		applyChatMessageDisplaySettings();
	});
	connect(network_private_cb, &QCheckBox::toggled, this, [this](bool enabled) {
		chat_network_private_messages_enabled = enabled;
		if(settings)
			settings->setValue("chat/network_private_messages_enabled_v2", enabled);
		showInfoNotification(enabled ? "Сетевые личные сообщения включены." : "Сетевые личные сообщения выключены для защиты сервера.");
	});
	connect(clear_history_button, &QPushButton::clicked, this, [this]() { clearPersistentChatHistory(); });
	connect(export_history_button, &QPushButton::clicked, this, [this]() {
		const QString filename = QFileDialog::getSaveFileName(this, tr("Экспортировать историю чата"), QString(), tr("HTML (*.html);;Текст (*.txt);;Все файлы (*.*)"));
		if(filename.isEmpty())
			return;

		QFile file(filename);
		if(!file.open(QIODevice::WriteOnly | QIODevice::Text))
		{
			showErrorNotification("Не удалось сохранить историю чата.");
			return;
		}
		const QByteArray data = filename.endsWith(".txt", Qt::CaseInsensitive) ?
			ui->chatMessagesTextEdit->toPlainText().toUtf8() :
			ui->chatMessagesTextEdit->toHtml().toUtf8();
		file.write(data);
		showInfoNotification("История чата экспортирована.");
	});
	connect(chat_tabs_bar, &QTabBar::currentChanged, this, [this](int index) {
		if(index == 2 && !chat_switching_to_private_conversation)
			chat_private_conversation_open = false;
		if(index != 2)
			chat_private_conversation_open = false;
		if(index == 4)
			showInfoNotification("Уведомления чата включаются во вкладке настроек.");
		updateChatLayoutForCurrentTab();
	});
	connect(chat_tabs_bar, &QTabBar::tabBarClicked, this, [this](int index) {
		if(index == 2 && !chat_switching_to_private_conversation)
		{
			chat_private_conversation_open = false;
			updateChatLayoutForCurrentTab();
		}
	});
#if 0
	connect(chat_tabs_bar, &QTabBar::currentChanged, this, [this, users_panel, chat_panel, body_splitter](int index) {
		const bool settings_tab = index == 5;
		const bool players_tab = index == 0;
		const bool chat_content_tab = index == 1 || index == 2 || index == 3 || index == 4 || settings_tab;
		setChatSettingsVisible(settings_tab);
		chat_showing_private_messages = index == 2;
		if(index == 1)
		{
			chat_showing_private_messages = false;
			chat_unread_count = 0;
			updateChatTabText();
		}
		if(index == 2)
		{
			chat_private_unread_count = 0;
			updatePrivateChatTabText();
			if(ui && ui->chatMessageLineEdit)
			{
				const QString recipient = QtUtils::toQString(chat_private_recipient_name);
				ui->chatMessageLineEdit->setPlaceholderText(recipient.isEmpty() ? tr("Выберите игрока слева для личного сообщения...") : tr("Личное сообщение: %1").arg(recipient));
			}
		}
		else if(index != 5 && ui && ui->chatMessageLineEdit)
			ui->chatMessageLineEdit->setPlaceholderText(tr("Введите сообщение..."));
		users_panel->setVisible(players_tab);
		chat_panel->setVisible(chat_content_tab);
		if(players_tab && chat_player_search_edit)
			chat_player_search_edit->setFocus();
		if(index == 3)
			showInfoNotification("Групповые чаты будут подключены позже.");
		if(index == 4)
			showInfoNotification("Уведомления чата включаются во вкладке настроек.");
		if(players_tab && users_panel->isVisible() && body_splitter->sizes().value(0) < 52)
			body_splitter->setSizes(QList<int>() << 110 << myMax(220, body_splitter->width() - 110));
		updateChatMessageVisibility();
	});
#endif

	refreshChatPlayerControls();
	setChatSettingsVisible(false);
	applyChatMessageDisplaySettings();
	chat_tabs_bar->setCurrentIndex(1);
	loadChatHistoryFromDisk();
	updateChatLayoutForCurrentTab();
	updateChatTabText();
	updatePrivateChatTabText();
}


void MainWindow::refreshChatPlayerControls()
{
	if(chat_user_combo)
	{
		bool old_uid_ok = false;
		const qulonglong old_uid = chat_user_combo->currentData(Qt::UserRole).toULongLong(&old_uid_ok);
		const QString old_route_name = chat_user_combo->currentData(Qt::UserRole + 1).toString();

		std::vector<AvatarNameInfo> names = collectChatAvatarNameInfos(gui_client, /*include_self=*/false);

		QSignalBlocker blocker(chat_user_combo);
		chat_user_combo->clear();

		int restore_index = -1;
		for(size_t i=0; i<names.size(); ++i)
		{
			chat_user_combo->addItem(QtUtils::toQString(names[i].name), QVariant((qulonglong)names[i].avatar_uid.value()));
			const int item_index = chat_user_combo->count() - 1;
			chat_user_combo->setItemData(item_index, QtUtils::toQString(names[i].route_name), Qt::UserRole + 1);

			if((old_uid_ok && old_uid == (qulonglong)names[i].avatar_uid.value()) ||
				(!old_route_name.isEmpty() && old_route_name == QtUtils::toQString(names[i].route_name)))
				restore_index = item_index;
		}

		if(restore_index >= 0)
			chat_user_combo->setCurrentIndex(restore_index);

		const bool have_user = chat_user_combo->count() > 0;
		chat_user_combo->setEnabled(have_user);
		if(chat_private_button)
			chat_private_button->setEnabled(have_user);
		if(chat_goto_user_button)
			chat_goto_user_button->setEnabled(have_user);
	}

	rebuildChatUserRows();
}


void MainWindow::refreshPrivateChatUnreadCount()
{
	int total = 0;
	for(auto it = chat_private_dialogs.begin(); it != chat_private_dialogs.end(); ++it)
		total += myMax(0, it.value().unread_count);
	chat_private_unread_count = total;
	updatePrivateChatTabText();
}


void MainWindow::openPrivateChatDialog(const QString& peer_name, UID peer_uid)
{
	const QString peer = peer_name.trimmed();
	if(peer.isEmpty())
		return;

	const QString key = chatPeerKey(peer);
	ChatPrivateDialogState& dialog = chat_private_dialogs[key];
	dialog.peer_key = key;
	if(dialog.peer_display_name.isEmpty())
		dialog.peer_display_name = peer;
	if(peer_uid.valid())
		dialog.peer_uid = peer_uid;
	dialog.unread_count = 0;
	if(!dialog.last_time.isValid())
		dialog.last_time = QDateTime::currentDateTime();

	chat_private_recipient_uid = dialog.peer_uid;
	chat_private_recipient_name = QtUtils::toStdString(dialog.peer_display_name);
	chat_private_conversation_open = true;
	chat_showing_private_messages = true;
	refreshPrivateChatUnreadCount();

	if(chat_tabs_bar)
	{
		chat_switching_to_private_conversation = true;
		chat_tabs_bar->setCurrentIndex(2);
		chat_switching_to_private_conversation = false;
	}
	updateChatLayoutForCurrentTab();

	if(ui && ui->chatMessageLineEdit)
	{
		ui->chatMessageLineEdit->setPlaceholderText(tr("Личное сообщение: %1").arg(dialog.peer_display_name));
		ui->chatMessageLineEdit->setFocus();
	}
}


void MainWindow::notePrivateChatDialogFromPlainText(const QString& plain_text, bool incoming_message, bool count_unread)
{
	const QString peer = privateChatPeerFromPlainText(plain_text);
	if(peer.isEmpty())
		return;

	const QString key = chatPeerKey(peer);
	ChatPrivateDialogState& dialog = chat_private_dialogs[key];
	dialog.peer_key = key;
	dialog.peer_display_name = peer;
	dialog.last_message = privateChatMessageBodyFromPlainText(plain_text);
	if(dialog.last_message.size() > 180)
		dialog.last_message = dialog.last_message.left(180) + QString::fromUtf8("...");
	dialog.last_time = QDateTime::currentDateTime();
	if(incoming_message && count_unread)
		dialog.unread_count++;

	refreshPrivateChatUnreadCount();
	if(chat_tabs_bar && chat_tabs_bar->currentIndex() == 2 && !chat_private_conversation_open)
		rebuildChatUserRows();
}


void MainWindow::rebuildPrivateDialogRows(const QString& filter)
{
	if(!chat_users_list_layout)
		return;

	if(chat_online_count_label)
		chat_online_count_label->setText(tr("Личные (%1)").arg(chat_private_dialogs.size()));

	QVector<ChatPrivateDialogState> dialogs;
	dialogs.reserve(chat_private_dialogs.size());
	for(auto it = chat_private_dialogs.begin(); it != chat_private_dialogs.end(); ++it)
	{
		const ChatPrivateDialogState& dialog = it.value();
		if(!filter.isEmpty() &&
			!dialog.peer_display_name.toLower().contains(filter) &&
			!dialog.last_message.toLower().contains(filter))
			continue;
		dialogs.push_back(dialog);
	}

	std::sort(dialogs.begin(), dialogs.end(), [](const ChatPrivateDialogState& a, const ChatPrivateDialogState& b) {
		return a.last_time > b.last_time;
	});

	if(dialogs.isEmpty())
	{
		QLabel* empty_label = new QLabel(chat_private_dialogs.isEmpty() ?
			tr("Личных диалогов пока нет. Откройте диалог через вкладку «Игроки».") :
			tr("Диалоги не найдены."), ui->chatWidget);
		empty_label->setObjectName("chatMutedText");
		empty_label->setAlignment(Qt::AlignCenter);
		empty_label->setWordWrap(true);
		empty_label->setMinimumHeight(90);
		chat_users_list_layout->addWidget(empty_label);
		chat_users_list_layout->addStretch(1);
		return;
	}

	for(const ChatPrivateDialogState& dialog : dialogs)
	{
		QFrame* row = new QFrame(ui->chatWidget);
		row->setObjectName("chatUserRow");
		row->setCursor(Qt::PointingHandCursor);
		row->setProperty("chatPrivateListRow", true);
		row->setProperty("chatOpenPrivatePeer", dialog.peer_display_name);
		row->installEventFilter(this);
		row->setMinimumHeight(54);

		QHBoxLayout* row_layout = new QHBoxLayout(row);
		row_layout->setContentsMargins(5, 5, 5, 5);
		row_layout->setSpacing(6);

		QLabel* avatar = new QLabel(chatInitialsForName(QtUtils::toStdString(dialog.peer_display_name)), row);
		avatar->setFixedSize(34, 34);
		avatar->setAlignment(Qt::AlignCenter);
		avatar->setStyleSheet("QLabel { background: #8b5cf6; color: #ffffff; border-radius: 17px; font-weight: 700; }");
		avatar->setProperty("chatPrivateListRow", true);
		avatar->setProperty("chatOpenPrivatePeer", dialog.peer_display_name);
		avatar->installEventFilter(this);
		row_layout->addWidget(avatar);

		QVBoxLayout* text_layout = new QVBoxLayout();
		text_layout->setContentsMargins(0, 0, 0, 0);
		text_layout->setSpacing(2);
		QLabel* name_label = new QLabel(dialog.peer_display_name, row);
		name_label->setStyleSheet("QLabel { font-weight: 700; }");
		name_label->setMinimumWidth(0);
		name_label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
		QLabel* last_label = new QLabel(dialog.last_message.isEmpty() ? tr("Диалог открыт") : dialog.last_message, row);
		last_label->setObjectName("chatMutedText");
		last_label->setMinimumWidth(0);
		last_label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
		last_label->setWordWrap(false);
		text_layout->addWidget(name_label);
		text_layout->addWidget(last_label);
		row_layout->addLayout(text_layout, 1);

		if(dialog.unread_count > 0)
		{
			QLabel* unread_label = new QLabel(QString::number(dialog.unread_count), row);
			unread_label->setAlignment(Qt::AlignCenter);
			unread_label->setFixedSize(22, 22);
			unread_label->setStyleSheet("QLabel { background: #22c55e; color: #ffffff; border-radius: 11px; font-weight: 700; }");
			row_layout->addWidget(unread_label);
		}

		QLabel* time_label = new QLabel(dialog.last_time.isValid() ? dialog.last_time.toString("HH:mm") : QString(), row);
		time_label->setObjectName("chatMutedText");
		time_label->setStyleSheet("QLabel { font-size: 11px; }");
		row_layout->addWidget(time_label);

		chat_users_list_layout->addWidget(row);
	}

	chat_users_list_layout->addStretch(1);
}


void MainWindow::rebuildChatUserRows()
{
	if(!chat_users_list_layout)
		return;

	clearLayoutAndDeleteWidgets(chat_users_list_layout);

	const bool private_list_mode = chat_tabs_bar && chat_tabs_bar->currentIndex() == 2 && !chat_private_conversation_open;
	const QString filter = chat_player_search_edit ? chat_player_search_edit->text().trimmed().toLower() : QString();

	if(private_list_mode)
	{
		rebuildPrivateDialogRows(filter);
		return;
	}

	const std::vector<AvatarNameInfo> names = collectChatAvatarNameInfos(gui_client, /*include_self=*/true);
	if(chat_online_count_label)
		chat_online_count_label->setText(tr("Онлайн (%1)").arg((int)names.size()));

	bool added_any = false;
	if(names.empty())
	{
		QLabel* empty_label = new QLabel(tr("Игроков онлайн пока нет."), ui->chatWidget);
		empty_label->setObjectName("chatMutedText");
		empty_label->setAlignment(Qt::AlignCenter);
		empty_label->setWordWrap(true);
		empty_label->setMinimumHeight(80);
		chat_users_list_layout->addWidget(empty_label);
		chat_users_list_layout->addStretch(1);
		return;
	}

	for(size_t i = 0; i < names.size(); ++i)
	{
		const AvatarNameInfo info = names[i];
		if(!filter.isEmpty() && !QtUtils::toQString(info.name).toLower().contains(filter) && !QtUtils::toQString(info.route_name).toLower().contains(filter))
			continue;

		QFrame* row = new QFrame(ui->chatWidget);
		row->setObjectName("chatUserRow");
		row->setContextMenuPolicy(Qt::CustomContextMenu);
		row->setCursor(private_list_mode && !info.is_self ? Qt::PointingHandCursor : Qt::ArrowCursor);
		row->setProperty("chatPrivateListRow", private_list_mode && !info.is_self);
		row->setProperty("chatOpenPrivateUid", QVariant((qulonglong)info.avatar_uid.value()));
		row->installEventFilter(this);
		row->setMinimumHeight(50);
		row->setMinimumWidth(0);
		QHBoxLayout* row_layout = new QHBoxLayout(row);
		row_layout->setContentsMargins(5, 5, 5, 5);
		row_layout->setSpacing(5);

		QWidget* avatar_wrap = new QWidget(row);
		avatar_wrap->setFixedSize(34, 34);
		QLabel* avatar = new QLabel(chatInitialsForName(info.name), avatar_wrap);
		avatar->setAlignment(Qt::AlignCenter);
		avatar->setGeometry(0, 0, 32, 32);
		avatar->setStyleSheet(QString("QLabel { background: %1; color: #ffffff; border-radius: 16px; font-weight: 700; }")
			.arg(chatCssColour(chatAvatarBackgroundColour(info))));
		QLabel* status_dot = new QLabel(QString::fromUtf8("●"), avatar_wrap);
		status_dot->setGeometry(24, 22, 12, 12);
		status_dot->setAlignment(Qt::AlignCenter);
		status_dot->setStyleSheet("QLabel { color: #34c759; background: #ffffff; border-radius: 6px; font-size: 12px; }");
		row_layout->addWidget(avatar_wrap);

		QVBoxLayout* name_layout = new QVBoxLayout();
		name_layout->setContentsMargins(0, 0, 0, 0);
		name_layout->setSpacing(2);
		QLabel* name_label = new QLabel(QtUtils::toQString(info.name), row);
		name_label->setMinimumWidth(0);
		name_label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
		name_label->setStyleSheet(QString("QLabel { color: %1; font-weight: 700; }").arg(chatCssColour(info.colour)));
		name_label->setTextInteractionFlags(Qt::TextSelectableByMouse);
		QLabel* role_label = new QLabel(row);
		const QString role_badge = chatRoleBadgeForName(info.name);
		const QString self_suffix = info.is_self ? QString::fromUtf8(" · Вы") : QString();
		role_label->setText(role_badge.isEmpty() ? (chatRoleLabelForName(info.name) + self_suffix) : (role_badge + " " + chatRoleLabelForName(info.name) + self_suffix));
		role_label->setObjectName("chatMutedText");
		role_label->setMinimumWidth(0);
		role_label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
		role_label->setStyleSheet("QLabel { font-size: 11px; }");
		name_layout->addWidget(name_label);
		name_layout->addWidget(role_label);
		row_layout->addLayout(name_layout, 1);
		if(private_list_mode && !info.is_self)
		{
			const QVariant uid_variant((qulonglong)info.avatar_uid.value());
			QWidget* click_widgets[] = { avatar_wrap, avatar, status_dot, name_label, role_label };
			for(QWidget* click_widget : click_widgets)
			{
				click_widget->setCursor(Qt::PointingHandCursor);
				click_widget->setProperty("chatPrivateListRow", true);
				click_widget->setProperty("chatOpenPrivateUid", uid_variant);
				click_widget->installEventFilter(this);
			}
		}

		QToolButton* message_button = makeChatToolButton(row, QString::fromUtf8("✉"), tr("Личное сообщение"), "icon");
		message_button->setEnabled(!info.is_self);
		QToolButton* profile_button = makeChatToolButton(row, QString::fromUtf8("♙"), tr("Профиль игрока"), "icon");
		QToolButton* more_button = makeChatToolButton(row, QString::fromUtf8("..."), tr("Действия с игроком"), "icon");
		for(QToolButton* button : { message_button, profile_button, more_button })
			button->setFixedSize(24, 24);
		row_layout->addWidget(message_button);
		row_layout->addWidget(profile_button);
		row_layout->addWidget(more_button);

		const UID avatar_uid = info.avatar_uid;
		connect(message_button, &QToolButton::clicked, this, [this, avatar_uid]() { startPrivateChatWithUser(avatar_uid); });
		connect(profile_button, &QToolButton::clicked, this, [this, avatar_uid]() { showChatUserProfile(avatar_uid); });
		connect(more_button, &QToolButton::clicked, this, [this, avatar_uid, more_button]() {
			showChatUserContextMenu(avatar_uid, more_button->mapToGlobal(QPoint(0, more_button->height())));
		});
		connect(row, &QWidget::customContextMenuRequested, this, [this, avatar_uid, row](const QPoint& pos) {
			showChatUserContextMenu(avatar_uid, row->mapToGlobal(pos));
		});

		chat_users_list_layout->addWidget(row);
		added_any = true;
	}

	if(!added_any)
	{
		QLabel* empty_filter_label = new QLabel(tr("Игроки не найдены."), ui->chatWidget);
		empty_filter_label->setObjectName("chatMutedText");
		empty_filter_label->setAlignment(Qt::AlignCenter);
		empty_filter_label->setMinimumHeight(80);
		chat_users_list_layout->addWidget(empty_filter_label);
	}

	chat_users_list_layout->addStretch(1);
}


void MainWindow::rebuildChatGroupRows()
{
	if(!chat_groups_list_layout)
		return;

	clearLayoutAndDeleteWidgets(chat_groups_list_layout);

	if(chat_groups.isEmpty())
	{
		QLabel* empty_label = new QLabel(tr("Групп пока нет. Нажмите +, чтобы создать группу."), ui ? ui->chatWidget : this);
		empty_label->setObjectName("chatMutedText");
		empty_label->setAlignment(Qt::AlignCenter);
		empty_label->setWordWrap(true);
		empty_label->setMinimumHeight(120);
		chat_groups_list_layout->addWidget(empty_label);
		chat_groups_list_layout->addStretch(1);
		return;
	}

	for(auto it = chat_groups.constBegin(); it != chat_groups.constEnd(); ++it)
	{
		const ChatGroupState group = it.value();
		QFrame* row = new QFrame(ui ? ui->chatWidget : this);
		row->setObjectName("chatUserRow");
		row->setMinimumHeight(58);
		QHBoxLayout* row_layout = new QHBoxLayout(row);
		row_layout->setContentsMargins(6, 6, 6, 6);
		row_layout->setSpacing(8);

		QLabel* avatar = new QLabel(QString::fromUtf8("#"), row);
		avatar->setFixedSize(34, 34);
		avatar->setAlignment(Qt::AlignCenter);
		avatar->setStyleSheet("QLabel { background: #14b8a6; color: #ffffff; border-radius: 17px; font-weight: 700; }");
		row_layout->addWidget(avatar);

		QVBoxLayout* text_layout = new QVBoxLayout();
		text_layout->setContentsMargins(0, 0, 0, 0);
		text_layout->setSpacing(2);
		QLabel* name_label = new QLabel(group.name, row);
		name_label->setStyleSheet("QLabel { font-weight: 700; }");
		name_label->setMinimumWidth(0);
		name_label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
		QLabel* meta_label = new QLabel(tr("%1 участников · %2").arg(group.members.size()).arg(group.invite_only ? tr("закрытая") : tr("открытая")), row);
		meta_label->setObjectName("chatMutedText");
		meta_label->setMinimumWidth(0);
		meta_label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
		text_layout->addWidget(name_label);
		text_layout->addWidget(meta_label);
		row_layout->addLayout(text_layout, 1);

		QToolButton* settings_button = makeChatToolButton(row, QString::fromUtf8("⚙"), tr("Настройки группы"), "icon");
		settings_button->setFixedSize(26, 26);
		row_layout->addWidget(settings_button);

		const QString group_id = group.id;
		connect(settings_button, &QToolButton::clicked, this, [this, group_id]() { showChatGroupSettings(group_id); });
		chat_groups_list_layout->addWidget(row);
	}

	chat_groups_list_layout->addStretch(1);
}


void MainWindow::createChatGroup()
{
	bool ok = false;
	const QString name = QInputDialog::getText(this, tr("Создать группу"), tr("Название группы:"), QLineEdit::Normal, QString(), &ok).trimmed();
	if(!ok || name.isEmpty())
		return;

	ChatGroupState group;
	group.id = QStringLiteral("local-%1").arg(QDateTime::currentMSecsSinceEpoch());
	group.name = name;
	group.description = tr("Новая группа");
	if(!gui_client.logged_in_user_name.empty())
		group.members << QtUtils::toQString(gui_client.logged_in_user_name);
	chat_groups.insert(group.id, group);
	rebuildChatGroupRows();
	showInfoNotification("Группа создана: " + QtUtils::toStdString(name));
}


void MainWindow::showChatGroupSettings(const QString& group_id)
{
	if(!chat_groups.contains(group_id))
		return;

	ChatGroupState group = chat_groups.value(group_id);

	QDialog dialog(this);
	dialog.setWindowTitle(tr("Настройки группы"));
	QVBoxLayout* layout = new QVBoxLayout(&dialog);
	layout->setContentsMargins(12, 12, 12, 12);
	layout->setSpacing(8);

	QLineEdit* name_edit = new QLineEdit(group.name, &dialog);
	name_edit->setPlaceholderText(tr("Название группы"));
	QLineEdit* description_edit = new QLineEdit(group.description, &dialog);
	description_edit->setPlaceholderText(tr("Описание"));
	QCheckBox* notifications_cb = new QCheckBox(tr("Уведомления группы включены"), &dialog);
	notifications_cb->setChecked(group.notifications_enabled);
	QCheckBox* invite_only_cb = new QCheckBox(tr("Закрытая группа: вступление только по приглашению"), &dialog);
	invite_only_cb->setChecked(group.invite_only);
	QListWidget* members_list = new QListWidget(&dialog);
	members_list->addItems(group.members);
	members_list->setMinimumHeight(130);

	QHBoxLayout* member_buttons_layout = new QHBoxLayout();
	QPushButton* add_member_button = new QPushButton(tr("Добавить игрока"), &dialog);
	QPushButton* remove_member_button = new QPushButton(tr("Удалить"), &dialog);
	member_buttons_layout->addWidget(add_member_button);
	member_buttons_layout->addWidget(remove_member_button);

	QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
	layout->addWidget(new QLabel(tr("Название"), &dialog));
	layout->addWidget(name_edit);
	layout->addWidget(new QLabel(tr("Описание"), &dialog));
	layout->addWidget(description_edit);
	layout->addWidget(notifications_cb);
	layout->addWidget(invite_only_cb);
	layout->addWidget(new QLabel(tr("Участники"), &dialog));
	layout->addWidget(members_list);
	layout->addLayout(member_buttons_layout);
	layout->addWidget(buttons);

	connect(add_member_button, &QPushButton::clicked, this, [this, members_list, &dialog]() {
		QStringList choices;
		const std::vector<AvatarNameInfo> names = collectChatAvatarNameInfos(gui_client, /*include_self=*/false);
		for(const AvatarNameInfo& info : names)
			choices << QtUtils::toQString(info.route_name.empty() ? info.name : info.route_name);
		choices.removeDuplicates();
		if(choices.isEmpty())
		{
			QMessageBox::information(&dialog, tr("Добавить игрока"), tr("Сейчас нет доступных игроков онлайн."));
			return;
		}

		bool ok = false;
		const QString member = QInputDialog::getItem(&dialog, tr("Добавить игрока"), tr("Игрок:"), choices, 0, false, &ok);
		if(ok && !member.isEmpty() && members_list->findItems(member, Qt::MatchExactly).isEmpty())
			members_list->addItem(member);
	});
	connect(remove_member_button, &QPushButton::clicked, members_list, [members_list]() {
		qDeleteAll(members_list->selectedItems());
	});
	connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

	if(dialog.exec() != QDialog::Accepted)
		return;

	group.name = name_edit->text().trimmed().isEmpty() ? group.name : name_edit->text().trimmed();
	group.description = description_edit->text().trimmed();
	group.notifications_enabled = notifications_cb->isChecked();
	group.invite_only = invite_only_cb->isChecked();
	group.members.clear();
	for(int i = 0; i < members_list->count(); ++i)
		group.members << members_list->item(i)->text();

	chat_groups[group_id] = group;
	rebuildChatGroupRows();
	showInfoNotification("Настройки группы сохранены.");
}


void MainWindow::appendChatMessageWidget(const QString& html, bool private_message)
{
	if(!chat_messages_list_layout)
		return;

	const QString plain_text = plainTextFromChatHtml(html);
	const QList<ChatAttachmentRef> attachments = extractChatAttachmentRefs(plain_text, gui_client);
	if(plain_text.isEmpty() && attachments.isEmpty())
		return;

	QFrame* message_row = new QFrame(ui->chatWidget);
	message_row->setObjectName("chatMessageRow");
	message_row->setProperty("chatMessageRow", true);
	message_row->setProperty("chatMessageHtml", html);
	message_row->setProperty("chatMessagePlainText", plain_text);
	message_row->setProperty("chatMessageLocalId", chat_message_counter + 1);
	message_row->setProperty("chatPrivateMessage", private_message);
	message_row->setProperty("chatIncomingPrivateMessage", isIncomingPrivateChatMessageHtml(html));
	message_row->setProperty("chatPrivatePeer", private_message ? privateChatPeerFromPlainText(plain_text) : QString());
	message_row->setContextMenuPolicy(Qt::CustomContextMenu);
	const bool system_message =
		plain_text.contains(" is here.") || plain_text.contains(" joined.") || plain_text.contains(" left.");
	const bool attachment_message = !attachments.isEmpty();
	const bool reply_message = plain_text.contains(QString::fromUtf8("Ответ:"));
	message_row->setProperty("chatAttachmentMessage", attachment_message);
	message_row->setProperty("chatSystemMessage", system_message);
	message_row->setProperty("chatReplyMessage", reply_message);
	message_row->setStyleSheet(chatMessageRowStyle(private_message, attachment_message, system_message, reply_message));
	QHBoxLayout* row_layout = new QHBoxLayout(message_row);
	row_layout->setContentsMargins(10, chat_compact_message_view ? 6 : 9, 10, chat_compact_message_view ? 6 : 9);
	row_layout->setSpacing(8);

	QVBoxLayout* text_layout = new QVBoxLayout();
	text_layout->setContentsMargins(0, 0, 0, 0);
	text_layout->setSpacing(4);
	QString display_html = html;
	for(const ChatAttachmentRef& attachment : attachments)
	{
		display_html.replace(attachment.token, QString());
		display_html.replace(attachment.token.toHtmlEscaped(), QString());
	}
	if(plainTextFromChatHtml(display_html).isEmpty())
		display_html = private_message ? tr("Личное вложение") : tr("Вложение");

	if(reply_message)
	{
		QString reply_preview = plain_text.mid(plain_text.indexOf(QString::fromUtf8("Ответ:")) + QString::fromUtf8("Ответ:").size()).trimmed();
		if(reply_preview.size() > 120)
			reply_preview = reply_preview.left(120) + QString::fromUtf8("...");
		QLabel* reply_label = new QLabel(QString::fromUtf8("↩ Ответ: %1").arg(reply_preview), message_row);
		reply_label->setObjectName("chatReplyPreview");
		reply_label->setWordWrap(true);
		reply_label->setTextInteractionFlags(Qt::TextSelectableByMouse);
		text_layout->addWidget(reply_label);
	}

	QLabel* body_label = new QLabel(display_html, message_row);
	body_label->setObjectName("chatMessageBody");
	body_label->setTextFormat(Qt::RichText);
	body_label->setOpenExternalLinks(true);
	body_label->setWordWrap(true);
	body_label->setTextInteractionFlags(Qt::TextBrowserInteraction);
	text_layout->addWidget(body_label);

	if(!attachments.isEmpty())
	{
		QHBoxLayout* attachments_layout = new QHBoxLayout();
		attachments_layout->setContentsMargins(0, 3, 0, 0);
		attachments_layout->setSpacing(6);
		for(const ChatAttachmentRef& attachment : attachments)
		{
			const QString local_path = attachment.local_path;
			const QFileInfo file_info(local_path);
			if(isChatImageAttachmentPath(local_path))
			{
				QImageReader reader(local_path);
				reader.setAutoTransform(true);
				QImage image = reader.read();
				QPixmap pixmap = image.isNull() ? QPixmap(local_path) : QPixmap::fromImage(image);
				if(!pixmap.isNull())
				{
					QLabel* image_label = new QLabel(message_row);
					image_label->setPixmap(pixmap.scaled(260, 170, Qt::KeepAspectRatio, Qt::SmoothTransformation));
					image_label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
					image_label->setToolTip(local_path);
					const QPalette palette = QApplication::palette();
					image_label->setStyleSheet(QString("QLabel { background: %1; border: 1px solid %2; border-radius: 6px; padding: 3px; }")
						.arg(cssChatColour(palette.color(QPalette::Base)), cssChatColour(palette.color(QPalette::Mid))));
					attachments_layout->addWidget(image_label);
					continue;
				}
			}

			if(local_path.isEmpty() && !attachment.resource_url.isEmpty() && isChatImageAttachmentPath(attachment.resource_url))
			{
				QLabel* image_label = new QLabel(tr("Загрузка..."), message_row);
				image_label->setMinimumSize(120, 70);
				image_label->setAlignment(Qt::AlignCenter);
				image_label->setToolTip(attachment.resource_url);
				const QPalette palette = QApplication::palette();
				image_label->setStyleSheet(QString("QLabel { background: %1; border: 1px solid %2; border-radius: 6px; padding: 3px; color: %3; }")
					.arg(cssChatColour(palette.color(QPalette::Base)), cssChatColour(palette.color(QPalette::Mid)), cssChatColour(palette.color(QPalette::Text))));
				attachments_layout->addWidget(image_label);

				QPointer<QLabel> image_label_ptr(image_label);
				QTimer* preview_timer = new QTimer(image_label);
				const QString resource_url = attachment.resource_url;
				connect(preview_timer, &QTimer::timeout, this, [this, image_label_ptr, preview_timer, resource_url, remaining_checks = 40]() mutable {
					if(image_label_ptr.isNull())
					{
						preview_timer->stop();
						return;
					}

					try
					{
						const URLString url(QtUtils::toStdString(resource_url));
						if(gui_client.resource_manager.nonNull() && ResourceManager::isValidURL(url) && gui_client.resource_manager->isFileForURLPresent(url))
						{
							const QString downloaded_path = QDir::toNativeSeparators(QtUtils::toQString(gui_client.resource_manager->pathForURL(url)));
							QImageReader reader(downloaded_path);
							reader.setAutoTransform(true);
							QImage image = reader.read();
							QPixmap pixmap = image.isNull() ? QPixmap(downloaded_path) : QPixmap::fromImage(image);
							if(!pixmap.isNull())
							{
								image_label_ptr->setPixmap(pixmap.scaled(260, 170, Qt::KeepAspectRatio, Qt::SmoothTransformation));
								image_label_ptr->setMinimumSize(QSize());
								image_label_ptr->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
								image_label_ptr->setToolTip(downloaded_path);
								preview_timer->stop();
								return;
							}
						}
					}
					catch(glare::Exception&)
					{}

					remaining_checks--;
					if(remaining_checks <= 0)
					{
						image_label_ptr->setText(tr("Файл"));
						preview_timer->stop();
					}
				});
				preview_timer->start(500);
				continue;
			}

			QToolButton* file_button = new QToolButton(message_row);
			const QString display_name = attachment.display_name.isEmpty() ? (file_info.fileName().isEmpty() ? attachment.resource_url : file_info.fileName()) : attachment.display_name;
			file_button->setText(QString::fromUtf8("📎 %1").arg(display_name));
			file_button->setToolTip(local_path.isEmpty() ? attachment.resource_url : local_path);
			file_button->setToolButtonStyle(Qt::ToolButtonTextOnly);
			file_button->setCursor(Qt::PointingHandCursor);
			const QPalette palette = QApplication::palette();
			file_button->setStyleSheet(QString("QToolButton { background: %1; border: 1px solid %2; border-radius: 6px; padding: 5px 8px; color: %3; } QToolButton:hover { border-color: %4; }")
				.arg(cssChatColour(palette.color(QPalette::Base)), cssChatColour(palette.color(QPalette::Mid)), cssChatColour(palette.color(QPalette::Text)), cssChatColour(palette.color(QPalette::Highlight))));
			const QString resource_url = attachment.resource_url;
			const QString server_hostname = QtUtils::toQString(gui_client.server_hostname);
			connect(file_button, &QToolButton::clicked, this, [local_path, resource_url, server_hostname]() {
				if(!local_path.isEmpty())
					QDesktopServices::openUrl(QUrl::fromLocalFile(local_path));
				else if(!resource_url.isEmpty() && !server_hostname.isEmpty())
					QDesktopServices::openUrl(QUrl(QStringLiteral("http://") + server_hostname + QStringLiteral("/resource/") + QString::fromStdString(web::Escaping::URLEscape(QtUtils::toStdString(resource_url)))));
			});
			attachments_layout->addWidget(file_button);
		}
		attachments_layout->addStretch(1);
		text_layout->addLayout(attachments_layout);
	}

	QLabel* time_label = new QLabel(QDateTime::currentDateTime().toString("HH:mm:ss"), message_row);
	time_label->setObjectName("chatMessageTimeLabel");
	time_label->setProperty("chatTimestampLabel", true);
	time_label->setVisible(chat_show_timestamps);
	text_layout->addWidget(time_label);

	QLabel* reaction_label = new QLabel(message_row);
	reaction_label->setObjectName("chatReactionLabel");
	reaction_label->setVisible(false);
	reaction_label->setStyleSheet("QLabel { font-weight: 700; padding: 2px 6px; border-radius: 8px; }");
	text_layout->addWidget(reaction_label);
	row_layout->addLayout(text_layout, 1);

	QToolButton* reply_button = makeChatToolButton(message_row, QString::fromUtf8("↩"), tr("Ответить"), "icon");
	QToolButton* like_button = makeChatToolButton(message_row, QString::fromUtf8("♡"), tr("Реакция"), "icon");
	QToolButton* message_more_button = makeChatToolButton(message_row, QString::fromUtf8("⋮"), tr("Действия с сообщением"), "icon");
	row_layout->addWidget(reply_button);
	row_layout->addWidget(like_button);
	row_layout->addWidget(message_more_button);

	connect(reply_button, &QToolButton::clicked, this, [this, plain_text]() {
		if(!ui || !ui->chatMessageLineEdit)
			return;
		const QString reply_prefix = tr("Ответ: ") + plain_text.left(90) + " ";
		ui->chatMessageLineEdit->setText(reply_prefix);
			ui->chatMessageLineEdit->setFocus();
	});
	connect(like_button, &QToolButton::clicked, this, [this, message_row, like_button]() {
		addReactionToChatMessage(message_row, QString::fromUtf8("♥"));
		like_button->setText(QString::fromUtf8("♥ 1"));
	});
	connect(message_more_button, &QToolButton::clicked, this, [this, message_row, plain_text, message_more_button]() {
		showChatMessageContextMenu(message_row, plain_text, message_more_button);
	});
	connect(message_row, &QWidget::customContextMenuRequested, this, [this, message_row, plain_text](const QPoint&) {
		showChatMessageContextMenu(message_row, plain_text, NULL);
	});

	const int insert_index = myMax(0, chat_messages_list_layout->count() - 1);
	chat_messages_list_layout->insertWidget(insert_index, message_row);
	chat_message_counter++;
	applyChatMessageDisplaySettings();
	updateChatMessageVisibility();

	if(chat_messages_scroll_area)
		QTimer::singleShot(0, this, [this]() {
			if(chat_messages_scroll_area && chat_messages_scroll_area->verticalScrollBar())
				chat_messages_scroll_area->verticalScrollBar()->setValue(chat_messages_scroll_area->verticalScrollBar()->maximum());
	});
}


void MainWindow::addReactionToChatMessage(QFrame* message_row, const QString& reaction)
{
	if(!message_row || reaction.isEmpty())
		return;

	QLabel* reaction_label = message_row->findChild<QLabel*>("chatReactionLabel");
	if(!reaction_label)
		return;

	message_row->setProperty("chatReaction", reaction);
	reaction_label->setText(QString::fromUtf8("%1 1").arg(reaction));
	reaction_label->setVisible(true);
	showInfoNotification(QtUtils::toStdString(tr("Реакция добавлена: %1").arg(reaction)));
}


void MainWindow::showChatMessageContextMenu(QFrame* message_row, const QString& plain_text, QToolButton* anchor_button)
{
	if(!message_row)
		return;

	QMenu menu(this);
	QAction* reply_action = menu.addAction(tr("Ответить"));
	QMenu* reactions_menu = menu.addMenu(tr("Реакция"));
	QAction* thumbs_up_action = reactions_menu->addAction(QString::fromUtf8("👍"));
	QAction* heart_action = reactions_menu->addAction(QString::fromUtf8("❤️"));
	QAction* laugh_action = reactions_menu->addAction(QString::fromUtf8("😂"));
	QAction* wow_action = reactions_menu->addAction(QString::fromUtf8("😮"));
	QAction* thumbs_down_action = reactions_menu->addAction(QString::fromUtf8("👎"));
	QAction* copy_action = menu.addAction(tr("Копировать текст"));
	QAction* report_action = menu.addAction(tr("Пожаловаться"));
	menu.addSeparator();
	QAction* delete_action = menu.addAction(tr("Удалить"));

	const QPoint menu_pos = anchor_button ?
		anchor_button->mapToGlobal(QPoint(0, anchor_button->height())) :
		QCursor::pos();
	QAction* selected = menu.exec(menu_pos);
	if(!selected)
		return;

	if(selected == reply_action && ui && ui->chatMessageLineEdit)
	{
		ui->chatMessageLineEdit->setText(tr("Ответ: ") + plain_text.left(90) + " ");
		ui->chatMessageLineEdit->setFocus();
	}
	else if(selected == thumbs_up_action)
		addReactionToChatMessage(message_row, QString::fromUtf8("👍"));
	else if(selected == heart_action)
		addReactionToChatMessage(message_row, QString::fromUtf8("❤️"));
	else if(selected == laugh_action)
		addReactionToChatMessage(message_row, QString::fromUtf8("😂"));
	else if(selected == wow_action)
		addReactionToChatMessage(message_row, QString::fromUtf8("😮"));
	else if(selected == thumbs_down_action)
		addReactionToChatMessage(message_row, QString::fromUtf8("👎"));
	else if(selected == copy_action && QGuiApplication::clipboard())
	{
		QGuiApplication::clipboard()->setText(plain_text);
		showInfoNotification("Текст сообщения скопирован.");
	}
	else if(selected == report_action)
	{
		showInfoNotification("Жалоба на сообщение отправлена в локальную очередь модерации.");
	}
	else if(selected == delete_action)
	{
		QMessageBox msg_box(this);
		msg_box.setWindowTitle(tr("Удалить сообщение"));
		msg_box.setText(tr("Удалить сообщение?"));
		msg_box.setInformativeText(tr("Можно удалить сообщение только у себя или у всех участников чата."));
		QAbstractButton* delete_for_me_button = msg_box.addButton(tr("У себя"), QMessageBox::AcceptRole);
		QAbstractButton* delete_for_everyone_button = msg_box.addButton(tr("У всех"), QMessageBox::DestructiveRole);
		msg_box.addButton(QMessageBox::Cancel);
		msg_box.exec();

		if(msg_box.clickedButton() == delete_for_me_button)
			deleteChatMessageRow(message_row, /*delete_for_everyone=*/false);
		else if(msg_box.clickedButton() == delete_for_everyone_button)
			deleteChatMessageRow(message_row, /*delete_for_everyone=*/true);
	}
}


void MainWindow::deleteChatMessageRow(QFrame* message_row, bool delete_for_everyone)
{
	if(!message_row)
		return;

	const QString html = message_row->property("chatMessageHtml").toString();
	const bool private_message = message_row->property("chatPrivateMessage").toBool();
	if(!html.isEmpty())
	{
		for(int i = 0; i < chat_history_entries.size(); ++i)
		{
			const QJsonObject entry = chat_history_entries.at(i).toObject();
			if(entry.value(QStringLiteral("html")).toString() == html &&
				entry.value(QStringLiteral("private")).toBool(false) == private_message)
			{
				chat_history_entries.removeAt(i);
				saveChatHistoryToDisk();
				break;
			}
		}
	}

	message_row->hide();
	message_row->deleteLater();
	if(delete_for_everyone)
		showInfoNotification("Сообщение скрыто локально. Сетевое удаление у всех будет включено после добавления серверного ID сообщений.");
	else
		showInfoNotification("Сообщение удалено у вас.");
}


void MainWindow::applyChatMessageDisplaySettings()
{
	if(!chat_messages_scroll_area)
		return;

	const QList<QLabel*> time_labels = chat_messages_scroll_area->findChildren<QLabel*>("chatMessageTimeLabel");
	for(QLabel* label : time_labels)
		label->setVisible(chat_show_timestamps);

	const QList<QFrame*> rows = chat_messages_scroll_area->findChildren<QFrame*>("chatMessageRow");
	for(QFrame* row : rows)
	{
		row->setStyleSheet(chatMessageRowStyle(
			row->property("chatPrivateMessage").toBool(),
			row->property("chatAttachmentMessage").toBool(),
			row->property("chatSystemMessage").toBool(),
			row->property("chatReplyMessage").toBool()
		));
		if(QLayout* layout = row->layout())
			layout->setContentsMargins(10, chat_compact_message_view ? 5 : 9, 10, chat_compact_message_view ? 5 : 9);
	}
}


void MainWindow::updateChatMessageVisibility()
{
	if(!chat_messages_scroll_area)
		return;

	const QList<QFrame*> rows = chat_messages_scroll_area->findChildren<QFrame*>("chatMessageRow");
	for(QFrame* row : rows)
	{
		const bool private_row = row->property("chatPrivateMessage").toBool();
		if(chat_showing_private_messages)
		{
			const QString row_peer = row->property("chatPrivatePeer").toString();
			const QString selected_peer = QtUtils::toQString(selectedChatRecipientName());
			row->setVisible(private_row && chat_private_conversation_open && chatPeerMatches(row_peer, selected_peer));
		}
		else
			row->setVisible(!private_row);
	}
}


void MainWindow::updateChatTabText()
{
	if(!chat_tabs_bar || chat_tabs_bar->count() <= 1)
		return;

	const QString base_text = tr("Чат");
	chat_tabs_bar->setTabText(1, chat_unread_count > 0 ? tr("%1 (%2)").arg(base_text).arg(chat_unread_count) : base_text);
}


void MainWindow::updatePrivateChatTabText()
{
	if(!chat_tabs_bar || chat_tabs_bar->count() <= 2)
		return;

	const QString base_text = tr("Личные");
	chat_tabs_bar->setTabText(2, chat_private_unread_count > 0 ? tr("%1 (%2)").arg(base_text).arg(chat_private_unread_count) : base_text);
}


void MainWindow::setChatSettingsVisible(bool visible)
{
	const bool groups_visible = chat_tabs_bar && chat_tabs_bar->currentIndex() == 3;
	if(chat_settings_page)
		chat_settings_page->setVisible(visible);
	if(chat_groups_page)
		chat_groups_page->setVisible(groups_visible);
	if(chat_messages_page)
		chat_messages_page->setVisible(!visible && !groups_visible);
	if(!visible)
		updateChatMessageVisibility();
}


void MainWindow::updateChatLayoutForCurrentTab()
{
	const int index = chat_tabs_bar ? chat_tabs_bar->currentIndex() : 1;
	const bool settings_tab = index == 5;
	const bool players_tab = index == 0;
	const bool private_tab = index == 2;
	const bool private_conversation = private_tab && chat_private_conversation_open && !selectedChatRecipientName().empty();
	const bool private_list = private_tab && !private_conversation;
	const bool main_chat_tab = index == 1;
	const bool group_tab = index == 3;
	const bool placeholder_tab = group_tab || index == 4;

	chat_showing_private_messages = private_conversation;

	setChatSettingsVisible(settings_tab);

	if(chat_users_panel)
		chat_users_panel->setVisible(players_tab || private_list);
	if(chat_main_panel)
		chat_main_panel->setVisible(main_chat_tab || private_conversation || placeholder_tab || settings_tab);
	if(chat_input_panel)
		chat_input_panel->setVisible(main_chat_tab || private_conversation);
	if(chat_body_splitter)
	{
		if((players_tab || private_list) && chat_users_panel && chat_users_panel->isVisible())
			chat_body_splitter->setSizes(QList<int>() << 140 << 0);
		else if((main_chat_tab || private_conversation || placeholder_tab || settings_tab) && chat_main_panel && chat_main_panel->isVisible())
			chat_body_splitter->setSizes(QList<int>() << 0 << myMax(220, chat_body_splitter->width()));
	}

	if(main_chat_tab)
	{
		chat_unread_count = 0;
		updateChatTabText();
	}
	if(private_tab)
	{
		refreshPrivateChatUnreadCount();
	}
	if(group_tab)
		rebuildChatGroupRows();

	if(ui && ui->chatMessageLineEdit)
	{
		if(private_conversation)
		{
			const QString recipient = QtUtils::toQString(chat_private_recipient_name);
			ui->chatMessageLineEdit->setPlaceholderText(recipient.isEmpty() ? tr("Личное сообщение...") : tr("Личное сообщение: %1").arg(recipient));
		}
		else
			ui->chatMessageLineEdit->setPlaceholderText(tr("Введите сообщение..."));
	}

	if(chat_online_count_label)
		chat_online_count_label->setText(private_list ? tr("Личные") : chat_online_count_label->text());
	if((players_tab || private_list) && chat_player_search_edit)
		chat_player_search_edit->setFocus();

	updateChatMessageVisibility();
	if(players_tab || private_list)
		rebuildChatUserRows();
}


std::string MainWindow::selectedChatRecipientName() const
{
	if(!chat_private_recipient_name.empty())
		return chat_private_recipient_name;

	if(!chat_user_combo || chat_user_combo->currentIndex() < 0)
		return std::string();

	const QString route_name = chat_user_combo->currentData(Qt::UserRole + 1).toString();
	if(!route_name.isEmpty())
		return QtUtils::toStdString(route_name);

	return QtUtils::toStdString(chat_user_combo->currentText());
}


UID MainWindow::selectedChatRecipientUID() const
{
	if(chat_private_recipient_uid.valid())
		return chat_private_recipient_uid;

	if(!chat_user_combo || chat_user_combo->currentIndex() < 0)
		return UID::invalidUID();

	bool uid_ok = false;
	const qulonglong uid = chat_user_combo->currentData(Qt::UserRole).toULongLong(&uid_ok);
	return uid_ok ? UID((uint64)uid) : UID::invalidUID();
}


void MainWindow::startPrivateChatWithUser(UID avatar_uid)
{
	AvatarNameInfo info;
	if(!getChatAvatarNameInfo(gui_client, avatar_uid, info))
	{
		showErrorNotification("Игрок уже не найден в этом мире.");
		refreshChatPlayerControls();
		return;
	}

	if(info.is_self)
	{
		showErrorNotification("Нельзя открыть личный чат с самим собой.");
		return;
	}

	const QString peer_name = QtUtils::toQString(info.route_name.empty() ? info.name : info.route_name);
	openPrivateChatDialog(peer_name, info.avatar_uid);

	if(chat_user_combo)
	{
		const int item_index = chat_user_combo->findData(QVariant((qulonglong)avatar_uid.value()), Qt::UserRole);
		if(item_index >= 0)
			chat_user_combo->setCurrentIndex(item_index);
	}

	if(ui && ui->chatMessageLineEdit)
	{
		ui->chatMessageLineEdit->setPlaceholderText(tr("Личное сообщение: %1").arg(QtUtils::toQString(info.name)));
		ui->chatMessageLineEdit->setFocus();
	}

	showInfoNotification("Личный чат: " + info.name);
}


void MainWindow::showChatUserProfile(UID avatar_uid)
{
	AvatarNameInfo info;
	if(!getChatAvatarNameInfo(gui_client, avatar_uid, info))
	{
		showErrorNotification("Игрок уже не найден в этом мире.");
		refreshChatPlayerControls();
		return;
	}

	Vec3d avatar_pos(0.0);
	bool have_pos = false;
	if(gui_client.world_state.nonNull())
	{
		Lock lock(gui_client.world_state->mutex);
		auto res = gui_client.world_state->avatars.find(avatar_uid);
		if(res != gui_client.world_state->avatars.end())
		{
			avatar_pos = res->second->pos;
			have_pos = true;
		}
	}

	QDialog dialog(this);
	dialog.setWindowTitle(tr("Профиль игрока"));
	dialog.setObjectName("chatProfileDialog");
	dialog.setMinimumWidth(420);
	dialog.setStyleSheet(
		"QDialog#chatProfileDialog { background: #ffffff; color: #111827; }"
		"QLabel#chatProfileName { font-size: 18px; font-weight: 700; }"
		"QLabel#chatProfileStatus { background: #dcfce7; color: #15803d; border: 1px solid #bbf7d0; border-radius: 5px; padding: 4px 8px; }"
		"QLabel#chatProfileRole { background: #f7f9fc; color: #374151; border: 1px solid #dbe3ee; border-radius: 5px; padding: 4px 8px; }"
		"QLabel#chatProfileKey { color: #697386; }"
		"QPushButton { background: #ffffff; border: 1px solid #dbe3ee; border-radius: 5px; padding: 7px 10px; }"
		"QPushButton:hover { background: #eef5ff; border-color: #a9c7ff; }"
	);

	QVBoxLayout* main_layout = new QVBoxLayout(&dialog);
	main_layout->setContentsMargins(18, 18, 18, 18);
	main_layout->setSpacing(14);

	QHBoxLayout* top_layout = new QHBoxLayout();
	top_layout->setSpacing(14);
	QLabel* avatar_label = new QLabel(chatInitialsForName(info.name), &dialog);
	avatar_label->setAlignment(Qt::AlignCenter);
	avatar_label->setFixedSize(74, 74);
	avatar_label->setStyleSheet(QString("QLabel { background: %1; color: #ffffff; border-radius: 37px; font-size: 24px; font-weight: 700; }")
		.arg(chatCssColour(chatAvatarBackgroundColour(info))));
	top_layout->addWidget(avatar_label);

	QVBoxLayout* title_layout = new QVBoxLayout();
	QLabel* name_label = new QLabel(QtUtils::toQString(info.name), &dialog);
	name_label->setObjectName("chatProfileName");
	name_label->setStyleSheet(QString("QLabel#chatProfileName { color: %1; font-size: 18px; font-weight: 700; }").arg(chatCssColour(info.colour)));
	QLabel* status_label = new QLabel(tr("Онлайн"), &dialog);
	status_label->setObjectName("chatProfileStatus");
	QLabel* role_label = new QLabel(chatRoleLabelForName(info.name), &dialog);
	role_label->setObjectName("chatProfileRole");
	title_layout->addWidget(name_label);
	QHBoxLayout* badges_layout = new QHBoxLayout();
	badges_layout->setContentsMargins(0, 0, 0, 0);
	badges_layout->setSpacing(6);
	badges_layout->addWidget(status_label);
	badges_layout->addWidget(role_label);
	badges_layout->addStretch(1);
	title_layout->addLayout(badges_layout);
	top_layout->addLayout(title_layout, 1);
	main_layout->addLayout(top_layout);

	QFrame* line = new QFrame(&dialog);
	line->setFrameShape(QFrame::HLine);
	line->setStyleSheet("QFrame { color: #e5eaf2; }");
	main_layout->addWidget(line);

	QGridLayout* details_layout = new QGridLayout();
	details_layout->setHorizontalSpacing(18);
	details_layout->setVerticalSpacing(8);
	const auto add_field = [&](int row, const QString& key, const QString& value) {
		QLabel* key_label = new QLabel(key, &dialog);
		key_label->setObjectName("chatProfileKey");
		QLabel* value_label = new QLabel(value, &dialog);
		value_label->setTextInteractionFlags(Qt::TextSelectableByMouse);
		details_layout->addWidget(key_label, row, 0);
		details_layout->addWidget(value_label, row, 1);
	};
	add_field(0, tr("UID:"), QtUtils::toQString(info.avatar_uid.toString()));
	add_field(1, tr("Ник:"), QtUtils::toQString(info.route_name));
	add_field(2, tr("Статус:"), tr("Онлайн"));
	add_field(3, tr("Ранг:"), chatRoleLabelForName(info.name));
	add_field(4, tr("Уровень:"), tr("нет данных"));
	add_field(5, tr("Опыт:"), tr("нет данных"));
	add_field(6, tr("Регистрация:"), tr("нет данных"));
	add_field(7, tr("Язык:"), tr("Русский"));
	add_field(8, tr("Группа:"), tr("нет данных"));
	if(have_pos)
		add_field(9, tr("Позиция:"), QtUtils::toQString(
			doubleToStringNDecimalPlaces(avatar_pos.x, 2) + ", " +
			doubleToStringNDecimalPlaces(avatar_pos.y, 2) + ", " +
			doubleToStringNDecimalPlaces(avatar_pos.z, 2)));
	main_layout->addLayout(details_layout);

	QHBoxLayout* button_layout = new QHBoxLayout();
	button_layout->addStretch(1);
	QPushButton* private_button = new QPushButton(tr("Личное сообщение"), &dialog);
	private_button->setEnabled(!info.is_self);
	QPushButton* close_button = new QPushButton(tr("Закрыть"), &dialog);
	button_layout->addWidget(private_button);
	button_layout->addWidget(close_button);
	main_layout->addLayout(button_layout);

	connect(private_button, &QPushButton::clicked, &dialog, [&dialog, this, avatar_uid]() {
		dialog.accept();
		startPrivateChatWithUser(avatar_uid);
	});
	connect(close_button, &QPushButton::clicked, &dialog, &QDialog::accept);

	dialog.exec();
}


void MainWindow::showChatUserContextMenu(UID avatar_uid, const QPoint& global_pos)
{
	AvatarNameInfo info;
	if(!getChatAvatarNameInfo(gui_client, avatar_uid, info))
	{
		showErrorNotification("Игрок уже не найден в этом мире.");
		refreshChatPlayerControls();
		return;
	}

	QMenu menu(this);
	QAction* private_action = menu.addAction(tr("Личное сообщение"));
	private_action->setEnabled(!info.is_self);
	QAction* profile_action = menu.addAction(tr("Показать профиль"));
	menu.addSeparator();
	QAction* add_friend_action = menu.addAction(tr("Добавить в друзья"));
	QAction* invite_group_action = menu.addAction(tr("Пригласить в группу"));
	QAction* voice_action = menu.addAction(tr("Пригласить в голосовой чат"));
	QAction* inventory_action = menu.addAction(tr("Посмотреть инвентарь"));
	QAction* trade_action = menu.addAction(tr("Передать предмет"));
	QAction* teleport_action = menu.addAction(tr("Телепортироваться"));
	teleport_action->setEnabled(!info.is_self);
	menu.addSeparator();
	QAction* report_action = menu.addAction(tr("Пожаловаться"));
	QAction* block_action = menu.addAction(tr("Заблокировать"));
	QAction* mute_action = menu.addAction(tr("Заглушить сообщения"));
	menu.addSeparator();
	QAction* copy_name_action = menu.addAction(tr("Копировать ник"));
	QAction* copy_uid_action = menu.addAction(tr("Копировать UID"));

	QAction* selected = menu.exec(global_pos);
	if(!selected)
		return;

	if(selected == private_action)
		startPrivateChatWithUser(avatar_uid);
	else if(selected == profile_action)
		showChatUserProfile(avatar_uid);
	else if(selected == teleport_action)
	{
		chat_private_recipient_uid = info.avatar_uid;
		chat_private_recipient_name = info.route_name.empty() ? info.name : info.route_name;
		if(chat_user_combo)
		{
			const int item_index = chat_user_combo->findData(QVariant((qulonglong)avatar_uid.value()), Qt::UserRole);
			if(item_index >= 0)
				chat_user_combo->setCurrentIndex(item_index);
		}
		teleportNearSelectedChatUser();
	}
	else if(selected == copy_name_action)
	{
		if(QGuiApplication::clipboard())
			QGuiApplication::clipboard()->setText(QtUtils::toQString(info.route_name));
		showInfoNotification("Ник скопирован: " + info.route_name);
	}
	else if(selected == copy_uid_action)
	{
		if(QGuiApplication::clipboard())
			QGuiApplication::clipboard()->setText(QtUtils::toQString(info.avatar_uid.toString()));
		showInfoNotification("UID скопирован: " + info.avatar_uid.toString());
	}
	else if(selected == add_friend_action || selected == invite_group_action || selected == voice_action ||
		selected == inventory_action || selected == trade_action || selected == report_action ||
		selected == block_action || selected == mute_action)
	{
		showInfoNotification(QtUtils::toStdString(selected->text() + tr(" будет подключено позже.")));
	}
}


void MainWindow::showChatAttachmentMenu()
{
	const QStringList selected_filenames = QFileDialog::getOpenFileNames(this, tr("Прикрепить файлы"), QString(), tr("Все файлы (*.*)"));
	if(selected_filenames.isEmpty())
		return;

	QStringList uploaded_filenames;
	QStringList attachment_markers;
	for(const QString& filename : selected_filenames)
	{
		try
		{
			const std::string resource_url = gui_client.prepareAndUploadChatAttachment(QtUtils::toStdString(filename));
			if(resource_url.empty())
				continue;

			uploaded_filenames << filename;
			const QString display_name = QFileInfo(filename).fileName();
			attachment_markers << makeMetasiberiaChatAttachmentMarker(QtUtils::toQString(resource_url), display_name.isEmpty() ? filename : display_name);
		}
		catch(glare::Exception& e)
		{
			showErrorNotification("Could not attach file: " + e.what());
		}
	}

	if(attachment_markers.isEmpty())
		return;

	if(ui && ui->chatMessageLineEdit)
	{
		const bool private_send = chat_showing_private_messages;
		const std::string recipient_name = selectedChatRecipientName();
		const UID recipient_avatar_uid = selectedChatRecipientUID();
		if(private_send && recipient_name.empty())
		{
			showErrorNotification("Выберите личный диалог для отправки вложения.");
			return;
		}
		if(private_send && !chat_network_private_messages_enabled)
		{
			showErrorNotification("Сетевые личные сообщения выключены, вложение не отправлено.");
			return;
		}

		appendLocalChatAttachmentMessage(uploaded_filenames);

		const QString typed_text = ui->chatMessageLineEdit->text().trimmed();
		QString safe_text = makeNetworkSafeChatMessage(typed_text);
		if(safe_text.isEmpty())
			safe_text = tr("Вложение");
		safe_text += " " + attachment_markers.join(" ");
		ui->chatMessageLineEdit->clear();

		const QString sender_label = private_send ?
			tr("Лично для %1").arg(QtUtils::toQString(recipient_name)) :
			QtUtils::toQString(gui_client.logged_in_user_name.empty() ? std::string("Вы") : gui_client.logged_in_user_name);
		const QString history_html =
			"<p><span style=\"font-weight:600;\">" + sender_label.toHtmlEscaped() + "</span>: " +
			safe_text.toHtmlEscaped() + "</p>";

		if(!safe_text.isEmpty() && !private_send)
		{
			chat_pending_attachment_echoes << safe_text;
			gui_client.sendChatMessage(QtUtils::toStdString(safe_text));
			rememberChatHistoryEntry(history_html, /*private_message=*/false);
		}
		else if(private_send)
		{
			chat_pending_attachment_echoes << safe_text;
			gui_client.sendPrivateChatMessage(recipient_name, recipient_avatar_uid, QtUtils::toStdString(safe_text));
			rememberChatHistoryEntry(history_html, /*private_message=*/true);
		}
	}
}


void MainWindow::showChatMoreSendMenu()
{
	QMenu menu(this);
	QAction* screenshot_action = menu.addAction(tr("Сделать снимок экрана"));
	QAction* voice_action = menu.addAction(tr("Записать голосовое сообщение"));
	QAction* contact_action = menu.addAction(tr("Отправить контакт"));
	QAction* coordinates_action = menu.addAction(tr("Отправить координаты"));
	QAction* poll_action = menu.addAction(tr("Создать опрос"));
	QAction* item_action = menu.addAction(tr("Поделиться предметом"));
	QAction* quest_action = menu.addAction(tr("Поделиться квестом"));
	QAction* link_action = menu.addAction(tr("Отправить ссылку"));
	QAction* export_action = menu.addAction(tr("Экспортировать историю переписки"));

	QWidget* source_widget = qobject_cast<QWidget*>(sender());
	const QPoint menu_pos = source_widget ? source_widget->mapToGlobal(QPoint(0, source_widget->height())) : QCursor::pos();
	QAction* selected = menu.exec(menu_pos);
	if(!selected)
		return;

	if(selected == link_action && ui && ui->chatMessageLineEdit)
	{
		ui->chatMessageLineEdit->setFocus();
		if(ui->chatMessageLineEdit->text().isEmpty())
			ui->chatMessageLineEdit->setText("https://");
		return;
	}

	if(selected == coordinates_action && ui && ui->chatMessageLineEdit)
	{
		const Vec3d pos = gui_client.cam_controller.getFirstPersonPosition();
		ui->chatMessageLineEdit->setFocus();
		ui->chatMessageLineEdit->setText(QtUtils::toQString(
			"Мои координаты: " +
			doubleToStringNDecimalPlaces(pos.x, 2) + ", " +
			doubleToStringNDecimalPlaces(pos.y, 2) + ", " +
			doubleToStringNDecimalPlaces(pos.z, 2)));
		return;
	}

	(void)screenshot_action;
	(void)voice_action;
	(void)contact_action;
	(void)poll_action;
	(void)item_action;
	(void)quest_action;
	(void)export_action;
	showInfoNotification(QtUtils::toStdString(selected->text() + tr(" будет подключено позже.")));
}


void MainWindow::sendPrivateChatMessageToSelectedUser()
{
	if(!ui)
		return;

	const std::string recipient_name = selectedChatRecipientName();
	if(recipient_name.empty())
	{
		showErrorNotification("Выберите игрока для личного сообщения.");
		return;
	}

	const std::string message = stripHeadAndTailWhitespace(QtUtils::toIndString(ui->chatMessageLineEdit->text()));
	if(message.empty())
	{
		showErrorNotification("Введите текст личного сообщения.");
		ui->chatMessageLineEdit->setFocus();
		return;
	}

	sendChatOrEmojiMessage(message);
	ui->chatMessageLineEdit->clear();
	ui->chatMessageLineEdit->setFocus();
}


void MainWindow::teleportNearSelectedChatUser()
{
	const UID target_uid = selectedChatRecipientUID();
	if(!target_uid.valid())
	{
		showErrorNotification("Выберите игрока, рядом с которым нужно оказаться.");
		return;
	}

	if(gui_client.world_state.isNull())
	{
		showErrorNotification("Нет подключенного мира.");
		return;
	}

	Vec3d target_pos(0.0);
	std::string target_name;
	bool found = false;
	{
		Lock lock(gui_client.world_state->mutex);
		auto res = gui_client.world_state->avatars.find(target_uid);
		if(res != gui_client.world_state->avatars.end())
		{
			target_pos = res->second->pos;
			target_name = res->second->getUseName();
			found = true;
		}
	}

	if(!found)
	{
		showErrorNotification("Выбранный игрок уже не найден в этом мире.");
		refreshChatPlayerControls();
		return;
	}

	Vec3d offset = gui_client.cam_controller.getFirstPersonPosition() - target_pos;
	offset.z = 0.0;
	double len = std::sqrt(offset.x * offset.x + offset.y * offset.y);
	if(len < 1.0e-3)
	{
		const Vec3d backwards = gui_client.cam_controller.getForwardsVec() * -1.0;
		offset = Vec3d(backwards.x, backwards.y, 0.0);
		len = std::sqrt(offset.x * offset.x + offset.y * offset.y);
		if(len < 1.0e-3)
		{
			offset = Vec3d(2.0, 0.0, 0.0);
			len = 2.0;
		}
	}

	const Vec3d new_pos = target_pos + offset * (2.0 / len);
	gui_client.cam_controller.setFirstAndThirdPersonPositions(new_pos);
	gui_client.player_physics.setEyePosition(new_pos);
	showInfoNotification("Переход к игроку: " + target_name);
}


void MainWindow::appendChatMessage(const std::string& msg)
{
	const QString html = QtUtils::toQString(msg);
	const QString plain_text = plainTextFromChatHtml(html);
	for(int i = 0; i < chat_pending_attachment_echoes.size(); ++i)
	{
		if(!chat_pending_attachment_echoes[i].isEmpty() && plain_text.contains(chat_pending_attachment_echoes[i]))
		{
			chat_pending_attachment_echoes.removeAt(i);
			return;
		}
	}

	const bool private_message = isPrivateChatMessageHtml(html);
	appendLocalChatMessage(html, private_message);
	if(private_message)
	{
		const bool incoming_private = isIncomingPrivateChatMessageHtml(html);
		const QString row_peer = privateChatPeerFromPlainText(plain_text);
		const bool active_peer_open = chat_showing_private_messages && chatPeerMatches(row_peer, QtUtils::toQString(selectedChatRecipientName()));
		notePrivateChatDialogFromPlainText(plain_text, incoming_private, incoming_private && !active_peer_open);
	}
	else if(!private_message && (!chat_tabs_bar || chat_tabs_bar->currentIndex() != 1))
	{
		chat_unread_count++;
		updateChatTabText();
	}
}


QString MainWindow::chatHistoryFilePath() const
{
	QDir dir(QtUtils::toQString(appdata_path));
	if(!dir.exists(QStringLiteral("chat")))
		dir.mkpath(QStringLiteral("chat"));
	return dir.filePath(QStringLiteral("chat/history_v1.json"));
}


void MainWindow::saveChatHistoryToDisk() const
{
	QFile file(chatHistoryFilePath());
	if(!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
		return;

	QJsonObject root;
	root[QStringLiteral("version")] = 1;
	root[QStringLiteral("messages")] = chat_history_entries;
	file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
}


void MainWindow::rememberChatHistoryEntry(const QString& html, bool private_message)
{
	if(chat_loading_history || html.trimmed().isEmpty())
		return;

	QJsonObject entry;
	entry[QStringLiteral("html")] = html;
	entry[QStringLiteral("private")] = private_message;
	entry[QStringLiteral("saved_at_utc")] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
	chat_history_entries.append(entry);

	const int max_history_messages = 500;
	while(chat_history_entries.size() > max_history_messages)
		chat_history_entries.removeAt(0);

	saveChatHistoryToDisk();
}


void MainWindow::clearChatMessageWidgets()
{
	if(!ui)
		return;

	ui->chatMessagesTextEdit->clear();
	if(chat_messages_list_layout)
		clearLayoutAndDeleteWidgets(chat_messages_list_layout);
	if(chat_messages_list_layout)
		chat_messages_list_layout->addStretch(1);
	chat_message_counter = 0;
	chat_unread_count = 0;
	chat_pending_attachment_echoes.clear();
	updateChatTabText();
	updateChatMessageVisibility();
}


void MainWindow::loadChatHistoryFromDisk()
{
	QFile file(chatHistoryFilePath());
	if(!file.exists() || !file.open(QIODevice::ReadOnly))
		return;

	QJsonParseError parse_error;
	const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parse_error);
	if(parse_error.error != QJsonParseError::NoError || !doc.isObject())
		return;

	const QJsonArray messages = doc.object().value(QStringLiteral("messages")).toArray();
	if(messages.isEmpty())
		return;

	chat_loading_history = true;
	chat_history_entries = messages;
	clearChatMessageWidgets();
	chat_private_dialogs.clear();

	for(const QJsonValue& value : messages)
	{
		const QJsonObject entry = value.toObject();
		const QString html = entry.value(QStringLiteral("html")).toString();
		const bool private_message = entry.value(QStringLiteral("private")).toBool(false);
		if(html.trimmed().isEmpty())
			continue;

		appendLocalChatMessage(html, private_message, /*remember_in_history=*/false);
		if(private_message)
			notePrivateChatDialogFromPlainText(plainTextFromChatHtml(html), isIncomingPrivateChatMessageHtml(html), /*count_unread=*/false);
	}

	chat_loading_history = false;
	refreshPrivateChatUnreadCount();
	updateChatMessageVisibility();
}


void MainWindow::clearPersistentChatHistory()
{
	chat_history_entries = QJsonArray();
	chat_private_dialogs.clear();
	clearChatMessageWidgets();
	saveChatHistoryToDisk();
	refreshPrivateChatUnreadCount();
	if(chat_tabs_bar && chat_tabs_bar->currentIndex() == 2)
		rebuildChatUserRows();
	showInfoNotification("История чата очищена.");
}


void MainWindow::appendLocalChatMessage(const QString& html, bool private_message, bool remember_in_history)
{
	if(ui && ui->chatMessagesTextEdit)
		ui->chatMessagesTextEdit->append(html);
	appendChatMessageWidget(html, private_message);
	if(remember_in_history)
		rememberChatHistoryEntry(html, private_message);
}


void MainWindow::appendLocalChatAttachmentMessage(const QStringList& selected_filenames)
{
	if(selected_filenames.isEmpty())
		return;

	QStringList file_urls;
	QStringList filenames;
	for(const QString& filename : selected_filenames)
	{
		file_urls << QUrl::fromLocalFile(filename).toString();
		const QString display_name = QFileInfo(filename).fileName();
		filenames << (display_name.isEmpty() ? filename : display_name);
	}

	QString sender_label;
	if(chat_showing_private_messages)
	{
		const QString recipient = QtUtils::toQString(chat_private_recipient_name);
		sender_label = recipient.isEmpty() ? tr("Личное вложение") : tr("Лично для %1").arg(recipient);
	}
	else
	{
		sender_label = QtUtils::toQString(gui_client.logged_in_user_name.empty() ? std::string("Вы") : gui_client.logged_in_user_name);
	}

	const QString typed_text = ui && ui->chatMessageLineEdit ? ui->chatMessageLineEdit->text().trimmed() : QString();
	const QString message_text = typed_text.isEmpty() ? tr("Вложение") : typed_text;
	const QString html =
		"<p><span style=\"font-weight:600;\">" + sender_label.toHtmlEscaped() + "</span>: " +
		message_text.toHtmlEscaped() + " " + file_urls.join(" ").toHtmlEscaped() + "</p>";
	appendLocalChatMessage(html, chat_showing_private_messages, /*remember_in_history=*/false);
	if(chat_showing_private_messages)
		notePrivateChatDialogFromPlainText(plainTextFromChatHtml(html), /*incoming_message=*/false, /*count_unread=*/false);
}


void MainWindow::clearChatMessages()
{
	clearChatMessageWidgets();
	loadChatHistoryFromDisk();
}


bool MainWindow::isShowParcelsEnabled() const
{
	return ui->actionShow_Parcels->isChecked();
}


void MainWindow::updateOnlineUsersList() // Works off world state avatars.
{
	if(!ui)
		return;

	if(gui_client.world_state.isNull())
	{
		ui->onlineUsersTextEdit->clear();
		refreshChatPlayerControls();
		return;
	}

	std::vector<AvatarNameInfo> names = collectChatAvatarNameInfos(gui_client, /*include_self=*/true);

	// Combine names into a single string, while escaping any HTML chars.
	QString s;
	for(size_t i=0; i<names.size(); ++i)
	{
		s += "<span style=\"color:" + chatCssColour(names[i].colour) + "\">";
		s += QtUtils::toQString(names[i].name).toHtmlEscaped() + "</span>" + ((i + 1 < names.size()) ? "<br/>" : "");
	}

	ui->onlineUsersTextEdit->setHtml(s);
	refreshChatPlayerControls();
}


void MainWindow::handleMapTilesResultReceivedMessage(const MapTilesResultReceivedMessage& msg)
{
	if(MetasiberiaMapDockWidget* map_widget = dynamic_cast<MetasiberiaMapDockWidget*>(map_dock_map_widget))
		map_widget->handleMapTilesResultReceivedMessage(msg);
}


void MainWindow::showHTMLMessageBox(const std::string& title, const std::string& msg)
{
	QMessageBox msgBox;
	msgBox.setTextFormat(Qt::RichText);
	msgBox.setWindowTitle(QtUtils::toQString(title));
	msgBox.setText(QtUtils::toQString(msg));
	msgBox.exec();
}


void MainWindow::showPlainTextMessageBox(const std::string& title, const std::string& msg)
{
	QMessageBox msgBox;
	msgBox.setWindowTitle(QtUtils::toQString(title));
	msgBox.setText(QtUtils::toQString(msg));
	msgBox.exec();
}


static Vec2f GLCoordsForGLWidgetPos(MainWindow* main_window, const Vec2f widget_pos)
{
	const int vp_width  = main_window->ui->glWidget->opengl_engine->getViewPortWidth();
	const int vp_height = main_window->ui->glWidget->opengl_engine->getViewPortHeight();

#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
	const double device_pixel_ratio = main_window->ui->glWidget->devicePixelRatio(); // For retina screens this is 2, meaning the gl viewport width is in physical pixels, of which have twice the density of qt pixel coordinates.
	const int use_vp_width  = (int)(vp_width  / device_pixel_ratio);
	const int use_vp_height = (int)(vp_height / device_pixel_ratio);
#else
	const int device_pixel_ratio = main_window->ui->glWidget->devicePixelRatio(); // For retina screens this is 2, meaning the gl viewport width is in physical pixels, of which have twice the density of qt pixel coordinates.
	const int use_vp_width  = vp_width  / device_pixel_ratio;
	const int use_vp_height = vp_height / device_pixel_ratio;
#endif

	return Vec2f(
		 (widget_pos.x - use_vp_width /2) / (use_vp_width /2),
		-(widget_pos.y - use_vp_height/2) / (use_vp_height/2)
	);
}


void MainWindow::timerEvent(QTimerEvent* event)
{
	PERFORMANCEAPI_INSTRUMENT("timerEvent");
	ZoneScoped; // Tracy profiler

	if(closing || in_CEF_message_loop)
		return;

	// We don't want to do the closeEvent stuff in the CEF message loop.  
	// If we got a close event in there, handle it now when we're in the main message loop, and not the CEF message loop.
	assert(!in_CEF_message_loop);
	if(should_close)
	{
		should_close = false;
		this->close();
		return;
	}

	Timer timerEvent_timer;

	in_CEF_message_loop = true;
	CEF::doMessageLoopWork();
	in_CEF_message_loop = false;


	// SDL_GameControllerUpdate(); // SDL gamepad support

	// Append any accumulated Qt debug messages to the log window.
	if(!qt_debug_msgs.empty())
	{
		for(size_t i=0; i<qt_debug_msgs.size(); ++i)
			logMessage(qt_debug_msgs[i]);
		qt_debug_msgs.clear();
	}

	ui->glWidget->makeCurrent(); // Need to make this gl widget context current, before we execute OpenGL calls in processLoading.


	const QPoint mouse_point = ui->glWidget->mapFromGlobal(QCursor::pos());

	MouseCursorState mouse_cursor_state;
	mouse_cursor_state.cursor_pos = Vec2i(mouse_point.x(), mouse_point.y()) * ui->glWidget->devicePixelRatio(); // Use devicePixelRatio to convert from logical to physical pixel coords.
	mouse_cursor_state.gl_coords =  GLCoordsForGLWidgetPos(this, Vec2f((float)mouse_point.x(), (float)mouse_point.y()));

	// NOTE: Stupid qt: QApplication::keyboardModifiers() doesn't update properly when just CTRL is pressed/released, without any other events.
	// So use GetAsyncKeyState on Windows, since it actually works.
#if defined(_WIN32)
	const bool ctrl_key_down = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
	const bool alt_key_down = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0; // alt = VK_MENU
#else
	const Qt::KeyboardModifiers modifiers = QApplication::keyboardModifiers();
	const bool ctrl_key_down = (modifiers & Qt::ControlModifier) != 0;
	const bool alt_key_down  = (modifiers & Qt::AltModifier)     != 0;
#endif
	mouse_cursor_state.alt_key_down = alt_key_down;
	mouse_cursor_state.ctrl_key_down = ctrl_key_down;
	gui_client.timerEvent(mouse_cursor_state);
	ui->objectEditor->setParticleDiagnostics(gui_client.getSelectedParticleEmitterParticleCount(), gui_client.getTotalParticleCount());

	// Update webcam dock (Qt).
	// Legacy path only (simple UI).  If WebcamWindow is active, it owns its own preview.
	if(!webcam_window && ui->webcamDockWidget->isVisible())
	{
		if(ui->webcamEnableCheckBox->isChecked())
		{
#if defined(_WIN32) && !defined(EMSCRIPTEN) && !defined(USE_SDL)
			QImage* qimg = static_cast<QImage*>(gui_client.getWebcamFrameAsQImage());
			if(qimg && !qimg->isNull())
				ui->webcamLabel->setPixmap(QPixmap::fromImage(*qimg));
			else
				ui->webcamLabel->setText("Waiting for webcam frame...");
#else
			ui->webcamLabel->setText("Webcam capture is not supported on this platform/build.");
			ui->webcamLabel->setPixmap(QPixmap());
#endif
		}
		else
		{
			ui->webcamLabel->setText("Webcam disabled");
			ui->webcamLabel->setPixmap(QPixmap());
		}
	}

#if INDIGO_SUPPORT
	if(this->ui->indigoView)
		this->ui->indigoView->timerThink();
#endif

	updateDiagnostics();
	
	updateStatusBar();
	updateMapDockState();

	runScreenshotCode();
	
	// Update URL Bar
	if(this->url_widget->shouldBeUpdated())
	{
		this->url_widget->setURL(gui_client.getCurrentURL());
	}

	const QPoint gl_pos = ui->glWidget->mapToGlobal(QPoint(200, 10));
	if(ui->infoDockWidget->geometry().topLeft() != gl_pos)
	{
		// conPrint("Positioning ui->infoDockWidget at " + toString(gl_pos.x()) + ", " + toString(gl_pos.y()));
		ui->infoDockWidget->setGeometry(gl_pos.x(), gl_pos.y(), 300, 1);
	}
	

	if(need_help_info_dock_widget_position)
	{
		// Position near bottom right corner of glWidget.
		ui->helpInfoDockWidget->setGeometry(QRect(ui->glWidget->mapToGlobal(ui->glWidget->geometry().bottomRight() + QPoint(-320, -120)), QSize(300, 100)));
		need_help_info_dock_widget_position = false;
	}

	last_timerEvent_CPU_work_elapsed = timerEvent_timer.elapsed();

	/*if(last_timerEvent_CPU_work_elapsed > 0.010)
	{
		logMessage("=============Long frame==================");
		logMessage("Frame CPU time: " + doubleToStringNSigFigs(last_timerEvent_CPU_work_elapsed * 1.0e3, 4) + " ms");
		logMessage("loading time: " + doubleToStringNSigFigs(frame_loading_time * 1.0e3, 4) + " ms");
		for(size_t i=0; i<loading_times.size(); ++i)
			logMessage("\t" + loading_times[i]);
		//conPrint("\tprocessing animated textures took " + doubleToStringNSigFigs(animated_tex_time * 1.0e3, 4) + " ms");
	}*/

	ui->glWidget->makeCurrent();

	// Render world-camera streams (Camera -> CameraScreen) before the main scene draw.
	// Requires the main GL context to be current.
	gui_client.renderWorldCameraStreams();
	gui_client.renderXRFrame(ui->glWidget->near_draw_dist, ui->glWidget->max_draw_dist);
	if(gui_client.getXRMirrorView().valid)
	{
		const XRMirrorView& mirror_view = gui_client.getXRMirrorView();
		ui->glWidget->setExternalPerspectiveCameraTransform(
			mirror_view.world_to_camera_space_matrix,
			mirror_view.sensor_width,
			mirror_view.lens_sensor_dist,
			mirror_view.render_aspect_ratio,
			mirror_view.lens_shift_up,
			mirror_view.lens_shift_right,
			mirror_view.projection_matrix_override_valid,
			mirror_view.projection_matrix_override
		);
	}
	else
	{
		ui->glWidget->clearExternalPerspectiveCameraTransform();
	}

	//Timer timer;
	{
		if(!gui_client.isXRActive())
		{
			Timer timer2;
			ZoneScopedNC("updateGL", 0x33FF33); // Tracy profiler
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
			ui->glWidget->update();
#else
			ui->glWidget->updateGL();
#endif
			//if(timer.elapsed() > 0.020)
			//	conPrint(doubleToStringNDecimalPlaces(Clock::getTimeSinceInit(), 3) + ": updateGL() took " + timer.elapsedStringNSigFigs(4));
			this->last_updateGL_time = timer2.elapsed();
			this->last_xr_companion_update_time = -1.0;
		}
		else
		{
			// While XR is active, render the desktop companion view at a throttled rate:
			// - high enough to keep visible sync with headset locomotion,
			// - low enough to avoid reintroducing the heavy per-frame companion cost.
			const bool can_render_companion_view = gui_client.getXRMirrorView().valid && this->isVisible() && !this->isMinimized();
			const double now = Clock::getTimeSinceInit();
			const bool companion_interval_elapsed = (this->last_xr_companion_update_time < 0.0) || ((now - this->last_xr_companion_update_time) >= XR_COMPANION_UPDATE_PERIOD_S);
			if(can_render_companion_view && companion_interval_elapsed)
			{
				Timer timer2;
				ZoneScopedNC("updateGL", 0x33FF33); // Tracy profiler
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
				ui->glWidget->update();
#else
				ui->glWidget->updateGL();
#endif
				this->last_updateGL_time = timer2.elapsed();
				this->last_xr_companion_update_time = now;
			}
			else
			{
				this->last_updateGL_time = 0.0;
			}
		}
	}

	// Plot the total time spent on CPU work this frame.
	// Note that we can't just measure the time of this timerEvent method with glWidget->updateGL(), because updateGL() will block for vsync, so it will include a lot of waiting time.
	// Instead use the sum of work time in this method plus the work time in OpenGLEngine::draw().
	if(CPU_render_stats_widget)
		CPU_render_stats_widget->addFrameTime((float)(last_timerEvent_CPU_work_elapsed + opengl_engine->last_draw_CPU_time));

	if(GPU_render_stats_widget)
		GPU_render_stats_widget->addFrameTime((float)opengl_engine->last_total_draw_GPU_time);
}


void MainWindow::changeEvent(QEvent* event)
{
	if(event->type() == QEvent::LanguageChange)
		refreshTranslatedUiText();

	// When the window is minimised, reduce the timer frequency from the default 1000hz.  Otherwise too much CPU will be used as no GL rendering takes place.
	// Restore it when maximised.
	if(event->type() == QEvent::WindowStateChange)
	{
		if(windowState() == Qt::WindowMinimized)
		{
			killTimer(main_timer_id);
			main_timer_id = startTimer(/*period (ms)=*/16);
		}
		else
		{
			startMainTimer();
		}
	}

	QMainWindow::changeEvent(event);
}


void MainWindow::updateDiagnostics()
{
	if(ui->diagnosticsDockWidget->isVisible() && (gui_client.num_frames_since_fps_timer_reset == 1))
	{
		ZoneScopedN("diagnostics"); // Tracy profiler

		//const double fps = num_frames / (double)fps_display_timer.elapsed();
		
		const bool do_graphics_diagnostics = ui->diagnosticsWidget->graphicsDiagnosticsCheckBox->isChecked();
		const bool do_physics_diagnostics = ui->diagnosticsWidget->physicsDiagnosticsCheckBox->isChecked();
		const bool do_terrain_diagnostics = ui->diagnosticsWidget->terrainDiagnosticsCheckBox->isChecked();

		const std::string msg = gui_client.getDiagnosticsString(do_graphics_diagnostics, do_physics_diagnostics, do_terrain_diagnostics, last_timerEvent_CPU_work_elapsed, last_updateGL_time);

		// Don't update diagnostics string when part of it is selected, so user can actually copy it.
		if(!ui->diagnosticsWidget->diagnosticsTextEdit->textCursor().hasSelection())
			ui->diagnosticsWidget->diagnosticsTextEdit->setPlainText(QtUtils::toQString(msg));
	}
}


void MainWindow::runScreenshotCode()
{
	if(run_as_screenshot_slave || test_screenshot_taking)
	{
		if(screenshot_output_path.empty()) // If we don't have a screenshot command we are currently executing:
		{
			try
			{
				if(run_as_screenshot_slave && screenshot_command_socket.isNull())
				{
					if(screenshot_command_listener.nonNull() && screenshot_command_listener->readable(/*timeout (s)=*/0.0))
					{
						conPrint("Accepting new screenshot command connection...");
						screenshot_command_socket = screenshot_command_listener->acceptConnection();
						screenshot_command_socket->setUseNetworkByteOrder(false);
						conPrint("Got screenshot command connection.");
					}
					else
						return;
				}

				if(test_screenshot_taking || screenshot_command_socket->readable(/*timeout (s)=*/0.01))
				{
					conPrint("Reading command from screenshot_command_socket etc...");
					const std::string command = test_screenshot_taking ? "takescreenshot" : screenshot_command_socket->readStringLengthFirst(1000);
					conPrint("Read screenshot command: " + command);
					screenshot_target_worldname_set = false;
					screenshot_target_worldname.clear();
					if(command == "takescreenshot")
					{
						if(test_screenshot_taking)
						{
							screenshot_campos = Vec3d(0, -1, 100);
							screenshot_camangles = Vec3d(0, 2.5f, 0); // (heading, pitch, roll).
							screenshot_width_px = 1024;
							screenshot_highlight_parcel_id = 10;
							screenshot_output_path = "test_screenshot.jpg";

							screenshot_ortho_sensor_width_m = 100;
							taking_map_screenshot = true;
						}
						else
						{
							screenshot_campos.x = screenshot_command_socket->readDouble();
							screenshot_campos.y = screenshot_command_socket->readDouble();
							screenshot_campos.z = screenshot_command_socket->readDouble();
							screenshot_camangles.x = screenshot_command_socket->readDouble();
							screenshot_camangles.y = screenshot_command_socket->readDouble();
							screenshot_camangles.z = screenshot_command_socket->readDouble();
							screenshot_width_px = screenshot_command_socket->readInt32();
							screenshot_highlight_parcel_id = screenshot_command_socket->readInt32();
							screenshot_output_path = screenshot_command_socket->readStringLengthFirst(1000);
							taking_map_screenshot = false;
						}
					}
					else if(command == "takemapscreenshot")
					{
						int tile_x, tile_y, tile_z;
						if(test_screenshot_taking)
						{
							tile_x = 0;
							tile_y = 0;
							tile_z = 7;
							screenshot_output_path = "test_screenshot.jpg";
						}
						else
						{
							tile_x = screenshot_command_socket->readInt32();
							tile_y = screenshot_command_socket->readInt32();
							tile_z = screenshot_command_socket->readInt32();
							screenshot_output_path = screenshot_command_socket->readStringLengthFirst(1000);
						}

						const int TILE_WIDTH_PX = 256; // Works the easiest with leaflet.js
						const float TILE_WIDTH_M = 5120.f / (1 << tile_z);
						screenshot_campos = Vec3d(
							(tile_x + 0.5) * TILE_WIDTH_M,
							(tile_y + 0.5) * TILE_WIDTH_M,
							200.0
						);
						screenshot_camangles = Vec3d(
							0, // Heading
							3.14, // pitch
							0 // roll
						);
						screenshot_ortho_sensor_width_m = TILE_WIDTH_M;
						screenshot_width_px = TILE_WIDTH_PX;
						screenshot_highlight_parcel_id = -1;
						taking_map_screenshot = true;
						screenshot_loading_timer.reset();
					}
					else if(command == "takemapscreenshot_world")
					{
						const std::string world_name = screenshot_command_socket->readStringLengthFirst(10000);
						const int tile_x = screenshot_command_socket->readInt32();
						const int tile_y = screenshot_command_socket->readInt32();
						const int tile_z = screenshot_command_socket->readInt32();
						screenshot_output_path = screenshot_command_socket->readStringLengthFirst(1000);

						screenshot_target_worldname_set = true;
						screenshot_target_worldname = world_name;

						const int TILE_WIDTH_PX = 256; // Works the easiest with leaflet.js
						const float TILE_WIDTH_M = 5120.f / (1 << tile_z);
						const double pos_x = (tile_x + 0.5) * TILE_WIDTH_M;
						const double pos_y = (tile_y + 0.5) * TILE_WIDTH_M;
						const double pos_z = 200.0;
						const double heading_deg = 0.0;

						// Switch to the requested world if needed, then let the normal "loaded_all" logic wait until everything is ready.
						if(gui_client.server_worldname != world_name)
						{
							std::string URL = "sub://" + gui_client.server_hostname + "/";
							URL += web::Escaping::URLEscape(world_name);
							URL += "?x=" + doubleToStringNDecimalPlaces(pos_x, 1) + "&y=" + doubleToStringNDecimalPlaces(pos_y, 1) + "&z=" + doubleToStringNDecimalPlaces(pos_z, 2) +
								"&heading=" + doubleToStringNDecimalPlaces(heading_deg, 1);
							gui_client.visitSubURL(URL, /*push_cur_URL_on_nav_stack=*/false, /*adjust_cur_URL_pos_back=*/false);

							total_timer.reset();
							time_since_last_screenshot.reset();
							time_since_last_waiting_msg.reset();
						}

						screenshot_campos = Vec3d(pos_x, pos_y, pos_z);
						screenshot_camangles = Vec3d(
							0, // Heading
							3.14, // pitch
							0 // roll
						);
						screenshot_ortho_sensor_width_m = TILE_WIDTH_M;
						screenshot_width_px = TILE_WIDTH_PX;
						screenshot_highlight_parcel_id = -1;
						taking_map_screenshot = true;
						screenshot_loading_timer.reset();
					}
					else if(command == "takegearsscreenshot")
					{
						taking_gear_screenshot = false;
						screenshot_gear_model_load_started = false;
						screenshot_gear_item = nullptr;
						screenshot_gear_world_ob = nullptr;

						// Read GearItem from socket using its self-describing buffer_size
						const uint32 version     = screenshot_command_socket->readUInt32();
						const uint32 buffer_size = screenshot_command_socket->readUInt32();
						if(buffer_size < 8 || buffer_size > 1000000)
							throw glare::Exception("Invalid GearItem buffer_size: " + toString(buffer_size));
						std::vector<uint8> gear_buf(buffer_size);
						std::memcpy(gear_buf.data(),     &version,     sizeof(uint32));
						std::memcpy(gear_buf.data() + 4, &buffer_size, sizeof(uint32));
						screenshot_command_socket->readData(gear_buf.data() + 8, buffer_size - 8);
						BufferInStream gear_stream(ArrayRef<uint8>(gear_buf.data(), gear_buf.size()));
						screenshot_gear_item = new GearItem();
						readGearItemFromStream(gear_stream, *screenshot_gear_item);

						screenshot_width_px = screenshot_command_socket->readInt32();
						screenshot_output_path = screenshot_command_socket->readStringLengthFirst(1000);
						taking_map_screenshot = false;
						taking_gear_screenshot = true;
						screenshot_loading_timer.reset();
					}
					else if(command == "quit")
					{
						conPrint("Received quit command, exiting...");
						exit(1);
					}
					else
						throw glare::Exception("received invalid screenshot command.");
				}
			}
			catch(glare::Exception& e)
			{
				conPrint("Excep while reading screenshot command from screenshot_command_socket: " + e.what() + ", waiting for next connection.");
				if(screenshot_command_socket.nonNull())
				{
					screenshot_command_socket->ungracefulShutdown();
					screenshot_command_socket = NULL;
				}
				//QMessageBox msgBox;
				//msgBox.setWindowTitle("Error");
				//msgBox.setText(QtUtils::toQString("Excep while reading screenshot command from screenshot_command_socket: " + e.what()));
				//msgBox.exec();
				return;
			}
		}
	}
	if(!screenshot_output_path.empty() && gui_client.world_state.nonNull())
	{
		if(taking_gear_screenshot)
		{
			// --- Gear screenshot path ---
			// On the first call, create a temp WorldObject for the gear model and trigger loading.
			if(!screenshot_gear_model_load_started && screenshot_gear_item.nonNull())
			{
				screenshot_gear_model_load_started = true;

				screenshot_gear_world_ob = new WorldObject();
				screenshot_gear_world_ob->uid = UID(100000000);
				screenshot_gear_world_ob->object_type = WorldObject::ObjectType_Generic;
				screenshot_gear_world_ob->model_url = screenshot_gear_item->model_url;
				screenshot_gear_world_ob->materials = screenshot_gear_item->materials;
				screenshot_gear_world_ob->pos = gui_client.cam_controller.getPosition();
				screenshot_gear_world_ob->scale = screenshot_gear_item->scale;
				screenshot_gear_world_ob->angle = 0;
				screenshot_gear_world_ob->axis = Vec3f(0, 0, 1);
				screenshot_gear_world_ob->in_proximity = true;
				screenshot_gear_world_ob->current_lod_level = 0;
				screenshot_gear_world_ob->max_model_lod_level = 0;
				screenshot_gear_world_ob->transformChanged();

				WorldStateLock lock(gui_client.world_state->mutex);
				gui_client.world_state->objects.insert(screenshot_gear_world_ob->uid, screenshot_gear_world_ob);
				gui_client.loadModelForObject(screenshot_gear_world_ob.ptr(), lock);
			}

			const size_t num_model_and_tex_tasks = gui_client.load_item_queue.size() + gui_client.model_and_texture_loader_task_manager.getNumUnfinishedTasks() + gui_client.model_loaded_messages_to_process.size();

			if(time_since_last_waiting_msg.elapsed() > 1.0)
			{
				conPrint("Waiting for gear model to load for screenshot (loading time: " + screenshot_loading_timer.elapsedStringNSigFigs(3) + ")...");
				time_since_last_waiting_msg.reset();
			}

			if(screenshot_loading_timer.elapsed() > 20.0)
			{
				conPrint("Took too long while trying to take gear screenshot, returning failure.");
				screenshot_output_path.clear();
				time_since_last_screenshot.reset();

				if(screenshot_command_socket.nonNull())
				{
					screenshot_command_socket->writeInt32(1);
					screenshot_command_socket->writeStringLengthFirst("Took too long while trying to take gear screenshot");
				}

				if(screenshot_gear_world_ob.nonNull())
				{
					gui_client.removeAndDeleteGLAndPhysicsObjectsForOb(*screenshot_gear_world_ob);
					Lock lock(gui_client.world_state->mutex);
					gui_client.world_state->objects.erase(screenshot_gear_world_ob->uid);
				}

				screenshot_gear_world_ob = nullptr;
				screenshot_gear_item = nullptr;
				taking_gear_screenshot = false;
				screenshot_gear_model_load_started = false;
				return;
			}

			const bool gear_loaded =
				screenshot_gear_world_ob.nonNull() &&
				screenshot_gear_world_ob->opengl_engine_ob.nonNull() &&
				(num_model_and_tex_tasks == 0) &&
				(gui_client.num_non_net_resources_downloading == 0) &&
				(gui_client.num_net_resources_downloading == 0) &&
				(time_since_last_screenshot.elapsed() > 1.0);

			if(gear_loaded)
			{
				OpenGLSceneRef gear_scene = new OpenGLScene(*opengl_engine);
				gear_scene->draw_water = false;
				gear_scene->background_colour = Colour3f(0.2f);
				gear_scene->bloom_strength = 0.3f;
				opengl_engine->addScene(gear_scene);

				OpenGLSceneRef old_scene = opengl_engine->getCurrentScene();
				opengl_engine->setCurrentScene(gear_scene);

				OpenGLMaterial env_mat;
				opengl_engine->setEnvMat(env_mat);
				opengl_engine->setSunDir(normalise(Vec4f(1,-1,1,0)));

				GLObjectRef gear_gl_ob = opengl_engine->allocateObject();
				gear_gl_ob->mesh_data = screenshot_gear_world_ob->opengl_engine_ob->mesh_data;
				gear_gl_ob->materials = screenshot_gear_world_ob->opengl_engine_ob->materials;
				gear_gl_ob->ob_to_world_matrix = Matrix4f::scaleMatrix(screenshot_gear_item->scale.x, screenshot_gear_item->scale.y, screenshot_gear_item->scale.z);
				opengl_engine->addObject(gear_gl_ob);

				const js::AABBox aabb_ws = gear_gl_ob->aabb_ws;
				const Vec4f centre = aabb_ws.centroid();
				const float half_diag = (aabb_ws.max_ - aabb_ws.min_).length() * 0.5f;
				const float cam_dist = myMax(0.1f, half_diag * 3.0f);

				const Matrix4f world_to_cam =
					Matrix4f::translationMatrix(0.f, cam_dist, 0.f) *
					Matrix4f::rotationAroundXAxis(-(1.3f - Maths::pi_2<float>())) *
					Matrix4f::rotationAroundZAxis(2.0f) *
					Matrix4f::translationMatrix(-centre);

				opengl_engine->setViewportDims(screenshot_width_px, screenshot_width_px);
				opengl_engine->setNearDrawDistance(myMax(0.001f, cam_dist * 0.01f));
				opengl_engine->setMaxDrawDistance(cam_dist * 100.f);
				opengl_engine->setPerspectiveCameraTransform(world_to_cam, /*sensor_width=*/0.035f, /*lens_sensor_dist=*/0.05f, /*render_aspect_ratio=*/1.0f, /*lens_shift_up=*/0.f, /*lens_shift_right=*/0.f);

				try
				{
					opengl_engine->waitForAllBuildingProgramsToBuild();
					ImageMapUInt8Ref map = opengl_engine->drawToBufferAndReturnImageMap();
					if(map->hasAlphaChannel())
						map = map->extract3ChannelImage();
					JPEGDecoder::save(map, screenshot_output_path, JPEGDecoder::SaveOptions(/*quality=*/95));

					screenshot_output_path.clear();
					time_since_last_screenshot.reset();

					if(screenshot_command_socket.nonNull())
					{
						screenshot_command_socket->writeInt32(0);
						screenshot_command_socket->writeStringLengthFirst("Success!");
					}
				}
				catch(glare::Exception& e)
				{
					conPrint("Exception while saving gear screenshot: " + e.what());
					screenshot_output_path.clear();
					time_since_last_screenshot.reset();

					if(screenshot_command_socket.nonNull())
					{
						screenshot_command_socket->writeInt32(1);
						screenshot_command_socket->writeStringLengthFirst("Exception: " + e.what());
					}
				}

				opengl_engine->removeObject(gear_gl_ob);
				opengl_engine->setCurrentScene(old_scene);
				opengl_engine->removeScene(gear_scene);

				if(screenshot_gear_world_ob.nonNull())
				{
					gui_client.removeAndDeleteGLAndPhysicsObjectsForOb(*screenshot_gear_world_ob);
					Lock lock(gui_client.world_state->mutex);
					gui_client.world_state->objects.erase(screenshot_gear_world_ob->uid);
				}

				screenshot_gear_world_ob = nullptr;
				screenshot_gear_item = nullptr;
				taking_gear_screenshot = false;
				screenshot_gear_model_load_started = false;
			}
			return;
		}

		if(!screenshot_output_path.empty()) // If we are in screenshot-taking mode:
		{
			// If we were asked to take a map screenshot for a specific world, wait until we are actually in that world.
			if(screenshot_target_worldname_set && (gui_client.server_worldname != screenshot_target_worldname))
				return;

			gui_client.cam_controller.setAngles(screenshot_camangles);
			gui_client.cam_controller.setFirstAndThirdPersonPositions(screenshot_campos);
			gui_client.player_physics.setEyePosition(screenshot_campos);

			// Enable fly mode so we don't just fall to the ground
			ui->actionFly_Mode->setChecked(true);
			gui_client.player_physics.setFlyModeEnabled(true);
			gui_client.cam_controller.setThirdPersonEnabled(false);
			ui->actionThird_Person_Camera->setChecked(false);
		}

		size_t num_obs;
		{
			Lock lock(gui_client.world_state->mutex);
			num_obs = gui_client.world_state->objects.size();
		}

		const bool map_screenshot = taking_map_screenshot;//parsed_args.isArgPresent("--takemapscreenshot");

		ui->glWidget->take_map_screenshot = map_screenshot;
		ui->glWidget->screenshot_ortho_sensor_width_m = screenshot_ortho_sensor_width_m;

		const size_t num_model_and_tex_tasks = gui_client.load_item_queue.size() + gui_client.model_and_texture_loader_task_manager.getNumUnfinishedTasks() + gui_client.model_loaded_messages_to_process.size();

		if(time_since_last_waiting_msg.elapsed() > 1.0)
		{
			conPrint("---------------Waiting for loading to be done for screenshot ---------------");
			printVar(num_obs);
			printVar(num_model_and_tex_tasks);
			printVar(gui_client.num_non_net_resources_downloading);
			printVar(gui_client.num_net_resources_downloading);

			time_since_last_waiting_msg.reset();
		}

		const bool loaded_all_resources =
			(num_model_and_tex_tasks == 0) &&
			(gui_client.num_non_net_resources_downloading == 0) &&
			(gui_client.num_net_resources_downloading == 0) &&
			(gui_client.terrain_system && gui_client.terrain_system->isTerrainFullyBuilt());
		const bool loaded_all =
			(time_since_last_screenshot.elapsed() > 3.0) && // Bit of a hack to allow time for the shadow mapping to render properly
			(num_obs > 0 || total_timer.elapsed() >= 15.0) && // Wait until we have downloaded some objects from the server, or (if the world is empty) X seconds have elapsed.
			(total_timer.elapsed() >= 4.0) && // Bit of a hack to allow time for the shadow mapping to render properly, also for the initial object query responses to arrive
			loaded_all_resources;

		if(loaded_all)
		{
			conPrint("Setting up for screenshot...");

			ui->editorDockWidget->hide();
			ui->chatDockWidget->hide();
			ui->diagnosticsDockWidget->hide();

			const int target_viewport_w = map_screenshot ? (screenshot_width_px * 2) : (650 * 2); // Existing screenshots are 650 px x 437 px.
			const int target_viewport_h = map_screenshot ? (screenshot_width_px * 2) : (437 * 2);

			conPrint("Setting geometry size...");

			// Make the gl widget a certain size so that the screenshot size / aspect ratio is consistent.
			ui->glWidget->setGeometry(0, 0, target_viewport_w, target_viewport_h);

			if(taking_map_screenshot)
				gui_client.removeParcelObjects();

			// Highlight requested parcel_id
			if(screenshot_highlight_parcel_id != -1)
			{
				Lock lock(gui_client.world_state->mutex);

				gui_client.addParcelObjects();

				auto res = gui_client.world_state->parcels.find(ParcelID(screenshot_highlight_parcel_id));
				if(res != gui_client.world_state->parcels.end())
				{
					// Deselect any existing gl objects
					ui->glWidget->opengl_engine->deselectAllObjects();

					gui_client.selected_parcel = res->second;
					ui->glWidget->opengl_engine->selectObject(gui_client.selected_parcel->opengl_engine_ob);
					ui->glWidget->opengl_engine->setSelectionOutlineColour(PARCEL_OUTLINE_COLOUR);
					ui->glWidget->opengl_engine->setSelectionOutlineWidth(6.0f);
				}
			}

			ui->glWidget->take_map_screenshot = taking_map_screenshot;

			opengl_engine->getCurrentScene()->draw_overlay_objects = false; // Hide UI

			opengl_engine->getCurrentScene()->cloud_shadows = false;

			try
			{
				conPrint("Taking screenshot...");

				ui->glWidget->updateGL(); // Make sure QGLWidget::paintGL gets called to set camera transform, sensor width etc.

				opengl_engine->setMainViewportDims(target_viewport_w, target_viewport_h);
				ImageMapUInt8Ref map = opengl_engine->drawToBufferAndReturnImageMap();
				if(map->hasAlphaChannel())
					map = map->extract3ChannelImage();

				JPEGDecoder::save(map, screenshot_output_path, JPEGDecoder::SaveOptions(/*quality=*/95));

				// Reset screenshot state
				screenshot_output_path.clear();
				screenshot_target_worldname_set = false;
				screenshot_target_worldname.clear();

				time_since_last_screenshot.reset();

				if(screenshot_command_socket.nonNull())
				{
					screenshot_command_socket->writeInt32(0); // Write success msg
					screenshot_command_socket->writeStringLengthFirst("Success!");
				}
			}
			catch(glare::Exception& e)
			{
				conPrint("Excep while saving screenshot: " + e.what());

				// Reset screenshot state
				screenshot_output_path.clear();

				time_since_last_screenshot.reset();

				if(screenshot_command_socket.nonNull())
				{
					screenshot_command_socket->writeInt32(1); // Write failure msg
					screenshot_command_socket->writeStringLengthFirst("Exception encountered: " + e.what());
				}
			}
		}
	}
}


static std::string printableServerURL(const std::string& hostname, const std::string& userpath)
{
	if(userpath.empty())
		return hostname;
	else
		return hostname + "/" + userpath;
}


void MainWindow::updateStatusBar()
{
	ZoneScoped; // Tracy profiler

	std::string status;
	switch(gui_client.connection_state)
	{
	case GUIClient::ServerConnectionState_NotConnected:
		status += "Not connected to server.";
		break;
	case GUIClient::ServerConnectionState_Connecting:
		status += "Connecting to " + printableServerURL(gui_client.server_hostname, gui_client.server_worldname) + "...";
		break;
	case GUIClient::ServerConnectionState_Connected:
		status += "Connected to " + printableServerURL(gui_client.server_hostname, gui_client.server_worldname);
		break;
	}

	const int total_num_non_net_resources_downloading = (int)gui_client.num_non_net_resources_downloading + (int)gui_client.download_queue.size();
	if(total_num_non_net_resources_downloading > 0)
		status += " | Downloading " + toString(total_num_non_net_resources_downloading) + ((total_num_non_net_resources_downloading == 1) ? " resource..." : " resources...");

	if(gui_client.num_net_resources_downloading > 0)
		status += " | Downloading " + toString(gui_client.num_net_resources_downloading) + ((gui_client.num_net_resources_downloading == 1) ? " web resource..." : " web resources...");

	if(gui_client.num_resources_uploading > 0)
		status += " | Uploading " + toString(gui_client.num_resources_uploading) + ((gui_client.num_resources_uploading == 1) ? " resource..." : " resources...");

	const size_t num_model_and_tex_tasks = gui_client.load_item_queue.size() + gui_client.model_and_texture_loader_task_manager.getNumUnfinishedTasks() + (gui_client.model_loaded_messages_to_process.size() + gui_client.texture_loaded_messages_to_process.size());
	if(num_model_and_tex_tasks > 0)
		status += " | Loading " + toString(num_model_and_tex_tasks) + ((num_model_and_tex_tasks == 1) ? " model or texture..." : " models and textures...");

	this->statusBar()->showMessage(QtUtils::toQString(status));
}


static void enqueueMessageToSend(ClientThread& client_thread, SocketBufferOutStream& packet)
{
	MessageUtils::updatePacketLengthField(packet);

	client_thread.enqueueDataToSend(packet.buf);
}


void MainWindow::on_actionAvatarSettings_triggered()
{
	if(avatar_dock_widget)
	{
		avatar_dock_widget->show();
		avatar_dock_widget->raise();
		avatar_dock_widget->activateWindow();
		return;
	}

	// Fallback (should not happen): keep legacy modal dialog if dock creation failed.
	AvatarSettingsDialog dialog(this->base_dir_path, this->settings, gui_client.resource_manager, &gui_client.animation_manager);
	(void)dialog.exec();
	ui->glWidget->makeCurrent();
}


void MainWindow::on_actionAddObject_triggered()
{
	const Vec3d ob_pos = gui_client.cam_controller.getFirstPersonPosition() + gui_client.cam_controller.getForwardsVec() * 2.0f;

	// Check permissions
	bool ob_pos_in_parcel;
	const bool have_creation_perms = gui_client.haveParcelObjectCreatePermissions(ob_pos, ob_pos_in_parcel);
	if(!have_creation_perms)
	{
		if(ob_pos_in_parcel)
			showErrorNotification("You do not have write permissions, and are not an admin for this parcel.");
		else
			showErrorNotification("You can only create objects in a parcel that you have write permissions for.");
		return;
	}

	AddObjectDialog dialog(this->base_dir_path, this->settings, gui_client.resource_manager, 
#ifdef _WIN32
		this->device_manager.ptr,
#else
		NULL,
#endif
		main_task_manager, high_priority_task_manager
	);
	const int res = dialog.exec();
	ui->glWidget->makeCurrent(); // Change back from the dialog GL context to the mainwindow GL context.

	if((res == QDialog::Accepted) && !dialog.loaded_materials.empty()) // If dialog was accepted, and we loaded an object successfully in it:
	{
		try
		{
			const Vec3d adjusted_ob_pos = ob_pos + gui_client.cam_controller.getRightVec() * dialog.ob_cam_right_translation + gui_client.cam_controller.getUpVec() * dialog.ob_cam_up_translation; // Centre object in front of camera

			// Some mesh types have a rotation to bring them to our z-up convention.  Don't change the rotation on those.
			Vec3f axis(0, 0, 1);
			float angle = 0;
			if(dialog.axis == Vec3f(0, 0, 1))
			{
				// If we don't have a rotation to z-up, make object face camera.
				angle = Maths::roundToMultipleFloating((float)gui_client.cam_controller.getAngles().x - Maths::pi_2<float>(), Maths::pi_4<float>()); // Round to nearest 45 degree angle, facing camera.
			}
			else
			{
				axis = dialog.axis;
				angle = dialog.angle;
			}

			gui_client.createObject(
				dialog.result_path,
				dialog.loaded_mesh,
				dialog.loaded_mesh_is_image_cube,
				dialog.loaded_voxels,
				adjusted_ob_pos,
				dialog.scale,
				axis,
				angle,
				dialog.loaded_materials
			);
		}
		catch(glare::Exception& e)
		{
			// Show error
			print(e.what());
			QErrorMessage m;
			m.showMessage(QtUtils::toQString(e.what()));
			m.exec();
		}
	}
}


void MainWindow::on_actionAddHypercard_triggered()
{
	const float quad_w = 0.4f;
	const Vec3d ob_pos = gui_client.cam_controller.getFirstPersonPosition() + gui_client.cam_controller.getForwardsVec() * 2.0f -
		gui_client.cam_controller.getUpVec() * quad_w * 0.5f -
		gui_client.cam_controller.getRightVec() * quad_w * 0.5f;

	// Check permissions
	bool ob_pos_in_parcel;
	const bool have_creation_perms = gui_client.haveParcelObjectCreatePermissions(ob_pos, ob_pos_in_parcel);
	if(!have_creation_perms)
	{
		if(ob_pos_in_parcel)
			showErrorNotification("You do not have write permissions, and are not an admin for this parcel.");
		else
			showErrorNotification("You can only create hypercards in a parcel that you have write permissions for.");
		return;
	}

	WorldObjectRef new_world_object = new WorldObject();
	new_world_object->uid = UID(0); // Will be set by server
	new_world_object->object_type = WorldObject::ObjectType_Hypercard;
	new_world_object->pos = ob_pos;
	new_world_object->axis = Vec3f(0, 0, 1);
	new_world_object->angle = Maths::roundToMultipleFloating((float)gui_client.cam_controller.getAngles().x - Maths::pi_2<float>(), Maths::pi_4<float>()); // Round to nearest 45 degree angle.
	new_world_object->scale = Vec3f(0.4f);
	new_world_object->content = "Select the object \nto edit this text";
	new_world_object->setAABBOS(js::AABBox(Vec4f(0,0,0,1), Vec4f(1,0,1,1)));

	// Send CreateObject message to server
	{
		MessageUtils::initPacket(scratch_packet, Protocol::CreateObject);
		new_world_object->writeToNetworkStream(scratch_packet, gui_client.server_protocol_version);

		enqueueMessageToSend(*gui_client.client_thread, scratch_packet);
	}

	showInfoNotification("Added hypercard.");
}


void MainWindow::on_actionAdd_Text_triggered()
{
	const float quad_w = 0.4f;
	const Vec3d ob_pos = gui_client.cam_controller.getFirstPersonPosition() + gui_client.cam_controller.getForwardsVec() * 2.0f -
		gui_client.cam_controller.getUpVec() * quad_w * 0.5f -
		gui_client.cam_controller.getRightVec() * quad_w * 0.5f;

	// Check permissions
	bool ob_pos_in_parcel;
	const bool have_creation_perms = gui_client.haveParcelObjectCreatePermissions(ob_pos, ob_pos_in_parcel);
	if(!have_creation_perms)
	{
		if(ob_pos_in_parcel)
			showErrorNotification("You do not have write permissions, and are not an admin for this parcel.");
		else
			showErrorNotification("You can only create text in a parcel that you have write permissions for.");
		return;
	}

	Quatf rot_upright = Quatf::fromAxisAndAngle(Vec3f(1,0,0), Maths::pi_2<float>());
	Quatf face_cam_rot = Quatf::fromAxisAndAngle(Vec3f(0,0,1), Maths::roundToMultipleFloating((float)gui_client.cam_controller.getAngles().x - Maths::pi_2<float>(), Maths::pi_4<float>())); // Round to nearest 45 degree angle.
	Quatf total_rot = face_cam_rot * rot_upright;
	Vec4f total_rot_axis;
	float total_rot_angle;
	total_rot.toAxisAndAngle(total_rot_axis, total_rot_angle);

	WorldObjectRef new_world_object = new WorldObject();
	new_world_object->uid = UID(0); // Will be set by server
	new_world_object->object_type = WorldObject::ObjectType_Text;
	new_world_object->pos = ob_pos;
	new_world_object->axis = toVec3f(total_rot_axis);
	new_world_object->angle = total_rot_angle;
	new_world_object->scale = Vec3f(0.4f);
	new_world_object->content = "Some Text";
	new_world_object->text_font = "Default"; // Initialize font field
	new_world_object->changed_flags = 0; // NO update flags for new object creation
	new_world_object->setAABBOS(js::AABBox(Vec4f(0,0,0,1), Vec4f(1,0,1,1)));

	new_world_object->materials.resize(1);
	new_world_object->materials[0] = new WorldMaterial();
	new_world_object->materials[0]->flags = WorldMaterial::COLOUR_TEX_HAS_ALPHA_FLAG | WorldMaterial::DOUBLE_SIDED_FLAG;

	// Send CreateObject message to server
	{
		MessageUtils::initPacket(scratch_packet, Protocol::CreateObject);
		new_world_object->writeToNetworkStream(scratch_packet, gui_client.server_protocol_version);

		enqueueMessageToSend(*gui_client.client_thread, scratch_packet);
	}

	showInfoNotification("Added Text.");
}


void MainWindow::on_actionAdd_Spotlight_triggered()
{
	const float quad_w = 0.4f;
	const Vec3d ob_pos = gui_client.cam_controller.getFirstPersonPosition() + gui_client.cam_controller.getForwardsVec() * 2.0f -
		gui_client.cam_controller.getUpVec() * quad_w * 0.5f -
		gui_client.cam_controller.getRightVec() * quad_w * 0.5f;

	// Check permissions
	bool ob_pos_in_parcel;
	const bool have_creation_perms = gui_client.haveParcelObjectCreatePermissions(ob_pos, ob_pos_in_parcel);
	if(!have_creation_perms)
	{
		if(ob_pos_in_parcel)
			showErrorNotification("You do not have write permissions, and are not an admin for this parcel.");
		else
			showErrorNotification("You can only create spotlights in a parcel that you have write permissions for.");
		return;
	}

	WorldObjectRef new_world_object = new WorldObject();
	new_world_object->uid = UID(0); // Will be set by server
	new_world_object->object_type = WorldObject::ObjectType_Spotlight;
	new_world_object->pos = ob_pos;
	new_world_object->axis = Vec3f(0, 0, 1);
	new_world_object->angle = 0;
	new_world_object->scale = Vec3f(1.f);

	new_world_object->type_data.spotlight_data.cone_start_angle = 0.317560429291521f; // = std::acos(0.95f); (old fixed value)
	new_world_object->type_data.spotlight_data.cone_end_angle   = 0.451026811796262f; // = std::acos(0.9f);  (old fixed value)

	// Emitting material
	new_world_object->materials.push_back(new WorldMaterial());
	new_world_object->materials.back()->emission_lum_flux_or_lum = 100000.f;

	// Spotlight housing material
	new_world_object->materials.push_back(new WorldMaterial());

	const float fixture_w = 0.1;
	const js::AABBox aabb_os = js::AABBox(Vec4f(-fixture_w/2, -fixture_w/2, 0,1), Vec4f(fixture_w/2,  fixture_w/2, 0,1));
	new_world_object->setAABBOS(aabb_os);


	// Send CreateObject message to server
	{
		MessageUtils::initPacket(scratch_packet, Protocol::CreateObject);
		new_world_object->writeToNetworkStream(scratch_packet, gui_client.server_protocol_version);

		enqueueMessageToSend(*gui_client.client_thread, scratch_packet);
	}

	showInfoNotification("Added spotlight.");
}


void MainWindow::on_actionAdd_Camera_triggered()
{
	if(gui_client.connection_state != GUIClient::ServerConnectionState_Connected)
	{
		showErrorNotification("Not connected to server.");
		return;
	}

	const uint32 camera_feature_protocol_version = 50; // ObjectType_Camera / ObjectType_CameraScreen support.
	if(gui_client.server_protocol_version < camera_feature_protocol_version)
	{
		showErrorNotification("This server does not support cameras yet. Server protocol version is " + toString(gui_client.server_protocol_version) +
			", required >= " + toString(camera_feature_protocol_version) + ".");
		return;
	}

	const Vec3d player_pos = gui_client.cam_controller.getFirstPersonPosition();
	const Vec3d fwd = gui_client.cam_controller.getForwardsVec();
	const Vec3d right = gui_client.cam_controller.getRightVec();
	const Vec3d up = gui_client.cam_controller.getUpVec();
	const Vec3d cam_ob_pos = player_pos + fwd * 2.0f - Vec3d(0,0,PlayerPhysics::getEyeHeight() * 0.4f);
	const Vec3d screen_ob_pos = cam_ob_pos + right * 1.2 + up * 0.2;

	// Check permissions for both objects.
	bool cam_ob_pos_in_parcel;
	if(!gui_client.haveParcelObjectCreatePermissions(cam_ob_pos, cam_ob_pos_in_parcel))
	{
		if(cam_ob_pos_in_parcel)
			showErrorNotification("You do not have write permissions, and are not an admin for this parcel.");
		else
			showErrorNotification("You can only create cameras in a parcel that you have write permissions for.");
		return;
	}

	bool screen_ob_pos_in_parcel;
	if(!gui_client.haveParcelObjectCreatePermissions(screen_ob_pos, screen_ob_pos_in_parcel))
	{
		if(screen_ob_pos_in_parcel)
			showErrorNotification("You do not have write permissions, and are not an admin for this parcel.");
		else
			showErrorNotification("You can only create camera screens in a parcel that you have write permissions for.");
		return;
	}

	// Screen mesh "forward" convention is local +Y (same as many existing object types),
	// while camera capture currently uses camera local +X as lens-forward.
	// Keep separate snapped angles so the camera points where the player looks,
	// and the screen stays oriented consistently with existing object conventions.
	const float camera_facing_angle = Maths::roundToMultipleFloating((float)gui_client.cam_controller.getAngles().x, Maths::pi_4<float>());
	const float screen_facing_angle = Maths::roundToMultipleFloating((float)gui_client.cam_controller.getAngles().x - Maths::pi_2<float>(), Maths::pi_4<float>());

	// Keep camera model upright by default (x = +90 deg in ObjectEditor),
	// while still orienting it towards the player's current facing direction around z.
	const Quatf yaw_rot = Quatf::zAxisRot(camera_facing_angle);
	const Quatf upright_rot = Quatf::xAxisRot(Maths::pi_2<float>());
	const Quatf camera_rot = normalise(yaw_rot * upright_rot);
	Vec4f camera_axis_4;
	float camera_angle = 0.f;
	camera_rot.toAxisAndAngle(camera_axis_4, camera_angle);
	const Vec3f camera_axis = Vec3f(camera_axis_4);

	if(gui_client.camera_opengl_mesh.isNull() || gui_client.camera_screen_opengl_mesh.isNull())
	{
		showErrorNotification("Camera meshes are not initialised yet. Please wait a moment and try again.");
		return;
	}

	WorldObjectRef camera_ob = new WorldObject();
	camera_ob->uid = UID(0); // Will be set by server
	camera_ob->object_type = WorldObject::ObjectType_Camera;
	camera_ob->pos = cam_ob_pos;
	camera_ob->axis = camera_axis;
	camera_ob->angle = camera_angle;
	camera_ob->scale = Vec3f(0.65f, 0.65f, 0.65f);
	camera_ob->type_data.camera_data.fov_y_rad = Maths::pi<float>() * 60.f / 180.f;
	camera_ob->type_data.camera_data.near_dist = 0.1f;
	camera_ob->type_data.camera_data.far_dist = 1000.f;
	camera_ob->type_data.camera_data.render_width = 512;
	camera_ob->type_data.camera_data.render_height = 288;
	camera_ob->type_data.camera_data.max_fps = 10;
	camera_ob->type_data.camera_data.enabled = 1;
	camera_ob->materials.push_back(new WorldMaterial());
	camera_ob->materials.back()->colour_rgb = Colour3f(0.15f, 0.15f, 0.15f);
	camera_ob->setAABBOS(gui_client.camera_opengl_mesh->aabb_os);

	WorldObjectRef screen_ob = new WorldObject();
	screen_ob->uid = UID(0); // Will be set by server
	screen_ob->object_type = WorldObject::ObjectType_CameraScreen;
	screen_ob->pos = screen_ob_pos;
	screen_ob->axis = Vec3f(0, 0, 1);
	screen_ob->angle = screen_facing_angle;
	screen_ob->scale = Vec3f(1.4f, 0.06f, 0.8f);
	screen_ob->type_data.camera_screen_data.source_camera_uid = 0; // Will be linked in a later phase after server-assigned UID is known.
	screen_ob->type_data.camera_screen_data.material_index = 0;
	screen_ob->type_data.camera_screen_data.enabled = 1;
	screen_ob->type_data.camera_screen_data._padding = 0;
	screen_ob->materials.push_back(new WorldMaterial());
	screen_ob->materials.back()->colour_rgb = Colour3f(0.05f, 0.05f, 0.05f);
	screen_ob->setAABBOS(gui_client.camera_screen_opengl_mesh->aabb_os);

	// Register expected camera/screen pair before network create messages to avoid a race
	// where server-assigned UIDs arrive before the pending pair is queued.
	gui_client.queuePendingCameraPairCreate(cam_ob_pos, screen_ob_pos);

	// Send CreateObject message for camera
	{
		MessageUtils::initPacket(scratch_packet, Protocol::CreateObject);
		camera_ob->writeToNetworkStream(scratch_packet, gui_client.server_protocol_version);
		enqueueMessageToSend(*gui_client.client_thread, scratch_packet);
	}

	// Send CreateObject message for camera screen
	{
		MessageUtils::initPacket(scratch_packet, Protocol::CreateObject);
		screen_ob->writeToNetworkStream(scratch_packet, gui_client.server_protocol_version);
		enqueueMessageToSend(*gui_client.client_thread, scratch_packet);
	}

	showInfoNotification("Added camera and camera screen.");
}


void MainWindow::on_actionAdd_Seat_triggered()
{
	if(gui_client.connection_state != GUIClient::ServerConnectionState_Connected)
	{
		showErrorNotification("Not connected to server.");
		return;
	}

	const uint32 seat_feature_protocol_version = 49; // AvatarSatOnSeat / AvatarGotUpFromSeat support.
	if(gui_client.server_protocol_version < seat_feature_protocol_version)
	{
		showErrorNotification("This server does not support seats yet. Server protocol version is " + toString(gui_client.server_protocol_version) +
			", required >= " + toString(seat_feature_protocol_version) + ".");
		return;
	}

	const float seat_w = 0.5f;
	const Vec3d ob_pos = gui_client.cam_controller.getFirstPersonPosition() + gui_client.cam_controller.getForwardsVec() * 2.0f -
		Vec3d(0,0,PlayerPhysics::getEyeHeight() * 0.3f);

	// Check permissions
	bool ob_pos_in_parcel;
	const bool have_creation_perms = gui_client.haveParcelObjectCreatePermissions(ob_pos, ob_pos_in_parcel);
	if(!have_creation_perms)
	{
		if(ob_pos_in_parcel)
			showErrorNotification("You do not have write permissions, and are not an admin for this parcel.");
		else
			showErrorNotification("You can only create seats in a parcel that you have write permissions for.");
		return;
	}

	WorldObjectRef new_world_object = new WorldObject();
	new_world_object->uid = UID(0); // Will be set by server
	new_world_object->object_type = WorldObject::ObjectType_Seat;
	new_world_object->pos = ob_pos;
	new_world_object->axis = Vec3f(0, 0, 1);
	new_world_object->angle = Maths::roundToMultipleFloating((float)gui_client.cam_controller.getAngles().x - Maths::pi_2<float>(), Maths::pi_4<float>()); // Face player
	new_world_object->scale = Vec3f(seat_w, seat_w, 1.f);

	// Set default seat data
	new_world_object->type_data.seat_data.upper_leg_angle = 1.57f; // ~90 degrees, legs bent forward at hips
	new_world_object->type_data.seat_data.lower_leg_angle = 1.57f; // ~90 degrees, bent at knees (negated in code)
	new_world_object->type_data.seat_data.upper_arm_angle = 2.65f; // ~152 degrees from overhead, arms down and slightly out
	new_world_object->type_data.seat_data.lower_arm_angle = 0.1f; // ~6 degrees, very slight elbow bend

	// Default material
	new_world_object->materials.push_back(new WorldMaterial());
	new_world_object->materials.back()->colour_rgb = Colour3f(0.4f, 0.5f, 0.6f);
	new_world_object->materials.back()->opacity = ScalarVal(0.5f);

	new_world_object->setAABBOS(gui_client.seat_opengl_mesh->aabb_os);

	// Send CreateObject message to server
	{
		MessageUtils::initPacket(scratch_packet, Protocol::CreateObject);
		new_world_object->writeToNetworkStream(scratch_packet, gui_client.server_protocol_version);

		enqueueMessageToSend(*gui_client.client_thread, scratch_packet);
	}

	showInfoNotification("Added seat.");
}


void MainWindow::on_actionAdd_Portal_triggered()
{
	const Vec3d ob_pos = gui_client.cam_controller.getFirstPersonPosition() + 
		removeComponentInDir(gui_client.cam_controller.getForwardsVec(), Vec3d(0,0,1)) * 3.0f - // Forwards from the camera position, parallel to ground plane
		Vec3d(0,0,PlayerPhysics::getEyeHeight()); // Then drop down to ground level that the player is standing on.

	// Check permissions
	bool ob_pos_in_parcel;
	const bool have_creation_perms = gui_client.haveParcelObjectCreatePermissions(ob_pos, ob_pos_in_parcel);
	if(!have_creation_perms)
	{
		if(ob_pos_in_parcel)
			showErrorNotification("You do not have write permissions, and are not an admin for this parcel.");
		else
			showErrorNotification("You can only create portals in a parcel that you have write permissions for.");
		return;
	}

	WorldObjectRef new_world_object = new WorldObject();
	new_world_object->uid = UID(0); // Will be set by server
	new_world_object->object_type = WorldObject::ObjectType_Portal;
	new_world_object->ensurePortalMaterialsPresent();
	new_world_object->pos = ob_pos;
	new_world_object->axis = Vec3f(0, 0, 1);
	new_world_object->angle = Maths::roundToMultipleFloating((float)gui_client.cam_controller.getAngles().x - Maths::pi_2<float>(), Maths::pi_4<float>()); // Round to nearest 45 degree angle, facing player.
	new_world_object->scale = Vec3f(1.f);

	new_world_object->setAABBOS(gui_client.spotlight_opengl_mesh->aabb_os);


	// Send CreateObject message to server
	{
		MessageUtils::initPacket(scratch_packet, Protocol::CreateObject);
		new_world_object->writeToNetworkStream(scratch_packet, gui_client.server_protocol_version);

		enqueueMessageToSend(*gui_client.client_thread, scratch_packet);
	}

	showInfoNotification("Added portal.");
}


static const char* favoritesSettingsKey()
{
	return "mainwindow/favorites";
}


struct FavoriteLocation
{
	QString name;
	QString url;
};


static std::vector<FavoriteLocation> loadFavoriteLocations(QSettings& settings)
{
	std::vector<FavoriteLocation> out;
	const QStringList rows = settings.value(favoritesSettingsKey()).toStringList();
	out.reserve((size_t)rows.size());

	for(const QString& row : rows)
	{
		const int tab_i = row.indexOf('\t');
		if(tab_i <= 0)
			continue;

		FavoriteLocation fav;
		fav.name = row.left(tab_i).trimmed();
		fav.url  = row.mid(tab_i + 1).trimmed();
		if(!fav.name.isEmpty() && !fav.url.isEmpty())
			out.push_back(fav);
	}

	return out;
}


static void saveFavoriteLocations(QSettings& settings, const std::vector<FavoriteLocation>& favs)
{
	QStringList rows;
	rows.reserve((int)favs.size());
	for(const FavoriteLocation& fav : favs)
		rows.push_back(fav.name + "\t" + fav.url);
	settings.setValue(favoritesSettingsKey(), rows);
}


void MainWindow::on_actionAdd_to_Favorites_triggered()
{
	const QString url = url_widget ? QtUtils::toQString(url_widget->getURL()).trimmed() : QString();
	if(url.isEmpty())
	{
		showErrorNotification("No current location URL to add to favorites.");
		return;
	}

	if(!settings)
	{
		showErrorNotification("Internal error: settings not available.");
		return;
	}

	std::vector<FavoriteLocation> favs = loadFavoriteLocations(*settings);
	for(const FavoriteLocation& fav : favs)
	{
		if(fav.url == url)
		{
			showInfoNotification("Location already in favorites.");
			return;
		}
	}

	FavoriteLocation fav;
	fav.name = "Favorite " + QString::number((int)favs.size() + 1);
	fav.url = url;
	favs.push_back(fav);
	saveFavoriteLocations(*settings, favs);

	updateFavoritesMenu();
	showInfoNotification("Added to favorites.");
}


void MainWindow::updateFavoritesMenu()
{
	if(!ui || !ui->menuGo_to_Favorites || !settings)
		return;

	ui->menuGo_to_Favorites->clear();
	const QString lucide_dir = LucideIconUtils::directoryForBasePath(base_dir_path);
	const QPalette icon_palette = QApplication::palette();
	const QColor foreground = icon_palette.color(QPalette::WindowText);
	const QColor favorite_colour = LucideIconUtils::themeAwareColour(
		QColor(QStringLiteral("#F59E0B")), icon_palette, QPalette::WindowText, QPalette::Window);

	const std::vector<FavoriteLocation> favs = loadFavoriteLocations(*settings);
	if(favs.empty())
	{
		QAction* a = ui->menuGo_to_Favorites->addAction(tr("(No favorites)"));
		if(!LucideIconUtils::setActionIcon(a, lucide_dir, QStringLiteral("star"), favorite_colour))
			setMenuActionGlyphIcon(a, QString::fromUtf8("★"));
		a->setEnabled(false);
		return;
	}

	for(const FavoriteLocation& fav : favs)
	{
		QAction* a = ui->menuGo_to_Favorites->addAction(fav.name);
		if(!LucideIconUtils::setActionIcon(a, lucide_dir, QStringLiteral("map-pin"), foreground))
			setMenuActionGlyphIcon(a, QString::fromUtf8("•"));
		a->setData(fav.url);
		connect(a, &QAction::triggered, this, [this, fav]() {
			visitSubURL(QtUtils::toStdString(fav.url));
		});
	}
}


bool MainWindow::eventFilter(QObject* obj, QEvent* event)
{
	if(ui && (obj == ui->menubar) && (event->type() == QEvent::ToolTip))
	{
		QHelpEvent* help_event = static_cast<QHelpEvent*>(event);
		QAction* action = ui->menubar->actionAt(help_event->pos());
		if(action && !action->toolTip().isEmpty())
		{
			QToolTip::showText(help_event->globalPos(), action->toolTip(), ui->menubar, ui->menubar->actionGeometry(action));
			return true;
		}

		QToolTip::hideText();
		event->ignore();
		return true;
	}

	if(event->type() == QEvent::MouseButtonRelease && obj && obj->property("chatPrivateListRow").toBool())
	{
		QMouseEvent* mouse_event = static_cast<QMouseEvent*>(event);
		if(mouse_event->button() == Qt::LeftButton)
		{
			bool uid_ok = false;
			const qulonglong uid_value = obj->property("chatOpenPrivateUid").toULongLong(&uid_ok);
			if(uid_ok)
			{
				startPrivateChatWithUser(UID((uint64)uid_value));
				return true;
			}

			const QString peer = obj->property("chatOpenPrivatePeer").toString();
			if(!peer.trimmed().isEmpty())
			{
				openPrivateChatDialog(peer);
				return true;
			}
		}
	}

	// Right-click on a favorite location in the "Go to Favorites" menu to rename or delete it.
	if(ui && ui->menuGo_to_Favorites && settings && (obj == ui->menuGo_to_Favorites))
	{
		QPoint menu_pos;
		QPoint global_pos;

		if(event->type() == QEvent::ContextMenu)
		{
			QContextMenuEvent* ce = static_cast<QContextMenuEvent*>(event);
			menu_pos   = ce->pos();
			global_pos = ce->globalPos();
		}
		else if(event->type() == QEvent::MouseButtonPress)
		{
			QMouseEvent* me = static_cast<QMouseEvent*>(event);
			if(me->button() != Qt::RightButton)
				return QMainWindow::eventFilter(obj, event);

			menu_pos   = me->pos();
			global_pos = me->globalPos();
		}
		else
		{
			return QMainWindow::eventFilter(obj, event);
		}

		QAction* a = ui->menuGo_to_Favorites->actionAt(menu_pos);
		if(!a)
			return QMainWindow::eventFilter(obj, event);

		const QString url = a->data().toString().trimmed();
		if(url.isEmpty())
			return QMainWindow::eventFilter(obj, event);

		QMenu context_menu;
		QAction* rename_action = context_menu.addAction(tr("Rename"));
		QAction* delete_action = context_menu.addAction(tr("Delete"));

		QAction* chosen_action = context_menu.exec(global_pos);
		if(chosen_action == rename_action)
		{
			bool ok = false;
			const QString new_name = QInputDialog::getText(this, tr("Rename favorite"), tr("New name:"), QLineEdit::Normal, a->text(), &ok).trimmed();
			if(ok && !new_name.isEmpty())
			{
				std::vector<FavoriteLocation> favs = loadFavoriteLocations(*settings);
				for(FavoriteLocation& fav : favs)
				{
					if(fav.url == url)
					{
						fav.name = new_name;
						saveFavoriteLocations(*settings, favs);
						updateFavoritesMenu();
						showInfoNotification("Favorite renamed.");
						break;
					}
				}
			}
			return true; // Eat the event so the menu doesn't trigger navigation.
		}
		else if(chosen_action == delete_action)
		{
			const QMessageBox::StandardButton res = QMessageBox::question(this, tr("Delete favorite"), tr("Delete this favorite?"), QMessageBox::Yes | QMessageBox::No);
			if(res == QMessageBox::Yes)
			{
				std::vector<FavoriteLocation> favs = loadFavoriteLocations(*settings);
				const size_t old_size = favs.size();
				favs.erase(std::remove_if(favs.begin(), favs.end(), [&](const FavoriteLocation& fav) { return fav.url == url; }), favs.end());
				if(favs.size() != old_size)
				{
					saveFavoriteLocations(*settings, favs);
					updateFavoritesMenu();
					showInfoNotification("Favorite deleted.");
				}
			}
			return true; // Eat the event so we don't navigate on right click.
		}

		// No action chosen, still eat the event so we don't navigate on right click.
		return true;
	}

	return QMainWindow::eventFilter(obj, event);
}


void MainWindow::on_actionAdd_Web_View_triggered()
{
	const float quad_w = 0.4f;
	const Vec3d ob_pos = gui_client.cam_controller.getFirstPersonPosition() + gui_client.cam_controller.getForwardsVec() * 2.0f -
		gui_client.cam_controller.getUpVec() * quad_w * 0.5f -
		gui_client.cam_controller.getRightVec() * quad_w * 0.5f;

	// Check permissions
	bool ob_pos_in_parcel;
	const bool have_creation_perms = gui_client.haveParcelObjectCreatePermissions(ob_pos, ob_pos_in_parcel);
	if(!have_creation_perms)
	{
		if(ob_pos_in_parcel)
			showErrorNotification("You do not have write permissions, and are not an admin for this parcel.");
		else
			showErrorNotification("You can only create web views in a parcel that you have write permissions for.");
		return;
	}

	WorldObjectRef new_world_object = new WorldObject();
	new_world_object->uid = UID(0); // Will be set by server
	new_world_object->object_type = WorldObject::ObjectType_WebView;
	new_world_object->pos = ob_pos;
	new_world_object->axis = Vec3f(0, 0, 1);
	new_world_object->angle = Maths::roundToMultipleFloating((float)gui_client.cam_controller.getAngles().x - Maths::pi_2<float>(), Maths::pi_4<float>()); // Round to nearest 45 degree angle.
	new_world_object->scale = Vec3f(/*width=*/1.f, /*depth=*/0.02f, /*height=*/1080.f / 1920.f);
	new_world_object->max_model_lod_level = 0;

	new_world_object->target_url = "https://vr.metasiberia.com/"; // Use a default URL - indicates to users how to set the URL.

	new_world_object->materials.resize(2);
	new_world_object->materials[0] = new WorldMaterial();
	new_world_object->materials[0]->colour_rgb = Colour3f(1.f);
	new_world_object->materials[1] = new WorldMaterial();

	const js::AABBox aabb_os = gui_client.image_cube_shape.getAABBOS();
	new_world_object->setAABBOS(aabb_os);


	// Send CreateObject message to server
	{
		MessageUtils::initPacket(scratch_packet, Protocol::CreateObject);
		new_world_object->writeToNetworkStream(scratch_packet, gui_client.server_protocol_version);

		enqueueMessageToSend(*gui_client.client_thread, scratch_packet);
	}

	showInfoNotification("Added web view.");
}


void MainWindow::on_actionAdd_Video_triggered()
{
	try
	{
		const float quad_w = 0.4f;
		const Vec3d ob_pos = gui_client.cam_controller.getFirstPersonPosition() + gui_client.cam_controller.getForwardsVec() * 2.0f -
			gui_client.cam_controller.getUpVec() * quad_w * 0.5f -
			gui_client.cam_controller.getRightVec() * quad_w * 0.5f;

		// Check permissions
		bool ob_pos_in_parcel;
		const bool have_creation_perms = gui_client.haveParcelObjectCreatePermissions(ob_pos, ob_pos_in_parcel);
		if(!have_creation_perms)
		{
			if(ob_pos_in_parcel)
				showErrorNotification("You do not have write permissions, and are not an admin for this parcel.");
			else
				showErrorNotification("You can only create videos in a parcel that you have write permissions for.");
			return;
		}

		AddVideoDialog dialog(this->settings, gui_client.resource_manager, 
#ifdef _WIN32
			this->device_manager.ptr
#else
			NULL
#endif
		);
		const int res = dialog.exec();

		if(res == QDialog::Accepted)
		{
			std::string use_URL;
			if(dialog.wasResultLocalPath())
			{
				if(dialog.getVideoLocalPath() == "")
					return;

				// Copy model to local resources dir if not already there.  UploadResourceThread will read from here.
				use_URL = gui_client.resource_manager->copyLocalFileToResourceDirAndReturnURL(dialog.getVideoLocalPath());
			}
			else
			{
				if(dialog.getVideoURL() == "")
					return;

				use_URL = dialog.getVideoURL();
			}

			WorldObjectRef new_world_object = new WorldObject();
			new_world_object->uid = UID(0); // Will be set by server
			new_world_object->object_type = WorldObject::ObjectType_Video;
			new_world_object->pos = ob_pos;
			new_world_object->axis = Vec3f(0, 0, 1);
			new_world_object->angle = Maths::roundToMultipleFloating((float)gui_client.cam_controller.getAngles().x - Maths::pi_2<float>(), Maths::pi_4<float>()); // Round to nearest 45 degree angle.
			new_world_object->scale = Vec3f(/*width=*/1.f, /*depth=*/0.02f, /*height=*/(float)dialog.video_height / dialog.video_width);
			new_world_object->max_model_lod_level = 0;

			BitUtils::setBit(new_world_object->flags, WorldObject::VIDEO_AUTOPLAY | WorldObject::VIDEO_LOOP);

			new_world_object->materials.resize(2);
			new_world_object->materials[0] = new WorldMaterial();
			new_world_object->materials[0]->colour_rgb = Colour3f(0.f);
			new_world_object->materials[0]->emission_lum_flux_or_lum = 20000; // NOTE: this material data is generally not used in loadModelForObject() for ObjectType_Video.
			new_world_object->materials[0]->emission_rgb = Colour3f(1.f);
			new_world_object->materials[0]->emission_texture_url = use_URL; // Video URL is stored in emission_texture_url. (need to put here so it is a resource dependency)
			new_world_object->materials[1] = new WorldMaterial();

			const js::AABBox aabb_os = gui_client.image_cube_shape.getAABBOS();
			new_world_object->setAABBOS(aabb_os);


			// Send CreateObject message to server
			{
				MessageUtils::initPacket(scratch_packet, Protocol::CreateObject);
				new_world_object->writeToNetworkStream(scratch_packet, gui_client.server_protocol_version);

				enqueueMessageToSend(*gui_client.client_thread, scratch_packet);
			}

			showInfoNotification("Added video.");
		}
	}
	catch(glare::Exception& e)
	{
		// Show error
		print(e.what());
		QErrorMessage m;
		m.showMessage(QtUtils::toQString(e.what()));
		m.exec();
	}
}


void MainWindow::on_actionAdd_Audio_Source_triggered()
{
	try
	{
		const float panel_w = 0.78f;
		const float panel_h = 0.24f;
		const Vec3d ob_pos = gui_client.cam_controller.getFirstPersonPosition() + gui_client.cam_controller.getForwardsVec() * 2.0f -
			gui_client.cam_controller.getUpVec() * panel_h * 0.5f -
			gui_client.cam_controller.getRightVec() * panel_w * 0.5f;

		// Check permissions
		bool ob_pos_in_parcel;
		const bool have_creation_perms = gui_client.haveParcelObjectCreatePermissions(ob_pos, ob_pos_in_parcel);
		if(!have_creation_perms)
		{
			if(ob_pos_in_parcel)
				showErrorNotification("You do not have write permissions, and are not an admin for this parcel.");
			else
				showErrorNotification("You can only create audio players in a parcel that you have write permissions for.");
			return;
		}

		const QString last_audio_dir = settings->value("mainwindow/lastAudioFileDir").toString();

		QFileDialog::Options options;
		QString selected_filter;
		const QStringList selected_filenames = QFileDialog::getOpenFileNames(this,
			tr("Select audio file(s)..."),
			last_audio_dir,
			tr("Audio file (*.mp3 *.wav *.aac *.m4a *.ogg *.opus *.flac)"),
			&selected_filter,
			options
		);

		if(!selected_filenames.isEmpty())
		{
			settings->setValue("mainwindow/lastAudioFileDir", QtUtils::toQString(FileUtils::getDirectory(QtUtils::toIndString(selected_filenames[0]))));

			std::vector<URLString> audio_file_URLs;
			audio_file_URLs.reserve(selected_filenames.size());
			for(int i=0; i<selected_filenames.size(); ++i)
			{
				const std::string path = QtUtils::toStdString(selected_filenames[i]);

				// Compute hash over audio file
				const uint64 audio_file_hash = FileChecksum::fileChecksum(path);

				const URLString audio_file_URL = ResourceManager::URLForPathAndHash(path, audio_file_hash);
				audio_file_URLs.push_back(audio_file_URL);

				// Copy audio file to local resources dir.  UploadResourceThread will read from here.
				gui_client.resource_manager->copyLocalFileToResourceDir(path, audio_file_URL);
			}

			WorldObjectRef new_world_object = new WorldObject();
			new_world_object->uid = UID(0); // Will be set by server
			new_world_object->object_type = WorldObject::ObjectType_WebView;
			new_world_object->pos = ob_pos;
			new_world_object->axis = Vec3f(0, 0, 1);
			new_world_object->angle = Maths::roundToMultipleFloating((float)gui_client.cam_controller.getAngles().x - Maths::pi_2<float>(), Maths::pi_4<float>()); // Round to nearest 45 degree angle.
			new_world_object->scale = Vec3f(panel_w, 0.02f, panel_h);
			new_world_object->max_model_lod_level = 0;
			new_world_object->model_url = "image_cube_5438347426447337425.bmesh";
			new_world_object->target_url = WorldObject::audioPlayerTargetURL();
			new_world_object->audio_volume = 1.0f;
			new_world_object->audio_player_activation_distance = WorldObject::DEFAULT_AUDIO_PLAYER_ACTIVATION_DISTANCE;
			BitUtils::setOrZeroBit(new_world_object->flags, WorldObject::AUDIO_AUTOPLAY, false);
			BitUtils::setOrZeroBit(new_world_object->flags, WorldObject::AUDIO_LOOP, false);
			BitUtils::setOrZeroBit(new_world_object->flags, WorldObject::AUDIO_SHUFFLE, false);

			for(size_t i=0; i<audio_file_URLs.size(); ++i)
			{
				if(i > 0)
					new_world_object->content += "\n";
				new_world_object->content += audio_file_URLs[i];
			}

			new_world_object->materials.resize(2);
			new_world_object->materials[0] = new WorldMaterial();
			new_world_object->materials[0]->colour_rgb = Colour3f(1.f);
			new_world_object->materials[1] = new WorldMaterial();

			const js::AABBox aabb_os = gui_client.image_cube_shape.getAABBOS();
			new_world_object->setAABBOS(aabb_os);


			// Send CreateObject message to server
			{
				MessageUtils::initPacket(scratch_packet, Protocol::CreateObject);
				new_world_object->writeToNetworkStream(scratch_packet, gui_client.server_protocol_version);

				enqueueMessageToSend(*gui_client.client_thread, scratch_packet);
			}

			showInfoNotification("Added audio player.");
		}
	}
	catch(glare::Exception& e)
	{
		QMessageBox msgBox;
		msgBox.setWindowTitle("Error");
		msgBox.setText(QtUtils::toQString(e.what()));
		msgBox.exec();
	}
}


void MainWindow::on_actionAdd_Decal_triggered()
{
	// Offset down by 0.25 to allow for centering with voxel width of 0.5.
	const Vec3d ob_pos = gui_client.cam_controller.getFirstPersonPosition() + gui_client.cam_controller.getForwardsVec() * 2.0f - Vec3d(0.25, 0.25, 0.25);

	// Check permissions
	bool ob_pos_in_parcel;
	const bool have_creation_perms = gui_client.haveParcelObjectCreatePermissions(ob_pos, ob_pos_in_parcel);
	if(!have_creation_perms)
	{
		if(ob_pos_in_parcel)
			showErrorNotification("You do not have write permissions, and are not an admin for this parcel.");
		else
			showErrorNotification("You can only create objects in a parcel that you have write permissions for.");
		return;
	}


	const Quatf facing_rot = Quatf::fromAxisAndAngle(Vec3f(0, 0, 1), Maths::roundToMultipleFloating((float)gui_client.cam_controller.getAngles().x - Maths::pi_2<float>(), Maths::pi_4<float>())); // Round to nearest 45 degree angle.
	const Quatf x_y_plane_to_vert_rot = Quatf::fromAxisAndAngle(Vec3f(1, 0, 0), Maths::pi_2<float>());

	Vec4f axis;
	float angle;
	(facing_rot * x_y_plane_to_vert_rot).toAxisAndAngle(axis, angle);

	WorldObjectRef new_world_object = new WorldObject();
	new_world_object->uid = UID(0); // Will be set by server
	new_world_object->object_type = WorldObject::ObjectType_Generic;
	new_world_object->pos = ob_pos;
	new_world_object->axis = Vec3f(axis);
	new_world_object->angle = angle;
	new_world_object->scale = Vec3f(1.f, 1.f, 1.f);
	new_world_object->max_model_lod_level = 0;
	BitUtils::zeroBit(new_world_object->flags, WorldObject::COLLIDABLE_FLAG); // make non-collidable.


	URLString unit_cube_mesh_URL = "unit_cube_bmesh_7263660735544605926.bmesh";
	if(!gui_client.resource_manager->isFileForURLPresent(unit_cube_mesh_URL))
	{
		Reference<Indigo::Mesh> indigo_mesh = MeshBuilding::makeUnitCubeIndigoMesh();
		BatchedMeshRef mesh = BatchedMesh::buildFromIndigoMesh(*indigo_mesh);
		const std::string bmesh_disk_path = PlatformUtils::getTempDirPath() + "/unit_cube.bmesh";
		mesh->writeToFile(bmesh_disk_path);
		unit_cube_mesh_URL = gui_client.resource_manager->copyLocalFileToResourceDirAndReturnURL(bmesh_disk_path);
		assert(unit_cube_mesh_URL == "unit_cube_bmesh_7263660735544605926.bmesh");
	}

	new_world_object->model_url = unit_cube_mesh_URL;

	new_world_object->materials.resize(1);
	new_world_object->materials[0] = new WorldMaterial();
	new_world_object->materials[0]->flags = WorldMaterial::DECAL_FLAG;

	const js::AABBox aabb_os = gui_client.image_cube_shape.getAABBOS();
	new_world_object->setAABBOS(aabb_os);


	// Send CreateObject message to server
	{
		MessageUtils::initPacket(scratch_packet, Protocol::CreateObject);
		new_world_object->writeToNetworkStream(scratch_packet, gui_client.server_protocol_version);

		enqueueMessageToSend(*gui_client.client_thread, scratch_packet);
	}



	showInfoNotification("Decal Object created.");

	// Deselect any currently selected object
	gui_client.deselectObject();
}


void MainWindow::on_actionAdd_Particles_triggered()
{
	const Vec3d ob_pos = gui_client.cam_controller.getFirstPersonPosition() + gui_client.cam_controller.getForwardsVec() * 2.0f - Vec3d(0, 0, 0.25);

	// Check permissions
	bool ob_pos_in_parcel;
	const bool have_creation_perms = gui_client.haveParcelObjectCreatePermissions(ob_pos, ob_pos_in_parcel);
	if(!have_creation_perms)
	{
		if(ob_pos_in_parcel)
			showErrorNotification("You do not have write permissions, and are not an admin for this parcel.");
		else
			showErrorNotification("You can only create particles in a parcel that you have write permissions for.");
		return;
	}

	URLString unit_cube_mesh_URL = "unit_cube_bmesh_7263660735544605926.bmesh";
	if(!gui_client.resource_manager->isFileForURLPresent(unit_cube_mesh_URL))
	{
		Reference<Indigo::Mesh> indigo_mesh = MeshBuilding::makeUnitCubeIndigoMesh();
		BatchedMeshRef mesh = BatchedMesh::buildFromIndigoMesh(*indigo_mesh);
		const std::string bmesh_disk_path = PlatformUtils::getTempDirPath() + "/unit_cube.bmesh";
		mesh->writeToFile(bmesh_disk_path);
		unit_cube_mesh_URL = gui_client.resource_manager->copyLocalFileToResourceDirAndReturnURL(bmesh_disk_path);
		assert(unit_cube_mesh_URL == "unit_cube_bmesh_7263660735544605926.bmesh");
	}

	WorldObjectRef new_world_object = new WorldObject();
	new_world_object->uid = UID(0); // Will be set by server
	new_world_object->object_type = WorldObject::ObjectType_Generic;
	new_world_object->pos = ob_pos;
	new_world_object->axis = Vec3f(0, 0, 1);
	new_world_object->angle = Maths::roundToMultipleFloating((float)gui_client.cam_controller.getAngles().x - Maths::pi_2<float>(), Maths::pi_4<float>());
	new_world_object->scale = Vec3f(0.25f);
	new_world_object->max_model_lod_level = 0;
	new_world_object->model_url = unit_cube_mesh_URL;
	new_world_object->content = ParticleEmitterSettings::serialiseToContent(ParticleEmitterSettings::defaultSmoke());
	new_world_object->script = "-- Particle emitter script\n-- emitter.start()\n-- emitter.stop()\n-- emitter.burst(32)\n-- emitter.clearParticles()\n";
	BitUtils::zeroBit(new_world_object->flags, WorldObject::COLLIDABLE_FLAG);

	new_world_object->materials.resize(1);
	new_world_object->materials[0] = new WorldMaterial();
	new_world_object->materials[0]->colour_rgb = Colour3f(0.12f, 0.55f, 1.0f);
	new_world_object->materials[0]->emission_rgb = Colour3f(0.1f, 0.45f, 0.9f);
	new_world_object->materials[0]->emission_lum_flux_or_lum = 120.f;
	new_world_object->materials[0]->opacity = ScalarVal(0.38f);
	new_world_object->materials[0]->flags = WorldMaterial::DOUBLE_SIDED_FLAG;
	new_world_object->setAABBOS(gui_client.image_cube_shape.getAABBOS());

	// Send CreateObject message to server
	{
		MessageUtils::initPacket(scratch_packet, Protocol::CreateObject);
		new_world_object->writeToNetworkStream(scratch_packet, gui_client.server_protocol_version);

		enqueueMessageToSend(*gui_client.client_thread, scratch_packet);
	}

	showInfoNotification("Added particles.");

	// Deselect any currently selected object
	gui_client.deselectObject();
}


void MainWindow::on_actionAddTree_triggered()
{
	const Vec3d ob_pos = gui_client.cam_controller.getFirstPersonPosition() + gui_client.cam_controller.getForwardsVec() * 3.0f;

	bool ob_pos_in_parcel;
	const bool have_creation_perms = gui_client.haveParcelObjectCreatePermissions(ob_pos, ob_pos_in_parcel);
	if(!have_creation_perms)
	{
		if(ob_pos_in_parcel)
			showErrorNotification("You do not have write permissions, and are not an admin for this parcel.");
		else
			showErrorNotification("You can only create trees in a parcel that you have write permissions for.");
		return;
	}

	TreeParams params = TreePresets::presetById(TreePresets::defaultPresetId());
	params.seed = (QRandomGenerator::global()->generate() & 0x7fffffffu);
	if(params.seed == 0)
		params.seed = 1;
	TreeSerialization::clamp(params);

	TreeObject tree(params);
	tree.rebuild();

	ModelLoading::MakeGLObjectResults results;
	ModelLoading::makeGLObjectForModelFile(*ui->glWidget->opengl_engine, *ui->glWidget->opengl_engine->vert_buf_allocator, /*allocator=*/nullptr, tree.generatedModelPath(), /*do_opengl_stuff=*/false, results);
	if(results.batched_mesh.isNull())
	{
		showErrorNotification("Failed to build procedural tree mesh.");
		return;
	}

	const std::string bmesh_disk_path = PlatformUtils::getTempDirPath() + "/metasiberia_tree.bmesh";
	BatchedMesh::WriteOptions write_options;
	write_options.compression_level = 9;
	results.batched_mesh->writeToFile(bmesh_disk_path, write_options);
	const uint64 model_hash = FileChecksum::fileChecksum(bmesh_disk_path);
	const URLString mesh_URL = ResourceManager::URLForNameAndExtensionAndHash("metasiberia_tree.obj", ::getExtension(bmesh_disk_path), model_hash);
	if(!gui_client.resource_manager->isFileForURLPresent(mesh_URL))
		gui_client.resource_manager->copyLocalFileToResourceDir(bmesh_disk_path, mesh_URL);
	const std::string resource_path = gui_client.resource_manager->pathForURL(mesh_URL);
	if(gui_client.connection_state != GUIClient::ServerConnectionState_NotConnected && FileUtils::fileExists(resource_path))
	{
		gui_client.num_resources_uploading++;
#if EMSCRIPTEN
		const size_t max_num_upload_threads = 1;
#else
		const size_t max_num_upload_threads = 4;
#endif
		if(gui_client.resource_upload_thread_manager.getNumThreads() == 0)
		{
			const std::string username = getUsernameForDomain(gui_client.server_hostname);
			const std::string password = getDecryptedPasswordForDomain(gui_client.server_hostname);
			for(size_t q=0; q<max_num_upload_threads; ++q)
				gui_client.resource_upload_thread_manager.addThread(new UploadResourceThread(&gui_client.msg_queue, &gui_client.upload_queue, gui_client.server_hostname, GUIClient::server_port, username, password, gui_client.client_tls_config, &gui_client.num_resources_uploading));
		}
		gui_client.upload_queue.enqueue(new ResourceToUpload(resource_path, mesh_URL));
	}

	WorldObjectRef new_world_object = new WorldObject();
	new_world_object->uid = UID(0);
	new_world_object->object_type = WorldObject::ObjectType_Generic;
	new_world_object->pos = ob_pos;
	new_world_object->axis = Vec3f(0, 0, 1);
	new_world_object->angle = 0.0f;
	new_world_object->scale = Vec3f(1.0f);
	new_world_object->max_model_lod_level = params.lodEnabled ? 2 : 0;
	new_world_object->model_url = mesh_URL;
	TreeObject::applyToWorldObject(*new_world_object, params, /*rebuild_mesh=*/false, TreeObject::findBundledAssetRoot(base_dir_path));

	// Convert bundled EZ-Tree textures into content-addressed resources before
	// the CreateObject packet is sent.  This makes the tree render identically
	// after reconnect and on other clients; the server can request the files via
	// the normal GetFile flow because they are already in ResourceManager.
	WorldObject::GetDependencyOptions dependency_options;
	dependency_options.use_basis = false;
	dependency_options.include_lightmaps = false;
	dependency_options.get_optimised_mesh = false;
	DependencyURLVector dependency_urls;
	new_world_object->appendDependencyURLsBaseLevel(dependency_options, dependency_urls);
	for(size_t i=0; i<dependency_urls.size(); ++i)
	{
		if(FileUtils::fileExists(dependency_urls[i].URL))
		{
			const URLString local_path = dependency_urls[i].URL;
			const URLString resource_url = ResourceManager::URLForPathAndHash(toStdString(local_path), FileChecksum::fileChecksum(local_path));
			if(!gui_client.resource_manager->isFileForURLPresent(resource_url))
				gui_client.resource_manager->copyLocalFileToResourceDir(toStdString(local_path), resource_url);
		}
	}
	new_world_object->convertLocalPathsToURLS(*gui_client.resource_manager);

	new_world_object->setAABBOS(results.batched_mesh->aabb_os);

	{
		MessageUtils::initPacket(scratch_packet, Protocol::CreateObject);
		new_world_object->writeToNetworkStream(scratch_packet, gui_client.server_protocol_version);
		enqueueMessageToSend(*gui_client.client_thread, scratch_packet);
	}

	showInfoNotification("Added procedural tree. Editor will open when the server confirms creation.");
	gui_client.deselectObject();
}


void MainWindow::on_actionAddScientificObject_triggered()
{
	const Vec3d ob_pos = gui_client.cam_controller.getFirstPersonPosition() + gui_client.cam_controller.getForwardsVec() * 2.0f - Vec3d(0, 0, 0.25);

	bool ob_pos_in_parcel;
	const bool have_creation_perms = gui_client.haveParcelObjectCreatePermissions(ob_pos, ob_pos_in_parcel);
	if(!have_creation_perms)
	{
		if(ob_pos_in_parcel)
			showErrorNotification("You do not have write permissions, and are not an admin for this parcel.");
		else
			showErrorNotification("You can only create scientific objects in a parcel that you have write permissions for.");
		return;
	}

	URLString unit_cube_mesh_URL = "unit_cube_bmesh_7263660735544605926.bmesh";
	if(!gui_client.resource_manager->isFileForURLPresent(unit_cube_mesh_URL))
	{
		Reference<Indigo::Mesh> indigo_mesh = MeshBuilding::makeUnitCubeIndigoMesh();
		BatchedMeshRef mesh = BatchedMesh::buildFromIndigoMesh(*indigo_mesh);
		const std::string bmesh_disk_path = PlatformUtils::getTempDirPath() + "/unit_cube.bmesh";
		mesh->writeToFile(bmesh_disk_path);
		unit_cube_mesh_URL = gui_client.resource_manager->copyLocalFileToResourceDirAndReturnURL(bmesh_disk_path);
		assert(unit_cube_mesh_URL == "unit_cube_bmesh_7263660735544605926.bmesh");
	}

	ScientificObjectSettings scientific_settings = ScientificObjectSettings::defaultObject();
	scientific_settings.name = "Scientific Object";
	scientific_settings.scientific_type = "custom";
	scientific_settings.source = "manual";
	scientific_settings.load_status = "idle";
	scientific_settings.load_status_message = "Scientific data has not been loaded yet.";
	scientific_settings.data_origin = "user";
	scientific_settings.collision_enabled = false;
	scientific_settings.solid = false;
	scientific_settings.trigger = false;
	scientific_settings.selectable = true;
	scientific_settings.movable = true;
	scientific_settings.gravity_enabled = false;
	scientific_settings.physics_motion_type = "static";
	scientific_settings.physics_shape = "mesh";
	scientific_settings.collision_layer = "scientific_visual";
	scientific_settings.description = "Universal scientific object. Import data, choose an online source, write code or generate code from a natural-language request.";
	scientific_settings.data_summary = "Scientific data has not been loaded yet. Use File, URL, Online Database, Code or Prompt source modes.";

	WorldObjectRef new_world_object = new WorldObject();
	new_world_object->uid = UID(0);
	new_world_object->object_type = WorldObject::ObjectType_Generic;
	new_world_object->pos = ob_pos;
	new_world_object->axis = Vec3f(0, 0, 1);
	new_world_object->angle = Maths::roundToMultipleFloating((float)gui_client.cam_controller.getAngles().x - Maths::pi_2<float>(), Maths::pi_4<float>());
	new_world_object->scale = Vec3f(0.5f);
	new_world_object->max_model_lod_level = 0;
	new_world_object->model_url = unit_cube_mesh_URL;
	new_world_object->content = ScientificObjectSettings::serialiseToContent(scientific_settings);
	new_world_object->script = "-- Scientific object script placeholder\n-- Future adapters can update this object from Python/JS/Lua-generated data.\n";
	new_world_object->setCollidable(false);
	new_world_object->setDynamic(false);
	new_world_object->setIsSensor(false);
	new_world_object->flags &= ~(WorldObject::AUDIO_AUTOPLAY | WorldObject::AUDIO_LOOP);
	new_world_object->mass = scientific_settings.physics_mass;
	new_world_object->friction = scientific_settings.physics_friction;
	new_world_object->restitution = scientific_settings.physics_restitution;

	new_world_object->materials.resize(1);
	new_world_object->materials[0] = new WorldMaterial();
	new_world_object->materials[0]->name = "Scientific Object Preview";
	new_world_object->materials[0]->colour_rgb = scientific_settings.display_colour;
	new_world_object->materials[0]->emission_rgb = Colour3f(0.03f, 0.12f, 0.18f);
	new_world_object->materials[0]->opacity = ScalarVal(0.58f);
	new_world_object->materials[0]->roughness = ScalarVal(0.35f);
	new_world_object->materials[0]->flags = WorldMaterial::DOUBLE_SIDED_FLAG;
	new_world_object->setAABBOS(gui_client.image_cube_shape.getAABBOS());

	{
		MessageUtils::initPacket(scratch_packet, Protocol::CreateObject);
		new_world_object->writeToNetworkStream(scratch_packet, gui_client.server_protocol_version);
		enqueueMessageToSend(*gui_client.client_thread, scratch_packet);
	}

	showInfoNotification("Added scientific object. Editor will open when the server confirms creation.");
	gui_client.deselectObject();
}


void MainWindow::on_actionAddCulturalObject_triggered()
{
	const Vec3d ob_pos = gui_client.cam_controller.getFirstPersonPosition() + gui_client.cam_controller.getForwardsVec() * 2.0f - Vec3d(0, 0, 0.25);

	bool ob_pos_in_parcel;
	const bool have_creation_perms = gui_client.haveParcelObjectCreatePermissions(ob_pos, ob_pos_in_parcel);
	if(!have_creation_perms)
	{
		if(ob_pos_in_parcel)
			showErrorNotification("You do not have write permissions, and are not an admin for this parcel.");
		else
			showErrorNotification("You can only create cultural objects in a parcel that you have write permissions for.");
		return;
	}

	URLString unit_cube_mesh_URL = "unit_cube_bmesh_7263660735544605926.bmesh";
	if(!gui_client.resource_manager->isFileForURLPresent(unit_cube_mesh_URL))
	{
		Reference<Indigo::Mesh> indigo_mesh = MeshBuilding::makeUnitCubeIndigoMesh();
		BatchedMeshRef mesh = BatchedMesh::buildFromIndigoMesh(*indigo_mesh);
		const std::string bmesh_disk_path = PlatformUtils::getTempDirPath() + "/unit_cube.bmesh";
		mesh->writeToFile(bmesh_disk_path);
		unit_cube_mesh_URL = gui_client.resource_manager->copyLocalFileToResourceDirAndReturnURL(bmesh_disk_path);
	}

	CulturalObjectSettings cultural_settings = CulturalObjectSettings::defaultObject();
	cultural_settings.uuid = QtUtils::toStdString(QUuid::createUuid().toString(QUuid::WithoutBraces));
	cultural_settings.title = "Cultural Object";
	cultural_settings.card_title = cultural_settings.title;
	cultural_settings.object_type = "custom";
	cultural_settings.cultural_category = "user_cultural_object";
	cultural_settings.description = "Cultural, artistic or heritage object. Choose its classifications, import local metadata, or connect it to a Cultural API source.";
	cultural_settings.source_mode = "manual";
	cultural_settings.provider_id = "manual";
	cultural_settings.retrieval_status = "idle";
	cultural_settings.modified_at = QtUtils::toStdString(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));

	WorldObjectRef new_world_object = new WorldObject();
	new_world_object->uid = UID(0);
	new_world_object->object_type = WorldObject::ObjectType_Generic;
	new_world_object->pos = ob_pos;
	new_world_object->axis = Vec3f(0, 0, 1);
	new_world_object->angle = Maths::roundToMultipleFloating((float)gui_client.cam_controller.getAngles().x - Maths::pi_2<float>(), Maths::pi_4<float>());
	new_world_object->scale = Vec3f(0.5f);
	new_world_object->max_model_lod_level = 0;
	new_world_object->model_url = unit_cube_mesh_URL;
	new_world_object->content = CulturalObjectSettings::serialiseToContent(cultural_settings);
	new_world_object->script = "-- Metasiberia CulturalObject v1\n";
	new_world_object->setCollidable(false);
	new_world_object->setDynamic(false);
	new_world_object->setIsSensor(false);

	new_world_object->materials.resize(1);
	new_world_object->materials[0] = new WorldMaterial();
	new_world_object->materials[0]->name = "Cultural Object Preview";
	new_world_object->materials[0]->colour_rgb = Colour3f(0.42f, 0.20f, 0.06f);
	new_world_object->materials[0]->emission_rgb = Colour3f(0.08f, 0.035f, 0.008f);
	new_world_object->materials[0]->opacity = ScalarVal(0.94f);
	new_world_object->materials[0]->roughness = ScalarVal(0.55f);
	new_world_object->materials[0]->flags = WorldMaterial::DOUBLE_SIDED_FLAG;
	new_world_object->setAABBOS(gui_client.image_cube_shape.getAABBOS());

	{
		MessageUtils::initPacket(scratch_packet, Protocol::CreateObject);
		new_world_object->writeToNetworkStream(scratch_packet, gui_client.server_protocol_version);
		enqueueMessageToSend(*gui_client.client_thread, scratch_packet);
	}

	showInfoNotification("Added cultural object. Editor will open when the server confirms creation.");
	gui_client.deselectObject();
}


void MainWindow::on_actionAddDocument_triggered()
{
	if(!document_editor_panel || !document_editor_dock_widget)
		return;

	const QString filename = QFileDialog::getOpenFileName(
		this,
		tr("Add Document"),
		QString(),
		tr("Documents (*.pdf *.md *.markdown *.html *.htm *.txt);;PDF (*.pdf);;Markdown (*.md *.markdown);;HTML (*.html *.htm);;Text (*.txt);;All files (*.*)"));
	if(filename.isEmpty())
		return;

	document_editor_dock_widget->setFloating(false);
	addDockWidget(Qt::RightDockWidgetArea, document_editor_dock_widget);
	document_editor_dock_widget->show();
	document_editor_dock_widget->raise();
	document_editor_panel->loadFile(filename);
}


void MainWindow::on_actionAdd_Voxels_triggered()
{
	// Offset down by 0.25 to allow for centering with voxel width of 0.5.
	const Vec3d ob_pos = gui_client.cam_controller.getFirstPersonPosition() + gui_client.cam_controller.getForwardsVec() * 2.0f - Vec3d(0.25, 0.25, 0.25);

	// Check permissions
	bool ob_pos_in_parcel;
	const bool have_creation_perms = gui_client.haveParcelObjectCreatePermissions(ob_pos, ob_pos_in_parcel);
	if(!have_creation_perms)
	{
		if(ob_pos_in_parcel)
			showErrorNotification("You do not have write permissions, and are not an admin for this parcel.");
		else
			showErrorNotification("You can only create objects in a parcel that you have write permissions for.");
		return;
	}

	
	WorldObjectRef new_world_object = new WorldObject();
	new_world_object->uid = UID(0); // Will be set by server
	new_world_object->object_type = WorldObject::ObjectType_VoxelGroup;
	new_world_object->materials.resize(1);
	new_world_object->materials[0] = new WorldMaterial();
	new_world_object->content = VoxelEditorData::serialiseToContent(VoxelEditorData::defaultForObject(*new_world_object));
	new_world_object->pos = ob_pos;
	new_world_object->axis = Vec3f(0, 0, 1);
	new_world_object->angle = 0;
	new_world_object->scale = Vec3f(0.5f); // This will be the initial width of the voxels
	new_world_object->getDecompressedVoxels().push_back(Voxel(Vec3<int>(0, 0, 0), 0)); // Start with a single voxel.
	new_world_object->compressVoxels();
	new_world_object->setAABBOS(new_world_object->getDecompressedVoxelGroup().getAABB());

	// Send CreateObject message to server
	{
		MessageUtils::initPacket(scratch_packet, Protocol::CreateObject);
		new_world_object->writeToNetworkStream(scratch_packet, gui_client.server_protocol_version);

		enqueueMessageToSend(*gui_client.client_thread, scratch_packet);
	}

	showInfoNotification("Voxel object created. The voxel editor will open after the server confirms creation.");

	// Deselect any currently selected object
	gui_client.deselectObject();
}


void MainWindow::on_actionCopy_Object_triggered()
{
	if(gui_client.selected_ob.nonNull())
	{
		QClipboard* clipboard = QGuiApplication::clipboard();
		QMimeData* mime_data = new QMimeData();

		BufferOutStream temp_buf;
		gui_client.selected_ob->writeToStream(temp_buf);

		mime_data->setData(/*mime-type:*/"x-substrata-object-binary", QByteArray((const char*)temp_buf.buf.data(), (int)temp_buf.buf.size()));
		clipboard->setMimeData(mime_data);
	}
	else if(gui_client.selected_parcel.nonNull())
	{
		if(!gui_client.logged_in_user_id.valid() || !isGodUser(gui_client.logged_in_user_id))
		{
			showErrorNotification("Only superadmin can copy parcels.");
			return;
		}

		QClipboard* clipboard = QGuiApplication::clipboard();
		QMimeData* mime_data = new QMimeData();

		BufferOutStream temp_buf;
		writeParcelToNetworkStream(*gui_client.selected_parcel, temp_buf, /*peer_protocol_version=*/Protocol::CyberspaceProtocolVersion);

		mime_data->setData(/*mime-type:*/"x-substrata-parcel-binary", QByteArray((const char*)temp_buf.buf.data(), (int)temp_buf.buf.size()));
		clipboard->setMimeData(mime_data);
	}
}


void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
	if(event->mimeData()->hasImage() || event->mimeData()->hasUrls())
		event->acceptProposedAction();
}


void MainWindow::dropEvent(QDropEvent* event)
{
	handlePasteOrDropMimeData(event->mimeData());
}


void MainWindow::handlePasteOrDropMimeData(const QMimeData* mime_data)
{
	try
	{
		// const QStringList formats = mime_data->formats();
		// for(auto it = formats.begin(); it != formats.end(); ++it)
		// 	conPrint("Format: " + it->toStdString());

		if(mime_data)
		{
			if(mime_data->hasUrls())
			{
				const QList<QUrl> urls = mime_data->urls();

				std::string image_path_to_load;
				std::string model_path_to_load;
				for(auto it = urls.begin(); it != urls.end(); ++it)
				{
					const std::string url = it->toString().toStdString();
					if(hasPrefix(url, "file:///"))
					{
						const std::string path = eatPrefix(url, "file:///");

						if(FileUtils::fileExists(path))
						{
							if(ImageDecoding::hasSupportedImageExtension(path))
								image_path_to_load = path;

							if(ModelLoading::hasSupportedModelExtension(path))
								model_path_to_load = path;
						}
					}
				}

				if(!image_path_to_load.empty())
					gui_client.createImageObject(image_path_to_load);

				if(!model_path_to_load.empty())
					gui_client.createModelObject(model_path_to_load);

				if(image_path_to_load.empty() && model_path_to_load.empty())
					throw glare::Exception("Pasted files / URLs did not contain a supported image or model format.");
			}
			else if(mime_data->hasFormat("x-substrata-object-binary")) // Binary encoded substrata object, from a user copying a substrata object.
			{
				const QByteArray ob_data = mime_data->data("x-substrata-object-binary");

				// Copy QByteArray to BufferInStream
				BufferInStream in_stream_buf;
				in_stream_buf.buf.resize(ob_data.size());
				if(ob_data.size() > 0)
					std::memcpy(in_stream_buf.buf.data(), ob_data.data(), ob_data.size());

				try
				{
					// Deserialise object
					WorldObjectRef pasted_ob = new WorldObject();
					readWorldObjectFromStream(in_stream_buf, *pasted_ob);

					// Choose a position for the pasted object.
					Vec3d new_ob_pos;
					if(pasted_ob->pos.getDist(gui_client.cam_controller.getFirstPersonPosition()) > 50.0) // If the source object is far from the camera:
					{
						// Position pasted object in front of the camera.
						const float ob_w = pasted_ob->getAABBWSLongestLength();
						new_ob_pos = gui_client.cam_controller.getFirstPersonPosition() + gui_client.cam_controller.getForwardsVec() * myMax(2.f, ob_w * 2.0f);
					}
					else
					{
						// If the camera is near the source object, position pasted object besides the source object.
						// Translate along an axis depending on the camera viewpoint.
						Vec3d use_offset_vec;
						if(std::fabs(gui_client.cam_controller.getRightVec().x) > std::fabs(gui_client.cam_controller.getRightVec().y))
							use_offset_vec = (gui_client.cam_controller.getRightVec().x > 0) ? Vec3d(1,0,0) : Vec3d(-1,0,0);
						else
							use_offset_vec = (gui_client.cam_controller.getRightVec().y > 0) ? Vec3d(0,1,0) : Vec3d(0,-1,0);

						// We don't want to paste directly in the same place as another object (for example a previously pasted object), otherwise users can create duplicate objects by mistake and lose them.
						// So check if there is already an object there, and choose another position if so.
						new_ob_pos = pasted_ob->pos;
						for(int i=0; i<100; ++i)
						{
							const Vec3d tentative_pos = pasted_ob->pos + use_offset_vec * (i + 1) * 0.5f;
							if(!gui_client.isObjectWithPosition(tentative_pos))
							{
								new_ob_pos = tentative_pos;
								break;
							}
						}
					}

					pasted_ob->pos = new_ob_pos;
					pasted_ob->transformChanged();

					// Check permissions
					bool ob_pos_in_parcel;
					const bool have_creation_perms = gui_client.haveParcelObjectCreatePermissions(new_ob_pos, ob_pos_in_parcel);
					if(!have_creation_perms)
					{
						if(ob_pos_in_parcel)
							showErrorNotification("You do not have write permissions, and are not an admin for this parcel.");
						else
							showErrorNotification("You can only create objects in a parcel that you have write permissions for.");
						return;
					}

					// Create object, by sending CreateObject message to server
					// Note that the recreated object will have a different ID than in the clipboard.
					{
						MessageUtils::initPacket(scratch_packet, Protocol::CreateObject);
						pasted_ob->writeToNetworkStream(scratch_packet, gui_client.server_protocol_version);

						enqueueMessageToSend(*gui_client.client_thread, scratch_packet);

						showInfoNotification("Object pasted.");
					}
				}
				catch(glare::Exception& e)
				{
					conPrint("Error while reading object from clipboard: " + e.what());
				}
			}
			else if(mime_data->hasFormat("x-substrata-parcel-binary")) // Binary encoded parcel, from a user copying a parcel.
			{
				if(!gui_client.logged_in_user_id.valid() || !isGodUser(gui_client.logged_in_user_id))
				{
					showErrorNotification("Only superadmin can paste parcels.");
					return;
				}

				const QByteArray parcel_data = mime_data->data("x-substrata-parcel-binary");

				// Copy QByteArray to BufferInStream
				BufferInStream in_stream_buf;
				in_stream_buf.buf.resize(parcel_data.size());
				if(parcel_data.size() > 0)
					std::memcpy(in_stream_buf.buf.data(), parcel_data.data(), parcel_data.size());

				try
				{
					Parcel pasted_parcel;
					(void)readParcelIDFromStream(in_stream_buf); // Read and ignore source parcel id.
					readParcelFromNetworkStreamGivenID(in_stream_buf, pasted_parcel, Protocol::CyberspaceProtocolVersion);

					// Offset parcel in XY using camera right+forward, so the pasted parcel is clearly visible nearby.
					const double size_x = std::fabs(pasted_parcel.aabb_max.x - pasted_parcel.aabb_min.x);
					const double size_y = std::fabs(pasted_parcel.aabb_max.y - pasted_parcel.aabb_min.y);
					const double side_dist = std::max(std::max(size_x, size_y), 1.0) + 3.0;
					const double forward_dist = std::max(std::min(size_x, size_y), 1.0) + 2.0;

					Vec2d right_2d(gui_client.cam_controller.getRightVec().x, gui_client.cam_controller.getRightVec().y);
					double right_len = std::sqrt(right_2d.x * right_2d.x + right_2d.y * right_2d.y);
					if(right_len > 1.0e-6)
						right_2d /= right_len;
					else
						right_2d = Vec2d(1.0, 0.0);

					Vec2d forw_2d(gui_client.cam_controller.getForwardsVec().x, gui_client.cam_controller.getForwardsVec().y);
					double forw_len = std::sqrt(forw_2d.x * forw_2d.x + forw_2d.y * forw_2d.y);
					if(forw_len > 1.0e-6)
						forw_2d /= forw_len;
					else
						forw_2d = Vec2d(0.0, 1.0);

					const Vec2d offset = right_2d * side_dist + forw_2d * forward_dist;

					for(int i=0; i<4; ++i)
						pasted_parcel.verts[i] += offset;

					pasted_parcel.id = ParcelID::invalidParcelID(); // Signal "create new parcel" to server.
					pasted_parcel.created_time = TimeStamp::currentTime();
					pasted_parcel.build();

					// Create parcel by sending ParcelFullUpdate with invalid parcel id; server will assign a fresh id.
					MessageUtils::initPacket(scratch_packet, Protocol::ParcelFullUpdate);
					writeParcelToNetworkStream(pasted_parcel, scratch_packet, /*peer_protocol_version=*/Protocol::CyberspaceProtocolVersion);
					enqueueMessageToSend(*gui_client.client_thread, scratch_packet);

					// Force parcel list refresh (helps if server-side broadcast is delayed).
					MessageUtils::initPacket(scratch_packet, Protocol::QueryParcels);
					enqueueMessageToSend(*gui_client.client_thread, scratch_packet);

					showInfoNotification("Parcel pasted. Server will assign a new parcel ID.");
				}
				catch(glare::Exception& e)
				{
					conPrint("Error while reading parcel from clipboard: " + e.what());
					showErrorNotification("Failed to paste parcel: " + e.what());
				}
			}
			else if(mime_data->hasImage()) // Image data (for example from snip screen)
			{
				QImage image = qvariant_cast<QImage>(mime_data->imageData());

				const std::string temp_path = PlatformUtils::getTempDirPath() + "/temp.jpg";
				const bool res = image.save(QtUtils::toQString(temp_path), "JPG", 95);
				if(!res)
					throw glare::Exception("Failed to save image to disk.");

				gui_client.createImageObjectForWidthAndHeight(temp_path, image.width(), image.height(), /*has alpha=*/false);
			}
		}
	}
	catch(glare::Exception& e)
	{
		// Show error
		print(e.what());
		QErrorMessage m;
		m.showMessage(QtUtils::toQString(e.what()));
		m.exec();
	}
}


void MainWindow::on_actionPaste_Object_triggered()
{
	QClipboard* clipboard = QGuiApplication::clipboard();
	const QMimeData* mime_data = clipboard->mimeData();

	handlePasteOrDropMimeData(mime_data);
}


void MainWindow::glWidgetCutShortcutTriggered()
{
	if(gui_client.gl_ui->getKeyboardFocusWidget().nonNull())
	{
		std::string new_clipboard_content;
		gui_client.gl_ui->handleCutEvent(new_clipboard_content);

		QMimeData* mime_data = new QMimeData();
		mime_data->setText(QtUtils::toQString(new_clipboard_content));

		QGuiApplication::clipboard()->setMimeData(mime_data);
	}
}


void MainWindow::glWidgetCopyShortcutTriggered()
{
	if(gui_client.gl_ui->getKeyboardFocusWidget().nonNull())
	{
		std::string new_clipboard_content;
		gui_client.gl_ui->handleCopyEvent(new_clipboard_content);

		QMimeData* mime_data = new QMimeData();
		mime_data->setText(QtUtils::toQString(new_clipboard_content));

		QGuiApplication::clipboard()->setMimeData(mime_data);
	}
	else
	{
		on_actionCopy_Object_triggered();
	}
}


void MainWindow::glWidgetPasteShortcutTriggered()
{
	if(gui_client.gl_ui->getKeyboardFocusWidget().nonNull())
	{
		QClipboard* clipboard = QGuiApplication::clipboard();
		const QMimeData* mime_data = clipboard->mimeData();

		if(mime_data->hasText())
		{
			TextInputEvent text_input_event;
			text_input_event.text = QtUtils::toStdString(mime_data->text());
			gui_client.handleTextInputEvent(text_input_event);
		}
		return;
	}
	else
		on_actionPaste_Object_triggered();
}


void MainWindow::on_actionCloneObject_triggered()
{
	if(gui_client.selected_ob.nonNull())
	{
		WorldObjectRef source_ob = gui_client.selected_ob;

		// Position cloned object besides the source object.
		// Translate along an axis depending on the camera viewpoint.
		Vec3d use_offset_vec;
		if(std::fabs(gui_client.cam_controller.getRightVec().x) > std::fabs(gui_client.cam_controller.getRightVec().y))
			use_offset_vec = (gui_client.cam_controller.getRightVec().x > 0) ? Vec3d(1,0,0) : Vec3d(-1,0,0);
		else
			use_offset_vec = (gui_client.cam_controller.getRightVec().y > 0) ? Vec3d(0,1,0) : Vec3d(0,-1,0);

		const Vec3d new_ob_pos = source_ob->pos + use_offset_vec;

		bool ob_pos_in_parcel;
		const bool have_creation_perms = gui_client.haveParcelObjectCreatePermissions(new_ob_pos, ob_pos_in_parcel);
		if(!have_creation_perms)
		{
			if(ob_pos_in_parcel)
				showErrorNotification("You do not have write permissions, and are not an admin for this parcel.");
			else
				showErrorNotification("You can only create objects in a parcel that you have write permissions for.");
			return;
		}

		WorldObjectRef new_world_object = new WorldObject();
		new_world_object->uid = UID(0); // Will be set by server
		new_world_object->object_type = source_ob->object_type;
		new_world_object->model_url = source_ob->model_url;
		new_world_object->script = source_ob->script;
		new_world_object->materials = source_ob->materials; // TODO: clone?
		new_world_object->content = source_ob->content;
		new_world_object->target_url = source_ob->target_url;
		new_world_object->pos = new_ob_pos;
		new_world_object->axis = source_ob->axis;
		new_world_object->angle = source_ob->angle;
		new_world_object->scale = source_ob->scale;
		new_world_object->flags = source_ob->flags;// | WorldObject::LIGHTMAP_NEEDS_COMPUTING_FLAG; // Lightmaps need to be built for it.
		new_world_object->getDecompressedVoxels() = source_ob->getDecompressedVoxels();
		new_world_object->getCompressedVoxels() = source_ob->getCompressedVoxels();
		new_world_object->audio_source_url = source_ob->audio_source_url;
		new_world_object->audio_volume = source_ob->audio_volume;
		new_world_object->audio_player_activation_distance = source_ob->audio_player_activation_distance;
		new_world_object->setAABBOS(source_ob->getAABBOS());

		new_world_object->max_model_lod_level = source_ob->max_model_lod_level;
		new_world_object->mass = source_ob->mass;
		new_world_object->friction = source_ob->friction;
		new_world_object->restitution = source_ob->restitution;


		// Send CreateObject message to server
		{
			MessageUtils::initPacket(scratch_packet, Protocol::CreateObject);
			new_world_object->writeToNetworkStream(scratch_packet, gui_client.server_protocol_version);

			enqueueMessageToSend(*gui_client.client_thread, scratch_packet);
		}

		// Deselect any currently selected object
		gui_client.deselectObject();

		showInfoNotification("Object cloned.");
	}
	else
	{
		QMessageBox msgBox;
		msgBox.setText("Please select an object before cloning.");
		msgBox.exec();
	}
}


void MainWindow::on_actionDeleteObject_triggered()
{
	if(gui_client.selected_ob.nonNull())
	{
		gui_client.deleteSelectedObject();
	}
}


void MainWindow::on_actionReset_Layout_triggered()
{
	ui->editorDockWidget->setFloating(false);
	this->addDockWidget(Qt::LeftDockWidgetArea, ui->editorDockWidget);
	ui->editorDockWidget->show();

	ui->chatDockWidget->setFloating(false);
	this->addDockWidget(Qt::RightDockWidgetArea, ui->chatDockWidget, Qt::Vertical);
	ui->chatDockWidget->show();

	if(map_dock_widget)
	{
		dockMetasiberiaMapLikeChat(this, map_dock_widget, ui->chatDockWidget);
		if(gui_client.usesEmbeddedMapDock())
			map_dock_widget->show();
		else
			map_dock_widget->hide();
	}

	ui->materialBrowserDockWidget->setFloating(false);
	this->addDockWidget(Qt::TopDockWidgetArea, ui->materialBrowserDockWidget, Qt::Horizontal);
	ui->materialBrowserDockWidget->show();

#if INDIGO_SUPPORT
	ui->indigoViewDockWidget->setFloating(false);
	this->addDockWidget(Qt::RightDockWidgetArea, ui->indigoViewDockWidget, Qt::Vertical);
	ui->indigoViewDockWidget->show();
#endif

	ui->helpInfoDockWidget->setFloating(true);
	ui->helpInfoDockWidget->show();
	// Position near bottom right corner of glWidget.
	ui->helpInfoDockWidget->setGeometry(QRect(ui->glWidget->mapToGlobal(ui->glWidget->geometry().bottomRight() + QPoint(-320, -120)), QSize(300, 100)));

	//this->addDockWidget(Qt::RightDockWidgetArea, ui->chatDockWidget, Qt::Vertical);
	//ui->chatDockWidget->show();

	ui->diagnosticsDockWidget->setFloating(false);
	this->addDockWidget(Qt::TopDockWidgetArea, ui->diagnosticsDockWidget, Qt::Horizontal);
	ui->diagnosticsDockWidget->show();


	// Enable tool bar
	ui->toolBar->setVisible(true);
}


void MainWindow::on_actionLogIn_triggered()
{
	if(gui_client.connection_state != GUIClient::ServerConnectionState_Connected)
	{
		QMessageBox msgBox;
		msgBox.setWindowTitle("Can't log in");
		msgBox.setText("You must be connected to a server to log in.");
		msgBox.exec();
		return;
	}

	LoginDialog dialog(settings, credential_manager, gui_client.server_hostname);
	const int res = dialog.exec();
	if(res == QDialog::Accepted)
	{
		const std::string username = QtUtils::toStdString(dialog.usernameLineEdit->text());
		const std::string password = QtUtils::toStdString(dialog.passwordLineEdit->text());

		//conPrint("username: " + username);
		//conPrint("password: " + password);
		//this->last_login_username = username;

		// Make LogInMessage packet and enqueue to send
		MessageUtils::initPacket(scratch_packet, Protocol::LogInMessage);
		scratch_packet.writeStringLengthFirst(username);
		scratch_packet.writeStringLengthFirst(password);

		enqueueMessageToSend(*gui_client.client_thread, scratch_packet);
	}
}


void MainWindow::on_actionLogOut_triggered()
{
	// Make message packet and enqueue to send
	MessageUtils::initPacket(scratch_packet, Protocol::LogOutMessage);
	enqueueMessageToSend(*gui_client.client_thread, scratch_packet);

	settings->setValue("LoginDialog/auto_login", false); // Don't log in automatically next start.
}


void MainWindow::on_actionSignUp_triggered()
{
	if(gui_client.connection_state != GUIClient::ServerConnectionState_Connected)
	{
		QMessageBox msgBox;
		msgBox.setWindowTitle("Can't sign up");
		msgBox.setText("You must be connected to a server to sign up.");
		msgBox.exec();
		return;
	}

	SignUpDialog dialog(settings, &credential_manager, gui_client.server_hostname);
	const int res = dialog.exec();
	if(res == QDialog::Accepted)
	{
		const std::string username = QtUtils::toStdString(dialog.usernameLineEdit->text());
		const std::string email    = QtUtils::toStdString(dialog.emailLineEdit->text());
		const std::string password = QtUtils::toStdString(dialog.passwordLineEdit->text());

		conPrint("username: " + username);
		conPrint("email:    " + email);
		conPrint("password: " + password);
		//this->last_login_username = username;

		// Make message packet and enqueue to send
		MessageUtils::initPacket(scratch_packet, Protocol::SignUpMessage);
		scratch_packet.writeStringLengthFirst(username);
		scratch_packet.writeStringLengthFirst(email);
		scratch_packet.writeStringLengthFirst(password);

		enqueueMessageToSend(*gui_client.client_thread, scratch_packet);
	}
}


void MainWindow::on_actionShow_Parcels_triggered()
{
	if(ui->actionShow_Parcels->isChecked())
	{
		gui_client.addParcelObjects();
	}
	else // Else if show parcels is now unchecked:
	{
		gui_client.removeParcelObjects();
	}

	settings->setValue("mainwindow/showParcels", QVariant(ui->actionShow_Parcels->isChecked()));
}


void MainWindow::on_actionFly_Mode_triggered()
{
	gui_client.setFlyModeEnabled(ui->actionFly_Mode->isChecked());

	settings->setValue("mainwindow/flyMode", QVariant(ui->actionFly_Mode->isChecked()));
}


void MainWindow::on_actionThird_Person_Camera_triggered()
{
	settings->setValue("mainwindow/thirdPersonCamera", QVariant(ui->actionThird_Person_Camera->isChecked()));

	gui_client.thirdPersonCameraToggled(ui->actionThird_Person_Camera->isChecked());
}


void MainWindow::on_actionGoToMainWorld_triggered()
{
	visitSubURL(makeMetasiberiaWorldSubURL(gui_client.server_hostname, /*world_name=*/""));
}


void MainWindow::on_actionGoToPersonalWorld_triggered()
{
	if(gui_client.logged_in_user_name != "")
	{
		visitSubURL(makeMetasiberiaWorldSubURL(gui_client.server_hostname, gui_client.logged_in_user_name));
	}
	else
	{
		QMessageBox msgBox;
		msgBox.setWindowTitle("Not logged in");
		msgBox.setText("You are not logged in, so we don't know your personal world name.  Please log in first.");
		msgBox.exec();
	}
}


void MainWindow::on_actionGo_to_CryptoVoxels_World_triggered()
{
	visitSubURL(makeMetasiberiaWorldSubURL(gui_client.server_hostname, "cryptovoxels"));
}


void MainWindow::on_actionGo_to_Substrata_Server_triggered()
{
	visitSubURL("sub://substrata.info/");
}


void MainWindow::on_actionGo_to_Metasiberia_Server_triggered()
{
	visitSubURL("sub://vr.metasiberia.com/");
}


void MainWindow::on_actionGo_to_Shki_nvkz_Server_triggered()
{
	visitSubURL("sub://176.197.223.42/");
}


void MainWindow::on_actionGo_to_Map_World_triggered()
{
	visitSubURL("sub://vr.metasiberia.com/map?x=0.0&y=0.0&z=1.67&heading=0.0");
}


void MainWindow::on_actionGo_to_Parcel_triggered()
{
	GoToParcelDialog d(this->settings);
	const int code = d.exec();
	if(code == QDialog::Accepted)
	{
		try
		{
			const int parcel_num = stringToInt(QtUtils::toStdString(d.parcelNumberLineEdit->text()));

			bool found = true;
			{
				Lock lock(gui_client.world_state->mutex);

				auto res = gui_client.world_state->parcels.find(ParcelID(parcel_num));
				if(res != gui_client.world_state->parcels.end())
				{
					const Parcel* parcel = res->second.ptr();

					gui_client.cam_controller.setFirstAndThirdPersonPositions(parcel->getVisitPosition());
					gui_client.player_physics.setEyePosition(parcel->getVisitPosition());
				}
				else
					found = false;
			}

			if(!found)
			{
				QMessageBox msgBox;
				msgBox.setWindowTitle("Invalid parcel number");
				msgBox.setText("There is no parcel with that number.");
				msgBox.exec();
			}
		}
		catch(glare::Exception&)
		{
			QMessageBox msgBox;
			msgBox.setWindowTitle("Invalid parcel number");
			msgBox.setText("Please enter just a number.");
			msgBox.exec();
		}
	}
}


void MainWindow::on_actionGo_to_Position_triggered()
{
	GoToPositionDialog d(this->settings, gui_client.cam_controller.getFirstPersonPosition());
	const int code = d.exec();
	if(code == QDialog::Accepted)
	{
		const Vec3d pos(
			d.XDoubleSpinBox->value(),
			d.YDoubleSpinBox->value(),
			d.ZDoubleSpinBox->value()
		);
			
		gui_client.cam_controller.setFirstAndThirdPersonPositions(pos);
		gui_client.player_physics.setEyePosition(pos);
	}
}


void MainWindow::on_actionSet_Start_Location_triggered()
{
	const std::string canonical_url = canonicaliseMetasiberiaSubURLHost(this->url_widget->getURL());
	settings->setValue(MainOptionsDialog::startLocationURLKey(), QtUtils::toQString(canonical_url));
}


void MainWindow::on_actionGo_To_Start_Location_triggered()
{
	const std::string start_URL = QtUtils::toStdString(settings->value(MainOptionsDialog::startLocationURLKey()).toString());
	const std::string canonical_start_URL = canonicaliseMetasiberiaSubURLHost(start_URL);
	if(canonical_start_URL != start_URL)
		settings->setValue(MainOptionsDialog::startLocationURLKey(), QtUtils::toQString(canonical_start_URL));

	if(canonical_start_URL.empty())
	{
		QMessageBox msgBox;
		msgBox.setWindowTitle("Invalid start location URL");
		msgBox.setText("You need to set a start location first with the 'Go > Set current location as start location' menu command.");
		msgBox.exec();
	}
	else
		visitSubURL(canonical_start_URL);
}


void MainWindow::on_actionFind_Object_triggered()
{
	FindObjectDialog d(this->settings);
	const int code = d.exec();
	if(code == QDialog::Accepted)
	{
		try
		{
			const int ob_id = stringToInt(QtUtils::toStdString(d.objectIDLineEdit->text()));

			bool found = true;
			{
				Lock lock(gui_client.world_state->mutex);

				auto res = gui_client.world_state->objects.find(UID(ob_id));
				if(res != gui_client.world_state->objects.end())
				{
					WorldObject* ob = res.getValue().ptr();

					gui_client.deselectObject();
					gui_client.selectObject(ob, /*selected_mat_index=*/0);
				}
				else
					found = false;
			}

			if(!found)
			{
				QMessageBox msgBox;
				msgBox.setWindowTitle("Invalid object id");
				msgBox.setText("There is no object with that id.");
				msgBox.exec();
			}
		}
		catch(glare::Exception&)
		{
			QMessageBox msgBox;
			msgBox.setWindowTitle("Invalid object id");
			msgBox.setText("Please enter just a number.");
			msgBox.exec();
		}
	}
}


void MainWindow::on_actionList_Objects_Nearby_triggered()
{
	ListObjectsNearbyDialog d(this->settings, gui_client.world_state.ptr(), gui_client.cam_controller.getPosition());
	const int code = d.exec();
	if(code == QDialog::Accepted)
	{
		const UID ob_id = d.getSelectedUID();
		if(ob_id.valid())
		{
			bool found = true;
			{
				Lock lock(gui_client.world_state->mutex);
				auto res = gui_client.world_state->objects.find(UID(ob_id));
				if(res != gui_client.world_state->objects.end())
				{
					WorldObject* ob = res.getValue().ptr();

					gui_client.deselectObject();
					gui_client.selectObject(ob, /*selected_mat_index=*/0);
				}
				else
					found = false;
			}

			if(!found)
			{
				QMessageBox msgBox;
				msgBox.setWindowTitle("Invalid object id");
				msgBox.setText("There is no object with that id.");
				msgBox.exec();
			}
		}
	}
}


void MainWindow::on_actionExport_view_to_Indigo_triggered()
{
	ui->indigoView->saveSceneToDisk();
}


void MainWindow::on_actionTake_Screenshot_triggered()
{
	opengl_engine->getCurrentScene()->draw_overlay_objects = false; // Hide UI

	ui->glWidget->makeCurrent();
	ImageMapUInt8Ref map = opengl_engine->drawToBufferAndReturnImageMap();
	if(map->hasAlphaChannel())
		map = map->extract3ChannelImage();

	const std::string path = this->appdata_path + "/screenshots/screenshot_" + toString((uint64)Clock::getSecsSince1970()) + ".png";
	try
	{
		FileUtils::createDirIfDoesNotExist(FileUtils::getDirectory(path));

		PNGDecoder::write(*map, path);
	
		showInfoNotification("Saved screenshot to " + path);

		settings_store->setStringValue("photo/last_saved_photo_path", path);
	}
	catch(glare::Exception& e)
	{
		QMessageBox msgBox;
		msgBox.setWindowTitle("Error");
		msgBox.setText(QtUtils::toQString("Saving screenshot to '" + path + "' failed: " + e.what()));
		msgBox.exec();
	}

	opengl_engine->getCurrentScene()->draw_overlay_objects = true; // Unhide UI.
}


void MainWindow::on_actionShow_Screenshot_Folder_triggered()
{
	try
	{
		const std::string path = this->appdata_path + "/screenshots/";

		PlatformUtils::openFileBrowserWindowAtLocation(path);
	}
	catch(glare::Exception& e)
	{
		QMessageBox msgBox;
		msgBox.setWindowTitle("Error");
		msgBox.setText(QtUtils::toQString(e.what()));
		msgBox.exec();
	}
}


void MainWindow::on_actionAbout_Substrata_triggered()
{
	AboutDialog d(this, appdata_path);
	d.exec();
}


void MainWindow::on_webcamEnableCheckBox_toggled(bool checked)
{
	// If the full WebcamWindow is active, it owns capture and UI.
	if(webcam_window)
		return;

	// Keep capture state in sync with checkbox state.
	gui_client.setWebcamEnabled(checked);

	if(!checked)
	{
		ui->webcamLabel->setText("Webcam disabled");
		ui->webcamLabel->setPixmap(QPixmap());
	}
}


void MainWindow::setWebcamWindowVisible(bool visible)
{
	if(!ui || !ui->webcamDockWidget)
		return;

	ui->webcamDockWidget->setVisible(visible);

	// If user hides the dock, stop capturing to avoid background CPU usage.
	if(!visible)
	{
		if(webcam_window)
			webcam_window->setWebcamEnabled(false);
		else if(ui->webcamEnableCheckBox)
			ui->webcamEnableCheckBox->setChecked(false);
	}
}


void MainWindow::on_actionUpdate_triggered()
{
	UpdateDialog d(update_manager, this);
	d.exec();
}


void MainWindow::on_actionOpen_Gear_Inventory_triggered()
{
	if(!gear_inventory_panel)
		return;

	ui->objectEditor->hide();
	ui->parcelEditor->hide();
	ui->botEditorWidget->hide();
	if(scientific_object_editor) scientific_object_editor->hide();
	if(cultural_object_editor) cultural_object_editor->hide();
	if(tree_editor_panel) tree_editor_panel->hide();
	if(voxel_editor_panel) voxel_editor_panel->hide();
	active_editor_kind = ActiveEditor_GearInventory;
	gear_inventory_panel->show();
	gear_inventory_panel->refreshFromClient();
	ui->editorDockWidget->setWindowTitle(tr("Инвентарь и экипировка"));
	if(ui->editorDockWidget->toggleViewAction())
		ui->editorDockWidget->toggleViewAction()->setText(ui->editorDockWidget->windowTitle());
	ui->editorDockWidget->setFloating(false);
	addDockWidget(Qt::LeftDockWidgetArea, ui->editorDockWidget);
	ui->editorDockWidget->show();
	gui_client.requestGearInventory();
}


void MainWindow::on_actionConvert_Selected_Object_To_Gear_Item_triggered()
{
	gui_client.convertSelectedObjectToGearItem();
}


void MainWindow::on_actionAddBot_triggered()
{
	// Position the bot 3 m in front of the player at ground level.
	const Vec3d bot_pos = gui_client.cam_controller.getFirstPersonPosition() +
		removeComponentInDir(gui_client.cam_controller.getForwardsVec(), Vec3d(0, 0, 1)) * 3.0 -
		Vec3d(0, 0, PlayerPhysics::getEyeHeight());

	// Heading: face toward the player (opposite of camera forward direction).
	const Vec3d fwd = gui_client.cam_controller.getForwardsVec();
	const float heading = (float)std::atan2(-fwd.x, -fwd.y); // bot faces the player

	gui_client.createBot(bot_pos, heading);
	showInfoNotification("Adding bot... Settings dialog will open on confirmation.");
}


void MainWindow::updateBotEditorPosition(double x, double y, double z)
{
	if(ui->botEditorWidget->isVisible())
	{
		ui->botEditorWidget->updatePosition(x, y, z);
	}
}


void MainWindow::setBotList(const std::vector<UIInterface::BotListEntry>& bots)
{
	std::vector<BotEditorWidget::BotListEntry> widget_bots;
	widget_bots.reserve(bots.size());
	for(const UIInterface::BotListEntry& bot : bots)
	{
		BotEditorWidget::BotListEntry entry;
		entry.bot_id = bot.bot_id;
		entry.avatar_uid = bot.avatar_uid;
		entry.name = bot.name;
		widget_bots.push_back(entry);
	}
	ui->botEditorWidget->setBotList(widget_bots);
}


void MainWindow::showBotEditor(uint64 bot_id, const UID& avatar_uid,
	const std::string& name, const std::string& avatar_url, const std::string& prompt,
	double px, double py, double pz, double heading_deg,
	const std::string& greeting_name, const std::string& greeting_url, double greeting_cooldown,
	const std::string& idle_name, const std::string& idle_url, double idle_interval,
	const std::string& reactive_name, const std::string& reactive_url, double reactive_cooldown,
	uint32 flags, double greeting_distance, double farewell_distance, double chat_radius,
	const Vec3f& model_scale,
	const std::string& ai_model_id, const std::string& ai_personality_preset, const std::string& ai_knowledge, double ai_temperature, uint32 ai_max_tokens,
	const std::string& audio_url, double audio_volume, double audio_radius, double audio_activation_distance, double audio_cooldown,
	uint32 trigger_flags, const std::string& trigger_keywords, double trigger_cooldown,
	uint32 greeting_gesture_flags, uint32 idle_gesture_flags, uint32 reactive_gesture_flags,
	const std::string& fallback_message,
	const std::string& surprise_name, const std::string& surprise_url, uint32 surprise_flags, double surprise_cooldown,
	const std::string& acknowledge_name, const std::string& acknowledge_url, uint32 acknowledge_flags, double acknowledge_cooldown,
	uint32 use_action_type, const std::string& use_action_param,
	const std::string& api_key, const std::string& api_endpoint,
	uint32 movement_type, double walk_speed, double wander_radius,
	const std::vector<BotWaypoint>& waypoints_raw,
	const std::vector<BotUseAction>& use_actions_raw,
	const std::string& farewell_gesture_name, const std::string& farewell_gesture_url,
	uint32 farewell_gesture_flags, double farewell_gesture_cooldown,
	const std::string& walk_gesture_name, const std::string& walk_gesture_url, uint32 walk_gesture_flags,
	const std::string& talk_gesture_name, const std::string& talk_gesture_url, uint32 talk_gesture_flags,
	const std::string& interaction_gesture_name, const std::string& interaction_gesture_url,
	uint32 interaction_gesture_flags, double interaction_gesture_cooldown,
	double audio_min_distance, double audio_start_delay,
	const std::string& greeting_audio_url, const std::string& farewell_audio_url2,
	const std::string& interaction_audio_url,
	// Block 9: advanced settings
	float conversation_timeout_s, uint32 max_llm_calls_per_hour,
	const std::string& webhook_url,
	uint32 active_hours_start_utc, uint32 active_hours_end_utc,
	const std::vector<BotScriptedResponse>& scripted_responses,
	const std::vector<std::string>& player_whitelist,
	const std::vector<std::string>& player_blacklist,
	const std::vector<BotToolFunctionInfo>& tool_functions,
	// Block 10
	uint32 ai_provider, float top_p, uint32 top_k,
	float frequency_penalty, float presence_penalty, uint32 max_context_messages,
	uint32 dialog_start_node_id, const std::vector<BotDialogNode>& dialog_nodes,
	// Block 11
	bool enable_player_memory, uint32 memory_summary_tokens,
	const std::string& content_filter_patterns, bool jailbreak_guard,
	// Block 12
	uint32 max_llm_calls_per_player_per_hour, bool response_cache_enabled,
	uint32 response_cache_ttl_s, const std::string& fallback_model_id,
	const std::string& fallback_api_key, const std::string& fallback_api_endpoint,
	uint32 llm_max_retries,
	uint32 stats_conversations_24h, uint32 stats_llm_calls_total)
{
	ui->botEditorWidget->init(&gui_client);

	// Connect signals once
	static bool connected = false;
	if(!connected)
	{
		connect(ui->botEditorWidget, &BotEditorWidget::saveClicked,   this, &MainWindow::onBotEditorSave);
		connect(ui->botEditorWidget, &BotEditorWidget::deleteClicked, this, &MainWindow::onBotEditorDelete);
		connect(ui->botEditorWidget, &BotEditorWidget::cancelClicked, this, &MainWindow::onBotEditorCancel);
		connect(ui->botEditorWidget, &BotEditorWidget::botSelected,   this, &MainWindow::onBotEditorBotSelected);
		connected = true;
	}

	// Hide other editors, show bot editor
	ui->objectEditor->hide();
	ui->parcelEditor->hide();
	if(scientific_object_editor)
		scientific_object_editor->hide();
	if(cultural_object_editor)
		cultural_object_editor->hide();
	if(tree_editor_panel)
		tree_editor_panel->hide();
	if(voxel_editor_panel)
		voxel_editor_panel->hide();

	// Set dock title to "Редактор ботов"
	ui->editorDockWidget->setWindowTitle(
		current_ui_language == RuntimeTranslation::UILanguage::Russian
		? "Редактор ботов"
		: "Bot Editor");

	const double heading_deg_val = heading_deg * (180.0 / 3.14159265358979323846);
	ui->botEditorWidget->setBot(bot_id, avatar_uid, name, avatar_url, prompt,
		px, py, pz, heading_deg_val,
		greeting_name, greeting_url, greeting_cooldown,
		idle_name, idle_url, idle_interval,
		reactive_name, reactive_url, reactive_cooldown,
		flags, greeting_distance, farewell_distance, chat_radius,
		model_scale,
		ai_model_id, ai_personality_preset, ai_knowledge, ai_temperature, ai_max_tokens,
		audio_url, audio_volume, audio_radius, audio_activation_distance, audio_cooldown,
		trigger_flags, trigger_keywords, trigger_cooldown,
		greeting_gesture_flags, idle_gesture_flags, reactive_gesture_flags,
		fallback_message,
		surprise_name, surprise_url, surprise_flags, surprise_cooldown,
		acknowledge_name, acknowledge_url, acknowledge_flags, acknowledge_cooldown,
		use_action_type, use_action_param,
		api_key, api_endpoint,
		movement_type, walk_speed, wander_radius,
		waypoints_raw, use_actions_raw,
		farewell_gesture_name, farewell_gesture_url, farewell_gesture_flags, farewell_gesture_cooldown,
		walk_gesture_name, walk_gesture_url, walk_gesture_flags,
		talk_gesture_name, talk_gesture_url, talk_gesture_flags,
		interaction_gesture_name, interaction_gesture_url, interaction_gesture_flags, interaction_gesture_cooldown,
		audio_min_distance, audio_start_delay,
		greeting_audio_url, farewell_audio_url2, interaction_audio_url,
		// Block 9
		conversation_timeout_s, max_llm_calls_per_hour, webhook_url,
		active_hours_start_utc, active_hours_end_utc,
		scripted_responses, player_whitelist, player_blacklist, tool_functions,
		// Block 10
		ai_provider, top_p, top_k,
		frequency_penalty, presence_penalty, max_context_messages,
		dialog_start_node_id, dialog_nodes,
		// Block 11
		enable_player_memory, memory_summary_tokens,
		content_filter_patterns, jailbreak_guard,
		// Block 12
		max_llm_calls_per_player_per_hour, response_cache_enabled,
		response_cache_ttl_s, fallback_model_id,
		fallback_api_key, fallback_api_endpoint,
		llm_max_retries,
		stats_conversations_24h, stats_llm_calls_total);

	ui->objectEditor->hide();
	ui->parcelEditor->hide();
	if(scientific_object_editor)
		scientific_object_editor->hide();
	if(cultural_object_editor)
		cultural_object_editor->hide();
	if(tree_editor_panel)
		tree_editor_panel->hide();
	if(voxel_editor_panel)
		voxel_editor_panel->hide();
	showEditorDockWidget();
}


void MainWindow::showBotPlayerMemoryList(uint64 bot_id, const std::vector<std::array<std::string,6>>& entries)
{
	if(ui->botEditorWidget->isVisible())
		ui->botEditorWidget->showPlayerMemoryList(entries);
}


void MainWindow::showBotConversationLog(uint64 bot_id, const std::vector<std::array<std::string,5>>& entries)
{
	if(ui->botEditorWidget->isVisible())
		ui->botEditorWidget->showConversationLog(entries);
}


void MainWindow::hideBotEditor()
{
	ui->botEditorWidget->clear(); // hides itself
	if(scientific_object_editor)
		scientific_object_editor->hide();
	if(cultural_object_editor)
		cultural_object_editor->hide();
	if(tree_editor_panel)
		tree_editor_panel->hide();
	if(voxel_editor_panel)
		voxel_editor_panel->hide();
	active_editor_kind = ActiveEditor_Object;
	ui->objectEditor->show();
	ui->editorDockWidget->setWindowTitle(
		current_ui_language == RuntimeTranslation::UILanguage::Russian ? "Редактор" : "Editor");
}


void MainWindow::onBotEditorSave()   { /* nothing extra needed - GUIClient call is in widget */ }
void MainWindow::onBotEditorDelete() { hideBotEditor(); }
void MainWindow::onBotEditorCancel() { hideBotEditor(); }
void MainWindow::onBotEditorBotSelected(uint64 /*bot_id*/, const UID& avatar_uid) { gui_client.selectBotAvatar(avatar_uid); }


void MainWindow::openBotSettingsDialog(uint64 bot_id)  // UIInterface override
{
	(void)bot_id;
	ui->editorDockWidget->show();
}


void MainWindow::on_actionOptions_triggered()
{
	const std::string prev_audio_input_dev_name = QtUtils::toStdString(settings->value(MainOptionsDialog::inputDeviceNameKey(), "Default").toString());

	MainOptionsDialog d(this->settings, gui_client.onlyLoadMostImportantObjectsDefaultValue());
	const int code = d.exec();
	if(code == QDialog::Accepted)
	{
		const float dist = (float)settings->value(MainOptionsDialog::objectLoadDistanceKey(), /*default val=*/(double)GUIClient::defaultObjectLoadDistance()).toDouble();
		gui_client.setObjectLoadDistance(dist);

		gui_client.setOnlyLoadMostImportantObs(settings->value(MainOptionsDialog::onlyLoadMostImportantObsKey(), /*default val=*/gui_client.onlyLoadMostImportantObjectsDefaultValue()).toBool());

		//ui->glWidget->opengl_engine->setMSAAEnabled(settings->value(MainOptionsDialog::MSAAKey(), /*default val=*/true).toBool());
		gui_client.opengl_engine->setSSAOEnabled(settings->value(MainOptionsDialog::SSAOKey(), /*default val=*/false).toBool());

		startMainTimer(); // Restart main timer, as the timer interval depends on max FPS, whiich may have changed.
	}

	gui_client.mic_read_thread_manager.enqueueMessage(new InputVolumeScaleChangedMessage(
		settings->value(MainOptionsDialog::inputScaleFactorNameKey(), /*default val=*/100).toInt() * 0.01f // input_vol_scale_factor (note: stored in percent in settings)
	));

	// Restart mic read thread if audio input device changed.
	if(QtUtils::toStdString(settings->value(MainOptionsDialog::inputDeviceNameKey(), "Default").toString()) != prev_audio_input_dev_name)
	{
		gui_client.mic_read_thread_manager.killThreadsBlocking();

		Reference<glare::MicReadThread> mic_read_thread = new glare::MicReadThread(&gui_client.msg_queue, gui_client.udp_socket, gui_client.client_avatar_uid, gui_client.server_hostname, gui_client.server_UDP_port,
			MainOptionsDialog::getInputDeviceName(settings),
			MainOptionsDialog::getInputScaleFactor(settings), // input_vol_scale_factor
			&gui_client.mic_read_status
		);
		gui_client.mic_read_thread_manager.addThread(mic_read_thread);
	}
}


void MainWindow::onUpdateCheckFinished()
{
	onUpdateAvailabilityChanged(update_manager && update_manager->updateAvailable());
}


void MainWindow::onUpdateAvailabilityChanged(bool available)
{
	if(!ui || !ui->actionUpdate || !update_manager)
		return;

	// Default UI state.
	ui->actionUpdate->setText("Update");
	ui->actionUpdate->setToolTip(ui->actionUpdate->text());
	ui->actionUpdate->setStatusTip(ui->actionUpdate->text());

	if(update_manager->checkInProgress())
		return;

	if(!update_manager->lastErrorString().isEmpty())
	{
		statusBar()->showMessage("Update check failed: " + update_manager->lastErrorString(), 15000);
		return;
	}

	if(!update_manager->hasCheckResult())
		return;

	if(available)
	{
		const QString tag = update_manager->latest().tag;
		ui->actionUpdate->setText("Update (" + tag + ")");
		ui->actionUpdate->setToolTip(ui->actionUpdate->text());
		ui->actionUpdate->setStatusTip(ui->actionUpdate->text());

		// Notify once per tag.
		const QString last_notified = settings ? settings->value("update/last_notified_tag", "").toString() : QString();
		if(settings && (last_notified != tag))
		{
			settings->setValue("update/last_notified_tag", tag);
			statusBar()->showMessage("Update available: " + tag, 20000);
			QMessageBox::information(this, "Update available", "A new version is available: " + tag + "\n\nUse the Update button in the top menu bar to download and install.");
		}
	}
	else
	{
		statusBar()->showMessage("You are up to date.", 8000);
	}
}


void MainWindow::on_actionUndo_triggered()
{
	try
	{
		if(active_editor_kind == ActiveEditor_Voxel)
		{
			if(gui_client.canUndoVoxelEdit())
			{
				gui_client.undoVoxelEdit(); // A permission failure is terminal; never fall through to unrelated global history.
				return;
			}
			if(!gui_client.selectedVoxelModificationAllowed("undo voxel edit"))
				return;
			gui_client.clearSelectedVoxelEditHistory(); // Drop any delta redo branch before crossing into global history.
		}
		WorldObjectRef ob = gui_client.undo_buffer.getUndoWorldObject();
		gui_client.applyUndoOrRedoObject(ob);
	}
	catch(glare::Exception& e)
	{
		conPrint("ERROR: Exception while trying to undo change: " + e.what());
	}
}


void MainWindow::on_actionRedo_triggered()
{
	try
	{
		if(active_editor_kind == ActiveEditor_Voxel)
		{
			if(gui_client.canRedoVoxelEdit())
			{
				gui_client.redoVoxelEdit();
				return;
			}
			if(!gui_client.selectedVoxelModificationAllowed("redo voxel edit"))
				return;
			gui_client.clearSelectedVoxelEditHistory();
		}
		WorldObjectRef ob = gui_client.undo_buffer.getRedoWorldObject();
		gui_client.applyUndoOrRedoObject(ob);
	}
	catch(glare::Exception& e)
	{
		conPrint("ERROR: Exception while trying to redo change: " + e.what());
	}
}


void MainWindow::on_actionShow_Log_triggered()
{
	this->log_window->show();
	this->log_window->raise();
}


void MainWindow::on_actionBake_Lightmaps_fast_for_all_objects_in_parcel_triggered()
{
	gui_client.bakeLightmapsForAllObjectsInParcel(WorldObject::LIGHTMAP_NEEDS_COMPUTING_FLAG);
}


void MainWindow::on_actionBake_lightmaps_high_quality_for_all_objects_in_parcel_triggered()
{
	gui_client.bakeLightmapsForAllObjectsInParcel(WorldObject::HIGH_QUAL_LIGHTMAP_NEEDS_COMPUTING_FLAG);
}


void MainWindow::on_actionSummon_Bike_triggered()
{
	try
	{
		gui_client.summonBike();
	}
	catch(glare::Exception& e)
	{
		showErrorNotification(e.what());

		QMessageBox msgBox;
		msgBox.setText(QtUtils::toQString(e.what()));
		msgBox.exec();
	}
}


void MainWindow::on_actionSummon_Hovercar_triggered()
{
	try
	{
		gui_client.summonHovercar();
	}
	catch(glare::Exception& e)
	{
		showErrorNotification(e.what());

		QMessageBox msgBox;
		msgBox.setText(QtUtils::toQString(e.what()));
		msgBox.exec();
	}
}


void MainWindow::on_actionSummon_Boat_triggered()
{
	try
	{
		gui_client.summonBoat();
	}
	catch(glare::Exception& e)
	{
		showErrorNotification(e.what());

		QMessageBox msgBox;
		msgBox.setText(QtUtils::toQString(e.what()));
		msgBox.exec();
	}
}


void MainWindow::on_actionSummon_Jet_Ski_triggered()
{
	try
	{
		gui_client.summonJetSki();
	}
	catch(glare::Exception& e)
	{
		showErrorNotification(e.what());

		QMessageBox msgBox;
		msgBox.setText(QtUtils::toQString(e.what()));
		msgBox.exec();
	}
}


void MainWindow::on_actionSummon_Car_triggered()
{
	try
	{
		gui_client.summonCar();
	}
	catch(glare::Exception& e)
	{
		showErrorNotification(e.what());

		QMessageBox msgBox;
		msgBox.setText(QtUtils::toQString(e.what()));
		msgBox.exec();
	}
}


void MainWindow::on_actionMute_Audio_toggled(bool checked)
{
	if(checked)
	{
		gui_client.audio_engine.setMasterVolume(0.f);
	}
	else
	{	
		gui_client.audio_engine.setMasterVolume(1.f);
	}
}


void MainWindow::on_actionSave_Object_To_Disk_triggered()
{
	if(gui_client.selected_ob)
	{
		QString last_save_object_dir = settings->value("mainwindow/lastSaveObjectDir").toString();

		QFileDialog::Options options;
		QString selected_filter;
		const QString selected_filename = QFileDialog::getSaveFileName(this,
			tr("Select file..."),
			last_save_object_dir,
			tr("XML file (*.xml)"),
			&selected_filter,
			options
		);

		if(!selected_filename.isEmpty())
		{
			settings->setValue("mainwindow/lastSaveObjectDir", QtUtils::toQString(FileUtils::getDirectory(QtUtils::toIndString(selected_filename))));

			try
			{
				const std::string xml = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n" + gui_client.selected_ob->serialiseToXML(/*tab depth=*/0);

				FileUtils::writeEntireFileTextMode(QtUtils::toStdString(selected_filename), xml);

				gui_client.showInfoNotification("Saved object to '" + QtUtils::toStdString(selected_filename) + "'.");
			}
			catch(glare::Exception& e)
			{
				QtUtils::showErrorMessageDialog("Error saving object to disk: " + e.what(), this);
			}
		}
	}
}


void MainWindow::on_actionSave_Parcel_Objects_To_Disk_triggered()
{
	QString last_save_object_dir = settings->value("mainwindow/lastSaveObjectDir").toString();

	QFileDialog::Options options;
	QString selected_filter;
	const QString selected_filename = QFileDialog::getSaveFileName(this,
		tr("Select file..."),
		last_save_object_dir,
		tr("XML file (*.xml)"),
		&selected_filter,
		options
	);

	if(!selected_filename.isEmpty())
	{
		settings->setValue("mainwindow/lastSaveObjectDir", QtUtils::toQString(FileUtils::getDirectory(QtUtils::toIndString(selected_filename))));

		try
		{
			size_t num_obs_serialised;
			const std::string xml = gui_client.serialiseAllObjectsInParcelToXML(num_obs_serialised);

			FileUtils::writeEntireFileTextMode(QtUtils::toStdString(selected_filename), xml);

			gui_client.showInfoNotification("Saved " + toString(num_obs_serialised) + " objects to '" + QtUtils::toIndString(selected_filename) + "'.");
		}
		catch(glare::Exception& e)
		{
			QtUtils::showErrorMessageDialog("Error saving objects to disk: " + e.what(), this);
		}
	}
}


class LoadObjectsFromXMLTask : public glare::Task, public PrintOutput
{
public:
	virtual void run(size_t /*thread_index*/) override
	{
		try
		{
			IndigoXMLDoc doc(xml_path);

			if(std::string(doc.getRootElement().name()) == "object")
			{
				WorldObjectRef ob = WorldObject::loadFromXMLElem(/*object file path=*/xml_path, /*convert rel paths to abs disk paths=*/false, doc.getRootElement());

				print("Creating object...");

				gui_client->createObjectLoadedFromXML(ob, *this);
			}
			else if(std::string(doc.getRootElement().name()) == "objects")
			{
				for(pugi::xml_node ob_node = doc.getRootElement().child("object"); ob_node && !stop_running; ob_node = ob_node.next_sibling("object"))
				{
					WorldObjectRef ob = WorldObject::loadFromXMLElem(/*object file path=*/xml_path, /*convert rel paths to abs disk paths=*/false, ob_node);

					print("Creating object...");

					try
					{
						gui_client->createObjectLoadedFromXML(ob, *this);
					}
					catch(glare::Exception& e)
					{
						// Catch exception and continue with next object.
						print("Error loading object from disk: " + e.what());
					}
				}
			}

			print("Done.");
		}
		catch(glare::Exception& e)
		{
			print("Error loading object(s) from disk: " + e.what());
			print("Done.");
		}
	}


	virtual void cancelTask() override
	{
		stop_running = 1;
	}

	void print(const std::string& s) override // Print a message and a newline character.
	{
		out_message_queue->enqueue(s);

		gui_client->msg_queue.enqueue(new LogMessage(s));
	}

	void printStr(const std::string& s) override // Print a message without a newline character.
	{}

	std::string xml_path;

	ThreadSafeQueue<std::string>* out_message_queue;
	GUIClient* gui_client;

	glare::AtomicInt stop_running;
};


void MainWindow::on_actionLoad_Objects_From_Disk_triggered()
{
	QString last_save_object_dir = settings->value("mainwindow/lastSaveObjectDir").toString();

	QFileDialog::Options options;
	QString selected_filter;
	const QString selected_filename = QFileDialog::getOpenFileName(this,
		tr("Select file..."),
		last_save_object_dir,
		tr("XML file (*.xml)"),
		&selected_filter,
		options
	);
	 
	if(!selected_filename.isEmpty())
	{
		settings->setValue("mainwindow/lastSaveObjectDir", QtUtils::toQString(FileUtils::getDirectory(QtUtils::toIndString(selected_filename))));

		// Do the work in another thread so we don't lock up the main thread.
		// The work will be done in a LoadObjectsFromXMLTask.
		ThreadSafeQueue<std::string> message_queue; // Messages will be emitted from LoadObjectsFromXMLTask, placed into this queue, and then read by the CreateObjectsDialog.
		
		{
			CreateObjectsDialog dialog(settings);
			dialog.msg_queue = &message_queue;

			Reference<LoadObjectsFromXMLTask> task = new LoadObjectsFromXMLTask();
			task->xml_path = QtUtils::toIndString(selected_filename);
			task->out_message_queue = &message_queue;
			task->gui_client = &gui_client;

			glare::TaskManager task_manager(1);
			task_manager.addTask(task);

			dialog.exec();

			task = NULL;
			task_manager.cancelAndWaitForTasksToComplete(); // Interrupt the LoadObjectsFromXMLTask if it hasn't completed already.
		}
	}
}


void MainWindow::on_actionDelete_All_Parcel_Objects_triggered()
{
	size_t num_obs_deleted;
	gui_client.deleteAllParcelObjects(num_obs_deleted);

	gui_client.showInfoNotification("Deleted " + toString(num_obs_deleted) + " objects.");
}


void MainWindow::on_actionEnter_Fullscreen_triggered()
{
	enterFullScreenMode();
}


void MainWindow::on_actionLanguage_English_triggered()
{
	// Language switching is handled explicitly via QAction::toggled connections in initialiseLanguageMenu().
}


void MainWindow::on_actionLanguage_Russian_triggered()
{
	// Language switching is handled explicitly via QAction::toggled connections in initialiseLanguageMenu().
}


void MainWindow::on_actionGo_Back_triggered()
{
	gui_client.goBack();
}


void MainWindow::openCurrentLocationInBrowserSlot()
{
	std::string host = gui_client.server_hostname.empty() ? std::string("vr.metasiberia.com") : gui_client.server_hostname;
	host = canonicaliseHostPortForMetasiberia(host);

	const bool use_http = hostIsLocalForWebMode(host);
	const std::string web_url = (use_http ? "http://" : "https://") + host + gui_client.getCurrentWebClientURLPath();
	const QUrl qurl = QUrl(QtUtils::toQString(web_url));
	if(!QDesktopServices::openUrl(qurl))
		showErrorNotification("Failed to open URL in browser: " + web_url);
}


void MainWindow::diagnosticsWidgetChanged()
{
	opengl_engine->setDrawWireFrames(ui->diagnosticsWidget->showWireframesCheckBox->isChecked());

	if(ui->diagnosticsWidget->showFrameTimeGraphsCheckBox->isChecked() && this->CPU_render_stats_widget.isNull())
	{
		opengl_engine->setProfilingEnabled(true);

		CPU_render_stats_widget = new RenderStatsWidget(opengl_engine, gui_client.gl_ui, /*widget index=*/0);
		GPU_render_stats_widget = new RenderStatsWidget(opengl_engine, gui_client.gl_ui, /*widget index=*/1);
	}
	else if(!ui->diagnosticsWidget->showFrameTimeGraphsCheckBox->isChecked() && CPU_render_stats_widget.nonNull())
	{
		opengl_engine->setProfilingEnabled(false);

		CPU_render_stats_widget = nullptr;
		GPU_render_stats_widget = nullptr;
	}

	gui_client.diagnosticsSettingsChanged();
}


void MainWindow::diagnosticsReloadTerrain()
{
	if(gui_client.terrain_system.nonNull())
	{
		gui_client.terrain_system->shutdown();
		gui_client.terrain_system = NULL;
	}

	// Just leave terrain_system null, will be reinitialised in MainWindow::updateGroundPlane().
}


void MainWindow::rebuildChatEmojiPopupContents()
{
	if(!chat_emoji_tab_widget)
		return;

	QString previous_tab_title;
	if(chat_emoji_tab_widget->currentIndex() >= 0)
		previous_tab_title = chat_emoji_tab_widget->tabText(chat_emoji_tab_widget->currentIndex());

	while(chat_emoji_tab_widget->count() > 0)
	{
		QWidget* page = chat_emoji_tab_widget->widget(0);
		chat_emoji_tab_widget->removeTab(0);
		delete page;
	}

	QFont emoji_picker_font = this->font();
#if defined(_WIN32)
	emoji_picker_font.setFamily("Segoe UI Emoji");
#endif
	emoji_picker_font.setPixelSize(54);

	const auto picker_categories = EmojiUtils::buildPickerCategories(gui_client.getRecentEmojiHistory());
	const QString empty_recent_text = QtUtils::toQString(std::string("\xD0\x97\xD0\xB4\xD0\xB5\xD1\x81\xD1\x8C \xD0\xBF\xD0\xBE\xD1\x8F\xD0\xB2\xD1\x8F\xD1\x82\xD1\x81\xD1\x8F \xD0\xBF\xD0\xBE\xD1\x81\xD0\xBB\xD0\xB5\xD0\xB4\xD0\xBD\xD0\xB8\xD0\xB5 emoji"));
	const QString emoji_button_style =
		"QToolButton { background: #ffffff; border: 1px solid #d7dde5; border-radius: 12px; padding: 0px; color: #111827; }"
		"QToolButton:hover { background: #eef4ff; border-color: #8ab2ff; }"
		"QToolButton:pressed { background: #dde9ff; }";

	for(size_t i=0; i<picker_categories.size(); ++i)
	{
		const EmojiUtils::EmojiPickerCategory& category = picker_categories[i];

		QScrollArea* scroll_area = new QScrollArea(chat_emoji_tab_widget);
		scroll_area->setWidgetResizable(true);
		scroll_area->setFrameShape(QFrame::NoFrame);
		scroll_area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

		QWidget* page = new QWidget(scroll_area);
		QGridLayout* grid_layout = new QGridLayout(page);
		grid_layout->setContentsMargins(12, 12, 18, 12);
		grid_layout->setHorizontalSpacing(12);
		grid_layout->setVerticalSpacing(12);
		grid_layout->setAlignment(Qt::AlignTop | Qt::AlignLeft);

		if(category.emojis.empty())
		{
			QLabel* empty_label = new QLabel(empty_recent_text, page);
			empty_label->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
			empty_label->setMinimumHeight(180);
			empty_label->setStyleSheet("QLabel { color: #6b7280; font-size: 16px; padding-top: 28px; }");
			grid_layout->addWidget(empty_label, 0, 0, 1, myMax(1, category.num_columns));
		}
		else
		{
			const int button_size_px = 88;
			for(size_t z=0; z<category.emojis.size(); ++z)
			{
				QToolButton* emoji_choice_button = new QToolButton(page);
				emoji_choice_button->setFont(emoji_picker_font);
				emoji_choice_button->setText(QtUtils::toQString(category.emojis[z]));
				emoji_choice_button->setFixedSize(QSize(button_size_px, button_size_px));
				emoji_choice_button->setToolButtonStyle(Qt::ToolButtonTextOnly);
				emoji_choice_button->setCursor(Qt::PointingHandCursor);
				emoji_choice_button->setToolTip(QtUtils::toQString(std::string(EmojiUtils::emojiDisplayName(category.emojis[z]))));
				emoji_choice_button->setStyleSheet(emoji_button_style);

				const std::string emoji = category.emojis[z];
				connect(emoji_choice_button, &QToolButton::clicked, this, [this, emoji]() {
					sendChatOrEmojiMessage(emoji);
					rebuildChatEmojiPopupContents();
				});

				grid_layout->addWidget(emoji_choice_button, (int)(z / category.num_columns), (int)(z % category.num_columns));
			}
		}

		const int min_page_width = 24 + myMax(1, category.num_columns) * 88 + myMax(0, category.num_columns - 1) * 12;
		page->setMinimumWidth(min_page_width);
		scroll_area->setWidget(page);
		chat_emoji_tab_widget->addTab(scroll_area, QtUtils::toQString(category.title));
	}

	int current_tab_index = 0;
	if(!picker_categories.empty() && picker_categories[0].emojis.empty() && picker_categories.size() > 1)
		current_tab_index = 1;

	if(!previous_tab_title.isEmpty())
	{
		for(int i=0; i<chat_emoji_tab_widget->count(); ++i)
		{
			if(chat_emoji_tab_widget->tabText(i) == previous_tab_title)
			{
				current_tab_index = i;
				break;
			}
		}
	}

	if(chat_emoji_tab_widget->count() > 0)
		chat_emoji_tab_widget->setCurrentIndex(myClamp(current_tab_index, 0, chat_emoji_tab_widget->count() - 1));
}


void MainWindow::sendChatOrEmojiMessage(const std::string& message)
{
	if(message.empty())
		return;

	const QString safe_message_q = makeNetworkSafeChatMessage(QtUtils::toQString(message));
	const std::string safe_message = QtUtils::toStdString(safe_message_q);
	if(safe_message.empty())
		return;

	if(chat_showing_private_messages)
	{
		const std::string recipient_name = selectedChatRecipientName();
		const UID recipient_avatar_uid = selectedChatRecipientUID();
		if(recipient_name.empty())
		{
			showErrorNotification("Выберите игрока слева для личного сообщения.");
			return;
		}

		if(chat_network_private_messages_enabled)
			gui_client.sendPrivateChatMessage(recipient_name, recipient_avatar_uid, safe_message);
		else
		{
			const QString recipient = QtUtils::toQString(chat_private_recipient_name.empty() ? recipient_name : chat_private_recipient_name);
			const QString html =
				"<p><span style=\"color:#7c3aed; font-weight:600;\">" +
				tr("Лично для %1").arg(recipient).toHtmlEscaped() +
				"</span>: " + safe_message_q.toHtmlEscaped() + "</p>";
			appendLocalChatMessage(html, true);
		}
		return;
	}

	if(EmojiUtils::isSupportedEmoji(safe_message))
		gui_client.sendEmojiChatMessage(safe_message);
	else
		gui_client.sendChatMessage(safe_message);
}


void MainWindow::toggleChatEmojiPopup()
{
	if(!chat_emoji_popup || !ui || !ui->chatEmojiButton)
		return;

	if(chat_emoji_popup->isVisible())
	{
		chat_emoji_popup->hide();
		return;
	}

	rebuildChatEmojiPopupContents();
	chat_emoji_popup->resize(chat_emoji_popup->minimumSize());

	QWidget* anchor_widget = qobject_cast<QWidget*>(sender());
	if(!anchor_widget)
		anchor_widget = ui->chatEmojiButton;

	QPoint popup_pos = anchor_widget->mapToGlobal(
		QPoint(
			anchor_widget->width() - chat_emoji_popup->width(),
			-chat_emoji_popup->height() - 4
		)
	);

	QScreen* target_screen = QGuiApplication::screenAt(anchor_widget->mapToGlobal(anchor_widget->rect().center()));
	if(!target_screen)
		target_screen = QGuiApplication::primaryScreen();

	if(target_screen)
	{
		const QRect available = target_screen->availableGeometry();
		popup_pos.setX(qBound(available.left(), popup_pos.x(), available.right() - chat_emoji_popup->width()));
		popup_pos.setY(qBound(available.top(), popup_pos.y(), available.bottom() - chat_emoji_popup->height()));
	}

	chat_emoji_popup->move(popup_pos);
	chat_emoji_popup->show();
	chat_emoji_popup->raise();
	chat_emoji_popup->activateWindow();
}


void MainWindow::sendChatMessageSlot()
{
	//conPrint("MainWindow::sendChatMessageSlot()");

	const std::string message = stripHeadAndTailWhitespace(QtUtils::toIndString(ui->chatMessageLineEdit->text()));
	if(message.empty())
		return;

	if(chat_showing_private_messages && selectedChatRecipientName().empty())
	{
		showErrorNotification("Выберите игрока слева для личного сообщения.");
		ui->chatMessageLineEdit->setFocus();
		return;
	}

	sendChatOrEmojiMessage(message);

	ui->chatMessageLineEdit->clear();
}


void MainWindow::sendLightmapNeededFlagsSlot()
{
	gui_client.sendLightmapNeededFlagsSlot();
}


// Object transform has been edited, e.g. by the object editor.
void MainWindow::objectTransformEditedSlot()
{
	try
	{
		gui_client.objectTransformEdited();
	}
	catch(glare::Exception& e)
	{
		QMessageBox msgBox;
		msgBox.setWindowTitle("Error");
		msgBox.setText(QtUtils::toQString(e.what()));
		msgBox.exec();
	}
}


// Object property (that is not a transform property) has been edited, e.g. by the object editor.
void MainWindow::objectEditedSlot()
{
	try
	{
		gui_client.objectEdited();
	}
	catch(glare::Exception& e)
	{
		QMessageBox msgBox;
		msgBox.setWindowTitle("Error");
		msgBox.setText(QtUtils::toQString(e.what()));
		msgBox.exec();
	}
}


void MainWindow::scriptChangedFromEditorSlot()
{
	if(gui_client.selected_ob)
		BitUtils::setBit(gui_client.selected_ob->changed_flags, WorldObject::SCRIPT_CHANGED);

	objectEditedSlot();
}


void MainWindow::particleBurstNowSlot()
{
	objectEditedSlot();
	gui_client.triggerSelectedParticleEmitterBurst();
}


void MainWindow::particleClearParticlesSlot()
{
	gui_client.clearSelectedParticleEmitterParticles();
}


// Parcel has been edited, e.g. by the parcel editor.
void MainWindow::parcelEditedSlot()
{
	if(gui_client.selected_parcel.nonNull())
	{
		ui->parcelEditor->toParcel(*gui_client.selected_parcel);

		Lock lock(gui_client.world_state->mutex);
		//this->selected_parcel->from_local_other_dirty = true;
		gui_client.world_state->dirty_from_local_parcels.insert(gui_client.selected_parcel);
	}
}


// World settings have been changed in local UI.
void MainWindow::worldSettingsChangedSlot()
{
	WorldSettings new_world_settings;
	this->ui->worldSettingsWidget->toWorldSettings(new_world_settings);

	gui_client.worldSettingsChangedFromUI(new_world_settings);
}


// An environment setting has been edited in the environment options dock widget
void MainWindow::environmentSettingChangedSlot()
{
	if(ui->glWidget->opengl_engine.nonNull())
	{
		const float theta = myClamp(::degreeToRad((float)ui->environmentOptionsWidget->sunThetaRealControl->value()), 0.01f, Maths::pi<float>() - 0.01f);
		const float phi   = ::degreeToRad((float)ui->environmentOptionsWidget->sunPhiRealControl->value());
		const Vec4f sundir = GeometrySampling::dirForSphericalCoords(phi, theta);

		ui->glWidget->opengl_engine->setSunDir(sundir);

		// Keep aurora rendering state in sync with the checkbox.
		if(ui->glWidget->opengl_engine->getCurrentScene())
		{
			const bool northern_lights_enabled = ui->environmentOptionsWidget->getNorthernLightsEnabled();
			ui->glWidget->opengl_engine->getCurrentScene()->draw_aurora = northern_lights_enabled;
		}
	}
}


void MainWindow::bakeObjectLightmapSlot()
{
	if(gui_client.selected_ob.nonNull())
	{
		// Don't bake lightmaps for objects with sketal animation for now (creating second UV set removes joints and weights).
		const bool has_skeletal_anim = gui_client.selected_ob->opengl_engine_ob.nonNull() && gui_client.selected_ob->opengl_engine_ob->mesh_data.nonNull() &&
			!gui_client.selected_ob->opengl_engine_ob->mesh_data->animation_data.animations.empty();

		if(has_skeletal_anim)
		{
			showErrorNotification("You cannot currently bake lightmaps for objects with skeletal animation.");
		}
		else
		{
			gui_client.selected_ob->lightmap_baking = true;

			BitUtils::setBit(gui_client.selected_ob->flags, WorldObject::LIGHTMAP_NEEDS_COMPUTING_FLAG);
			gui_client.objs_with_lightmap_rebuild_needed.insert(gui_client.selected_ob);
			lightmap_flag_timer->start(/*msec=*/20); // Trigger sending update-lightmap update flag message later.
		}
	}
}


void MainWindow::bakeObjectLightmapHighQualSlot()
{
	if(gui_client.selected_ob.nonNull())
	{
		// Don't bake lightmaps for objects with sketal animation for now (creating second UV set removes joints and weights).
		const bool has_skeletal_anim = gui_client.selected_ob->opengl_engine_ob.nonNull() && gui_client.selected_ob->opengl_engine_ob->mesh_data.nonNull() &&
			!gui_client.selected_ob->opengl_engine_ob->mesh_data->animation_data.animations.empty();

		if(has_skeletal_anim)
		{
			showErrorNotification("You cannot currently bake lightmaps for objects with skeletal animation.");
		}
		else
		{
			gui_client.selected_ob->lightmap_baking = true;

			BitUtils::setBit(gui_client.selected_ob->flags, WorldObject::HIGH_QUAL_LIGHTMAP_NEEDS_COMPUTING_FLAG);
			gui_client.objs_with_lightmap_rebuild_needed.insert(gui_client.selected_ob);
			lightmap_flag_timer->start(/*msec=*/20); // Trigger sending update-lightmap update flag message later.
		}
	}
}


void MainWindow::removeLightmapSignalSlot()
{
	if(gui_client.selected_ob.nonNull())
	{
		gui_client.selected_ob->lightmap_url.clear();

		objectEditedSlot();
	}
}


void MainWindow::posAndRot3DControlsToggledSlot()
{
	const bool enabled = posAndRot3DControlsEnabled();
	gui_client.posAndRot3DControlsToggled(enabled);
	
	if(active_editor_kind == ActiveEditor_Scientific)
		settings->setValue("scientificObjectEditor/show3DControls", enabled);
	else if(active_editor_kind == ActiveEditor_Cultural)
		settings->setValue("culturalObjectEditor/show3DControls", enabled);
	else if(active_editor_kind == ActiveEditor_Tree)
		settings->setValue("treeEditor/show3DControls", enabled);
	else if(active_editor_kind == ActiveEditor_Voxel)
		settings->setValue("voxelEditor/show3DControls", enabled);
	else
		settings->setValue("objectEditor/show3DControlsCheckBoxChecked", enabled);
}


void MainWindow::materialSelectedInBrowser(const std::string& path)
{
	if(gui_client.selected_ob.nonNull())
	{
		const bool have_edit_permissions = gui_client.objectModificationAllowedWithMsg(*gui_client.selected_ob, "edit");
		if(have_edit_permissions)
			this->ui->objectEditor->materialSelectedInBrowser(path);
		else
			showErrorNotification("You do not have write permissions for this object, so you can't apply a material to it.");
	}
}


void MainWindow::URLChangedSlot()
{
	const std::string URL = this->url_widget->getURL();
	visitSubURL(URL);
}


void MainWindow::visitSubURL(const std::string& URL) // Visit a substrata 'sub://' URL.  Checks hostname and only reconnects if the hostname is different from the current one.
{
	try
	{
		std::string URL_to_visit = URL;
		std::string notification;
		std::string resolve_error;
		std::string resolved_URL;
		if(resolveMetasiberiaMapAddressInput(URL, gui_client, resolved_URL, notification, resolve_error))
		{
			if(!resolve_error.empty())
			{
				showErrorNotification(resolve_error);
				return;
			}

			if(!resolved_URL.empty())
			{
				URL_to_visit = resolved_URL;
				if(url_widget)
					url_widget->setURL(URL_to_visit);
			}
		}

		gui_client.visitSubURL(canonicaliseMetasiberiaSubURLHost(URL_to_visit));
		if(!notification.empty())
			showInfoNotification(notification);
	}
	catch(glare::Exception& e) // Handle URL parse failure
	{
		conPrint(e.what());
		QMessageBox msgBox;
		msgBox.setText(QtUtils::toQString(e.what()));
		msgBox.exec();
	}
}


static MouseButton fromQtMouseButton(Qt::MouseButton b)
{
	if(b == Qt::MouseButton::LeftButton)
		return MouseButton::Left;
	else if(b == Qt::MouseButton::RightButton)
		return MouseButton::Right;
	else if(b == Qt::MouseButton::MiddleButton)
		return MouseButton::Middle;
	else if(b == Qt::MouseButton::BackButton)
		return MouseButton::Back;
	else if(b == Qt::MouseButton::ForwardButton)
		return MouseButton::Forward;
	else
		return MouseButton::None;
}


static uint32 fromQTMouseButtons(Qt::MouseButtons b)
{
	uint32 res = 0;
	if(BitUtils::isBitSet((uint32)b, (uint32)Qt::MouseButton::LeftButton))   res |= MouseButton::Left;
	if(BitUtils::isBitSet((uint32)b, (uint32)Qt::MouseButton::MiddleButton)) res |= MouseButton::Middle;
	if(BitUtils::isBitSet((uint32)b, (uint32)Qt::MouseButton::RightButton))  res |= MouseButton::Right;
	return res;
}


static uint32 fromQtModifiers(Qt::KeyboardModifiers modifiers)
{
	const bool ctrl_key_down = (modifiers & Qt::ControlModifier) != 0;
	const bool alt_key_down  = (modifiers & Qt::AltModifier)     != 0;
	const bool shift_down    = (modifiers & Qt::ShiftModifier)   != 0;

	return 
		(ctrl_key_down ? Modifiers::Ctrl  : 0) |
		(alt_key_down  ? Modifiers::Alt   : 0) |
		(shift_down    ? Modifiers::Shift : 0);
}


void MainWindow::glWidgetMousePressed(QMouseEvent* e)
{
	if(!opengl_engine)
		return;

	if(active_editor_kind == ActiveEditor_Scientific && scientific_object_editor && gui_client.selected_ob.nonNull() && gui_client.selected_ob->physics_object.nonNull() &&
		(e->button() == Qt::LeftButton || e->button() == Qt::RightButton))
	{
		const Vec2i pixel_pos = Vec2i(e->pos().x(), e->pos().y()) * ui->glWidget->devicePixelRatio();
		const Vec4f origin_ws = gui_client.cam_controller.getPosition().toVec4fPoint();
		const Vec4f dir_ws = gui_client.getDirForPixelTrace(pixel_pos.x, pixel_pos.y);
		const Matrix4f world_to_ob = gui_client.selected_ob->physics_object->getWorldToObMatrix();
		if(scientific_object_editor->handleSceneRay(world_to_ob * origin_ws, world_to_ob * dir_ws, e->button() == Qt::RightButton, (e->modifiers() & Qt::ControlModifier) != 0, e->globalPos()))
		{
			setCamRotationOnMouseDragEnabled(false);
			e->accept();
			return;
		}
	}

	const Vec2f widget_pos((float)e->pos().x(), (float)e->pos().y());

	MouseEvent mouse_event;
	mouse_event.cursor_pos = Vec2i(e->pos().x(), e->pos().y()) * ui->glWidget->devicePixelRatio(); // Use devicePixelRatio to convert from logical to physical pixel coords.
	mouse_event.gl_coords = GLCoordsForGLWidgetPos(this, widget_pos);
	mouse_event.button = fromQtMouseButton(e->button());
	mouse_event.modifiers = fromQtModifiers(e->modifiers());

	gui_client.mousePressed(mouse_event);

	if(mouse_event.accepted)
		e->accept();
}


void MainWindow::glWidgetMouseReleased(QMouseEvent* e)
{
	if(!opengl_engine)
		return;

	const Vec2f widget_pos((float)e->pos().x(), (float)e->pos().y());
	const Vec2f gl_coords = GLCoordsForGLWidgetPos(this, widget_pos);

	MouseEvent mouse_event;
	mouse_event.cursor_pos = Vec2i(e->pos().x(), e->pos().y()) * ui->glWidget->devicePixelRatio(); // Use devicePixelRatio to convert from logical to physical pixel coords.
	mouse_event.gl_coords = gl_coords;
	mouse_event.button = fromQtMouseButton(e->button());
	mouse_event.modifiers = fromQtModifiers(e->modifiers());

	gui_client.mouseReleased(mouse_event);

	if(mouse_event.accepted)
		e->accept();
}


void MainWindow::setUIForSelectedObject() // Enable/disable delete object action etc..
{
	const bool have_selected_ob = gui_client.selected_ob.nonNull();
	this->ui->actionCloneObject->setEnabled(have_selected_ob);
	this->ui->actionDeleteObject->setEnabled(have_selected_ob);
}


void MainWindow::startObEditorTimerIfNotActive()
{
	// Set a timer to call updateObjectEditorObTransformSlot() later. Not calling this every frame avoids stutters with webviews playing back videos interacting with Qt updating spinboxes.
	if(!update_ob_editor_transform_timer->isActive())
		update_ob_editor_transform_timer->start(/*msec=*/50);
}


void MainWindow::startLightmapFlagTimer()
{
	lightmap_flag_timer->start(/*msec=*/20); // Trigger sending update-lightmap update flag message later.
}


bool MainWindow::getVoxelEditorToolState(VoxelToolType& tool_out, VoxelToolSettings& settings_out) const
{
	if(active_editor_kind != ActiveEditor_Voxel || !voxel_editor_panel || !voxel_editor_panel->sceneToolsEnabled())
		return false;
	tool_out = voxel_editor_panel->currentTool();
	settings_out = voxel_editor_panel->toolSettings();
	return true;
}


void MainWindow::voxelEditorMaterialPicked(const int material_index)
{
	if(active_editor_kind == ActiveEditor_Voxel && voxel_editor_panel)
		voxel_editor_panel->selectMaterialIndex(material_index);
}


void MainWindow::voxelEditorObjectDataChanged(const WorldObject& ob)
{
	if(active_editor_kind == ActiveEditor_Voxel && voxel_editor_panel)
		voxel_editor_panel->notifyVoxelDataChanged(ob);
}


void MainWindow::gearInventoryUpdated()
{
	// Avatar and gear meshes can finish loading in a burst.  Rebuilding every
	// card and the preview once per callback stalls the Qt render thread, so
	// coalesce all callbacks already queued for this event-loop iteration.
	if(!gear_inventory_panel || !gear_inventory_panel->isVisible() || gear_inventory_refresh_pending)
		return;

	gear_inventory_refresh_pending = true;
	QTimer::singleShot(0, gear_inventory_panel, [this]() {
		gear_inventory_refresh_pending = false;
		if(gear_inventory_panel && gear_inventory_panel->isVisible())
			gear_inventory_panel->refreshFromClient();
	});
}


void MainWindow::showAvatarSettings()
{
	on_actionAvatarSettings_triggered();
}


void MainWindow::setCamRotationOnMouseDragEnabled(bool enabled)
{
	ui->glWidget->setCamRotationOnMouseDragEnabled(enabled);
}


bool MainWindow::isCursorHidden()
{
	return ui->glWidget->isCursorHidden();
}


void MainWindow::hideCursor()
{
	ui->glWidget->hideCursor();
}


void MainWindow::setKeyboardCameraMoveEnabled(bool enabled)
{
	ui->glWidget->setKeyboardCameraMoveEnabled(enabled);
}


bool MainWindow::isKeyboardCameraMoveEnabled()
{
	return ui->glWidget->isKeyboardCameraMoveEnabled();
}


bool MainWindow::hasFocus()
{
	return ui->glWidget->hasFocus();
}


void MainWindow::setHelpInfoLabelToDefaultText()
{
	this->ui->helpInfoLabel->setText(defaultHelpInfoMessageText());
}


void MainWindow::setHelpInfoLabel(const std::string& text)
{
	this->ui->helpInfoLabel->setText(QtUtils::toQString(text));
}


void MainWindow::showParcelEditor()
{
	ui->objectEditor->hide();
	if(gear_inventory_panel)
		gear_inventory_panel->hide();
	if(scientific_object_editor)
		scientific_object_editor->hide();
	if(cultural_object_editor)
		cultural_object_editor->hide();
	if(tree_editor_panel)
		tree_editor_panel->hide();
	if(voxel_editor_panel)
		voxel_editor_panel->hide();
	ui->parcelEditor->show();
}


void MainWindow::setParcelEditorForParcel(const Parcel& parcel)
{
	ui->parcelEditor->setFromParcel(parcel);
}


void MainWindow::setParcelEditorEnabled(bool b)
{
	ui->parcelEditor->setEnabled(b);
}


void MainWindow::setParcelEditorPermissions(bool can_edit_basic_fields, bool can_edit_owner_and_geometry, bool can_edit_member_lists)
{
	ui->parcelEditor->setEditingPermissions(can_edit_basic_fields, can_edit_owner_and_geometry, can_edit_member_lists);
}


void MainWindow::enableThirdPersonCamera()
{
	ui->actionThird_Person_Camera->setChecked(true);
	ui->actionThird_Person_Camera->triggered(true); // Need to manually trigger the action.
}


void MainWindow::toggleFlyMode()
{
	ui->actionFly_Mode->toggle();
	ui->actionFly_Mode->triggered(ui->actionFly_Mode->isChecked()); // Need to manually emit triggered signal, toggle doesn't do it.
}


void MainWindow::toggleThirdPersonCameraMode()
{
	ui->actionThird_Person_Camera->toggle();
	ui->actionThird_Person_Camera->triggered(ui->actionThird_Person_Camera->isChecked()); // Need to manually emit triggered signal, toggle doesn't do it.
}


void MainWindow::enableThirdPersonCameraIfNotAlreadyEnabled()
{
	if(!ui->actionThird_Person_Camera->isChecked())
		ui->actionThird_Person_Camera->trigger();
}


void MainWindow::enableFirstPersonCamera()
{
	ui->actionThird_Person_Camera->setChecked(false);
	ui->actionThird_Person_Camera->triggered(false); // Need to manually trigger the action.
}


void MainWindow::openURL(const std::string& URL)
{
	QDesktopServices::openUrl(QtUtils::toQString(URL));
}


Vec2i MainWindow::getMouseCursorWidgetPos()
{
	const QPoint mouse_point = ui->glWidget->mapFromGlobal(QCursor::pos());

	return Vec2i(mouse_point.x(), mouse_point.y()) * ui->glWidget->devicePixelRatio(); // Use devicePixelRatio to convert from logical to physical pixel coords.
}


std::string MainWindow::getUsernameForDomain(const std::string& domain)
{
	return credential_manager.getUsernameForDomain(domain);
}


std::string MainWindow::getDecryptedPasswordForDomain(const std::string& domain)
{
	return credential_manager.getDecryptedPasswordForDomain(domain);
}


bool MainWindow::inScreenshotTakingMode()
{
	return !screenshot_output_path.empty();
}


void MainWindow::takeScreenshot()
{
	on_actionTake_Screenshot_triggered();
}


void MainWindow::showScreenshots()
{
	on_actionShow_Screenshot_Folder_triggered();
}


void MainWindow::doObjectSelectionTraceForMouseEvent(QMouseEvent* e)
{
	const Vec2f widget_pos((float)e->pos().x(), (float)e->pos().y());
	const Vec2f gl_coords = GLCoordsForGLWidgetPos(this, widget_pos);

	MouseEvent mouse_event;
	mouse_event.cursor_pos = Vec2i(e->pos().x(), e->pos().y()) * ui->glWidget->devicePixelRatio(); // Use devicePixelRatio to convert from logical to physical pixel coords.
	mouse_event.gl_coords = gl_coords;
	mouse_event.button = fromQtMouseButton(e->button());
	mouse_event.modifiers = fromQtModifiers(e->modifiers());

	gui_client.doObjectSelectionTraceForMouseEvent(mouse_event);
}


void MainWindow::glWidgetMouseDoubleClicked(QMouseEvent* e)
{
	//conPrint("MainWindow::glWidgetMouseDoubleClicked()");

	const Vec2f widget_pos((float)e->pos().x(), (float)e->pos().y());
	const Vec2f gl_coords = GLCoordsForGLWidgetPos(this, widget_pos);

	MouseEvent mouse_event;
	mouse_event.cursor_pos = Vec2i(e->pos().x(), e->pos().y()) * ui->glWidget->devicePixelRatio(); // Use devicePixelRatio to convert from logical to physical pixel coords.
	mouse_event.gl_coords = gl_coords;
	mouse_event.button = fromQtMouseButton(e->button());
	mouse_event.modifiers = fromQtModifiers(e->modifiers());

	gui_client.mouseDoubleClicked(mouse_event);
}


void MainWindow::glWidgetMouseMoved(QMouseEvent* e)
{
	if(ui->glWidget->opengl_engine.isNull() || !ui->glWidget->opengl_engine->initSucceeded())
		return;

	const Vec2f widget_pos((float)e->pos().x(), (float)e->pos().y());
	const Vec2f gl_coords = GLCoordsForGLWidgetPos(this, widget_pos);

	MouseEvent mouse_event;
	mouse_event.cursor_pos = Vec2i(e->pos().x(), e->pos().y()) * ui->glWidget->devicePixelRatio(); // Use devicePixelRatio to convert from logical to physical pixel coords.
	mouse_event.gl_coords = gl_coords;
	mouse_event.modifiers = fromQtModifiers(e->modifiers());
	mouse_event.button_state = fromQTMouseButtons(e->buttons());

	gui_client.mouseMoved(mouse_event);

	if(mouse_event.accepted)
	{
		e->accept();
		return;
	}
}


void MainWindow::updateObjectEditorObTransformSlot()
{
	if(gui_client.selected_ob.nonNull())
	{
		if(active_editor_kind == ActiveEditor_Scientific && scientific_object_editor)
			scientific_object_editor->setTransformFromObject(*gui_client.selected_ob);
		else if(active_editor_kind == ActiveEditor_Cultural && cultural_object_editor)
			cultural_object_editor->setTransformFromObject(*gui_client.selected_ob);
		else if(active_editor_kind == ActiveEditor_Tree && tree_editor_panel)
			ui->objectEditor->setTransformFromObject(*gui_client.selected_ob);
		else
			ui->objectEditor->setTransformFromObject(*gui_client.selected_ob);
	}
}


void setKeyEventFromQt(const QKeyEvent* e, KeyEvent& event_out)
{
	KeyEvent& ev = event_out;
	
	ev.key = Key::Key_None;
	ev.native_virtual_key = e->nativeVirtualKey();
	//ev.text = QtUtils::toStdString(e->text());
	ev.modifiers = fromQtModifiers(e->modifiers());

	const int qt_key = e->key();

	// Just check for keys we use currently.  TODO: all keys
	if(qt_key == Qt::Key::Key_Escape)
		ev.key = Key::Key_Escape;
	else if(qt_key == Qt::Key::Key_Backspace)
		ev.key = Key::Key_Backspace;
	else if(qt_key == Qt::Key::Key_Delete)
		ev.key = Key::Key_Delete;
	else if(qt_key == Qt::Key::Key_Space)
		ev.key = Key::Key_Space;
	else if(qt_key == Qt::Key::Key_Enter)
		ev.key = Key::Key_Enter;
	else if(qt_key == Qt::Key::Key_Return)
		ev.key = Key::Key_Return;

	else if(qt_key == Qt::Key::Key_BracketLeft)
		ev.key = Key::Key_LeftBracket;
	else if(qt_key == Qt::Key::Key_BracketRight)
		ev.key = Key::Key_RightBracket;

	else if(qt_key == Qt::Key::Key_PageUp)
		ev.key = Key::Key_PageUp;
	else if(qt_key == Qt::Key::Key_PageDown)
		ev.key = Key::Key_PageDown;

	else if(qt_key == Qt::Key::Key_Home)
		ev.key = Key::Key_Home;
	else if(qt_key == Qt::Key::Key_End)
		ev.key = Key::Key_End;

	else if(qt_key == Qt::Key::Key_Equal)
		ev.key = Key::Key_Equals;
	else if(qt_key == Qt::Key::Key_Minus)
		ev.key = Key::Key_Minus;
	else if(qt_key == Qt::Key::Key_Plus)
		ev.key = Key::Key_Plus;

	else if(qt_key == Qt::Key::Key_Left)
		ev.key = Key::Key_Left;
	else if(qt_key == Qt::Key::Key_Right)
		ev.key = Key::Key_Right;
	else if(qt_key == Qt::Key::Key_Up)
		ev.key = Key::Key_Up;
	else if(qt_key == Qt::Key::Key_Down)
		ev.key = Key::Key_Down;

	// A-Z
	if(qt_key >= Qt::Key_A && qt_key <= Qt::Key_Z)
		ev.key = (Key)((int)Key::Key_A + ((int)qt_key - (int)Qt::Key_A));

	// 0-9
	if(qt_key >= Qt::Key_0 && qt_key <= Qt::Key_9)
		ev.key = (Key)((int)Key::Key_0 + ((int)qt_key - (int)Qt::Key_0));

	// F1-F12
	if(qt_key >= Qt::Key_F1 && qt_key <= Qt::Key_F12)
		ev.key = (Key)((int)Key::Key_F1 + ((int)qt_key - (int)Qt::Key_F1));
}


void MainWindow::enterFullScreenMode()
{
	// Save window state (saves which dock widgets are open etc.)
	this->pre_fullscreen_window_state = this->saveState();

	//this->setWindowFlags(Qt::Window);
	this->showFullScreen();

	this->ui->menubar->hide();
	this->ui->toolBar->hide();
	this->ui->statusbar->hide();

	// Hide all dock widgets
	this->ui->helpInfoDockWidget->hide();
	this->ui->chatDockWidget->hide();
	if(this->map_dock_widget)
		this->map_dock_widget->hide();
	this->ui->editorDockWidget->hide();
	this->ui->materialBrowserDockWidget->hide();
	this->ui->environmentDockWidget->hide();
	this->ui->worldSettingsWidget->hide();
	this->ui->diagnosticsDockWidget->hide();
	this->ui->worldSettingsDockWidget->hide();

	gui_client.showInfoNotification("Full-screen mode entered.  Press ALT + ENTER to exit it.");
}

void MainWindow::exitFromFullScreenMode()
{
	this->showNormal();
	this->ui->menubar->show();
	this->ui->statusbar->show();
	this->restoreState(this->pre_fullscreen_window_state);
}


void MainWindow::glWidgetKeyPressed(QKeyEvent* e)
{
	// If ALT+ENTER is pressed, enter or exit fullscreen mode.
	if((e->key() == Qt::Key_Return) && ((e->modifiers() & Qt::AltModifier) != 0))
	{
		if(this->isFullScreen())
			exitFromFullScreenMode();
		else
			enterFullScreenMode();
		return;
	}

	if((e->key() == Qt::Key_Home) || (e->key() == Qt::Key_End))
	{
		if(gui_client.requestXRRecenter())
			gui_client.showInfoNotification("XR recentered. Keep looking straight ahead in the headset.");
		else
			gui_client.showInfoNotification("XR recenter is unavailable because there is no active XR session.");
		return;
	}

	if(active_editor_kind == ActiveEditor_Voxel && voxel_editor_panel && voxel_editor_panel->handleShortcut(e))
	{
		e->accept();
		return;
	}

#if BUILD_TESTS
	if(e->key() == Qt::Key_F6)
	{
		ui->glWidget->opengl_engine->show_ssao = !ui->glWidget->opengl_engine->show_ssao;
		conPrint("Toggling show_ssao to " + boolToString(ui->glWidget->opengl_engine->show_ssao));
	}
	if(e->key() == Qt::Key_F7)
	{
		ui->glWidget->opengl_engine->toggleShowTexDebug(0);
	}
	if(e->key() == Qt::Key_F8)
	{
		ui->glWidget->opengl_engine->toggleShowTexDebug(1);
	}
	if(e->key() == Qt::Key_F9)
	{
		ui->glWidget->opengl_engine->toggleShowTexDebug(2);
	}
	if(e->key() == Qt::Key_F10)
	{
		ui->glWidget->opengl_engine->toggleShowTexDebug(3);
	}
	if(e->key() == Qt::Key_F11)
	{
		ui->glWidget->opengl_engine->toggleShowTexDebug(4);
	}
	if(e->key() == Qt::Key_F12)
	{
		ui->glWidget->opengl_engine->toggleShowTexDebug(5);
	}
#endif

	KeyEvent key_event;
	setKeyEventFromQt(e, key_event);

	if(!e->text().isEmpty() && 
		key_event.key != Key::Key_Backspace && 
		key_event.key != Key::Key_Delete &&
		key_event.key != Key::Key_Left &&
		key_event.key != Key::Key_Right &&
		key_event.key != Key::Key_Return &&
		key_event.key != Key::Key_Enter &&
		key_event.key != Key::Key_Escape
		)
	{
		TextInputEvent text_input_event;
		text_input_event.text = QtUtils::toStdString(e->text());
		gui_client.handleTextInputEvent(text_input_event);
		if(text_input_event.accepted)
			return;
	}

	gui_client.keyPressed(key_event);
}


void MainWindow::glWidgetkeyReleased(QKeyEvent* e)
{
	KeyEvent key_event;
	setKeyEventFromQt(e, key_event);

	gui_client.keyReleased(key_event);
}


void MainWindow::glWidgetFocusOut()
{
	gui_client.focusOut();
}


void MainWindow::glWidgetMouseWheelEvent(QWheelEvent* e)
{
	const Vec2f widget_pos((float)e->pos().x(), (float)e->pos().y());
	const Vec2f gl_coords = GLCoordsForGLWidgetPos(this, widget_pos);

	MouseWheelEvent mouse_event;
	mouse_event.cursor_pos = Vec2i(e->pos().x(), e->pos().y()) * ui->glWidget->devicePixelRatio(); // Use devicePixelRatio to convert from logical to physical pixel coords.
	mouse_event.gl_coords = gl_coords;
	mouse_event.angle_delta = Vec2f((float)e->angleDelta().x() / 8.f, (float)e->angleDelta().y() / 8.f); // angleDelta() returns "the relative amount that the wheel was rotated, in eighths of a degree".
	mouse_event.modifiers = fromQtModifiers(e->modifiers());

	gui_client.onMouseWheelEvent(mouse_event);

	if(mouse_event.accepted)
	{
		e->accept();
		return;
	}
}


void MainWindow::gamepadButtonXChanged(bool pressed)
{
	gui_client.gamepadButtonXChanged(pressed);
}


void MainWindow::gamepadButtonAChanged(bool pressed)
{
	gui_client.gamepadButtonAChanged(pressed);
}


void MainWindow::glWidgetViewportResized(int w, int h)
{
	gui_client.viewportResized(w, h);
}


void MainWindow::setGLWidgetContextAsCurrent()
{
	this->ui->glWidget->makeCurrent();
}


bool MainWindow::connectedToUsersWorldOrGodUser()
{
	return gui_client.connectedToUsersWorldOrGodUser();
}


Vec2i MainWindow::getGlWidgetPosInGlobalSpace()
{
	const QPoint p = this->ui->glWidget->mapToGlobal(this->ui->glWidget->pos());
	return Vec2i(p.x(), p.y());
}


// See https://doc.qt.io/qt-5/qdesktopservices.html, this slot should be called when the user clicks on a sub:// link somewhere in the system.
void MainWindow::handleURL(const QUrl &url)
{
	try
	{
		URLParseResults parse_results = URLParser::parseURL(QtUtils::toStdString(url.toString()));
		
		gui_client.connectToServer(parse_results);
	}
	catch(glare::Exception& e)
	{
		QtUtils::showErrorMessageDialog("Error parsing URL: " + e.what(), this);
	}
}


void MainWindow::openServerScriptLogSlot()
{
	const std::string hostname = gui_client.server_hostname.empty() ? "vr.metasiberia.com" : gui_client.server_hostname;

	QDesktopServices::openUrl(QtUtils::toQString("https://" + hostname + "/script_log"));
}


void MainWindow::webViewDataLinkHovered(const std::string& url)
{
	if(url.empty())
	{
		ui->glWidget->setCursorIfNotHidden(Qt::ArrowCursor);
	}
	else
	{
		ui->glWidget->setCursorIfNotHidden(Qt::PointingHandCursor);
	}
}


#if 0 // Use SDL for gamepad input:

// game_controller
bool MainWindow::gamepadAttached()
{
	return game_controller != nullptr;
}
static float removeDeadZone(float x)
{
	if(std::fabs(x) < (8000.f / 32768.f))
		return 0.f;
	else
		return x;
}

float MainWindow::gamepadButtonL2()
{
	const Sint16 val = SDL_GameControllerGetAxis(game_controller, /*axis=*/SDL_CONTROLLER_AXIS_TRIGGERLEFT);
	return (float)val / SDL_JOYSTICK_AXIS_MAX;
}

float MainWindow::gamepadButtonR2()
{
	const Sint16 val = SDL_GameControllerGetAxis(game_controller, /*axis=*/SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
	return (float)val / SDL_JOYSTICK_AXIS_MAX;
}

// NOTE: seems to be an issue in SDL that the left axis maps to the left keypad instead of left stick on a Logitech F310 gamepad.
float MainWindow::gamepadAxisLeftX()
{
	const Sint16 val = SDL_GameControllerGetAxis(game_controller, /*axis=*/SDL_CONTROLLER_AXIS_LEFTX);
	return removeDeadZone(val / 32768.f);
}

float MainWindow::gamepadAxisLeftY()
{
	const Sint16 val = SDL_GameControllerGetAxis(game_controller, /*axis=*/SDL_CONTROLLER_AXIS_LEFTY);
	return removeDeadZone(val / 32768.f);
}

float MainWindow::gamepadAxisRightX()
{
	const Sint16 val = SDL_GameControllerGetAxis(game_controller, /*axis=*/SDL_CONTROLLER_AXIS_RIGHTX);
	return removeDeadZone(val / 32768.f);
}

float MainWindow::gamepadAxisRightY()
{
	const Sint16 val = SDL_GameControllerGetAxis(game_controller, /*axis=*/SDL_CONTROLLER_AXIS_RIGHTY);
	return removeDeadZone(val / 32768.f);
}

#else

#if SUBSTRATA_USE_QT_GAMEPAD
bool MainWindow::gamepadAttached()
{
	return ui->glWidget->gamepad != nullptr;
}

float MainWindow::gamepadButtonL2()
{
	return ui->glWidget->gamepad ? (float)ui->glWidget->gamepad->buttonL2() : 0.0f;
}

float MainWindow::gamepadButtonR2()
{
	return ui->glWidget->gamepad ? (float)ui->glWidget->gamepad->buttonR2() : 0.0f;
}

float MainWindow::gamepadAxisLeftX()
{
	return ui->glWidget->gamepad ? (float)ui->glWidget->gamepad->axisLeftX() : 0.0f;
}

float MainWindow::gamepadAxisLeftY()
{
	return ui->glWidget->gamepad ? (float)ui->glWidget->gamepad->axisLeftY() : 0.0f;
}

float MainWindow::gamepadAxisRightX()
{
	return ui->glWidget->gamepad ? (float)ui->glWidget->gamepad->axisRightX() : 0.0f;
}

float MainWindow::gamepadAxisRightY()
{
	return ui->glWidget->gamepad ? (float)ui->glWidget->gamepad->axisRightY() : 0.0f;
}
#else
bool MainWindow::gamepadAttached()
{
	return false;
}

float MainWindow::gamepadButtonL2() { return 0.0f; }
float MainWindow::gamepadButtonR2() { return 0.0f; }
float MainWindow::gamepadAxisLeftX() { return 0.0f; }
float MainWindow::gamepadAxisLeftY() { return 0.0f; }
float MainWindow::gamepadAxisRightX() { return 0.0f; }
float MainWindow::gamepadAxisRightY() { return 0.0f; }
#endif
#endif


bool MainWindow::supportsSharedGLContexts() const
{
#if defined(_WIN32)
	return true;
#else
	return false; // Not implemented yet for Mac and Linux
#endif
}


void* MainWindow::makeNewSharedGLContext()
{
#if defined(_WIN32)
	return (void*)ui->glWidget->makeNewSharedGLContext();
#else
	return nullptr;
#endif
}


void MainWindow::makeGLContextCurrent(void* context_)
{
#if defined(_WIN32)
	HWND hwnd = reinterpret_cast<HWND>(ui->glWidget->winId());
	HDC hdc = GetDC(hwnd);

	HGLRC handle = (HGLRC)context_;
	BOOL res = wglMakeCurrent(hdc, handle);
	assert(res != 0);
	(void)res;
#endif
}


void* MainWindow::getID3D11Device() const
{
#if defined(_WIN32)
	return (void*)d3d_device.ptr;
#else
	return nullptr;
#endif
}


std::string MainWindow::showOpenFileDialog(const std::string& caption, const std::vector<FileTypeFilter>& file_type_filters, const std::string& settings_key)
{
	QString previous_file = "";

	QSettings local_settings("Glare Technologies", "Cyberspace");

	std::string filter; // e.g. "Images (*.png *.xpm *.jpg);;Text files (*.txt);;XML files (*.xml)"  (see https://doc.qt.io/qt-6/qfiledialog.html)
	for(size_t i=0; i<file_type_filters.size(); ++i)
	{
		const FileTypeFilter& f = file_type_filters[i];
		filter += f.description + " (";
		for(size_t z=0; z<f.file_types.size(); ++z)
		{
			filter += "*." + f.file_types[z];
			if(z + 1 < f.file_types.size())
				filter += " ";
		}
		filter += ")";
		if(i + 1 < file_type_filters.size())
			filter += ";;";
	}

	const QString file = QFileDialog::getOpenFileName(this, QtUtils::toQString(caption), previous_file, QtUtils::toQString(filter));

	if(!file.isNull())
		local_settings.setValue(QtUtils::toQString(settings_key), QtUtils::toQString(FileUtils::getDirectory(QtUtils::toIndString(file)))); // Store dir selected

	return QtUtils::toStdString(file);
}


// The mouse was double-clicked on a web-view object
void MainWindow::webViewMouseDoubleClicked(QMouseEvent* e)
{
	doObjectSelectionTraceForMouseEvent(e);
}


#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
typedef qintptr NativeEventArgType;
#else
typedef long NativeEventArgType;
#endif


// Override nativeEvent() so we can handle WM_COPYDATA messages from other Substrata processes.
// See https://www.programmersought.com/article/216036067/
bool MainWindow::nativeEvent(const QByteArray& event_type, void* message, NativeEventArgType* result)
{
#if defined(_WIN32)
	if(event_type == "windows_generic_MSG")
	{
		MSG* msg = reinterpret_cast<MSG*>(message);

		if(msg->message == WM_COPYDATA)
		{
			COPYDATASTRUCT* copy_data = reinterpret_cast<COPYDATASTRUCT*>(msg->lParam);

			const std::string text_body((const char*)copy_data->lpData, (const char*)copy_data->lpData + copy_data->cbData);

			if(hasPrefix(text_body, "openSubURL:"))
			{
				const std::string url = eatPrefix(text_body, "openSubURL:");

				conPrint("Opening URL '" + url + "'...");

				try
				{
					URLParseResults parse_results = URLParser::parseURL(url);
		
					gui_client.connectToServer(parse_results);

					// Flash the taskbar icon, since the this window may not be visible.
					QApplication::alert(this);
				}
				catch(glare::Exception& e)
				{
					conPrint("Error parsing URL: " + e.what()); // TODO: show message box?
				}
			}
		}
	}
#endif

	return QWidget::nativeEvent(event_type, message, result); // Hand on to Qt processing
}


// If we receive a file-open event before the mainwindow has been created, (e.g. main_window is NULL), then store the url to use when the mainwindow is created.
// However if the mainwindow has already been created, call connectToServer on it.
// Note that this event will only be sent on Macs: "Note: This class is currently supported for macOS only."
// See https://www.programmersought.com/article/55521160114/ - "Use url scheme in mac os to evoke qt program and get startup parameters"
class OpenEventFilter : public QObject
{
public:
	OpenEventFilter() : main_window(NULL) {}

	virtual bool eventFilter(QObject* obj, QEvent* event)
	{
		if(event->type() == QEvent::FileOpen)
		{
			QFileOpenEvent* fileEvent = static_cast<QFileOpenEvent*>(event);
			if(!fileEvent->url().isEmpty())
			{
				const QString qurl = fileEvent->url().toString();

				if(main_window)
				{
					try
					{
						URLParseResults parse_results = URLParser::parseURL(QtUtils::toStdString(qurl));

						main_window->gui_client.connectToServer(parse_results);

						QApplication::alert(main_window); // Flash the taskbar icon, since the this window may not be visible.
					}
					catch(glare::Exception& e)
					{
						conPrint("Error parsing URL: " + e.what()); // TODO: show message box?
					}
				}
				else
					url = QtUtils::toStdString(qurl);
			}
			return true; // Should the event be filtered out, e.g. have we handled it?
		}
		else
		{
			return QObject::eventFilter(obj, event);
		}
	}

	MainWindow* main_window;
	std::string url;
};


// Enable bugsplat unless the DISABLE_BUGSPLAT env var is set to a non-zero value.
#ifdef BUGSPLAT_SUPPORT
static bool shouldEnableBugSplat()
{
	try
	{
		const std::string val = PlatformUtils::getEnvironmentVariable("DISABLE_BUGSPLAT");
		return val == "0";
	}
	catch(glare::Exception&)
	{
		return true;
	}
}
#endif

#ifndef FUZZING


static void qtMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg)
{
	std::string msgstr = QtUtils::toStdString(msg);
	std::string typestr;
	switch(type)
	{
	case QtDebugMsg:    typestr = "Debug";    break;
	case QtInfoMsg:     typestr = "Info";     break;
	case QtWarningMsg:  typestr = "Warning";  break;
	case QtCriticalMsg: typestr = "Critical"; break;
	case QtFatalMsg:    typestr = "Fatal";    break;
	default: break;
	}
	std::string context_str;
	if(context.file)
		context_str = " (" + std::string(context.file) + ":" + toString(context.line) + ", " + (context.function ? std::string(context.function) : "") + ")";
	else
		context_str = " (no location info)";

	const std::string formatted_msg = "Qt: " + typestr + ": " + msgstr + context_str;
	qt_debug_msgs.push_back(formatted_msg);
}


#ifdef BUGSPLAT_SUPPORT
static bool bugSplatExceptionCallback(UINT nCode, LPVOID lpVal1, LPVOID lpVal2)
{
	if(nCode == MDSCB_EXCEPTIONCODE)
	{
		// Flush the log file to ensure all buffered data is written to disk.
		if(log_file)
		{
			log_file->getFileStream() << (doubleToStringNDecimalPlaces(Clock::getTimeSinceInit(), 3) + " s:   Crash caught by BugSplat.");
			log_file->flush();
		}
	}
	return false; // Continue with default BugSplat handling
}
#endif


int main(int argc, char *argv[])
{
	ZoneScoped; // Tracy profiler

	MiniDmpSender* minidump_sender = nullptr;
#ifdef BUGSPLAT_SUPPORT
	if(shouldEnableBugSplat())
	{
		ZoneScopedN("Bugsplat initialization"); // Tracy profiler

		// BugSplat initialization.
		minidump_sender = new MiniDmpSender(
			L"Metasiberia", // database
			L"Metasiberia Beta", // app
			StringUtils::UTF8ToPlatformUnicodeEncoding(cyberspace_version).c_str(), // version
			NULL, // app identifier
			MDSF_USEGUARDMEMORY | MDSF_LOGFILE | MDSF_PREVENTHIJACKING // flags
		);
		minidump_sender->setCallback(bugSplatExceptionCallback);

		// The following calls add support for collecting crashes for abort(), vectored exceptions, out of memory,
		// pure virtual function calls, and for invalid parameters for OS functions.
		// These calls should be used for each module that links with a separate copy of the CRT.
		SetGlobalCRTExceptionBehavior();
		SetPerThreadCRTExceptionBehavior(); // This call needed in each thread of your app
	}
#endif

	qInstallMessageHandler(qtMessageHandler); // Install our message handler.

	QLoggingCategory::setFilterRules("qt.gamepad=true"); // Enable logging of information from the Qt gamepad subsystem for now.

	QApplication::setAttribute(Qt::AA_UseDesktopOpenGL); // See https://forum.qt.io/topic/73255/qglwidget-blank-screen-on-different-computer/7

	//QtWebEngine::initialize();
	QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
//	QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
//	QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
//	QtWebEngineQuick::initialize();

	// Note that this is deliberately constructed outside of the try..catch block below, because QErrorMessage crashes when displayed if
	// GuiClientApplication has been destroyed. (stupid qt).
	GuiClientApplication app(argc, argv);

	try
	{
		OpenEventFilter* open_even_filter = new OpenEventFilter();
		app.installEventFilter(open_even_filter);

#if defined(_WIN32)
		const bool com_init_success = WMFVideoReader::initialiseCOM();
#endif

		GUIClient::staticInit();


		const std::string cyberspace_base_dir_path = PlatformUtils::getResourceDirectoryPath();
		const std::string appdata_path = PlatformUtils::getOrCreateAppDataDirectory("Cyberspace");

		try
		{
			log_file = new FileOutStream(appdata_path + "/log.txt", /*openmode=*/std::ios::out);

#ifdef BUGSPLAT_SUPPORT
			if(minidump_sender)
				minidump_sender->sendAdditionalFile(StringUtils::UTF8ToPlatformUnicodeEncoding(appdata_path + "/log.txt").c_str());
#endif
		}
		catch(glare::Exception& e)
		{
			conPrint("Failed to open log file for writing: " + e.what());
		}

#if defined(_WIN32)
		// Initialize the Media Foundation platform.
		WMFVideoReader::initialiseWMF();
#endif


		QDir::setCurrent(QtUtils::toQString(cyberspace_base_dir_path));
	
		conPrint("cyberspace_base_dir_path: " + cyberspace_base_dir_path);

		// Get a vector of the args.  Note that we will use app.arguments(), because it's the only way to get the args in Unicode in Qt.
		const QStringList arg_list = app.arguments();
		std::vector<std::string> args;
		for(int i = 0; i < arg_list.size(); ++i)
			args.push_back(QtUtils::toIndString(arg_list.at((int)i)));


		std::map<std::string, std::vector<ArgumentParser::ArgumentType> > syntax;
		syntax["--test"] = std::vector<ArgumentParser::ArgumentType>(); // Run unit tests
		syntax["-h"] = std::vector<ArgumentParser::ArgumentType>(1, ArgumentParser::ArgumentType_string); // Specify hostname to connect to
		syntax["-u"] = std::vector<ArgumentParser::ArgumentType>(1, ArgumentParser::ArgumentType_string); // Specify server URL to connect to
		syntax["-linku"] = std::vector<ArgumentParser::ArgumentType>(1, ArgumentParser::ArgumentType_string); // Specify server URL to connect to, when a user has clicked on a substrata URL hyperlink.
		syntax["--processanims"] = std::vector<ArgumentParser::ArgumentType>(); // Build animation data
		syntax["--screenshotslave"] = std::vector<ArgumentParser::ArgumentType>(); // Run GUI as a screenshot-taking slave.
		syntax["--testscreenshot"] = std::vector<ArgumentParser::ArgumentType>(); // Test screenshot taking
		syntax["--no_MDI"] = std::vector<ArgumentParser::ArgumentType>(); // Disable MDI in graphics engine
		syntax["--no_bindless"] = std::vector<ArgumentParser::ArgumentType>(); // Disable bindless textures in graphics engine
		syntax["--use_temp_resources_db"] = std::vector<ArgumentParser::ArgumentType>(); // Use a temporary, fresh resource database.  For testing.
		syntax["--vr"] = std::vector<ArgumentParser::ArgumentType>(); // Prefer VR startup when XR is compiled in.
		syntax["--desktop"] = std::vector<ArgumentParser::ArgumentType>(); // Disable XR startup and stay in desktop mode.
		syntax["--scientific_pubchem_smoke"] = std::vector<ArgumentParser::ArgumentType>(1, ArgumentParser::ArgumentType_string); // Run a narrow PubChem HTTPS smoke check and write a JSON report.
		syntax["--scientific_pubchem_apply_smoke"] = std::vector<ArgumentParser::ArgumentType>(1, ArgumentParser::ArgumentType_string); // Run PubChem UI selection/apply smoke and write a JSON report.
		syntax["--scientific_molecule_info_smoke"] = std::vector<ArgumentParser::ArgumentType>(1, ArgumentParser::ArgumentType_string); // Run molecule labels/selection/measurement/Russian-search smoke.
		syntax["--tree_generator_smoke"] = std::vector<ArgumentParser::ArgumentType>(1, ArgumentParser::ArgumentType_string); // Run procedural tree generator determinism smoke.
		syntax["--tree_editor_smoke"] = std::vector<ArgumentParser::ArgumentType>(1, ArgumentParser::ArgumentType_string); // Run tree editor preset-population/repair smoke.
		syntax["--voxel_editor_smoke"] = std::vector<ArgumentParser::ArgumentType>(1, ArgumentParser::ArgumentType_string); // Run native voxel editor data/tools/widget smoke.

		if(args.size() == 3 && args[1] == "-NSDocumentRevisionsDebugMode")
			args.resize(1); // This is some XCode debugging rubbish, remove it

		ArgumentParser parsed_args(args, syntax);

		if(parsed_args.isArgPresent("--scientific_pubchem_smoke"))
			return ScientificObjectEditor::runPubChemSmokeCheck(QtUtils::toQString(parsed_args.getArgStringValue("--scientific_pubchem_smoke")));
		if(parsed_args.isArgPresent("--scientific_pubchem_apply_smoke"))
			return ScientificObjectEditor::runPubChemApplySmokeCheck(QtUtils::toQString(parsed_args.getArgStringValue("--scientific_pubchem_apply_smoke")));
		if(parsed_args.isArgPresent("--scientific_molecule_info_smoke"))
			return ScientificObjectEditor::runMoleculeInformationSmokeCheck(QtUtils::toQString(parsed_args.getArgStringValue("--scientific_molecule_info_smoke")));
		if(parsed_args.isArgPresent("--tree_generator_smoke"))
			return TreeGenerator::runSmokeCheck(parsed_args.getArgStringValue("--tree_generator_smoke"));
		if(parsed_args.isArgPresent("--tree_editor_smoke"))
			return TreeEditorPanel::runSmokeCheck(parsed_args.getArgStringValue("--tree_editor_smoke"));
		if(parsed_args.isArgPresent("--voxel_editor_smoke"))
		{
			std::string report;
			const bool smoke_ok = VoxelEditorPanel::runSmokeCheck(report);
			QFile report_file(QtUtils::toQString(parsed_args.getArgStringValue("--voxel_editor_smoke")));
			if(!report_file.open(QIODevice::WriteOnly | QIODevice::Truncate) || report_file.write(QByteArray(report.data(), (int)report.size())) != (qint64)report.size())
				return 2;
			return smoke_ok ? 0 : 1;
		}

		if(parsed_args.isArgPresent("--test"))
		{
			TestSuite::test();
			return 0;
		}

		// Build .subanim processed animation files from animation GLBs.
		if(parsed_args.isArgPresent("--processanims"))
		{
			AvatarGraphics::processAnimationData();
			return 0;
		}


		//std::string server_hostname = "vr.metasiberia.com";
		//std::string server_userpath = "";
		std::string server_URL = "sub://vr.metasiberia.com";
		bool server_URL_explicitly_specified = false;

		if(parsed_args.isArgPresent("-h"))
		{
			server_URL = "sub://" + parsed_args.getArgStringValue("-h");
			server_URL_explicitly_specified = true;
		}
			//server_hostname = parsed_args.getArgStringValue("-h");
		if(parsed_args.isArgPresent("-u"))
		{
			server_URL = parsed_args.getArgStringValue("-u");
			server_URL_explicitly_specified = true;
			//const std::string URL = parsed_args.getArgStringValue("-u");
			//try
			//{
			//	URLParseResults parse_res = URLParser::parseURL(URL);

			//	server_hostname = parse_res.hostname;
			//	server_userpath = parse_res.userpath;
			//}
			//catch(glare::Exception& e) // Handle URL parse failure
			//{
			//	QMessageBox msgBox;
			//	msgBox.setText(QtUtils::toQString(e.what()));
			//	msgBox.exec();
			//	return 1;
			//}
		}
		else if(parsed_args.isArgPresent("-linku"))
		{
#if defined(_WIN32)
			// If we already have a Metasiberia application open on this computer, we want to tell that one to go to the URL, instead of opening another instance.
			// Search for an already existing Window called "Metasiberia Beta vx.y"
			// If it exists, send a Windows message to that process, telling it to open the URL, and return from this process.
			// TODO: work out how to do this on Mac and Linux, if it's needed.
			const std::string target_window_title = computeWindowTitle();
			const HWND target_hwnd = ::FindWindowA(NULL, target_window_title.c_str());
			if(target_hwnd != 0)
			{
				const std::string msg_body = "openSubURL:" + parsed_args.getArgStringValue("-linku");

				COPYDATASTRUCT copy_data;
				copy_data.dwData = 0;
				copy_data.cbData = (DWORD)msg_body.size(); // The size, in bytes, of the data pointed to by the lpData member.
				copy_data.lpData = (void*)msg_body.data(); // The data to be passed to the receiving application

				SendMessage(target_hwnd,
					WM_COPYDATA,
					NULL,
					(LPARAM)&copy_data
				);
				return 0;
			}
			else
			{
				server_URL = parsed_args.getArgStringValue("-linku");
				server_URL_explicitly_specified = true;
			}
#else
			server_URL = parsed_args.getArgStringValue("-linku");
			server_URL_explicitly_specified = true;
#endif
		}

		if(!open_even_filter->url.empty())
		{
			// If we have received a url from a file-open event on Mac:
			server_URL = open_even_filter->url; // Use it
			server_URL_explicitly_specified = true;
		}


		int app_exec_res;
		{ // Scope of MainWindow mw.

			// We want to call connectToServer as quickly as possible to hide the latency of setting up the TLS connection to the server.
			// So do the bare minimum of initialisation, call connectToServer, then do the reset (setting up UI etc.)

			MainWindow mw(cyberspace_base_dir_path, appdata_path, parsed_args);
			mw.minidump_sender = minidump_sender;

			// If the user didn't explicitly specify a URL (e.g. on the command line), and there is a valid start location URL setting, use it.
			if(!server_URL_explicitly_specified)
			{
				const std::string start_loc_URL_setting = QtUtils::toStdString(mw.settings->value(MainOptionsDialog::startLocationURLKey()).toString());
				if(!start_loc_URL_setting.empty())
				{
					const std::string canonical_start_loc_URL = canonicaliseMetasiberiaSubURLHost(start_loc_URL_setting);
					if(canonical_start_loc_URL != start_loc_URL_setting)
						mw.settings->setValue(MainOptionsDialog::startLocationURLKey(), QtUtils::toQString(canonical_start_loc_URL));

					server_URL = canonical_start_loc_URL;
				}
			}

			server_URL = canonicaliseMetasiberiaSubURLHost(server_URL);

			try
			{
				URLParseResults parse_results = URLParser::parseURL(server_URL);

				mw.gui_client.connectToServer(parse_results);
			}
			catch(glare::Exception& e)
			{
				QtUtils::showErrorMessageDialog(e.what(), &mw);
			}

			// Do rest of initialisation now we have called connectToServer().
			mw.gui_client.postConnectInitialise();

			bool enable_CEF = true;
			try
			{
				const std::string val = PlatformUtils::getEnvironmentVariable("SUBSTRATA_ENABLE_CEF");
				if(toLowerCase(val) == "false")
					enable_CEF = false;
			}
			catch(glare::Exception& )
			{}

			if(enable_CEF)
				CEF::initialiseCEF(cyberspace_base_dir_path, appdata_path);

			open_even_filter->main_window = &mw;

			if(parsed_args.isArgPresent("--screenshotslave"))
				mw.run_as_screenshot_slave = true;

			if(parsed_args.isArgPresent("--testscreenshot"))
				mw.test_screenshot_taking = true;

			mw.initialiseUI();

			if(!enable_CEF)
				mw.logMessage("!!!!! Disallowing CEF usage due to SUBSTRATA_ENABLE_CEF env var being set to false !!!!!");
			else
			{
				if(CEF::initialisationFailed())
					mw.logMessage("CEF initialisation failed: " + CEF::getInitialisationFailureErrorString()); // Log CEF initialisation failure now that mw.log_window has been created.
				else
					mw.logMessage("CEF initialised successfully.");
			}

			mw.show(); // Calls glWidget->initializeGL() which initialises OpenGLEngine.

			mw.raise();

			if(!mw.ui->glWidget->opengl_engine->initSucceeded())
			{
				const std::string msg = "OpenGL engine initialisation failed: " + mw.ui->glWidget->opengl_engine->getInitialisationErrorMsg();
				
				mw.logMessage(msg);
				
				QtUtils::showErrorMessageDialog(msg, &mw);
				return 1;
			}
			mw.opengl_engine = mw.ui->glWidget->opengl_engine;
			mw.gui_client.opengl_engine = mw.opengl_engine;

			mw.gui_client.cam_controller.setFirstAndThirdPersonPositions(Vec3d(0,0,4.7));
			mw.ui->glWidget->setCameraController(&mw.gui_client.cam_controller);
			mw.gui_client.cam_controller.setMoveScale(0.3f);



			mw.afterGLInitInitialise();


			app_exec_res = app.exec();

			open_even_filter->main_window = NULL;
		} // End scope of MainWindow mw

#if defined(_WIN32)
		WMFVideoReader::shutdownWMF();
#endif

		delete log_file;
		log_file = nullptr;
		
		GUIClient::staticShutdown();

#if defined(_WIN32)
		if(com_init_success) WMFVideoReader::shutdownCOM();
#endif

		return app_exec_res;
	}
	catch(Indigo::IndigoException& e)
	{
		// Show error
		conPrint(toStdString(e.what()));
		QErrorMessage m;
		m.showMessage(QtUtils::toQString(e.what()));
		m.exec();
		return 1;
	}
	catch(glare::Exception& e)
	{
		// Show error
		conPrint(e.what());
		QErrorMessage m;
		m.showMessage(QtUtils::toQString(e.what()));
		m.exec();
		return 1;
	}
}


#endif // End #ifndef FUZZING
