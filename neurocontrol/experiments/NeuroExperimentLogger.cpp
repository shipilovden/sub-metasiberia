#include "NeuroExperimentLogger.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QDirIterator>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QRegularExpression>
#include <QtCore/QSaveFile>
#include <QtCore/QStandardPaths>
#include <QtCore/QUuid>
#include <QtCore/QVariant>
#include <algorithm>
#include <chrono>

namespace neurocontrol {

namespace {

const qint64 kMaxMetadataBytes = 1024 * 1024;
const qint64 kMaxEventLineBytes = 64 * 1024;
const qint64 kMaxEventResponseBytes = 2 * 1024 * 1024;

bool writeJsonAtomically(const QString& path, const QJsonObject& object, QString* error)
{
	QSaveFile file(path);
	if(!file.open(QIODevice::WriteOnly))
	{
		if(error) *error = file.errorString();
		return false;
	}
	const QByteArray bytes = QJsonDocument(object).toJson(QJsonDocument::Indented);
	if(file.write(bytes) != bytes.size() || !file.commit())
	{
		if(error) *error = file.errorString();
		return false;
	}
	return true;
}

QJsonObject readJsonFile(const QString& path, QString* error = nullptr)
{
	QFile file(path);
	if(!file.open(QIODevice::ReadOnly))
	{
		if(error) *error = file.errorString();
		return {};
	}
	if(file.size() < 0 || file.size() > kMaxMetadataBytes)
	{
		if(error) *error = QStringLiteral("JSON file exceeds the size limit.");
		return {};
	}
	QJsonParseError parse_error;
	const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parse_error);
	if(parse_error.error != QJsonParseError::NoError || !document.isObject())
	{
		if(error) *error = parse_error.errorString();
		return {};
	}
	return document.object();
}

bool validSessionId(const QString& session_id)
{
	static const QRegularExpression pattern(QStringLiteral("^[0-9]{8}T[0-9]{9}Z_[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$"));
	return pattern.match(session_id).hasMatch();
}

bool isWithin(const QString& root, const QString& path)
{
	const QString canonical_root = QFileInfo(root).canonicalFilePath();
	const QString canonical_path = QFileInfo(path).canonicalFilePath();
	if(canonical_root.isEmpty() || canonical_path.isEmpty()) return false;
	const QString relative = QDir(canonical_root).relativeFilePath(canonical_path);
	return relative != QStringLiteral("..") && !relative.startsWith(QStringLiteral("../")) && !QDir::isAbsolutePath(relative);
}

QString sessionDirectoryFor(const QString& session_id)
{
	if(!validSessionId(session_id)) return QString();
	const QString root = NeuroExperimentLogger::experimentsRootPath();
	const QString path = QDir(root).filePath(session_id);
	const QFileInfo info(path);
	if(!info.exists() || !info.isDir() || info.isSymLink() || !isWithin(root, path)) return QString();
	return info.canonicalFilePath();
}

QString safeSessionFilePath(const QString& directory, const QString& file_name)
{
	const QString path = QDir(directory).filePath(file_name);
	const QFileInfo info(path);
	if(!info.exists() || !info.isFile() || info.isSymLink() || !isWithin(directory, path)) return QString();
	return info.canonicalFilePath();
}

QJsonObject errorObject(const QString& code, const QString& message)
{
	return QJsonObject{{"ok", false}, {"error_code", code}, {"error", message}};
}

qint64 directorySize(const QString& path)
{
	qint64 size = 0;
	QDirIterator it(path, QDir::Files | QDir::Hidden | QDir::System | QDir::NoSymLinks, QDirIterator::Subdirectories);
	while(it.hasNext())
	{
		it.next();
		size += std::max<qint64>(0, it.fileInfo().size());
	}
	return size;
}

} // namespace


NeuroExperimentLogger::~NeuroExperimentLogger()
{
	finishSession(QStringLiteral("client_shutdown"), 0.0);
	joinPreviousWriter();
}


QString NeuroExperimentLogger::experimentsRootPath()
{
	QString app_data = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
	if(app_data.isEmpty()) app_data = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
	if(app_data.isEmpty()) return QString();
	return QDir(app_data).filePath(QStringLiteral("NeuroControl/experiments"));
}


