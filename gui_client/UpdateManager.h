#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QUrl>
#include <atomic>


class UpdateManager final : public QObject
{
	Q_OBJECT
public:
	struct InstallerAsset
	{
		QString name;
		QUrl url;
		qint64 size_B = -1;
	};

	struct UpdateInfo
	{
		QString tag;   // e.g. "v0.0.2"
		QString title; // e.g. "Metasiberia Beta v0.0.2"
		QString notes; // release body
		InstallerAsset installer;
		bool prerelease = false;
	};

	explicit UpdateManager(QObject* parent = nullptr);

	void checkForUpdatesAsync(bool force = false);

	bool checkInProgress() const { return in_progress; }
	bool hasCheckResult() const { return has_result; }
	bool updateAvailable() const { return update_available; }

	QString currentVersionString() const;
	const UpdateInfo& latest() const { return latest_info; }
	QString lastErrorString() const { return last_error; }

signals:
	void updateCheckFinished();
	void updateAvailabilityChanged(bool available);

private:
	struct ParsedVersion
	{
		int major = 0;
		int minor = 0;
		int patch = 0;
		bool valid = false;
	};

	static ParsedVersion parseVersionLike(const QString& s); // "v0.0.2" / "0.0.2" / "0.0.2-beta"
	static int compareVersions(const ParsedVersion& a, const ParsedVersion& b); // -1/0/1

	void startRequest();
	void runRequestInBackground(uint32_t request_id);

	bool in_progress = false;
	bool has_result = false;
	bool update_available = false;
	QString last_error;
	UpdateInfo latest_info;
	QString last_checked_tag;

	std::atomic<uint32_t> request_serial { 0 };
};
