#include "PluginPackage.h"
#include "PluginRuntime.h"
#include "QtPluginHostAdapter.h"
#include "PluginsDockWidget.h"
#include "PluginPanelWidget.h"
#include "RuntimeTranslation.h"
#include "LucideIconUtils.h"

#include <QtWidgets/QApplication>
#include <QtWidgets/QAbstractButton>
#include <QtCore/QBuffer>
#include <QtCore/QCryptographicHash>
#include <QtCore/QDebug>
#include <QtCore/QElapsedTimer>
#include <QtCore/QEventLoop>
#include <QtCore/QDir>
#include <QtCore/QDirIterator>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QRegularExpression>
#include <QtCore/QTemporaryDir>
#include <QtCore/QTimer>
#include <QtCore/QThread>
#include <QtCore/QMutex>
#include <QtCore/QMutexLocker>
#include <QtCore/QMap>
#include <QtCore/QProcess>
#include <QtCore/QProcessEnvironment>
#include <QtCore/QSaveFile>
#include <QtGui/QImage>
#include <QtGui/QPainter>
#include <QtGui/QPixmap>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QDialog>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QDockWidget>
#include <QtWidgets/QFrame>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QMainWindow>
#include <QtWidgets/QMenu>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QTabWidget>
#include <QtWidgets/QTextBrowser>
#include <QtWidgets/QToolButton>

#include <iostream>
#include <algorithm>
#include <cmath>
#include <functional>
#include <zlib.h>


namespace
{
int failures = 0;

void check(const bool condition, const char* message)
{
	if(!condition)
	{
		std::cerr << "FAIL: " << message << std::endl;
		++failures;
	}
	else
		std::cout << "PASS: " << message << std::endl;
}

bool waitFor(const std::function<bool()>& condition, const int timeout_ms = 2500)
{
	QElapsedTimer timer;
	timer.start();
	while(!condition() && timer.elapsed() < timeout_ms)
	{
		QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
		QThread::msleep(1);
	}
	QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
	return condition();
}

void append16(QByteArray& output, const quint16 value)
{
	output.append(static_cast<char>(value & 0xff));
	output.append(static_cast<char>((value >> 8) & 0xff));
}

void append32(QByteArray& output, const quint32 value)
{
	append16(output, static_cast<quint16>(value & 0xffff));
	append16(output, static_cast<quint16>(value >> 16));
}

struct TestEntry
{
	QByteArray name;
	QByteArray contents;
	quint32 unix_mode = 0;
	bool deflate = false;
};

QByteArray deflateRaw(const QByteArray& input)
{
	z_stream stream = {};
	if(deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY) != Z_OK)
		return QByteArray();
	QByteArray output;
	output.resize(static_cast<int>(deflateBound(&stream, static_cast<uLong>(input.size()))));
	stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(input.constData()));
	stream.avail_in = static_cast<uInt>(input.size());
	stream.next_out = reinterpret_cast<Bytef*>(output.data());
	stream.avail_out = static_cast<uInt>(output.size());
	const int result = deflate(&stream, Z_FINISH);
	const bool valid = result == Z_STREAM_END;
	output.resize(static_cast<int>(stream.total_out));
	deflateEnd(&stream);
	return valid ? output : QByteArray();
}

QByteArray makeZip(const QVector<TestEntry>& entries)
{
	QByteArray output;
	QVector<quint32> offsets;
	QVector<quint32> checksums;
	QVector<QByteArray> payloads;
	QVector<quint16> methods;
	for(const TestEntry& entry : entries)
	{
		payloads.push_back(entry.deflate ? deflateRaw(entry.contents) : entry.contents);
		methods.push_back(entry.deflate ? 8 : 0);
		offsets.push_back(static_cast<quint32>(output.size()));
		checksums.push_back(static_cast<quint32>(crc32(0,
			reinterpret_cast<const Bytef*>(entry.contents.constData()), static_cast<uInt>(entry.contents.size()))));
		append32(output, 0x04034b50);
		append16(output, 20); // version needed
		append16(output, 1u << 11); // UTF-8
		append16(output, methods.back());
		append16(output, 0); append16(output, 0);
		append32(output, checksums.back());
		append32(output, static_cast<quint32>(payloads.back().size()));
		append32(output, static_cast<quint32>(entry.contents.size()));
		append16(output, static_cast<quint16>(entry.name.size()));
		append16(output, 0);
		output += entry.name;
		output += payloads.back();
	}
	const quint32 central_offset = static_cast<quint32>(output.size());
	for(int i = 0; i < entries.size(); ++i)
	{
		const TestEntry& entry = entries[i];
		append32(output, 0x02014b50);
		append16(output, static_cast<quint16>((3u << 8) | 20u)); // Unix creator
		append16(output, 20);
		append16(output, 1u << 11);
		append16(output, methods[i]);
		append16(output, 0); append16(output, 0);
		append32(output, checksums[i]);
		append32(output, static_cast<quint32>(payloads[i].size()));
		append32(output, static_cast<quint32>(entry.contents.size()));
		append16(output, static_cast<quint16>(entry.name.size()));
		append16(output, 0); append16(output, 0);
		append16(output, 0); append16(output, 0);
		append32(output, entry.unix_mode << 16);
		append32(output, offsets[i]);
		output += entry.name;
	}
	const quint32 central_size = static_cast<quint32>(output.size()) - central_offset;
	append32(output, 0x06054b50);
	append16(output, 0); append16(output, 0);
	append16(output, static_cast<quint16>(entries.size()));
	append16(output, static_cast<quint16>(entries.size()));
	append32(output, central_size);
	append32(output, central_offset);
	append16(output, 0);
	return output;
}

QByteArray tinyPng();