QString NeuroExperimentLogger::allocateSessionDirectory(QString* error)
{
	const QString root_path = experimentsRootPath();
	if(root_path.isEmpty())
	{
		if(error) *error = QStringLiteral("Qt не предоставил локальный каталог данных приложения.");
		return QString();
	}
	QDir root(root_path);
	if(!root.exists() && !QDir().mkpath(root_path))
	{
		if(error) *error = QStringLiteral("Не удалось создать локальный каталог экспериментов.");
		return QString();
	}
	qint64 total_bytes = 0;
	const QRegularExpression session_pattern(QStringLiteral("^[0-9]{8}T[0-9]{9}Z_[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$"));
	for(const QString& name : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks, QDir::Name))
	{
		const QString path = root.filePath(name);
		if(session_pattern.match(name).hasMatch() && isWithin(root_path, path)) total_bytes += directorySize(path);
	}
	if(total_bytes + kMaxSessionBytes > kMaxTotalBytes)
	{
		if(error) *error = QStringLiteral("Достигнут локальный лимит хранения экспериментов; прежние журналы не удалялись.");
		return QString();
	}
	for(int attempt = 0; attempt < 4; ++attempt)
	{
		const QString stamp = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd'T'hhmmsszzz'Z'"));
		const QString id = stamp + QLatin1Char('_') + QUuid::createUuid().toString(QUuid::WithoutBraces).toLower();
		const QString path = root.filePath(id);
		if(root.mkdir(id)) return path;
	}
	if(error) *error = QStringLiteral("Не удалось создать уникальный каталог сеанса.");
	return QString();
}


bool NeuroExperimentLogger::startSession(const QJsonObject& metadata, QString* error)
{
	if(active_.load()) finishSession(QStringLiteral("replaced_by_new_session"), 0.0);
	joinPreviousWriter();
	QString allocation_error;
	const QString directory = allocateSessionDirectory(&allocation_error);
	if(directory.isEmpty())
	{
		if(error) *error = allocation_error;
		return false;
	}
	session_directory_ = directory;
	session_id_ = QFileInfo(directory).fileName();
	started_utc_ = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
	ended_utc_.clear();
	ended_monotonic_seconds_ = 0.0;
	completion_reason_.clear();
	writer_error_.clear();
	truncated_.store(false);
	storage_limit_reached_.store(false);
	queue_overflow_.store(false);
	queue_.clear();
	stopping_ = false;
	writer_ready_flag_ = false;
	writer_opened_ = false;
	writer_finished_ = false;
	event_file_bytes_ = 0;
	forward_candidate_count_ = 0;
	forward_decision_count_ = 0;
	stop_decision_count_ = 0;
	classification_switch_count_ = 0;
	movement_input_switch_count_ = 0;
	commands_submitted_count_ = 0;
	controller_trace_count_ = 0;
	avatar_telemetry_count_ = 0;
	motion_episode_count_ = 0;
	stop_reason_counts_.clear();
	motion_episodes_.clear();
	last_classification_result_.clear();
	have_controller_movement_state_ = false;
	last_controller_movement_state_ = false;
	movement_input_active_ = false;
	movement_episode_start_ = 0.0;
	movement_episode_max_speed_ = 0.0;
	movement_episode_command_id_ = 0;
	next_event_id_.store(0);
	attempted_events_.store(0);
	attempted_predictions_.store(0);
	dropped_events_.store(0);
	dropped_predictions_.store(0);
	written_events_.store(0);
	written_predictions_.store(0);

	session_metadata_ = metadata;
	session_metadata_.insert(QStringLiteral("schema_version"), 1);
	session_metadata_.insert(QStringLiteral("session_id"), session_id_);
	session_metadata_.insert(QStringLiteral("started_utc"), started_utc_);
	session_metadata_.insert(QStringLiteral("logger_format"), QStringLiteral("metasiberia-neurocontrol-experiment-v1"));
	session_metadata_.insert(QStringLiteral("storage_policy"), QJsonObject{
		{QStringLiteral("maximum_session_bytes"), kMaxSessionBytes},
		{QStringLiteral("maximum_total_bytes"), kMaxTotalBytes},
		{QStringLiteral("maximum_events"), static_cast<qint64>(kMaxEventsPerSession)},
		{QStringLiteral("queue_capacity_events"), static_cast<qint64>(kMaxQueuedEvents)},
		{QStringLiteral("raw_eeg_copied"), false}});
	QString metadata_error;
	if(!writeJsonAtomically(QDir(directory).filePath(QStringLiteral("session.json")), session_metadata_, &metadata_error))
	{
		if(error) *error = QStringLiteral("Не удалось сохранить параметры сеанса: %1").arg(metadata_error);
		return false;
	}

	active_.store(true);
	accepting_events_.store(true);
	writer_ = std::thread(&NeuroExperimentLogger::writerMain, this);
	std::unique_lock<std::mutex> lock(mutex_);
	const bool ready = writer_ready_.wait_for(lock, std::chrono::seconds(3), [this] { return writer_ready_flag_; });
	const bool opened = ready && writer_opened_;
	const QString open_error = writer_error_;
	lock.unlock();
	if(!opened)
	{
		active_.store(false);
		accepting_events_.store(false);
		if(error) *error = ready ? QStringLiteral("Не удалось открыть поток событий: %1").arg(open_error) : QStringLiteral("Истекло ожидание запуска фоновой записи журнала.");
		joinPreviousWriter();
		return false;
	}
	return true;
}


