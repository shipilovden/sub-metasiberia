#pragma once

#include "PluginPackage.h"
#include "PluginHostInterface.h"

#include <QtCore/QHash>
#include <QtCore/QJsonObject>
#include <QtCore/QList>
#include <QtCore/QMutex>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QtGlobal>
#include <cstddef>
#include <functional>
#include <memory>

struct lua_State;


struct PluginRuntimeStatus
{
	bool enabled = false;
	bool running = false;
	QString error;
};


// Production execution is supervised in a child process. Luau remains a
// capability-limited runtime, not an operating-system sandbox.
class PluginRuntime final
{
public:
	static constexpr size_t MaximumVmBytes = 16u * 1024u * 1024u;
	static constexpr qsizetype MaximumEntryBytes = 256 * 1024;
	static constexpr int MaximumSafepointsPerCall = 10000;
	static constexpr qint64 MaximumExecutionMilliseconds = 750;

	explicit PluginRuntime(PluginPackageManager& packageManager);
#if defined(METASIBERIA_PLUGIN_TESTING)
	PluginRuntime(PluginPackageManager& packageManager, bool isolatedExecution);
#endif
	~PluginRuntime();

	void setHost(PluginHostInterface* host);
	void setStateChangedCallback(std::function<void()> callback);
	bool hasHost() const { return host != nullptr; }

	bool requiresExplicitTrust(const QString& pluginId, const QString& version) const;
	bool enable(const QString& pluginId, const QString& version, bool explicitUserConsent, QString& error);
	bool disable(const QString& pluginId, const QString& version, QString& error);
	void restorePreviouslyEnabledLocalPlugins();
	void shutdown(); // Stops VMs and removes UI callbacks, preserving the user's enabled preference.

	PluginRuntimeStatus status(const QString& pluginId, const QString& version) const;
	bool hasActiveInstance(const QString& pluginId, const QString& version) const;
	static int runWorkerProcess(const QStringList& arguments);
#if defined(METASIBERIA_PLUGIN_TESTING)
	void terminateWorkerForTest(const QString& pluginId, const QString& version);
	int workerProcessStartedCountForTest() const { return worker_process_started_count; }
	int workerProcessFinishedCountForTest() const { return worker_process_finished_count; }
#endif

private:
#if !defined(METASIBERIA_PLUGIN_TESTING)
	// Only runWorkerProcess can construct a non-isolated runtime. Its private tag
	// keeps the Luau execution path unavailable to the production GUI host.
	struct WorkerProcessExecutionTag {};
	PluginRuntime(PluginPackageManager& packageManager, WorkerProcessExecutionTag);
#endif
	struct Instance;
	struct PersistentPreference;

	static void* limitedAllocator(void* userData, void* pointer, size_t oldSize, size_t newSize);
	static void instructionInterrupt(lua_State* state, int gc);
	static int registerCommandApi(lua_State* state);
	static int registerPanelApi(lua_State* state);
	static int closeViewApi(lua_State* state);
	static int notificationApi(lua_State* state);
	static int sceneSummaryApi(lua_State* state);

	bool startVerifiedPlugin(const PluginPackageInfo& info, const std::shared_ptr<Instance>& instance, QString& error);
	bool invokeCommand(const std::shared_ptr<Instance>& instance, QString& error);
	bool invokePanelAction(const std::shared_ptr<Instance>& instance, const QString& actionId, QString& error);
	bool readPreference(const QString& pluginId, PersistentPreference& preference, QString& error) const;
	bool writePreference(const QString& pluginId, const PersistentPreference& preference, QString& error) const;
	void setStatus(const QString& pluginId, const QString& version, const PluginRuntimeStatus& status);
	void enqueueCommand(const std::shared_ptr<Instance>& instance);
	void enqueuePanelAction(const std::shared_ptr<Instance>& instance, const QString& actionId);
	void runInstance(const std::shared_ptr<Instance>& instance, const PluginPackageInfo& info);
	bool runCommand(const std::shared_ptr<Instance>& instance, QString& error);
	bool runPanelAction(const std::shared_ptr<Instance>& instance, const QString& actionId, QString& error);
	void postWorkerCompletion(const std::shared_ptr<Instance>& instance, const PluginRuntimeStatus& status,
		bool updateStatus);
	void processWorkerOutput(const std::shared_ptr<Instance>& instance);
	void sendWorkerMessage(const std::shared_ptr<Instance>& instance, const QJsonObject& message);
	void handleWorkerFinished(const std::shared_ptr<Instance>& instance, int exitCode, int exitStatus);
	void logLifecycle(const QString& stage, const QString& pluginId, const QString& version,
		const QString& code = QString(), qint64 durationMs = -1);
	QString instanceKey(const QString& pluginId, const QString& version) const;

	PluginPackageManager& package_manager;
	PluginHostInterface* host = nullptr; // non-owning; unset before host teardown
	QHash<QString, std::shared_ptr<Instance>> active_instances;
	QHash<QString, PluginRuntimeStatus> runtime_statuses;
	std::function<void()> state_changed_callback;
	bool shutting_down = false;
	bool isolated_execution = true;
	bool worker_process_context = false;
#if defined(METASIBERIA_PLUGIN_TESTING)
	int worker_process_started_count = 0;
	int worker_process_finished_count = 0;
#endif
	mutable QMutex preference_mutex;
	std::shared_ptr<int> callback_lifetime = std::make_shared<int>(0);
};
