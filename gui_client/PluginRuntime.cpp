#include "PluginRuntime.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QElapsedTimer>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QMetaObject>
#include <QtCore/QMutexLocker>
#include <QtCore/QProcess>
#include <QtCore/QStandardPaths>
#include <QtCore/QTimer>
#include <QtCore/QRegularExpression>
#include <QtCore/QSaveFile>
#include <QtCore/QThread>
#include <QtCore/QWaitCondition>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <iostream>
#include <lua.h>
#include <lualib.h>
#include <luacode.h>
#include <mutex>
#include <thread>
#include <chrono>
#include <cstdio>
#include <cmath>

#include <cstdlib>


namespace
{
const QString kLocalPackageSource = QStringLiteral("Local package");
const QString kCommandPermission = QStringLiteral("ui.command");
const QString kNotificationPermission = QStringLiteral("ui.notification");
const QString kPanelPermission = QStringLiteral("ui.panel");
const QString kSceneSummaryPermission = QStringLiteral("scene.read.summary");
const int kMaximumSceneSummaryRequestsPerPlugin = 120;
const int kSceneSummaryWorkerTimeoutMs = 500;


struct QueuedPluginInvocation
{
	bool panelAction = false;
	QString actionId;
};


bool isSafeRelativePath(const QString& relative)
{
	if(relative.isEmpty() || QDir::isAbsolutePath(relative) || relative.contains(QLatin1Char(':')))
		return false;
	const QString normalized = QString(relative).replace(QLatin1Char('\\'), QLatin1Char('/'));
	const QStringList components = normalized.split(QLatin1Char('/'));
	for(const QString& component : components)
		if(component.isEmpty() || component == QStringLiteral(".") || component == QStringLiteral(".."))
			return false;
	return true;
}


bool isCanonicalChild(const QString& candidate, const QString& root)
{
	QString normalized_root = QDir::cleanPath(root);
	QString normalized_candidate = QDir::cleanPath(candidate);
	if(!normalized_root.endsWith(QLatin1Char('/')))
		normalized_root += QLatin1Char('/');
#if defined(Q_OS_WIN)
	return normalized_candidate.startsWith(normalized_root, Qt::CaseInsensitive);
#else
	return normalized_candidate.startsWith(normalized_root, Qt::CaseSensitive);
#endif
}

QMutex& pluginWorkerProtocolMutex()
{
	static QMutex mutex;
	return mutex;
}

void sendPluginWorkerProtocolMessage(const QJsonObject& message)
{
	QMutexLocker lock(&pluginWorkerProtocolMutex());
	QJsonObject versioned = message;
	versioned.insert(QStringLiteral("protocolMajor"), 1);
	versioned.insert(QStringLiteral("protocolMinor"), 0);
	std::cout << QJsonDocument(versioned).toJson(QJsonDocument::Compact).constData() << '\n';
	std::cout.flush();
}

class PluginWorkerIpcHost final : public PluginHostInterface
{
public:
	explicit PluginWorkerIpcHost(QCoreApplication& application) : app(application) {}

	bool registerCommand(const QString& pluginId, const QString& pluginVersion, const QString& commandId,
		const QString& title, const std::function<void()>& callback, QString& error) override
	{
		if(!callback)
		{
			error = QStringLiteral("A plugin can register only one primary menu action.");
			return false;
		}
		{
			QMutexLocker lock(&mutex);
			if(has_primary_action)
			{
				error = QStringLiteral("A plugin can register only one primary menu action.");
				return false;
			}
		}
		QJsonObject message;
		message.insert(QStringLiteral("type"), QStringLiteral("registerCommand"));
		message.insert(QStringLiteral("requestId"), QStringLiteral("startup-command"));
		message.insert(QStringLiteral("pluginId"), pluginId);
		message.insert(QStringLiteral("pluginVersion"), pluginVersion);
		message.insert(QStringLiteral("commandId"), commandId);
		message.insert(QStringLiteral("title"), title);
		if(!awaitRegistration(message, QStringLiteral("startup-command"), error)) return false;
		{
			QMutexLocker lock(&mutex);
			command_callback = callback;
			has_primary_action = true;
		}
		return true;
	}

	bool registerPanel(const QString& pluginId, const QString& pluginVersion, const QString& viewId,
		const QString& title, const QJsonObject& layout,
		const std::function<void(const QString&)>& actionCallback, QString& error) override
	{
		if(!actionCallback)
		{
			error = QStringLiteral("A plugin can register only one primary menu action.");
			return false;
		}
		{
			QMutexLocker lock(&mutex);
			if(has_primary_action)
			{
				error = QStringLiteral("A plugin can register only one primary menu action.");
				return false;
			}
		}
		QJsonObject message;
		message.insert(QStringLiteral("type"), QStringLiteral("registerPanel"));
		message.insert(QStringLiteral("requestId"), QStringLiteral("startup-panel"));
		message.insert(QStringLiteral("pluginId"), pluginId);
		message.insert(QStringLiteral("pluginVersion"), pluginVersion);
		message.insert(QStringLiteral("viewId"), viewId);
		message.insert(QStringLiteral("title"), title);
		message.insert(QStringLiteral("layout"), layout);
		if(!awaitRegistration(message, QStringLiteral("startup-panel"), error)) return false;
		{
			QMutexLocker lock(&mutex);
			panel_action_callback = actionCallback;
			registered_view_id = viewId;
			has_primary_action = true;
		}
		return true;
	}

	void removePluginResources(const QString&, const QString&) override
	{
		QMutexLocker lock(&mutex);
		command_callback = {};
		panel_action_callback = {};
		registered_view_id.clear();
		has_primary_action = false;
	}

	void closePanel(const QString& pluginId, const QString& pluginVersion, const QString& viewId) override
	{
		{
			QMutexLocker lock(&mutex);
			if(!has_primary_action || registered_view_id != viewId) return;
		}
		QJsonObject message;
		message.insert(QStringLiteral("type"), QStringLiteral("closeView"));
		message.insert(QStringLiteral("pluginId"), pluginId);
		message.insert(QStringLiteral("pluginVersion"), pluginVersion);
		message.insert(QStringLiteral("viewId"), viewId);
		sendPluginWorkerProtocolMessage(message);
	}

	void showNotification(const QString& pluginId, const QString& title, const QString& body) override
	{
		QJsonObject message;
		message.insert(QStringLiteral("type"), QStringLiteral("notification"));
		message.insert(QStringLiteral("pluginId"), pluginId);
		message.insert(QStringLiteral("title"), title);
		message.insert(QStringLiteral("body"), body);
		sendPluginWorkerProtocolMessage(message);
	}

	bool getSceneSummary(const QString& pluginId, const QString& pluginVersion,
		QJsonObject& summary, QString& error) override
	{
		if(pluginId.isEmpty() || pluginVersion.isEmpty())
		{
			error = QStringLiteral("The scene summary request has no plugin identity.");
			return false;
		}
		QString request_id;
		{
			QMutexLocker lock(&scene_mutex);
			if(scene_request_waiting)
			{
				error = QStringLiteral("A scene summary request is already pending.");
				return false;
			}
			scene_request_waiting = true;
			scene_result_received = false;
			scene_request_error.clear();
			scene_result = QJsonObject();
			request_id = QStringLiteral("scene-%1").arg(++scene_request_serial);
			pending_scene_request_id = request_id;
		}
		QJsonObject message;
		message.insert(QStringLiteral("type"), QStringLiteral("sceneSummary"));
		message.insert(QStringLiteral("requestId"), request_id);
		message.insert(QStringLiteral("pluginId"), pluginId);
		message.insert(QStringLiteral("pluginVersion"), pluginVersion);
		sendPluginWorkerProtocolMessage(message);

		QMutexLocker lock(&scene_mutex);
		QElapsedTimer timer;
		timer.start();
		while(!scene_result_received && timer.elapsed() < kSceneSummaryWorkerTimeoutMs)
		{
			const qint64 remaining_ms = std::max<qint64>(1, kSceneSummaryWorkerTimeoutMs - timer.elapsed());
			scene_result_changed.wait(&scene_mutex,
				static_cast<unsigned long>(remaining_ms));
		}
		scene_request_waiting = false;
		pending_scene_request_id.clear();
		if(!scene_result_received)
		{
			error = QStringLiteral("The client did not answer the scene summary request within 500 ms.");
			return false;
		}
		if(!scene_request_error.isEmpty())
		{
			error = scene_request_error;
			return false;
		}
		summary = scene_result;
		return true;
	}

	bool isUiThread() const override { return QThread::currentThread() == app.thread(); }
	QList<PluginCommandRegistration> registeredCommands() const override { return {}; }
	QList<PluginPanelRegistration> registeredPanels() const override { return {}; }

