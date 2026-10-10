/*=====================================================================
CodexAppServerClient.h
----------------------
Small JSON-RPC client for the locally installed Codex app-server.
=====================================================================*/
#pragma once


#include <QtCore/QObject>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonArray>
#include <QtCore/QStringList>
#include <QtCore/QProcess>
#include <QtCore/QString>


class CodexAppServerClient final : public QObject
{
	Q_OBJECT
public:
	explicit CodexAppServerClient(QObject* parent = nullptr);
	~CodexAppServerClient() override;

	bool start(const QString& endpoint, const QString& working_directory);
	void stop();
	bool isRunning() const;
	bool isReady() const { return thread_ready && metasiberia_mcp_ready && !turn_pending; }
	bool isTurnActive() const { return turn_pending; }
	void refreshStatus();
	QString codexPath() const { return codex_executable; }

	bool sendMessage(const QString& text, const QStringList& image_paths, const QString& model, const QString& effort);

signals:
	void modelsAvailable(const QJsonArray& models);
	void modelListFailed(const QString& message);
	void statusChanged(const QString& status);
	void assistantDelta(const QString& delta);
	void assistantMessageStarted();
	void assistantMessageFinished();
	void generatedImage(const QString& path);
	void ready();
	void turnActiveChanged(bool active);
	void activityChanged(const QString& phase);
	void error(const QString& message);

private slots:
	void onProcessStarted();
	void onReadyReadStandardOutput();
	void onReadyReadStandardError();
	void onProcessError(QProcess::ProcessError error);
	void onProcessFinished(int exit_code, QProcess::ExitStatus exit_status);

private:
	QString findCodexExecutable() const;
	bool ensureMCPRegistration(const QString& endpoint);
	void sendRequest(const QString& method, const QJsonObject& params, int id);
	void processLine(const QByteArray& line);

	QProcess process;
	QByteArray output_buffer;
	QString codex_executable;
	QString mcp_endpoint;
	QString working_directory;
	QString thread_id;
	int next_request_id;
	int model_request_id = -1;
	int turn_request_id = -1;
	QJsonArray available_models;
	bool initialized;
	bool thread_ready;
	bool turn_pending;
	QString metasiberia_mcp_status;
	bool metasiberia_mcp_ready;
};