QByteArray tinySvg()
{
	return QByteArrayLiteral("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"24\" height=\"24\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"#A78BFA\" stroke-width=\"2\"><path d=\"M12 22v-5\"/><circle cx=\"12\" cy=\"8\" r=\"6\"/></svg>");
}

QByteArray manifest(const int apiMajor = 1, const QString& runtime = QStringLiteral("luau"))
{
	QJsonObject api;
	api.insert(QStringLiteral("major"), apiMajor);
	api.insert(QStringLiteral("minor"), 0);
	QJsonObject object;
	object.insert(QStringLiteral("schemaVersion"), 1);
	object.insert(QStringLiteral("id"), QStringLiteral("org.metasiberia.test.hello"));
	object.insert(QStringLiteral("name"), QStringLiteral("Hello test"));
	object.insert(QStringLiteral("author"), QStringLiteral("Test fixture"));
	object.insert(QStringLiteral("version"), QStringLiteral("1.0.0"));
	object.insert(QStringLiteral("description"), QStringLiteral("A package that must stay inert."));
	object.insert(QStringLiteral("runtime"), runtime);
	object.insert(QStringLiteral("entry"), QStringLiteral("main.luau"));
	object.insert(QStringLiteral("icon"), QStringLiteral("assets/plugin.png"));
	object.insert(QStringLiteral("api"), api);
	object.insert(QStringLiteral("permissions"), QJsonArray());
	return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

QVector<TestEntry> validEntries(const int apiMajor = 1, const QString& runtime = QStringLiteral("luau"))
{
	return {{QByteArrayLiteral("plugin.json"), manifest(apiMajor, runtime), 0100644},
		{QByteArrayLiteral("main.luau"), QByteArrayLiteral("write_marker_if_executed() -- inert payload"), 0100644, true},
		{QByteArrayLiteral("assets/plugin.png"), tinyPng(), 0100644}};
}


QVector<TestEntry> luauEntries(const QByteArray& script, const QStringList& permissions = QStringList())
{
	QVector<TestEntry> entries = validEntries();
	QJsonObject object = QJsonDocument::fromJson(entries[0].contents).object();
	QJsonArray permission_array;
	for(const QString& permission : permissions) permission_array.push_back(permission);
	object.insert(QStringLiteral("permissions"), permission_array);
	entries[0].contents = QJsonDocument(object).toJson(QJsonDocument::Compact);
	entries[1].contents = script;
	return entries;
}


QVector<TestEntry> oldHello100Entries()
{
	QVector<TestEntry> entries = luauEntries(QByteArrayLiteral("-- Hello Metasiberia 1.0.0 legacy entrypoint\nlocal message = 'Hello, Metasiberia!'"));
	QJsonObject object = QJsonDocument::fromJson(entries[0].contents).object();
	object.insert(QStringLiteral("id"), QStringLiteral("org.metasiberia.example.hello"));
	object.insert(QStringLiteral("name"), QStringLiteral("Hello Metasiberia"));
	object.insert(QStringLiteral("author"), QStringLiteral("Metasiberia Example"));
	object.insert(QStringLiteral("version"), QStringLiteral("1.0.0"));
	object.insert(QStringLiteral("description"), QStringLiteral("Legacy package fixture with no command registration."));
	object.insert(QStringLiteral("permissions"), QJsonArray());
	entries[0].contents = QJsonDocument(object).toJson(QJsonDocument::Compact);
	return entries;
}


class FakePluginHost final : public PluginHostInterface
{
public:
	struct Notification { QString pluginId; QString title; QString body; };

	bool registerCommand(const QString& pluginId, const QString& pluginVersion, const QString& commandId,
		const QString& title, const std::function<void()>& callback, QString& error) override
	{
		QMutexLocker lock(&mutex);
		const QString key = pluginId + QLatin1Char('@') + pluginVersion + QLatin1Char('/') + commandId;
		if(commands.contains(key) || !callback)
		{
			error = QStringLiteral("duplicate command");
			return false;
		}
		commands.insert(key, PluginCommandRegistration{pluginId, pluginVersion, commandId, title, callback});
		return true;
	}
	bool registerPanel(const QString& pluginId, const QString& pluginVersion, const QString& viewId,
		const QString& title, const QJsonObject& layout,
		const std::function<void(const QString&)>& actionCallback, QString& error) override
	{
		if(!actionCallback || !validatePluginPanelLayout(layout, error)) return false;
		QMutexLocker lock(&mutex);
		const QString key = pluginId + QLatin1Char('@') + pluginVersion + QLatin1Char('/') + viewId;
		if(panels.contains(key))
		{
			error = QStringLiteral("duplicate panel");
			return false;
		}
		panels.insert(key, PluginPanelRegistration{pluginId, pluginVersion, viewId, title, layout, actionCallback});
		return true;
	}
	void removePluginResources(const QString& pluginId, const QString& pluginVersion) override
	{
		QMutexLocker lock(&mutex);
		for(auto it = commands.begin(); it != commands.end(); )
			if(it->pluginId == pluginId && it->pluginVersion == pluginVersion) it = commands.erase(it); else ++it;
		for(auto it = panels.begin(); it != panels.end(); )
			if(it->pluginId == pluginId && it->pluginVersion == pluginVersion) it = panels.erase(it); else ++it;
	}
	void closePanel(const QString&, const QString&, const QString&) override {}
	void showNotification(const QString& pluginId, const QString& title, const QString& body) override
	{
		QMutexLocker lock(&mutex);
		notifications.push_back({pluginId, title, body});
		last_notification_thread = QThread::currentThread();
	}
	bool isUiThread() const override
	{
		return QCoreApplication::instance() && QThread::currentThread() == QCoreApplication::instance()->thread();
	}
	QList<PluginCommandRegistration> registeredCommands() const override
	{
		QMutexLocker lock(&mutex);
		return commands.values();
	}
	QList<PluginPanelRegistration> registeredPanels() const override
	{
		QMutexLocker lock(&mutex);
		return panels.values();
	}
	QList<Notification> notificationSnapshot() const
	{
		QMutexLocker lock(&mutex);
		return notifications;
	}
	QThread* notificationThreadSnapshot() const
	{
		QMutexLocker lock(&mutex);
		return last_notification_thread;
	}

	mutable QMutex mutex;
	QMap<QString, PluginCommandRegistration> commands;
	QMap<QString, PluginPanelRegistration> panels;
	QList<Notification> notifications;
	QThread* last_notification_thread = nullptr;
};


double channelLinear(const double value)
{
	return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
}


double contrastRatio(const QColor& first, const QColor& second)
{
	const double a = 0.2126 * channelLinear(first.redF()) + 0.7152 * channelLinear(first.greenF()) + 0.0722 * channelLinear(first.blueF());
	const double b = 0.2126 * channelLinear(second.redF()) + 0.7152 * channelLinear(second.greenF()) + 0.0722 * channelLinear(second.blueF());
	return (std::max(a, b) + 0.05) / (std::min(a, b) + 0.05);
}

bool writePackage(const QString& path, const QVector<TestEntry>& entries)
{
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(makeZip(entries)) > 0;
}


QVector<TestEntry> readPluginProject(const QString& projectDirectory)
{
	QVector<TestEntry> entries;
	const QDir root(projectDirectory);
	if(!root.exists()) return entries;
	QStringList paths;
	QDirIterator iterator(projectDirectory, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
	while(iterator.hasNext())
	{
		const QString path = iterator.next();
		const QFileInfo info(path);
		if(info.isSymLink()) return {};
		paths.push_back(root.relativeFilePath(path));
	}
	paths.sort(Qt::CaseSensitive);
	for(const QString& relative : paths)
	{
		QFile file(root.filePath(relative));
		if(!file.open(QIODevice::ReadOnly)) return {};
		const QByteArray contents = file.readAll();
		if(contents.size() != file.size()) return {};
		entries.push_back({relative.toUtf8(), contents, 0100644});
	}
	return entries;
}

QByteArray tinyPng()
{
	QImage image(48, 48, QImage::Format_ARGB32);
	image.fill(Qt::transparent);
	QPainter painter(&image);
	painter.setRenderHint(QPainter::Antialiasing);
	painter.setPen(QPen(QColor(229, 218, 255), 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
	painter.setBrush(QColor(120, 82, 180));
	QPolygon box;
	box << QPoint(24, 5) << QPoint(42, 15) << QPoint(42, 34) << QPoint(24, 44) << QPoint(6, 34) << QPoint(6, 15);
	painter.drawPolygon(box);
	painter.setBrush(Qt::NoBrush);
	painter.drawLine(6, 15, 24, 25);
	painter.drawLine(42, 15, 24, 25);
	painter.drawLine(24, 25, 24, 44);
	painter.end();
	QByteArray bytes;
	QBuffer buffer(&bytes);
	if(!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG")) return QByteArray();
	return bytes;
}

class InMemoryStoreFixture final : public PluginStoreCatalogProvider
{
public:
	bool isAvailable() const override { return true; }
	QString statusText() const override { return QStringLiteral("Test-only in-memory catalog fixture"); }
	QList<PluginPackageInfo> search(const QString& query, QString& error) const override
	{
		error.clear();
		if(query.isEmpty() || QStringLiteral("sample published package").contains(query, Qt::CaseInsensitive))
			return {entry};
		return {};
	}
	PluginPackageInfo entry = []() {
		PluginPackageInfo info;
		info.id = QStringLiteral("org.example.published");
		info.name = QStringLiteral("Sample published package");
		info.author = QStringLiteral("Test only");
		info.version = QStringLiteral("1.0.0");
		info.apiMajor = 1;
		info.apiMinor = 0;
		info.apiCompatible = true;
		info.categories = QStringList() << QStringLiteral("Tools");
		return info;
	}();
};

int captureUi(const QString& output_prefix, const QString& icon_directory)
{
	QTemporaryDir temporary;
	if(!temporary.isValid()) return 2;
	const QString package_path = temporary.filePath(QStringLiteral("hello.mspkg"));
	if(!writePackage(package_path, validEntries())) return 3;
	const QString plugin_root = temporary.filePath(QStringLiteral("plugins"));
	const QString settings_root = temporary.filePath(QStringLiteral("settings"));
	PluginPackageManager installer(plugin_root, nullptr, settings_root);
	PluginPackageInfo installed_info;
	QString error;
	if(!installer.installPackage(package_path, installed_info, error))
	{
		std::cerr << "Could not prepare screenshot package: " << error.toStdString() << std::endl;
		return 4;
	}
	InMemoryStoreFixture catalog;
	RuntimeTranslation::RuntimeTranslator translator;
	QApplication::instance()->installTranslator(&translator);

	QPalette palette;
	palette.setColor(QPalette::Window, QColor(QStringLiteral("#2B2B30")));
	palette.setColor(QPalette::WindowText, QColor(QStringLiteral("#E8E8EA")));
	palette.setColor(QPalette::Base, QColor(QStringLiteral("#1E1E22")));
	palette.setColor(QPalette::AlternateBase, QColor(QStringLiteral("#34343A")));
	palette.setColor(QPalette::Text, QColor(QStringLiteral("#E8E8EA")));
	palette.setColor(QPalette::Button, QColor(QStringLiteral("#35353B")));
	palette.setColor(QPalette::ButtonText, QColor(QStringLiteral("#E8E8EA")));
	palette.setColor(QPalette::Highlight, QColor(QStringLiteral("#604A8C")));
	palette.setColor(QPalette::HighlightedText, QColor(QStringLiteral("#FFFFFF")));
	QApplication::setPalette(palette);

	QMainWindow host;
	FakePluginHost plugin_host;
	host.setWindowTitle(QStringLiteral("Metasiberia — Plugin Manager preview"));
	host.resize(1440, 900);
	QLabel* viewport = new QLabel(QStringLiteral("Metasiberia · isolated Plugin Manager preview"), &host);
	viewport->setAlignment(Qt::AlignCenter);
	viewport->setStyleSheet(QStringLiteral("background: qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #4A77A1,stop:0.56 #729CB9,stop:0.57 #C9C9C9,stop:1 #B7B7B7); color:#FFFFFF; font-size:20px;"));
	host.setCentralWidget(viewport);
	PluginsDockWidget* panel = new PluginsDockWidget(&host, &catalog, plugin_root, settings_root, icon_directory);
	panel->setPluginHost(&plugin_host);
	panel->setObjectName(QStringLiteral("pluginsDockWidgetPreview"));
	host.addDockWidget(Qt::RightDockWidgetArea, panel);
	panel->show();
	host.show();
	QApplication::processEvents();

	const QStringList names = {QStringLiteral("catalog"), QStringLiteral("installed"),
		QStringLiteral("installation"), QStringLiteral("instructions")};
	for(int index = 0; index < names.size(); ++index)
	{
		panel->findChild<QTabWidget*>(QStringLiteral("pluginManagerTabs"))->setCurrentIndex(index);
		if(index == 0)
			panel->findChild<QListWidget*>(QStringLiteral("pluginStoreList"))->setCurrentRow(0);
		if(index == 1)
			panel->findChild<QListWidget*>(QStringLiteral("installedPluginList"))->setCurrentRow(0);
		QApplication::processEvents();
		if(!host.grab().save(output_prefix + QLatin1Char('-') + names[index] + QStringLiteral(".png")))
			return 5;
	}
	QMenu menu;
	for(const PluginListEntry& entry : panel->installedPlugins())
	{
		QAction* action = menu.addAction(entry.icon, QStringLiteral("%1 · %2 · %3").arg(entry.name, entry.version, entry.state));
		action->setToolTip(QStringLiteral("Open plugin details. Enabling a local package requires explicit trust confirmation."));
	}
	menu.addSeparator();
	menu.addAction(QStringLiteral("+ Добавить"));
	menu.ensurePolished();
	menu.adjustSize();
	QPixmap menu_image(menu.size());
	menu_image.fill(Qt::transparent);
	menu.render(&menu_image);
	if(!menu_image.save(output_prefix + QStringLiteral("-top-menu.png"))) return 6;
	PluginPackageManager cleanup(plugin_root, nullptr, settings_root);
	if(!cleanup.removePackage(installed_info.id, installed_info.version, false, error)) return 7;
	panel->reloadPluginCatalogs();
	panel->showInstalledTab();
	QApplication::processEvents();
	if(!host.grab().save(output_prefix + QStringLiteral("-empty.png"))) return 8;
	return 0;
}


int pluginUiEventLoopIntegration(QApplication& app)
{
	QTemporaryDir temporary;
	check(temporary.isValid(), "Qt event-loop integration uses an isolated temporary data root");
	if(!temporary.isValid()) return 2;
	const QString package_dir = temporary.filePath(QStringLiteral("packages"));
	const QString plugin_root = temporary.filePath(QStringLiteral("plugins"));
	const QString settings_root = temporary.filePath(QStringLiteral("settings"));
	QDir().mkpath(package_dir);
	const QString old_package = QDir(package_dir).filePath(QStringLiteral("hello-metasiberia-1.0.0.mspkg"));
	const QString updated_package = QDir(package_dir).filePath(QStringLiteral("hello-metasiberia-1.0.4.mspkg"));
	check(writePackage(old_package, oldHello100Entries()), "prepare old Hello 1.0.0 package fixture without modifying user plugins");
	check(writePackage(updated_package, readPluginProject(QStringLiteral(METASIBERIA_PLUGIN_TEST_HELLO_DIR))),
		"prepare the real Hello 1.0.4 update fixture with the same plugin id");
	PluginPackageManager installer(plugin_root, nullptr, settings_root);
	PluginPackageInfo old_info;
	QString error;
	check(installer.installPackage(old_package, old_info, error) && old_info.id == QStringLiteral("org.metasiberia.example.hello") &&
		old_info.version == QStringLiteral("1.0.0"), "install the legacy Hello 1.0.0 fixture into the temporary plugin directory");

	QMainWindow host_window;
	host_window.resize(640, 480);
	PluginsDockWidget panel(&host_window, nullptr, plugin_root, settings_root, QStringLiteral(METASIBERIA_PLUGIN_TEST_ICON_DIR));
	QtPluginHostAdapter host_adapter({});
	panel.setPluginHost(&host_adapter);
	host_window.addDockWidget(Qt::RightDockWidgetArea, &panel);
	panel.showInstalledTab();
	host_window.show();
	panel.show();
	QCheckBox* toggle = panel.findChild<QCheckBox*>(QStringLiteral("pluginEnabledSwitch"));
	check(toggle && toggle->isEnabled() && !toggle->isChecked(), "the real installed-plugin checkbox is ready for the legacy package");
	if(!toggle) return 3;

	int heartbeats = 0;
	int phase = 0;
	bool first_trust_prompt_seen = false;
	bool update_trust_prompt_seen = false;
	bool update_trust_cancelled = false;
	bool integration_timed_out = false;
	int integration_result = 1;
	QElapsedTimer elapsed;
	QTimer heartbeat;
	heartbeat.setInterval(10);
	QObject::connect(&heartbeat, &QTimer::timeout, &app, [&]() { ++heartbeats; });
	QTimer trust_prompt_driver;
	trust_prompt_driver.setInterval(10);
	QObject::connect(&trust_prompt_driver, &QTimer::timeout, &app, [&]() {
		for(QWidget* const window : QApplication::topLevelWidgets())
		{
			auto* const prompt = qobject_cast<QDialog*>(window);
			if(!prompt || prompt->objectName() != QStringLiteral("pluginTrustDialog") || !prompt->isVisible()) continue;
			QDialogButtonBox* const buttons = prompt->findChild<QDialogButtonBox*>();
			if(!buttons) continue;
			if(phase == 0)
			{
				first_trust_prompt_seen = true;
				if(QAbstractButton* const button = buttons->button(QDialogButtonBox::Ok))
					button->click();
				return;
			}
			else if(phase == 2)
			{
				update_trust_prompt_seen = true;
				if(QAbstractButton* const button = buttons->button(QDialogButtonBox::Cancel))
				{
					update_trust_cancelled = true;
					button->click();
					phase = 3;
					return;
				}
			}
		}
	});
	QTimer poll;
	poll.setInterval(10);
	QObject::connect(&poll, &QTimer::timeout, &app, [&]() {
		if(phase == 0)
		{
			const PluginRuntimeStatus status = panel.pluginRuntimeStatusForTest(old_info.id, old_info.version);
			if(!status.error.isEmpty() && !panel.pluginRuntimeHasActiveInstanceForTest(old_info.id, old_info.version) &&
				panel.pluginWorkerProcessFinishedCountForTest() == 1)
			{
				check(first_trust_prompt_seen, "checkbox opens a trust prompt without a blocking nested dialog loop");
				check(status.error.contains(QStringLiteral("did not register a primary menu action")) &&
					!status.enabled && !status.running, "legacy Hello 1.0.0 reports the expected error and never remains Running");
				check(panel.pluginWorkerProcessStartedCountForTest() == 1 && panel.pluginWorkerProcessFinishedCountForTest() == 1,
					"one child worker starts and exits exactly once for the checkbox action");
				check(heartbeats >= 5, "Qt event loop continues delivering heartbeat timers during worker startup and failure");
				phase = 1;
				QTimer::singleShot(0, &app, [&panel, &app, &updated_package, &old_info, &phase]() {
					panel.installPackageFileForTest(updated_package);
					check(panel.installConfirmationWasRequestedForTest(), "the UI update handler reaches its install confirmation gate");
					const QList<PluginListEntry> installed = panel.installedPlugins();
					check(installed.size() == 1 && installed.first().id == old_info.id && installed.first().version == QStringLiteral("1.0.4"),
						"the Plugin Manager UI update handler replaces Hello 1.0.0 with 1.0.4 for the same id");
					QLabel* const status_label = panel.findChild<QLabel*>(QStringLiteral("installedPluginStatus"));
					QCheckBox* const updated_toggle = panel.findChild<QCheckBox*>(QStringLiteral("pluginEnabledSwitch"));
					check(status_label && status_label->text().contains(QStringLiteral("1.0.4")) && updated_toggle &&
						!updated_toggle->isChecked(), "the UI shows updated version 1.0.4 disabled before any enable attempt");
					phase = 2;
					QTimer::singleShot(0, &app, [&panel]() {
						if(QCheckBox* const checkbox = panel.findChild<QCheckBox*>(QStringLiteral("pluginEnabledSwitch")))
							checkbox->click();
					});
				});
			}
			else if(elapsed.elapsed() > 9000)
			{
				integration_timed_out = true;
				integration_result = 4;
				app.exit(integration_result);
			}
		}
		else if(phase == 3 && update_trust_cancelled && panel.pluginWorkerProcessStartedCountForTest() == 1 &&
			!panel.pluginRuntimeHasActiveInstanceForTest(old_info.id, QStringLiteral("1.0.4")))
		{
			check(update_trust_prompt_seen, "changed installed-content hash requires a new trust confirmation");
			check(panel.pluginWorkerProcessStartedCountForTest() == 1,
				"cancelling updated-package trust does not start another worker");
			check(heartbeats >= 5, "Qt event loop remains responsive through update and trust cancellation");
			integration_result = 0;
			app.exit(0);
		}
	});
	QTimer hard_deadline;
	hard_deadline.setSingleShot(true);
	QObject::connect(&hard_deadline, &QTimer::timeout, &app, [&]() {
		integration_timed_out = true;
		integration_result = 5;
		app.exit(integration_result);
	});
	QObject::connect(&app, &QCoreApplication::aboutToQuit, &panel, [&panel]() { panel.shutdownPluginRuntime(); });
	heartbeat.start();
	trust_prompt_driver.start();
	poll.start();
	hard_deadline.start(10000);
	elapsed.start();
	QTimer::singleShot(0, &app, [&panel]() {
		if(QCheckBox* const checkbox = panel.findChild<QCheckBox*>(QStringLiteral("pluginEnabledSwitch")))
			checkbox->click();
	});
	const int event_loop_result = app.exec();
	check(!integration_timed_out && event_loop_result == integration_result && integration_result == 0,
		"bounded Qt integration event loop completes before the hard test deadline");
	check(!panel.pluginRuntimeHasActiveInstanceForTest(old_info.id, old_info.version) &&
		!panel.pluginRuntimeHasActiveInstanceForTest(old_info.id, QStringLiteral("1.0.4")),
		"worker resources and active-instance records are cleaned after the error and cancelled update");
	return failures == 0 ? 0 : 1;
}


int pluginProductionWorkerIntegration(QApplication& app, const QString& production_client_executable)
{
	QTemporaryDir temporary;
	check(temporary.isValid(), "production-worker test uses isolated plugin, settings and Qt data roots");
	if(!temporary.isValid()) return 2;
	const QFileInfo executable_info(production_client_executable);
	check(executable_info.isFile() && executable_info.fileName().compare(QStringLiteral("gui_client.exe"), Qt::CaseInsensitive) == 0 &&
		executable_info.absolutePath().contains(QStringLiteral("RelWithDebInfo"), Qt::CaseInsensitive),
		"worker process is the canonical RelWithDebInfo gui_client.exe");
	if(!executable_info.isFile()) return 3;

	const QString package_path = temporary.filePath(QStringLiteral("hello-metasiberia-1.0.4.mspkg"));
	const QString plugin_root = temporary.filePath(QStringLiteral("plugins"));
	const QString settings_root = temporary.filePath(QStringLiteral("settings"));
	const QVector<TestEntry> hello_entries = readPluginProject(QStringLiteral(METASIBERIA_PLUGIN_TEST_HELLO_DIR));
	check(writePackage(package_path, hello_entries), "package the real Hello Metasiberia 1.0.4 panel plugin for the production worker");
	PluginPackageManager package_manager(plugin_root, nullptr, settings_root);
	PluginPackageInfo info;
	PluginPackageInfo install_result;
	QString error;
	const bool package_installed = package_manager.installPackage(package_path, install_result, error);
	const QList<PluginPackageInfo> installed_plugins = package_manager.installedPlugins();
	if(!installed_plugins.isEmpty()) info = installed_plugins.first();
	check(package_installed && info.version == QStringLiteral("1.0.4") && info.installedContentSha256.size() == 32 &&
		info.id == QStringLiteral("org.metasiberia.example.hello") && info.apiCompatible,
		"install Hello 1.0.4 only in the temporary test plugin root; installation leaves it disabled and inert");
	if(!info.installed) return 4;

	const QString preference_directory = package_manager.settingsDirectoryForId(info.id);
	check(!preference_directory.isEmpty() && QDir().mkpath(preference_directory),
		"create the isolated worker trust-preference directory");
	QJsonObject preference;
	preference.insert(QStringLiteral("schemaVersion"), 1);
	preference.insert(QStringLiteral("approvedPackageSha256"), QString::fromLatin1(info.installedContentSha256.toHex()));
	preference.insert(QStringLiteral("enabledVersion"), QString());
	preference.insert(QStringLiteral("enabled"), false);
	QSaveFile preference_file(QDir(preference_directory).filePath(QStringLiteral("runtime.json")));
	const QByteArray preference_data = QJsonDocument(preference).toJson(QJsonDocument::Compact);
	check(preference_file.open(QIODevice::WriteOnly) && preference_file.write(preference_data) == preference_data.size() &&
		preference_file.commit(), "seed exact-content consent in the temporary test settings so the production worker can start");

	const QString isolated_local_app_data = temporary.filePath(QStringLiteral("qt-local-app-data"));
	const QString isolated_roaming_app_data = temporary.filePath(QStringLiteral("qt-roaming-app-data"));
	QDir().mkpath(isolated_local_app_data);
	QDir().mkpath(isolated_roaming_app_data);
	QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
	environment.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
	environment.insert(QStringLiteral("LOCALAPPDATA"), isolated_local_app_data);
	environment.insert(QStringLiteral("APPDATA"), isolated_roaming_app_data);
	QProcess worker;
	worker.setProgram(executable_info.absoluteFilePath());
	worker.setArguments({QStringLiteral("--plugin-runtime-worker"), package_manager.rootDirectory(),
		package_manager.settingsRootDirectory(), info.id, info.version});
	worker.setWorkingDirectory(executable_info.absolutePath());
	worker.setProcessEnvironment(environment);
	worker.setProcessChannelMode(QProcess::SeparateChannels);

	QByteArray standard_output;
	QByteArray standard_error;
	QString worker_error;
	QString protocol_error;
	bool panel_registered = false;
	bool worker_running = false;
	bool notification_received = false;
	bool stop_requested = false;
	bool worker_stopped = false;
	bool process_finished = false;
	bool timed_out = false;
	bool panel_opened = false;
	bool panel_closed_and_reopened = false;
	bool panel_closed_on_disable = false;
	QtPluginHostAdapter host([&](const QString&, const QString& title, const QString& body) {
		notification_received = title == QStringLiteral("Hello Metasiberia") && body == QStringLiteral("Привет, Метасибирь!");
	});
	QMenu plugin_menu;
	QDockWidget* panel_dock = nullptr;
	PluginPanelWidget* panel_widget = nullptr;
	const auto rebuild_test_menu = [&]() {
		plugin_menu.clear();
		if(worker_running)
			for(const PluginPanelRegistration& registration : host.registeredPanels())
				if(registration.pluginId == info.id && registration.pluginVersion == info.version)
				{
					QAction* const action = plugin_menu.addAction(info.name);
					action->setObjectName(QStringLiteral("pluginPrimaryAction"));
					action->setIcon(info.icon);
					QObject::connect(action, &QAction::triggered, &plugin_menu, [&, registration]() {
						if(!panel_dock)
						{
							panel_widget = new PluginPanelWidget(registration.layout);
						panel_dock = new QDockWidget(registration.title);
						panel_dock->setWidget(panel_widget);
						QObject::connect(panel_widget, &PluginPanelWidget::actionTriggered, &plugin_menu,
							[&, registration](const QString& action_id) {
								host.dispatchPanelAction(registration.pluginId, registration.pluginVersion, registration.viewId, action_id);
							});
						panel_dock->show();
					}
					else panel_dock->show();
					panel_dock->raise();
				});
				}
	};
	host.setLifecycleCallbacks(rebuild_test_menu, [&](const QString& plugin_id, const QString& version, const QString&) {
		if(plugin_id == info.id && version == info.version && panel_dock)
		{
			panel_dock->hide();
			panel_closed_on_disable = true;
		}
	});
	rebuild_test_menu();
	QList<QAction*> initial_primary_actions;
	for(QAction* action : plugin_menu.actions())
		if(action->objectName() == QStringLiteral("pluginPrimaryAction")) initial_primary_actions.push_back(action);
	check(initial_primary_actions.isEmpty() && !info.enabled && !info.running,
		"installing the package leaves it disabled and absent from the Plugins menu until worker registration succeeds");
	int exit_code = -1;
	QProcess::ExitStatus exit_status = QProcess::CrashExit;
	int event_loop_heartbeats = 0;
	QEventLoop event_loop;
	QTimer heartbeat;
	heartbeat.setInterval(5);
	QObject::connect(&heartbeat, &QTimer::timeout, &app, [&]() { ++event_loop_heartbeats; });
	const auto send_message = [&worker, &protocol_error](const QJsonObject& message) {
		QByteArray line = QJsonDocument(message).toJson(QJsonDocument::Compact);
		line.append('\n');
		if(worker.write(line) != line.size()) protocol_error = QStringLiteral("Could not write a complete worker IPC message.");
	};
	QObject::connect(&worker, &QProcess::readyReadStandardOutput, &event_loop, [&]() {
		standard_output += worker.readAllStandardOutput();
		for(;;)
		{
			const qsizetype newline = standard_output.indexOf('\n');
			if(newline < 0) break;
			const QByteArray line = standard_output.left(newline);
			standard_output.remove(0, newline + 1);
			QJsonParseError parse_error;
			const QJsonDocument document = QJsonDocument::fromJson(line, &parse_error);
			if(parse_error.error != QJsonParseError::NoError || !document.isObject())
			{
				protocol_error = QStringLiteral("Production worker emitted a non-JSON stdout line.");
				continue;
			}
			const QJsonObject message = document.object();
			const QString type = message.value(QStringLiteral("type")).toString();
			if(type == QStringLiteral("registerPanel"))
			{
				panel_registered = message.value(QStringLiteral("pluginId")).toString() == info.id &&
					message.value(QStringLiteral("pluginVersion")).toString() == info.version &&
					message.value(QStringLiteral("viewId")).toString() == QStringLiteral("main") &&
					message.value(QStringLiteral("title")).toString() == QStringLiteral("Hello Metasiberia") &&
					validatePluginPanelLayout(message.value(QStringLiteral("layout")).toObject(), protocol_error);
				if(panel_registered)
				{
					const QJsonObject layout = message.value(QStringLiteral("layout")).toObject();
					panel_registered = host.registerPanel(info.id, info.version, QStringLiteral("main"),
						QStringLiteral("Hello Metasiberia"), layout,
						[&send_message](const QString& action_id) {
							QJsonObject action;
							action.insert(QStringLiteral("type"), QStringLiteral("invokePanelAction"));
							action.insert(QStringLiteral("viewId"), QStringLiteral("main"));
							action.insert(QStringLiteral("actionId"), action_id);
							send_message(action);
						}, protocol_error);
				}
				if(!panel_registered && protocol_error.isEmpty()) protocol_error = QStringLiteral("Hello 1.0.4 sent unexpected panel registration data.");
				QJsonObject response;
				response.insert(QStringLiteral("type"), QStringLiteral("registerResult"));
				response.insert(QStringLiteral("requestId"), message.value(QStringLiteral("requestId")).toString());
				response.insert(QStringLiteral("ok"), panel_registered);
				response.insert(QStringLiteral("error"), panel_registered ? QString() : protocol_error);
				send_message(response);
			}
			else if(type == QStringLiteral("status"))
			{
				const QString status_error = message.value(QStringLiteral("error")).toString();
				if(!status_error.isEmpty())
				{
					worker_error = status_error;
					if(!stop_requested)
					{
						stop_requested = true;
						worker.closeWriteChannel();
					}
				}
				if(message.value(QStringLiteral("enabled")).toBool(false) && message.value(QStringLiteral("running")).toBool(false))
				{
					worker_running = true;
					rebuild_test_menu();
					if(!panel_opened)
					{
						panel_opened = true;
						QTimer::singleShot(0, &event_loop, [&]() {
							QList<QAction*> primary_actions;
							for(QAction* action : plugin_menu.actions())
								if(action->objectName() == QStringLiteral("pluginPrimaryAction")) primary_actions.push_back(action);
							check(primary_actions.size() == 1, "a running Hello panel contributes exactly one primary Plugins-menu action");
							const QJsonObject second_layout = QJsonObject{{QStringLiteral("type"), QStringLiteral("column")},
								{QStringLiteral("children"), QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
									{QStringLiteral("text"), QStringLiteral("Second plugin")}}}}};
							QString second_plugin_error;
							const bool second_plugin_registered = host.registerPanel(QStringLiteral("org.example.second"), QStringLiteral("1.0.0"),
								QStringLiteral("main"), QStringLiteral("Second plugin"), second_layout,
								[](const QString&) {}, second_plugin_error);
							check(second_plugin_registered && host.registeredPanels().size() == 2,
								"two plugins may use the same local view id without colliding because registrations are plugin-scoped");
							host.removePluginResources(QStringLiteral("org.example.second"), QStringLiteral("1.0.0"));
							primary_actions.clear();
							for(QAction* action : plugin_menu.actions())
								if(action->objectName() == QStringLiteral("pluginPrimaryAction")) primary_actions.push_back(action);
							if(!primary_actions.isEmpty())
							{
								QAction* const menu_action = primary_actions.first();
								menu_action->trigger();
								QApplication::processEvents();
								bool greeting_text_found = false;
								if(panel_widget)
									for(QLabel* label : panel_widget->findChildren<QLabel*>())
										greeting_text_found = greeting_text_found || label->text() == QStringLiteral("Привет, Метасибирь!");
								QPushButton* const greeting_button = panel_widget
									? panel_widget->findChild<QPushButton*>(QStringLiteral("pluginViewAction_say_hello")) : nullptr;
								check(panel_widget && panel_widget->isValid() && greeting_text_found,
									"the native Qt panel renders the greeting supplied by the Luau layout declaration");
								const bool closed_without_disabling = panel_dock && (panel_dock->close(), panel_dock->isHidden()) && worker_running;
								menu_action->trigger();
								QApplication::processEvents();
								panel_closed_and_reopened = closed_without_disabling && panel_dock && panel_dock->isVisible();
								check(panel_closed_and_reopened, "closing a panel leaves the plugin running and its menu action reopens the same view");
								check(greeting_button != nullptr, "the host renders the plugin-declared button as a native Qt control");
								if(greeting_button) greeting_button->click();
							}
							else protocol_error = QStringLiteral("The running panel plugin had no menu action.");
						});
					}
				}
				if(stop_requested && !message.value(QStringLiteral("enabled")).toBool(false) &&
					!message.value(QStringLiteral("running")).toBool(false) && status_error.isEmpty())
					worker_stopped = true;
			}
			else if(type == QStringLiteral("notification"))
			{
				notification_received = message.value(QStringLiteral("pluginId")).toString() == info.id &&
					message.value(QStringLiteral("title")).toString() == QStringLiteral("Hello Metasiberia") &&
					message.value(QStringLiteral("body")).toString() == QStringLiteral("Привет, Метасибирь!");
				if(!notification_received) protocol_error = QStringLiteral("Hello emitted an unexpected notification payload.");
				if(!stop_requested)
				{
					host.removePluginResources(info.id, info.version);
					panel_closed_on_disable = panel_closed_on_disable && (!panel_dock || panel_dock->isHidden());
					QList<QAction*> remaining_actions;
					for(QAction* action : plugin_menu.actions())
						if(action->objectName() == QStringLiteral("pluginPrimaryAction")) remaining_actions.push_back(action);
					check(remaining_actions.isEmpty() && panel_closed_on_disable,
						"disabling removes the primary menu action and closes its open panel");
					stop_requested = true;
					QJsonObject stop;
					stop.insert(QStringLiteral("type"), QStringLiteral("stop"));
					send_message(stop);
					worker.closeWriteChannel();
				}
			}
		}
	});
	QObject::connect(&worker, &QProcess::readyReadStandardError, &event_loop, [&]() {
		standard_error += worker.readAllStandardError();
		if(standard_error.size() > 8192) standard_error = standard_error.right(8192);
	});
	QObject::connect(&worker, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), &event_loop,
		[&](const int code, const QProcess::ExitStatus status) {
			process_finished = true;
			exit_code = code;
			exit_status = status;
			event_loop.quit();
		});
	QObject::connect(&worker, &QProcess::errorOccurred, &event_loop, [&](const QProcess::ProcessError process_error) {
		if(process_error == QProcess::FailedToStart)
		{
			protocol_error = worker.errorString();
			event_loop.quit();
		}
	});
	QTimer deadline;
	deadline.setSingleShot(true);
	QObject::connect(&deadline, &QTimer::timeout, &event_loop, [&]() {
		timed_out = true;
		if(worker.state() != QProcess::NotRunning) worker.kill();
		event_loop.quit();
	});
	heartbeat.start();
	worker.start(QIODevice::ReadWrite);
	deadline.start(12000);
	event_loop.exec();
	heartbeat.stop();
	if(worker.state() != QProcess::NotRunning)
	{
		worker.kill();
		QEventLoop reap_loop;
		QTimer reap_deadline;
		reap_deadline.setSingleShot(true);
		QObject::connect(&worker, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), &reap_loop,
			[&reap_loop](int, QProcess::ExitStatus) { reap_loop.quit(); });
		QObject::connect(&reap_deadline, &QTimer::timeout, &reap_loop, [&reap_loop]() { reap_loop.quit(); });
		reap_deadline.start(2000);
		reap_loop.exec();
	}
	check(!timed_out, "production worker handshake and shutdown complete before the 12-second deadline");
	check(protocol_error.isEmpty(), "production worker IPC uses the expected bounded JSON messages");
	check(worker_error.isEmpty(), worker_error.isEmpty() ? "production build accepts the dedicated worker execution context" : "production worker reported a runtime error");
	check(panel_registered, "the production worker registers Hello 1.0.4's ui.registerPanel view over IPC");
	check(worker_running, "Hello reaches Running through the production worker process");
	check(panel_closed_and_reopened, "the native panel closes and reopens without stopping its worker");
	check(notification_received, "the panel button dispatches its action to Luau and returns ui.showNotification through IPC");
	check(panel_closed_on_disable, "disabling the plugin removes its panel registration and closes its view");
	check(worker_stopped && process_finished && exit_status == QProcess::NormalExit && exit_code == 0,
		"the production worker acknowledges disable and exits cleanly");
	check(event_loop_heartbeats >= 2, "the test-side Qt event loop remains responsive while the production worker runs");
	if(!worker_error.isEmpty()) std::cerr << "Production worker status error: " << worker_error.toStdString() << std::endl;
	if(!protocol_error.isEmpty()) std::cerr << "Production worker protocol error: " << protocol_error.toStdString() << std::endl;
	if(!standard_error.trimmed().isEmpty() && failures > 0)
		std::cerr << "Production worker stderr (diagnostic): " << standard_error.right(1000).toStdString() << std::endl;
	if(panel_dock) delete panel_dock;
	Q_UNUSED(app);
	return failures == 0 ? 0 : 1;
}


int pluginSceneInfoWorkerIntegration(QApplication& app, const QString& production_client_executable)
{
	QTemporaryDir temporary;
	check(temporary.isValid(), "scene-info worker test uses isolated package, settings and Qt data roots");
	if(!temporary.isValid()) return 2;
	const QFileInfo executable_info(production_client_executable);
	check(executable_info.isFile() && executable_info.fileName().compare(QStringLiteral("gui_client.exe"), Qt::CaseInsensitive) == 0 &&
		executable_info.absolutePath().contains(QStringLiteral("RelWithDebInfo"), Qt::CaseInsensitive),
		"scene-info test launches the canonical RelWithDebInfo client worker");
	if(!executable_info.isFile()) return 3;

	const QString package_path = temporary.filePath(QStringLiteral("scene-info-1.0.0.mspkg"));
	QVector<TestEntry> entries = luauEntries(QByteArrayLiteral(
		"ui.registerCommand('show_scene_summary', 'Scene Information', function() "
		"local s = scene.getSummary(); local connected = s.connected and 'Connected' or 'Not connected'; "
		"local body = connected .. '\\nServer: ' .. s.server.host .. '\\nWorld: ' .. s.server.world .. "
		"string.format('\\nCamera: %.2f, %.2f, %.2f', s.camera.x, s.camera.y, s.camera.z); "
		"ui.showNotification('Scene Information', body) end)"),
		{QStringLiteral("ui.command"), QStringLiteral("ui.notification"), QStringLiteral("scene.read.summary")});
	QJsonObject manifest_object = QJsonDocument::fromJson(entries[0].contents).object();
	manifest_object.insert(QStringLiteral("id"), QStringLiteral("org.metasiberia.examples.scene-info"));
	manifest_object.insert(QStringLiteral("name"), QStringLiteral("Scene Information"));
	manifest_object.insert(QStringLiteral("api"), QJsonObject{{QStringLiteral("major"), 1}, {QStringLiteral("minor"), 2}});
	entries[0].contents = QJsonDocument(manifest_object).toJson(QJsonDocument::Compact);
	check(writePackage(package_path, entries), "package a Luau scene-info plugin using API v1.2");

	PluginPackageManager manager(temporary.filePath(QStringLiteral("plugins")), nullptr, temporary.filePath(QStringLiteral("settings")));
	PluginPackageInfo installed;
	QString error;
	check(manager.installPackage(package_path, installed, error) && installed.apiCompatible && installed.apiMinor == 2,
		"install the scene-info package as inert files in the isolated test root");
	if(!installed.installed) return 4;
	// installPackage's immediate result is package metadata; the reloaded installed
	// catalog carries the content hash that the worker independently verifies.
	const QList<PluginPackageInfo> installed_packages = manager.installedPlugins();
	check(installed_packages.size() == 1 && installed_packages.first().id == installed.id &&
		installed_packages.first().version == installed.version && installed_packages.first().installedContentSha256.size() == 32,
		"read the verified installed-content identity from the catalog before seeding test consent");
	if(installed_packages.size() != 1 || installed_packages.first().installedContentSha256.size() != 32) return 4;
	installed = installed_packages.first();
	const QString preference_directory = manager.settingsDirectoryForId(installed.id);
	check(QDir().mkpath(preference_directory), "create isolated scene-info trust state");
	QJsonObject preference{{QStringLiteral("schemaVersion"), 1},
		{QStringLiteral("approvedPackageSha256"), QString::fromLatin1(installed.installedContentSha256.toHex())},
		{QStringLiteral("enabledVersion"), QString()}, {QStringLiteral("enabled"), false}};
	QSaveFile preference_file(QDir(preference_directory).filePath(QStringLiteral("runtime.json")));
	const QByteArray preference_bytes = QJsonDocument(preference).toJson(QJsonDocument::Compact);
	check(preference_file.open(QIODevice::WriteOnly) && preference_file.write(preference_bytes) == preference_bytes.size() &&
		preference_file.commit(), "seed exact-hash consent only inside the temporary test directory");

	const QString local_data = temporary.filePath(QStringLiteral("local-data"));
	const QString roaming_data = temporary.filePath(QStringLiteral("roaming-data"));
	QDir().mkpath(local_data);
	QDir().mkpath(roaming_data);
	QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
	environment.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
	environment.insert(QStringLiteral("LOCALAPPDATA"), local_data);
	environment.insert(QStringLiteral("APPDATA"), roaming_data);
	QProcess worker;
	worker.setProgram(executable_info.absoluteFilePath());
	worker.setArguments({QStringLiteral("--plugin-runtime-worker"), manager.rootDirectory(), manager.settingsRootDirectory(), installed.id, installed.version});
	worker.setWorkingDirectory(executable_info.absolutePath());
	worker.setProcessEnvironment(environment);
	worker.setProcessChannelMode(QProcess::SeparateChannels);

	const QJsonObject fixture_summary{
		{QStringLiteral("connected"), true},
		{QStringLiteral("server"), QJsonObject{{QStringLiteral("host"), QStringLiteral("vr.test.invalid")},
			{QStringLiteral("world"), QStringLiteral("Plugin API Test")}}},
		{QStringLiteral("camera"), QJsonObject{{QStringLiteral("x"), 12.5}, {QStringLiteral("y"), -4.25}, {QStringLiteral("z"), 3.0}}}
	};
	QtPluginHostAdapter host([](const QString&, const QString&, const QString&) {});
	int summary_provider_calls = 0;
	host.setSceneSummaryProvider([&](const QString& plugin_id, const QString& version, QJsonObject& summary, QString& provider_error) {
		if(plugin_id != installed.id || version != installed.version)
		{
			provider_error = QStringLiteral("Plugin identity mismatch in test host provider.");
			return false;
		}
		++summary_provider_calls;
		summary = fixture_summary;
		return true;
	});

	QByteArray output;
	QByteArray standard_error;
	QString protocol_error;
	QString worker_error;
	bool registered = false;
	bool running = false;
	bool scene_request_seen = false;
	bool notification_seen = false;
	bool stop_sent = false;
	bool stopped_status_seen = false;
	bool process_finished = false;
	bool timed_out = false;
	int exit_code = -1;
	QProcess::ExitStatus exit_status = QProcess::CrashExit;
	QEventLoop loop;
	QTimer heartbeats;
	heartbeats.setInterval(5);
	int heartbeat_count = 0;
	QObject::connect(&heartbeats, &QTimer::timeout, &app, [&]() { ++heartbeat_count; });
	const auto send = [&](QJsonObject message) {
		message.insert(QStringLiteral("protocolMajor"), 1);
		message.insert(QStringLiteral("protocolMinor"), 0);
		QByteArray line = QJsonDocument(message).toJson(QJsonDocument::Compact);
		line.append('\n');
		if(worker.write(line) != line.size()) protocol_error = QStringLiteral("Could not write a complete scene-info protocol message.");
	};
	QObject::connect(&worker, &QProcess::readyReadStandardOutput, &loop, [&]() {
		output += worker.readAllStandardOutput();
		for(;;)
		{
			const qsizetype newline = output.indexOf('\n');
			if(newline < 0) break;
			const QByteArray line = output.left(newline);
			output.remove(0, newline + 1);
			const QJsonDocument doc = QJsonDocument::fromJson(line);
			if(!doc.isObject()) { protocol_error = QStringLiteral("Scene-info worker emitted invalid JSON."); continue; }
			const QJsonObject message = doc.object();
			if(message.value(QStringLiteral("protocolMajor")).toInt(-1) != 1)
				protocol_error = QStringLiteral("Scene-info worker omitted or mismatched protocol major version.");
			const QString type = message.value(QStringLiteral("type")).toString();
			if(type == QStringLiteral("registerCommand"))
			{
				QString registration_error;
				registered = message.value(QStringLiteral("pluginId")).toString() == installed.id &&
					message.value(QStringLiteral("pluginVersion")).toString() == installed.version &&
					message.value(QStringLiteral("commandId")).toString() == QStringLiteral("show_scene_summary") &&
					host.registerCommand(installed.id, installed.version, QStringLiteral("show_scene_summary"),
						QStringLiteral("Scene Information"), []() {}, registration_error);
				if(!registered && !registration_error.isEmpty()) protocol_error = registration_error;
				send(QJsonObject{{QStringLiteral("type"), QStringLiteral("registerResult")},
					{QStringLiteral("requestId"), message.value(QStringLiteral("requestId"))},
					{QStringLiteral("ok"), registered},
					{QStringLiteral("error"), registration_error}});
			}
			else if(type == QStringLiteral("status"))
			{
				const QString status_error = message.value(QStringLiteral("error")).toString();
				if(!status_error.isEmpty()) worker_error = status_error;
				if(message.value(QStringLiteral("running")).toBool(false) && !running)
				{
					running = true;
					send(QJsonObject{{QStringLiteral("type"), QStringLiteral("invoke")}});
				}
				if(stop_sent && !message.value(QStringLiteral("enabled")).toBool(false) &&
					!message.value(QStringLiteral("running")).toBool(false))
					stopped_status_seen = true;
			}
			else if(type == QStringLiteral("sceneSummary"))
			{
				QString summary_error;
				QJsonObject summary;
				const bool identity_ok = message.value(QStringLiteral("pluginId")).toString() == installed.id &&
					message.value(QStringLiteral("pluginVersion")).toString() == installed.version;
				const bool provided = identity_ok && host.getSceneSummary(installed.id, installed.version, summary, summary_error);
				scene_request_seen = provided && summary == fixture_summary;
				send(QJsonObject{{QStringLiteral("type"), QStringLiteral("sceneSummaryResult")},
					{QStringLiteral("requestId"), message.value(QStringLiteral("requestId"))},
					{QStringLiteral("ok"), provided}, {QStringLiteral("summary"), summary},
					{QStringLiteral("error"), summary_error}});
			}
			else if(type == QStringLiteral("notification"))
			{
				const QString body = message.value(QStringLiteral("body")).toString();
				notification_seen = message.value(QStringLiteral("pluginId")).toString() == installed.id &&
					message.value(QStringLiteral("title")).toString() == QStringLiteral("Scene Information") &&
					body.contains(QStringLiteral("Server: vr.test.invalid")) &&
					body.contains(QStringLiteral("World: Plugin API Test")) &&
					body.contains(QStringLiteral("Camera: 12.50, -4.25, 3.00"));
				stop_sent = true;
				send(QJsonObject{{QStringLiteral("type"), QStringLiteral("stop")}});
				worker.closeWriteChannel();
			}
		}
	});
	QObject::connect(&worker, &QProcess::readyReadStandardError, &loop, [&]() {
		standard_error += worker.readAllStandardError();
		if(standard_error.size() > 4096) standard_error = standard_error.right(4096);
	});
	QObject::connect(&worker, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), &loop,
		[&](int code, QProcess::ExitStatus status) { process_finished = true; exit_code = code; exit_status = status; loop.quit(); });
	QTimer deadline;
	deadline.setSingleShot(true);
	QObject::connect(&deadline, &QTimer::timeout, &loop, [&]() { timed_out = true; worker.kill(); loop.quit(); });
	heartbeats.start();
	worker.start(QIODevice::ReadWrite);
	deadline.start(12000);
	loop.exec();
	heartbeats.stop();
	if(worker.state() != QProcess::NotRunning)
	{
		worker.kill();
		QEventLoop reap;
		QTimer reap_deadline;
		reap_deadline.setSingleShot(true);
		QObject::connect(&worker, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), &reap,
			[&reap](int, QProcess::ExitStatus) { reap.quit(); });
		QObject::connect(&reap_deadline, &QTimer::timeout, &reap, &QEventLoop::quit);
		reap_deadline.start(1500);
		reap.exec();
	}
	check(!timed_out, "scene-info command and worker shutdown complete before the hard deadline");
	check(protocol_error.isEmpty(), "scene-info uses protocol v1 JSON Lines messages");
	check(worker_error.isEmpty(), "production worker completed without reporting a runtime error");
	check(registered && running, "scene-info registers a command and reaches Running through production worker");
	check(scene_request_seen && summary_provider_calls == 1, "scene.getSummary reaches the Qt host provider exactly once with bounded DTO");
	check(notification_seen, "scene-info formats the real host DTO and returns it through ui.showNotification");
	check(stopped_status_seen, "the production worker reports disabled and stopped before process exit");
	check(heartbeat_count >= 2, "Qt event loop remains responsive during scene summary IPC");
	check(process_finished && exit_status == QProcess::NormalExit && exit_code == 0,
		"the production worker exits cleanly after disable");
	if(!protocol_error.isEmpty()) std::cerr << "Scene-info protocol error: " << protocol_error.toStdString() << std::endl;
	if(!worker_error.isEmpty()) std::cerr << "Scene-info worker error: " << worker_error.toStdString() << std::endl;
	if(!standard_error.trimmed().isEmpty() && failures > 0)
		std::cerr << "Scene-info worker stderr: " << standard_error.right(1000).toStdString() << std::endl;
	Q_UNUSED(app);
	return failures == 0 ? 0 : 1;
}
}