	void handleInput(const QByteArray& line, PluginRuntime& runtime, const QString& pluginId, const QString& version)
	{
		QJsonParseError parse_error;
		const QJsonDocument document = QJsonDocument::fromJson(line, &parse_error);
		if(parse_error.error != QJsonParseError::NoError || !document.isObject()) return;
		const QJsonObject message = document.object();
		if(message.contains(QStringLiteral("protocolMajor")) &&
			message.value(QStringLiteral("protocolMajor")).toInt(-1) != 1) return;
		const QString type = message.value(QStringLiteral("type")).toString();
		if(type == QStringLiteral("registerResult"))
		{
			QMutexLocker lock(&mutex);
			if(!registration_waiting || message.value(QStringLiteral("requestId")).toString() != pending_request_id) return;
			registration_accepted = message.value(QStringLiteral("ok")).toBool(false);
			registration_error = message.value(QStringLiteral("error")).toString().left(300);
			registration_result_received = true;
			registration_changed.wakeAll();
		}
		else if(type == QStringLiteral("sceneSummaryResult"))
		{
			QMutexLocker lock(&scene_mutex);
			if(!scene_request_waiting || message.value(QStringLiteral("requestId")).toString() != pending_scene_request_id) return;
			if(message.value(QStringLiteral("ok")).toBool(false))
				scene_result = message.value(QStringLiteral("summary")).toObject();
			else
				scene_request_error = message.value(QStringLiteral("error")).toString().left(300);
			scene_result_received = true;
			scene_result_changed.wakeAll();
		}
		else if(type == QStringLiteral("invoke"))
		{
			std::function<void()> callback;
			{
				QMutexLocker lock(&mutex);
				callback = has_primary_action ? command_callback : std::function<void()>();
			}
			if(callback) callback();
		}
		else if(type == QStringLiteral("invokePanelAction"))
		{
			std::function<void(const QString&)> callback;
			QString registered_view_id_snapshot;
			{
				QMutexLocker lock(&mutex);
				callback = has_primary_action ? panel_action_callback : std::function<void(const QString&)>();
				registered_view_id_snapshot = registered_view_id;
			}
			const QString action_id = message.value(QStringLiteral("actionId")).toString();
			const QString view_id = message.value(QStringLiteral("viewId")).toString();
			if(callback && view_id == registered_view_id_snapshot && action_id.size() <= 64) callback(action_id);
		}
		else if(type == QStringLiteral("stop"))
		{
			{
				QMutexLocker lock(&mutex);
				if(registration_waiting)
				{
					registration_error = QStringLiteral("The plugin was disabled during startup.");
					registration_result_received = true;
					registration_changed.wakeAll();
				}
			}
			{
				QMutexLocker lock(&scene_mutex);
				if(scene_request_waiting)
				{
					scene_request_error = QStringLiteral("The plugin was disabled while reading the scene summary.");
					scene_result_received = true;
					scene_result_changed.wakeAll();
				}
			}
			QMetaObject::invokeMethod(QCoreApplication::instance(), [&runtime, pluginId, version]() {
				QString ignored;
				runtime.disable(pluginId, version, ignored);
			}, Qt::QueuedConnection);
		}
	}

private:
	bool awaitRegistration(const QJsonObject& message, const QString& requestId, QString& error)
	{
		{
			QMutexLocker lock(&mutex);
			registration_waiting = true;
			registration_result_received = false;
			registration_accepted = false;
			registration_error.clear();
			pending_request_id = requestId;
		}
		sendPluginWorkerProtocolMessage(message);
		QMutexLocker lock(&mutex);
		QElapsedTimer timer;
		timer.start();
		while(!registration_result_received && timer.elapsed() < 5000)
			registration_changed.wait(&mutex, static_cast<unsigned long>(5000 - timer.elapsed()));
		registration_waiting = false;
		pending_request_id.clear();
		if(!registration_result_received)
		{
			error = QStringLiteral("The client did not acknowledge plugin UI registration.");
			return false;
		}
		if(!registration_accepted)
		{
			error = registration_error.isEmpty() ? QStringLiteral("The client rejected plugin UI registration.") : registration_error;
			return false;
		}
		return true;
	}

	QCoreApplication& app;
	QMutex mutex;
	QWaitCondition registration_changed;
	bool registration_waiting = false;
	bool registration_result_received = false;
	bool registration_accepted = false;
	bool has_primary_action = false;
	QString registration_error;
	QString pending_request_id;
	QMutex scene_mutex;
	QWaitCondition scene_result_changed;
	bool scene_request_waiting = false;
	bool scene_result_received = false;
	quint64 scene_request_serial = 0;
	QString pending_scene_request_id;
	QString scene_request_error;
	QJsonObject scene_result;
	QString registered_view_id;
	std::function<void()> command_callback;
	std::function<void(const QString&)> panel_action_callback;
};

void pluginWorkerQtMessageHandler(QtMsgType type, const QMessageLogContext&, const QString& message)
{
	const QByteArray safe_message = message.left(500).toLocal8Bit();
	std::fprintf(stderr, "Plugin worker Qt message (%d): %s\n", static_cast<int>(type), safe_message.constData());
}


void openLibrary(lua_State* state, const lua_CFunction function, const char* name)
{
	lua_pushcfunction(state, function, name);
	lua_pushstring(state, name);
	lua_call(state, 1, 0);
}


QString errorFromLua(lua_State* state)
{
	const char* text = lua_tostring(state, -1);
	return QString::fromUtf8(text ? text : "Luau execution failed").left(1000);
}
}


struct PluginRuntime::PersistentPreference
{
	QString approvedPackageSha256;
	QString enabledVersion;
	bool enabled = false;
};


struct PluginRuntime::Instance
{
	PluginRuntime* owner = nullptr;
	std::weak_ptr<int> ownerLifetime;
	std::weak_ptr<Instance> self;
	QProcess* process = nullptr;
	QByteArray process_output;
	bool expected_process_exit = false;
	bool user_requested_stop = false;
	QElapsedTimer started_at;
	lua_State* state = nullptr;
	QString pluginId;
	QString version;
	QString commandId;
	QString commandTitle;
	QString viewId;
	QString viewTitle;
	QJsonObject viewLayout;
	QStringList permissions;
	size_t memoryUsed = 0;
	int commandReference = -1;
	int panelActionReference = -1;
	int safepoints = 0;
	int notifications = 0;
	bool initializing = true;
	std::atomic_bool stopRequested{false};
	std::atomic_bool preservePreferenceOnStop{false};
	std::atomic_bool executing{false};
	std::atomic_bool workerFinished{false};
	std::atomic<int> sceneSummaryRequests{0};
	std::mutex queueMutex;
	std::condition_variable queueWake;
	std::deque<QueuedPluginInvocation> queuedCommands;
	std::thread worker;
	QElapsedTimer executionClock;

	~Instance()
	{
		if(state)
			lua_close(state);
	}
};


PluginRuntime::PluginRuntime(PluginPackageManager& packageManager)
: package_manager(packageManager), isolated_execution(true)
{}


#if defined(METASIBERIA_PLUGIN_TESTING)
PluginRuntime::PluginRuntime(PluginPackageManager& packageManager, const bool isolatedExecution)
: package_manager(packageManager), isolated_execution(isolatedExecution)
{}
#else
PluginRuntime::PluginRuntime(PluginPackageManager& packageManager, WorkerProcessExecutionTag)
: package_manager(packageManager), isolated_execution(false), worker_process_context(true)
{}
#endif


PluginRuntime::~PluginRuntime()
{
	shutdown();
	callback_lifetime.reset();
}


QString PluginRuntime::instanceKey(const QString& pluginId, const QString& version) const
{
	return pluginId + QLatin1Char('@') + version;
}


void PluginRuntime::setHost(PluginHostInterface* newHost)
{
	if(host == newHost)
		return;
	shutdown();
	host = newHost;
	shutting_down = newHost == nullptr;
}


void PluginRuntime::setStateChangedCallback(std::function<void()> callback)
{
	state_changed_callback = std::move(callback);
}


void* PluginRuntime::limitedAllocator(void* userData, void* pointer, const size_t oldSize, const size_t newSize)
{
	Instance* const instance = static_cast<Instance*>(userData);
	if(newSize == 0)
	{
		if(pointer)
		{
			instance->memoryUsed -= std::min(instance->memoryUsed, oldSize);
			std::free(pointer);
		}
		return nullptr;
	}

	const size_t old_allocation = pointer ? std::min(instance->memoryUsed, oldSize) : 0;
	const size_t retained = instance->memoryUsed - old_allocation;
	if(newSize > MaximumVmBytes || retained > MaximumVmBytes - newSize)
		return nullptr;
	void* const result = std::realloc(pointer, newSize);
	if(result)
		instance->memoryUsed = retained + newSize;
	return result;
}


int PluginRuntime::sceneSummaryApi(lua_State* state)
{
	Instance* const instance = static_cast<Instance*>(lua_touserdata(state, lua_upvalueindex(1)));
	if(!instance || !instance->owner || !instance->owner->host)
	{
		luaL_error(state, "Plugin host is not available.");
		return 0;
	}
	if(instance->stopRequested.load())
	{
		luaL_error(state, "The plugin is stopping and cannot read scene information.");
		return 0;
	}
	if(!instance->permissions.contains(kSceneSummaryPermission))
	{
		luaL_error(state, "The plugin did not request the scene.read.summary capability.");
		return 0;
	}
	QJsonObject summary;
	QString error;
	if(!instance->owner->host->getSceneSummary(instance->pluginId, instance->version, summary, error))
	{
		luaL_error(state, "%s", error.left(300).toUtf8().constData());
		return 0;
	}
	const QJsonObject server = summary.value(QStringLiteral("server")).toObject();
	const QJsonObject camera = summary.value(QStringLiteral("camera")).toObject();
	const QJsonValue connected = summary.value(QStringLiteral("connected"));
	const QJsonValue host = server.value(QStringLiteral("host"));
	const QJsonValue world = server.value(QStringLiteral("world"));
	const QJsonValue x = camera.value(QStringLiteral("x"));
	const QJsonValue y = camera.value(QStringLiteral("y"));
	const QJsonValue z = camera.value(QStringLiteral("z"));
	if(!connected.isBool() || !host.isString() || host.toString().size() > 253 ||
		!world.isString() || world.toString().size() > 128 || !x.isDouble() || !y.isDouble() || !z.isDouble() ||
		!std::isfinite(x.toDouble()) || !std::isfinite(y.toDouble()) || !std::isfinite(z.toDouble()) ||
		QJsonDocument(summary).toJson(QJsonDocument::Compact).size() > 4096)
	{
		luaL_error(state, "The host returned an invalid scene summary.");
		return 0;
	}
	lua_createtable(state, 0, 3);
	lua_pushboolean(state, connected.toBool()); lua_setfield(state, -2, "connected");
	lua_createtable(state, 0, 2);
	const QByteArray host_utf8 = host.toString().toUtf8();
	lua_pushlstring(state, host_utf8.constData(), static_cast<size_t>(host_utf8.size())); lua_setfield(state, -2, "host");
	const QByteArray world_utf8 = world.toString().toUtf8();
	lua_pushlstring(state, world_utf8.constData(), static_cast<size_t>(world_utf8.size())); lua_setfield(state, -2, "world");
	lua_setfield(state, -2, "server");
	lua_createtable(state, 0, 3);
	lua_pushnumber(state, x.toDouble()); lua_setfield(state, -2, "x");
	lua_pushnumber(state, y.toDouble()); lua_setfield(state, -2, "y");
	lua_pushnumber(state, z.toDouble()); lua_setfield(state, -2, "z");
	lua_setfield(state, -2, "camera");
	return 1;
}


