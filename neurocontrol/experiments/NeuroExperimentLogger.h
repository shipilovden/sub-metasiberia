#pragma once

#include <QtCore/QJsonObject>
#include <QtCore/QString>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

namespace neurocontrol {

// Asynchronous local-only experiment journal. appendEvent() only stamps and
// queues a small JSON object; all file I/O and summary generation happen on
// the worker thread.
class NeuroExperimentLogger final
{
public:
	NeuroExperimentLogger() = default;
	~NeuroExperimentLogger();
	NeuroExperimentLogger(const NeuroExperimentLogger&) = delete;
	NeuroExperimentLogger& operator=(const NeuroExperimentLogger&) = delete;

	bool startSession(const QJsonObject& metadata, QString* error = nullptr);
	void appendEvent(QJsonObject event);
	void finishSession(const QString& reason, double monotonic_seconds);
	bool isActive() const { return active_.load(); }
	QString sessionId() const { return session_id_; }
	QString sessionDirectory() const { return session_directory_; }

	static QString experimentsRootPath();
	static QJsonObject listSessions(int limit);
	static QJsonObject readSessionSummary(const QString& session_id);
	static QJsonObject readEventPage(const QString& session_id, qint64 cursor_bytes, int limit);

private:
	static constexpr qint64 kMaxSessionBytes = 64LL * 1024LL * 1024LL;
	static constexpr qint64 kMaxTotalBytes = 2LL * 1024LL * 1024LL * 1024LL;
	static constexpr qint64 kMaxEventsPerSession = 200000;
	static constexpr std::size_t kMaxQueuedEvents = 16384;

	void writerMain();
	void countWrittenEvent(const QJsonObject& event);
	void closeMovementEpisode(double end_time, const QString& reason);
	QJsonObject makeSummary() const;
	QString allocateSessionDirectory(QString* error);
	void joinPreviousWriter();

	std::atomic_bool active_{false};
	std::atomic_bool accepting_events_{false};
	std::atomic<quint64> next_event_id_{0};
	std::atomic<quint64> attempted_events_{0};
	std::atomic<quint64> attempted_predictions_{0};
	std::atomic<quint64> dropped_events_{0};
	std::atomic<quint64> dropped_predictions_{0};
	std::atomic<quint64> written_events_{0};
	std::atomic<quint64> written_predictions_{0};

	mutable std::mutex mutex_;
	std::condition_variable queue_ready_;
	std::condition_variable writer_ready_;
	std::deque<QJsonObject> queue_;
	std::thread writer_;
	bool stopping_ = false;
	bool writer_ready_flag_ = false;
	bool writer_opened_ = false;
	bool writer_finished_ = true;
	std::atomic_bool truncated_{false};
	std::atomic_bool storage_limit_reached_{false};
	std::atomic_bool queue_overflow_{false};
	QString writer_error_;
	QString completion_reason_;
	QString started_utc_;
	QString ended_utc_;
	double ended_monotonic_seconds_ = 0.0;
	QString session_id_;
	QString session_directory_;
	QJsonObject session_metadata_;

	qint64 event_file_bytes_ = 0;
	quint64 forward_candidate_count_ = 0;
	quint64 forward_decision_count_ = 0;
	quint64 stop_decision_count_ = 0;
	quint64 classification_switch_count_ = 0;
	quint64 movement_input_switch_count_ = 0;
	quint64 commands_submitted_count_ = 0;
	quint64 controller_trace_count_ = 0;
	quint64 avatar_telemetry_count_ = 0;
	quint64 motion_episode_count_ = 0;
	std::map<QString, quint64> stop_reason_counts_;
	std::vector<QJsonObject> motion_episodes_;
	QString last_classification_result_;
	bool have_controller_movement_state_ = false;
	bool last_controller_movement_state_ = false;
	bool movement_input_active_ = false;
	double movement_episode_start_ = 0.0;
	double movement_episode_max_speed_ = 0.0;
	quint64 movement_episode_command_id_ = 0;
};

} // namespace neurocontrol