void NeuroExperimentLogger::appendEvent(QJsonObject event)
{
	if(!active_.load()) return;
	const QString type = event.value(QStringLiteral("event_type")).toString();
	attempted_events_.fetch_add(1);
	if(type == QStringLiteral("prediction")) attempted_predictions_.fetch_add(1);
	event.insert(QStringLiteral("session_id"), session_id_);
	event.insert(QStringLiteral("journal_event_id"), static_cast<qint64>(next_event_id_.fetch_add(1) + 1));
	if(!event.contains(QStringLiteral("timestamp_utc")))
		event.insert(QStringLiteral("timestamp_utc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if(!accepting_events_.load() || queue_.size() >= kMaxQueuedEvents)
		{
			dropped_events_.fetch_add(1);
			if(type == QStringLiteral("prediction")) dropped_predictions_.fetch_add(1);
			if(queue_.size() >= kMaxQueuedEvents)
			{
				truncated_.store(true);
				queue_overflow_.store(true);
				accepting_events_.store(false);
			}
			return;
		}
		queue_.push_back(std::move(event));
	}
	queue_ready_.notify_one();
}


void NeuroExperimentLogger::finishSession(const QString& reason, double monotonic_seconds)
{
	if(!active_.load()) return;
	QJsonObject end_event{{QStringLiteral("event_type"), QStringLiteral("session_end")},
		{QStringLiteral("reason"), reason}, {QStringLiteral("monotonic_seconds"), monotonic_seconds}};
	appendEvent(end_event);
	{
		std::lock_guard<std::mutex> lock(mutex_);
		completion_reason_ = reason;
		ended_utc_ = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
		ended_monotonic_seconds_ = monotonic_seconds;
		accepting_events_.store(false);
		stopping_ = true;
	}
	active_.store(false);
	queue_ready_.notify_one();
}


void NeuroExperimentLogger::joinPreviousWriter()
{
	if(writer_.joinable()) writer_.join();
}


void NeuroExperimentLogger::writerMain()
{
	QFile event_file(QDir(session_directory_).filePath(QStringLiteral("events.jsonl")));
	const bool opened = event_file.open(QIODevice::WriteOnly | QIODevice::Truncate);
	{
		std::lock_guard<std::mutex> lock(mutex_);
		writer_ready_flag_ = true;
		writer_opened_ = opened;
		if(!opened) writer_error_ = event_file.errorString();
		writer_finished_ = !opened;
	}
	writer_ready_.notify_all();
	if(!opened)
	{
		accepting_events_.store(false);
		active_.store(false);
		QString ignored;
		writeJsonAtomically(QDir(session_directory_).filePath(QStringLiteral("summary.json")), makeSummary(), &ignored);
		return;
	}

	while(true)
	{
		QJsonObject event;
		{
			std::unique_lock<std::mutex> lock(mutex_);
			queue_ready_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
			if(queue_.empty() && stopping_) break;
			event = std::move(queue_.front());
			queue_.pop_front();
		}
		if(writer_error_.isEmpty())
		{
			const QByteArray line = QJsonDocument(event).toJson(QJsonDocument::Compact) + '\n';
			if(written_events_.load() >= static_cast<quint64>(kMaxEventsPerSession) ||
				event_file_bytes_ + line.size() > kMaxSessionBytes)
			{
				truncated_.store(true);
				storage_limit_reached_.store(true);
				accepting_events_.store(false);
				dropped_events_.fetch_add(1);
				if(event.value(QStringLiteral("event_type")).toString() == QStringLiteral("prediction")) dropped_predictions_.fetch_add(1);
				continue;
			}
			if(event_file.write(line) != line.size() || !event_file.flush())
			{
				writer_error_ = event_file.errorString().isEmpty() ? QStringLiteral("Не удалось записать строку журнала.") : event_file.errorString();
				truncated_.store(true);
				accepting_events_.store(false);
				dropped_events_.fetch_add(1);
				if(event.value(QStringLiteral("event_type")).toString() == QStringLiteral("prediction")) dropped_predictions_.fetch_add(1);
				continue;
			}
			event_file_bytes_ += line.size();
			written_events_.fetch_add(1);
			countWrittenEvent(event);
		}
		else
		{
			dropped_events_.fetch_add(1);
			if(event.value(QStringLiteral("event_type")).toString() == QStringLiteral("prediction")) dropped_predictions_.fetch_add(1);
		}
	}
	event_file.flush();
	event_file.close();
	if(movement_input_active_)
		closeMovementEpisode(ended_monotonic_seconds_, QStringLiteral("session_finished"));
	QString summary_error;
	if(!writeJsonAtomically(QDir(session_directory_).filePath(QStringLiteral("summary.json")), makeSummary(), &summary_error))
	{
		std::lock_guard<std::mutex> lock(mutex_);
		writer_error_ = QStringLiteral("Не удалось сохранить итоговую сводку: %1").arg(summary_error);
	}
	{
		std::lock_guard<std::mutex> lock(mutex_);
		writer_finished_ = true;
	}
}


void NeuroExperimentLogger::countWrittenEvent(const QJsonObject& event)
{
	const QString type = event.value(QStringLiteral("event_type")).toString();
	if(type == QStringLiteral("prediction"))
	{
		written_predictions_.fetch_add(1);
		if(event.value(QStringLiteral("candidate_command")).toString() == QStringLiteral("FORWARD")) ++forward_candidate_count_;
		const QString decision = event.value(QStringLiteral("classification_result")).toString();
		if(!decision.isEmpty())
		{
			if(!last_classification_result_.isEmpty() && last_classification_result_ != decision) ++classification_switch_count_;
			last_classification_result_ = decision;
		}
		if(decision == QStringLiteral("FORWARD")) ++forward_decision_count_;
		else if(decision == QStringLiteral("STOP")) ++stop_decision_count_;
	}
	else if(type == QStringLiteral("command_submitted")) ++commands_submitted_count_;
	else if(type == QStringLiteral("controller_trace"))
	{
		++controller_trace_count_;
		const QString reason = event.value(QStringLiteral("safety_reason")).toString();
		if(!reason.isEmpty() && reason != QStringLiteral("none")) ++stop_reason_counts_[reason];
		const bool movement = event.value(QStringLiteral("movement_input_applied")).toBool();
		if(have_controller_movement_state_ && last_controller_movement_state_ != movement) ++movement_input_switch_count_;
		have_controller_movement_state_ = true;
		last_controller_movement_state_ = movement;
		const double now = event.value(QStringLiteral("evaluated_at_monotonic_seconds")).toDouble();
		const double speed = event.value(QStringLiteral("observed_ground_speed_mps")).toDouble();
		if(movement)
		{
			if(!movement_input_active_)
			{
				movement_input_active_ = true;
				movement_episode_start_ = now;
				movement_episode_max_speed_ = speed;
				movement_episode_command_id_ = static_cast<quint64>(event.value(QStringLiteral("command_event_id")).toVariant().toULongLong());
			}
			movement_episode_max_speed_ = std::max(movement_episode_max_speed_, speed);
		}
		else if(movement_input_active_)
			closeMovementEpisode(now, reason.isEmpty() ? QStringLiteral("input_not_applied") : reason);
	}
	else if(type == QStringLiteral("avatar_telemetry")) ++avatar_telemetry_count_;
}


void NeuroExperimentLogger::closeMovementEpisode(double end_time, const QString& reason)
{
	if(!movement_input_active_) return;
	const double duration = std::max(0.0, end_time - movement_episode_start_);
	motion_episodes_.push_back(QJsonObject{
		{QStringLiteral("episode_index"), static_cast<qint64>(++motion_episode_count_)},
		{QStringLiteral("command_event_id"), static_cast<qint64>(movement_episode_command_id_)},
		{QStringLiteral("started_at_monotonic_seconds"), movement_episode_start_},
		{QStringLiteral("ended_at_monotonic_seconds"), end_time},
		{QStringLiteral("duration_seconds"), duration},
		{QStringLiteral("maximum_observed_ground_speed_mps"), movement_episode_max_speed_},
		{QStringLiteral("end_reason"), reason},
		{QStringLiteral("position_start"), QJsonValue::Null},
		{QStringLiteral("position_end"), QJsonValue::Null},
		{QStringLiteral("positions_available"), false}});
	movement_input_active_ = false;
	movement_episode_start_ = 0.0;
	movement_episode_max_speed_ = 0.0;
	movement_episode_command_id_ = 0;
}


QJsonObject NeuroExperimentLogger::makeSummary() const
{
	QJsonObject stop_reasons;
	for(const auto& pair : stop_reason_counts_) stop_reasons.insert(pair.first, static_cast<qint64>(pair.second));
	QJsonArray episodes;
	for(const QJsonObject& episode : motion_episodes_) episodes.append(episode);
	const quint64 attempted = attempted_events_.load();
	const quint64 written = written_events_.load();
	const quint64 predictions_attempted = attempted_predictions_.load();
	const quint64 predictions_written = written_predictions_.load();
	const quint64 dropped = dropped_events_.load();
	const quint64 dropped_predictions = dropped_predictions_.load();
	const double duration = std::max(0.0, ended_monotonic_seconds_ - session_metadata_.value(QStringLiteral("start_monotonic_seconds")).toDouble());
	return QJsonObject{
		{QStringLiteral("format"), QStringLiteral("metasiberia-neurocontrol-experiment-summary-v1")},
		{QStringLiteral("session_id"), session_id_},
		{QStringLiteral("started_utc"), started_utc_},
		{QStringLiteral("ended_utc"), ended_utc_},
		{QStringLiteral("completion_reason"), completion_reason_},
		{QStringLiteral("duration_seconds"), duration},
		{QStringLiteral("complete"), dropped == 0 && writer_error_.isEmpty() && !truncated_.load()},
		{QStringLiteral("truncated_or_incomplete"), dropped != 0 || !writer_error_.isEmpty() || truncated_.load()},
		{QStringLiteral("writer_error"), writer_error_},
		{QStringLiteral("storage_limit_reached"), storage_limit_reached_.load()},
		{QStringLiteral("queue_overflow"), queue_overflow_.load()},
		{QStringLiteral("event_counts"), QJsonObject{
			{QStringLiteral("attempted"), static_cast<qint64>(attempted)},
			{QStringLiteral("written"), static_cast<qint64>(written)},
			{QStringLiteral("dropped_or_unwritten"), static_cast<qint64>(dropped)}}},
		{QStringLiteral("prediction_counts"), QJsonObject{
			{QStringLiteral("generated"), static_cast<qint64>(predictions_attempted)},
			{QStringLiteral("recorded"), static_cast<qint64>(predictions_written)},
			{QStringLiteral("dropped_or_unwritten"), static_cast<qint64>(dropped_predictions)},
			{QStringLiteral("gap_free"), predictions_attempted == predictions_written && dropped == 0 && writer_error_.isEmpty() && !truncated_.load()},
			{QStringLiteral("candidate_forward"), static_cast<qint64>(forward_candidate_count_)},
			{QStringLiteral("confirmed_forward_decisions"), static_cast<qint64>(forward_decision_count_)},
			{QStringLiteral("stop_decisions"), static_cast<qint64>(stop_decision_count_)},
			{QStringLiteral("classification_switches"), static_cast<qint64>(classification_switch_count_)}}},
		{QStringLiteral("command_submissions"), static_cast<qint64>(commands_submitted_count_)},
		{QStringLiteral("movement_input_switches"), static_cast<qint64>(movement_input_switch_count_)},
		{QStringLiteral("controller_trace_events"), static_cast<qint64>(controller_trace_count_)},
		{QStringLiteral("avatar_telemetry_samples"), static_cast<qint64>(avatar_telemetry_count_)},
		{QStringLiteral("safety_reason_event_counts"), stop_reasons},
		{QStringLiteral("movement_input_episode_count"), static_cast<qint64>(motion_episode_count_)},
		{QStringLiteral("movement_input_episodes"), episodes},
		{QStringLiteral("raw_eeg_copied"), false},
		{QStringLiteral("event_log_bytes"), event_file_bytes_}};
}


QJsonObject NeuroExperimentLogger::listSessions(int limit)
{
	if(limit < 1 || limit > 100) return errorObject(QStringLiteral("INVALID_LIMIT"), QStringLiteral("limit должен быть от 1 до 100."));
	const QString root_path = experimentsRootPath();
	if(root_path.isEmpty()) return errorObject(QStringLiteral("STORAGE_UNAVAILABLE"), QStringLiteral("Локальный каталог данных приложения недоступен."));
	if(!QDir(root_path).exists()) return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("experiments"), QJsonArray{}}, {QStringLiteral("count"), 0}};
	const QDir root(root_path);
	QStringList names = root.entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks, QDir::Name | QDir::Reversed);
	QJsonArray result;
	for(const QString& name : names)
	{
		if(result.size() >= limit) break;
		if(!validSessionId(name)) continue;
		const QString directory = sessionDirectoryFor(name);
		if(directory.isEmpty()) continue;
		const QString metadata_path = safeSessionFilePath(directory, QStringLiteral("session.json"));
		if(metadata_path.isEmpty()) continue;
		const QJsonObject metadata = readJsonFile(metadata_path);
		if(metadata.isEmpty()) continue;
		const QString summary_path = safeSessionFilePath(directory, QStringLiteral("summary.json"));
		const QJsonObject summary = summary_path.isEmpty() ? QJsonObject{} : readJsonFile(summary_path);
		const bool has_summary = !summary.isEmpty();
		result.append(QJsonObject{
			{QStringLiteral("session_id"), name},
			{QStringLiteral("started_utc"), metadata.value(QStringLiteral("started_utc"))},
			{QStringLiteral("source_id"), metadata.value(QStringLiteral("source_id"))},
			{QStringLiteral("recording_name"), metadata.value(QStringLiteral("recording_name"))},
			{QStringLiteral("model_name"), metadata.value(QStringLiteral("model_name"))},
			{QStringLiteral("status"), has_summary ? (summary.value(QStringLiteral("complete")).toBool() ? QStringLiteral("complete") : QStringLiteral("incomplete")) : QStringLiteral("running_or_interrupted")},
			{QStringLiteral("prediction_counts"), summary.value(QStringLiteral("prediction_counts"))}});
	}
	return QJsonObject{{QStringLiteral("ok"), true},
		{QStringLiteral("experiments"), result}, {QStringLiteral("count"), result.size()}, {QStringLiteral("raw_eeg_included"), false}};
}