void PluginRuntime::instructionInterrupt(lua_State* state, const int gc)
{
	if(gc >= 0)
		return;
	Instance* const instance = static_cast<Instance*>(lua_callbacks(state)->userdata);
	if(!instance)
		return;
	++instance->safepoints;
	if(instance->safepoints > MaximumSafepointsPerCall ||
		(instance->executionClock.isValid() && instance->executionClock.elapsed() > MaximumExecutionMilliseconds))
	{
		lua_callbacks(state)->interrupt = nullptr;
		lua_rawcheckstack(state, 1);
		luaL_error(state, "Plugin execution limit exceeded; the plugin was stopped.");
	}
}


int PluginRuntime::registerCommandApi(lua_State* state)
{
	Instance* const instance = static_cast<Instance*>(lua_touserdata(state, lua_upvalueindex(1)));
	if(!instance || !instance->owner || !instance->owner->host)
	{
		luaL_error(state, "Plugin host is not available.");
		return 0;
	}
	if(instance->stopRequested.load())
		luaL_error(state, "The plugin is stopping and cannot register resources.");
	if(!instance->initializing || instance->commandReference >= 0 || instance->panelActionReference >= 0)
	{
		luaL_error(state, "A plugin can register only one primary menu action during startup.");
		return 0;
	}
	if(!instance->permissions.contains(kCommandPermission))
	{
		luaL_error(state, "The plugin did not request the ui.command capability.");
		return 0;
	}

	size_t command_id_size = 0;
	size_t title_size = 0;
	const char* command_id_utf8 = luaL_checklstring(state, 1, &command_id_size);
	const char* title_utf8 = luaL_checklstring(state, 2, &title_size);
	luaL_checktype(state, 3, LUA_TFUNCTION);
	if(command_id_size == 0 || command_id_size > 64 || title_size == 0 || title_size > 80)
	{
		luaL_error(state, "Command id/title is empty or exceeds its size limit.");
		return 0;
	}

	QString command_id;
	QString title;
	bool valid_command_id = false;
	{
		command_id = QString::fromUtf8(command_id_utf8, static_cast<int>(command_id_size));
		title = QString::fromUtf8(title_utf8, static_cast<int>(title_size)).trimmed();
		static const QRegularExpression id_pattern(QStringLiteral("^[a-z][a-z0-9._-]{0,63}$"));
		valid_command_id = id_pattern.match(command_id).hasMatch() && !title.isEmpty() &&
			!title.contains(QChar::Null) && !title.contains(QLatin1Char('\n')) && !title.contains(QLatin1Char('\r'));
	}
	if(!valid_command_id)
	{
		luaL_error(state, "Command id or title has an invalid format.");
		return 0;
	}

	lua_pushvalue(state, 3);
	const int command_reference = lua_ref(state, -1);
	bool registered = false;
	{
		QString ignored_error;
		const std::weak_ptr<Instance> weak_instance = instance->self;
		const std::function<void()> callback = [weak_instance]() {
			if(const std::shared_ptr<Instance> locked = weak_instance.lock())
				if(!locked->ownerLifetime.expired() && locked->owner)
					locked->owner->enqueueCommand(locked);
		};
		registered = instance->owner->host->registerCommand(instance->pluginId, instance->version,
			command_id, title, callback, ignored_error);
	}
	if(!registered)
	{
		lua_unref(state, command_reference);
		luaL_error(state, "The host rejected this command registration.");
		return 0;
	}
	if(instance->stopRequested.load())
	{
		instance->owner->host->removePluginResources(instance->pluginId, instance->version);
		luaL_error(state, "The plugin was disabled during startup.");
	}
	instance->commandId = command_id;
	instance->commandTitle = title;
	instance->commandReference = command_reference;
	lua_pushboolean(state, 1);
	return 1;
}


int PluginRuntime::registerPanelApi(lua_State* state)
{
	Instance* const instance = static_cast<Instance*>(lua_touserdata(state, lua_upvalueindex(1)));
	if(!instance || !instance->owner || !instance->owner->host)
	{
		luaL_error(state, "Plugin host is not available.");
		return 0;
	}
	if(instance->stopRequested.load()) luaL_error(state, "The plugin is stopping and cannot register resources.");
	if(!instance->initializing || instance->commandReference >= 0 || instance->panelActionReference >= 0)
		luaL_error(state, "A plugin can register only one primary panel action during startup.");
	if(!instance->permissions.contains(kPanelPermission))
		luaL_error(state, "The plugin did not request the ui.panel capability.");

	size_t view_id_size = 0;
	size_t title_size = 0;
	size_t layout_size = 0;
	const char* const view_id_utf8 = luaL_checklstring(state, 1, &view_id_size);
	const char* const title_utf8 = luaL_checklstring(state, 2, &title_size);
	const char* const layout_utf8 = luaL_checklstring(state, 3, &layout_size);
	luaL_checktype(state, 4, LUA_TFUNCTION);
	if(view_id_size == 0 || view_id_size > 64 || title_size == 0 || title_size > 80 || layout_size == 0 || layout_size > 4096)
		luaL_error(state, "Panel id, title or layout is empty or exceeds its size limit.");
	const QString view_id = QString::fromUtf8(view_id_utf8, static_cast<int>(view_id_size));
	const QString title = QString::fromUtf8(title_utf8, static_cast<int>(title_size)).trimmed();
	static const QRegularExpression id_pattern(QStringLiteral("^[a-z][a-z0-9._-]{0,63}$"));
	if(!id_pattern.match(view_id).hasMatch() || title.isEmpty() || title.contains(QChar::Null) ||
		title.contains(QLatin1Char('\n')) || title.contains(QLatin1Char('\r')))
		luaL_error(state, "Panel id or title has an invalid format.");
	const QByteArray layout_bytes(layout_utf8, static_cast<int>(layout_size));
	QJsonParseError parse_error;
	const QJsonDocument layout_document = QJsonDocument::fromJson(layout_bytes, &parse_error);
	if(parse_error.error != QJsonParseError::NoError || !layout_document.isObject())
		luaL_error(state, "Panel layout must be a valid JSON object.");

	lua_pushvalue(state, 4);
	const int callback_reference = lua_ref(state, -1);
	const std::weak_ptr<Instance> weak_instance = instance->self;
	const std::function<void(const QString&)> callback = [weak_instance](const QString& action_id) {
		if(const std::shared_ptr<Instance> locked = weak_instance.lock())
			if(!locked->ownerLifetime.expired() && locked->owner)
				locked->owner->enqueuePanelAction(locked, action_id);
	};
	QString registration_error;
	const bool registered = instance->owner->host->registerPanel(instance->pluginId, instance->version,
		view_id, title, layout_document.object(), callback, registration_error);
	if(!registered)
	{
		lua_unref(state, callback_reference);
		luaL_error(state, "%s", registration_error.left(300).toUtf8().constData());
	}
	if(instance->stopRequested.load())
	{
		instance->owner->host->removePluginResources(instance->pluginId, instance->version);
		luaL_error(state, "The plugin was disabled during startup.");
	}
	instance->viewId = view_id;
	instance->viewTitle = title;
	instance->viewLayout = layout_document.object();
	instance->panelActionReference = callback_reference;
	lua_pushboolean(state, 1);
	return 1;
}


int PluginRuntime::closeViewApi(lua_State* state)
{
	Instance* const instance = static_cast<Instance*>(lua_touserdata(state, lua_upvalueindex(1)));
	if(!instance || !instance->owner || !instance->owner->host)
	{
		luaL_error(state, "Plugin host is not available.");
		return 0;
	}
	if(instance->stopRequested.load()) luaL_error(state, "The plugin is stopping and cannot access its view.");
	if(!instance->permissions.contains(kPanelPermission))
		luaL_error(state, "The plugin did not request the ui.panel capability.");
	if(instance->viewId.isEmpty()) luaL_error(state, "The plugin has no registered panel view.");
	instance->owner->host->closePanel(instance->pluginId, instance->version, instance->viewId);
	return 0;
}


int PluginRuntime::notificationApi(lua_State* state)
{
	Instance* const instance = static_cast<Instance*>(lua_touserdata(state, lua_upvalueindex(1)));
	if(!instance || !instance->owner || !instance->owner->host)
	{
		luaL_error(state, "Plugin host is not available.");
		return 0;
	}
	if(instance->stopRequested.load())
		luaL_error(state, "The plugin is stopping and cannot show notifications.");
	if(!instance->permissions.contains(kNotificationPermission))
	{
		luaL_error(state, "The plugin did not request the ui.notification capability.");
		return 0;
	}
	if(instance->notifications >= 3)
	{
		luaL_error(state, "Plugin notification limit exceeded for this execution.");
		return 0;
	}

	size_t title_size = 0;
	size_t body_size = 0;
	const char* title_utf8 = luaL_checklstring(state, 1, &title_size);
	const char* body_utf8 = luaL_checklstring(state, 2, &body_size);
	if(title_size == 0 || title_size > 80 || body_size == 0 || body_size > 500)
	{
		luaL_error(state, "Notification text is empty or exceeds its size limit.");
		return 0;
	}

	QString title;
	QString body;
	{
		title = QString::fromUtf8(title_utf8, static_cast<int>(title_size)).trimmed();
		body = QString::fromUtf8(body_utf8, static_cast<int>(body_size)).trimmed();
	}
	if(title.isEmpty() || body.isEmpty() || title.contains(QChar::Null) || body.contains(QChar::Null))
	{
		luaL_error(state, "Notification text is invalid.");
		return 0;
	}
	++instance->notifications;
	instance->owner->host->showNotification(instance->pluginId, title, body);
	return 0;
}