int main(int argc, char** argv)
{
	if(qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
	QApplication app(argc, argv);
	if(argc >= 2 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--plugin-runtime-worker"))
		return PluginRuntime::runWorkerProcess(app.arguments());
	if(argc >= 2 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--plugin-ui-event-loop-test"))
		return pluginUiEventLoopIntegration(app);
	if(argc >= 3 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--plugin-production-worker-test"))
		return pluginProductionWorkerIntegration(app, QString::fromLocal8Bit(argv[2]));
	if(argc >= 3 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--plugin-scene-info-worker-test"))
		return pluginSceneInfoWorkerIntegration(app, QString::fromLocal8Bit(argv[2]));
	if(argc == 3 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--capture-ui"))
		return captureUi(QString::fromLocal8Bit(argv[2]), QStringLiteral(METASIBERIA_PLUGIN_TEST_ICON_DIR));
	if(argc > 1)
	{
		PluginPackageInfo package_info;
		QString validation_error;
		if(!PluginPackageValidator::inspect(QString::fromLocal8Bit(argv[1]), package_info, validation_error))
		{
			std::cerr << "PACKAGE REJECTED: " << validation_error.toStdString() << std::endl;
			return 1;
		}
		std::cout << "PACKAGE VALID: " << package_info.id.toStdString() << " " << package_info.version.toStdString()
			<< " runtime=" << package_info.runtime.toStdString() << " (inert; not executed)" << std::endl;
		return 0;
	}
	QTemporaryDir temporary;
	check(temporary.isValid(), "temporary test root is available");
	if(!temporary.isValid()) return 1;
	const QString root = temporary.filePath(QStringLiteral("plugins"));
	const QString settings_root = temporary.filePath(QStringLiteral("settings"));
	const QString packages = temporary.filePath(QStringLiteral("packages"));
	QDir().mkpath(packages);

	QString error;
	PluginPackageInfo info;
	const QString valid_package = QDir(packages).filePath(QStringLiteral("hello.mspkg"));
	check(writePackage(valid_package, validEntries()), "build valid .mspkg fixture");
	check(PluginPackageValidator::inspect(valid_package, info, error) && info.apiCompatible && info.runtime == QStringLiteral("luau") &&
		info.packageSha256.size() == 32 && info.iconPath == QStringLiteral("assets/plugin.png") && !info.icon.isNull(),
		"valid package manifest, API and bounded raster icon are accepted");
	check(PluginPackageValidator::compareVersions(QStringLiteral("1.10.0"), QStringLiteral("1.9.9")) > 0 &&
		PluginPackageValidator::compareVersions(QStringLiteral("1.0.0"), QStringLiteral("1.0.0-rc.10")) > 0 &&
		PluginPackageValidator::compareVersions(QStringLiteral("1.0.0-rc.2"), QStringLiteral("1.0.0-rc.10")) < 0 &&
		PluginPackageValidator::compareVersions(QStringLiteral("1.0.0+first"), QStringLiteral("1.0.0+second")) == 0,
		"catalog semantic-version ordering follows core, prerelease and build metadata rules");
	QVector<TestEntry> svg_entries = validEntries();
	QJsonObject svg_manifest = QJsonDocument::fromJson(svg_entries[0].contents).object();
	svg_manifest.insert(QStringLiteral("icon"), QStringLiteral("assets/hello.svg"));
	svg_entries[0].contents = QJsonDocument(svg_manifest).toJson(QJsonDocument::Compact);
	svg_entries.push_back({QByteArrayLiteral("assets/hello.svg"), tinySvg(), 0100644});
	const QString svg_package = QDir(packages).filePath(QStringLiteral("svg-icon.mspkg"));
	check(writePackage(svg_package, svg_entries) && PluginPackageValidator::inspect(svg_package, info, error) &&
		info.iconPath == QStringLiteral("assets/hello.svg") && !info.icon.isNull() && !info.icon.pixmap(24, 24).isNull(),
		"manifest-based SVG plugin icons are validated and rasterized for Qt UI");
	const QString svg_install_root = temporary.filePath(QStringLiteral("svg-installed-plugins"));
	PluginPackageManager svg_manager(svg_install_root, nullptr, temporary.filePath(QStringLiteral("svg-installed-settings")));
	PluginPackageInfo svg_installed;
	check(svg_manager.installPackage(svg_package, svg_installed, error) &&
		svg_manager.installedPlugins().first().iconPath == QStringLiteral("assets/hello.svg") &&
		!svg_manager.installedPlugins().first().icon.isNull(),
		"installed third-party packages load their manifest SVG without adding it to client resources");
	QVector<TestEntry> unsafe_svg_entries = svg_entries;
	unsafe_svg_entries.last().contents = QByteArrayLiteral("<svg xmlns=\"http://www.w3.org/2000/svg\"><script>alert(1)</script></svg>");
	const QString unsafe_svg_package = QDir(packages).filePath(QStringLiteral("unsafe-svg-icon.mspkg"));
	check(writePackage(unsafe_svg_package, unsafe_svg_entries) && !PluginPackageValidator::inspect(unsafe_svg_package, info, error),
		"SVG plugin icons reject active markup before rendering");

	QVector<TestEntry> incompatible_entries = validEntries(2);
	const QString incompatible = QDir(packages).filePath(QStringLiteral("incompatible.mspkg"));
	check(writePackage(incompatible, incompatible_entries) && PluginPackageValidator::inspect(incompatible, info, error) && !info.apiCompatible,
		"incompatible API package is identified without execution");
	PluginPackageManager manager(root, nullptr, settings_root);
	check(!manager.installPackage(incompatible, info, error), "incompatible API package cannot be installed");
	check(!manager.installPackage(valid_package, info, error, QByteArray(64, '0')),
		"package hash must still match the reviewed package before installation");

	QVector<TestEntry> traversal = validEntries();
	traversal.push_back({QByteArrayLiteral("../escaped.txt"), QByteArrayLiteral("no"), 0100644});
	const QString dangerous = QDir(packages).filePath(QStringLiteral("traversal.mspkg"));
	check(writePackage(dangerous, traversal) && !PluginPackageValidator::inspect(dangerous, info, error),
		"path traversal entry is rejected");
	QVector<TestEntry> reserved_path = validEntries();
	reserved_path.push_back({QByteArrayLiteral(".metasiberia-install.json"), QByteArrayLiteral("not metadata"), 0100644});
	const QString reserved_package = QDir(packages).filePath(QStringLiteral("reserved-metadata.mspkg"));
	check(writePackage(reserved_package, reserved_path) && !PluginPackageValidator::inspect(reserved_package, info, error),
		"package cannot overwrite the install-integrity manifest");

	QVector<TestEntry> duplicate = validEntries();
	duplicate.push_back({QByteArrayLiteral("MAIN.LUAU"), QByteArrayLiteral("duplicate"), 0100644});
	const QString duplicate_path = QDir(packages).filePath(QStringLiteral("duplicate-path.mspkg"));
	check(writePackage(duplicate_path, duplicate) && !PluginPackageValidator::inspect(duplicate_path, info, error),
		"case-insensitive duplicate package path is rejected");

	QVector<TestEntry> symlink = validEntries();
	symlink.push_back({QByteArrayLiteral("link"), QByteArrayLiteral("target"), 0120777});
	const QString symlink_path = QDir(packages).filePath(QStringLiteral("symlink.mspkg"));
	check(writePackage(symlink_path, symlink) && !PluginPackageValidator::inspect(symlink_path, info, error),
		"symbolic-link archive entry is rejected");

	const QString corrupt = QDir(packages).filePath(QStringLiteral("corrupt.mspkg"));
	QByteArray corrupt_data = makeZip(validEntries());
	const int payload = corrupt_data.indexOf(QByteArrayLiteral("org.metasiberia.test.hello"));
	if(payload >= 0) corrupt_data[payload] = static_cast<char>(corrupt_data[payload] ^ 0x01);
	QFile corrupt_file(corrupt);
	const bool wrote_corrupt = corrupt_file.open(QIODevice::WriteOnly) && corrupt_file.write(corrupt_data) == corrupt_data.size();
	corrupt_file.close();
	check(wrote_corrupt && !PluginPackageValidator::inspect(corrupt, info, error), "corrupt entry fails CRC validation");

	check(manager.installPackage(valid_package, info, error), "verified local package installs atomically");
	QList<PluginPackageInfo> installed = manager.installedPlugins();
	check(installed.size() == 1 && installed[0].installed && !installed[0].enabled && !installed[0].running &&
		installed[0].state.contains(QStringLiteral("disabled")),
		"installed package remains installed, disabled and non-running");
	const QString marker = temporary.filePath(QStringLiteral("execution-marker"));
	check(!QFileInfo::exists(marker), "installation does not execute the declared Luau entry point");
	check(!manager.installPackage(valid_package, info, error), "duplicate id/version install is rejected");
	QVector<TestEntry> same_version_different_contents = validEntries();
	QJsonObject same_version_manifest = QJsonDocument::fromJson(same_version_different_contents[0].contents).object();
	same_version_manifest.insert(QStringLiteral("description"), QStringLiteral("Different content with an unchanged version."));
	same_version_different_contents[0].contents = QJsonDocument(same_version_manifest).toJson(QJsonDocument::Compact);
	const QString same_version_package = QDir(packages).filePath(QStringLiteral("same-version-different-hash.mspkg"));
	check(writePackage(same_version_package, same_version_different_contents) &&
		!manager.installPackage(same_version_package, info, error) &&
		error.contains(QStringLiteral("increase the version")) && manager.installedPlugins().size() == 1 &&
		manager.installedPlugins().first().packageSha256 == installed.first().packageSha256,
		"a changed package hash cannot silently replace files under the same plugin version");

	PluginPackageManager reloaded(root, nullptr, settings_root);
	check(reloaded.installedPlugins().size() == 1, "installed catalog reloads after restart");
	const QString installed_script_path = QDir(installed[0].installPath).filePath(QStringLiteral("main.luau"));
	QFile script(installed_script_path);
	check(script.open(QIODevice::WriteOnly | QIODevice::Truncate) && script.write("changed") == 7, "tamper fixture modifies installed file");
	script.close();
	reloaded.reload();
	check(reloaded.installedPlugins().isEmpty(), "modified installed package fails integrity validation");
	const QByteArray original_script = QByteArrayLiteral("write_marker_if_executed() -- inert payload");
	check(script.open(QIODevice::WriteOnly | QIODevice::Truncate) && script.write(original_script) == original_script.size(), "restore fixture package content");
	script.close();
	reloaded.reload();
	check(reloaded.installedPlugins().size() == 1, "restored package passes integrity validation");

	const QByteArray runtime_script = QByteArrayLiteral(
		"ui.registerCommand('hello', 'Hello Metasiberia', function() "
		"ui.showNotification('Hello Metasiberia', 'Привет, Метасибирь!') end)");
	const QString runtime_package = QDir(packages).filePath(QStringLiteral("hello-runtime.mspkg"));
	const QStringList runtime_permissions = {QStringLiteral("ui.command"), QStringLiteral("ui.notification")};
	check(writePackage(runtime_package, luauEntries(runtime_script, runtime_permissions)),
		"build a Luau package fixture with only command and notification capabilities");
	const QString runtime_root = temporary.filePath(QStringLiteral("runtime-plugins"));
	const QString runtime_settings = temporary.filePath(QStringLiteral("runtime-settings"));
	PluginPackageManager runtime_manager(runtime_root, nullptr, runtime_settings);
	PluginPackageInfo runtime_info;
	check(runtime_manager.installPackage(runtime_package, runtime_info, error), "install a Luau package without executing its entrypoint");
	FakePluginHost fake_host;
	PluginRuntime runtime(runtime_manager, false);
	runtime.setHost(&fake_host);
	check(!runtime.enable(runtime_info.id, runtime_info.version, false, error) &&
		fake_host.registeredCommands().isEmpty() && !runtime.status(runtime_info.id, runtime_info.version).running,
		"an installed package cannot start without explicit hash trust");
	const QByteArray originally_trusted_content = runtime_manager.installedPlugins().first().installedContentSha256;
	check(runtime.enable(runtime_info.id, runtime_info.version, true, error) &&
		waitFor([&]() { return fake_host.registeredCommands().size() == 1 && runtime.status(runtime_info.id, runtime_info.version).running; }),
		"explicitly trusted local Luau package starts asynchronously and registers its command");
	check(runtime.disable(runtime_info.id, runtime_info.version, error), "stop the initially trusted package before integrity mutation test");
	PluginPackageInfo current_runtime_info = runtime_manager.installedPlugins().first();
	QFile installed_script(QDir(current_runtime_info.installPath).filePath(QStringLiteral("main.luau")));
	QFile install_metadata(QDir(current_runtime_info.installPath).filePath(QStringLiteral(".metasiberia-install.json")));
	QByteArray updated_script;
	QJsonObject install_metadata_object;
	if(installed_script.open(QIODevice::ReadOnly)) updated_script = installed_script.readAll() + QByteArrayLiteral("\n-- local file changed\n");
	installed_script.close();
	QJsonParseError metadata_parse_error;
	if(install_metadata.open(QIODevice::ReadOnly))
		install_metadata_object = QJsonDocument::fromJson(install_metadata.readAll(), &metadata_parse_error).object();
	install_metadata.close();
	QJsonObject installed_hashes = install_metadata_object.value(QStringLiteral("files")).toObject();
	installed_hashes.insert(QStringLiteral("main.luau"), QString::fromLatin1(QCryptographicHash::hash(updated_script, QCryptographicHash::Sha256).toHex()));
	install_metadata_object.insert(QStringLiteral("files"), installed_hashes);
	const bool changed_content_written = !updated_script.isEmpty() &&
		installed_script.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
		installed_script.write(updated_script) == updated_script.size();
	installed_script.close();
	const bool changed_metadata_written = install_metadata.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
		install_metadata.write(QJsonDocument(install_metadata_object).toJson(QJsonDocument::Compact)) > 0;
	install_metadata.close();
	runtime_manager.reload();
	current_runtime_info = runtime_manager.installedPlugins().first();
	check(changed_content_written && changed_metadata_written &&
		current_runtime_info.installedContentSha256 != originally_trusted_content && runtime.requiresExplicitTrust(runtime_info.id, runtime_info.version) &&
		!runtime.enable(runtime_info.id, runtime_info.version, false, error) && fake_host.registeredCommands().isEmpty(),
		"changed installed content requires fresh trust even if its local file-hash manifest was edited to match");
	check(runtime.enable(runtime_info.id, runtime_info.version, true, error) &&
		waitFor([&]() { return fake_host.registeredCommands().size() == 1 && runtime.status(runtime_info.id, runtime_info.version).running; }),
		"the package starts after its changed content receives fresh explicit trust");
	check(runtime.enable(runtime_info.id, runtime_info.version, false, error) && fake_host.registeredCommands().size() == 1,
		"repeated enable is idempotent and does not register duplicate commands");
	PluginCommandRegistration stale_registration = fake_host.registeredCommands().first();
	stale_registration.callback();
	check(waitFor([&]() { return fake_host.notificationSnapshot().size() == 1; }) &&
		fake_host.notificationSnapshot().first().title == QStringLiteral("Hello Metasiberia") &&
		fake_host.notificationSnapshot().first().body == QStringLiteral("Привет, Метасибирь!") &&
		fake_host.notificationThreadSnapshot() != app.thread(),
		"a real Luau command runs off the UI thread and invokes the bounded notification host API");
	check(runtime.disable(runtime_info.id, runtime_info.version, error) && fake_host.registeredCommands().isEmpty() &&
		!runtime.status(runtime_info.id, runtime_info.version).running,
		"disable removes the command immediately and asks its worker to close the VM");
	const int notifications_before_stale = fake_host.notificationSnapshot().size();
	stale_registration.callback();
	check(fake_host.notificationSnapshot().size() == notifications_before_stale,
		"a stale callback retained by the test cannot execute after disable");
	check(waitFor([&]() { return runtime.enable(runtime_info.id, runtime_info.version, false, error); }) &&
		waitFor([&]() { return fake_host.registeredCommands().size() == 1 && runtime.status(runtime_info.id, runtime_info.version).running; }),
		"a previously trusted package can be re-enabled without asking for trust again");
	runtime.shutdown();
	check(fake_host.registeredCommands().isEmpty(), "runtime shutdown unregisters commands while preserving enabled preference");
	FakePluginHost restored_host;
	PluginRuntime restored_runtime(runtime_manager, false);
	restored_runtime.setHost(&restored_host);
	restored_runtime.restorePreviouslyEnabledLocalPlugins();
	check(waitFor([&]() { return restored_runtime.status(runtime_info.id, runtime_info.version).running &&
		restored_host.registeredCommands().size() == 1; }),
		"only a matching previously approved local hash is restored at startup");
	check(restored_runtime.disable(runtime_info.id, runtime_info.version, error), "restored plugin can be explicitly disabled");

	const QString isolated_root = temporary.filePath(QStringLiteral("isolated-runtime-plugins"));
	const QString isolated_settings = temporary.filePath(QStringLiteral("isolated-runtime-settings"));
	PluginPackageManager isolated_manager(isolated_root, nullptr, isolated_settings);
	PluginPackageInfo isolated_info;
	check(isolated_manager.installPackage(runtime_package, isolated_info, error), "install package for child-process runtime test");
	FakePluginHost isolated_host;
	PluginRuntime isolated_runtime(isolated_manager, true);
	isolated_runtime.setHost(&isolated_host);
	const bool isolated_enabled = isolated_runtime.enable(isolated_info.id, isolated_info.version, true, error);
	const bool isolated_started = isolated_enabled &&
		waitFor([&]() { return isolated_runtime.status(isolated_info.id, isolated_info.version).running &&
			isolated_host.registeredCommands().size() == 1; }, 10000);
	if(!isolated_started)
	{
		const PluginRuntimeStatus status = isolated_runtime.status(isolated_info.id, isolated_info.version);
		std::cerr << "Command child worker diagnostics: enableError=" << error.toStdString()
			<< " statusEnabled=" << status.enabled << " statusRunning=" << status.running
			<< " statusError=" << status.error.toStdString() << " active="
			<< isolated_runtime.hasActiveInstance(isolated_info.id, isolated_info.version) << std::endl;
	}
	check(isolated_started,
		"production runtime starts Luau in a supervised child process and registers the command over bounded JSONL IPC");
	if(!isolated_host.registeredCommands().isEmpty()) isolated_host.registeredCommands().first().callback();
	check(waitFor([&]() { return isolated_host.notificationSnapshot().size() == 1; }, 5000) &&
		isolated_host.notificationSnapshot().first().body == QStringLiteral("Привет, Метасибирь!") &&
		isolated_host.notificationThreadSnapshot() == app.thread(),
		"child-process command invokes the documented notification API on the Qt UI thread");
	check(isolated_runtime.disable(isolated_info.id, isolated_info.version, error) &&
		waitFor([&]() { return !isolated_runtime.hasActiveInstance(isolated_info.id, isolated_info.version); }, 5000) &&
		isolated_host.registeredCommands().isEmpty(),
		"isolated plugin stops asynchronously, closes IPC and removes its command without joining on the UI thread");

	const QByteArray runaway_script = QByteArrayLiteral(
		"ui.registerCommand('runaway', 'Runaway', function() while true do end end)");
	const QString runaway_package = QDir(packages).filePath(QStringLiteral("isolated-runaway.mspkg"));
	QVector<TestEntry> isolated_runaway_entries = luauEntries(runaway_script, runtime_permissions);
	QJsonObject isolated_runaway_manifest = QJsonDocument::fromJson(isolated_runaway_entries[0].contents).object();
	isolated_runaway_manifest.insert(QStringLiteral("version"), QStringLiteral("1.0.1"));
	isolated_runaway_entries[0].contents = QJsonDocument(isolated_runaway_manifest).toJson(QJsonDocument::Compact);
	check(writePackage(runaway_package, isolated_runaway_entries), "build isolated runaway fixture package");
	PluginPackageInfo runaway_info;
	check(isolated_manager.installPackage(runaway_package, runaway_info, error), "install isolated runaway fixture");
	check(isolated_runtime.enable(runaway_info.id, runaway_info.version, true, error) &&
		waitFor([&]() { return isolated_runtime.status(runaway_info.id, runaway_info.version).running; }, 10000),
		"isolated runaway fixture starts only after the user trusts its package hash");
	if(!isolated_host.registeredCommands().isEmpty()) isolated_host.registeredCommands().first().callback();
	check(waitFor([&]() { return !isolated_runtime.status(runaway_info.id, runaway_info.version).error.isEmpty(); }, 5000),
		"an infinite Luau callback is stopped by the execution budget and reported by the supervisor");
	check(waitFor([&]() { return !isolated_runtime.hasActiveInstance(runaway_info.id, runaway_info.version); }, 5000),
		"runtime failure exits its worker process and leaves the client-side supervisor responsive");
	QVector<TestEntry> stop_during_callback_entries = luauEntries(QByteArrayLiteral(
		"ui.registerCommand('busy', 'Busy command', function() "
		"ui.showNotification('busy', 'callback entered'); while true do end end)"), runtime_permissions);
	QJsonObject stop_during_callback_manifest = QJsonDocument::fromJson(stop_during_callback_entries[0].contents).object();
	stop_during_callback_manifest.insert(QStringLiteral("version"), QStringLiteral("1.0.2"));
	stop_during_callback_entries[0].contents = QJsonDocument(stop_during_callback_manifest).toJson(QJsonDocument::Compact);
	const QString stop_during_callback_package = QDir(packages).filePath(QStringLiteral("stop-during-callback.mspkg"));
	PluginPackageInfo stop_during_callback_info;
	check(writePackage(stop_during_callback_package, stop_during_callback_entries) &&
		isolated_manager.installPackage(stop_during_callback_package, stop_during_callback_info, error) &&
		isolated_runtime.enable(stop_during_callback_info.id, stop_during_callback_info.version, true, error) &&
		waitFor([&]() { return isolated_runtime.status(stop_during_callback_info.id, stop_during_callback_info.version).running &&
			isolated_host.registeredCommands().size() == 1; }, 10000),
		"a replacement worker starts for the disable-during-callback test");
	const int notifications_before_busy_command = isolated_host.notificationSnapshot().size();
	const QList<PluginCommandRegistration> busy_commands = isolated_host.registeredCommands();
	if(!busy_commands.isEmpty()) busy_commands.first().callback();
	const bool busy_callback_entered = waitFor([&]() {
		const QList<FakePluginHost::Notification> values = isolated_host.notificationSnapshot();
		return values.size() > notifications_before_busy_command && values.last().body == QStringLiteral("callback entered");
	}, 5000);
	check(busy_callback_entered, "the child process enters a long-running command before the stop request");
	check(isolated_runtime.disable(stop_during_callback_info.id, stop_during_callback_info.version, error) &&
		isolated_host.registeredCommands().isEmpty() &&
		waitFor([&]() { return !isolated_runtime.hasActiveInstance(stop_during_callback_info.id, stop_during_callback_info.version); }, 5000),
		"disabling during a running callback unregisters immediately and asynchronously stops its worker");
	QVector<TestEntry> crash_entries = luauEntries(QByteArrayLiteral(
		"ui.registerCommand('crash', 'Crash recovery', function() end)"), runtime_permissions);
	QJsonObject crash_manifest = QJsonDocument::fromJson(crash_entries[0].contents).object();
	crash_manifest.insert(QStringLiteral("id"), QStringLiteral("org.metasiberia.test.crash-recovery"));
	crash_entries[0].contents = QJsonDocument(crash_manifest).toJson(QJsonDocument::Compact);
	const QString crash_package = QDir(packages).filePath(QStringLiteral("crash-recovery.mspkg"));
	PluginPackageInfo crash_info;
	check(writePackage(crash_package, crash_entries) && isolated_manager.installPackage(crash_package, crash_info, error) &&
		isolated_runtime.enable(crash_info.id, crash_info.version, true, error) &&
		waitFor([&]() { return isolated_runtime.status(crash_info.id, crash_info.version).running &&
			isolated_host.registeredCommands().size() == 1; }, 10000),
		"crash-recovery fixture starts in its isolated worker process");
	isolated_runtime.terminateWorkerForTest(crash_info.id, crash_info.version);
	check(waitFor([&]() { return !isolated_runtime.status(crash_info.id, crash_info.version).error.isEmpty() &&
		!isolated_runtime.hasActiveInstance(crash_info.id, crash_info.version); }, 5000) &&
		isolated_host.registeredCommands().isEmpty(),
		"an abnormal worker exit is reported, disables its saved preference and removes its command without terminating the host");
	check(isolated_runtime.enable(crash_info.id, crash_info.version, false, error) &&
		waitFor([&]() { return isolated_runtime.status(crash_info.id, crash_info.version).running &&
			isolated_host.registeredCommands().size() == 1; }, 10000),
		"the host can restart the same trusted package after a worker process crash");
	check(isolated_runtime.disable(crash_info.id, crash_info.version, error) &&
		waitFor([&]() { return !isolated_runtime.hasActiveInstance(crash_info.id, crash_info.version); }, 5000),
		"the recovered worker can be disabled normally");
	isolated_runtime.shutdown();
	check(isolated_host.registeredCommands().isEmpty(), "isolated runtime shutdown removes process-owned command callbacks");

	const QString catalog_runtime_root = temporary.filePath(QStringLiteral("catalog-runtime-plugins"));
	PluginPackageManager catalog_runtime_manager(catalog_runtime_root, nullptr,
		temporary.filePath(QStringLiteral("catalog-runtime-settings")));
	PluginPackageInfo catalog_runtime_info;
	check(catalog_runtime_manager.installPackage(runtime_package, catalog_runtime_info, error, QByteArray(),
		QStringLiteral("Plugin catalog")), "install a catalog-sourced Luau fixture as inert content");
	FakePluginHost catalog_runtime_host;
	PluginRuntime catalog_runtime(catalog_runtime_manager, false);
	catalog_runtime.setHost(&catalog_runtime_host);
	check(!catalog_runtime.enable(catalog_runtime_info.id, catalog_runtime_info.version, true, error) &&
		error.contains(QStringLiteral("local packages")) && catalog_runtime_host.registeredCommands().isEmpty(),
		"catalog packages remain non-executable even after a trust-confirmation request");

	const QString callback_error_package = QDir(packages).filePath(QStringLiteral("callback-error.mspkg"));
	const QByteArray callback_error_script = QByteArrayLiteral(
		"metasiberia.registerCommand('fail', 'Failing command', function() error('callback failure') end)");
	check(writePackage(callback_error_package, luauEntries(callback_error_script, {QStringLiteral("ui.command")})),
		"build a plugin that fails only when its menu command is invoked");
	PluginPackageManager callback_error_manager(temporary.filePath(QStringLiteral("callback-error-plugins")), nullptr,
		temporary.filePath(QStringLiteral("callback-error-settings")));
	PluginPackageInfo callback_error_info;
	check(callback_error_manager.installPackage(callback_error_package, callback_error_info, error),
		"install a callback-error package without execution");
	FakePluginHost callback_error_host;
	PluginRuntime callback_error_runtime(callback_error_manager, false);
	callback_error_runtime.setHost(&callback_error_host);
	check(callback_error_runtime.enable(callback_error_info.id, callback_error_info.version, true, error) &&
		waitFor([&]() { return callback_error_host.registeredCommands().size() == 1; }), "enable the callback-error fixture");
	const PluginCommandRegistration failing_registration = callback_error_host.registeredCommands().first();
	failing_registration.callback();
	check(waitFor([&]() { return callback_error_host.registeredCommands().isEmpty() &&
		callback_error_runtime.status(callback_error_info.id, callback_error_info.version).error.contains(QStringLiteral("callback failure")); }) &&
		callback_error_host.notificationSnapshot().isEmpty(),
		"a command runtime error stops its VM, removes host registrations and reports an inline error state");
	const int error_notifications_before_stale = callback_error_host.notificationSnapshot().size();
	failing_registration.callback();
	check(callback_error_host.registeredCommands().isEmpty() &&
		callback_error_host.notificationSnapshot().size() == error_notifications_before_stale,
		"a stale failed-command callback cannot restart the VM or emit a duplicate error");

	const QString restricted_package = QDir(packages).filePath(QStringLiteral("restricted-libraries.mspkg"));
	const QByteArray restricted_script = QByteArrayLiteral(
		"assert(io == nil and os == nil and debug == nil and coroutine == nil and require == nil)\n"
		"metasiberia.registerCommand('safe', 'Restricted API check', function() end)");
	check(writePackage(restricted_package, luauEntries(restricted_script, {QStringLiteral("ui.command")})),
		"build a restricted-library Luau fixture");
	{
		PluginPackageManager restricted_manager(temporary.filePath(QStringLiteral("restricted-plugins")), nullptr,
			temporary.filePath(QStringLiteral("restricted-settings")));
		PluginPackageInfo restricted_info;
		check(restricted_manager.installPackage(restricted_package, restricted_info, error), "install restricted-library fixture");
		FakePluginHost restricted_host;
		PluginRuntime restricted_runtime(restricted_manager, false);
		restricted_runtime.setHost(&restricted_host);
		check(restricted_runtime.enable(restricted_info.id, restricted_info.version, true, error) &&
			waitFor([&]() { return restricted_host.registeredCommands().size() == 1 &&
				restricted_runtime.status(restricted_info.id, restricted_info.version).running; }),
			"filesystem, network, debug, coroutine and module-loading libraries are absent from the plugin VM");
		restricted_runtime.disable(restricted_info.id, restricted_info.version, error);
	}

	const QString denied_api_package = QDir(packages).filePath(QStringLiteral("denied-api.mspkg"));
	check(writePackage(denied_api_package, luauEntries(QByteArrayLiteral("ui.showNotification('x', 'y')"), {})),
		"build a package that calls an undeclared capability");
	{
		PluginPackageManager denied_manager(temporary.filePath(QStringLiteral("denied-plugins")), nullptr,
			temporary.filePath(QStringLiteral("denied-settings")));
		PluginPackageInfo denied_info;
		check(denied_manager.installPackage(denied_api_package, denied_info, error), "install undeclared-capability fixture");
		FakePluginHost denied_host;
		PluginRuntime denied_runtime(denied_manager, false);
		denied_runtime.setHost(&denied_host);
		check(denied_runtime.enable(denied_info.id, denied_info.version, true, error) &&
			waitFor([&]() { return !denied_runtime.status(denied_info.id, denied_info.version).error.isEmpty(); }) &&
			denied_runtime.status(denied_info.id, denied_info.version).error.contains(QStringLiteral("ui.notification")) &&
			denied_host.notificationSnapshot().isEmpty(),
			"the host rejects a notification API that the package did not declare");
	}

	const QString legacy_package = QDir(packages).filePath(QStringLiteral("legacy-inert-1.0.0.mspkg"));
	QVector<TestEntry> legacy_entries = luauEntries(QByteArrayLiteral("local message = 'Hello, Metasiberia!'"),
		{QStringLiteral("ui.command"), QStringLiteral("ui.notification")});
	QJsonObject legacy_manifest = QJsonDocument::fromJson(legacy_entries[0].contents).object();
	legacy_manifest.insert(QStringLiteral("id"), QStringLiteral("org.metasiberia.example.hello"));
	legacy_entries[0].contents = QJsonDocument(legacy_manifest).toJson(QJsonDocument::Compact);
	check(writePackage(legacy_package, legacy_entries),
		"build the old inert package fixture without command registration");
	const QString legacy_runtime_root = temporary.filePath(QStringLiteral("legacy-runtime-plugins"));
	const QString legacy_settings_root = temporary.filePath(QStringLiteral("legacy-runtime-settings"));
	PluginPackageManager legacy_manager(legacy_runtime_root, nullptr, legacy_settings_root);
	PluginPackageInfo legacy_info;
	check(legacy_manager.installPackage(legacy_package, legacy_info, error), "install the old inert Hello package");
	FakePluginHost legacy_host;
	PluginRuntime legacy_runtime(legacy_manager, true);
	legacy_runtime.setHost(&legacy_host);
	check(legacy_runtime.enable(legacy_info.id, legacy_info.version, true, error) &&
		waitFor([&]() { return !legacy_runtime.status(legacy_info.id, legacy_info.version).error.isEmpty() &&
			!legacy_runtime.hasActiveInstance(legacy_info.id, legacy_info.version); }, 10000) &&
		!legacy_runtime.status(legacy_info.id, legacy_info.version).enabled &&
		!legacy_runtime.status(legacy_info.id, legacy_info.version).running && legacy_host.registeredCommands().isEmpty(),
		"the old inert Hello 1.0.0 fails in its child process, reports the registration error and stays disabled");

	QVector<TestEntry> hello_103_entries = luauEntries(QByteArrayLiteral(
		"ui.registerCommand('hello', 'Hello Metasiberia', function() end)"),
		{QStringLiteral("ui.command"), QStringLiteral("ui.notification")});
	QJsonObject hello_103_manifest = QJsonDocument::fromJson(hello_103_entries[0].contents).object();
	hello_103_manifest.insert(QStringLiteral("id"), QStringLiteral("org.metasiberia.example.hello"));
	hello_103_manifest.insert(QStringLiteral("name"), QStringLiteral("Hello Metasiberia"));
	hello_103_manifest.insert(QStringLiteral("version"), QStringLiteral("1.0.3"));
	QJsonObject hello_103_api = hello_103_manifest.value(QStringLiteral("api")).toObject();
	hello_103_api.insert(QStringLiteral("minor"), 0);
	hello_103_manifest.insert(QStringLiteral("api"), hello_103_api);
	hello_103_entries[0].contents = QJsonDocument(hello_103_manifest).toJson(QJsonDocument::Compact);
	const QString hello_103_package = QDir(packages).filePath(QStringLiteral("hello-1.0.3.mspkg"));
	PluginPackageInfo hello_103_info;
	check(writePackage(hello_103_package, hello_103_entries) && legacy_manager.installPackage(hello_103_package, hello_103_info, error) &&
		hello_103_info.version == QStringLiteral("1.0.3"), "prepare and install the existing Hello 1.0.3 package before the API update");
	check(legacy_runtime.enable(hello_103_info.id, hello_103_info.version, true, error) &&
		waitFor([&]() { return legacy_runtime.status(hello_103_info.id, hello_103_info.version).running &&
			legacy_host.registeredCommands().size() == 1; }), "Hello 1.0.3 still runs through the backwards-compatible command API");
	check(legacy_runtime.disable(hello_103_info.id, hello_103_info.version, error) &&
		legacy_host.registeredCommands().isEmpty() &&
		waitFor([&]() { return !legacy_runtime.hasActiveInstance(hello_103_info.id, hello_103_info.version); }, 5000),
		"Hello 1.0.3 stops and its worker exits before the package update starts");

	const QVector<TestEntry> updated_entries = readPluginProject(QStringLiteral(METASIBERIA_PLUGIN_TEST_HELLO_DIR));
	const QString updated_package = QDir(packages).filePath(QStringLiteral("hello-1.0.4.mspkg"));
	PluginPackageInfo updated_info;
	check(writePackage(updated_package, updated_entries) && legacy_manager.installPackage(updated_package, updated_info, error) &&
		updated_info.id == QStringLiteral("org.metasiberia.example.hello") && updated_info.version == QStringLiteral("1.0.4") &&
		legacy_manager.installedPlugins().size() == 1 && legacy_manager.installedPlugins().first().version == QStringLiteral("1.0.4"),
		"an update atomically replaces the legacy Hello package with 1.0.4 and prevents parallel versions for one plugin id");
	const bool hello_104_enabled = legacy_runtime.enable(updated_info.id, updated_info.version, true, error);
	const bool hello_104_started = hello_104_enabled &&
		waitFor([&]() { return legacy_runtime.status(updated_info.id, updated_info.version).running &&
			legacy_host.registeredPanels().size() == 1; });
	if(!hello_104_started)
	{
		const PluginRuntimeStatus status = legacy_runtime.status(updated_info.id, updated_info.version);
		std::cerr << "Hello 1.0.4 child worker diagnostics: enableError=" << error.toStdString()
			<< " statusEnabled=" << status.enabled << " statusRunning=" << status.running
			<< " statusError=" << status.error.toStdString() << " active="
			<< legacy_runtime.hasActiveInstance(updated_info.id, updated_info.version) << std::endl;
	}
	check(hello_104_started,
		"the packaged Hello SDK example registers its panel after updating the installed legacy package");
	check(legacy_runtime.disable(updated_info.id, updated_info.version, error) && legacy_host.registeredPanels().isEmpty() &&
		!legacy_runtime.status(updated_info.id, updated_info.version).running,
		"the replacement can be disabled without a client restart");
	const QString qt_runtime_root = temporary.filePath(QStringLiteral("qt-host-runtime-plugins"));
	PluginPackageManager qt_runtime_manager(qt_runtime_root, nullptr, temporary.filePath(QStringLiteral("qt-host-runtime-settings")));
	PluginPackageInfo qt_runtime_info;
	check(qt_runtime_manager.installPackage(updated_package, qt_runtime_info, error), "install the example for the actual Qt host adapter");
	bool qt_notification_received = false;
	bool qt_notification_on_ui_thread = false;
	QtPluginHostAdapter qt_host([&](const QString&, const QString& title, const QString& body) {
		qt_notification_received = title == QStringLiteral("Hello Metasiberia") && body == QStringLiteral("Привет, Метасибирь!");
		qt_notification_on_ui_thread = QThread::currentThread() == app.thread();
	});
	PluginRuntime qt_runtime(qt_runtime_manager, false);
	qt_runtime.setHost(&qt_host);
	check(qt_runtime.enable(qt_runtime_info.id, qt_runtime_info.version, true, error) &&
		waitFor([&]() { return qt_runtime.status(qt_runtime_info.id, qt_runtime_info.version).running &&
			qt_host.registeredPanels().size() == 1; }), "start the panel package through the Qt host adapter");
	const QList<PluginPanelRegistration> qt_panels = qt_host.registeredPanels();
	check(!qt_panels.isEmpty() && qt_panels.first().pluginId == qt_runtime_info.id &&
		qt_panels.first().title == QStringLiteral("Hello Metasiberia") && validatePluginPanelLayout(qt_panels.first().layout, error),
		"the Qt host adapter accepts only the bounded declarative panel DTO, without exposing QWidget pointers");
	if(!qt_panels.isEmpty()) qt_host.dispatchPanelAction(qt_panels.first().pluginId, qt_panels.first().pluginVersion,
		qt_panels.first().viewId, QStringLiteral("say_hello"));
	check(waitFor([&]() { return qt_notification_received; }) && qt_notification_on_ui_thread,
		"the actual Qt host dispatches the panel action and marshals its notification onto the UI event loop");
	check(qt_runtime.disable(qt_runtime_info.id, qt_runtime_info.version, error) && qt_host.registeredPanels().isEmpty(),
		"the Qt host removes plugin menu actions on disable");

	for(const QPair<QString, QByteArray>& failure_case : {
		qMakePair(QStringLiteral("script-error"), QByteArrayLiteral(
			"metasiberia.registerCommand('partial', 'Partial command', function() end); error('startup failure')")),
		qMakePair(QStringLiteral("endless-loop"), QByteArrayLiteral("while true do end")),
		qMakePair(QStringLiteral("memory-limit"), QByteArrayLiteral("local x = string.rep('x', 20 * 1024 * 1024)"))})
	{
		const QString case_package = QDir(packages).filePath(failure_case.first + QStringLiteral(".mspkg"));
		check(writePackage(case_package, luauEntries(failure_case.second, {QStringLiteral("ui.command")})),
			"build a bounded runtime failure fixture");
		const QString case_root = temporary.filePath(failure_case.first + QStringLiteral("-plugins"));
		PluginPackageManager case_manager(case_root, nullptr,
			temporary.filePath(failure_case.first + QStringLiteral("-settings")));
		PluginPackageInfo case_info;
		check(case_manager.installPackage(case_package, case_info, error), "install runtime failure fixture inertly");
		FakePluginHost case_host;
		PluginRuntime case_runtime(case_manager, false);
		case_runtime.setHost(&case_host);
		const bool accepted = case_runtime.enable(case_info.id, case_info.version, true, error);
		check(accepted && waitFor([&]() { return !case_runtime.status(case_info.id, case_info.version).error.isEmpty(); }) &&
			case_host.registeredCommands().isEmpty() && !case_runtime.status(case_info.id, case_info.version).enabled &&
			!case_runtime.status(case_info.id, case_info.version).running,
			"script errors, infinite loops and memory exhaustion are bounded and leave no command callbacks");
	}

	const QString runaway_command_package = QDir(packages).filePath(QStringLiteral("runaway-command.mspkg"));
	const QByteArray runaway_command_script = QByteArrayLiteral(
		"ui.registerCommand('runaway', 'Runaway test', function() while true do end end)");
	check(writePackage(runaway_command_package, luauEntries(runaway_command_script, {QStringLiteral("ui.command")})),
		"build an infinite-loop menu command fixture");
	PluginPackageManager runaway_manager(temporary.filePath(QStringLiteral("runaway-command-plugins")), nullptr,
		temporary.filePath(QStringLiteral("runaway-command-settings")));
	PluginPackageInfo runaway_command_info;
	check(runaway_manager.installPackage(runaway_command_package, runaway_command_info, error), "install runaway-command fixture");
	FakePluginHost runaway_host;
	PluginRuntime runaway_runtime(runaway_manager, false);
	runaway_runtime.setHost(&runaway_host);
	check(runaway_runtime.enable(runaway_command_info.id, runaway_command_info.version, true, error) &&
		waitFor([&]() { return runaway_host.registeredCommands().size() == 1; }), "start runaway-command fixture");
	bool ui_timer_fired = false;
	QTimer::singleShot(25, [&ui_timer_fired]() { ui_timer_fired = true; });
	runaway_host.registeredCommands().first().callback();
	const bool runaway_stopped_with_error = waitFor([&]() {
		return !runaway_runtime.status(runaway_command_info.id, runaway_command_info.version).error.isEmpty() && ui_timer_fired;
	});
	const PluginRuntimeStatus runaway_status = runaway_runtime.status(runaway_command_info.id, runaway_command_info.version);
	if(!runaway_stopped_with_error || !ui_timer_fired || !runaway_host.registeredCommands().isEmpty() || runaway_status.running)
		std::cerr << "Runaway diagnostic: stopped=" << runaway_stopped_with_error << " timer=" << ui_timer_fired <<
			" commands=" << runaway_host.registeredCommands().size() << " enabled=" << runaway_status.enabled <<
			" running=" << runaway_status.running << " error=" << runaway_status.error.toStdString() << std::endl;
	check(runaway_stopped_with_error && ui_timer_fired && runaway_host.registeredCommands().isEmpty() && !runaway_status.running,
		"a runaway menu command is interrupted on its worker while the Qt event loop stays responsive");

	QVector<TestEntry> missing_entry = luauEntries(runtime_script, runtime_permissions);
	QJsonObject bad_entry_manifest = QJsonDocument::fromJson(missing_entry[0].contents).object();
	bad_entry_manifest.insert(QStringLiteral("entry"), QStringLiteral("missing.luau"));
	missing_entry[0].contents = QJsonDocument(bad_entry_manifest).toJson(QJsonDocument::Compact);
	const QString missing_entry_package = QDir(packages).filePath(QStringLiteral("missing-entry.mspkg"));
	check(writePackage(missing_entry_package, missing_entry) && !PluginPackageValidator::inspect(missing_entry_package, info, error),
		"a manifest with a missing or invalid Luau entrypoint is rejected before execution");

	const QString plugin_settings = reloaded.settingsDirectoryForId(QStringLiteral("org.metasiberia.test.hello"));
	QDir().mkpath(plugin_settings);
	QFile settings_file(QDir(plugin_settings).filePath(QStringLiteral("settings.json")));
	check(settings_file.open(QIODevice::WriteOnly) && settings_file.write("{}") == 2, "create isolated plugin settings fixture");
	settings_file.close();
	check(!reloaded.removePackage(QStringLiteral("../escape"), QStringLiteral("1.0.0"), false, error),
		"removal rejects an unauthorized traversal id");
	check(reloaded.removePackage(QStringLiteral("org.metasiberia.test.hello"), QStringLiteral("1.0.0"), false, error),
		"removal deletes only the selected plugin and preserves settings");
	check(QFileInfo::exists(plugin_settings), "plugin settings are retained when requested");
	check(reloaded.installPackage(valid_package, info, error), "package can be installed again after removal");
	check(reloaded.removePackage(QStringLiteral("org.metasiberia.test.hello"), QStringLiteral("1.0.0"), true, error),
		"removal with explicit settings deletion succeeds");
	check(!QFileInfo::exists(plugin_settings), "only selected plugin settings are deleted");

	InMemoryStoreFixture fixture;
	PluginPackageManager store_manager(temporary.filePath(QStringLiteral("store-plugins")), &fixture, settings_root);
	check(fixture.isAvailable() && store_manager.storePlugins(QStringLiteral("published"), error).size() == 1,
		"explicit test fixture provider can search store entries");
	check(store_manager.storePlugins(QStringLiteral("not found"), error).isEmpty(), "store search applies query filtering");
	EmptyPluginStoreCatalogProvider empty_provider;
	check(!empty_provider.isAvailable() && empty_provider.search(QString(), error).isEmpty(),
		"production default catalog is honestly empty without a provider");

	const QString ui_root = temporary.filePath(QStringLiteral("ui-plugins"));
	QPalette ui_palette;
	ui_palette.setColor(QPalette::Window, QColor(QStringLiteral("#2B2B30")));
	ui_palette.setColor(QPalette::WindowText, QColor(QStringLiteral("#E8E8EA")));
	ui_palette.setColor(QPalette::Base, QColor(QStringLiteral("#1E1E22")));
	ui_palette.setColor(QPalette::AlternateBase, QColor(QStringLiteral("#34343A")));
	ui_palette.setColor(QPalette::Text, QColor(QStringLiteral("#E8E8EA")));
	ui_palette.setColor(QPalette::Highlight, QColor(QStringLiteral("#604A8C")));
	ui_palette.setColor(QPalette::HighlightedText, QColor(QStringLiteral("#FFFFFF")));
	ui_palette.setColor(QPalette::Disabled, QPalette::Text, QColor(QStringLiteral("#55555A")));
	QApplication::setPalette(ui_palette);
	PluginPackageManager ui_installer(ui_root, nullptr, settings_root);
	check(ui_installer.installPackage(valid_package, info, error), "prepare installed package for UI/store coherence test");
	PluginsDockWidget plugin_panel(nullptr, &fixture, ui_root, settings_root, QStringLiteral(METASIBERIA_PLUGIN_TEST_ICON_DIR));
	FakePluginHost ui_plugin_host;
	plugin_panel.setPluginHost(&ui_plugin_host);
	QApplication::processEvents(); // dispose the constructor's pre-host list row widgets
	QTabWidget* tabs = plugin_panel.findChild<QTabWidget*>(QStringLiteral("pluginManagerTabs"));
	QListWidget* store_list = plugin_panel.findChild<QListWidget*>(QStringLiteral("pluginStoreList"));
	QListWidget* installed_list = plugin_panel.findChild<QListWidget*>(QStringLiteral("installedPluginList"));
	QLineEdit* store_search = plugin_panel.findChild<QLineEdit*>(QStringLiteral("pluginCatalogSearch"));
	QPushButton* catalog_install = plugin_panel.findChild<QPushButton*>(QStringLiteral("pluginCatalogInstallButton"));
	QLabel* installed_details = plugin_panel.findChild<QLabel*>(QStringLiteral("installedPluginDetails"));
	const QStringList expected_tab_tooltips = {QStringLiteral("Catalog"), QStringLiteral("Installed"),
		QStringLiteral("Installation"), QStringLiteral("Instructions")};
	bool tabs_have_icons_and_tooltips = tabs && tabs->count() == 4;
	if(tabs)
		for(int i = 0; i < tabs->count(); ++i)
			tabs_have_icons_and_tooltips = tabs_have_icons_and_tooltips && tabs->tabText(i).isEmpty() &&
				!tabs->tabIcon(i).isNull() && tabs->tabToolTip(i) == expected_tab_tooltips[i];
	check(tabs_have_icons_and_tooltips && tabs->iconSize() == QSize(14, 14) && !tabs->documentMode(),
		"plugin manager uses compact native-style Lucide tabs with accessible tooltips");
	const QIcon expected_plug_tab_icon = LucideIconUtils::tintedIcon(QStringLiteral(METASIBERIA_PLUGIN_TEST_ICON_DIR),
		QStringLiteral("plug"), QApplication::palette().color(QPalette::WindowText), 14);
	check(tabs && !expected_plug_tab_icon.isNull() &&
		tabs->tabIcon(0).pixmap(14, 14).toImage() == expected_plug_tab_icon.pixmap(14, 14).toImage(),
		"the first Plugin Manager tab uses the same Lucide plug icon as the Plugins entry point");
	check(store_list && store_list->count() == 1 && installed_list && installed_list->count() == 1 &&
		store_list->item(0)->data(Qt::UserRole).toString() != installed_list->item(0)->data(Qt::UserRole).toString(),
		"test store publication remains distinct from the installed catalog");
	if(store_search)
	{
		store_search->setText(QStringLiteral("not found"));
		check(store_list->count() == 0 && installed_list->count() == 1,
			"catalog search does not alter the installed plugin catalog");
		store_search->clear();
	}
	QCheckBox* runtime_toggle = plugin_panel.findChild<QCheckBox*>(QStringLiteral("pluginEnabledSwitch"));
	check(catalog_install && !catalog_install->isEnabled() && runtime_toggle && runtime_toggle->isEnabled() &&
		!runtime_toggle->isChecked() && !runtime_toggle->text().isEmpty() &&
		runtime_toggle->toolTip() == QStringLiteral("Run this local Luau package after explicit trust confirmation."),
		"catalog install is unavailable while a compatible local Luau runtime toggle is ready for explicit trust");
	check(runtime_toggle && runtime_toggle->styleSheet().contains(QStringLiteral("#E8E8EA")) &&
		runtime_toggle->styleSheet().contains(QStringLiteral("indicator:checked")) &&
		runtime_toggle->styleSheet().contains(QStringLiteral("indicator:hover")),
		"the enable checkbox has explicit high-contrast text and visible hover/checked states on dark rows");
	QLabel* installed_row_icon = plugin_panel.findChild<QLabel*>(QStringLiteral("installedPluginRowIcon"));
	check(installed_list && installed_list->item(0)->sizeHint().height() == 46 && installed_row_icon &&
		installed_row_icon->pixmap() && installed_row_icon->pixmap()->size() == QSize(18, 18),
		"installed plugin rows and manifest icons use compact logical dimensions");
	QLabel* installed_status = plugin_panel.findChild<QLabel*>(QStringLiteral("installedPluginStatus"));
	const QColor selected_row_background = installed_list ? installed_list->palette().color(QPalette::Highlight) : QColor();
	bool status_contrast_ok = runtime_toggle && runtime_toggle->isEnabled() && installed_status && !installed_status->isHidden();
	if(status_contrast_ok)
		for(const QPalette::ColorGroup group : {QPalette::Active, QPalette::Inactive, QPalette::Disabled})
			status_contrast_ok = status_contrast_ok && contrastRatio(installed_status->palette().color(group, QPalette::Text), selected_row_background) >= 4.5;
	check(status_contrast_ok, "secondary status remains readable in active, inactive, disabled and selected-row palette states");
	check(store_list && store_list->item(0)->text().contains(QStringLiteral("Available")) &&
		!store_list->item(0)->icon().isNull() && store_list->item(0)->text().contains(QStringLiteral("Tools")),
		"catalog card shows its icon, category and availability state");
	const QList<PluginListEntry> menu_entries = plugin_panel.installedPlugins();
	check(menu_entries.size() == 1 && menu_entries[0].installed && !menu_entries[0].enabled && !menu_entries[0].running &&
		menu_entries[0].state.contains(QStringLiteral("Disabled")) && menu_entries[0].state.contains(QStringLiteral("Not running")) &&
		!menu_entries[0].icon.isNull(), "installed menu model separates installed, enabled and running states and carries its icon to MainWindow");
	int catalog_change_signals = 0;
	QObject::connect(&plugin_panel, &PluginsDockWidget::installedPluginsChanged, &plugin_panel,
		[&catalog_change_signals]() { ++catalog_change_signals; });
	plugin_panel.reloadPluginCatalogs();
	check(catalog_change_signals > 0 && plugin_panel.installedPlugins().size() == 1 && store_list->count() == 1,
		"reload keeps the panel, menu source and store listing coherent");
	plugin_panel.showInstalledTab();
	installed_list->setCurrentRow(0);
	QTextBrowser* instructions = plugin_panel.findChild<QTextBrowser*>(QStringLiteral("pluginInstructionsBrowser"));
	QLabel* installed_detail_icon = plugin_panel.findChild<QLabel*>(QStringLiteral("installedPluginDetailsIcon"));
	check(installed_details && installed_details->text().contains(QStringLiteral("Disabled")) &&
		installed_details->text().contains(QStringLiteral("Not running")) &&
		installed_details->text().contains(QStringLiteral("Author")) && installed_details->text().contains(QStringLiteral("Compatibility")) &&
		installed_details->text().contains(QStringLiteral("Permissions")) && installed_detail_icon && installed_detail_icon->pixmap() &&
		installed_detail_icon->pixmap()->size() == QSize(24, 24),
		"installed detail card separates a compact icon, author, compatibility, permissions and explicit states");
	if(installed_details)
	{
		const QRegularExpression muted_color_regex(QStringLiteral("color:(#[0-9a-fA-F]{6})"));
		QRegularExpressionMatchIterator colors = muted_color_regex.globalMatch(installed_details->text());
		QColor muted_details;
		if(colors.hasNext()) colors.next(); // First span is the accent color on the plugin name.
		if(colors.hasNext()) muted_details = QColor(colors.next().captured(1));
		check(muted_details.isValid() && contrastRatio(muted_details, ui_palette.color(QPalette::Window)) >= 4.5,
			"muted details and technical labels meet readable contrast on the dark panel surface");
	}
	QToolButton* detail_toggle = plugin_panel.findChild<QToolButton*>(QStringLiteral("installedPluginDetailsToggle"));
	QFrame* detail_separator = plugin_panel.findChild<QFrame*>(QStringLiteral("installedPluginDetailsSeparator"));
	QLabel* technical_details = plugin_panel.findChild<QLabel*>(QStringLiteral("installedPluginTechnicalDetails"));
	check(detail_toggle && !detail_toggle->isChecked() && detail_separator && detail_separator->isHidden() &&
		technical_details && technical_details->isHidden(), "technical plugin details start collapsed");
	if(detail_toggle) detail_toggle->click();
	check(detail_toggle && detail_toggle->isChecked() && detail_separator && !detail_separator->isHidden() &&
		technical_details && !technical_details->isHidden() && technical_details->text().contains(QStringLiteral("Plugin ID")),
		"technical plugin metadata expands below a separator");
	check(instructions && instructions->toHtml().contains(QStringLiteral("<ol")) &&
		instructions->toHtml().contains(QStringLiteral("Current limitations")) &&
		instructions->toHtml().contains(QStringLiteral("Important")) &&
		instructions->toHtml().contains(QStringLiteral("What comes later")),
		"instructions use structured sections, numbered steps, lists and callouts");
	check(store_search && !store_search->toolTip().isEmpty() && !catalog_install->toolTip().isEmpty() &&
		!plugin_panel.findChild<QPushButton*>(QStringLiteral("installPluginButton"))->toolTip().isEmpty() &&
		!plugin_panel.findChild<QPushButton*>(QStringLiteral("removeSelectedPluginButton"))->toolTip().isEmpty(),
		"search and install controls have explanatory tooltips");
	const QString icon_directory = QStringLiteral(METASIBERIA_PLUGIN_TEST_ICON_DIR);
	const QIcon plug_icon = LucideIconUtils::tintedIcon(icon_directory, QStringLiteral("plug"), ui_palette.color(QPalette::WindowText), 18);
	check(QFileInfo::exists(QDir(icon_directory).filePath(QStringLiteral("plug.svg"))) &&
		!plug_icon.isNull() && !plug_icon.pixmap(18, 18).isNull(),
		"Lucide plug icon is available for the top Plugins control and renders at toolbar size");
	plugin_panel.showCatalogTab();
	check(tabs->currentIndex() == 0, "catalog navigation selects the catalog tab");

	const QString future_runtime = QDir(packages).filePath(QStringLiteral("future-runtime.mspkg"));
	check(writePackage(future_runtime, validEntries(1, QStringLiteral("future-vm-9"))) &&
		manager.installPackage(future_runtime, info, error) && info.runtime == QStringLiteral("future-vm-9") &&
		!info.enabled && !info.running && info.state.contains(QStringLiteral("disabled")),
		"unknown runtime can only be stored as inert files");
	return failures == 0 ? 0 : 1;
}