QJsonObject NeuroExperimentLogger::readSessionSummary(const QString& session_id)
{
	const QString directory = sessionDirectoryFor(session_id);
	if(directory.isEmpty()) return errorObject(QStringLiteral("EXPERIMENT_NOT_FOUND"), QStringLiteral("Сеанс не найден в разрешённом каталоге журналов."));
	QString error;
	const QString metadata_path = safeSessionFilePath(directory, QStringLiteral("session.json"));
	if(metadata_path.isEmpty()) return errorObject(QStringLiteral("EXPERIMENT_METADATA_INVALID"), QStringLiteral("Файл метаданных отсутствует или не находится внутри каталога сеанса."));
	const QJsonObject metadata = readJsonFile(metadata_path, &error);
	if(metadata.isEmpty()) return errorObject(QStringLiteral("EXPERIMENT_METADATA_INVALID"), QStringLiteral("Не удалось прочитать метаданные сеанса: %1").arg(error));
	const QString summary_path = safeSessionFilePath(directory, QStringLiteral("summary.json"));
	const QJsonObject summary = summary_path.isEmpty() ? QJsonObject{} : readJsonFile(summary_path);
	return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("session_id"), session_id},
		{QStringLiteral("status"), summary.isEmpty() ? QStringLiteral("running_or_interrupted") : summary.value(QStringLiteral("complete")).toBool() ? QStringLiteral("complete") : QStringLiteral("incomplete")},
		{QStringLiteral("metadata"), metadata}, {QStringLiteral("summary_available"), !summary.isEmpty()},
		{QStringLiteral("summary"), summary}};
}