bool PluginRuntime::readPreference(const QString& pluginId, PersistentPreference& preference, QString& error) const
{
	QMutexLocker preference_lock(&preference_mutex);
	preference = PersistentPreference();
	const QString directory = package_manager.settingsDirectoryForId(pluginId);
	if(directory.isEmpty())
	{
		error = QStringLiteral("The plugin settings path is invalid.");
		return false;
	}
	QFileInfo directory_info(directory);
	if(directory_info.exists() && (!directory_info.isDir() || directory_info.isSymLink()))
	{
		error = QStringLiteral("The plugin settings directory is not a regular directory.");
		return false;
	}
	const QString path = QDir(directory).filePath(QStringLiteral("runtime.json"));
	QFileInfo preference_info(path);
	if(!preference_info.exists())
		return true;
	if(preference_info.isSymLink() || preference_info.size() > 4096)
	{
		error = QStringLiteral("The plugin runtime preference file is unsafe or oversized.");
		return false;
	}
	QFile file(path);
	if(!file.open(QIODevice::ReadOnly))
	{
		error = QStringLiteral("The plugin runtime preference file could not be read.");
		return false;
	}
	QJsonParseError parse_error;
	const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parse_error);
	if(parse_error.error != QJsonParseError::NoError || !document.isObject() ||
		document.object().value(QStringLiteral("schemaVersion")).toInt(-1) != 1)
	{
		error = QStringLiteral("The plugin runtime preference file is invalid.");
		return false;
	}
	const QJsonObject object = document.object();
	preference.approvedPackageSha256 = object.value(QStringLiteral("approvedPackageSha256")).toString();
	preference.enabledVersion = object.value(QStringLiteral("enabledVersion")).toString();
	preference.enabled = object.value(QStringLiteral("enabled")).toBool(false);
	return true;
}


bool PluginRuntime::writePreference(const QString& pluginId, const PersistentPreference& preference, QString& error) const
{
	QMutexLocker preference_lock(&preference_mutex);
	const QString directory = package_manager.settingsDirectoryForId(pluginId);
	if(directory.isEmpty())
	{
		error = QStringLiteral("The plugin settings path is invalid.");
		return false;
	}
	QDir parent(QFileInfo(directory).absolutePath());
	QFileInfo parent_info(parent.absolutePath());
	if(parent_info.exists() && (!parent_info.isDir() || parent_info.isSymLink()))
	{
		error = QStringLiteral("The plugin settings parent is not a regular directory.");
		return false;
	}
	if(!parent.exists() && !QDir().mkpath(parent.absolutePath()))
	{
		error = QStringLiteral("The plugin settings parent could not be created.");
		return false;
	}
	QFileInfo directory_info(directory);
	if(directory_info.exists() && (!directory_info.isDir() || directory_info.isSymLink()))
	{
		error = QStringLiteral("The plugin settings directory is not a regular directory.");
		return false;
	}
	if(!directory_info.exists() && !QDir().mkpath(directory))
	{
		error = QStringLiteral("The plugin settings directory could not be created.");
		return false;
	}
	const QString path = QDir(directory).filePath(QStringLiteral("runtime.json"));
	if(QFileInfo(path).isSymLink())
	{
		error = QStringLiteral("The plugin runtime preference file cannot be a symbolic link.");
		return false;
	}
	QJsonObject object;
	object.insert(QStringLiteral("schemaVersion"), 1);
	object.insert(QStringLiteral("approvedPackageSha256"), preference.approvedPackageSha256);
	object.insert(QStringLiteral("enabledVersion"), preference.enabledVersion);
	object.insert(QStringLiteral("enabled"), preference.enabled);
	QSaveFile file(path);
	const QByteArray data = QJsonDocument(object).toJson(QJsonDocument::Compact);
	if(!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
	{
		error = QStringLiteral("The plugin runtime preference could not be saved.");
		return false;
	}
	return true;
}


bool PluginRuntime::requiresExplicitTrust(const QString& pluginId, const QString& version) const
{
	for(const PluginPackageInfo& info : package_manager.installedPlugins())
		if(info.id == pluginId && info.version == version)
		{
			PersistentPreference preference;
			QString error;
			return !readPreference(pluginId, preference, error) ||
				preference.approvedPackageSha256 != QString::fromLatin1(info.installedContentSha256.toHex());
		}
	return true;
}


bool PluginRuntime::enable(const QString& pluginId, const QString& version, const bool explicitUserConsent, QString& error)
{
	error.clear();
#if !defined(METASIBERIA_PLUGIN_TESTING)
	if(!isolated_execution && !worker_process_context)
	{
		error = QStringLiteral("In-process plugin execution is disabled in production builds.");
		return false;
	}
#endif
	if(!host || !host->isUiThread())
	{
		error = QStringLiteral("Plugin Runtime can only start on the client UI thread.");
		return false;
	}
	const QString key = instanceKey(pluginId, version);
	if(active_instances.contains(key))
	{
		if(active_instances.value(key)->stopRequested.load())
		{
			error = QStringLiteral("The previous plugin instance is still stopping. Try again in a moment.");
			return false;
		}
		return true; // Repeated clicks do not create a second VM or command.
	}

	PluginPackageInfo info;
	bool found = false;
	for(const PluginPackageInfo& installed : package_manager.installedPlugins())
		if(installed.id == pluginId && installed.version == version)
		{
			info = installed;
			found = true;
			break;
		}
	if(!found || !info.installed)
	{
		error = QStringLiteral("The selected plugin version is not installed.");
		setStatus(pluginId, version, PluginRuntimeStatus{false, false, error});
		return false;
	}
	if(info.source != kLocalPackageSource)
	{
		error = QStringLiteral("Only explicitly trusted local packages can run in this prototype. Catalog packages remain inert.");
		setStatus(pluginId, version, PluginRuntimeStatus{false, false, error});
		return false;
	}
	if(info.runtime != QStringLiteral("luau"))
	{
		error = QStringLiteral("This prototype can execute only Luau packages.");
		setStatus(pluginId, version, PluginRuntimeStatus{false, false, error});
		return false;
	}
	if(!info.apiCompatible)
	{
		error = QStringLiteral("The package is incompatible with this Plugin API version.");
		setStatus(pluginId, version, PluginRuntimeStatus{false, false, error});
		return false;
	}
	if(info.packageSha256.size() != 32 || info.installedContentSha256.size() != 32)
	{
		error = QStringLiteral("The installed package has no verified identity hash. Reinstall it from a reviewed local .mspkg file.");
		setStatus(pluginId, version, PluginRuntimeStatus{false, false, error});
		return false;
	}
	for(const QString& permission : info.permissions)
		if(permission != kCommandPermission && permission != kNotificationPermission && permission != kPanelPermission &&
			permission != kSceneSummaryPermission)
		{
			error = QStringLiteral("The package requests an unsupported capability: %1").arg(permission);
			setStatus(pluginId, version, PluginRuntimeStatus{false, false, error});
			return false;
		}

	PersistentPreference preference;
	if(!readPreference(pluginId, preference, error))
	{
		setStatus(pluginId, version, PluginRuntimeStatus{false, false, error});
		return false;
	}
	const QString digest = QString::fromLatin1(info.installedContentSha256.toHex());
	if(preference.approvedPackageSha256 != digest && !explicitUserConsent)
	{
		error = QStringLiteral("Explicit trust confirmation is required for this package version and hash.");
		setStatus(pluginId, version, PluginRuntimeStatus{false, false, error});
		return false;
	}
	QString other_active_version;
	for(auto it = active_instances.constBegin(); it != active_instances.constEnd(); ++it)
		if(it.value()->pluginId == pluginId && it.value()->version != version)
		{
			other_active_version = it.value()->version;
			break;
		}
	if(!other_active_version.isEmpty())
	{
		QString disable_error;
		if(!disable(pluginId, other_active_version, disable_error))
		{
			error = QStringLiteral("Another version of this plugin could not be stopped safely: %1").arg(disable_error);
			setStatus(pluginId, version, PluginRuntimeStatus{false, false, error});
			return false;
		}
		if(active_instances.contains(instanceKey(pluginId, other_active_version)))
		{
			error = QStringLiteral("The previous plugin version is still stopping. Try again in a moment.");
			setStatus(pluginId, version, PluginRuntimeStatus{false, false, error});
			return false;
		}
	}
	if(explicitUserConsent)
		preference.approvedPackageSha256 = digest;
	preference.enabledVersion = version;
	preference.enabled = true;
	if(!writePreference(pluginId, preference, error))
	{
		setStatus(pluginId, version, PluginRuntimeStatus{false, false, error});
		return false;
	}
	const std::shared_ptr<Instance> instance = std::make_shared<Instance>();
	instance->owner = this;
	instance->ownerLifetime = callback_lifetime;
	instance->pluginId = info.id;
	instance->version = info.version;
	instance->permissions = info.permissions;
	instance->self = instance;
	active_instances.insert(key, instance);
	instance->started_at.start();
		logLifecycle(isolated_execution ? QStringLiteral("worker_start_requested") : QStringLiteral("in_process_start_requested"),
		info.id, info.version);
	if(isolated_execution)
	{
		QCoreApplication* const application = QCoreApplication::instance();
		if(!application)
		{
			active_instances.remove(key);
			preference.enabled = false;
			preference.enabledVersion.clear();
			writePreference(pluginId, preference, error);
			error = QStringLiteral("The Qt application event loop is unavailable for the isolated runtime.");
			setStatus(pluginId, version, PluginRuntimeStatus{false, false, error});
			return false;
		}
		QProcess* const process = new QProcess(application);
		instance->process = process;
		process->setProcessChannelMode(QProcess::SeparateChannels);
		const std::weak_ptr<int> lifetime = callback_lifetime;
		QObject::connect(process, &QProcess::readyReadStandardOutput, application, [this, lifetime, instance]() {
			if(lifetime.expired()) return;
			processWorkerOutput(instance);
		});
		QObject::connect(process, &QProcess::readyReadStandardError, application, [lifetime, process]() {
			if(lifetime.expired()) return;
			// Drain child diagnostics without copying script/source data into the
			// client log or letting QProcess buffer unbounded stderr output.
			process->readAllStandardError();
		});
		QObject::connect(process, &QProcess::started, application, [this, lifetime, instance]() {
			if(lifetime.expired()) return;
#if defined(METASIBERIA_PLUGIN_TESTING)
			++worker_process_started_count;
#endif
			logLifecycle(QStringLiteral("worker_process_started"), instance->pluginId, instance->version,
				QStringLiteral("child_process"), instance->started_at.isValid() ? instance->started_at.elapsed() : -1);
		});
		QObject::connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), application,
			[this, lifetime, instance](const int exitCode, const QProcess::ExitStatus exitStatus) {
				if(lifetime.expired()) return;
#if defined(METASIBERIA_PLUGIN_TESTING)
				++worker_process_finished_count;
#endif
				logLifecycle(QStringLiteral("worker_process_finished"), instance->pluginId, instance->version,
					exitStatus == QProcess::CrashExit ? QStringLiteral("crash_exit") : QStringLiteral("normal_exit"),
					instance->started_at.isValid() ? instance->started_at.elapsed() : -1);
				handleWorkerFinished(instance, exitCode, static_cast<int>(exitStatus));
			});
		QObject::connect(process, &QProcess::errorOccurred, application, [this, lifetime, instance](const QProcess::ProcessError processError) {
			if(lifetime.expired()) return;
			if(processError == QProcess::FailedToStart)
			{
				const QString start_error = instance->process ? instance->process->errorString() : QString();
				qWarning() << "Plugin worker failed to start" << instance->pluginId << instance->version << start_error;
				setStatus(instance->pluginId, instance->version,
					PluginRuntimeStatus{false, false, QStringLiteral("Could not start the plugin worker: %1").arg(start_error)});
				logLifecycle(QStringLiteral("worker_start_failed"), instance->pluginId, instance->version,
					QStringLiteral("failed_to_start"));
				handleWorkerFinished(instance, -1, static_cast<int>(QProcess::CrashExit));
			}
		});
		const QString executable = QCoreApplication::applicationFilePath();
		const QStringList arguments = {
			QStringLiteral("--plugin-runtime-worker"), package_manager.rootDirectory(),
			package_manager.settingsRootDirectory(), info.id, info.version
		};
		process->start(executable, arguments, QIODevice::ReadWrite);
		setStatus(pluginId, version, PluginRuntimeStatus{true, false, QString()});
		QTimer::singleShot(15000, process, [this, lifetime, instance]() {
			if(lifetime.expired()) return;
			if(instance->process && instance->process->state() != QProcess::NotRunning &&
				!status(instance->pluginId, instance->version).running)
			{
				logLifecycle(QStringLiteral("startup_timeout"), instance->pluginId, instance->version,
					QStringLiteral("startup_timeout"));
				setStatus(instance->pluginId, instance->version,
					PluginRuntimeStatus{false, false, QStringLiteral("Plugin worker startup exceeded 15 seconds and was stopped.")});
				instance->expected_process_exit = true;
				instance->stopRequested.store(true);
				if(host) host->removePluginResources(instance->pluginId, instance->version);
				instance->process->kill();
			}
		});
		return true;
	}
	try
	{
		instance->worker = std::thread([this, instance, info]() { runInstance(instance, info); });
	}
	catch(const std::exception& exception)
	{
		active_instances.remove(key);
		if(host) host->removePluginResources(pluginId, version);
		preference.enabled = false;
		preference.enabledVersion.clear();
		QString preference_error;
		if(!writePreference(pluginId, preference, preference_error) && !preference_error.isEmpty())
			error = QStringLiteral("Could not start the plugin worker: %1; preference rollback failed: %2")
				.arg(QString::fromLocal8Bit(exception.what()), preference_error);
		else
			error = QStringLiteral("Could not start the plugin worker: %1").arg(QString::fromLocal8Bit(exception.what()));
		setStatus(pluginId, version, PluginRuntimeStatus{false, false, error});
		return false;
	}
	setStatus(pluginId, version, PluginRuntimeStatus{true, false, QString()});
	return true;
}


void PluginRuntime::enqueueCommand(const std::shared_ptr<Instance>& instance)
{
	if(!instance || instance->stopRequested.load())
		return;
	{
		std::lock_guard<std::mutex> lock(instance->queueMutex);
		if(instance->stopRequested.load() || instance->queuedCommands.size() >= 8)
			return;
		instance->queuedCommands.push_back(QueuedPluginInvocation{false, QString()});
	}
	instance->queueWake.notify_one();
}


void PluginRuntime::enqueuePanelAction(const std::shared_ptr<Instance>& instance, const QString& actionId)
{
	if(!instance || instance->stopRequested.load() || actionId.isEmpty() || actionId.size() > 64)
		return;
	{
		std::lock_guard<std::mutex> lock(instance->queueMutex);
		if(instance->stopRequested.load() || instance->queuedCommands.size() >= 8)
			return;
		instance->queuedCommands.push_back(QueuedPluginInvocation{true, actionId});
	}
	instance->queueWake.notify_one();
}


void PluginRuntime::runInstance(const std::shared_ptr<Instance>& instance, const PluginPackageInfo& info)
{
	QString error;
	instance->executing.store(true);
	const bool started = startVerifiedPlugin(info, instance, error);
	instance->executing.store(false);
	if(started && !instance->stopRequested.load())
		postWorkerCompletion(instance, PluginRuntimeStatus{true, true, QString()}, true);

	if(started && !instance->stopRequested.load())
	{
		for(;;)
		{
			QueuedPluginInvocation invocation;
			{
				std::unique_lock<std::mutex> lock(instance->queueMutex);
				instance->queueWake.wait(lock, [&instance]() {
					return instance->stopRequested.load() || !instance->queuedCommands.empty();
				});
				if(instance->stopRequested.load())
					break;
				invocation = instance->queuedCommands.front();
				instance->queuedCommands.pop_front();
			}
			instance->executing.store(true);
			const bool invocation_succeeded = invocation.panelAction
				? invokePanelAction(instance, invocation.actionId, error)
				: invokeCommand(instance, error);
			if(!invocation_succeeded)
			{
				instance->executing.store(false);
				break;
			}
			instance->executing.store(false);
		}
	}

	const bool cancelled = instance->stopRequested.load();
	if(!error.isEmpty() && !cancelled)
	{
		instance->stopRequested.store(true);
		PersistentPreference preference;
		QString preference_error;
		if(readPreference(instance->pluginId, preference, preference_error))
		{
			preference.enabled = false;
			preference.enabledVersion.clear();
			if(!writePreference(instance->pluginId, preference, preference_error) && !preference_error.isEmpty())
				error += QStringLiteral(" Preference rollback failed: %1").arg(preference_error);
		}
	}
	if(host)
		host->removePluginResources(instance->pluginId, instance->version);
	if(instance->state)
	{
		lua_close(instance->state);
		instance->state = nullptr;
	}
	instance->workerFinished.store(true);
	const bool report_error = !error.isEmpty() && !cancelled;
	postWorkerCompletion(instance, report_error ? PluginRuntimeStatus{false, false, error} : PluginRuntimeStatus(), report_error);
}


void PluginRuntime::postWorkerCompletion(const std::shared_ptr<Instance>& instance,
	const PluginRuntimeStatus& worker_status, const bool updateStatus)
{
	QCoreApplication* const application = QCoreApplication::instance();
	if(!application)
		return;
	const std::weak_ptr<int> lifetime = callback_lifetime;
	QMetaObject::invokeMethod(application, [this, lifetime, instance, worker_status, updateStatus]() {
		if(lifetime.expired() || shutting_down)
			return;
		if(updateStatus && (!instance->stopRequested.load() || !worker_status.error.isEmpty()))
			setStatus(instance->pluginId, instance->version, worker_status);
		if(instance->workerFinished.load())
		{
			if(instance->worker.joinable()) instance->worker.join();
			const QString key = instanceKey(instance->pluginId, instance->version);
			const auto it = active_instances.find(key);
			if(it != active_instances.end() && it.value() == instance)
				active_instances.erase(it);
			if(!updateStatus && state_changed_callback)
				state_changed_callback();
		}
	}, Qt::QueuedConnection);
}