QJsonObject NeuroExperimentLogger::readEventPage(const QString& session_id, qint64 cursor_bytes, int limit)
{
	if(cursor_bytes < 0 || cursor_bytes > kMaxSessionBytes) return errorObject(QStringLiteral("INVALID_CURSOR"), QStringLiteral("cursor_bytes вне допустимого диапазона."));
	if(limit < 1 || limit > 200) return errorObject(QStringLiteral("INVALID_LIMIT"), QStringLiteral("limit должен быть от 1 до 200."));
	const QString directory = sessionDirectoryFor(session_id);
	if(directory.isEmpty()) return errorObject(QStringLiteral("EXPERIMENT_NOT_FOUND"), QStringLiteral("Сеанс не найден в разрешённом каталоге журналов."));
	const QString path = QDir(directory).filePath(QStringLiteral("events.jsonl"));
	const QFileInfo info(path);
	if(!info.exists() || info.isSymLink() || !isWithin(directory, path)) return errorObject(QStringLiteral("EVENT_LOG_NOT_FOUND"), QStringLiteral("Файл событий сеанса отсутствует."));
	if(info.size() > kMaxSessionBytes) return errorObject(QStringLiteral("EVENT_LOG_TOO_LARGE"), QStringLiteral("Размер файла событий превышает защитный предел."));
	QFile file(path);
	if(!file.open(QIODevice::ReadOnly)) return errorObject(QStringLiteral("EVENT_LOG_UNAVAILABLE"), QStringLiteral("Не удалось открыть файл событий для чтения."));
	const qint64 file_size = file.size();
	if(cursor_bytes > file_size) return errorObject(QStringLiteral("INVALID_CURSOR"), QStringLiteral("cursor_bytes выходит за текущий размер журнала."));
	if(cursor_bytes > 0)
	{
		if(!file.seek(cursor_bytes - 1) || file.read(1) != QByteArray("\n"))
			return errorObject(QStringLiteral("INVALID_CURSOR"), QStringLiteral("Курсор должен указывать на границу строки из предыдущего ответа."));
	}
	if(!file.seek(cursor_bytes)) return errorObject(QStringLiteral("INVALID_CURSOR"), QStringLiteral("Не удалось перейти к курсору."));
	QJsonArray events;
	qint64 response_bytes = 0;
	qint64 next_cursor = cursor_bytes;
	while(events.size() < limit && !file.atEnd())
	{
		const qint64 line_start = file.pos();
		const QByteArray line = file.readLine(kMaxEventLineBytes + 1);
		if(line.isEmpty()) break;
		if(line.size() > kMaxEventLineBytes) return errorObject(QStringLiteral("EVENT_LINE_TOO_LARGE"), QStringLiteral("Строка событий превышает защитный предел."));
		if(!line.endsWith('\n'))
		{
			next_cursor = line_start; // Do not expose a partially written final line.
			break;
		}
		QJsonParseError parse_error;
		const QJsonDocument document = QJsonDocument::fromJson(line.trimmed(), &parse_error);
		if(parse_error.error != QJsonParseError::NoError || !document.isObject())
			return errorObject(QStringLiteral("EVENT_LOG_CORRUPT"), QStringLiteral("Не удалось разобрать полную строку журнала."));
		response_bytes += line.size();
		if(response_bytes > kMaxEventResponseBytes) return errorObject(QStringLiteral("EVENT_PAGE_TOO_LARGE"), QStringLiteral("Страница журнала превышает защитный предел."));
		events.append(document.object());
		next_cursor = file.pos();
	}
	const bool more = next_cursor < file_size;
	return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("session_id"), session_id},
		{QStringLiteral("events"), events}, {QStringLiteral("count"), events.size()},
		{QStringLiteral("next_cursor_bytes"), next_cursor}, {QStringLiteral("more"), more},
		{QStringLiteral("raw_eeg_included"), false}};
}

} // namespace neurocontrol