bool PluginRuntime::startVerifiedPlugin(const PluginPackageInfo& info,
	const std::shared_ptr<Instance>& instance, QString& error)
{
	if(instance->stopRequested.load())
	{
		error = QStringLiteral("Plugin startup was cancelled.");
		return false;
	}
	PluginPackageInfo verified_info;
	if(!PluginPackageValidator::readInstalledDirectory(info.installPath, verified_info, error, false))
		return false;
	if(verified_info.id != info.id || verified_info.version != info.version ||
		verified_info.packageSha256 != info.packageSha256 ||
		verified_info.installedContentSha256 != info.installedContentSha256 || verified_info.source != kLocalPackageSource)
	{
		error = QStringLiteral("Installed package identity changed after it was selected.");
		return false;
	}
	if(!isSafeRelativePath(verified_info.entryPoint) || !verified_info.entryPoint.endsWith(QStringLiteral(".luau"), Qt::CaseInsensitive))
	{
		error = QStringLiteral("The Luau entrypoint must be a safe package-relative .luau file.");
		return false;
	}
	const QString canonical_root = QFileInfo(info.installPath).canonicalFilePath();
	const QString entry_path = QDir(canonical_root).filePath(verified_info.entryPoint);
	QFileInfo entry_info(entry_path);
	if(entry_info.isSymLink() || !entry_info.isFile() || entry_info.size() <= 0 || entry_info.size() > MaximumEntryBytes ||
		!isCanonicalChild(entry_info.canonicalFilePath(), canonical_root))
	{
		error = QStringLiteral("The Luau entrypoint is missing, unsafe or larger than 256 KiB.");
		return false;
	}
	QFile entry_file(entry_info.canonicalFilePath());
	if(!entry_file.open(QIODevice::ReadOnly))
	{
		error = QStringLiteral("The Luau entrypoint could not be read.");
		return false;
	}
	const QByteArray source = entry_file.readAll();
	if(source.size() != entry_info.size() || source.size() > MaximumEntryBytes)
	{
		error = QStringLiteral("The Luau entrypoint changed while it was being read.");
		return false;
	}
	PluginPackageInfo final_verified_info;
	if(!PluginPackageValidator::readInstalledDirectory(info.installPath, final_verified_info, error, false) ||
		final_verified_info.packageSha256 != info.packageSha256 ||
		final_verified_info.installedContentSha256 != info.installedContentSha256)
	{
		if(error.isEmpty()) error = QStringLiteral("Installed package integrity changed during startup.");
		return false;
	}

	instance->state = lua_newstate(&PluginRuntime::limitedAllocator, instance.get());
	if(!instance->state)
	{
		error = QStringLiteral("The Luau VM could not start within its 16 MiB memory limit.");
		return false;
	}
	lua_callbacks(instance->state)->userdata = instance.get();
	lua_callbacks(instance->state)->interrupt = &PluginRuntime::instructionInterrupt;

	openLibrary(instance->state, luaopen_base, "");
	openLibrary(instance->state, luaopen_table, LUA_TABLIBNAME);
	openLibrary(instance->state, luaopen_string, LUA_STRLIBNAME);
	openLibrary(instance->state, luaopen_math, LUA_MATHLIBNAME);
	openLibrary(instance->state, luaopen_bit32, LUA_BITLIBNAME);
	openLibrary(instance->state, luaopen_buffer, LUA_BUFFERLIBNAME);
	openLibrary(instance->state, luaopen_utf8, LUA_UTF8LIBNAME);
	for(const char* blocked_global : {"collectgarbage", "getfenv", "setfenv", "loadstring"})
	{
		lua_pushnil(instance->state);
		lua_setglobal(instance->state, blocked_global);
	}
	lua_createtable(instance->state, 0, 5);
	lua_pushlightuserdata(instance->state, instance.get());
	lua_pushcclosure(instance->state, &PluginRuntime::registerCommandApi, "ui.registerCommand", 1);
	lua_setfield(instance->state, -2, "registerCommand");
	lua_pushlightuserdata(instance->state, instance.get());
	lua_pushcclosure(instance->state, &PluginRuntime::registerPanelApi, "ui.registerPanel", 1);
	lua_setfield(instance->state, -2, "registerPanel");
	lua_pushlightuserdata(instance->state, instance.get());
	lua_pushcclosure(instance->state, &PluginRuntime::closeViewApi, "ui.closeView", 1);
	lua_setfield(instance->state, -2, "closeView");
	lua_pushlightuserdata(instance->state, instance.get());
	lua_pushcclosure(instance->state, &PluginRuntime::notificationApi, "ui.showNotification", 1);
	lua_setfield(instance->state, -2, "showNotification");
	lua_pushlightuserdata(instance->state, instance.get());
	lua_pushcclosure(instance->state, &PluginRuntime::notificationApi, "metasiberia.notify", 1);
	lua_setfield(instance->state, -2, "notify");
	lua_pushvalue(instance->state, -1);
	lua_setglobal(instance->state, "ui");
	lua_setglobal(instance->state, "metasiberia"); // Compatibility alias for early v1.0 packages.
	lua_createtable(instance->state, 0, 1);
	lua_pushlightuserdata(instance->state, instance.get());
	lua_pushcclosure(instance->state, &PluginRuntime::sceneSummaryApi, "scene.getSummary", 1);
	lua_setfield(instance->state, -2, "getSummary");
	lua_setglobal(instance->state, "scene");
	luaL_sandbox(instance->state);

	lua_CompileOptions compile_options = {};
	compile_options.optimizationLevel = 1;
	compile_options.debugLevel = 1;
	size_t bytecode_size = 0;
	char* const bytecode = luau_compile(source.constData(), static_cast<size_t>(source.size()), &compile_options, &bytecode_size);
	if(!bytecode || bytecode_size == 0)
	{
		if(bytecode) std::free(bytecode);
		error = QStringLiteral("Luau compiler did not produce bytecode.");
		return false;
	}
	const QByteArray chunk_name = (QStringLiteral("@") + info.entryPoint).toUtf8();
	const int load_status = luau_load(instance->state, chunk_name.constData(), bytecode, bytecode_size, 0);
	std::free(bytecode);
	if(load_status != LUA_OK)
	{
		error = errorFromLua(instance->state);
		return false;
	}
	instance->safepoints = 0;
	instance->notifications = 0;
	instance->executionClock.start();
	const int run_status = lua_pcall(instance->state, 0, 0, 0);
	instance->executionClock.invalidate();
	if(run_status != LUA_OK)
	{
		error = errorFromLua(instance->state);
		return false;
	}
	instance->initializing = false;
	if(instance->commandReference < 0 && instance->panelActionReference < 0)
	{
		error = QStringLiteral("The plugin entrypoint did not register a primary menu action.");
		return false;
	}
	return true;
}


bool PluginRuntime::invokeCommand(const std::shared_ptr<Instance>& instance, QString& error)
{
	error.clear();
	if(!instance || !instance->state || instance->stopRequested.load())
	{
		error = QStringLiteral("The plugin command is no longer active.");
		return false;
	}
	lua_rawgeti(instance->state, LUA_REGISTRYINDEX, instance->commandReference);
	instance->safepoints = 0;
	instance->notifications = 0;
	instance->executionClock.start();
	const int status = lua_pcall(instance->state, 0, 0, 0);
	instance->executionClock.invalidate();
	if(status != LUA_OK)
	{
		error = errorFromLua(instance->state);
		return false;
	}
	return true;
}


bool PluginRuntime::invokePanelAction(const std::shared_ptr<Instance>& instance, const QString& actionId, QString& error)
{
	error.clear();
	if(!instance || !instance->state || instance->stopRequested.load() || instance->panelActionReference < 0 ||
		actionId.isEmpty() || actionId.size() > 64)
	{
		error = QStringLiteral("The plugin panel action is no longer active.");
		return false;
	}
	lua_rawgeti(instance->state, LUA_REGISTRYINDEX, instance->panelActionReference);
	const QByteArray action_id_utf8 = actionId.toUtf8();
	lua_pushlstring(instance->state, action_id_utf8.constData(), static_cast<size_t>(action_id_utf8.size()));
	instance->safepoints = 0;
	instance->notifications = 0;
	instance->executionClock.start();
	const int status = lua_pcall(instance->state, 1, 0, 0);
	instance->executionClock.invalidate();
	if(status != LUA_OK)
	{
		error = errorFromLua(instance->state);
		return false;
	}
	return true;
}


bool PluginRuntime::disable(const QString& pluginId, const QString& version, QString& error)
{
	error.clear();
	if(host && !host->isUiThread())
	{
		error = QStringLiteral("Plugin enable/disable actions must be requested on the client UI thread.");
		return false;
	}
	const QString key = instanceKey(pluginId, version);
	const std::shared_ptr<Instance> instance = active_instances.value(key);
	if(instance)
	{
		instance->stopRequested.store(true);
		if(host) host->removePluginResources(pluginId, version);
		if(instance->process)
		{
			instance->expected_process_exit = true;
			instance->user_requested_stop = true;
			QJsonObject stop_message;
			stop_message.insert(QStringLiteral("type"), QStringLiteral("stop"));
			sendWorkerMessage(instance, stop_message);
			instance->process->closeWriteChannel();
			QProcess* const process = instance->process;
			QTimer::singleShot(2000, process, [process]() {
				if(process->state() != QProcess::NotRunning)
					process->kill();
			});
			logLifecycle(QStringLiteral("stop_requested"), pluginId, version, QStringLiteral("user_request"));
		}
		else
		{
			instance->queueWake.notify_all();
		if(!instance->executing.load() && instance->worker.joinable())
		{
			instance->worker.join();
			active_instances.remove(key);
		}
		}
	}
	PersistentPreference preference;
	if(!readPreference(pluginId, preference, error))
	{
		setStatus(pluginId, version, PluginRuntimeStatus{false, false, error});
		return false;
	}
	preference.enabled = false;
	preference.enabledVersion.clear();
	if(!writePreference(pluginId, preference, error))
	{
		setStatus(pluginId, version, PluginRuntimeStatus{false, false, error});
		return false;
	}
	setStatus(pluginId, version, PluginRuntimeStatus());
	return true;
}


void PluginRuntime::restorePreviouslyEnabledLocalPlugins()
{
	if(!host || !host->isUiThread())
		return;
	for(const PluginPackageInfo& info : package_manager.installedPlugins())
	{
		PersistentPreference preference;
		QString error;
		if(!readPreference(info.id, preference, error))
		{
			setStatus(info.id, info.version, PluginRuntimeStatus{false, false, error});
			continue;
		}
		if(!preference.enabled)
			continue;
		if(preference.enabledVersion != info.version)
			continue;
		if(preference.approvedPackageSha256 != QString::fromLatin1(info.installedContentSha256.toHex()))
		{
			preference.enabled = false;
			preference.enabledVersion.clear();
			writePreference(info.id, preference, error);
			setStatus(info.id, info.version, PluginRuntimeStatus{false, false,
				QStringLiteral("Package identity changed; explicit trust confirmation is required again.")});
			continue;
		}
		if(!enable(info.id, info.version, false, error))
			setStatus(info.id, info.version, PluginRuntimeStatus{false, false, error});
	}
	if(state_changed_callback) state_changed_callback();
}


void PluginRuntime::shutdown()
{
	shutting_down = true;
	for(auto it = active_instances.begin(); it != active_instances.end(); ++it)
	{
		const std::shared_ptr<Instance>& instance = it.value();
		instance->preservePreferenceOnStop.store(true);
		instance->stopRequested.store(true);
		if(host) host->removePluginResources(instance->pluginId, instance->version);
		if(instance->process)
		{
			instance->expected_process_exit = true;
			// Client shutdown must never wait for plugin code on the Qt UI thread.
			QObject::disconnect(instance->process, nullptr, QCoreApplication::instance(), nullptr);
			instance->process->kill();
			instance->process->deleteLater();
			instance->process = nullptr;
		}
		else
			instance->queueWake.notify_all();
	}
	for(auto it = active_instances.begin(); it != active_instances.end(); ++it)
	{
		const std::shared_ptr<Instance>& instance = it.value();
		if(instance->worker.joinable()) instance->worker.join();
		runtime_statuses.insert(instanceKey(instance->pluginId, instance->version),
			PluginRuntimeStatus{true, false, QString()});
	}
	active_instances.clear();
	host = nullptr;
}


PluginRuntimeStatus PluginRuntime::status(const QString& pluginId, const QString& version) const
{
	const QString key = instanceKey(pluginId, version);
	return runtime_statuses.value(key);
}


bool PluginRuntime::hasActiveInstance(const QString& pluginId, const QString& version) const
{
	return active_instances.contains(instanceKey(pluginId, version));
}


#if defined(METASIBERIA_PLUGIN_TESTING)
void PluginRuntime::terminateWorkerForTest(const QString& pluginId, const QString& version)
{
	const std::shared_ptr<Instance> instance = active_instances.value(instanceKey(pluginId, version));
	if(instance && instance->process)
		instance->process->kill();
}
#endif


void PluginRuntime::logLifecycle(const QString& stage, const QString& pluginId, const QString& version,
	const QString& code, const qint64 durationMs)
{
	QString directory = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
	if(directory.isEmpty()) directory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
	if(directory.isEmpty() || !QDir().mkpath(directory)) return;
	const QString path = QDir(directory).filePath(QStringLiteral("plugin-lifecycle.jsonl"));
	QFileInfo path_info(path);
	if(path_info.isSymLink()) return;
	if(path_info.exists() && path_info.size() > 1024 * 1024)
	{
		const QString rotated = path + QStringLiteral(".1");
		if(QFileInfo(rotated).isSymLink()) return;
		QFile::remove(rotated);
		QFile::rename(path, rotated);
	}
	QJsonObject entry;
	entry.insert(QStringLiteral("timestampUtc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
	entry.insert(QStringLiteral("stage"), stage.left(64));
	entry.insert(QStringLiteral("pluginId"), pluginId.left(128));
	entry.insert(QStringLiteral("version"), version.left(64));
	if(!code.isEmpty()) entry.insert(QStringLiteral("code"), code.left(64));
	if(durationMs >= 0) entry.insert(QStringLiteral("durationMs"), durationMs);
	QFile file(path);
	if(file.open(QIODevice::WriteOnly | QIODevice::Append))
	{
		file.write(QJsonDocument(entry).toJson(QJsonDocument::Compact));
		file.write("\n");
	}
}


void PluginRuntime::sendWorkerMessage(const std::shared_ptr<Instance>& instance, const QJsonObject& message)
{
	if(!instance || !instance->process || instance->process->state() == QProcess::NotRunning) return;
	QJsonObject versioned = message;
	versioned.insert(QStringLiteral("protocolMajor"), 1);
	versioned.insert(QStringLiteral("protocolMinor"), 0);
	QByteArray line = QJsonDocument(versioned).toJson(QJsonDocument::Compact);
	if(line.size() > 8192) return;
	line.append('\n');
	if(instance->process->bytesToWrite() + line.size() > 64 * 1024)
	{
		logLifecycle(QStringLiteral("ipc_rejected"), instance->pluginId, instance->version, QStringLiteral("input_queue_limit"));
		return;
	}
	instance->process->write(line);
}


void PluginRuntime::processWorkerOutput(const std::shared_ptr<Instance>& instance)
{
	if(!instance || !instance->process) return;
	if(active_instances.value(instanceKey(instance->pluginId, instance->version)) != instance) return;
	instance->process_output += instance->process->readAllStandardOutput();
	if(instance->process_output.size() > 64 * 1024)
	{
		logLifecycle(QStringLiteral("ipc_rejected"), instance->pluginId, instance->version, QStringLiteral("output_limit"));
		instance->expected_process_exit = true;
		setStatus(instance->pluginId, instance->version, PluginRuntimeStatus{false, false,
			QStringLiteral("Plugin worker exceeded the bounded IPC output limit and was stopped.")});
		if(host) host->removePluginResources(instance->pluginId, instance->version);
		instance->process->kill();
		return;
	}
	for(;;)
	{
		const qsizetype newline = instance->process_output.indexOf('\n');
		if(newline < 0) break;
		const QByteArray line = instance->process_output.left(newline);
		instance->process_output.remove(0, newline + 1);
		if(line.isEmpty() || line.size() > 8192) continue;
		QJsonParseError parse_error;
		const QJsonDocument document = QJsonDocument::fromJson(line, &parse_error);
		if(parse_error.error != QJsonParseError::NoError || !document.isObject())
		{
			logLifecycle(QStringLiteral("ipc_rejected"), instance->pluginId, instance->version, QStringLiteral("invalid_json"));
			continue;
		}
		const QJsonObject message = document.object();
		if(message.contains(QStringLiteral("protocolMajor")) &&
			message.value(QStringLiteral("protocolMajor")).toInt(-1) != 1)
		{
			logLifecycle(QStringLiteral("ipc_rejected"), instance->pluginId, instance->version,
				QStringLiteral("protocol_version"));
			continue;
		}
		const QString type = message.value(QStringLiteral("type")).toString();
		if(type == QStringLiteral("registerCommand") || type == QStringLiteral("registerPanel"))
		{
			const QString plugin_id = message.value(QStringLiteral("pluginId")).toString();
			const QString version = message.value(QStringLiteral("pluginVersion")).toString();
			const QString title = message.value(QStringLiteral("title")).toString();
			QString error;
			bool registered = false;
			if(!instance->stopRequested.load() && plugin_id == instance->pluginId && version == instance->version && host)
			{
				const std::weak_ptr<Instance> weak_instance = instance;
				if(type == QStringLiteral("registerCommand"))
				{
					const QString command_id = message.value(QStringLiteral("commandId")).toString();
					registered = host->registerCommand(plugin_id, version, command_id, title,
						[this, weak_instance]() {
							if(const std::shared_ptr<Instance> active = weak_instance.lock())
							{
								if(active->stopRequested.load()) return;
								logLifecycle(QStringLiteral("command_invoked"), active->pluginId, active->version,
									QStringLiteral("plugins_menu"));
								QJsonObject invoke;
								invoke.insert(QStringLiteral("type"), QStringLiteral("invoke"));
								sendWorkerMessage(active, invoke);
							}
						}, error);
				}
				else
				{
					const QString view_id = message.value(QStringLiteral("viewId")).toString();
					const QJsonObject layout = message.value(QStringLiteral("layout")).toObject();
					registered = host->registerPanel(plugin_id, version, view_id, title, layout,
						[this, weak_instance, view_id](const QString& action_id) {
							if(const std::shared_ptr<Instance> active = weak_instance.lock())
							{
								if(active->stopRequested.load()) return;
								logLifecycle(QStringLiteral("panel_action_invoked"), active->pluginId, active->version,
									action_id.left(64));
								QJsonObject invoke;
								invoke.insert(QStringLiteral("type"), QStringLiteral("invokePanelAction"));
								invoke.insert(QStringLiteral("viewId"), view_id);
								invoke.insert(QStringLiteral("actionId"), action_id);
								sendWorkerMessage(active, invoke);
							}
						}, error);
				}
			}
			else error = QStringLiteral("Worker identity does not match the selected package.");
			QJsonObject response;
			response.insert(QStringLiteral("type"), QStringLiteral("registerResult"));
			response.insert(QStringLiteral("requestId"), message.value(QStringLiteral("requestId")).toString());
			response.insert(QStringLiteral("ok"), registered);
			response.insert(QStringLiteral("error"), error.left(300));
			sendWorkerMessage(instance, response);
			logLifecycle(registered ? QStringLiteral("primary_action_registered") : QStringLiteral("primary_action_rejected"),
				instance->pluginId, instance->version, registered ? type : QStringLiteral("host_rejected"));
		}
		else if(type == QStringLiteral("sceneSummary"))
		{
			const QString plugin_id = message.value(QStringLiteral("pluginId")).toString();
			const QString version = message.value(QStringLiteral("pluginVersion")).toString();
			const QString request_id = message.value(QStringLiteral("requestId")).toString();
			QJsonObject summary;
			QString error;
			bool ok = false;
			const int request_count = instance->sceneSummaryRequests.fetch_add(1);
			if(request_id.isEmpty() || request_id.size() > 64 || plugin_id != instance->pluginId ||
				version != instance->version || instance->stopRequested.load())
				error = QStringLiteral("The scene summary request did not match the active plugin.");
			else if(request_count >= kMaximumSceneSummaryRequestsPerPlugin)
				error = QStringLiteral("The plugin exceeded its scene summary request limit.");
			else if(!instance->permissions.contains(kSceneSummaryPermission))
				error = QStringLiteral("The plugin did not request the scene.read.summary capability.");
			else if(!host || !host->isUiThread())
				error = QStringLiteral("The scene summary host is unavailable on the UI thread.");
			else
				ok = host->getSceneSummary(plugin_id, version, summary, error);
			if(ok && QJsonDocument(summary).toJson(QJsonDocument::Compact).size() > 4096)
			{
				ok = false;
				error = QStringLiteral("The scene summary exceeded the 4 KiB IPC limit.");
			}
			QJsonObject response;
			response.insert(QStringLiteral("type"), QStringLiteral("sceneSummaryResult"));
			response.insert(QStringLiteral("requestId"), request_id);
			response.insert(QStringLiteral("ok"), ok);
			response.insert(QStringLiteral("summary"), ok ? summary : QJsonObject());
			response.insert(QStringLiteral("error"), ok ? QString() : error.left(300));
			sendWorkerMessage(instance, response);
		}
		else if(type == QStringLiteral("closeView"))
		{
			const QString plugin_id = message.value(QStringLiteral("pluginId")).toString();
			const QString version = message.value(QStringLiteral("pluginVersion")).toString();
			const QString view_id = message.value(QStringLiteral("viewId")).toString();
			if(!instance->stopRequested.load() && host && plugin_id == instance->pluginId && version == instance->version)
				host->closePanel(plugin_id, version, view_id);
		}
		else if(type == QStringLiteral("notification"))
		{
			const QString plugin_id = message.value(QStringLiteral("pluginId")).toString();
			const QString title = message.value(QStringLiteral("title")).toString();
			const QString body = message.value(QStringLiteral("body")).toString();
			if(!instance->stopRequested.load() && host && plugin_id == instance->pluginId && title.size() <= 80 && body.size() <= 500)
			{
				logLifecycle(QStringLiteral("notification_received"), instance->pluginId, instance->version,
					QStringLiteral("bounded_ipc"));
				host->showNotification(plugin_id, title, body);
			}
		}
		else if(type == QStringLiteral("status"))
		{
			const bool enabled = message.value(QStringLiteral("enabled")).toBool(false);
			const bool running = message.value(QStringLiteral("running")).toBool(false);
			const QString error = message.value(QStringLiteral("error")).toString().left(500);
			setStatus(instance->pluginId, instance->version, PluginRuntimeStatus{enabled, running, error});
			if(!error.isEmpty())
			{
				logLifecycle(QStringLiteral("worker_failed"), instance->pluginId, instance->version, QStringLiteral("runtime_error"));
				if(host) host->removePluginResources(instance->pluginId, instance->version);
				if(instance->process && instance->process->state() != QProcess::NotRunning)
				{
					QJsonObject stop_message;
					stop_message.insert(QStringLiteral("type"), QStringLiteral("stop"));
					sendWorkerMessage(instance, stop_message);
					instance->process->closeWriteChannel();
				}
			}
			else if(running)
				logLifecycle(QStringLiteral("worker_running"), instance->pluginId, instance->version, QStringLiteral("ok"));
		}
	}
}


void PluginRuntime::handleWorkerFinished(const std::shared_ptr<Instance>& instance, const int exitCode, const int exitStatus)
{
	if(!instance) return;
	const QString key = instanceKey(instance->pluginId, instance->version);
	const auto found = active_instances.find(key);
	if(found == active_instances.end() || found.value() != instance) return;
	if(instance->process && !instance->process_output.isEmpty()) processWorkerOutput(instance);
	const bool was_expected = instance->expected_process_exit || instance->stopRequested.load();
	PluginRuntimeStatus current = runtime_statuses.value(key);
	if(!was_expected && current.error.isEmpty())
	{
		current.enabled = false;
		current.running = false;
		current.error = QStringLiteral("Plugin runtime process exited unexpectedly (code %1).").arg(exitCode);
		PersistentPreference preference;
		QString preference_error;
		if(readPreference(instance->pluginId, preference, preference_error))
		{
			preference.enabled = false;
			preference.enabledVersion.clear();
			writePreference(instance->pluginId, preference, preference_error);
		}
		logLifecycle(exitStatus == static_cast<int>(QProcess::CrashExit) ? QStringLiteral("worker_crashed") : QStringLiteral("worker_exited"),
			instance->pluginId, instance->version, QStringLiteral("unexpected_exit"));
	}
	else if(was_expected)
	{
		current.enabled = false;
		current.running = false;
		// A forced exit caused by startup timeout or an IPC violation is still
		// a failure. Clear diagnostics only for the user's explicit disable.
		if(instance->user_requested_stop)
			current.error.clear();
		logLifecycle(QStringLiteral("worker_stopped"), instance->pluginId, instance->version,
			current.error.isEmpty() ? QStringLiteral("ok") : QStringLiteral("worker_failure"),
			instance->started_at.isValid() ? instance->started_at.elapsed() : -1);
	}
	if(!current.error.isEmpty())
	{
		PersistentPreference preference;
		QString preference_error;
		if(readPreference(instance->pluginId, preference, preference_error))
		{
			preference.enabled = false;
			preference.enabledVersion.clear();
			writePreference(instance->pluginId, preference, preference_error);
		}
	}
	if(host) host->removePluginResources(instance->pluginId, instance->version);
	if(instance->process)
	{
		instance->process->deleteLater();
		instance->process = nullptr;
	}
	active_instances.erase(found);
	setStatus(instance->pluginId, instance->version, current);
}


int PluginRuntime::runWorkerProcess(const QStringList& arguments)
{
	if(arguments.size() != 6 || arguments.at(1) != QStringLiteral("--plugin-runtime-worker"))
		return 64;
	qInstallMessageHandler(&pluginWorkerQtMessageHandler);
	QCoreApplication* const application = QCoreApplication::instance();
	if(!application) return 65;
	const QString plugin_root = arguments.at(2);
	const QString settings_root = arguments.at(3);
	const QString plugin_id = arguments.at(4);
	const QString version = arguments.at(5);
	PluginPackageManager package_manager(plugin_root, nullptr, settings_root, false);
#if defined(METASIBERIA_PLUGIN_TESTING)
	PluginRuntime runtime(package_manager, false);
#else
	PluginRuntime runtime(package_manager, WorkerProcessExecutionTag{});
#endif
	PluginWorkerIpcHost host(*application);
	runtime.setHost(&host);
	std::atomic_bool stop_requested{false};
	int process_result = 0;
	runtime.setStateChangedCallback([&]() {
		const PluginRuntimeStatus status = runtime.status(plugin_id, version);
		QJsonObject message;
		message.insert(QStringLiteral("type"), QStringLiteral("status"));
		message.insert(QStringLiteral("enabled"), status.enabled);
		message.insert(QStringLiteral("running"), status.running);
		message.insert(QStringLiteral("error"), status.error.left(500));
		sendPluginWorkerProtocolMessage(message);
		if(!status.error.isEmpty())
		{
			process_result = 1;
			application->quit();
		}
		else if(stop_requested.load() && !runtime.hasActiveInstance(plugin_id, version))
			application->quit();
	});
	std::thread input_reader([&]() {
		std::string line;
		while(std::getline(std::cin, line))
		{
			if(line.size() > 8192) continue;
			const QByteArray bytes(line.data(), static_cast<int>(line.size()));
			QJsonParseError parse_error;
			const QJsonDocument document = QJsonDocument::fromJson(bytes, &parse_error);
			if(parse_error.error == QJsonParseError::NoError && document.isObject())
			{
				const QJsonObject message = document.object();
				if(message.contains(QStringLiteral("protocolMajor")) &&
					message.value(QStringLiteral("protocolMajor")).toInt(-1) != 1) continue;
				if(message.value(QStringLiteral("type")).toString() == QStringLiteral("stop"))
					stop_requested.store(true);
			}
			host.handleInput(bytes, runtime, plugin_id, version);
		}
	});
	QString error;
	if(!runtime.enable(plugin_id, version, false, error))
	{
		QJsonObject failed;
		failed.insert(QStringLiteral("type"), QStringLiteral("status"));
		failed.insert(QStringLiteral("enabled"), false);
		failed.insert(QStringLiteral("running"), false);
		failed.insert(QStringLiteral("error"), error.left(500));
		sendPluginWorkerProtocolMessage(failed);
		process_result = 1;
	}
	else
	{
		QTimer::singleShot(15000, application, [&]() {
			if(!runtime.status(plugin_id, version).running)
			{
				process_result = 1;
				stop_requested.store(true);
				QString ignored;
				runtime.disable(plugin_id, version, ignored);
				application->quit();
			}
		});
	}
	const int event_result = process_result == 0 ? application->exec() : 1;
	runtime.shutdown();
	application->processEvents();
	if(input_reader.joinable()) input_reader.join();
	return process_result != 0 ? process_result : event_result;
}


void PluginRuntime::setStatus(const QString& pluginId, const QString& version, const PluginRuntimeStatus& status)
{
	runtime_statuses.insert(instanceKey(pluginId, version), status);
	if(isolated_execution)
		logLifecycle(status.running ? QStringLiteral("status_running") :
			!status.error.isEmpty() ? QStringLiteral("status_failed") :
			status.enabled ? QStringLiteral("status_starting") : QStringLiteral("status_disabled"),
			pluginId, version, status.error.isEmpty() ? QStringLiteral("ok") : QStringLiteral("runtime_error"));
	if(state_changed_callback)
		state_changed_callback();
}
