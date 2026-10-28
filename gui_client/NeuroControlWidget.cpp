#include "NeuroControlWidget.h"

#include "GUIClient.h"
#include "../neurocontrol/avatar/NeuroCommand.h"
#include "../neurocontrol/acquisition/BrainflowSyntheticSource.h"
#include "../neurocontrol/acquisition/LslEegSource.h"
#include "../neurocontrol/acquisition/LslRecordingFile.h"
#include "../neurocontrol/experiments/NeuroExperimentLogger.h"
#include "../neurocontrol/processing/NeuroClassifier.h"
#include "../neurocontrol/processing/LslShadowPipeline.h"
#include "../neurocontrol/processing/LslAuditReportWriter.h"
#include "../shared/Version.h"
#include <utils/Clock.h>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QCoreApplication>
#include <QtCore/QCryptographicHash>
#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QDirIterator>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QMap>
#include <QtCore/QLocale>
#include <QtCore/QProcess>
#include <QtCore/QSaveFile>
#include <QtCore/QSettings>
#include <QtCore/QSignalBlocker>
#include <QtCore/QStorageInfo>
#include <QtCore/QStandardPaths>
#include <QtCore/QUuid>
#include <QtCore/QRegularExpression>
#include <QtCore/QTimer>
#include <QtCore/QThread>
#include <QtGui/QPainter>
#include <QtGui/QPainterPath>
#include <QtGui/QTextCursor>
#include <QtGui/QTextDocument>
#include <QtGui/QTextBlock>
#include <QtGui/QTextFragment>
#include <QtGui/QMouseEvent>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDoubleSpinBox>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QListWidgetItem>
#include <QtWidgets/QProgressBar>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QScrollArea>
#include <QtWidgets/QSlider>
#include <QtWidgets/QTabWidget>
#include <QtWidgets/QTextEdit>
#include <QtWidgets/QTextBrowser>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QApplication>
#include <QtGui/QClipboard>
#include <QtWidgets/QGroupBox>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <limits>
#include <cmath>
#include <cstring>
#include <deque>
#include <mutex>
#include <numeric>
#include <set>
#include <stdexcept>
#include <thread>
#include <tuple>

class NeuroTextBrowser final : public QTextBrowser
{
public:
	explicit NeuroTextBrowser(QWidget* parent) : QTextBrowser(parent)
	{
		QPalette colours = palette();
		colours.setColor(QPalette::Link, QColor("#76C7FF"));
		colours.setColor(QPalette::LinkVisited, QColor("#76C7FF"));
		setPalette(colours);
		document()->setDefaultStyleSheet(QStringLiteral("a { color: #76C7FF; text-decoration: underline; }"));
		setMouseTracking(true);
		viewport()->setMouseTracking(true);
	}
protected:
	void mouseMoveEvent(QMouseEvent* event) override
	{
		const QString anchor = anchorAt(event->pos());
		if(anchor != hovered_anchor_) { hovered_anchor_ = anchor; recolorLinks(); }
		QTextBrowser::mouseMoveEvent(event);
	}
	void leaveEvent(QEvent* event) override
	{
		if(!hovered_anchor_.isEmpty()) { hovered_anchor_.clear(); recolorLinks(); }
		QTextBrowser::leaveEvent(event);
	}
private:
	void recolorLinks()
	{
		std::vector<std::tuple<int, int, bool>> links;
		for(QTextBlock block = document()->begin(); block.isValid(); block = block.next())
			for(auto item = block.begin(); !item.atEnd(); ++item)
			{
			const QTextFragment fragment = item.fragment();
				if(!fragment.isValid() || !fragment.charFormat().isAnchor()) continue;
				links.emplace_back(fragment.position(), fragment.length(),
					!hovered_anchor_.isEmpty() && fragment.charFormat().anchorHref() == hovered_anchor_);
			}
		for(const auto& link : links)
		{
			QTextCursor cursor(document());
			cursor.setPosition(std::get<0>(link));
			cursor.setPosition(std::get<0>(link) + std::get<1>(link), QTextCursor::KeepAnchor);
			QTextCharFormat colour;
			colour.setForeground(QColor(std::get<2>(link) ? "#A7DEFF" : "#76C7FF"));
			cursor.mergeCharFormat(colour);
		}
	}
	QString hovered_anchor_;
};

class WaveformView final : public QWidget
{
public:
	explicit WaveformView(QWidget* parent, bool compact = false) : QWidget(parent), compact_(compact)
	{
		QFont plot_font(QStringLiteral("Segoe UI"));
		plot_font.setPointSize(9);
		setFont(plot_font);
		setMinimumHeight(compact_ ? 95 : 135);
	}
	void setChannels(const std::vector<std::string>& names, int sample_rate)
	{
		channel_names_ = names;
		traces_.assign(names.size(), {});
		sample_rate_ = std::max(1, sample_rate);
		times_.clear();
		visible_.assign(names.size(), true);
		updateMinimumHeight();
		update();
	}
	void setChannelTypes(const std::vector<std::string>& types) { channel_types_ = types; update(); }
	void setVisible(const std::vector<bool>& visible) { visible_ = visible; updateMinimumHeight(); update(); }
	void setWindowSeconds(int seconds) { window_seconds_ = std::clamp(seconds, 2, 30); update(); }
	void setVerticalScale(double microvolts) { vertical_scale_ = std::max(1.0, microvolts); update(); }
	void setAutoScale(bool value) { auto_scale_ = value; update(); }
	void setPaused(bool value) { paused_ = value; }
	void setEvents(const std::vector<std::pair<int, std::string>>& events) { events_ = events; update(); }
	void clear()
	{
		for(auto& trace : traces_) trace.clear();
		times_.clear();
		update();
	}
	void appendFrame(const double* values, int count, double timestamp)
	{
		if(paused_ || !values || count != static_cast<int>(traces_.size())) return;
		for(int channel = 0; channel < count; ++channel)
			traces_[static_cast<std::size_t>(channel)].push_back(std::isfinite(values[channel]) ? values[channel] : 0.0);
		times_.push_back(timestamp);
		const std::size_t capacity = static_cast<std::size_t>(sample_rate_) * 30;
		if(times_.size() > capacity)
		{
			for(auto& trace : traces_) if(!trace.empty()) trace.pop_front();
			times_.pop_front();
		}
	}
	void refresh() { update(); }
	std::vector<double> channelTail(int channel, int requested_samples) const
	{
		if(channel < 0 || channel >= static_cast<int>(traces_.size())) return {};
		const auto& trace = traces_[static_cast<std::size_t>(channel)];
		const std::size_t start = trace.size() > static_cast<std::size_t>(requested_samples) ? trace.size() - static_cast<std::size_t>(requested_samples) : 0;
		return std::vector<double>(trace.begin() + static_cast<std::ptrdiff_t>(start), trace.end());
	}
private:
	void updateMinimumHeight()
	{
		if(compact_) return;
		const int visible_count = static_cast<int>(std::count(visible_.begin(), visible_.end(), true));
		setMinimumHeight(std::max(135, visible_count * std::max(54, fontMetrics().height() * 2 + 12) + 34));
	}
protected:
	void paintEvent(QPaintEvent*) override
	{
		QPainter painter(this);
		painter.fillRect(rect(), QColor("#181a20"));
		const int left = compact_ ? 116 : 160, right = 10, top = 8, bottom = 30;
		const QRect area(left, top, std::max(1, width() - left - right), std::max(1, height() - top - bottom));
		std::vector<int> shown;
		for(int i = 0; i < static_cast<int>(channel_names_.size()); ++i)
			if(i < static_cast<int>(visible_.size()) && visible_[static_cast<std::size_t>(i)]) shown.push_back(i);
		if(shown.empty())
		{
			painter.setPen(QColor("#a1a1aa"));
			painter.drawText(area, Qt::AlignCenter, tr("Выберите хотя бы один канал ЭЭГ"));
			return;
		}
		const int row_height = std::max(1, area.height() / static_cast<int>(shown.size()));
		const int text_height = painter.fontMetrics().height();
		const double newest = times_.empty() ? 0.0 : times_.back();
		const double oldest = newest - window_seconds_;
		for(int row = 0; row < static_cast<int>(shown.size()); ++row)
		{
			const int channel = shown[static_cast<std::size_t>(row)];
			const QRect lane(area.left(), area.top() + row * row_height, area.width(), row_height);
			painter.setPen(QColor("#30343d"));
			for(int tick = 0; tick <= 4; ++tick)
			{
				const int y = lane.top() + tick * lane.height() / 4;
				painter.drawLine(lane.left(), y, lane.right(), y);
			}
			for(int tick = 0; tick <= 5; ++tick)
			{
				const int x = lane.left() + tick * lane.width() / 5;
				painter.drawLine(x, lane.top(), x, lane.bottom());
			}
			double range = vertical_scale_;
			const auto& trace = traces_[static_cast<std::size_t>(channel)];
			if(auto_scale_ && !trace.empty())
			{
				range = 5.0;
				for(std::size_t i = 0; i < trace.size() && i < times_.size(); ++i)
					if(times_[i] >= oldest && times_[i] <= newest) range = std::max(range, std::abs(trace[i]));
				range = std::min(1000.0, std::ceil(range * 1.2 / 5.0) * 5.0);
			}
			painter.setPen(QColor("#d4d4d8"));
			const QString name = channel < static_cast<int>(channel_names_.size()) ? QString::fromUtf8(channel_names_[static_cast<std::size_t>(channel)].c_str()) : QString::number(channel + 1);
			const QString kind = channel < static_cast<int>(channel_types_.size()) ? QString::fromUtf8(channel_types_[static_cast<std::size_t>(channel)].c_str()) : QString();
			const QString title = kind.isEmpty() ? QStringLiteral("%1 · %2").arg(channel + 1).arg(name) : QStringLiteral("%1 · %2 · %3").arg(channel + 1).arg(name, kind);
			painter.drawText(QRect(5, lane.top() + 2, left - 12, text_height), Qt::AlignLeft | Qt::AlignVCenter,
				painter.fontMetrics().elidedText(title, Qt::ElideRight, left - 12));
			painter.setPen(QColor("#a1a1aa"));
			painter.drawText(QRect(5, lane.bottom() - text_height - 2, left - 12, text_height), Qt::AlignLeft | Qt::AlignVCenter,
				QStringLiteral("±%1 µV").arg(range, 0, 'g', 3));
			if(trace.size() >= 2 && times_.size() == trace.size())
			{
				QPainterPath path;
				bool begun = false;
				for(std::size_t i = 0; i < trace.size(); ++i)
				{
					const double time = times_[i];
					if(time < oldest || time > newest) continue;
					const double x = lane.left() + (time - oldest) * lane.width() / std::max(0.001, newest - oldest);
					const double y = lane.center().y() - std::clamp(trace[i] / range, -1.0, 1.0) * (lane.height() * 0.43);
					if(!begun) { path.moveTo(x, y); begun = true; } else path.lineTo(x, y);
				}
				painter.setPen(QPen(QColor::fromHsv((channel * 37 + 168) % 360, 150, 230), 1.35));
				painter.drawPath(path);
			}
			if(sample_rate_ > 0)
				for(const auto& event : events_)
				{
					const double time = static_cast<double>(event.first) / sample_rate_;
					if(time < oldest || time > newest) continue;
					const int x = lane.left() + static_cast<int>((time - oldest) * lane.width() / std::max(0.001, newest - oldest));
					painter.setPen(QPen(QColor("#fbbf24"), 1, Qt::DashLine));
					painter.drawLine(x, lane.top(), x, lane.bottom());
					if(row == 0) painter.drawText(x + 3, lane.top() + text_height, QString::fromUtf8(event.second.c_str()));
				}
		}
		painter.setPen(QColor("#a1a1aa"));
		for(int tick = 0; tick <= 5; ++tick)
		{
			const int x = area.left() + tick * area.width() / 5;
			const int seconds_ago = window_seconds_ - tick * window_seconds_ / 5;
			painter.drawText(QRect(x - 24, height() - bottom, 48, bottom - 2), Qt::AlignCenter,
				seconds_ago == 0 ? QStringLiteral("0 s") : QStringLiteral("−%1 s").arg(seconds_ago));
		}
	}
private:
	std::vector<std::string> channel_names_;
	std::vector<std::string> channel_types_;
	std::vector<std::deque<double>> traces_;
	std::vector<bool> visible_;
	std::deque<double> times_;
	std::vector<std::pair<int, std::string>> events_;
	int sample_rate_ = 1;
	int window_seconds_ = 5;
	double vertical_scale_ = 100.0;
	bool auto_scale_ = true;
	bool paused_ = false;
	bool compact_ = false;
};

class SpectrumView final : public QWidget
{
public:
	explicit SpectrumView(QWidget* parent) : QWidget(parent)
	{
		QFont plot_font(QStringLiteral("Segoe UI"));
		plot_font.setPointSize(9);
		setFont(plot_font);
		setMinimumHeight(180);
	}
	void setSpectrum(const std::vector<double>& samples, int sample_rate)
	{
		bins_.clear();
		if(sample_rate <= 0 || samples.size() < static_cast<std::size_t>(sample_rate)) { update(); return; }
		const int count = static_cast<int>(samples.size());
		const int max_hz = std::min(40, sample_rate / 2);
		std::vector<double> windowed(samples.size());
		double window_energy = 0.0;
		for(int i = 0; i < count; ++i)
		{
			const double window = 0.5 - 0.5 * std::cos(2.0 * M_PI * i / std::max(1, count - 1));
			windowed[static_cast<std::size_t>(i)] = (std::isfinite(samples[static_cast<std::size_t>(i)]) ? samples[static_cast<std::size_t>(i)] : 0.0) * window;
			window_energy += window * window;
		}
		for(int frequency = 0; frequency <= max_hz * 2; ++frequency)
		{
			const double hz = frequency * 0.5;
			if(hz < 0.5) continue;
			double real = 0.0, imaginary = 0.0;
			for(int i = 0; i < count; ++i)
			{
				const double phase = 2.0 * M_PI * hz * i / sample_rate;
				const double value = windowed[static_cast<std::size_t>(i)];
				real += value * std::cos(phase);
				imaginary -= value * std::sin(phase);
			}
			// One-sided Hann-windowed PSD in (microvolts squared)/Hz.
			const double power = 2.0 * (real * real + imaginary * imaginary) / (sample_rate * window_energy);
			bins_.push_back({hz, 10.0 * std::log10(std::max(power, 1e-12))});
		}
		update();
	}
	const std::vector<std::pair<double, double>>& bins() const { return bins_; }
protected:
	void paintEvent(QPaintEvent*) override
	{
		QPainter painter(this);
		painter.fillRect(rect(), QColor("#181a20"));
		const QRect area(55, 25, std::max(1, width() - 70), std::max(1, height() - 73));
		painter.setPen(QColor("#d4d4d8"));
		painter.drawText(QRect(5, 3, width() - 10, 18), Qt::AlignLeft | Qt::AlignVCenter, tr("Спектральная плотность · dB(µV²/Hz)"));
		const std::array<QColor, 4> colours = {QColor("#60a5fa"), QColor("#a78bfa"), QColor("#34d399"), QColor("#fbbf24")};
		const std::array<QPair<double, double>, 4> bands = {{{0.5,4.0},{4.0,8.0},{8.0,13.0},{13.0,30.0}}};
		for(int band = 0; band < 4; ++band)
		{
			const int left = area.left() + static_cast<int>(bands[band].first * area.width() / 40.0);
			const int right = area.left() + static_cast<int>(bands[band].second * area.width() / 40.0);
			painter.fillRect(QRect(left, area.top(), right - left, area.height()), QColor(colours[band].red(), colours[band].green(), colours[band].blue(), 20));
		}
		painter.setPen(QColor("#3f434c"));
		for(int tick = 0; tick <= 4; ++tick)
		{
			const int y = area.top() + tick * area.height() / 4;
			painter.drawLine(area.left(), y, area.right(), y);
		}
		painter.setPen(QColor("#a1a1aa"));
		double high = 10.0, low = -80.0;
		if(!bins_.empty())
		{
			high = bins_.front().second;
			low = high;
			for(const auto& bin : bins_) { high = std::max(high, bin.second); low = std::min(low, bin.second); }
			high += 5.0;
			low -= 5.0;
		}
		painter.drawText(QRect(2, area.top(), 48, 18), Qt::AlignRight, QString::number(high, 'f', 0));
		painter.drawText(QRect(2, area.bottom() - 18, 48, 18), Qt::AlignRight, QString::number(low, 'f', 0));
		for(int tick = 0; tick <= 4; ++tick)
		{
			const int x = area.left() + tick * area.width() / 4;
			painter.drawText(QRect(x - 24, area.bottom() + 3, 48, 20), Qt::AlignCenter, QStringLiteral("%1 Hz").arg(tick * 10));
		}
		if(bins_.size() > 1)
		{
			QPainterPath path;
			for(std::size_t i = 0; i < bins_.size(); ++i)
			{
				const double x = area.left() + bins_[i].first * area.width() / 40.0;
				const double y = area.bottom() - (std::clamp(bins_[i].second, low, high) - low) * area.height() / std::max(1.0, high - low);
				if(i == 0) path.moveTo(x, y); else path.lineTo(x, y);
			}
			painter.setPen(QPen(QColor("#72d8c6"), 1.5));
			painter.drawPath(path);
		}
		else
		{
			painter.setPen(QColor("#a1a1aa"));
			painter.drawText(area, Qt::AlignCenter, tr("Недостаточно отсчётов для спектра"));
		}
		const QStringList labels = {QStringLiteral("Delta"), QStringLiteral("Theta"), QStringLiteral("Alpha"), QStringLiteral("Beta")};
		for(int band = 0; band < labels.size(); ++band)
		{
			painter.setPen(colours[band]);
			const double center_hz = (bands[band].first + bands[band].second) * 0.5;
			const int x = area.left() + static_cast<int>(center_hz * area.width() / 40.0);
			painter.drawText(QRect(x - 34, height() - 26, 68, 19), Qt::AlignCenter, labels[band]);
		}
	}
private:
	std::vector<std::pair<double, double>> bins_;
};

struct NeuroControlSyntheticReader
{
	std::thread thread;
	std::atomic<bool> stop{false};
	std::mutex mutex;
	std::deque<std::vector<float>> samples;
	std::vector<std::string> channels;
	int sample_rate = 0;
	std::string error;
	std::uint64_t dropped_samples = 0;
};

struct NeuroControlLslDiscovery
{
	std::mutex mutex;
	QVector<neurocontrol::LslStreamDescriptor> streams;
	QString error;
	bool complete = false;
};

struct NeuroControlLslConfiguration
{
	std::mutex mutex;
	QString path;
	QString provenance;
	QString sha256;
	QString error;
	bool ready = false;
	bool complete = false;
};

struct NeuroControlLslReader
{
	std::atomic<bool> stop{false};
	std::mutex mutex;
	std::deque<neurocontrol::EegFrame> frames;
	QStringList events;
	QString last_reported_error;
	neurocontrol::LslStreamDescriptor descriptor;
	neurocontrol::LslTimestampDiagnostics timestamps;
	std::uint64_t buffer_overruns = 0;
	std::uint64_t pause_events = 0;
	std::uint64_t reconnects = 0;
	std::uint64_t errors = 0;
	bool connected = false;
	bool finished = false;
	std::unique_ptr<neurocontrol::LslRecordingWriter> recording_writer;
	QString recording_id;
	QString recording_partial_path;
	QString recording_final_path;
	QString recording_metadata_path;
	QJsonObject recording_metadata;
	QString recording_error;
	QString recording_completion_reason;
	std::uint64_t recording_samples = 0;
	std::uint64_t recording_checksum = 0;
	std::uint64_t recording_start_pauses = 0;
	std::uint64_t recording_start_reconnects = 0;
	std::uint64_t recording_start_errors = 0;
	std::uint64_t recording_start_display_drops = 0;
	bool recording_active = false;
	bool recording_stop_requested = false;
	bool recording_finalized = false;
	bool recording_file_complete = false;
};

struct NeuroControlLslRecordingLoad
{
	std::mutex mutex;
	QString recording_id;
	QString path;
	QString error;
	QJsonObject metadata;
	std::vector<float> samples;
	std::vector<double> source_timestamps;
	std::vector<double> arrival_timestamps;
	std::vector<double> unit_to_volts;
	std::uint32_t channel_count = 0;
	std::uint64_t sample_count = 0;
	bool complete = false;
	bool ready = false;
};

struct NeuroControlLslShadowAudit
{
	std::mutex mutex;
	QString recording_id;
	int target_sample_rate_hz = 160;
	QString state = QStringLiteral("queued");
	QJsonObject result;
	bool complete = false;
};

namespace {

bool writeJsonAtomically(const QString& path, const QJsonObject& value, QString& error)
{
	QSaveFile file(path);
	if(!file.open(QIODevice::WriteOnly)) { error = file.errorString(); return false; }
	const QByteArray json = QJsonDocument(value).toJson(QJsonDocument::Indented);
	if(file.write(json) != json.size() || !file.commit()) { error = file.errorString(); return false; }
	return true;
}

bool validLslRecordingId(const QString& id)
{
	static const QRegularExpression pattern(QStringLiteral("^[A-Za-z0-9_-]{1,96}$"));
	return pattern.match(id).hasMatch();
}

QString lslChannelKind(const QString& name, const QString& type)
{
	const QString normalized = type.trimmed().toUpper();
	const QString label = name.trimmed().toUpper();
	if(normalized == QLatin1String("ECG") || normalized == QLatin1String("EKG") ||
		label.contains(QStringLiteral("ECG")) || label.contains(QStringLiteral("EKG")) || label.contains(QStringLiteral("ЭКГ"))) return QStringLiteral("ECG");
	if(normalized == QLatin1String("EEG") || normalized.isEmpty()) return QStringLiteral("EEG");
	return type.trimmed();
}

std::vector<double> numbers(const QJsonArray& array)
{
	std::vector<double> result;
	for(const QJsonValue& value : array) result.push_back(value.toDouble());
	return result;
}

neurocontrol::Model parseModel(const QJsonObject& json)
{
	neurocontrol::Model model;
	if(json.value("format").toString() != "metasiberia-neurocontrol-csp-lda-v1") return model;
	const auto parse_command = [](const QString& name, neurocontrol::CommandType& command) {
		if(name == QStringLiteral("FORWARD")) command = neurocontrol::CommandType::Forward;
		else if(name == QStringLiteral("TURN_LEFT")) command = neurocontrol::CommandType::TurnLeft;
		else if(name == QStringLiteral("TURN_RIGHT")) command = neurocontrol::CommandType::TurnRight;
		else if(name == QStringLiteral("STOP")) command = neurocontrol::CommandType::Stop;
		else return false;
		return true;
	};
	if(json.contains("positive_command") && !parse_command(json.value("positive_command").toString(), model.positive_command)) return neurocontrol::Model();
	if(json.contains("negative_command") && !parse_command(json.value("negative_command").toString(), model.negative_command)) return neurocontrol::Model();
	model.experimental_turn_mapping = json.value("command_mapping_is_experimental").toBool(false);
	model.sample_rate_hz = json.value("sample_rate_hz").toInt();
	model.window_samples = json.value("window_samples").toInt();
	model.step_samples = json.value("step_samples").toInt(0);
	for(const QJsonValue& value : json.value("channels").toArray()) model.channels.push_back(value.toString().toStdString());
	const QJsonArray sos = json.value("sos_filter").toArray().isEmpty() ?
		json.value("sos_bandpass_8_30_hz").toArray() : json.value("sos_filter").toArray();
	for(const QJsonValue& value : sos)
	{
		const auto row = numbers(value.toArray());
		if(row.size() != 6) return neurocontrol::Model();
		model.sos.push_back({row[0], row[1], row[2], row[3], row[4], row[5]});
	}
	for(const QJsonValue& value : json.value("csp_filters").toArray()) model.csp_filters.push_back(numbers(value.toArray()));
	model.lda_coefficients = numbers(json.value("lda_coefficients").toArray());
	model.lda_intercept = json.value("lda_intercept").toDouble();
	model.forward_threshold = json.value("forward_threshold").toDouble();
	return model;
}

QString findNeurocontrolRoot()
{
	// The canonical executable is staged outside the source tree. __FILE__ is
	// an absolute path in the development build, so it also works when the
	// client is launched from the output directory or a desktop shortcut.
	for(QString start : {QDir::currentPath(), QApplication::applicationDirPath(), QFileInfo(QString::fromUtf8(__FILE__)).absolutePath()})
	{
		QDir directory(start);
		for(int depth = 0; depth < 10; ++depth)
		{
			if(directory.exists(QStringLiteral("neurocontrol/tools/prepare_train.py")))
				return directory.filePath(QStringLiteral("neurocontrol"));
			if(directory.dirName() == QStringLiteral("neurocontrol") && directory.exists(QStringLiteral("tools/prepare_train.py")))
				return directory.absolutePath();
			if(!directory.cdUp()) break;
		}
	}
	return QString();
}

QJsonObject currentClientBuildInfo()
{
	const QString executable_path = QCoreApplication::applicationFilePath();
	const QFileInfo executable(executable_path);
	return QJsonObject{
		{"version", QString::fromStdString(::cyberspace_version)},
		{"executable_path", executable_path},
		{"executable_exists", executable.exists() && executable.isFile()},
		{"executable_size_bytes", executable.exists() ? executable.size() : 0},
		{"executable_modified_at_utc", executable.exists() ? executable.lastModified().toUTC().toString(Qt::ISODateWithMs) : QString()}
	};
}

QJsonObject neuroMCPError(const QString& code, const QString& message)
{
	return QJsonObject{{"_mcp_error", QJsonObject{{"code", code}, {"message", message}}}};
}

bool validPhysioNetRecordingId(const QString& id)
{
	if(id.size() != 7 || id[0] != QLatin1Char('S') || id[4] != QLatin1Char('R')) return false;
	for(int i : {1, 2, 3, 5, 6}) if(!id[i].isDigit()) return false;
	return true;
}

QString neuroCommandName(neurocontrol::CommandType type)
{
	switch(type)
	{
	case neurocontrol::CommandType::Forward: return QStringLiteral("FORWARD");
	case neurocontrol::CommandType::TurnLeft: return QStringLiteral("TURN_LEFT");
	case neurocontrol::CommandType::TurnRight: return QStringLiteral("TURN_RIGHT");
	default: return QStringLiteral("STOP");
	}
}

QString neuroSourceName(int index)
{
	if(index == 0) return QStringLiteral("recorded_edf");
	if(index == 1) return QStringLiteral("brainflow_synthetic");
	if(index == 3) return QStringLiteral("lsl_live");
	if(index == 4) return QStringLiteral("lsl_recorded");
	return QStringLiteral("live_eeg_unavailable");
}

QString neuroMotionReasonName(neurocontrol::MotionBlockReason reason)
{
	switch(reason)
	{
	case neurocontrol::MotionBlockReason::ControllerDisabled: return QStringLiteral("controller_disabled");
	case neurocontrol::MotionBlockReason::PhysicalInputHasPriority: return QStringLiteral("physical_input_has_priority");
	case neurocontrol::MotionBlockReason::AvatarMovementDisallowed: return QStringLiteral("avatar_movement_disallowed");
	case neurocontrol::MotionBlockReason::CommandStaleOrClockInvalid: return QStringLiteral("command_stale_or_clock_invalid");
	case neurocontrol::MotionBlockReason::EEGDataUnavailable: return QStringLiteral("eeg_data_unavailable");
	case neurocontrol::MotionBlockReason::EEGDataStale: return QStringLiteral("eeg_data_stale");
	case neurocontrol::MotionBlockReason::ClassifierDelayed: return QStringLiteral("classifier_delayed");
	case neurocontrol::MotionBlockReason::StreamGenerationMismatch: return QStringLiteral("stream_generation_mismatch");
	case neurocontrol::MotionBlockReason::MotionCommandNotRearmed: return QStringLiteral("motion_command_not_rearmed");
	case neurocontrol::MotionBlockReason::SignalQualityNotGood: return QStringLiteral("signal_quality_not_good");
	case neurocontrol::MotionBlockReason::SyntheticSourceCannotControl: return QStringLiteral("synthetic_source_cannot_control");
	case neurocontrol::MotionBlockReason::ConfidenceBelowThreshold: return QStringLiteral("confidence_below_threshold");
	case neurocontrol::MotionBlockReason::StopCommand: return QStringLiteral("stop_command");
	case neurocontrol::MotionBlockReason::TurnMappingNotEnabled: return QStringLiteral("turn_mapping_not_enabled");
	default: return QStringLiteral("none");
	}
}

bool pathIsWithin(const QString& root, const QString& path)
{
	const QString canonical_root = QDir(root).canonicalPath();
	const QString canonical_path = QFileInfo(path).canonicalFilePath();
	if(canonical_root.isEmpty() || canonical_path.isEmpty()) return false;
	const QString relative = QDir(canonical_root).relativeFilePath(canonical_path);
	return relative != QStringLiteral("..") && !relative.startsWith(QStringLiteral("../")) && !QDir::isAbsolutePath(relative);
}

QString relativeRecordingId(const QString& root, const QString& path, bool prepared)
{
	const QString relative = QDir(root).relativeFilePath(QFileInfo(path).canonicalFilePath()).replace(QLatin1Char('\\'), QLatin1Char('/'));
	if(relative.isEmpty() || relative.startsWith(QStringLiteral("../")) || QDir::isAbsolutePath(relative)) return QString();
	const QFileInfo file(relative);
	const QString base = file.completeBaseName();
	const bool top_level = !relative.contains(QLatin1Char('/'));
	if(prepared)
	{
		// Keep ordinary processed IDs (including S01_walk6min) stable. Prefix
		// PhysioNet processed copies so S001R14 continues to mean the raw EDF.
		return top_level && !validPhysioNetRecordingId(base) ? base : QStringLiteral("processed:") + relative.left(relative.size() - file.suffix().size() - 1);
	}
	if(top_level && validPhysioNetRecordingId(base)) return base.toUpper();
	return QStringLiteral("raw:") + relative.left(relative.size() - file.suffix().size() - 1);
}

QFileInfoList filesUnder(const QString& root, const QStringList& patterns, int max_files = 20000)
{
	QFileInfoList files;
	if(QDir(root).canonicalPath().isEmpty()) return files;
	QDirIterator iterator(root, patterns, QDir::Files | QDir::Readable, QDirIterator::Subdirectories);
	while(iterator.hasNext() && files.size() < max_files)
	{
		QFileInfo file(iterator.next());
		const QString canonical = file.canonicalFilePath();
		if(file.isFile() && !canonical.isEmpty() && pathIsWithin(root, canonical)) files.push_back(QFileInfo(canonical));
	}
	return files;
}

QJsonObject readJsonObjectFile(const QString& path)
{
	QFile file(path);
	if(!file.open(QIODevice::ReadOnly) || file.size() > 8 * 1024 * 1024) return {};
	const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
	return document.isObject() ? document.object() : QJsonObject();
}
}

NeuroControlWidget::NeuroControlWidget(GUIClient* client, QWidget* parent) : QWidget(parent), client_(client)
{
	const QString local_data = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
	if(!local_data.isEmpty())
	{
		lsl_recordings_directory_ = QDir(local_data).filePath(QStringLiteral("neurocontrol/lsl-recordings"));
		QDir().mkpath(lsl_recordings_directory_);
	}
	neuro_stream_generation_ = client_->resetNeuroControlStream();
	experiment_logger_ = std::make_unique<neurocontrol::NeuroExperimentLogger>();
	// Keep the panel narrower than the plot contents; traces scroll vertically.
	setMinimumWidth(320);
	setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
	auto* root = new QVBoxLayout(this);
	root->setSizeConstraint(QLayout::SetNoConstraint);
	root->setContentsMargins(6, 4, 6, 6);
	tabs_ = new QTabWidget(this);
	tabs_->setMinimumWidth(0);
	tabs_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
	root->addWidget(tabs_);
	auto* overview = new QWidget(tabs_);
	auto* overview_layout = new QVBoxLayout(overview);
	overview_layout->setContentsMargins(8, 8, 8, 8);
	overview_layout->addWidget(new QLabel(tr("Нейроуправление MetaSiberia · исследовательский прототип"), overview));
	source_ = new QComboBox(overview);
	source_->addItem(tr("Записанная ЭЭГ · EDF / BDF / F32"), QStringLiteral("recorded"));
	source_->addItem(tr("Синтетическая ЭЭГ · BrainFlow"), QStringLiteral("synthetic"));
	source_->addItem(tr("ЭЭГ с устройства · пока не подключено"), QStringLiteral("live"));
	source_->addItem(tr("LSL — поток в реальном времени"), QStringLiteral("lsl"));
	source_->addItem(tr("Запись LSL · воспроизведение"), QStringLiteral("lsl_recorded"));
	overview_layout->addWidget(source_);
	auto* file_row = new QHBoxLayout;
	auto* load_recording = new QPushButton(tr("Загрузить запись…"), overview);
	auto* load_model = new QPushButton(tr("Модель…"), overview);
	save_model_button_ = new QPushButton(tr("Сохранить копию модели…"), overview);
	save_model_button_->setEnabled(false);
	file_row->addWidget(load_recording);
	file_row->addWidget(load_model);
	file_row->addWidget(save_model_button_);
	overview_layout->addLayout(file_row);
	recording_label_ = new QLabel(tr("Запись не выбрана"), overview);
	model_label_ = new QLabel(tr("Модель не обучена или не выбрана"), overview);
	source_status_label_ = new QLabel(tr("Источник: ожидание выбора"), overview);
	auto* source_group = new QGroupBox(tr("Источник ЭЭГ"), overview);
	auto* source_group_layout = new QVBoxLayout(source_group);
	source_group_layout->addWidget(source_status_label_);
	source_group_layout->addWidget(recording_label_);
	auto* lsl_group = new QGroupBox(tr("Приём LSL · только сигналы и диагностика"), source_group);
	auto* lsl_layout = new QVBoxLayout(lsl_group);
	auto* library_row = new QHBoxLayout;
	auto* lsl_library_button = new QPushButton(tr("Библиотека LSL…"), lsl_group);
	lsl_library_path_ = QSettings().value(QStringLiteral("neurocontrol/lslLibraryPath")).toString();
	library_row->addWidget(lsl_library_button);
	lsl_layout->addLayout(library_row);
	lsl_streams_ = new QComboBox(lsl_group);
	lsl_streams_->setMinimumWidth(0);
	lsl_layout->addWidget(lsl_streams_);
	lsl_units_override_ = new QComboBox(lsl_group);
	lsl_units_override_->addItem(tr("Единицы из XML"), QStringLiteral("auto"));
	lsl_units_override_->addItem(tr("Переопределить: микровольты"), QStringLiteral("microvolts"));
	lsl_units_override_->addItem(tr("Переопределить: вольты"), QStringLiteral("volts"));
	lsl_units_override_->addItem(tr("Переопределить: милливольты"), QStringLiteral("millivolts"));
	lsl_layout->addWidget(lsl_units_override_);
	auto* lsl_buttons = new QHBoxLayout;
	lsl_scan_button_ = new QPushButton(tr("Найти потоки"), lsl_group);
	lsl_connect_button_ = new QPushButton(tr("Подключить"), lsl_group);
	lsl_disconnect_button_ = new QPushButton(tr("Отключить"), lsl_group);
	lsl_connect_button_->setEnabled(false);
	lsl_disconnect_button_->setEnabled(false);
	lsl_buttons->addWidget(lsl_scan_button_);
	lsl_buttons->addWidget(lsl_connect_button_);
	lsl_buttons->addWidget(lsl_disconnect_button_);
	lsl_layout->addLayout(lsl_buttons);
	lsl_shadow_checkbox_ = new QCheckBox(tr("Теневая классификация · результат не передаётся аватару"), lsl_group);
	lsl_shadow_checkbox_->setChecked(false);
	lsl_shadow_checkbox_->setEnabled(false);
	lsl_shadow_checkbox_->setToolTip(tr("После проверки частоты, каналов, единиц и контракта монтажа поток ресемплируется FIR-фильтром до частоты модели. Результаты остаются диагностическими и не управляют аватаром."));
	lsl_layout->addWidget(lsl_shadow_checkbox_);
	lsl_shadow_contract_confirm_checkbox_ = new QCheckBox(tr("Я сверил монтаж и референс потока с обучающими данными модели"), lsl_group);
	lsl_shadow_contract_confirm_checkbox_->setChecked(false);
	lsl_shadow_contract_confirm_checkbox_->setEnabled(false);
	lsl_shadow_contract_confirm_checkbox_->setToolTip(tr("Потребуется, если XML LSL или JSON модели не содержат точных сведений о монтаже/референсе. Несовпадающие заявленные значения всё равно блокируют классификацию."));
	lsl_layout->addWidget(lsl_shadow_contract_confirm_checkbox_);
	lsl_shadow_status_label_ = new QLabel(tr("Теневая классификация выключена. Сначала загрузите совместимую модель и подключите поток."), lsl_group);
	lsl_shadow_status_label_->setWordWrap(true);
	lsl_layout->addWidget(lsl_shadow_status_label_);
	lsl_details_label_ = new QLabel(tr("Выберите библиотеку lsl.dll, затем найдите поток."), lsl_group);
	lsl_details_label_->setWordWrap(true);
	if(!lsl_library_path_.isEmpty())
		lsl_details_label_->setText(tr("Saved path: %1. MCP validates bitness and exports through neuro.configure_lsl.").arg(QFileInfo(lsl_library_path_).fileName()));
	lsl_layout->addWidget(lsl_details_label_);
	auto* lsl_recording_controls = new QGridLayout;
	lsl_record_button_ = new QPushButton(tr("Начать запись"), lsl_group);
	lsl_record_stop_button_ = new QPushButton(tr("Завершить запись"), lsl_group);
	lsl_record_load_button_ = new QPushButton(tr("Открыть запись…"), lsl_group);
	lsl_record_play_button_ = new QPushButton(tr("Воспроизвести запись"), lsl_group);
	lsl_record_stop_button_->setEnabled(false);
	lsl_record_play_button_->setEnabled(false);
	lsl_recording_status_label_ = new QLabel(tr("LSL-запись не выполняется"), lsl_group);
	lsl_recording_status_label_->setWordWrap(true);
	lsl_recording_controls->addWidget(lsl_record_button_, 0, 0);
	lsl_recording_controls->addWidget(lsl_record_stop_button_, 0, 1);
	lsl_recording_controls->addWidget(lsl_record_load_button_, 1, 0);
	lsl_recording_controls->addWidget(lsl_record_play_button_, 1, 1);
	lsl_layout->addLayout(lsl_recording_controls);
	lsl_layout->addWidget(lsl_recording_status_label_);
	lsl_group->setVisible(false);
	source_group_layout->addWidget(lsl_group);
	overview_layout->addWidget(source_group);
	overview_waveform_ = new WaveformView(overview, true);
	overview_layout->addWidget(overview_waveform_);
	auto* dataset_group = new QGroupBox(tr("Научные данные"), overview);
	auto* dataset_group_layout = new QVBoxLayout(dataset_group);
	dataset_status_label_ = new QLabel(tr("Проверка данных не выполнена"), dataset_group);
	dataset_status_label_->setWordWrap(true);
	dataset_group_layout->addWidget(dataset_status_label_);
	overview_layout->addWidget(dataset_group);
	auto* model_group = new QGroupBox(tr("Модель"), overview);
	auto* model_group_layout = new QVBoxLayout(model_group);
	model_group_layout->addWidget(model_label_);
	model_details_label_ = new QLabel(tr("Загрузите модель JSON или обучите CSP + LDA."), model_group);
	model_details_label_->setWordWrap(true);
	model_group_layout->addWidget(model_details_label_);
	overview_layout->addWidget(model_group);
	state_label_ = new QLabel(tr("Состояние: ОСТАНОВКА"), overview);
	avatar_status_label_ = new QLabel(tr("Аватар: УПРАВЛЕНИЕ ОТКЛЮЧЕНО"), overview);
	confidence_ = new QProgressBar(overview);
	confidence_->setRange(0, 100);
	confidence_->setFormat(tr("Уверенность команды «Вперёд»: %p%"));
	auto* recognition_group = new QGroupBox(tr("Распознавание"), overview);
	auto* recognition_layout = new QVBoxLayout(recognition_group);
	recognition_layout->addWidget(state_label_);
	recognition_details_label_ = new QLabel(tr("Предсказаний нет"), recognition_group);
	recognition_details_label_->setWordWrap(true);
	recognition_layout->addWidget(recognition_details_label_);
	recognition_layout->addWidget(confidence_);
	overview_layout->addWidget(recognition_group);
	control_enabled_ = new QCheckBox(tr("Разрешить управление аватаром по распознанной ЭЭГ"), overview);
	control_enabled_->setToolTip(tr("Synthetic Board и программные тесты не считаются распознаванием намерений."));
	auto* avatar_group = new QGroupBox(tr("Аватар"), overview);
	auto* avatar_group_layout = new QVBoxLayout(avatar_group);
	avatar_group_layout->addWidget(avatar_status_label_);
	avatar_group_layout->addWidget(control_enabled_);
	overview_layout->addWidget(avatar_group);
	auto* system_group = new QGroupBox(tr("Система"), overview);
	auto* system_group_layout = new QVBoxLayout(system_group);
	system_details_label_ = new QLabel(tr("Проверка Python и инструментов…"), system_group);
	system_details_label_->setWordWrap(true);
	system_group_layout->addWidget(system_details_label_);
	overview_layout->addWidget(system_group);
	auto* playback_row = new QHBoxLayout;
	start_button_ = new QPushButton(tr("Старт"), overview);
	pause_button_ = new QPushButton(tr("Пауза"), overview);
	auto* stop_button = new QPushButton(tr("Стоп"), overview);
	auto* emergency_stop = new QPushButton(tr("Аварийная остановка"), overview);
	playback_row->addWidget(start_button_);
	playback_row->addWidget(pause_button_);
	playback_row->addWidget(stop_button);
	overview_layout->addWidget(emergency_stop);
	overview_layout->addLayout(playback_row);
	auto* seek_row = new QHBoxLayout;
	playback_time_label_ = new QLabel(tr("00:00 / 00:00"), overview);
	seek_slider_ = new QSlider(Qt::Horizontal, overview);
	seek_slider_->setRange(0, 0);
	playback_speed_ = new QComboBox(overview);
	for(const double speed : {0.25, 0.5, 1.0, 2.0, 4.0}) playback_speed_->addItem(QStringLiteral("%1×").arg(speed), speed);
	playback_speed_->setCurrentIndex(2);
	playback_speed_->setToolTip(tr("Для оценки задержки используйте 1×. Ускоренное воспроизведение — только для просмотра и анализа."));
	seek_row->addWidget(playback_time_label_);
	seek_row->addWidget(seek_slider_, 1);
	seek_row->addWidget(new QLabel(tr("Скорость"), overview));
	seek_row->addWidget(playback_speed_);
	overview_layout->addLayout(seek_row);
	auto* overview_scroll = new QScrollArea(tabs_);
	overview_scroll->setWidgetResizable(true);
	overview_scroll->setFrameShape(QFrame::NoFrame);
	overview_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	overview_scroll->setWidget(overview);
	tabs_->addTab(overview_scroll, tr("Обзор"));

	auto* signals_page = new QWidget(tabs_);
	auto* signals_layout = new QVBoxLayout(signals_page);
	signals_layout->setContentsMargins(8, 8, 8, 8);
	auto* plot_toolbar = new QGridLayout;
	time_window_ = new QComboBox(signals_page);
	for(int seconds : {2, 5, 10, 30}) time_window_->addItem(tr("%1 с").arg(seconds), seconds);
	time_window_->setCurrentIndex(1);
	vertical_scale_ = new QDoubleSpinBox(signals_page);
	vertical_scale_->setRange(5.0, 1000.0);
	vertical_scale_->setValue(100.0);
	vertical_scale_->setSuffix(tr(" мкВ"));
	vertical_scale_->setToolTip(tr("Вертикальный диапазон каждой осциллограммы при выключенном автомасштабе."));
	plot_auto_scale_ = new QCheckBox(tr("Автомасштаб"), signals_page);
	plot_auto_scale_->setChecked(true);
	plot_paused_ = new QCheckBox(tr("Пауза графика"), signals_page);
	auto* show_all = new QPushButton(tr("Все каналы"), signals_page);
	auto* hide_all = new QPushButton(tr("Скрыть"), signals_page);
	auto* show_detail = new QPushButton(tr("Подробно выбранный канал"), signals_page);
	plot_toolbar->addWidget(new QLabel(tr("Окно"), signals_page), 0, 0);
	plot_toolbar->addWidget(time_window_, 0, 1);
	plot_toolbar->addWidget(new QLabel(tr("Диапазон"), signals_page), 0, 2);
	plot_toolbar->addWidget(vertical_scale_, 0, 3);
	plot_toolbar->addWidget(plot_auto_scale_, 1, 0, 1, 2);
	plot_toolbar->addWidget(plot_paused_, 1, 2, 1, 2);
	plot_toolbar->addWidget(show_all, 2, 0, 1, 2);
	plot_toolbar->addWidget(hide_all, 2, 2, 1, 2);
	plot_toolbar->addWidget(show_detail, 3, 0, 1, 4);
	auto* reset_plot = new QPushButton(tr("Сбросить масштаб"), signals_page);
	plot_toolbar->addWidget(reset_plot, 4, 0, 1, 4);
	signals_layout->addLayout(plot_toolbar);
	channel_list_ = new QListWidget(signals_page);
	channel_list_->setMaximumHeight(110);
	signals_layout->addWidget(channel_list_);
	waveform_ = new WaveformView(signals_page);
	auto* waveform_scroll = new QScrollArea(signals_page);
	waveform_scroll->setWidgetResizable(true);
	waveform_scroll->setFrameShape(QFrame::NoFrame);
	waveform_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	waveform_scroll->setWidget(waveform_);
	signals_layout->addWidget(waveform_scroll, 3);
	auto* spectrum_row = new QHBoxLayout;
	spectrum_row->addWidget(new QLabel(tr("Спектр канала"), signals_page));
	spectrum_channel_ = new QComboBox(signals_page);
	spectrum_channel_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
	spectrum_row->addWidget(spectrum_channel_, 1);
	spectrum_ = new SpectrumView(signals_page);
	signals_layout->addLayout(spectrum_row);
	signals_layout->addWidget(spectrum_, 2);
	tabs_->addTab(signals_page, tr("Сигналы ЭЭГ"));

	auto* training_page = new QWidget(tabs_);
	auto* training_layout = new QVBoxLayout(training_page);
	training_layout->addWidget(new QLabel(tr("Обучение классификатора CSP + LDA"), training_page));
	auto* dataset_training_group = new QGroupBox(tr("PhysioNet EEG Motor Movement/Imagery · S001"), training_page);
	auto* dataset_training_layout = new QVBoxLayout(dataset_training_group);
	dataset_training_status_ = new QLabel(tr("Проверка научных данных…"), dataset_training_group);
	dataset_training_status_->setWordWrap(true);
	dataset_training_layout->addWidget(dataset_training_status_);
	audit_button_ = new QPushButton(tr("Проверить файлы EDF через MNE"), dataset_training_group);
	dataset_training_layout->addWidget(audit_button_);
	training_layout->addWidget(dataset_training_group);
	training_task_ = new QComboBox(training_page);
	training_task_->addItem(tr("Стопы / покой → FORWARD proxy · R06/R10, тест R14"), QStringLiteral("feet"));
	training_task_->addItem(tr("Левая / правая кисть → условный поворот · R04/R08, тест R12"), QStringLiteral("hands"));
	training_layout->addWidget(training_task_);
	auto* training_explanation = new QLabel(tr("Окна обучения и теста не пересекаются. Смысл T1/T2 берётся из серии записи. Условное сопоставление кистей с поворотами не является распознаванием естественного поворота тела."), training_page);
	training_explanation->setWordWrap(true);
	training_layout->addWidget(training_explanation);
	auto* preparation_group = new QGroupBox(tr("Подготовка сигнала"), training_page);
	auto* preparation_form = new QFormLayout(preparation_group);
	const auto make_parameter = [preparation_group](double value, double minimum, double maximum, double step, const QString& suffix) {
		auto* box = new QDoubleSpinBox(preparation_group);
		box->setRange(minimum, maximum);
		box->setSingleStep(step);
		box->setDecimals(1);
		box->setValue(value);
		box->setSuffix(suffix);
		return box;
	};
	low_cutoff_ = make_parameter(8.0, 0.5, 40.0, 0.5, tr(" Гц"));
	high_cutoff_ = make_parameter(30.0, 5.0, 79.0, 1.0, tr(" Гц"));
	epoch_seconds_ = make_parameter(2.0, 1.0, 8.0, 0.5, tr(" с"));
	epoch_step_seconds_ = make_parameter(2.0, 0.5, 8.0, 0.5, tr(" с"));
	event_offset_seconds_ = make_parameter(0.5, 0.0, 3.0, 0.1, tr(" с"));
	confidence_threshold_ = make_parameter(0.75, 0.55, 0.95, 0.01, QString());
	notch_filter_ = new QCheckBox(tr("50 Гц"), preparation_group);
	preparation_form->addRow(tr("Нижняя частота полосы"), low_cutoff_);
	preparation_form->addRow(tr("Верхняя частота полосы"), high_cutoff_);
	preparation_form->addRow(tr("Длина эпохи"), epoch_seconds_);
	preparation_form->addRow(tr("Шаг окна"), epoch_step_seconds_);
	preparation_form->addRow(tr("Смещение от начала события"), event_offset_seconds_);
	preparation_form->addRow(tr("Порог класса"), confidence_threshold_);
	preparation_form->addRow(tr("Режекторный фильтр"), notch_filter_);
	training_channels_ = new QListWidget(preparation_group);
	training_channels_->setMaximumHeight(82);
	for(const char* name : {"FC3","FC1","FCz","FC2","FC4","C5","C3","C1","Cz","C2","C4","C6","CP3","CP1","CPz","CP2","CP4"})
	{
		auto* item = new QListWidgetItem(QString::fromLatin1(name), training_channels_);
		item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
		item->setCheckState(Qt::Checked);
	}
	preparation_form->addRow(tr("Каналы 10–10"), training_channels_);
	training_layout->addWidget(preparation_group);
	training_result_ = new NeuroTextBrowser(training_page);
	training_result_->setOpenExternalLinks(true);
	training_result_->setHtml(tr("<p>Отчёт качества появится после обучения или загрузки существующего отчёта.</p><p><a href=\"https://physionet.org/content/eegmmidb/1.0.0/\">PhysioNet EEGMMIDB</a> · <a href=\"https://mne.tools/stable/auto_examples/decoding/decoding_csp_eeg.html\">Пример CSP в MNE</a></p>"));
	training_layout->addWidget(training_result_, 1);
	training_status_ = new QLabel(tr("Python и скрипты обучения не выбраны."), training_page);
	training_layout->addWidget(training_status_);
	training_progress_ = new QProgressBar(training_page);
	training_progress_->setRange(0, 100);
	training_progress_->setValue(0);
	training_progress_->setFormat(tr("Готово"));
	training_layout->addWidget(training_progress_);
	auto* tool_buttons = new QHBoxLayout;
	auto* choose_python = new QPushButton(tr("Python…"), training_page);
	auto* choose_trainer = new QPushButton(tr("Скрипт обучения…"), training_page);
	auto* choose_downloader = new QPushButton(tr("Скрипт загрузки…"), training_page);
	tool_buttons->addWidget(choose_python);
	tool_buttons->addWidget(choose_trainer);
	tool_buttons->addWidget(choose_downloader);
	training_layout->addLayout(tool_buttons);
	auto* training_buttons = new QHBoxLayout;
	download_button_ = new QPushButton(tr("Загрузить набор"), training_page);
	train_button_ = new QPushButton(tr("Обучить и проверить"), training_page);
	auto* load_trained_model = new QPushButton(tr("Загрузить модель…"), training_page);
	training_buttons->addWidget(download_button_);
	training_buttons->addWidget(train_button_);
	training_buttons->addWidget(load_trained_model);
	training_layout->addLayout(training_buttons);
	tabs_->addTab(training_page, tr("Обучение модели"));

	auto* avatar_page = new QWidget(tabs_);
	auto* avatar_layout = new QVBoxLayout(avatar_page);
	avatar_layout->addWidget(new QLabel(tr("Распознавание ЭЭГ и проверка команд аватара"), avatar_page));
	auto* avatar_commands = new QFormLayout;
	avatar_prediction_label_ = new QLabel(tr("Предсказание пока отсутствует"), avatar_page);
	avatar_action_label_ = new QLabel(tr("УПРАВЛЕНИЕ ОТКЛЮЧЕНО"), avatar_page);
	avatar_confidence_ = new QProgressBar(avatar_page);
	avatar_confidence_->setRange(0, 100);
	avatar_confidence_->setFormat(tr("Уверенность текущего класса: %p%"));
	positive_class_probability_ = new QProgressBar(avatar_page);
	positive_class_probability_->setRange(0, 100);
	negative_class_probability_ = new QProgressBar(avatar_page);
	negative_class_probability_->setRange(0, 100);
	avatar_commands->addRow(tr("Состояние команды"), avatar_action_label_);
	avatar_commands->addRow(tr("Последняя оценка"), avatar_prediction_label_);
	avatar_commands->addRow(tr("Уверенность модели"), avatar_confidence_);
	avatar_commands->addRow(tr("Оценка класса"), positive_class_probability_);
	avatar_commands->addRow(tr("Оценка второго класса"), negative_class_probability_);
	avatar_layout->addLayout(avatar_commands);
	avatar_layout->addWidget(new QLabel(tr("ЭЭГ-распознавание: запись воспроизводится с исходной частотой и классифицируется моделью. Команда движения проходит фильтр качества, порога и тайм-аута."), avatar_page));
	avatar_control_enabled_ = new QCheckBox(tr("Разрешить управление аватаром по распознанной ЭЭГ"), avatar_page);
	avatar_layout->addWidget(avatar_control_enabled_);
	avatar_layout->addWidget(new QLabel(tr("Тестовый режим · команды программные, это не распознавание ЭЭГ"), avatar_page));
	test_forward_ = new QCheckBox(tr("Во время тестового поворота двигаться вперёд"), avatar_page);
	avatar_layout->addWidget(test_forward_);
	auto* test_forward_button = new QPushButton(tr("Идти вперёд · тест"), avatar_page);
	auto* test_turn_row = new QHBoxLayout;
	auto* left = new QPushButton(tr("Влево · тест"), avatar_page);
	auto* right = new QPushButton(tr("Вправо · тест"), avatar_page);
	test_turn_row->addWidget(left);
	test_turn_row->addWidget(right);
	avatar_layout->addWidget(test_forward_button);
	avatar_layout->addLayout(test_turn_row);
	auto* test_stop = new QPushButton(tr("Стоп · тест"), avatar_page);
	auto* avatar_emergency_stop = new QPushButton(tr("Аварийная остановка"), avatar_page);
	avatar_layout->addWidget(test_stop);
	avatar_layout->addWidget(avatar_emergency_stop);
	avatar_layout->addStretch(1);
	tabs_->addTab(avatar_page, tr("Управление аватаром"));

	auto* help_page = new NeuroTextBrowser(tabs_);
	help_page->setOpenExternalLinks(true);
	help_page->setHtml(tr("<h1>Инструкция NeuroControl</h1><h2>Оглавление</h2><ol><li><a href=\"#about\">Что такое ЭЭГ и двигательное воображение</a></li><li><a href=\"#flow\">Как проходит команда</a></li><li><a href=\"#synthetic\">Синтетическая ЭЭГ</a></li><li><a href=\"#recorded\">Записанная ЭЭГ</a></li><li><a href=\"#model\">Модель</a></li><li><a href=\"#avatar\">Управление аватаром</a></li><li><a href=\"#limits\">Ограничения и безопасность</a></li></ol><h2><a name=\"about\"></a>1. ЭЭГ и двигательное воображение</h2><p>ЭЭГ измеряет небольшие изменения электрического потенциала на поверхности головы. Двигательное воображение — мысленное представление движения без фактического выполнения. Это не чтение мыслей: классификатор ищет статистические закономерности в сигнале и может ошибаться.</p><h2><a name=\"flow\"></a>2. Путь команды</h2><p>Мозговая активность → получение ЭЭГ → фильтрация → признаки → классификатор → фильтр безопасности → контроллер аватара.</p><h2><a name=\"synthetic\"></a>3. Проверка синтетического источника</h2><ol><li>Выберите «Синтетическая ЭЭГ · BrainFlow».</li><li>Нажмите «Старт» и откройте «Сигналы ЭЭГ».</li><li>Выберите каналы и временной диапазон.</li><li>Проверьте поступление отсчётов, затем нажмите «Стоп».</li></ol><p>Это искусственный поток для проверки подключения и отображения. Он не содержит намерений человека и не управляет аватаром.</p><h2><a name=\"recorded\"></a>4. Научные записи</h2><ol><li>Нажмите «Запись EDF…» и выберите файл.</li><li>При первом использовании укажите Python с установленными MNE и NumPy; EDF будет подготовлен в отдельном процессе.</li><li>После загрузки проверьте каналы, частоту, события и графики, затем нажмите «Старт».</li><li>Для оценки задержки используйте скорость 1×. Ускорение предназначено только для просмотра.</li></ol><p>В стандартных запусках R06/R10/R14: T0 — покой, T1 — воображение движения обеих кистей, T2 — обеих стоп. В R04/R08/R12 T1/T2 означают воображение движения левого/правого кулака. Для других EDF назначение кодов неизвестно, если оно не описано в самих данных.</p><h2><a name=\"model\"></a>5. Модель и обучение</h2><p>Обучение CSP + LDA выполняется отдельно от интерфейса. Данные проверки берутся из отдельной серии, не использованной для обучения. Отчёт показывает сбалансированную точность, точность и полноту положительного класса, F1, матрицу ошибок, ложные команды и ограничения разметки. После обучения модель можно сохранить копией.</p><h2><a name=\"avatar\"></a>6. Управление аватаром</h2><p>Управление выключено по умолчанию. Сначала загрузите совместимую модель и запись, проверьте оценки классов и качество сигнала, затем включите разрешение управления. Тестовые команды поворота помечены как программная симуляция. «Аварийная остановка» снимает разрешение и отправляет STOP.</p><h2><a name=\"limits\"></a>7. Ограничения и безопасность</h2><ul><li>ЭЭГ не читает мысли буквально; классификация может ошибаться и требует индивидуальной калибровки.</li><li>Воображение движения обеих стоп не означает воображение ходьбы.</li><li>В текущих записях нет классов естественного мысленного поворота тела.</li><li>Условное сопоставление воображения движения кистей с поворотом не является распознаванием естественного поворота.</li><li>При плохом сигнале, недостаточной уверенности, устаревшей команде или отключении разрешения движение прекращается.</li><li>Физическая ЭЭГ-гарнитура пока не подключена. Модуль — исследовательский прототип, не медицинское изделие.</li></ul><p><a href=\"https://brainflow.readthedocs.io/en/stable/SupportedBoards.html\">Документация BrainFlow</a> · <a href=\"https://physionet.org/content/eegmmidb/1.0.0/\">Набор PhysioNet EEGMMIDB</a></p>"));
	QTextCursor help_cursor = help_page->textCursor();
	help_cursor.movePosition(QTextCursor::End);
	help_cursor.insertHtml(tr("<h2>Дополнительные разделы</h2><p><a href=\"#dataset-check\">Проверка научных данных</a> · <a href=\"#training-steps\">Обучение</a> · <a href=\"#errors\">Ошибки</a> · <a href=\"#calibration\">Калибровка</a></p>"
		"<h2><a name=\"dataset-check\"></a>Проверка научных данных</h2><p>На вкладке «Обучение модели» проверьте статус набора S001. Если EDF отсутствуют, нажмите «Загрузить набор». Если отчёт проверки отсутствует, нажмите «Проверить файлы EDF через MNE». Статус «данные проверены» появляется после чтения сигналов и событий через MNE и сверки SHA-256 с PhysioNet.</p>"
		"<h2><a name=\"training-steps\"></a>Обучение модели</h2><p>Выберите задачу «Стопы / покой», проверьте каналы и параметры, затем нажмите «Обучить и проверить». R06 и R10 используются для обучения, R14 — для независимой проверки. После успешной работы модель загружается автоматически; результаты отображаются на этой вкладке. Synthetic Board работает и без модели: он проверяет поток и графики, но не распознаёт намерения и не двигает аватаром.</p>"
		"<h2><a name=\"errors\"></a>Возможные ошибки</h2><ul><li>Нет EDF: загрузите набор или выберите существующий файл.</li><li>Нет отчёта MNE: запустите проверку EDF.</li><li>Python не найден: выберите локальный python.exe на вкладке обучения.</li><li>Нет MNE/NumPy/SciPy/scikit-learn: установите зависимости из requirements-lock.txt в отдельное окружение.</li><li>Несовместима модель: используйте EDF с теми же каналами и частотой, что при обучении.</li><li>Нет спектра: запустите источник и дождитесь минимум одной секунды отсчётов.</li></ul>"
		"<h2><a name=\"calibration\"></a>Индивидуальная калибровка</h2><p>Текущая модель S001 не калибрована для другого человека. Для индивидуального управления нужны запись собственных сессий, точные синхронные метки упражнений и независимая проверка модели до разрешения движения. Поддержка внешней медицинской системы пока проектируется.</p>"));
	help_cursor.movePosition(QTextCursor::End);
	help_cursor.insertHtml(tr("<h2>Работа с ИИ-помощником NeuroControl</h2><p>При включённом MCP во встроенном чате зарегистрированы 49 инструментов <code>neuro.*</code>. ИИ читает актуальное состояние виджета, метаданные EDF/моделей, текущий спектр, предсказания и трассу контроллера. Технические статьи доступны по темам через локальные MCP-ресурсы.</p><p>Примеры: «Покажи состояние нейроуправления»; «Какие EDF найдены и проверены?»; «Загрузи S001R14»; «Проверь совместимость модели, затем оцени S001_feet_vs_rest на S001R14»; «Почему последнее FORWARD не дошло до аватара?»; «Найди поток ЭЭГ Нейрософт и проверь его через LSL без включения управления аватаром»; «Проверь сохранённую LSL-запись через neuro.audit_lsl_recording с target_sample_rate=160 и сохрани отчёт S05»; «Останови нейроуправление».</p><p>Live LSL доступен для графиков, диагностики, записи и отдельно включаемой теневой классификации. Классификация требует точных каналов модели, единиц, устойчивой измеренной частоты, совместимого или отдельно подтверждённого монтажа/референса; смена частоты выполняется потоковым FIR-ресемплером. Источник с непроверенной временной шкалой остаётся помеченным как таковой. Теневая оценка никогда не отправляет команды аватару. MCP показывает счётчики, скорость, ошибки и причины блокировки. Offline-аудит проверяет `.nlsr` тем же ресемплерным трактом, но не запускает классификатор: chunk timestamps не подтверждают фактическую скорость источника. Архивное LSL-воспроизведение остаётся только визуальным. Оценка модели выполняется в фоне и сохраняется отдельным отчётом; её результат доступен через состояние задачи. Изменение состояния выполняется только по вашей явной команде. Обучение и подготовка EDF работают в фоне; состояние задачи можно уточнить через чат. Синтетический поток не является мозговой активностью. Не отправляйте сырые персональные записи во внешний сервис.</p>"));
	tabs_->addTab(help_page, tr("Инструкция"));

	auto* diagnostics_page = new QWidget(tabs_);
	auto* diagnostics_layout = new QVBoxLayout(diagnostics_page);
	diagnostics_summary_ = new QLabel(tr("Источник: ожидание · получено 0 отсчётов · потери 0 · частота приёма — · задержка классификации — · последняя классификация — · контроллер: остановлен"), diagnostics_page);
	diagnostics_summary_->setWordWrap(true);
	diagnostics_layout->addWidget(diagnostics_summary_);
	auto* diag_toolbar = new QGridLayout;
	diagnostic_filter_ = new QComboBox(diagnostics_page);
	diagnostic_filter_->addItems({tr("Все уровни"), QStringLiteral("INFO"), QStringLiteral("WARNING"), QStringLiteral("ERROR"), QStringLiteral("DEBUG")});
	diagnostic_category_filter_ = new QComboBox(diagnostics_page);
	diagnostic_category_filter_->addItems({tr("Все категории"), tr("BrainFlow"), tr("EDF"), tr("Python"), tr("Обучение"), tr("Классификатор"), tr("Аватар"), tr("Интерфейс")});
	auto* clear_log = new QPushButton(tr("Очистить"), diagnostics_page);
	auto* copy_log = new QPushButton(tr("Копировать"), diagnostics_page);
	auto* save_log = new QPushButton(tr("Сохранить…"), diagnostics_page);
	diag_toolbar->addWidget(diagnostic_filter_, 0, 0);
	diag_toolbar->addWidget(diagnostic_category_filter_, 0, 1);
	diag_toolbar->addWidget(clear_log, 1, 0);
	diag_toolbar->addWidget(copy_log, 1, 1);
	diag_toolbar->addWidget(save_log, 2, 0, 1, 2);
	diagnostics_layout->addLayout(diag_toolbar);
	diagnostics_ = new QTextEdit(diagnostics_page);
	diagnostics_->setReadOnly(true);
	diagnostics_layout->addWidget(diagnostics_, 1);
	tabs_->addTab(diagnostics_page, tr("Диагностика"));

	const QString neuro_root = findNeurocontrolRoot();
	QSettings preferences;
	python_path_ = preferences.value(QStringLiteral("neurocontrol/pythonPath")).toString();
	if(python_path_.isEmpty() || !QFileInfo::exists(python_path_))
	{
		const QString local_python = QDir(neuro_root).filePath(QStringLiteral(".venv/Scripts/python.exe"));
		python_path_ = QFileInfo::exists(local_python) ? local_python : QStandardPaths::findExecutable(QStringLiteral("python"));
	}
	training_script_path_ = QDir(neuro_root).filePath(QStringLiteral("tools/prepare_train.py"));
	evaluation_script_path_ = QDir(neuro_root).filePath(QStringLiteral("tools/evaluate_model.py"));
	download_script_path_ = QDir(neuro_root).filePath(QStringLiteral("tools/download_physionet.py"));
	audit_script_path_ = QDir(neuro_root).filePath(QStringLiteral("tools/audit_dataset.py"));
	prepare_recording_script_path_ = QDir(neuro_root).filePath(QStringLiteral("tools/prepare_recording.py"));
	if(neuro_root.isEmpty())
	{
		training_script_path_.clear();
		evaluation_script_path_.clear();
		download_script_path_.clear();
		audit_script_path_.clear();
		prepare_recording_script_path_.clear();
	}
	training_process_ = new QProcess(this);
	training_process_->setProcessChannelMode(QProcess::SeparateChannels);
	connect(training_process_, &QProcess::readyReadStandardOutput, this, [this] {
		const QString output = QString::fromUtf8(training_process_->readAllStandardOutput()).trimmed();
		if(!output.isEmpty())
		{
			if(output.contains(QStringLiteral("Загрузка EDF"))) training_status_->setText(tr("Чтение EDF и подготовка эпох…"));
			else if(output.contains(QStringLiteral("Обучение CSP"))) training_status_->setText(tr("Обучение CSP + LDA и независимая проверка…"));
			else if(output.contains(QStringLiteral("Сохранение многоканального потока"))) training_status_->setText(tr("Сохранение подготовленных данных…"));
			appendDiagnostic(output);
		}
	});
	connect(training_process_, &QProcess::readyReadStandardError, this, [this] {
		const QString output = QString::fromUtf8(training_process_->readAllStandardError()).trimmed();
		if(!output.isEmpty()) appendDiagnostic(tr("Вывод инструмента: %1").arg(output));
	});
	connect(training_process_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, [this](int exit_code, QProcess::ExitStatus exit_status) {
		train_button_->setEnabled(true);
		download_button_->setEnabled(true);
		audit_button_->setEnabled(true);
		if(exit_status != QProcess::NormalExit || exit_code != 0)
		{
			last_job_finished_ = true;
			last_job_success_ = false;
			last_job_exit_code_ = exit_code;
			active_job_is_training_ = false;
			active_job_is_preparing_ = false;
			active_job_is_audit_ = false;
			active_job_is_evaluation_ = false;
			training_status_->setText(tr("Инструмент завершился с ошибкой (%1). См. журнал диагностики.").arg(exit_code));
			training_progress_->setRange(0, 100);
			training_progress_->setValue(0);
			training_progress_->setFormat(tr("Ошибка"));
			appendDiagnostic(tr("Инструмент Python завершился с ошибкой (код %1). Проверьте зависимости MNE, NumPy, SciPy и scikit-learn.").arg(exit_code));
			return;
		}
		last_job_finished_ = true;
		last_job_success_ = true;
		last_job_exit_code_ = exit_code;
		if(active_job_is_training_)
		{
			const QString stem = training_task_->currentData().toString() == QStringLiteral("hands") ? QStringLiteral("S001_left_vs_right_fist") : QStringLiteral("S001_feet_vs_rest");
			QDir root(QFileInfo(training_script_path_).absolutePath());
			root.cdUp();
			const QString model_path = root.filePath(QStringLiteral("models/%1.json").arg(stem));
			const QString metrics_path = root.filePath(QStringLiteral("datasets/processed/%1_metrics.json").arg(stem));
			if(readJsonObjectFile(model_path).isEmpty() || readJsonObjectFile(metrics_path).isEmpty())
			{
				last_job_success_ = false;
				last_error_ = tr("Скрипт завершился без читаемой модели или отчёта метрик.");
				active_job_is_training_ = false;
				training_status_->setText(last_error_);
				training_progress_->setRange(0, 100);
				training_progress_->setValue(0);
				training_progress_->setFormat(tr("Ошибка результата"));
				appendDiagnostic(last_error_);
				return;
			}
			loadModelPath(model_path);
			if(QFileInfo(loaded_model_path_).absoluteFilePath() != QFileInfo(model_path).absoluteFilePath())
			{
				last_job_success_ = false;
				last_error_ = tr("Обученная модель не прошла проверку загрузки.");
				active_job_is_training_ = false;
				training_status_->setText(last_error_);
				training_progress_->setRange(0, 100);
				training_progress_->setValue(0);
				training_progress_->setFormat(tr("Ошибка модели"));
				appendDiagnostic(last_error_);
				return;
			}
			refreshTrainingReport(metrics_path);
			training_status_->setText(tr("Обучение и независимая проверка завершены. Модель: %1").arg(QFileInfo(model_path).fileName()));
			appendDiagnostic(tr("Обучение завершено; качество рассчитано на отдельной записи."));
		}
		else if(active_job_is_preparing_)
		{
			training_status_->setText(tr("EDF подготовлен. Загружаются графики и события…"));
			loadRecordingPath(prepared_metadata_path_);
		}
		else if(active_job_is_audit_)
		{
			refreshDatasetStatus();
			training_status_->setText(dataset_verified_ ? tr("Научные EDF проверены через MNE и PhysioNet SHA-256.") : dataset_status_text_);
		}
		else if(active_job_is_evaluation_)
		{
			const QJsonObject report = readJsonObjectFile(last_evaluation_report_path_);
			if(report.value(QStringLiteral("format")).toString() != QStringLiteral("metasiberia-neurocontrol-evaluation-v1") ||
				report.value(QStringLiteral("model_id")).toString() != evaluation_model_id_ ||
				report.value(QStringLiteral("recording_id")).toString() != evaluation_recording_id_)
			{
				last_job_success_ = false;
				last_error_ = tr("Оценщик завершился, но отчёт не прошёл проверку модели и записи.");
				training_status_->setText(last_error_);
				training_progress_->setFormat(tr("Ошибка отчёта"));
				appendDiagnostic(last_error_);
			}
			else
			{
				last_evaluation_report_ = report;
				training_status_->setText(tr("Оценка завершена: %1 на %2 (%3 окон). Отчёт сохранён отдельно.")
					.arg(evaluation_model_id_, evaluation_recording_id_).arg(report.value(QStringLiteral("window_count")).toInt()));
				appendDiagnostic(tr("Оценка выбранной модели по размеченной записи завершена; исходные модель и метрики не изменены."));
			}
		}
		else
		{
			refreshDatasetStatus();
			training_status_->setText(dataset_verified_ ? tr("Загрузка и проверка набора завершены. Можно запускать обучение.") : dataset_status_text_);
			appendDiagnostic(tr("Записи PhysioNet загружены и проверены по SHA-256."));
		}
		active_job_is_training_ = false;
		active_job_is_preparing_ = false;
		active_job_is_audit_ = false;
		active_job_is_evaluation_ = false;
		training_progress_->setRange(0, 100);
		training_progress_->setValue(100);
		training_progress_->setFormat(last_job_success_ ? tr("Готово") : tr("Ошибка результата"));
	});
	connect(training_process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
		if(error == QProcess::FailedToStart)
		{
			train_button_->setEnabled(true);
			download_button_->setEnabled(true);
			audit_button_->setEnabled(true);
			training_status_->setText(tr("Не удалось запустить Python. Выберите python.exe с установленными MNE, NumPy, SciPy и scikit-learn."));
			active_job_is_training_ = false;
			active_job_is_preparing_ = false;
			active_job_is_audit_ = false;
			active_job_is_evaluation_ = false;
			last_job_finished_ = true;
			last_job_success_ = false;
			last_job_exit_code_ = -1;
			training_progress_->setRange(0, 100);
			training_progress_->setValue(0);
			training_progress_->setFormat(tr("Ошибка запуска"));
			appendDiagnostic(tr("Не удалось запустить Python: %1").arg(training_process_->errorString()));
		}
	});
	appendDiagnostic(tr("NeuroControl готов. Синтетический сигнал, воспроизведение EDF, классификация и тестовые команды обозначаются раздельно."));
	timer_ = new QTimer(this);
	timer_->setTimerType(Qt::PreciseTimer);
	timer_->setInterval(40);
	connect(load_recording, &QPushButton::clicked, this, [this] { loadRecording(); });
	connect(load_model, &QPushButton::clicked, this, [this] { loadModel(); });
	connect(save_model_button_, &QPushButton::clicked, this, [this] { saveModelCopy(); });
	connect(load_trained_model, &QPushButton::clicked, this, [this] { loadModel(); });
	connect(choose_python, &QPushButton::clicked, this, [this] {
		const QString path = QFileDialog::getOpenFileName(this, tr("Выберите интерпретатор Python"), python_path_, tr("Приложения Python (python.exe);;Все файлы (*)"));
		if(path.isEmpty()) return;
		python_path_ = path;
		QSettings().setValue(QStringLiteral("neurocontrol/pythonPath"), python_path_);
		training_status_->setText(tr("Python: %1").arg(QFileInfo(path).fileName()));
	});
	connect(choose_trainer, &QPushButton::clicked, this, [this] {
		const QString path = QFileDialog::getOpenFileName(this, tr("Выберите prepare_train.py"), training_script_path_, tr("Скрипт обучения (prepare_train.py);;Скрипты Python (*.py)"));
		if(!path.isEmpty()) training_script_path_ = path;
	});
	connect(choose_downloader, &QPushButton::clicked, this, [this] {
		const QString path = QFileDialog::getOpenFileName(this, tr("Выберите download_physionet.py"), download_script_path_, tr("Загрузчик данных (download_physionet.py);;Скрипты Python (*.py)"));
		if(!path.isEmpty()) download_script_path_ = path;
	});
	connect(download_button_, &QPushButton::clicked, this, [this] {
		const bool hands = training_task_->currentData().toString() == QStringLiteral("hands");
		active_job_is_preparing_ = false;
		runPythonScript(download_script_path_, {QStringLiteral("--runs"), hands ? QStringLiteral("4") : QStringLiteral("6"), hands ? QStringLiteral("8") : QStringLiteral("10"), hands ? QStringLiteral("12") : QStringLiteral("14")}, false);
	});
	connect(audit_button_, &QPushButton::clicked, this, [this] {
		const bool hands = training_task_->currentData().toString() == QStringLiteral("hands");
		active_job_is_audit_ = true;
		runPythonScript(audit_script_path_, {QStringLiteral("--runs"), hands ? QStringLiteral("4") : QStringLiteral("6"), hands ? QStringLiteral("8") : QStringLiteral("10"), hands ? QStringLiteral("12") : QStringLiteral("14")}, false);
		if(training_process_->state() == QProcess::NotRunning) active_job_is_audit_ = false;
	});
	connect(training_task_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { refreshDatasetStatus(); });
	connect(train_button_, &QPushButton::clicked, this, [this] {
		refreshDatasetStatus();
		if(!dataset_verified_) { appendDiagnostic(dataset_status_text_); return; }
		if(low_cutoff_->value() >= high_cutoff_->value()) { appendDiagnostic(tr("Верхняя частота полосового фильтра должна быть выше нижней.")); return; }
		QStringList channels;
		for(int i = 0; i < training_channels_->count(); ++i)
			if(training_channels_->item(i)->checkState() == Qt::Checked) channels.push_back(training_channels_->item(i)->text());
		if(channels.size() < 2) { appendDiagnostic(tr("Для CSP + LDA выберите не менее двух ЭЭГ-каналов.")); return; }
		QStringList arguments = {QStringLiteral("--task"), training_task_->currentData().toString(),
			QStringLiteral("--low-hz"), QLocale::c().toString(low_cutoff_->value(), 'f', 1),
			QStringLiteral("--high-hz"), QLocale::c().toString(high_cutoff_->value(), 'f', 1),
			QStringLiteral("--window-seconds"), QLocale::c().toString(epoch_seconds_->value(), 'f', 1),
			QStringLiteral("--step-seconds"), QLocale::c().toString(epoch_step_seconds_->value(), 'f', 1),
			QStringLiteral("--offset-seconds"), QLocale::c().toString(event_offset_seconds_->value(), 'f', 1),
			QStringLiteral("--threshold"), QLocale::c().toString(confidence_threshold_->value(), 'f', 2),
			QStringLiteral("--channels"), channels.join(QLatin1Char(','))};
		if(notch_filter_->isChecked()) arguments.push_back(QStringLiteral("--notch-50"));
		runPythonScript(training_script_path_, arguments, true);
	});
	connect(start_button_, &QPushButton::clicked, this, [this] { if(paused_) resumePlayback(); else start(); });
	connect(pause_button_, &QPushButton::clicked, this, [this] { if(paused_) resumePlayback(); else pausePlayback(); });
	connect(stop_button, &QPushButton::clicked, this, [this] { stop(); });
	connect(emergency_stop, &QPushButton::clicked, this, [this] { stop(); appendDiagnostic(tr("Аварийная остановка: разрешение управления снято.")); });
	connect(avatar_emergency_stop, &QPushButton::clicked, this, [this] { stop(); appendDiagnostic(tr("Аварийная остановка: разрешение управления снято.")); });
	connect(timer_, &QTimer::timeout, this, [this] { tick(); });
	connect(left, &QPushButton::clicked, this, [this] { sendTestTurn(true); });
	connect(right, &QPushButton::clicked, this, [this] { sendTestTurn(false); });
	connect(test_forward_button, &QPushButton::clicked, this, [this] {
		neurocontrol::Command command;
		command.type = neurocontrol::CommandType::Forward;
		command.confidence = 1.0;
		command.timestamp_seconds = Clock::getTimeSinceInit();
		command.minimum_confidence = applied_threshold_;
		command.event_id = ++next_command_event_id_;
		command.quality = neurocontrol::SignalQuality::Good;
		command.source = neurocontrol::CommandSource::SimulatedTest;
		client_->setNeuroControlEnabled(true);
		client_->setNeuroControlCommand(command);
		appendExperimentEvent(QJsonObject{{QStringLiteral("event_type"), QStringLiteral("command_submitted")},
			{QStringLiteral("command"), QStringLiteral("FORWARD")}, {QStringLiteral("command_source"), QStringLiteral("simulated_test")},
			{QStringLiteral("command_event_id"), static_cast<qint64>(command.event_id)},
			{QStringLiteral("submission_result"), client_->neuro_control_command.event_id == command.event_id ? QStringLiteral("submitted") : QStringLiteral("rejected")}});
		avatar_status_label_->setText(tr("ИДЁТ ВПЕРЁД · ТЕСТ"));
		appendDiagnostic(tr("Симуляция команды движения вперёд."));
		const int generation = ++test_generation_;
		QTimer::singleShot(700, this, [this, generation] { if(generation == test_generation_) { publishStop(); if(!control_enabled_->isChecked()) client_->setNeuroControlEnabled(false); } });
	});
	connect(test_stop, &QPushButton::clicked, this, [this] { ++test_generation_; publishStop(); if(!control_enabled_->isChecked()) client_->setNeuroControlEnabled(false); appendDiagnostic(tr("Тестовая команда: стоп.")); });
	connect(source_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, lsl_group](int index) {
		stop();
		lsl_group->setVisible(index == 3 || index == 4);
		const bool synthetic = index == 1;
		source_status_label_->setText(synthetic ? tr("Источник: BrainFlow Synthetic Board · готов к запуску") : index == 2 ? tr("Источник: физическое устройство пока не подключено") : index == 3 ? tr("Источник: LSL · не подключён") : index == 4 ? tr("Источник: сохранённая запись LSL · воспроизведение, не живой поток") : tr("Источник: воспроизведение EDF/F32"));
		if(synthetic)
			updateChannels({}); // Wait for the actual BrainFlow channel metadata.
		else if(index == 0 && !recording_channels_.empty()) updateChannels(recording_channels_);
		else if(index == 4 && !lsl_recording_playback_samples_.empty())
		{
			std::vector<std::string> names, types, units;
			const QJsonArray channels = lsl_loaded_recording_metadata_.value(QStringLiteral("channels")).toArray();
			for(const QJsonValue& value : channels)
			{
				const QJsonObject channel = value.toObject();
				names.push_back(channel.value(QStringLiteral("name")).toString().toUtf8().toStdString());
				types.push_back(channel.value(QStringLiteral("display_type")).toString().toUtf8().toStdString());
				units.push_back(channel.value(QStringLiteral("unit")).toString().toUtf8().toStdString());
			}
			updateChannels(names, types, units);
		}
		else updateChannels({});
		const bool is_recorded = index == 0 || index == 4;
		seek_slider_->setEnabled(is_recorded && channel_count_ > 0);
		playback_speed_->setEnabled(index == 0);
		refreshControlAvailability();
	});
	connect(lsl_library_button, &QPushButton::clicked, this, [this] {
		const QString path = QFileDialog::getOpenFileName(this, tr("Выберите установленную библиотеку LSL"), lsl_library_path_, tr("Библиотека LSL (lsl.dll);;Библиотеки (*.dll)"));
		if(path.isEmpty()) return;
		stop();
		lsl_library_path_ = path;
		QSettings().setValue(QStringLiteral("neurocontrol/lslLibraryPath"), path);
		lsl_library_state_ = QStringLiteral("not_checked");
		lsl_library_error_.clear();
		lsl_details_label_->setText(tr("Библиотека: %1").arg(QFileInfo(path).fileName()));
	});
	connect(lsl_scan_button_, &QPushButton::clicked, this, [this] { scanLsl(); });
	connect(lsl_connect_button_, &QPushButton::clicked, this, [this] { connectLsl(); });
	connect(lsl_disconnect_button_, &QPushButton::clicked, this, [this] { disconnectLsl(); });
	connect(lsl_shadow_contract_confirm_checkbox_, &QCheckBox::toggled, this, [this](bool) { refreshLslShadowCompatibility(); });
	connect(lsl_shadow_checkbox_, &QCheckBox::toggled, this, [this](bool enabled) {
		if(!enabled)
		{
			resetLslShadowPipeline(QString(), true);
			lsl_shadow_windows_ = 0;
			lsl_shadow_timestamp_gaps_ = 0;
			lsl_shadow_stall_events_ = 0;
			if(lsl_shadow_status_label_)
				lsl_shadow_status_label_->setText(lsl_shadow_compatible_ ? tr("Совместимость подтверждена; теневая классификация выключена.") : lsl_shadow_compatibility_reason_);
			return;
		}
		if(!lsl_shadow_compatible_ || !lsl_shadow_model_)
		{
			lsl_shadow_checkbox_->setChecked(false);
			return;
		}
		resetLslShadowPipeline(QString(), true);
		if(lsl_shadow_status_label_)
			lsl_shadow_status_label_->setText(tr("Теневая классификация включена: анализ идёт локально, команды аватару не отправляются."));
		appendDiagnostic(tr("LSL: теневой классификатор включён; совместимость модели/каналов/частоты подтверждена, команды аватару не отправляются."));
	});
	connect(lsl_record_button_, &QPushButton::clicked, this, [this] { startLslRecording(); });
	connect(lsl_record_stop_button_, &QPushButton::clicked, this, [this] { requestStopLslRecording(); });
	connect(lsl_record_load_button_, &QPushButton::clicked, this, [this] {
		const QString path = QFileDialog::getOpenFileName(this, tr("Открыть запись LSL"), lsl_recordings_directory_, tr("LSL recording metadata (*.json)"));
		if(path.isEmpty()) return;
		QFileInfo selected(path);
		if(QFileInfo(lsl_recordings_directory_).canonicalFilePath().isEmpty() ||
			!pathIsWithin(lsl_recordings_directory_, selected.canonicalFilePath()))
		{
			appendDiagnostic(tr("LSL-запись должна находиться в локальном каталоге NeuroControl."));
			return;
		}
		loadLslRecording(selected.completeBaseName());
	});
	connect(lsl_record_play_button_, &QPushButton::clicked, this, [this] { startLslRecordingPlayback(); });
	connect(lsl_streams_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
		if(index < 0 || index >= lsl_discovered_.size()) return;
		const auto& d = lsl_discovered_.at(index);
		lsl_details_label_->setText(tr("%1 · %2 каналов · %3 Гц номинально · %4 · %5")
			.arg(d.name).arg(d.channel_count).arg(d.nominal_hz, 0, 'g', 8).arg(d.format, d.hostname));
	});
	lsl_poll_timer_ = new QTimer(this);
	lsl_poll_timer_->setInterval(50);
	connect(lsl_poll_timer_, &QTimer::timeout, this, [this] { pollLsl(); });
	lsl_poll_timer_->start();
	connect(control_enabled_, &QCheckBox::toggled, this, [this](bool enabled) {
		if(avatar_control_enabled_->isChecked() != enabled) avatar_control_enabled_->setChecked(enabled);
		client_->setNeuroControlEnabled(enabled);
		appendExperimentEvent(QJsonObject{{QStringLiteral("event_type"), QStringLiteral("control_permission_changed")},
			{QStringLiteral("enabled"), enabled}});
		avatar_status_label_->setText(enabled ? tr("ОЖИДАНИЕ КОМАНДЫ · РАСПОЗНАВАНИЕ") : tr("УПРАВЛЕНИЕ ОТКЛЮЧЕНО"));
		if(!enabled) publishStop();
		refreshDiagnosticsSummary();
	});
	connect(avatar_control_enabled_, &QCheckBox::toggled, this, [this](bool enabled) {
		if(control_enabled_->isChecked() != enabled) control_enabled_->setChecked(enabled);
	});
	connect(time_window_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] {
		const int seconds = time_window_->currentData().toInt();
		waveform_->setWindowSeconds(seconds);
		overview_waveform_->setWindowSeconds(std::min(5, seconds));
	});
	connect(playback_speed_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] {
		if(!playing_ || paused_ || source_->currentIndex() != 0) return;
		appendExperimentEvent(QJsonObject{{QStringLiteral("event_type"), QStringLiteral("playback_speed_changed")},
			{QStringLiteral("playback_speed"), playback_speed_->currentData().toDouble()}});
		playback_start_sample_ = next_sample_;
		elapsed_.restart();
		playback_epoch_monotonic_seconds_ = Clock::getTimeSinceInit();
		neuro_stream_generation_ = client_->resetNeuroControlStream();
		if(classifier_) classifier_->reset();
		if(control_enabled_->isChecked()) publishStop();
	});
	connect(vertical_scale_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), waveform_, &WaveformView::setVerticalScale);
	connect(plot_auto_scale_, &QCheckBox::toggled, waveform_, &WaveformView::setAutoScale);
	connect(plot_paused_, &QCheckBox::toggled, waveform_, &WaveformView::setPaused);
	connect(show_all, &QPushButton::clicked, this, [this] {
		for(int i = 0; i < channel_list_->count(); ++i) channel_list_->item(i)->setCheckState(Qt::Checked);
	});
	connect(hide_all, &QPushButton::clicked, this, [this] {
		for(int i = 0; i < channel_list_->count(); ++i) channel_list_->item(i)->setCheckState(Qt::Unchecked);
	});
	connect(show_detail, &QPushButton::clicked, this, [this] {
		const int selected = spectrum_channel_->currentIndex();
		for(int i = 0; i < channel_list_->count(); ++i)
			channel_list_->item(i)->setCheckState(i == selected ? Qt::Checked : Qt::Unchecked);
	});
	connect(reset_plot, &QPushButton::clicked, this, [this] {
		plot_auto_scale_->setChecked(true);
		vertical_scale_->setValue(100.0);
		time_window_->setCurrentIndex(1);
	});
	connect(channel_list_, &QListWidget::itemChanged, this, [this] {
		std::vector<bool> visible(active_channels_.size(), false);
		for(int i = 0; i < channel_list_->count() && i < static_cast<int>(visible.size()); ++i)
			visible[static_cast<std::size_t>(i)] = channel_list_->item(i)->checkState() == Qt::Checked;
		waveform_->setVisible(visible);
	});
	connect(spectrum_channel_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { refreshSpectrum(); });
	connect(seek_slider_, &QSlider::valueChanged, this, [this](int value) {
		if(seek_slider_->isSliderDown() && !updating_seek_slider_) seekPlayback(value);
	});
	connect(clear_log, &QPushButton::clicked, this, [this] { diagnostic_entries_.clear(); last_diagnostic_.clear(); last_diagnostic_category_.clear(); diagnostics_->clear(); refreshOverviewStatus(); });
	connect(copy_log, &QPushButton::clicked, this, [this] { QApplication::clipboard()->setText(diagnostics_->toPlainText()); });
	connect(save_log, &QPushButton::clicked, this, [this] {
		const QString path = QFileDialog::getSaveFileName(this, tr("Сохранить журнал NeuroControl"), QStringLiteral("neurocontrol-log.txt"), tr("Текстовые файлы (*.txt)"));
		if(path.isEmpty()) return;
		QSaveFile file(path);
		if(!file.open(QIODevice::WriteOnly | QIODevice::Text)) { appendDiagnostic(tr("Не удалось сохранить журнал.")); return; }
		const QString report = tr("NeuroControl · диагностический отчёт · %1\n%2\n\n%3\n")
			.arg(QDateTime::currentDateTime().toString(Qt::ISODate), diagnostics_summary_->text(), diagnostic_entries_.join(QLatin1Char('\n')));
		file.write(report.toUtf8());
		if(!file.commit()) appendDiagnostic(tr("Не удалось завершить сохранение журнала."));
	});
	connect(diagnostic_filter_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { refreshDiagnosticView(); });
	connect(diagnostic_category_filter_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { refreshDiagnosticView(); });
	connect(source_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] {
		const bool recorded = source_->currentIndex() == 0;
		const bool lsl_recorded = source_->currentIndex() == 4 && !lsl_recording_source_timestamps_.empty();
		seek_slider_->setEnabled((recorded && !recorded_samples_.empty()) || lsl_recorded);
		playback_speed_->setEnabled(recorded);
	});
	connect(time_window_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] {
		refreshSpectrum();
	});
	refreshDatasetStatus();
	refreshControlAvailability();
	refreshOverviewStatus();
}

NeuroControlWidget::~NeuroControlWidget() { stop(); }

void NeuroControlWidget::configureLslLibrary(bool discover_after_configuration)
{
	if(lsl_library_configuration_)
	{
		lsl_discovery_after_configuration_ = lsl_discovery_after_configuration_ || discover_after_configuration;
		return;
	}
	if(lsl_reader_ || lsl_connection_state_ == QLatin1String("connected") || lsl_connection_state_ == QLatin1String("connecting"))
	{
		lsl_library_state_ = QStringLiteral("busy");
		lsl_library_error_ = QStringLiteral("Отключите текущий LSL-поток перед перенастройкой библиотеки.");
		return;
	}
	lsl_discovery_after_configuration_ = discover_after_configuration;
	lsl_library_state_ = QStringLiteral("searching");
	lsl_library_error_.clear();
	if(lsl_details_label_) lsl_details_label_->setText(tr("Поиск официального пакета LabRecorder и проверка lsl.dll…"));
	lsl_library_configuration_ = std::make_shared<NeuroControlLslConfiguration>();
	const auto state = lsl_library_configuration_;
	const QString application_directory = QCoreApplication::applicationDirPath();
	const QString saved_path = lsl_library_path_;
	auto* thread = QThread::create([state, application_directory, saved_path] {
		QString path, provenance, sha256, error;
		const bool ready = neurocontrol::LslEegSource::discoverInstalledLabRecorderLibrary(
			application_directory, saved_path, path, provenance, sha256, error);
		std::lock_guard<std::mutex> lock(state->mutex);
		state->path = path;
		state->provenance = provenance;
		state->sha256 = sha256;
		state->error = error;
		state->ready = ready;
		state->complete = true;
	});
	connect(thread, &QThread::finished, thread, &QObject::deleteLater);
	thread->start();
}

void NeuroControlWidget::scanLsl()
{
	if(lsl_library_state_ != QLatin1String("ready"))
	{
		configureLslLibrary(true);
		return;
	}
	if(lsl_discovery_) return;
	lsl_discovered_.clear();
	lsl_streams_->clear();
	lsl_discovery_state_ = QStringLiteral("searching");
	lsl_discovery_message_.clear();
	lsl_connect_button_->setEnabled(false);
	lsl_scan_button_->setEnabled(false);
	lsl_details_label_->setText(tr("Поиск доступных LSL-потоков…"));
	lsl_discovery_ = std::make_shared<NeuroControlLslDiscovery>();
	const auto state = lsl_discovery_;
	const QString library = lsl_library_path_;
	auto* thread = QThread::create([state, library] {
		QVector<neurocontrol::LslStreamDescriptor> streams;
		QString error;
		neurocontrol::LslEegSource::discover(library, streams, error);
		std::lock_guard<std::mutex> lock(state->mutex);
		state->streams = std::move(streams);
		state->error = error;
		state->complete = true;
	});
	connect(thread, &QThread::finished, thread, &QObject::deleteLater);
	thread->start();
}

void NeuroControlWidget::connectLsl()
{
	if(lsl_reader_)
	{
		bool finished = false;
		{ std::lock_guard<std::mutex> lock(lsl_reader_->mutex); finished = lsl_reader_->finished; }
		if(!finished) { appendDiagnostic(tr("LSL: дождитесь завершения отключения текущего потока.")); return; }
		lsl_reader_.reset();
	}
	if(lsl_library_state_ != QLatin1String("ready"))
	{
		appendDiagnostic(tr("LSL: DLL ещё не настроена; вызовите neuro.configure_lsl."));
		return;
	}
	const int index = lsl_streams_ ? lsl_streams_->currentIndex() : -1;
	if(index < 0 || index >= lsl_discovered_.size()) { appendDiagnostic(tr("Сначала найдите и выберите LSL-поток.")); return; }
	if(source_->currentIndex() != 3) source_->setCurrentIndex(3);
	stop();
	if(lsl_shadow_checkbox_ && lsl_shadow_checkbox_->isChecked()) lsl_shadow_checkbox_->setChecked(false);
	if(lsl_shadow_contract_confirm_checkbox_ && lsl_shadow_contract_confirm_checkbox_->isChecked())
	{
		QSignalBlocker blocker(lsl_shadow_contract_confirm_checkbox_);
		lsl_shadow_contract_confirm_checkbox_->setChecked(false);
	}
	lsl_error_.clear();
	lsl_timestamp_diagnostics_ = {};
	lsl_recent_timestamps_.clear();
	lsl_pause_events_ = lsl_reconnects_ = lsl_errors_ = 0;
	received_samples_ = dropped_samples_ = rate_sample_snapshot_ = 0;
	receive_rate_hz_ = 0.0;
	rate_measured_ = false;
	last_rate_update_ms_ = QDateTime::currentMSecsSinceEpoch();
	lsl_rate_clock_.start();
	lsl_connection_state_ = QStringLiteral("connecting");
	lsl_active_stream_ = lsl_discovered_.at(index);
	lsl_active_units_override_ = lsl_units_override_->currentData().toString();
	lsl_units_override_->setEnabled(false);
	lsl_shadow_rate_history_.clear();
	lsl_shadow_windows_ = 0;
	lsl_shadow_timestamp_gaps_ = 0;
	lsl_shadow_stall_events_ = 0;
	lsl_shadow_pipeline_.reset();
	resetLslShadowPipeline(QString(), true);
	lsl_reader_ = std::make_shared<NeuroControlLslReader>();
	const auto state = lsl_reader_;
	const QString library = lsl_library_path_;
	const auto selected = lsl_active_stream_;
	const QString override_units = lsl_units_override_->currentData().toString() == QStringLiteral("auto") ? QString() : lsl_units_override_->currentData().toString();
	auto* thread = QThread::create([state, library, selected, override_units] {
		auto finalizeRecording = [state](const QString& reason) {
			std::lock_guard<std::mutex> lock(state->mutex);
			if(!state->recording_writer) return;
			neurocontrol::LslRecordingWriter& writer = *state->recording_writer;
			QString error_text = state->recording_error;
			std::string file_error;
			bool file_complete = false;
			if(error_text.isEmpty())
				file_complete = writer.finalize(std::filesystem::path(state->recording_final_path.toStdWString()), file_error);
			if(!file_complete)
			{
				writer.leaveIncomplete();
				if(error_text.isEmpty()) error_text = QString::fromStdString(file_error);
			}
			QJsonObject metadata = state->recording_metadata;
			metadata.insert(QStringLiteral("status"), file_complete ? QStringLiteral("complete") : QStringLiteral("incomplete"));
			metadata.insert(QStringLiteral("complete"), file_complete);
			metadata.insert(QStringLiteral("data_file"), QFileInfo(file_complete ? state->recording_final_path : state->recording_partial_path).fileName());
			metadata.insert(QStringLiteral("sample_count"), static_cast<qint64>(writer.sampleCount()));
			metadata.insert(QStringLiteral("binary_checksum_fnv1a64"), QString::number(writer.checksum(), 16).rightJustified(16, QLatin1Char('0')));
			metadata.insert(QStringLiteral("ended_at_utc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
			metadata.insert(QStringLiteral("completion_reason"), reason);
			metadata.insert(QStringLiteral("recording_error"), error_text);
			metadata.insert(QStringLiteral("receiver_counters_at_end"), QJsonObject{
				{QStringLiteral("pauses"), static_cast<qint64>(state->pause_events - state->recording_start_pauses)},
				{QStringLiteral("reconnects"), static_cast<qint64>(state->reconnects - state->recording_start_reconnects)},
				{QStringLiteral("errors"), static_cast<qint64>(state->errors - state->recording_start_errors)},
				{QStringLiteral("local_display_buffer_drops"), static_cast<qint64>(state->buffer_overruns - state->recording_start_display_drops)}});
			const neurocontrol::LslTimestampDiagnostics& td = state->timestamps;
			metadata.insert(QStringLiteral("timestamp_diagnostics_at_end"), QJsonObject{
				{QStringLiteral("received"), static_cast<qint64>(td.received)}, {QStringLiteral("repeated"), static_cast<qint64>(td.repeated)},
				{QStringLiteral("backwards"), static_cast<qint64>(td.backwards)}, {QStringLiteral("nonfinite"), static_cast<qint64>(td.nonfinite)},
				{QStringLiteral("coarse_intervals"), static_cast<qint64>(td.coarse_intervals)}, {QStringLiteral("integer_valued"), static_cast<qint64>(td.integer_valued)},
				{QStringLiteral("clock_correction_available"), td.correction_available}, {QStringLiteral("clock_compatible"), td.clock_compatible},
				{QStringLiteral("last_clock_correction_seconds"), td.last_correction_seconds}});
			QString metadata_error;
			if(!writeJsonAtomically(state->recording_metadata_path, metadata, metadata_error))
			{
				if(error_text.isEmpty()) error_text = QStringLiteral("Metadata write failed: %1").arg(metadata_error);
			}
			state->recording_metadata = metadata;
			state->recording_samples = writer.sampleCount();
			state->recording_checksum = writer.checksum();
			state->recording_error = error_text;
			state->recording_file_complete = file_complete;
			state->recording_active = false;
			state->recording_finalized = true;
			state->recording_completion_reason = reason;
			state->recording_writer.reset();
			state->events.push_back(file_complete ? QStringLiteral("LSL: запись сохранена: %1 (%2 отсчётов)").arg(state->recording_id).arg(state->recording_samples) :
				QStringLiteral("LSL: запись оставлена неполной: %1 · %2").arg(state->recording_id, state->recording_error));
		};
		bool ever_connected = false;
		while(!state->stop.load())
		{
			neurocontrol::LslEegSource source(library, selected, override_units);
			std::string error;
			if(!source.start(error))
			{
				std::lock_guard<std::mutex> lock(state->mutex);
				++state->errors;
				const QString message = QString::fromStdString(error);
				if(state->last_reported_error != message) { state->events.push_back(QStringLiteral("LSL: %1").arg(message)); state->last_reported_error = message; }
				if(error.find("Неизвестные единицы") != std::string::npos || error.find("Неподдерживаемый формат") != std::string::npos) break;
			}
			else
			{
				{
					std::lock_guard<std::mutex> lock(state->mutex);
					state->connected = true;
					state->descriptor = source.descriptor();
					state->last_reported_error.clear();
					if(ever_connected) ++state->reconnects;
					state->events.push_back(ever_connected ? QStringLiteral("LSL: переподключение выполнено") : QStringLiteral("LSL: поток подключён"));
				}
				ever_connected = true;
				auto last_data = std::chrono::steady_clock::now();
				bool silent = false;
				while(!state->stop.load())
				{
					error.clear();
					auto frames = source.readChunk(error);
					if(!error.empty())
					{
						std::lock_guard<std::mutex> lock(state->mutex);
						++state->errors;
						state->events.push_back(QStringLiteral("LSL: ошибка чтения: %1").arg(QString::fromStdString(error)));
						break;
					}
					if(!frames.empty())
					{
						last_data = std::chrono::steady_clock::now();
						std::lock_guard<std::mutex> lock(state->mutex);
						if(silent) state->events.push_back(QStringLiteral("LSL: поступление отсчётов возобновилось"));
						silent = false;
						state->timestamps = source.diagnostics();
						if(state->recording_writer && state->recording_active)
						{
							std::string record_error;
						if(!state->recording_writer->appendChunk(frames, record_error))
							{ state->recording_error = QString::fromStdString(record_error); state->recording_stop_requested = true; }
						}
						for(auto& frame : frames)
						{
							if(state->frames.size() >= 10000) { state->frames.pop_front(); ++state->buffer_overruns; }
							state->frames.push_back(std::move(frame));
						}
					}
					else
					{
						const double idle = std::chrono::duration<double>(std::chrono::steady_clock::now() - last_data).count();
						if(!silent && idle >= 1.25)
						{
							std::lock_guard<std::mutex> lock(state->mutex);
							++state->pause_events;
							state->events.push_back(QStringLiteral("LSL: пауза поступления более 1,25 с"));
							silent = true;
						}
						if(idle >= 5.0) break;
					}
					bool finalize_requested = false;
					{ std::lock_guard<std::mutex> lock(state->mutex); finalize_requested = state->recording_stop_requested; state->recording_stop_requested = false; }
					if(finalize_requested) finalizeRecording(QStringLiteral("requested"));
				}
				source.stop();
				{
					std::lock_guard<std::mutex> lock(state->mutex);
					state->connected = false;
					if(!state->stop.load()) state->events.push_back(QStringLiteral("LSL: соединение потеряно, выполняется поиск источника"));
				}
			}
			for(int i = 0; i < 5 && !state->stop.load(); ++i) QThread::msleep(100);
		}
		finalizeRecording(QStringLiteral("disconnected_or_reader_stopped"));
		std::lock_guard<std::mutex> lock(state->mutex);
		state->connected = false;
		state->finished = true;
	});
	connect(thread, &QThread::finished, thread, &QObject::deleteLater);
	thread->start();
	lsl_connect_button_->setEnabled(false);
	lsl_disconnect_button_->setEnabled(true);
	lsl_details_label_->setText(tr("Подключение к %1…").arg(selected.name));
	appendDiagnostic(tr("LSL: подключение запрошено; классификация и нейроуправление для этого источника отключены."));
}

void NeuroControlWidget::startLslDiagnostic(int duration_seconds)
{
	lsl_diagnostic_duration_seconds_ = std::clamp(duration_seconds, 1, 300);
	lsl_diagnostic_start_received_ = received_samples_;
	lsl_diagnostic_start_drops_ = dropped_samples_;
	lsl_diagnostic_start_pauses_ = lsl_pause_events_;
	lsl_diagnostic_start_reconnects_ = lsl_reconnects_;
	lsl_diagnostic_start_errors_ = lsl_errors_;
	lsl_diagnostic_start_timestamps_ = lsl_timestamp_diagnostics_;
	lsl_diagnostic_started_at_utc_ = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
	lsl_diagnostic_completed_at_utc_.clear();
	lsl_diagnostic_report_path_.clear();
	lsl_diagnostic_report_markdown_.clear();
	lsl_diagnostic_elapsed_.start();
	lsl_diagnostic_running_ = true;
	lsl_diagnostic_state_ = QStringLiteral("running");
	lsl_diagnostic_result_ = QJsonObject{{"state", "running"}, {"requested_duration_seconds", lsl_diagnostic_duration_seconds_},
		{"started_at_utc", lsl_diagnostic_started_at_utc_}, {"auto_disconnect", true}};
	appendDiagnostic(tr("LSL: начат диагностический интервал %1 с; сырые отсчёты не сохраняются.").arg(lsl_diagnostic_duration_seconds_));
}

void NeuroControlWidget::finishLslDiagnostic(const QString& completion_reason)
{
	if(lsl_shadow_checkbox_ && lsl_shadow_checkbox_->isChecked()) lsl_shadow_checkbox_->setChecked(false);
	if(!lsl_diagnostic_running_) return;
	const double elapsed_seconds = lsl_diagnostic_elapsed_.isValid() ? lsl_diagnostic_elapsed_.elapsed() / 1000.0 : 0.0;
	const std::uint64_t received = received_samples_ - lsl_diagnostic_start_received_;
	const double average_rate = elapsed_seconds > 0.0 ? static_cast<double>(received) / elapsed_seconds : 0.0;
	const bool completed = completion_reason == QLatin1String("completed");
	const QString final_state = completed ? QStringLiteral("completed") : QStringLiteral("interrupted");
	lsl_diagnostic_completed_at_utc_ = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
	QStringList channel_names, channel_units;
	for(int i = 0; i < lsl_active_stream_.channel_count; ++i)
	{
		channel_names.append(lsl_active_stream_.channels.value(i, QStringLiteral("channel_%1").arg(i + 1)));
		channel_units.append(lsl_active_stream_.units.value(i));
	}
	const auto& timestamps = lsl_timestamp_diagnostics_;
	const QJsonObject summary{
		{"state", final_state}, {"completion_reason", completion_reason}, {"requested_duration_seconds", lsl_diagnostic_duration_seconds_},
		{"elapsed_seconds", elapsed_seconds}, {"started_at_utc", lsl_diagnostic_started_at_utc_},
		{"completed_at_utc", lsl_diagnostic_completed_at_utc_}, {"stream_name", lsl_active_stream_.name},
		{"stream_uid", lsl_active_stream_.uid}, {"source_id", lsl_active_stream_.source_id},
		{"nominal_rate_hz", lsl_active_stream_.nominal_hz}, {"measured_average_receive_rate_hz", average_rate},
		{"electrode_montage", lsl_active_stream_.electrode_montage},
		{"reference_scheme", lsl_active_stream_.reference_scheme},
		{"shadow_classification_enabled", lsl_shadow_checkbox_ && lsl_shadow_checkbox_->isChecked()},
		{"shadow_classification_windows", static_cast<qint64>(lsl_shadow_windows_)},
		{"shadow_timestamp_gap_resets", static_cast<qint64>(lsl_shadow_timestamp_gaps_)},
		{"shadow_stall_events", static_cast<qint64>(lsl_shadow_stall_events_)},
		{"shadow_last_processing_batch_ms", lsl_shadow_last_processing_ms_},
		{"channel_count", lsl_active_stream_.channel_count}, {"channels", QJsonArray::fromStringList(channel_names)},
		{"units", QJsonArray::fromStringList(channel_units)}, {"format", lsl_active_stream_.format},
		{"samples_received", static_cast<qint64>(received)},
		{"local_display_buffer_drops", static_cast<qint64>(dropped_samples_ - lsl_diagnostic_start_drops_)},
		{"pause_events", static_cast<qint64>(lsl_pause_events_ - lsl_diagnostic_start_pauses_)},
		{"reconnects", static_cast<qint64>(lsl_reconnects_ - lsl_diagnostic_start_reconnects_)},
		{"errors", static_cast<qint64>(lsl_errors_ - lsl_diagnostic_start_errors_)},
		{"source_timestamp_samples", static_cast<qint64>(timestamps.received - lsl_diagnostic_start_timestamps_.received)},
		{"source_timestamp_repeats", static_cast<qint64>(timestamps.repeated - lsl_diagnostic_start_timestamps_.repeated)},
		{"source_timestamp_backwards", static_cast<qint64>(timestamps.backwards - lsl_diagnostic_start_timestamps_.backwards)},
		{"source_timestamp_nonfinite", static_cast<qint64>(timestamps.nonfinite - lsl_diagnostic_start_timestamps_.nonfinite)},
		{"source_timestamp_coarse_intervals", static_cast<qint64>(timestamps.coarse_intervals - lsl_diagnostic_start_timestamps_.coarse_intervals)},
		{"timestamp_quality", lslStatusJson().value(QStringLiteral("timestamp_quality")).toString()},
		{"last_source_timestamp_seconds", timestamps.last_source_seconds},
		{"last_receiver_arrival_monotonic_seconds", timestamps.last_arrival_seconds},
		{"clock_correction_available", timestamps.correction_available}, {"clock_compatible", timestamps.clock_compatible},
		{"library_path", lsl_library_path_}, {"library_sha256", lsl_library_sha256_},
		{"classifier_connected", false}, {"avatar_control_enabled", false}, {"raw_eeg_contents_saved", false}};
	const QString markdown = QStringLiteral(
		"# NeuroControl LSL diagnostic\n\n"
		"- State: %1 (%2)\n- Requested duration: %3 s\n- Measured duration: %4 s\n"
		"- Started UTC: %5\n- Completed UTC: %6\n- Stream: %7\n- UID: `%8`\n- Source ID: `%9`\n"
		"- Nominal source rate: %10 Hz\n- Measured receive rate: %11 samples/s\n"
		"- Channels: %12\n- Channel names: %13\n- XML units: %14\n- Format: %15\n"
		"- Received samples: %16\n- Local display buffer drops: %17\n- Pauses: %18\n- Reconnects: %19\n- Errors: %20\n"
		"- Timestamp quality: %21\n- Repeated / backwards / nonfinite / coarse intervals: %22 / %23 / %24 / %25\n"
		"- Clock correction available / compatible: %26 / %27\n- LSL library: `%28`\n- Library SHA-256: `%29`\n\n"
		"No raw EEG values were written. A difference between nominal rate and receive rate is descriptive and does not by itself prove dropped source samples.\n")
		.arg(final_state, completion_reason).arg(lsl_diagnostic_duration_seconds_).arg(elapsed_seconds, 0, 'f', 3)
		.arg(lsl_diagnostic_started_at_utc_, lsl_diagnostic_completed_at_utc_, lsl_active_stream_.name,
			lsl_active_stream_.uid, lsl_active_stream_.source_id)
		.arg(lsl_active_stream_.nominal_hz, 0, 'g', 10).arg(average_rate, 0, 'f', 3)
		.arg(lsl_active_stream_.channel_count).arg(channel_names.join(QStringLiteral(", ")))
		.arg(channel_units.join(QStringLiteral(", "))).arg(lsl_active_stream_.format).arg(received)
		.arg(dropped_samples_ - lsl_diagnostic_start_drops_).arg(lsl_pause_events_ - lsl_diagnostic_start_pauses_)
		.arg(lsl_reconnects_ - lsl_diagnostic_start_reconnects_).arg(lsl_errors_ - lsl_diagnostic_start_errors_)
		.arg(summary.value(QStringLiteral("timestamp_quality")).toString())
		.arg(timestamps.repeated - lsl_diagnostic_start_timestamps_.repeated)
		.arg(timestamps.backwards - lsl_diagnostic_start_timestamps_.backwards)
		.arg(timestamps.nonfinite - lsl_diagnostic_start_timestamps_.nonfinite)
		.arg(timestamps.coarse_intervals - lsl_diagnostic_start_timestamps_.coarse_intervals)
		.arg(timestamps.correction_available ? QStringLiteral("yes") : QStringLiteral("no"))
		.arg(timestamps.clock_compatible ? QStringLiteral("yes") : QStringLiteral("no"))
		.arg(lsl_library_path_, lsl_library_sha256_);
	lsl_diagnostic_report_markdown_ = markdown;
	const QString app_data = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
	if(!app_data.isEmpty())
	{
		QDir report_directory(app_data);
		if(report_directory.mkpath(QStringLiteral("neurocontrol/lsl-diagnostics")))
		{
			report_directory.cd(QStringLiteral("neurocontrol/lsl-diagnostics"));
			const QString file_name = QStringLiteral("lsl-diagnostic-%1.md").arg(QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz")));
			QSaveFile file(report_directory.filePath(file_name));
			if(file.open(QIODevice::WriteOnly | QIODevice::Text) && file.write(markdown.toUtf8()) == markdown.toUtf8().size() && file.commit())
				lsl_diagnostic_report_path_ = file.fileName();
		}
	}
	lsl_diagnostic_running_ = false;
	lsl_diagnostic_state_ = final_state;
	lsl_diagnostic_result_ = summary;
	lsl_diagnostic_result_.insert(QStringLiteral("report_path"), lsl_diagnostic_report_path_);
	lsl_diagnostic_result_.insert(QStringLiteral("report_markdown"), markdown);
	lsl_diagnostic_result_.insert(QStringLiteral("report_saved"), !lsl_diagnostic_report_path_.isEmpty());
	if(lsl_diagnostic_report_path_.isEmpty()) lsl_diagnostic_result_.insert(QStringLiteral("report_write_error"), QStringLiteral("Не удалось сохранить файл в AppLocalDataLocation; текст отчёта доступен через neuro.get_lsl_status."));
	if(lsl_reader_)
	{
		{
			std::lock_guard<std::mutex> lock(lsl_reader_->mutex);
			if(lsl_reader_->recording_writer) lsl_reader_->recording_stop_requested = true;
		}
		lsl_reader_->stop.store(true);
		lsl_connection_state_ = QStringLiteral("disconnecting");
	}
	else lsl_connection_state_ = QStringLiteral("disconnected");
	if(!lsl_reader_ && lsl_units_override_) lsl_units_override_->setEnabled(true);
	playing_ = false;
	if(lsl_disconnect_button_) lsl_disconnect_button_->setEnabled(false);
	if(lsl_connect_button_) lsl_connect_button_->setEnabled(lsl_streams_ && lsl_streams_->count() > 0);
	if(source_ && source_->currentIndex() == 3 && source_status_label_) source_status_label_->setText(tr("Источник: LSL · отключён после диагностики"));
	appendDiagnostic(tr("LSL: диагностический интервал завершён; поток отключён."));
}

void NeuroControlWidget::disconnectLsl()
{
	if(lsl_shadow_checkbox_ && lsl_shadow_checkbox_->isChecked()) lsl_shadow_checkbox_->setChecked(false);
	if(lsl_shadow_contract_confirm_checkbox_ && lsl_shadow_contract_confirm_checkbox_->isChecked())
	{
		QSignalBlocker blocker(lsl_shadow_contract_confirm_checkbox_);
		lsl_shadow_contract_confirm_checkbox_->setChecked(false);
	}
	resetLslShadowPipeline(QString(), true);
	if(lsl_diagnostic_running_)
	{
		finishLslDiagnostic(QStringLiteral("interrupted_by_disconnect"));
		return;
	}
	if(lsl_reader_)
	{
		{
			std::lock_guard<std::mutex> lock(lsl_reader_->mutex);
			if(lsl_reader_->recording_writer) lsl_reader_->recording_stop_requested = true;
		}
		lsl_reader_->stop.store(true);
		lsl_connection_state_ = QStringLiteral("disconnecting");
		if(lsl_disconnect_button_) lsl_disconnect_button_->setEnabled(false);
		if(lsl_connect_button_) lsl_connect_button_->setEnabled(false);
		if(lsl_details_label_) lsl_details_label_->setText(tr("Отключение LSL; активная запись будет корректно завершена…"));
		return;
	}
	lsl_connection_state_ = QStringLiteral("disconnected");
	if(lsl_units_override_) lsl_units_override_->setEnabled(true);
	lsl_shadow_rate_history_.clear();
	if(lsl_connect_button_) lsl_connect_button_->setEnabled(lsl_streams_ && lsl_streams_->count() > 0);
	if(lsl_disconnect_button_) lsl_disconnect_button_->setEnabled(false);
	if(source_ && source_->currentIndex() == 3 && source_status_label_)
		source_status_label_->setText(tr("Источник: LSL · отключён"));
}

void NeuroControlWidget::startLslRecording()
{
	if(!lsl_reader_ || source_->currentIndex() != 3 || lsl_connection_state_ != QLatin1String("connected"))
	{
		appendDiagnostic(tr("LSL: запись можно начать только при подключённом потоке."));
		return;
	}
	if(lsl_active_stream_.format != QLatin1String("float32"))
	{
		appendDiagnostic(tr("LSL: lossless запись доступна только для источника float32."));
		return;
	}
	if(!lsl_active_stream_.native_float32_exact_available)
	{
		appendDiagnostic(tr("LSL: точная запись float32 невозможна: загруженная lsl.dll не предоставляет lsl_pull_chunk_f; файл не создан."));
		return;
	}
	if(lsl_recordings_directory_.isEmpty() || !QDir().mkpath(lsl_recordings_directory_))
	{
		appendDiagnostic(tr("LSL: не удалось создать каталог записей NeuroControl."));
		return;
	}
	const QStorageInfo storage(lsl_recordings_directory_);
	if(storage.isValid() && storage.bytesAvailable() < 128ll * 1024ll * 1024ll)
	{
		appendDiagnostic(tr("LSL: на диске осталось меньше 128 МиБ; запись не начата."));
		return;
	}
	const QString id = QStringLiteral("lsl-%1-%2")
		.arg(QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMddTHHmmsszzzZ")), QUuid::createUuid().toString(QUuid::WithoutBraces));
	const QString partial_path = QDir(lsl_recordings_directory_).filePath(id + QStringLiteral(".nlsr.part"));
	const QString final_path = QDir(lsl_recordings_directory_).filePath(id + QStringLiteral(".nlsr"));
	const QString metadata_path = QDir(lsl_recordings_directory_).filePath(id + QStringLiteral(".json"));
	QJsonArray channels;
	QJsonArray names, channel_types, units, effective_units, factors;
	const QString unit_override = lsl_active_units_override_;
	for(int i = 0; i < lsl_active_stream_.channel_count; ++i)
	{
		const QString name = lsl_active_stream_.channels.value(i, QStringLiteral("Канал %1").arg(i + 1));
		const QString type = lsl_active_stream_.channel_types.value(i);
		const QString display_type = lslChannelKind(name, type);
		const QString raw_unit = lsl_active_stream_.units.value(i);
		const QString effective_unit = unit_override == QLatin1String("auto") ? raw_unit : unit_override;
		const double factor = neurocontrol::LslEegSource::unitScaleToVolts(effective_unit);
		if(factor == 0.0)
		{
			appendDiagnostic(tr("LSL: единицы канала %1 неизвестны; запись не начата.").arg(name));
			return;
		}
		channels.append(QJsonObject{{QStringLiteral("index"), i}, {QStringLiteral("name"), name},
			{QStringLiteral("type"), type}, {QStringLiteral("display_type"), display_type},
			{QStringLiteral("unit"), raw_unit}, {QStringLiteral("effective_unit"), effective_unit},
			{QStringLiteral("unit_to_volts"), factor}});
		names.append(name);
		channel_types.append(type);
		units.append(raw_unit);
		effective_units.append(effective_unit);
		factors.append(factor);
	}
	const QString started = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
	QJsonObject metadata{
		{QStringLiteral("format"), QStringLiteral("metasiberia-neurocontrol-lsl-recording-v1")},
		{QStringLiteral("binary_format"), QStringLiteral("metasiberia-neurocontrol-lslr-v1")},
		{QStringLiteral("recording_id"), id}, {QStringLiteral("status"), QStringLiteral("recording")},
		{QStringLiteral("complete"), false}, {QStringLiteral("data_file"), QFileInfo(partial_path).fileName()},
		{QStringLiteral("started_at_utc"), started}, {QStringLiteral("ended_at_utc"), QJsonValue()},
		{QStringLiteral("name"), lsl_active_stream_.name}, {QStringLiteral("type"), lsl_active_stream_.type},
		{QStringLiteral("uid"), lsl_active_stream_.uid}, {QStringLiteral("source_id"), lsl_active_stream_.source_id},
		{QStringLiteral("hostname"), lsl_active_stream_.hostname}, {QStringLiteral("nominal_rate_hz"), lsl_active_stream_.nominal_hz},
		{QStringLiteral("electrode_montage"), lsl_active_stream_.electrode_montage},
		{QStringLiteral("reference_scheme"), lsl_active_stream_.reference_scheme},
		{QStringLiteral("channel_count"), lsl_active_stream_.channel_count}, {QStringLiteral("sample_format"), QStringLiteral("float32")},
		{QStringLiteral("native_float32_exact_available"), lsl_active_stream_.native_float32_exact_available},
		{QStringLiteral("channels"), channels}, {QStringLiteral("channel_names"), names},
		{QStringLiteral("channel_types"), channel_types}, {QStringLiteral("units"), units},
		{QStringLiteral("effective_units"), effective_units}, {QStringLiteral("unit_to_volts"), factors},
		{QStringLiteral("raw_lsl_xml_base64"), QString::fromLatin1(lsl_active_stream_.raw_xml.toBase64())},
		{QStringLiteral("raw_samples_preserved"), true},
		{QStringLiteral("timestamp_storage"), QJsonObject{
			{QStringLiteral("source"), QStringLiteral("original per-sample LSL timestamps, stored as IEEE-754 float64 without correction")},
			{QStringLiteral("receiver_arrival"), QStringLiteral("lsl_local_clock sampled once per received chunk and repeated for each sample in that chunk")},
			{QStringLiteral("display_timeline"), QStringLiteral("replay uses sample_index / nominal_rate_hz; it is derived and does not replace stored timestamps")}}},
		{QStringLiteral("started_receiver_counters"), QJsonObject{
			{QStringLiteral("pauses"), static_cast<qint64>(lsl_pause_events_)},
			{QStringLiteral("reconnects"), static_cast<qint64>(lsl_reconnects_)},
			{QStringLiteral("errors"), static_cast<qint64>(lsl_errors_)},
			{QStringLiteral("local_display_buffer_drops"), static_cast<qint64>(dropped_samples_)}}},
		{QStringLiteral("data_integrity"), QStringLiteral("complete footer, exact file size and FNV-1a-64 payload checksum are checked when loaded")},
		{QStringLiteral("classifier_connected"), false}, {QStringLiteral("avatar_control_enabled"), false}};
	auto writer = std::make_unique<neurocontrol::LslRecordingWriter>();
	std::string error;
	if(!writer->open(std::filesystem::path(partial_path.toStdWString()), static_cast<std::uint32_t>(lsl_active_stream_.channel_count), error))
	{
		appendDiagnostic(tr("LSL: не удалось начать запись: %1").arg(QString::fromStdString(error)));
		return;
	}
	QString metadata_error;
	if(!writeJsonAtomically(metadata_path, metadata, metadata_error))
	{
		writer->leaveIncomplete();
		appendDiagnostic(tr("LSL: не удалось записать начальные метаданные: %1").arg(metadata_error));
		return;
	}
	{
		std::lock_guard<std::mutex> lock(lsl_reader_->mutex);
		if(!lsl_reader_->connected || lsl_reader_->recording_writer)
		{
			writer->leaveIncomplete();
			appendDiagnostic(tr("LSL: состояние потока изменилось до начала записи."));
			return;
		}
		lsl_reader_->recording_id = id;
		lsl_reader_->recording_partial_path = partial_path;
		lsl_reader_->recording_final_path = final_path;
		lsl_reader_->recording_metadata_path = metadata_path;
		lsl_reader_->recording_metadata = metadata;
		lsl_reader_->recording_error.clear();
		lsl_reader_->recording_completion_reason.clear();
		lsl_reader_->recording_samples = 0;
		lsl_reader_->recording_start_pauses = lsl_reader_->pause_events;
		lsl_reader_->recording_start_reconnects = lsl_reader_->reconnects;
		lsl_reader_->recording_start_errors = lsl_reader_->errors;
		lsl_reader_->recording_start_display_drops = lsl_reader_->buffer_overruns;
		lsl_reader_->recording_active = true;
		lsl_reader_->recording_stop_requested = false;
		lsl_reader_->recording_finalized = false;
		lsl_reader_->recording_writer = std::move(writer);
		lsl_reader_->events.push_back(QStringLiteral("LSL: начата запись %1").arg(id));
	}
	lsl_recording_status_ = QJsonObject{{QStringLiteral("state"), QStringLiteral("recording")},
		{QStringLiteral("recording_id"), id}, {QStringLiteral("metadata_path"), metadata_path},
		{QStringLiteral("data_path"), partial_path}, {QStringLiteral("samples_written"), 0},
		{QStringLiteral("raw_samples_preserved"), true}, {QStringLiteral("classifier_connected"), false},
		{QStringLiteral("avatar_control_enabled"), false}};
	lsl_recording_status_label_->setText(tr("Записывается %1 · float32, %2 каналов").arg(id).arg(lsl_active_stream_.channel_count));
	lsl_record_button_->setEnabled(false);
	lsl_record_stop_button_->setEnabled(true);
	appendDiagnostic(tr("LSL: запись началась в %1; исходные float32 и временные метки пишутся независимо от обновления графика.").arg(metadata_path));
}

void NeuroControlWidget::requestStopLslRecording()
{
	if(!lsl_reader_) return;
	{
		std::lock_guard<std::mutex> lock(lsl_reader_->mutex);
		if(!lsl_reader_->recording_active) return;
		lsl_reader_->recording_stop_requested = true;
		lsl_reader_->recording_active = false;
	}
	lsl_recording_status_.insert(QStringLiteral("state"), QStringLiteral("finalizing"));
	lsl_record_stop_button_->setEnabled(false);
	lsl_recording_status_label_->setText(tr("Завершение записи и проверка бинарного файла…"));
	appendDiagnostic(tr("LSL: запрошено штатное завершение записи; приём потока продолжается."));
}

QJsonObject NeuroControlWidget::lslRecordingStatusJson() const
{
	QJsonObject active = lsl_recording_status_;
	if(lsl_reader_)
	{
		std::lock_guard<std::mutex> lock(lsl_reader_->mutex);
		if(!lsl_reader_->recording_id.isEmpty())
		{
			const QString state = lsl_reader_->recording_finalized ? (lsl_reader_->recording_file_complete ? QStringLiteral("complete") : QStringLiteral("incomplete")) :
				lsl_reader_->recording_stop_requested || !lsl_reader_->recording_active ? QStringLiteral("finalizing") : QStringLiteral("recording");
			active = QJsonObject{{QStringLiteral("state"), state}, {QStringLiteral("recording_id"), lsl_reader_->recording_id},
				{QStringLiteral("metadata_path"), lsl_reader_->recording_metadata_path},
				{QStringLiteral("data_path"), lsl_reader_->recording_finalized && lsl_reader_->recording_file_complete ? lsl_reader_->recording_final_path : lsl_reader_->recording_partial_path},
				{QStringLiteral("samples_written"), static_cast<qint64>(lsl_reader_->recording_samples)},
				{QStringLiteral("binary_checksum_fnv1a64"), QString::number(lsl_reader_->recording_checksum, 16).rightJustified(16, QLatin1Char('0'))},
				{QStringLiteral("error"), lsl_reader_->recording_error}, {QStringLiteral("completion_reason"), lsl_reader_->recording_completion_reason},
				{QStringLiteral("raw_samples_preserved"), true}, {QStringLiteral("classifier_connected"), false},
				{QStringLiteral("avatar_control_enabled"), false}};
		}
	}
	QJsonObject loaded;
	if(!lsl_loaded_recording_id_.isEmpty())
		loaded = QJsonObject{{QStringLiteral("recording_id"), lsl_loaded_recording_id_},
			{QStringLiteral("path"), lsl_loaded_recording_path_}, {QStringLiteral("loaded"), true},
			{QStringLiteral("sample_count"), static_cast<qint64>(lsl_recording_source_timestamps_.size())},
			{QStringLiteral("channel_count"), channel_count_},
			{QStringLiteral("integrity"), lsl_loaded_recording_metadata_.value(QStringLiteral("integrity")).toString()},
			{QStringLiteral("metadata"), lsl_loaded_recording_metadata_},
			{QStringLiteral("playback_running"), lsl_recording_playback_ && playing_},
			{QStringLiteral("playback_paused"), lsl_recording_playback_ && paused_},
			{QStringLiteral("raw_eeg_contents_sent"), false}};
	return QJsonObject{{QStringLiteral("active_recording"), active}, {QStringLiteral("loaded_recording"), loaded},
		{QStringLiteral("recordings_directory"), lsl_recordings_directory_},
		{QStringLiteral("load_in_progress"), lsl_recording_load_in_progress_},
		{QStringLiteral("raw_eeg_contents_sent"), false}};
}

QJsonArray NeuroControlWidget::listLslRecordingsJson() const
{
	QJsonArray result;
	const QDir directory(lsl_recordings_directory_);
	const QFileInfoList files = directory.entryInfoList(QStringList{QStringLiteral("*.json")}, QDir::Files | QDir::Readable, QDir::Time);
	for(const QFileInfo& file_info : files)
	{
		if(!pathIsWithin(lsl_recordings_directory_, file_info.canonicalFilePath()) || file_info.size() > 1024 * 1024) continue;
		QFile file(file_info.absoluteFilePath());
		if(!file.open(QIODevice::ReadOnly)) continue;
		const QJsonObject metadata = QJsonDocument::fromJson(file.readAll()).object();
		const QString id = metadata.value(QStringLiteral("recording_id")).toString();
		if(!validLslRecordingId(id) || metadata.value(QStringLiteral("format")).toString() != QLatin1String("metasiberia-neurocontrol-lsl-recording-v1")) continue;
		QString state = metadata.value(QStringLiteral("status")).toString();
		QString data_name = metadata.value(QStringLiteral("data_file")).toString();
		if(QFileInfo(data_name).fileName() != data_name) state = QStringLiteral("invalid_path");
		const QString data_path = directory.filePath(data_name);
		if(state == QLatin1String("recording") && QFileInfo::exists(data_path)) state = QStringLiteral("incomplete");
		if((state == QLatin1String("complete") || state == QLatin1String("recording") || state == QLatin1String("incomplete")) && !QFileInfo::exists(data_path)) state = QStringLiteral("missing_data_file");
		result.append(QJsonObject{{QStringLiteral("recording_id"), id}, {QStringLiteral("status"), state},
			{QStringLiteral("name"), metadata.value(QStringLiteral("name"))}, {QStringLiteral("uid"), metadata.value(QStringLiteral("uid"))},
			{QStringLiteral("started_at_utc"), metadata.value(QStringLiteral("started_at_utc"))},
			{QStringLiteral("sample_count"), metadata.value(QStringLiteral("sample_count"))},
			{QStringLiteral("channel_count"), metadata.value(QStringLiteral("channel_count"))},
			{QStringLiteral("nominal_rate_hz"), metadata.value(QStringLiteral("nominal_rate_hz"))},
			{QStringLiteral("sample_format"), metadata.value(QStringLiteral("sample_format"))},
			{QStringLiteral("data_file"), data_name}, {QStringLiteral("data_file_bytes"), QFileInfo(data_path).size()},
			{QStringLiteral("integrity_checked"), false}});
	}
	return result;
}

void NeuroControlWidget::loadLslRecording(const QString& recording_id)
{
	if(!validLslRecordingId(recording_id)) { appendDiagnostic(tr("LSL-запись: некорректный идентификатор.")); return; }
	if(lsl_recording_load_in_progress_) { appendDiagnostic(tr("LSL-запись: предыдущая загрузка ещё выполняется.")); return; }
	if(lsl_reader_ || lsl_connection_state_ == QLatin1String("connected") || lsl_connection_state_ == QLatin1String("connecting"))
		{ appendDiagnostic(tr("Сначала остановите запись и отключите живой LSL-поток.")); return; }
	if(lsl_recording_playback_) stopLslRecordingPlayback();
	const QString metadata_path = QDir(lsl_recordings_directory_).filePath(recording_id + QStringLiteral(".json"));
	if(!QFileInfo::exists(metadata_path) || !pathIsWithin(lsl_recordings_directory_, QFileInfo(metadata_path).canonicalFilePath()))
		{ appendDiagnostic(tr("LSL-запись не найдена в локальном каталоге NeuroControl.")); return; }
	lsl_recording_load_in_progress_ = true;
	lsl_recording_load_ = std::make_shared<NeuroControlLslRecordingLoad>();
	const auto state = lsl_recording_load_;
	const QString directory = lsl_recordings_directory_;
	auto* thread = QThread::create([state, metadata_path, directory, recording_id] {
		QString error;
		QFile metadata_file(metadata_path);
		QJsonObject metadata;
		if(!metadata_file.open(QIODevice::ReadOnly) || metadata_file.size() > 1024 * 1024)
			error = QStringLiteral("Не удалось безопасно прочитать JSON-метаданные записи.");
		else metadata = QJsonDocument::fromJson(metadata_file.readAll()).object();
		QString data_path;
		std::vector<float> samples;
		std::vector<double> source_timestamps, arrival_timestamps, unit_factors;
		std::uint32_t channel_count = 0;
		std::uint64_t sample_count = 0;
		if(error.isEmpty() && (metadata.value(QStringLiteral("format")).toString() != QLatin1String("metasiberia-neurocontrol-lsl-recording-v1") ||
			metadata.value(QStringLiteral("recording_id")).toString() != recording_id ||
			metadata.value(QStringLiteral("status")).toString() != QLatin1String("complete") || !metadata.value(QStringLiteral("complete")).toBool()))
			error = QStringLiteral("Запись неполная или использует неподдерживаемую версию формата.");
		const QString data_name = metadata.value(QStringLiteral("data_file")).toString();
		if(error.isEmpty() && (QFileInfo(data_name).fileName() != data_name || !data_name.endsWith(QStringLiteral(".nlsr"))))
			error = QStringLiteral("Имя бинарного файла метаданных недопустимо.");
		if(error.isEmpty())
		{
			QFileInfo binary_info(QDir(directory).filePath(data_name));
			data_path = binary_info.canonicalFilePath();
			if(!binary_info.isFile() || !pathIsWithin(directory, data_path)) error = QStringLiteral("Бинарный файл отсутствует или находится вне каталога записей.");
			else if(binary_info.size() > 128ll * 1024ll * 1024ll) error = QStringLiteral("Воспроизведение ограничено бинарными файлами размером до 128 МиБ; файл сохранён и не изменён.");
		}
		neurocontrol::LslRecordingReader reader;
		neurocontrol::LslRecordingInfo info;
		std::string recording_error;
		if(error.isEmpty() && !reader.open(std::filesystem::path(data_path.toStdWString()), info, recording_error))
		{
			error = QString::fromStdString(recording_error);
		}
		if(error.isEmpty() && !info.complete) error = QStringLiteral("Запись отмечена как неполная в бинарном заголовке.");
		if(error.isEmpty() && !reader.verify(recording_error)) error = QString::fromStdString(recording_error);
		channel_count = info.channel_count;
		sample_count = info.sample_count;
		const QJsonArray channel_array = metadata.value(QStringLiteral("channels")).toArray();
		if(error.isEmpty() && (channel_array.size() != static_cast<int>(channel_count) || sample_count == 0 ||
			metadata.value(QStringLiteral("sample_count")).toVariant().toULongLong() != sample_count))
			error = QStringLiteral("Количество каналов или отсчётов в JSON и бинарном файле не совпадает.");
		if(error.isEmpty())
		{
			const std::uint64_t values_count = sample_count * channel_count;
			if(values_count > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) error = QStringLiteral("Запись слишком велика для этого клиента.");
			else
			{
				samples.reserve(static_cast<std::size_t>(values_count));
				source_timestamps.reserve(static_cast<std::size_t>(sample_count));
				arrival_timestamps.reserve(static_cast<std::size_t>(sample_count));
				for(const QJsonValue& value : channel_array)
				{
					const double factor = value.toObject().value(QStringLiteral("unit_to_volts")).toDouble(0.0);
					if(!std::isfinite(factor) || factor <= 0.0) { error = QStringLiteral("Неверный масштаб единиц одного из каналов."); break; }
					unit_factors.push_back(factor);
				}
			}
		}
		for(std::uint64_t offset = 0; error.isEmpty() && offset < sample_count; offset += 4096)
		{
			const std::uint64_t count = std::min<std::uint64_t>(4096, sample_count - offset);
			std::vector<neurocontrol::EegFrame> frames;
			std::string read_error;
			if(!reader.readRange(offset, count, frames, read_error)) { error = QString::fromStdString(read_error); break; }
			for(const auto& frame : frames)
			{
				if(frame.raw_source_values.size() != channel_count) { error = QStringLiteral("Бинарный файл содержит неправильное число значений в кадре."); break; }
				samples.insert(samples.end(), frame.raw_source_values.begin(), frame.raw_source_values.end());
				source_timestamps.push_back(frame.source_timestamp_seconds);
				arrival_timestamps.push_back(frame.arrival_monotonic_seconds);
			}
		}
		{
			std::lock_guard<std::mutex> lock(state->mutex);
			state->recording_id = recording_id;
			state->path = data_path;
			state->error = error;
			state->metadata = metadata;
			state->samples = std::move(samples);
			state->source_timestamps = std::move(source_timestamps);
			state->arrival_timestamps = std::move(arrival_timestamps);
			state->unit_to_volts = std::move(unit_factors);
			state->channel_count = channel_count;
			state->sample_count = sample_count;
			state->ready = error.isEmpty();
			state->complete = true;
		}
	});
	connect(thread, &QThread::finished, thread, &QObject::deleteLater);
	thread->start();
	lsl_recording_status_.insert(QStringLiteral("load_state"), QStringLiteral("loading"));
	lsl_recording_status_label_->setText(tr("Проверка записи %1 и загрузка данных в фоне…").arg(recording_id));
}

void NeuroControlWidget::startLslRecordingPlayback()
{
	if(source_->currentIndex() != 4 || lsl_loaded_recording_id_.isEmpty() || channel_count_ <= 0 ||
		lsl_recording_playback_samples_.empty() || lsl_recording_source_timestamps_.empty())
	{
		appendDiagnostic(tr("Сначала загрузите и проверьте завершённую запись LSL."));
		return;
	}
	if(playing_ && lsl_recording_playback_) return;
	const std::size_t sample_count = lsl_recording_source_timestamps_.size();
	const std::size_t values_count = lsl_recording_playback_samples_.size();
	if(values_count != sample_count * static_cast<std::size_t>(channel_count_))
	{
		appendDiagnostic(tr("LSL-запись: размер массива отсчётов не совпадает с метаданными."));
		return;
	}
	lsl_recording_playback_rate_hz_ = lsl_loaded_recording_metadata_.value(QStringLiteral("nominal_rate_hz")).toDouble();
	if(!std::isfinite(lsl_recording_playback_rate_hz_) || lsl_recording_playback_rate_hz_ <= 0.0)
		lsl_recording_playback_rate_hz_ = sample_rate_ > 0 ? sample_rate_ : 250.0;
	sample_rate_ = std::clamp(static_cast<int>(std::llround(lsl_recording_playback_rate_hz_)), 1, 4096);
	const std::uint64_t sample_count_u64 = static_cast<std::uint64_t>(sample_count);
	next_sample_ = std::clamp(next_sample_, 0, static_cast<int>(std::min<std::uint64_t>(sample_count_u64, std::numeric_limits<int>::max())));
	if(static_cast<std::uint64_t>(next_sample_) >= sample_count_u64) next_sample_ = 0;
	lsl_recording_playback_start_sample_ = static_cast<std::uint64_t>(next_sample_);
	playback_start_sample_ = next_sample_;
	received_samples_ = 0;
	dropped_samples_ = 0;
	waveform_->clear();
	overview_waveform_->clear();
	lsl_recording_playback_ = true;
	playing_ = true;
	paused_ = false;
	elapsed_.start();
	start_button_->setText(tr("Старт"));
	start_button_->setEnabled(false);
	pause_button_->setEnabled(true);
	pause_button_->setText(tr("Пауза"));
	const int seconds = static_cast<int>(std::ceil(static_cast<double>(sample_count) / lsl_recording_playback_rate_hz_));
	playback_time_label_->setText(QStringLiteral("00:00 / %1:%2")
		.arg(seconds / 60, 2, 10, QLatin1Char('0')).arg(seconds % 60, 2, 10, QLatin1Char('0')));
	source_status_label_->setText(tr("Источник: сохранённая LSL-запись · воспроизведение · не живой поток · %1 Гц номинально")
		.arg(lsl_recording_playback_rate_hz_, 0, 'g', 8));
	lsl_recording_status_label_->setText(tr("Воспроизведение %1 · исходные timestamps доступны отдельно")
		.arg(lsl_loaded_recording_id_));
	timer_->start();
	appendDiagnostic(tr("Воспроизведение LSL-записи начато как архивная визуализация; классификатор и управление аватаром не используются."));
}

void NeuroControlWidget::stopLslRecordingPlayback()
{
	if(!lsl_recording_playback_) return;
	if(timer_) timer_->stop();
	lsl_recording_playback_ = false;
	playing_ = false;
	paused_ = false;
	if(start_button_) { start_button_->setEnabled(true); start_button_->setText(tr("Старт")); }
	if(pause_button_) { pause_button_->setEnabled(false); pause_button_->setText(tr("Пауза")); }
	if(lsl_recording_status_label_)
		lsl_recording_status_label_->setText(tr("Запись %1 загружена · целостность проверена · архивное воспроизведение остановлено")
			.arg(lsl_loaded_recording_id_));
	appendDiagnostic(tr("Воспроизведение LSL-записи остановлено; исходные данные и timestamps не изменены."));
	refreshControlAvailability();
}

void NeuroControlWidget::pollLsl()
{
	if(lsl_recording_load_)
	{
		bool complete = false, ready = false;
		QString id, path, error;
		QJsonObject metadata;
		std::vector<float> samples;
		std::vector<double> source_timestamps, arrival_timestamps, unit_factors;
		std::uint32_t channels = 0;
		std::uint64_t sample_count = 0;
		{
			std::lock_guard<std::mutex> lock(lsl_recording_load_->mutex);
			complete = lsl_recording_load_->complete;
			if(complete)
			{
				ready = lsl_recording_load_->ready; id = lsl_recording_load_->recording_id; path = lsl_recording_load_->path;
				error = lsl_recording_load_->error; metadata = lsl_recording_load_->metadata;
				samples = std::move(lsl_recording_load_->samples);
				source_timestamps = std::move(lsl_recording_load_->source_timestamps);
				arrival_timestamps = std::move(lsl_recording_load_->arrival_timestamps);
				unit_factors = std::move(lsl_recording_load_->unit_to_volts);
				channels = lsl_recording_load_->channel_count; sample_count = lsl_recording_load_->sample_count;
			}
		}
		if(complete)
		{
			lsl_recording_load_.reset();
			lsl_recording_load_in_progress_ = false;
			if(ready)
			{
				lsl_loaded_recording_id_ = id; lsl_loaded_recording_path_ = path;
				lsl_loaded_recording_metadata_ = metadata;
				lsl_loaded_recording_metadata_.insert(QStringLiteral("integrity"), QStringLiteral("verified"));
				lsl_recording_playback_samples_ = std::move(samples);
				lsl_recording_source_timestamps_ = std::move(source_timestamps);
				lsl_recording_arrival_timestamps_ = std::move(arrival_timestamps);
				lsl_recording_unit_to_volts_ = std::move(unit_factors);
				channel_count_ = static_cast<int>(channels);
				lsl_recording_playback_rate_hz_ = metadata.value(QStringLiteral("nominal_rate_hz")).toDouble(250.0);
				if(!std::isfinite(lsl_recording_playback_rate_hz_) || lsl_recording_playback_rate_hz_ <= 0.0) lsl_recording_playback_rate_hz_ = 250.0;
				sample_rate_ = std::clamp(static_cast<int>(std::llround(lsl_recording_playback_rate_hz_)), 1, 4096);
				next_sample_ = 0; lsl_recording_playback_ = false; playing_ = paused_ = false;
				seek_slider_->setRange(0, static_cast<int>(std::min<std::uint64_t>(sample_count ? sample_count - 1 : 0, std::numeric_limits<int>::max())));
				seek_slider_->setEnabled(sample_count > 0);
				const double duration = static_cast<double>(sample_count) / sample_rate_;
				const int seconds = static_cast<int>(std::round(duration));
				playback_time_label_->setText(QStringLiteral("00:00 / %1:%2").arg(seconds / 60, 2, 10, QLatin1Char('0')).arg(seconds % 60, 2, 10, QLatin1Char('0')));
				if(source_->currentIndex() != 4) source_->setCurrentIndex(4);
				else
				{
					std::vector<std::string> names, types, units;
					for(const QJsonValue& value : metadata.value(QStringLiteral("channels")).toArray())
					{
						const QJsonObject channel = value.toObject();
						names.push_back(channel.value(QStringLiteral("name")).toString().toUtf8().toStdString());
						types.push_back(channel.value(QStringLiteral("display_type")).toString().toUtf8().toStdString());
						units.push_back(channel.value(QStringLiteral("unit")).toString().toUtf8().toStdString());
					}
					updateChannels(names, types, units);
				}
				lsl_record_play_button_->setEnabled(true);
				lsl_recording_status_label_->setText(tr("Загружена запись %1 · %2 каналов · %3 отсчётов · целостность проверена")
					.arg(id).arg(channel_count_).arg(sample_count));
				lsl_recording_status_ = QJsonObject{{QStringLiteral("state"), QStringLiteral("loaded")},
					{QStringLiteral("recording_id"), id}, {QStringLiteral("path"), path},
					{QStringLiteral("sample_count"), static_cast<qint64>(sample_count)},
					{QStringLiteral("channel_count"), channel_count_}, {QStringLiteral("integrity"), QStringLiteral("verified")}};
				appendDiagnostic(tr("LSL: запись %1 загружена после проверки checksum; источник обозначен как сохранённый поток, не живое устройство.").arg(id));
			}
			else
			{
				lsl_recording_status_ = QJsonObject{{QStringLiteral("load_state"), QStringLiteral("error")}, {QStringLiteral("recording_id"), id}, {QStringLiteral("error"), error}};
				lsl_recording_status_label_->setText(tr("Ошибка загрузки LSL-записи: %1").arg(error));
				appendDiagnostic(tr("LSL: %1").arg(error));
			}
		}
	}
	if(lsl_library_configuration_)
	{
		QString path, provenance, sha256, error;
		bool complete = false, ready = false;
		{
			std::lock_guard<std::mutex> lock(lsl_library_configuration_->mutex);
			complete = lsl_library_configuration_->complete;
			if(complete)
			{
				path = lsl_library_configuration_->path;
				provenance = lsl_library_configuration_->provenance;
				sha256 = lsl_library_configuration_->sha256;
				error = lsl_library_configuration_->error;
				ready = lsl_library_configuration_->ready;
			}
		}
		if(complete)
		{
			lsl_library_configuration_.reset();
			lsl_library_state_ = ready ? QStringLiteral("ready") : QStringLiteral("error");
			lsl_library_error_ = error;
			if(ready)
			{
				lsl_library_path_ = path;
				lsl_library_provenance_ = provenance;
				lsl_library_sha256_ = sha256;
				QSettings().setValue(QStringLiteral("neurocontrol/lslLibraryPath"), lsl_library_path_);
				lsl_details_label_->setText(tr("Verified LabRecorder lsl.dll: %1 · SHA-256 %2").arg(QFileInfo(path).fileName(), sha256.left(16)));
				appendDiagnostic(tr("LSL: найдена и проверена x64-библиотека LabRecorder; путь сохранён в настройках."));
			}
			else
			{
				lsl_discovery_state_ = QStringLiteral("library_error");
				lsl_discovery_message_ = error;
				lsl_details_label_->setText(tr("LSL library unavailable: %1").arg(error));
				appendDiagnostic(tr("LSL: %1").arg(error));
			}
			const bool discover = lsl_discovery_after_configuration_;
			lsl_discovery_after_configuration_ = false;
			if(ready && discover) scanLsl();
		}
	}
	if(lsl_discovery_)
	{
		QVector<neurocontrol::LslStreamDescriptor> streams;
		QString error;
		bool complete = false;
		{
			std::lock_guard<std::mutex> lock(lsl_discovery_->mutex);
			complete = lsl_discovery_->complete;
			if(complete) { streams = lsl_discovery_->streams; error = lsl_discovery_->error; }
		}
		if(complete)
		{
			lsl_discovery_.reset();
			lsl_discovered_ = std::move(streams);
			lsl_streams_->clear();
			for(const auto& d : lsl_discovered_)
				lsl_streams_->addItem(QStringLiteral("%1 · %2 · %3 Гц · %4 кан. [%5]")
					.arg(d.name, d.type).arg(d.nominal_hz, 0, 'g', 8).arg(d.channel_count).arg(d.hostname));
			lsl_scan_button_->setEnabled(true);
			lsl_connect_button_->setEnabled(!lsl_discovered_.isEmpty() && !lsl_reader_);
			if(!error.isEmpty())
			{
				lsl_discovery_state_ = QStringLiteral("error");
				lsl_discovery_message_ = error;
				lsl_details_label_->setText(tr("Ошибка поиска: %1").arg(error));
				appendDiagnostic(tr("LSL: %1").arg(error));
			}
			else if(lsl_discovered_.isEmpty())
			{
				lsl_discovery_state_ = QStringLiteral("no_streams");
				lsl_discovery_message_ = QStringLiteral("Активных числовых LSL-потоков не найдено. Запущенное окно Neurosoft не означает, что экспорт LSL публикует ЭЭГ; проверьте включение регистрации/экспорта в самой программе.");
				lsl_details_label_->setText(lsl_discovery_message_);
				appendDiagnostic(tr("LSL: %1").arg(lsl_discovery_message_));
			}
			else
			{
				lsl_discovery_state_ = QStringLiteral("complete");
				lsl_discovery_message_ = QStringLiteral("Найдено числовых LSL-потоков: %1.").arg(lsl_discovered_.size());
			}
		}
	}
	if(!lsl_reader_) return;
	std::deque<neurocontrol::EegFrame> frames;
	QStringList events;
	neurocontrol::LslStreamDescriptor descriptor;
	neurocontrol::LslTimestampDiagnostics timestamps;
	std::uint64_t overrun = 0, pauses = 0, reconnects = 0, errors = 0;
	bool connected = false, finished = false, stop_requested = false, record_finalized = false;
	QString recording_id, recording_error;
	QJsonObject recording_metadata;
	std::uint64_t recording_samples = 0, recording_checksum = 0;
	bool recording_file_complete = false;
	{
		std::lock_guard<std::mutex> lock(lsl_reader_->mutex);
		frames.swap(lsl_reader_->frames);
		events.swap(lsl_reader_->events);
		descriptor = lsl_reader_->descriptor;
		timestamps = lsl_reader_->timestamps;
		overrun = lsl_reader_->buffer_overruns;
		pauses = lsl_reader_->pause_events;
		reconnects = lsl_reader_->reconnects;
		errors = lsl_reader_->errors;
		connected = lsl_reader_->connected;
		finished = lsl_reader_->finished;
		stop_requested = lsl_reader_->stop.load();
		recording_id = lsl_reader_->recording_id;
		recording_error = lsl_reader_->recording_error;
		record_finalized = lsl_reader_->recording_finalized;
		recording_samples = lsl_reader_->recording_writer ? lsl_reader_->recording_writer->sampleCount() : lsl_reader_->recording_samples;
		recording_checksum = lsl_reader_->recording_writer ? lsl_reader_->recording_writer->checksum() : lsl_reader_->recording_checksum;
		recording_file_complete = lsl_reader_->recording_file_complete;
		recording_metadata = lsl_reader_->recording_metadata;
	}
	for(const QString& event : events) { appendDiagnostic(event); lsl_error_ = event; }
	lsl_pause_events_ = pauses;
	lsl_reconnects_ = reconnects;
	lsl_errors_ = errors;
	lsl_timestamp_diagnostics_ = timestamps;
	if(!recording_id.isEmpty())
	{
		lsl_recording_status_ = QJsonObject{{QStringLiteral("state"), record_finalized ? (recording_file_complete ? QStringLiteral("complete") : QStringLiteral("incomplete")) : QStringLiteral("recording")},
			{QStringLiteral("recording_id"), recording_id}, {QStringLiteral("sample_count"), static_cast<qint64>(recording_samples)},
			{QStringLiteral("binary_checksum_fnv1a64"), QString::number(recording_checksum, 16).rightJustified(16, QLatin1Char('0'))},
			{QStringLiteral("error"), recording_error}, {QStringLiteral("metadata"), recording_metadata}};
		if(record_finalized)
		{
			lsl_record_stop_button_->setEnabled(false);
			lsl_record_button_->setEnabled(source_->currentIndex() == 3 && connected && recording_file_complete);
			lsl_recording_status_label_->setText(recording_file_complete ? tr("Запись %1 сохранена · %2 отсчётов · %3.nlsr")
				.arg(recording_id).arg(recording_samples).arg(recording_id) : tr("Запись %1 неполная: %2").arg(recording_id, recording_error));
		}
	}
	if(overrun > dropped_samples_) { appendDiagnostic(tr("LSL: локальный буфер графика отбросил %1 отсчётов.").arg(overrun - dropped_samples_)); dropped_samples_ = overrun; }
	if(source_->currentIndex() != 3)
	{
		refreshLslShadowCompatibility();
		if(finished)
		{
			lsl_recording_status_ = lslRecordingStatusJson();
			lsl_reader_.reset();
			lsl_connection_state_ = QStringLiteral("disconnected");
			lsl_units_override_->setEnabled(true);
			lsl_shadow_rate_history_.clear();
		}
		return;
	}
	if(connected && lsl_connection_state_ != QLatin1String("connected"))
	{
		const bool was_reconnecting = lsl_connection_state_ == QLatin1String("reconnecting");
		if(was_reconnecting) resetLslShadowPipeline(QString(), true, neurocontrol::LslShadowResetReason::Reconnected);
		lsl_active_stream_ = descriptor;
		sample_rate_ = std::isfinite(descriptor.nominal_hz) && descriptor.nominal_hz > 0 ? static_cast<int>(std::round(descriptor.nominal_hz)) : 250;
		channel_count_ = descriptor.channel_count;
		std::vector<std::string> names;
		for(int i = 0; i < descriptor.channel_count; ++i)
			names.push_back((descriptor.channels.value(i).isEmpty() ? tr("Канал %1").arg(i + 1) : descriptor.channels.at(i)).toUtf8().toStdString());
		event_marks_.clear();
		std::vector<std::string> types, units;
		for(int i = 0; i < descriptor.channel_count; ++i)
		{
			const QString type = lslChannelKind(descriptor.channels.value(i), descriptor.channel_types.value(i));
			types.push_back(type.toUtf8().toStdString());
			units.push_back(descriptor.units.value(i).toUtf8().toStdString());
		}
		updateChannels(names, types, units);
		playing_ = true;
		lsl_connection_state_ = QStringLiteral("connected");
		refreshControlAvailability();
	}
	else if(!connected && lsl_connection_state_ == QLatin1String("connected"))
	{
		if(lsl_shadow_checkbox_ && lsl_shadow_checkbox_->isChecked()) lsl_shadow_checkbox_->setChecked(false);
		if(lsl_shadow_contract_confirm_checkbox_ && lsl_shadow_contract_confirm_checkbox_->isChecked())
		{
			QSignalBlocker blocker(lsl_shadow_contract_confirm_checkbox_);
			lsl_shadow_contract_confirm_checkbox_->setChecked(false);
		}
		lsl_shadow_rate_history_.clear();
		lsl_shadow_rate_stable_ = false;
		lsl_shadow_observed_rate_hz_ = 0.0;
		resetLslShadowPipeline(QString(), true, neurocontrol::LslShadowResetReason::ConnectionLost);
		playing_ = false;
		lsl_connection_state_ = QStringLiteral("reconnecting");
	}
	if(!frames.empty() && channel_count_ == lsl_active_stream_.channel_count)
	{
		std::vector<double> microvolts(static_cast<std::size_t>(channel_count_));
		for(const auto& frame : frames)
		{
			++received_samples_;
			++next_sample_;
			lsl_recent_timestamps_.emplace_back(frame.source_timestamp_seconds, frame.arrival_monotonic_seconds);
			if(lsl_recent_timestamps_.size() > 4096) lsl_recent_timestamps_.pop_front();
			for(int i = 0; i < channel_count_; ++i) microvolts[static_cast<std::size_t>(i)] = frame.volts[static_cast<std::size_t>(i)] * 1e6;
			waveform_->appendFrame(microvolts.data(), channel_count_, frame.display_timestamp_seconds);
			overview_waveform_->appendFrame(microvolts.data(), channel_count_, frame.display_timestamp_seconds);
		}
		waveform_->refresh();
		overview_waveform_->refresh();
		refreshSpectrum();
	}
	if(lsl_rate_clock_.isValid() && lsl_rate_clock_.elapsed() >= 1000)
	{
		const qint64 elapsed_ms = lsl_rate_clock_.elapsed();
		receive_rate_hz_ = static_cast<double>(received_samples_ - rate_sample_snapshot_) * 1000.0 / static_cast<double>(elapsed_ms);
		rate_measured_ = true;
		rate_sample_snapshot_ = received_samples_;
		lsl_rate_clock_.restart();
		if(connected && std::isfinite(receive_rate_hz_) && receive_rate_hz_ > 0.0)
		{
			lsl_shadow_rate_history_.push_back(receive_rate_hz_);
			while(lsl_shadow_rate_history_.size() > 5) lsl_shadow_rate_history_.pop_front();
		}
		refreshLslShadowCompatibility();
	}
	if(connected)
	{
		QStringList units = descriptor.units;
		units.removeDuplicates();
		QStringList names = descriptor.channels;
		for(QString& name : names) name = name.simplified();
		const QString detail = tr("%1 · %2 каналов · %3 Гц номинально · %4 · единицы XML: %5 · приём: %6 отсч/с\nКаналы: %7\nTimestamps: %8 · паузы: %9 · переподключения: %10")
			.arg(descriptor.name).arg(descriptor.channel_count).arg(descriptor.nominal_hz, 0, 'g', 8)
			.arg(descriptor.format, units.isEmpty() ? tr("не указаны") : units.join(QStringLiteral(", ")))
			.arg(receive_rate_hz_, 0, 'f', 1).arg(names.join(QStringLiteral(", ")))
			.arg(lslStatusJson().value(QStringLiteral("timestamp_quality")).toString())
			.arg(lsl_pause_events_).arg(lsl_reconnects_);
		source_status_label_->setText(tr("Источник: LSL · подключён · %1").arg(descriptor.name));
		lsl_details_label_->setText(detail);
	}
	else if(finished)
	{
		playing_ = false;
		lsl_connection_state_ = stop_requested ? QStringLiteral("disconnected") : QStringLiteral("error");
		lsl_units_override_->setEnabled(true);
		lsl_shadow_rate_history_.clear();
		lsl_disconnect_button_->setEnabled(false);
		lsl_record_button_->setEnabled(false);
		lsl_connect_button_->setEnabled(lsl_streams_->count() > 0);
		lsl_recording_status_ = lslRecordingStatusJson();
		lsl_reader_.reset();
	}
	else if(lsl_connection_state_ == QLatin1String("reconnecting"))
		source_status_label_->setText(tr("Источник: LSL · переподключение"));
	refreshLslShadowCompatibility();
	if(connected) processLslShadowFrames(frames);
	if(lsl_shadow_checkbox_ && lsl_shadow_checkbox_->isChecked() && frames.empty() &&
		lsl_shadow_arrival_clock_.isValid() && lsl_shadow_arrival_clock_.elapsed() > 1000 && !lsl_shadow_stalled_)
	{
		resetLslShadowPipeline(QString(), true, neurocontrol::LslShadowResetReason::Stall);
		lsl_shadow_stalled_ = true;
		lsl_shadow_rate_history_.clear();
		lsl_shadow_rate_stable_ = false;
		lsl_shadow_observed_rate_hz_ = 0.0;
		++lsl_shadow_stall_events_;
		if(lsl_shadow_status_label_)
			lsl_shadow_status_label_->setText(tr("Live LSL не присылал данные больше 1 секунды. Предсказание сброшено; классификатор ждёт новый непрерывный поток."));
	}
	if(lsl_diagnostic_running_ && (lsl_connection_state_ == QLatin1String("error") || lsl_connection_state_ == QLatin1String("disconnected")))
		finishLslDiagnostic(QStringLiteral("connection_lost"));
	else if(lsl_diagnostic_running_ && lsl_diagnostic_elapsed_.elapsed() >= lsl_diagnostic_duration_seconds_ * 1000)
		finishLslDiagnostic(QStringLiteral("completed"));
	refreshDiagnosticsSummary();
}

void NeuroControlWidget::refreshLslShadowCompatibility()
{
	const bool was_compatible = lsl_shadow_compatible_;
	lsl_shadow_channel_indices_.clear();
	lsl_shadow_compatible_ = false;
	lsl_shadow_rate_stable_ = false;
	lsl_shadow_observed_rate_hz_ = 0.0;
	lsl_shadow_input_rate_hz_ = 0;
	lsl_shadow_manual_contract_required_ = false;
	lsl_shadow_compatibility_reason_.clear();
	lsl_shadow_montage_reference_state_.clear();
	lsl_shadow_reason_codes_.clear();
	if(loaded_model_path_.isEmpty() || !lsl_shadow_model_)
	{
		lsl_shadow_reason_codes_.append(QStringLiteral("model_missing"));
		lsl_shadow_compatibility_reason_ = tr("Загрузите корректную модель перед теневой классификацией.");
	}
	else if(!source_ || source_->currentIndex() != 3 || lsl_connection_state_ != QLatin1String("connected"))
	{
		lsl_shadow_reason_codes_.append(QStringLiteral("live_lsl_not_connected"));
		lsl_shadow_compatibility_reason_ = tr("Подключите live LSL-поток для проверки совместимости.");
	}
	else
	{
		const int model_rate = model_metadata_.value(QStringLiteral("sample_rate_hz")).toInt();
		const double advertised_rate = lsl_active_stream_.nominal_hz;
		const int input_rate = std::isfinite(advertised_rate) ? static_cast<int>(std::llround(advertised_rate)) : 0;
		if(input_rate >= 8 && input_rate <= 4000 && std::abs(advertised_rate - input_rate) <= 0.01)
			lsl_shadow_input_rate_hz_ = input_rate;
		if(model_rate <= 0 || input_rate <= 0 || std::abs(advertised_rate - input_rate) > 0.01)
		{
			lsl_shadow_reason_codes_.append(QStringLiteral("nominal_rate_not_integer"));
			lsl_shadow_compatibility_reason_ = tr("Нужна регулярная целочисленная частота LSL и корректная частота модели; поток заявляет %1 Гц, модель — %2 Гц.")
				.arg(advertised_rate, 0, 'g', 8).arg(model_rate);
		}
		else if(model_channels_.empty() || lsl_active_stream_.channel_count != lsl_active_stream_.channels.size())
		{
			lsl_shadow_reason_codes_.append(QStringLiteral("channel_metadata_incomplete"));
			lsl_shadow_compatibility_reason_ = tr("Метаданные каналов потока неполные.");
		}
		else
		{
			QMap<QString, QVector<int>> source_indices;
			for(int i = 0; i < lsl_active_stream_.channels.size(); ++i)
			{
				const QString name = lsl_active_stream_.channels.at(i).trimmed().toUpper();
				if(!name.isEmpty()) source_indices[name].push_back(i);
			}
			QString missing_or_invalid;
			std::set<QString> requested_names;
			for(const std::string& model_channel : model_channels_)
			{
				const QString requested = QString::fromStdString(model_channel).trimmed().toUpper();
				if(requested.isEmpty() || requested_names.count(requested) != 0 || !source_indices.contains(requested) || source_indices.value(requested).size() != 1)
				{
					missing_or_invalid = QString::fromStdString(model_channel);
					break;
				}
				const int source_index = source_indices.value(requested).front();
				const QString channel_type = lsl_active_stream_.channel_types.value(source_index).trimmed();
				if(lslChannelKind(lsl_active_stream_.channels.at(source_index), channel_type) != QLatin1String("EEG"))
				{
					missing_or_invalid = QString::fromStdString(model_channel);
					break;
				}
				const QString unit_override = lsl_active_units_override_ != QLatin1String("auto") ?
					lsl_active_units_override_ : QString();
				const QString effective_unit = unit_override.isEmpty() ? lsl_active_stream_.units.value(source_index) : unit_override;
				if(neurocontrol::LslEegSource::unitScaleToVolts(effective_unit) == 0.0)
				{
					missing_or_invalid = QString::fromStdString(model_channel);
					break;
				}
				requested_names.insert(requested);
				lsl_shadow_channel_indices_.push_back(static_cast<std::size_t>(source_index));
			}
			if(!missing_or_invalid.isEmpty())
			{
				lsl_shadow_reason_codes_.append(QStringLiteral("channel_mapping_or_unit_invalid"));
				lsl_shadow_channel_indices_.clear();
				lsl_shadow_compatibility_reason_ = tr("Канал модели «%1» отсутствует/дублируется, имеет неподходящий тип или неизвестную единицу измерения.").arg(missing_or_invalid);
			}
			else
			{
				if(lsl_shadow_rate_history_.size() < 5)
				{
					lsl_shadow_reason_codes_.append(QStringLiteral("receive_rate_pending"));
					lsl_shadow_compatibility_reason_ = tr("Ожидание 5 секунд измерений фактического приёма LSL перед проверкой частоты модели.");
				}
				else
				{
					const double sum = std::accumulate(lsl_shadow_rate_history_.begin(), lsl_shadow_rate_history_.end(), 0.0);
					lsl_shadow_observed_rate_hz_ = sum / lsl_shadow_rate_history_.size();
					const auto bounds = std::minmax_element(lsl_shadow_rate_history_.begin(), lsl_shadow_rate_history_.end());
					const double allowed_mean_error = std::max(2.0, input_rate * 0.03);
					const double allowed_window_span = std::max(10.0, input_rate * 0.12);
					lsl_shadow_rate_stable_ = std::abs(lsl_shadow_observed_rate_hz_ - advertised_rate) <= allowed_mean_error &&
						(*bounds.second - *bounds.first) <= allowed_window_span;
					if(!lsl_shadow_rate_stable_)
					{
						lsl_shadow_reason_codes_.append(QStringLiteral("receive_rate_outside_tolerance"));
						lsl_shadow_compatibility_reason_ = tr("Номинальная частота потока %1 Гц не подтверждена стабильным приёмом: среднее %2 отсч/с за 5 секунд. Теневая классификация заблокирована.")
							.arg(advertised_rate, 0, 'g', 8).arg(lsl_shadow_observed_rate_hz_, 0, 'f', 1);
					}
					else
					{
				const QJsonObject preprocessing = model_metadata_.value(QStringLiteral("preprocessing")).toObject();
				const QString model_montage = model_metadata_.value(QStringLiteral("electrode_montage")).toString(
					preprocessing.value(QStringLiteral("electrode_montage")).toString()).trimmed();
				const QString model_reference = model_metadata_.value(QStringLiteral("reference_scheme")).toString(
					model_metadata_.value(QStringLiteral("reference")).toString(
					preprocessing.value(QStringLiteral("reference_scheme")).toString())).trimmed();
				const QString source_montage = lsl_active_stream_.electrode_montage.trimmed();
				const QString source_reference = lsl_active_stream_.reference_scheme.trimmed();
				const auto canonical_contract_value = [](QString value) { return value.trimmed().toLower().simplified(); };
				const bool montage_missing = model_montage.isEmpty() || source_montage.isEmpty();
				const bool reference_missing = model_reference.isEmpty() || source_reference.isEmpty();
				if(!montage_missing && canonical_contract_value(model_montage) != canonical_contract_value(source_montage))
				{
					lsl_shadow_reason_codes_.append(QStringLiteral("montage_mismatch"));
					lsl_shadow_compatibility_reason_ = tr("Монтаж источника и модели различается; классификация заблокирована.");
					lsl_shadow_montage_reference_state_ = tr("несовпадение подтверждённых метаданных");
				}
				else if(!reference_missing && canonical_contract_value(model_reference) != canonical_contract_value(source_reference))
				{
					lsl_shadow_reason_codes_.append(QStringLiteral("reference_mismatch"));
					lsl_shadow_compatibility_reason_ = tr("Референс источника и модели различается; классификация заблокирована.");
					lsl_shadow_montage_reference_state_ = tr("несовпадение подтверждённых метаданных");
				}
				else
				{
					lsl_shadow_manual_contract_required_ = montage_missing || reference_missing;
					if(lsl_shadow_manual_contract_required_ &&
						(!lsl_shadow_contract_confirm_checkbox_ || !lsl_shadow_contract_confirm_checkbox_->isChecked()))
					{
						lsl_shadow_reason_codes_.append(QStringLiteral("electrode_contract_confirmation_required"));
						lsl_shadow_montage_reference_state_ = tr("неполные метаданные; требуется ручная сверка монтажа и референса");
						lsl_shadow_compatibility_reason_ = tr("Каналы, единицы и частота подходят, но монтаж/референс не полностью заданы. Сверьте их с обучающей записью и подтвердите отдельно.");
					}
					else
					{
						lsl_shadow_compatible_ = true;
						lsl_shadow_reason_codes_.clear();
						lsl_shadow_montage_reference_state_ = lsl_shadow_manual_contract_required_ ?
							tr("метаданные неполны; оператор подтвердил соответствие") : tr("монтаж и референс совпали по метаданным");
						if(!lsl_shadow_pipeline_ || !lsl_shadow_pipeline_->matches(input_rate, model_rate,
							static_cast<std::size_t>(lsl_active_stream_.channel_count), lsl_shadow_channel_indices_, true))
						{
							auto pipeline = std::make_unique<neurocontrol::LslShadowPipeline>();
							std::string pipeline_error;
							if(!pipeline->configure(input_rate, model_rate, static_cast<std::size_t>(lsl_active_stream_.channel_count),
								lsl_shadow_channel_indices_, lsl_shadow_model_.get(), pipeline_error))
							{
								lsl_shadow_compatible_ = false;
								lsl_shadow_manual_contract_required_ = false;
								lsl_shadow_compatibility_reason_ = QString::fromStdString(pipeline_error);
							}
							else lsl_shadow_pipeline_ = std::move(pipeline);
						}
					}
					if(lsl_shadow_compatible_)
					{
						lsl_shadow_input_rate_hz_ = input_rate;
			lsl_shadow_compatibility_reason_ = tr("Технически совместимо: %1→%2 Гц (%3/%4), %5 каналов модели, вход переведён в вольты; %6. FIR задерживает примерно на %7 мс. Точность модели на этом источнике не подтверждена.")
							.arg(input_rate).arg(model_rate).arg(lsl_shadow_pipeline_->resampleUp()).arg(lsl_shadow_pipeline_->resampleDown())
							.arg(lsl_shadow_channel_indices_.size()).arg(lsl_shadow_montage_reference_state_)
							.arg(lsl_shadow_pipeline_->groupDelayMilliseconds(), 0, 'f', 2);
					}
				}
				}
			}
		}
	}
	}
	if(lsl_shadow_contract_confirm_checkbox_)
	{
		lsl_shadow_contract_confirm_checkbox_->setEnabled(lsl_shadow_manual_contract_required_);
		if(!lsl_shadow_manual_contract_required_ && lsl_shadow_contract_confirm_checkbox_->isChecked())
		{
			QSignalBlocker blocker(lsl_shadow_contract_confirm_checkbox_);
			lsl_shadow_contract_confirm_checkbox_->setChecked(false);
		}
	}
	if(lsl_shadow_checkbox_)
	{
		lsl_shadow_checkbox_->setEnabled(lsl_shadow_compatible_);
		if(!lsl_shadow_compatible_ && lsl_shadow_checkbox_->isChecked()) lsl_shadow_checkbox_->setChecked(false);
	}
	if(!lsl_shadow_compatible_ && (was_compatible || (lsl_shadow_checkbox_ && lsl_shadow_checkbox_->isChecked()) || !lsl_shadow_prediction_.isEmpty()))
		resetLslShadowPipeline(QString(), true);
	if(lsl_shadow_status_label_)
	{
		if(lsl_shadow_checkbox_ && lsl_shadow_checkbox_->isChecked())
			lsl_shadow_status_label_->setText(tr("Теневая классификация активна · команды аватару не отправляются."));
		else lsl_shadow_status_label_->setText(lsl_shadow_compatibility_reason_);
	}
}

void NeuroControlWidget::resetLslShadowPipeline(const QString& reason, bool clear_prediction,
	neurocontrol::LslShadowResetReason reset_reason)
{
	if(lsl_shadow_pipeline_) lsl_shadow_pipeline_->reset(reset_reason);
	lsl_shadow_last_display_timestamp_ = 0.0;
	lsl_shadow_last_processing_ms_ = 0.0;
	lsl_shadow_stalled_ = false;
	lsl_shadow_arrival_clock_.invalidate();
	if(clear_prediction) lsl_shadow_prediction_ = {};
	if(!reason.isEmpty() && lsl_shadow_status_label_) lsl_shadow_status_label_->setText(reason);
}

void NeuroControlWidget::processLslShadowFrames(const std::deque<neurocontrol::EegFrame>& frames)
{
	if(!lsl_shadow_checkbox_ || !lsl_shadow_checkbox_->isChecked() || !lsl_shadow_compatible_ ||
		!lsl_shadow_pipeline_ || !lsl_shadow_pipeline_->hasClassifier() || frames.empty()) return;
	const int model_rate = lsl_shadow_pipeline_->outputRateHz();
	if(model_rate <= 0 || lsl_shadow_input_rate_hz_ <= 0) return;
	if(frames.size() > static_cast<std::size_t>(lsl_shadow_input_rate_hz_ * 4))
	{
		resetLslShadowPipeline(QString(), true, neurocontrol::LslShadowResetReason::QueueOverflow);
		lsl_shadow_status_label_->setText(tr("Буфер теневой классификации сброшен: очередь превысила 4 секунды. График и запись LSL продолжаются отдельно; оценка начнётся с новых отсчётов."));
		return;
	}
	std::vector<neurocontrol::EegFrame> chunk(frames.begin(), frames.end());
	lsl_shadow_arrival_clock_.restart();
	lsl_shadow_stalled_ = false;
	const std::uint64_t invalid_resets_before = lsl_shadow_pipeline_->stats().invalid_frame_resets;
	neurocontrol::LslShadowPipelineBatch batch;
	std::string processing_error;
	const bool valid = lsl_shadow_pipeline_->processChunk(chunk.data(), chunk.size(), batch, processing_error);
	const auto& stats = lsl_shadow_pipeline_->stats();
	lsl_shadow_windows_ = stats.classifier_windows;
	if(stats.timestamp_gap_resets > lsl_shadow_timestamp_gaps_ || stats.invalid_frame_resets > invalid_resets_before)
		lsl_shadow_prediction_ = {};
	lsl_shadow_timestamp_gaps_ = stats.timestamp_gap_resets;
	lsl_shadow_last_processing_ms_ = stats.last_processing_batch_ms;
	if(!valid && lsl_shadow_status_label_)
		lsl_shadow_status_label_->setText(tr("Теневая обработка сбросила некорректные кадры; график и запись продолжаются. %1")
			.arg(QString::fromStdString(processing_error)));
	for(const neurocontrol::Classification& result : batch.classifications)
	{
			const QString candidate = !result.class_confident ? tr("неуверенно") :
				result.candidate_command == model_positive_command_ ? model_positive_label_ :
				result.candidate_command == model_negative_command_ ? model_negative_label_ : neuroCommandName(result.candidate_command);
			lsl_shadow_prediction_ = QJsonObject{
				{QStringLiteral("available"), true}, {QStringLiteral("candidate_class"), candidate},
				{QStringLiteral("candidate_command"), neuroCommandName(result.candidate_command)},
				{QStringLiteral("classification_command"), neuroCommandName(result.command)},
				{QStringLiteral("positive_probability"), result.forward_probability},
				{QStringLiteral("signal_good"), result.signal_good},
				{QStringLiteral("class_confident"), result.class_confident},
				{QStringLiteral("awaiting_confirmation"), result.awaiting_confirmation},
				{QStringLiteral("consecutive_candidate_windows"), result.consecutive_candidate_windows},
				{QStringLiteral("sample_index"), static_cast<qint64>(result.sample_index)},
				{QStringLiteral("window_count"), static_cast<qint64>(lsl_shadow_windows_)},
				{QStringLiteral("receiver_display_gap_resets"), static_cast<qint64>(lsl_shadow_timestamp_gaps_)},
				{QStringLiteral("input_sample_rate_hz"), lsl_shadow_input_rate_hz_},
				{QStringLiteral("model_sample_rate_hz"), model_rate},
				{QStringLiteral("resample_up"), lsl_shadow_pipeline_->resampleUp()},
				{QStringLiteral("resample_down"), lsl_shadow_pipeline_->resampleDown()},
				{QStringLiteral("resampler_group_delay_ms"), lsl_shadow_pipeline_->groupDelayMilliseconds()},
				{QStringLiteral("input_samples"), static_cast<qint64>(stats.input_samples)},
				{QStringLiteral("output_samples"), static_cast<qint64>(stats.output_samples)},
				{QStringLiteral("pipeline_errors"), static_cast<qint64>(stats.errors)},
				{QStringLiteral("montage_reference_state"), lsl_shadow_montage_reference_state_},
				{QStringLiteral("source_timestamp_quality"), lslStatusJson().value(QStringLiteral("timestamp_quality")).toString()},
				{QStringLiteral("interpretation_status"), QStringLiteral("diagnostic_only_not_validated_for_source")},
				{QStringLiteral("avatar_command_sent"), false}};
			if(lsl_shadow_status_label_)
				lsl_shadow_status_label_->setText(tr("Теневая оценка: %1 · p(%2) = %3 · окно %4 · аватар не управляется")
					.arg(result.signal_good ? candidate : tr("сигнал ненадёжен"), model_positive_label_)
					.arg(result.forward_probability, 0, 'f', 3).arg(lsl_shadow_windows_));
	}
	if(!lsl_shadow_prediction_.isEmpty())
		lsl_shadow_prediction_.insert(QStringLiteral("processing_batch_ms"), lsl_shadow_last_processing_ms_);
}

void NeuroControlWidget::startLslShadowRecordingAudit(const QString& recording_id, int target_sample_rate_hz)
{
	if(lsl_shadow_audit_)
	{
		std::lock_guard<std::mutex> lock(lsl_shadow_audit_->mutex);
		if(!lsl_shadow_audit_->complete) return;
	}
	auto state = std::make_shared<NeuroControlLslShadowAudit>();
	state->recording_id = recording_id;
	state->target_sample_rate_hz = target_sample_rate_hz;
	lsl_shadow_audit_ = state;
	lsl_shadow_audit_state_ = QStringLiteral("queued");
	const QString directory = lsl_recordings_directory_;
	const QString metadata_path = QDir(directory).filePath(recording_id + QStringLiteral(".json"));
	const std::shared_ptr<neurocontrol::Model> model = lsl_shadow_model_;
	const QJsonObject model_metadata = model_metadata_;
	auto* thread = QThread::create([state, directory, metadata_path, recording_id, target_sample_rate_hz, model, model_metadata] {
		const auto finish = [state](const QJsonObject& result) {
			std::lock_guard<std::mutex> lock(state->mutex);
			state->result = result;
			state->state = result.value(QStringLiteral("state")).toString(QStringLiteral("error"));
			state->complete = true;
		};
		{
			std::lock_guard<std::mutex> lock(state->mutex);
			state->state = QStringLiteral("running");
		}
		QString error;
		QString integrity_status = QStringLiteral("checking");
		QString integrity_reason = QStringLiteral("sidecar_and_binary_checks_in_progress");
		const auto setIntegrityFailure = [&](const QString& code, const QString& detail, bool checked = true) {
			integrity_status = checked ? QStringLiteral("failed") : QStringLiteral("not_checked");
			integrity_reason = code + (detail.isEmpty() ? QString() : QStringLiteral(": ") + detail);
			error = detail;
		};
		const QFileInfo metadata_info(metadata_path);
		const QString canonical_metadata_path = metadata_info.canonicalFilePath();
		if(!metadata_info.isFile() || !pathIsWithin(directory, canonical_metadata_path))
			setIntegrityFailure(QStringLiteral("sidecar_missing_or_outside_recording_root"), QStringLiteral("The recording metadata is missing or outside the local recording directory."));
		QFile metadata_file(canonical_metadata_path);
		QJsonObject metadata;
		if(error.isEmpty() && metadata_info.size() > 1024 * 1024)
			setIntegrityFailure(QStringLiteral("sidecar_size_limit_exceeded"), QStringLiteral("The metadata sidecar exceeds the 1 MiB audit limit."), false);
		if(error.isEmpty() && !metadata_file.open(QIODevice::ReadOnly))
			setIntegrityFailure(QStringLiteral("sidecar_read_failed"), QStringLiteral("Cannot read the LSL recording metadata sidecar."));
		if(error.isEmpty())
		{
			QJsonParseError parse_error;
			const QJsonDocument document = QJsonDocument::fromJson(metadata_file.readAll(), &parse_error);
			if(parse_error.error != QJsonParseError::NoError || !document.isObject())
				setIntegrityFailure(QStringLiteral("sidecar_json_invalid"), QStringLiteral("The metadata sidecar is not a valid JSON object."));
			else metadata = document.object();
		}
		if(error.isEmpty() && (metadata.value(QStringLiteral("format")).toString() != QLatin1String("metasiberia-neurocontrol-lsl-recording-v1") ||
			metadata.value(QStringLiteral("recording_id")).toString() != recording_id ||
			metadata.value(QStringLiteral("status")).toString() != QLatin1String("complete") || !metadata.value(QStringLiteral("complete")).toBool()))
			setIntegrityFailure(QStringLiteral("sidecar_incomplete_or_unsupported"), QStringLiteral("The LSL recording is incomplete or uses an unsupported sidecar format."));
		const QString data_name = metadata.value(QStringLiteral("data_file")).toString();
		QString data_path;
		if(error.isEmpty() && (QFileInfo(data_name).fileName() != data_name || !data_name.endsWith(QStringLiteral(".nlsr"))))
			setIntegrityFailure(QStringLiteral("sidecar_binary_filename_invalid"), QStringLiteral("The sidecar contains an unsafe binary filename."));
		if(error.isEmpty())
		{
			const QFileInfo binary_info(QDir(directory).filePath(data_name));
			data_path = binary_info.canonicalFilePath();
			if(!binary_info.isFile() || !pathIsWithin(directory, data_path))
				setIntegrityFailure(QStringLiteral("binary_missing_or_outside_recording_root"), QStringLiteral("The recording binary is missing or outside the local recording directory."));
			else if(binary_info.size() > 128ll * 1024ll * 1024ll)
				setIntegrityFailure(QStringLiteral("binary_size_limit_exceeded"), QStringLiteral("Offline audit is bounded to recording files no larger than 128 MiB."), false);
		}
		neurocontrol::LslRecordingReader reader;
		neurocontrol::LslRecordingInfo info;
		std::string recording_error;
		if(error.isEmpty() && !reader.open(std::filesystem::path(data_path.toStdWString()), info, recording_error))
			setIntegrityFailure(QStringLiteral("binary_header_or_layout_invalid"), QString::fromStdString(recording_error));
		if(error.isEmpty() && !info.complete)
			setIntegrityFailure(QStringLiteral("binary_incomplete"), QStringLiteral("The .nlsr complete marker or exact record count is invalid."));
		if(error.isEmpty() && !reader.verify(info, recording_error))
			setIntegrityFailure(QStringLiteral("binary_payload_checksum_failed"), QString::fromStdString(recording_error));
		if(error.isEmpty())
		{
			bool checksum_ok = false;
			const qulonglong sidecar_checksum = metadata.value(QStringLiteral("binary_checksum_fnv1a64")).toString().toULongLong(&checksum_ok, 16);
			if(!checksum_ok || sidecar_checksum != info.checksum)
				setIntegrityFailure(QStringLiteral("sidecar_checksum_mismatch"), QStringLiteral("The binary header checksum does not match the sidecar checksum."));
		}
		const QJsonArray channel_metadata = metadata.value(QStringLiteral("channels")).toArray();
		const std::uint64_t sidecar_samples = metadata.value(QStringLiteral("sample_count")).toVariant().toULongLong();
		const double nominal_rate = metadata.value(QStringLiteral("nominal_rate_hz")).toDouble();
		if(error.isEmpty() && (channel_metadata.size() != static_cast<int>(info.channel_count) || sidecar_samples != info.sample_count ||
			!std::isfinite(nominal_rate) || nominal_rate < 8.0 || nominal_rate > 4000.0 || std::abs(nominal_rate - std::round(nominal_rate)) > 0.01))
			setIntegrityFailure(QStringLiteral("sidecar_metadata_mismatch"), QStringLiteral("The sidecar channel/sample counts or nominal sample rate do not match the binary recording."));
		if(error.isEmpty())
		{
			integrity_status = QStringLiteral("verified");
			integrity_reason = QStringLiteral("header, complete marker, exact record count, payload FNV-1a-64, sidecar checksum and metadata all match");
		}
		if(!error.isEmpty())
		{
			finish(QJsonObject{{QStringLiteral("state"), QStringLiteral("error")}, {QStringLiteral("recording_id"), recording_id},
				{QStringLiteral("error"), error}, {QStringLiteral("integrity_status"), integrity_status},
				{QStringLiteral("integrity_reason"), integrity_reason}, {QStringLiteral("integrity_verified"), integrity_status == QLatin1String("verified")},
				{QStringLiteral("target_sample_rate_hz"), target_sample_rate_hz},
				{QStringLiteral("classifier_ran"), false}, {QStringLiteral("avatar_command_sent"), false}});
			return;
		}

		const int input_rate = static_cast<int>(std::llround(nominal_rate));
		const bool model_available = model && model->valid();
		const int model_rate = model_available ? model->sample_rate_hz : 0;
		std::vector<std::size_t> eeg_indices;
		std::vector<double> unit_factors(info.channel_count, 0.0);
		neurocontrol::LslShadowCompatibilityRequest compatibility_request;
		compatibility_request.advertised_rate_hz = nominal_rate;
		compatibility_request.model_rate_hz = model_available ? model_rate : target_sample_rate_hz;
		compatibility_request.model_channels = model_available ? model->channels : std::vector<std::string>{};
		compatibility_request.source_montage = metadata.value(QStringLiteral("electrode_montage")).toString().toUtf8().toStdString();
		compatibility_request.source_reference = metadata.value(QStringLiteral("reference_scheme")).toString().toUtf8().toStdString();
		const QJsonObject preprocessing = model_metadata.value(QStringLiteral("preprocessing")).toObject();
		compatibility_request.model_montage = model_metadata.value(QStringLiteral("electrode_montage")).toString(preprocessing.value(QStringLiteral("electrode_montage")).toString()).toUtf8().toStdString();
		compatibility_request.model_reference = model_metadata.value(QStringLiteral("reference_scheme")).toString(
			model_metadata.value(QStringLiteral("reference")).toString(preprocessing.value(QStringLiteral("reference_scheme")).toString())).toUtf8().toStdString();
		for(int i = 0; i < channel_metadata.size(); ++i)
		{
			const QJsonObject channel = channel_metadata.at(i).toObject();
			const QString name = channel.value(QStringLiteral("name")).toString();
			const QString type = channel.value(QStringLiteral("display_type")).toString(channel.value(QStringLiteral("type")).toString());
			const QString unit = channel.value(QStringLiteral("effective_unit")).toString(channel.value(QStringLiteral("unit")).toString());
			unit_factors[static_cast<std::size_t>(i)] = channel.value(QStringLiteral("unit_to_volts")).toDouble(0.0);
			compatibility_request.source_channels.push_back({name.toUtf8().toStdString(), type.toUtf8().toStdString(), unit.toUtf8().toStdString()});
			if(lslChannelKind(name, type) == QLatin1String("EEG"))
			{
				if(!std::isfinite(unit_factors[static_cast<std::size_t>(i)]) || unit_factors[static_cast<std::size_t>(i)] <= 0.0)
				{
					error = QStringLiteral("An EEG channel has an invalid recorded unit conversion.");
					break;
				}
				eeg_indices.push_back(static_cast<std::size_t>(i));
			}
		}
		if(error.isEmpty() && eeg_indices.empty()) error = QStringLiteral("The recording contains no EEG channels for resampler audit.");
		if(!error.isEmpty())
		{
			finish(QJsonObject{{QStringLiteral("state"), QStringLiteral("error")}, {QStringLiteral("recording_id"), recording_id},
				{QStringLiteral("error"), error}, {QStringLiteral("integrity_status"), integrity_status},
				{QStringLiteral("integrity_reason"), integrity_reason}, {QStringLiteral("integrity_verified"), integrity_status == QLatin1String("verified")},
				{QStringLiteral("target_sample_rate_hz"), target_sample_rate_hz},
				{QStringLiteral("classifier_ran"), false}, {QStringLiteral("avatar_command_sent"), false}});
			return;
		}
		neurocontrol::LslShadowCompatibility compatibility;
		if(model_available) compatibility = neurocontrol::evaluateLslShadowCompatibility(compatibility_request);
		else compatibility.reason_codes.emplace_back("model_not_loaded");
		neurocontrol::LslShadowPipeline pipeline;
		if(!pipeline.configure(input_rate, target_sample_rate_hz, info.channel_count, eeg_indices, nullptr, recording_error))
		{
			finish(QJsonObject{{QStringLiteral("state"), QStringLiteral("error")}, {QStringLiteral("recording_id"), recording_id},
				{QStringLiteral("error"), QString::fromStdString(recording_error)}, {QStringLiteral("integrity_status"), integrity_status},
				{QStringLiteral("integrity_reason"), integrity_reason}, {QStringLiteral("integrity_verified"), integrity_status == QLatin1String("verified")},
				{QStringLiteral("target_sample_rate_hz"), target_sample_rate_hz},
				{QStringLiteral("classifier_ran"), false}, {QStringLiteral("avatar_command_sent"), false}});
			return;
		}
		std::uint64_t output_count = 0, invalid_input = 0, nonfinite_output = 0;
		std::uint64_t source_repeated = 0, source_backwards = 0, source_nonfinite = 0;
		std::uint64_t arrival_repeated = 0, arrival_backwards = 0, arrival_nonfinite = 0;
		double previous_source = std::numeric_limits<double>::quiet_NaN();
		double previous_arrival = std::numeric_limits<double>::quiet_NaN();
		double first_source = std::numeric_limits<double>::quiet_NaN(), last_source = std::numeric_limits<double>::quiet_NaN();
		double first_arrival = std::numeric_limits<double>::quiet_NaN(), last_arrival = std::numeric_limits<double>::quiet_NaN();
		double output_power = 0.0;
		const auto processing_start = std::chrono::steady_clock::now();
		for(std::uint64_t offset = 0; offset < info.sample_count; offset += 2048)
		{
			const std::uint64_t count = std::min<std::uint64_t>(2048, info.sample_count - offset);
			std::vector<neurocontrol::EegFrame> frames;
			if(!reader.readRange(offset, count, frames, recording_error))
			{
				error = QString::fromStdString(recording_error);
				break;
			}
			for(std::size_t i = 0; i < frames.size(); ++i)
			{
				auto& frame = frames[i];
				if(!frame.raw_source_values_exact || frame.raw_source_values.size() != info.channel_count)
				{
					error = QStringLiteral("A binary frame does not contain exact float32 samples.");
					break;
				}
				if(!std::isfinite(frame.source_timestamp_seconds)) ++source_nonfinite;
				else if(!std::isfinite(first_source)) first_source = frame.source_timestamp_seconds;
				if(std::isfinite(frame.source_timestamp_seconds)) last_source = frame.source_timestamp_seconds;
				if(std::isfinite(frame.source_timestamp_seconds) && std::isfinite(previous_source))
				{
					if(frame.source_timestamp_seconds == previous_source) ++source_repeated;
					else if(frame.source_timestamp_seconds < previous_source) ++source_backwards;
				}
				if(std::isfinite(frame.source_timestamp_seconds)) previous_source = frame.source_timestamp_seconds;
				if(!std::isfinite(frame.arrival_monotonic_seconds)) ++arrival_nonfinite;
				else if(!std::isfinite(first_arrival)) first_arrival = frame.arrival_monotonic_seconds;
				if(std::isfinite(frame.arrival_monotonic_seconds)) last_arrival = frame.arrival_monotonic_seconds;
				if(std::isfinite(frame.arrival_monotonic_seconds) && std::isfinite(previous_arrival))
				{
					if(frame.arrival_monotonic_seconds == previous_arrival) ++arrival_repeated;
					else if(frame.arrival_monotonic_seconds < previous_arrival) ++arrival_backwards;
				}
				if(std::isfinite(frame.arrival_monotonic_seconds)) previous_arrival = frame.arrival_monotonic_seconds;
				frame.volts.assign(info.channel_count, 0.0f);
				frame.signal_good = true;
				for(std::size_t channel = 0; channel < info.channel_count; ++channel)
				{
					const double value = static_cast<double>(frame.raw_source_values[channel]) * unit_factors[channel];
					if(std::find(eeg_indices.begin(), eeg_indices.end(), channel) != eeg_indices.end())
					{
						if(!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max())
						{
							++invalid_input;
							frame.signal_good = false;
						}
						else frame.volts[channel] = static_cast<float>(value);
					}
				}
				frame.display_timestamp_seconds = static_cast<double>(offset + i) / input_rate;
			}
			if(!error.isEmpty()) break;
			neurocontrol::LslShadowPipelineBatch batch;
			std::string batch_error;
			if(!pipeline.processChunk(frames.data(), frames.size(), batch, batch_error))
			{
				error = QString::fromStdString(batch_error.empty() ? "The shared LSL processing pipeline rejected a recording chunk." : batch_error);
				break;
			}
			for(const auto& sample : batch.resampled_samples)
			{
				++output_count;
				for(float value : sample)
				{
					if(!std::isfinite(value)) ++nonfinite_output;
					else output_power += static_cast<double>(value) * value;
				}
			}
		}
		const double processing_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - processing_start).count();
		const auto& stats = pipeline.stats();
		const double duration = static_cast<double>(info.sample_count) / input_rate;
		const double output_rms_volts = output_count > 0 && !eeg_indices.empty() ? std::sqrt(output_power / (output_count * eeg_indices.size())) : 0.0;
		QJsonArray reason_codes;
		for(const std::string& code : compatibility.reason_codes) reason_codes.append(QString::fromStdString(code));
		const bool static_contract_passed = model_available && compatibility.resampling_supported &&
			compatibility.channel_indices.size() == compatibility_request.model_channels.size() &&
			std::all_of(compatibility.reason_codes.begin(), compatibility.reason_codes.end(), [](const std::string& code) { return code == "receive_rate_pending"; });
		QJsonArray model_reason_codes;
		for(const std::string& code : compatibility.reason_codes) model_reason_codes.append(QString::fromStdString(code));
		QJsonObject result{
			{QStringLiteral("state"), error.isEmpty() ? QStringLiteral("completed") : QStringLiteral("error")},
			{QStringLiteral("recording_id"), recording_id}, {QStringLiteral("error"), error},
			{QStringLiteral("integrity_status"), integrity_status}, {QStringLiteral("integrity_reason"), integrity_reason},
			{QStringLiteral("integrity_verified"), integrity_status == QLatin1String("verified")},
			{QStringLiteral("input_rate_hz_nominal"), input_rate}, {QStringLiteral("target_sample_rate_hz"), target_sample_rate_hz},
			{QStringLiteral("output_rate_hz"), target_sample_rate_hz},
			{QStringLiteral("model_sample_rate_hz"), model_available ? QJsonValue(model_rate) : QJsonValue(QJsonValue::Null)},
			{QStringLiteral("input_samples"), static_cast<qint64>(info.sample_count)},
			{QStringLiteral("output_samples"), static_cast<qint64>(output_count)},
			{QStringLiteral("duration_seconds_nominal"), duration},
			{QStringLiteral("source_timestamp_elapsed_seconds"), std::isfinite(first_source) && std::isfinite(last_source) ? QJsonValue(last_source - first_source) : QJsonValue(QJsonValue::Null)},
			{QStringLiteral("receiver_arrival_elapsed_seconds"), std::isfinite(first_arrival) && std::isfinite(last_arrival) ? QJsonValue(last_arrival - first_arrival) : QJsonValue(QJsonValue::Null)},
			{QStringLiteral("effective_output_rate_hz_nominal_timeline"), duration > 0.0 ? output_count / duration : 0.0},
			{QStringLiteral("channel_count"), static_cast<int>(info.channel_count)},
			{QStringLiteral("eeg_channel_count"), static_cast<int>(eeg_indices.size())},
			{QStringLiteral("resample_up"), pipeline.resampleUp()}, {QStringLiteral("resample_down"), pipeline.resampleDown()},
			{QStringLiteral("resampler_filter_taps"), static_cast<qint64>(pipeline.filterLength())},
			{QStringLiteral("resampler_configured"), pipeline.isConfigured()},
			{QStringLiteral("fir_group_delay_ms"), pipeline.groupDelayMilliseconds()},
			{QStringLiteral("processing_ms"), processing_ms}, {QStringLiteral("output_rms_volts"), output_rms_volts},
			{QStringLiteral("invalid_input_values"), static_cast<qint64>(invalid_input)},
			{QStringLiteral("nonfinite_output_values"), static_cast<qint64>(nonfinite_output)},
			{QStringLiteral("pipeline_errors"), static_cast<qint64>(stats.errors)},
			{QStringLiteral("pipeline_timestamp_gap_resets"), static_cast<qint64>(stats.timestamp_gap_resets)},
			{QStringLiteral("pipeline_invalid_frame_resets"), static_cast<qint64>(stats.invalid_frame_resets)},
			{QStringLiteral("source_timestamp_repeats"), static_cast<qint64>(source_repeated)},
			{QStringLiteral("source_timestamp_backwards"), static_cast<qint64>(source_backwards)},
			{QStringLiteral("source_timestamp_nonfinite"), static_cast<qint64>(source_nonfinite)},
			{QStringLiteral("arrival_timestamp_repeats"), static_cast<qint64>(arrival_repeated)},
			{QStringLiteral("arrival_timestamp_backwards"), static_cast<qint64>(arrival_backwards)},
			{QStringLiteral("arrival_timestamp_nonfinite"), static_cast<qint64>(arrival_nonfinite)},
			{QStringLiteral("timestamp_basis"), QStringLiteral("Processing uses a derived sample_index / nominal_rate timeline; original timestamps remain diagnostic only and do not validate sample production rate.")},
			{QStringLiteral("receive_rate_measurable_from_archive"), false},
			{QStringLiteral("compatibility_static_check_passed"), static_contract_passed},
			{QStringLiteral("compatibility_state"), static_contract_passed ? QStringLiteral("static_contract_passed_live_rate_validation_pending") : QStringLiteral("blocked_or_model_unavailable")},
			{QStringLiteral("model_compatibility_state"), !model_available ? QStringLiteral("not_loaded") :
				(static_contract_passed ? QStringLiteral("static_contract_passed_live_rate_pending") : QStringLiteral("blocked"))},
			{QStringLiteral("model_compatibility_reason_codes"), model_reason_codes},
			{QStringLiteral("compatibility_montage_reference_state"), QString::fromStdString(compatibility.montage_reference_state)},
			{QStringLiteral("compatibility_reason_codes"), reason_codes},
			{QStringLiteral("classification_state"), QStringLiteral("not_run_offline_classifier_disabled")},
			{QStringLiteral("classifier_ran"), false}, {QStringLiteral("avatar_command_sent"), false},
			{QStringLiteral("raw_eeg_values_returned"), false}};
		finish(result);
	});
	connect(thread, &QThread::finished, thread, &QObject::deleteLater);
	thread->start();
}

QJsonObject NeuroControlWidget::lslStatusJson() const
{
	const bool reader_exists = static_cast<bool>(lsl_reader_);
	bool reader_finished = !reader_exists;
	bool reader_stop_requested = false;
	if(lsl_reader_)
	{
		std::lock_guard<std::mutex> lock(lsl_reader_->mutex);
		reader_finished = lsl_reader_->finished;
		reader_stop_requested = lsl_reader_->stop.load();
	}
	QJsonArray discovered;
	for(const auto& stream : lsl_discovered_)
	{
		QJsonArray stream_channels, stream_units;
		for(const QString& channel : stream.channels) stream_channels.append(channel);
		for(const QString& unit : stream.units) stream_units.append(unit);
		discovered.append(QJsonObject{{"uid", stream.uid}, {"source_id", stream.source_id},
			{"name", stream.name}, {"type", stream.type}, {"hostname", stream.hostname},
			{"channel_count", stream.channel_count}, {"channels", stream_channels}, {"units", stream_units},
			{"electrode_montage", stream.electrode_montage}, {"reference_scheme", stream.reference_scheme},
			{"nominal_rate_hz", stream.nominal_hz}, {"format", stream.format}});
	}
	QJsonArray channels;
	for(int i = 0; i < lsl_active_stream_.channel_count; ++i)
		channels.append(QJsonObject{{"index", i}, {"name", lsl_active_stream_.channels.value(i)},
			{"type", lsl_active_stream_.channel_types.value(i)}, {"unit", lsl_active_stream_.units.value(i)}});
	const auto& d = lsl_timestamp_diagnostics_;
	const bool coarse = d.repeated > 0 || d.backwards > 0 || d.nonfinite > 0 || d.coarse_intervals > 0;
	const QString quality = d.received == 0 ? QStringLiteral("unknown") :
		coarse && d.correction_available && !d.clock_compatible ? QStringLiteral("coarse_and_clock_incompatible") :
		coarse ? QStringLiteral("insufficient_resolution_or_nonmonotonic") :
		d.correction_available && !d.clock_compatible ? QStringLiteral("clock_incompatible") :
		d.correction_available ? QStringLiteral("plausible") : QStringLiteral("clock_comparison_unavailable");
	QJsonObject diagnostic = lsl_diagnostic_result_;
	if(lsl_diagnostic_running_)
		diagnostic = QJsonObject{{"state", "running"}, {"requested_duration_seconds", lsl_diagnostic_duration_seconds_},
			{"elapsed_seconds", lsl_diagnostic_elapsed_.isValid() ? lsl_diagnostic_elapsed_.elapsed() / 1000.0 : 0.0},
			{"received_samples", static_cast<qint64>(received_samples_ - lsl_diagnostic_start_received_)},
			{"started_at_utc", lsl_diagnostic_started_at_utc_}, {"auto_disconnect", true}};
	QJsonArray shadow_reason_codes;
	for(const QString& code : lsl_shadow_reason_codes_) shadow_reason_codes.append(code);
	const neurocontrol::LslShadowPipelineStats empty_shadow_stats;
	const neurocontrol::LslShadowPipelineStats& shadow_stats = lsl_shadow_pipeline_ ? lsl_shadow_pipeline_->stats() : empty_shadow_stats;
	const bool shadow_enabled = lsl_shadow_checkbox_ && lsl_shadow_checkbox_->isChecked();
	const QString shadow_pipeline_state = lsl_shadow_compatible_ ?
		(shadow_enabled ? QStringLiteral("active") : QStringLiteral("compatible_not_enabled")) : QStringLiteral("blocked");
	QJsonObject shadow_pipeline{
		{"state", shadow_pipeline_state}, {"input_rate_hz", lsl_shadow_input_rate_hz_},
		{"observed_receive_rate_hz", lsl_shadow_observed_rate_hz_},
		{"output_rate_hz", lsl_shadow_pipeline_ ? lsl_shadow_pipeline_->outputRateHz() : model_metadata_.value(QStringLiteral("sample_rate_hz")).toInt()},
		{"resampler_configured", lsl_shadow_pipeline_ && lsl_shadow_pipeline_->isConfigured()},
		{"resample_up", lsl_shadow_pipeline_ ? lsl_shadow_pipeline_->resampleUp() : 0},
		{"resample_down", lsl_shadow_pipeline_ ? lsl_shadow_pipeline_->resampleDown() : 0},
		{"group_delay_ms", lsl_shadow_pipeline_ ? lsl_shadow_pipeline_->groupDelayMilliseconds() : 0.0},
		{"input_samples", static_cast<qint64>(shadow_stats.input_samples)},
		{"output_samples", static_cast<qint64>(shadow_stats.output_samples)},
		{"classification_windows", static_cast<qint64>(shadow_stats.classifier_windows)},
		{"errors", static_cast<qint64>(shadow_stats.errors)}, {"last_error", QString::fromStdString(shadow_stats.last_error)},
		{"timestamp_gap_resets", static_cast<qint64>(shadow_stats.timestamp_gap_resets)},
		{"invalid_frame_resets", static_cast<qint64>(shadow_stats.invalid_frame_resets)},
		{"queue_overflow_resets", static_cast<qint64>(shadow_stats.queue_overflow_resets)},
		{"stall_resets", static_cast<qint64>(shadow_stats.stall_resets)},
		{"connection_loss_resets", static_cast<qint64>(shadow_stats.connection_loss_resets)},
		{"reconnect_resets", static_cast<qint64>(shadow_stats.reconnect_resets)},
		{"manual_resets", static_cast<qint64>(shadow_stats.manual_resets)},
		{"last_processing_batch_ms", shadow_stats.last_processing_batch_ms},
		{"receive_rate_stable", lsl_shadow_rate_stable_}, {"compatible", lsl_shadow_compatible_},
		{"compatibility_reason", lsl_shadow_compatibility_reason_}, {"reason_codes", shadow_reason_codes},
		{"montage_reference_state", lsl_shadow_montage_reference_state_},
		{"classifier_ready", lsl_shadow_pipeline_ && lsl_shadow_pipeline_->hasClassifier()},
		{"classifier_active", shadow_enabled && lsl_shadow_pipeline_ && lsl_shadow_pipeline_->hasClassifier()},
		{"avatar_command_sent", false}};
	QJsonObject offline_shadow_audit{{"state", lsl_shadow_audit_state_}, {"integrity_status", "not_checked"}};
	if(lsl_shadow_audit_)
	{
		std::lock_guard<std::mutex> lock(lsl_shadow_audit_->mutex);
		offline_shadow_audit = lsl_shadow_audit_->result;
		if(offline_shadow_audit.isEmpty()) offline_shadow_audit = QJsonObject{{"state", lsl_shadow_audit_->state},
			{"recording_id", lsl_shadow_audit_->recording_id}, {"target_sample_rate_hz", lsl_shadow_audit_->target_sample_rate_hz},
			{"integrity_status", lsl_shadow_audit_->complete ? QStringLiteral("not_checked") : QStringLiteral("checking")}};
	}
	return QJsonObject{{"state", lsl_connection_state_}, {"search_in_progress", lsl_discovery_ != nullptr},
		{"reader_worker_exists", reader_exists}, {"reader_worker_finished", reader_finished},
		{"reader_stop_requested", reader_stop_requested}, {"reader_cleanup_pending", reader_exists && !reader_finished},
		{"library_state", lsl_library_state_}, {"library_ready", lsl_library_state_ == QLatin1String("ready")},
		{"library_path", lsl_library_path_}, {"library_sha256", lsl_library_sha256_},
		{"library_provenance", lsl_library_provenance_}, {"library_error", lsl_library_error_},
		{"discovery_state", lsl_discovery_state_}, {"discovery_message", lsl_discovery_message_},
		{"configuration_in_progress", lsl_library_configuration_ != nullptr}, {"discovered_streams", discovered},
		{"name", lsl_active_stream_.name},
		{"source_id", lsl_active_stream_.source_id}, {"uid", lsl_active_stream_.uid},
		{"channel_count", lsl_active_stream_.channel_count}, {"channels", channels},
		{"nominal_rate_hz", lsl_active_stream_.nominal_hz}, {"measured_receive_rate_hz", receive_rate_hz_},
		{"electrode_montage", lsl_active_stream_.electrode_montage},
		{"reference_scheme", lsl_active_stream_.reference_scheme},
		{"effective_units_override", lsl_active_units_override_},
		{"shadow_observed_receive_rate_hz", lsl_shadow_observed_rate_hz_},
		{"shadow_receive_rate_sample_count", static_cast<qint64>(lsl_shadow_rate_history_.size())},
		{"shadow_receive_rate_stable", lsl_shadow_rate_stable_},
		{"shadow_windows", static_cast<qint64>(lsl_shadow_windows_)},
		{"shadow_timestamp_gap_resets", static_cast<qint64>(lsl_shadow_timestamp_gaps_)},
		{"shadow_stall_events", static_cast<qint64>(lsl_shadow_stall_events_)},
		{"shadow_last_processing_batch_ms", lsl_shadow_last_processing_ms_},
		{"format", lsl_active_stream_.format}, {"native_float32_exact_available", lsl_active_stream_.native_float32_exact_available},
		{"received_samples", static_cast<qint64>(received_samples_)},
		{"local_display_buffer_drops", static_cast<qint64>(dropped_samples_)},
		{"pause_events", static_cast<qint64>(lsl_pause_events_)}, {"reconnects", static_cast<qint64>(lsl_reconnects_)},
		{"errors", static_cast<qint64>(lsl_errors_)}, {"last_error_or_event", lsl_error_},
		{"timestamp_quality", quality}, {"source_timestamp_seconds", d.last_source_seconds},
		{"receiver_arrival_monotonic_seconds", d.last_arrival_seconds},
		{"source_timestamp_repeats", static_cast<qint64>(d.repeated)},
		{"source_timestamp_backwards", static_cast<qint64>(d.backwards)},
		{"source_timestamp_nonfinite", static_cast<qint64>(d.nonfinite)},
		{"source_timestamp_coarse_intervals", static_cast<qint64>(d.coarse_intervals)},
		{"clock_correction_available", d.correction_available}, {"clock_compatible", d.clock_compatible},
		{"diagnostic", diagnostic},
		{"raw_eeg_contents_sent", false}, {"classifier_connected", false}, {"avatar_control_enabled", false},
		{"shadow_classification_compatible", lsl_shadow_compatible_},
		{"shadow_classification_enabled", lsl_shadow_checkbox_ && lsl_shadow_checkbox_->isChecked()},
		{"shadow_classification_reason", lsl_shadow_compatibility_reason_},
		{"shadow_reason_codes", shadow_reason_codes}, {"shadow_pipeline", shadow_pipeline},
		{"offline_recording_audit", offline_shadow_audit}, {"shadow_prediction", lsl_shadow_prediction_}};
}


void NeuroControlWidget::startExperimentSession()
{
	if(!experiment_logger_) return;
	const int source_index = source_ ? source_->currentIndex() : -1;
	const QString source_id = source_index == 0 ? QStringLiteral("recorded_edf") :
		source_index == 1 ? QStringLiteral("brainflow_synthetic") : QStringLiteral("unavailable");
	const QJsonObject recording_metadata = loaded_recording_metadata_path_.isEmpty() ? QJsonObject{} :
		readJsonObjectFile(loaded_recording_metadata_path_);
	QJsonArray channels;
	for(const std::string& name : recording_channels_) channels.append(QString::fromStdString(name));
	QJsonObject recorded_source{{QStringLiteral("source_file"), recording_metadata.value(QStringLiteral("source_file"))},
		{QStringLiteral("source_sha256"), recording_metadata.value(QStringLiteral("source_sha256"))},
		{QStringLiteral("prepared_data_sha256"), recording_metadata.value(QStringLiteral("data_sha256"))},
		{QStringLiteral("samples"), recording_metadata.value(QStringLiteral("samples"))},
		{QStringLiteral("sample_rate_hz"), recording_metadata.value(QStringLiteral("sample_rate_hz"))},
		{QStringLiteral("annotations"), recording_metadata.value(QStringLiteral("annotations"))},
		{QStringLiteral("preprocessing"), recording_metadata.value(QStringLiteral("preprocessing"))}};
	const QString recording_name = source_index == 1 ? QStringLiteral("BrainFlow Synthetic Board") :
		recording_metadata.value(QStringLiteral("source_file")).toString(QFileInfo(loaded_recording_metadata_path_).completeBaseName());
	const double playback_speed = source_index == 0 && playback_speed_ ? playback_speed_->currentData().toDouble() : 1.0;
	const QJsonObject classifier_settings{
		{QStringLiteral("algorithm"), QStringLiteral("CSP + LDA")},
		{QStringLiteral("positive_class"), model_positive_label_},
		{QStringLiteral("negative_class"), model_negative_label_},
		{QStringLiteral("positive_class_command"), neuroCommandName(model_positive_command_)},
		{QStringLiteral("negative_class_command"), neuroCommandName(model_negative_command_)},
		{QStringLiteral("forward_confirmation_windows"), 2},
		{QStringLiteral("stop_confirmation_windows"), 1},
		{QStringLiteral("stop_is_immediate"), true},
		{QStringLiteral("window_samples"), model_metadata_.value(QStringLiteral("window_samples"))},
		{QStringLiteral("step_samples"), model_metadata_.value(QStringLiteral("step_samples"))},
		{QStringLiteral("window_seconds"), sample_rate_ > 0 ? model_metadata_.value(QStringLiteral("window_samples")).toDouble() / sample_rate_ : 0.0},
		{QStringLiteral("step_seconds"), sample_rate_ > 0 ? model_metadata_.value(QStringLiteral("step_samples")).toDouble() / sample_rate_ : 0.0},
		{QStringLiteral("model_threshold"), model_threshold_},
		{QStringLiteral("controller_safety_floor"), 0.75},
		{QStringLiteral("effective_threshold"), applied_threshold_},
		{QStringLiteral("configuration"), model_metadata_}};
	QJsonObject session_metadata{
		{QStringLiteral("source_id"), source_id},
		{QStringLiteral("source_name"), neuroSourceName(source_index)},
		{QStringLiteral("recording_name"), recording_name},
		{QStringLiteral("recording_metadata"), recorded_source},
		{QStringLiteral("model_name"), loaded_model_path_.isEmpty() ? QString() : QFileInfo(loaded_model_path_).fileName()},
		{QStringLiteral("sample_rate_hz"), sample_rate_},
		{QStringLiteral("channels"), channels},
		{QStringLiteral("classifier_settings"), classifier_settings},
		{QStringLiteral("playback_speed"), playback_speed},
		{QStringLiteral("control_enabled_at_start"), control_enabled_ && control_enabled_->isChecked()},
		{QStringLiteral("start_monotonic_seconds"), Clock::getTimeSinceInit()},
		{QStringLiteral("raw_eeg_copied"), false}};
	QString error;
	if(!experiment_logger_->startSession(session_metadata, &error))
	{
		appendDiagnostic(tr("Журналирование эксперимента недоступно; воспроизведение и управление не изменены: %1").arg(error));
		return;
	}
	last_experiment_controller_signature_.clear();
	last_experiment_telemetry_ms_ = 0;
	appendDiagnostic(tr("Журнал эксперимента %1 создан в локальном каталоге данных приложения.").arg(experiment_logger_->sessionId()));
}


void NeuroControlWidget::appendExperimentEvent(QJsonObject event)
{
	if(!experiment_logger_ || !experiment_logger_->isActive()) return;
	if(!event.contains(QStringLiteral("timestamp_utc")))
		event.insert(QStringLiteral("timestamp_utc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
	if(!event.contains(QStringLiteral("monotonic_seconds")))
		event.insert(QStringLiteral("monotonic_seconds"), Clock::getTimeSinceInit());
	experiment_logger_->appendEvent(std::move(event));
}


void NeuroControlWidget::recordControllerTelemetry()
{
	if(!experiment_logger_ || !experiment_logger_->isActive() || !client_) return;
	const qint64 now_ms = QDateTime::currentMSecsSinceEpoch();
	const bool trace_available = client_->neuro_control_trace_available;
	if(trace_available)
	{
		const neurocontrol::ControllerTrace& trace = client_->neuro_control_trace;
		const QString reason = neuroMotionReasonName(trace.reason);
		const QString signature = QStringLiteral("%1|%2|%3|%4|%5|%6|%7|%8")
			.arg(static_cast<qulonglong>(trace.event_id)).arg(reason)
			.arg(trace.forward_input_applied).arg(trace.turn_input_applied)
			.arg(trace.eeg_heartbeat_available).arg(trace.eeg_heartbeat_valid)
			.arg(trace.enabled).arg(static_cast<qulonglong>(trace.stream_generation));
		if(signature != last_experiment_controller_signature_)
		{
			QJsonObject event{
				{QStringLiteral("event_type"), QStringLiteral("controller_trace")},
				{QStringLiteral("command_event_id"), static_cast<qint64>(trace.event_id)},
				{QStringLiteral("stream_generation"), static_cast<qint64>(trace.stream_generation)},
				{QStringLiteral("evaluated_at_monotonic_seconds"), trace.evaluated_at_seconds},
				{QStringLiteral("command_age_seconds"), trace.command_age_seconds},
				{QStringLiteral("classifier_maximum_age_seconds"), trace.classifier_maximum_age_seconds},
				{QStringLiteral("classification_input_age_seconds"), trace.classification_input_age_seconds},
				{QStringLiteral("heartbeat_sample_index"), trace.eeg_heartbeat_available ? QJsonValue(static_cast<qint64>(trace.heartbeat_sample_index)) : QJsonValue(QJsonValue::Null)},
				{QStringLiteral("command_sample_index"), trace.command_sample_index > 0 ? QJsonValue(static_cast<qint64>(trace.command_sample_index)) : QJsonValue(QJsonValue::Null)},
				{QStringLiteral("heartbeat_age_seconds"), trace.eeg_heartbeat_available ? QJsonValue(trace.eeg_data_age_seconds) : QJsonValue(QJsonValue::Null)},
				{QStringLiteral("heartbeat_available"), trace.eeg_heartbeat_available},
				{QStringLiteral("heartbeat_valid"), trace.eeg_heartbeat_valid},
				{QStringLiteral("watchdog_passed_safety"), trace.passed_safety},
				{QStringLiteral("safety_reason"), reason},
				{QStringLiteral("controller_enabled"), trace.enabled},
				{QStringLiteral("physical_input_active"), trace.physical_input_active},
				{QStringLiteral("avatar_movement_allowed"), trace.avatar_movement_allowed},
				{QStringLiteral("forward_input_applied"), trace.forward_input_applied},
				{QStringLiteral("turn_input_applied"), trace.turn_input_applied},
				{QStringLiteral("movement_input_applied"), trace.forward_input_applied || trace.turn_input_applied != 0},
				{QStringLiteral("observed_ground_speed_mps"), trace.observed_ground_speed_mps},
				{QStringLiteral("avatar_position"), QJsonValue::Null},
				{QStringLiteral("avatar_position_available"), false}};
			event.insert(QStringLiteral("timestamp_utc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
			event.insert(QStringLiteral("monotonic_seconds"), trace.evaluated_at_seconds);
			appendExperimentEvent(std::move(event));
			last_experiment_controller_signature_ = signature;
		}
	}
	if(last_experiment_telemetry_ms_ == 0 || now_ms - last_experiment_telemetry_ms_ >= 200)
	{
		const neurocontrol::EEGDataHeartbeat& heartbeat = client_->neuro_control_eeg_heartbeat;
		const double now = Clock::getTimeSinceInit();
		const double heartbeat_age = heartbeat.available ? now - heartbeat.timestamp_seconds : 0.0;
		const neurocontrol::ControllerTrace& trace = client_->neuro_control_trace;
		const QJsonValue trace_event_id = trace_available ? QJsonValue(static_cast<qint64>(trace.event_id)) : QJsonValue(QJsonValue::Null);
		const QJsonValue heartbeat_sample_index = heartbeat.available ? QJsonValue(static_cast<qint64>(heartbeat.sample_index)) : QJsonValue(QJsonValue::Null);
		const QJsonValue heartbeat_age_value = heartbeat.available ? QJsonValue(heartbeat_age) : QJsonValue(QJsonValue::Null);
		const QJsonValue observed_speed = trace_available ? QJsonValue(trace.observed_ground_speed_mps) : QJsonValue(QJsonValue::Null);
		QJsonObject event{
			{QStringLiteral("event_type"), QStringLiteral("avatar_telemetry")},
			{QStringLiteral("monotonic_seconds"), now},
			{QStringLiteral("controller_trace_available"), trace_available},
			{QStringLiteral("command_event_id"), trace_event_id},
			{QStringLiteral("stream_generation"), static_cast<qint64>(client_->neuro_control_stream_generation)},
			{QStringLiteral("controller_enabled"), client_->neuro_control_enabled},
			{QStringLiteral("heartbeat_available"), heartbeat.available},
			{QStringLiteral("heartbeat_valid"), heartbeat.valid},
			{QStringLiteral("heartbeat_sample_index"), heartbeat_sample_index},
			{QStringLiteral("heartbeat_age_seconds"), heartbeat_age_value},
			{QStringLiteral("watchdog_reason"), trace_available ? neuroMotionReasonName(trace.reason) : QStringLiteral("awaiting_controller_trace")},
			{QStringLiteral("forward_input_applied"), trace_available && trace.forward_input_applied},
			{QStringLiteral("turn_input_applied"), trace_available ? trace.turn_input_applied : 0},
			{QStringLiteral("movement_input_applied"), trace_available && (trace.forward_input_applied || trace.turn_input_applied != 0)},
			{QStringLiteral("observed_ground_speed_mps"), observed_speed},
			{QStringLiteral("avatar_position"), QJsonValue::Null},
			{QStringLiteral("avatar_position_available"), false}};
		event.insert(QStringLiteral("timestamp_utc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
		appendExperimentEvent(std::move(event));
		last_experiment_telemetry_ms_ = now_ms;
	}
}


QJsonObject NeuroControlWidget::handleMCPRequest(const QString& tool, const QJsonObject& arguments)
{
	if(tool == QStringLiteral("neuro.start_lsl_recording"))
	{
		startLslRecording();
		const QJsonObject status = lslRecordingStatusJson();
		const QString state = status.value(QStringLiteral("active_recording")).toObject().value(QStringLiteral("state")).toString();
		if(state != QLatin1String("recording")) return neuroMCPError("LSL_RECORDING_NOT_STARTED", QStringLiteral("Запись не началась; проверьте neuro.get_lsl_status и диагностику NeuroControl."));
		return QJsonObject{{"accepted", true}, {"state", state}, {"recording", status.value(QStringLiteral("active_recording"))},
			{"status_tool", "neuro.get_lsl_recording_status"}};
	}
	if(tool == QStringLiteral("neuro.stop_lsl_recording"))
	{
		const QString current_state = lslRecordingStatusJson().value(QStringLiteral("active_recording")).toObject().value(QStringLiteral("state")).toString();
		if(!lsl_reader_ || current_state != QLatin1String("recording")) return neuroMCPError("LSL_RECORDING_NOT_ACTIVE", QStringLiteral("Нет активной записи; сохранённую запись можно проверить через neuro.load_lsl_recording."));
		requestStopLslRecording();
		return QJsonObject{{"accepted", true}, {"state", "finalizing"}, {"status_tool", "neuro.get_lsl_recording_status"}};
	}
	if(tool == QStringLiteral("neuro.get_lsl_recording_status")) return lslRecordingStatusJson();
	if(tool == QStringLiteral("neuro.list_lsl_recordings"))
	{
		const QJsonArray recordings = listLslRecordingsJson();
		return QJsonObject{{"recordings", recordings}, {"recordings_directory", lsl_recordings_directory_}, {"count", recordings.size()}};
	}
	if(tool == QStringLiteral("neuro.audit_lsl_recording"))
	{
		if(lsl_reader_ || lsl_connection_state_ == QLatin1String("connected") || lsl_recording_load_in_progress_)
			return neuroMCPError("LSL_AUDIT_REQUIRES_IDLE_SOURCE", QStringLiteral("Для изолированного офлайн-аудита отключите live LSL и дождитесь завершения записи/загрузки."));
		const QString id = arguments.value(QStringLiteral("recording_id")).toString();
		if(!validLslRecordingId(id)) return neuroMCPError("LSL_RECORDING_ID_INVALID", QStringLiteral("Укажите безопасный recording_id из neuro.list_lsl_recordings."));
		int target_sample_rate_hz = 160;
		if(arguments.contains(QStringLiteral("target_sample_rate")))
		{
			const QJsonValue value = arguments.value(QStringLiteral("target_sample_rate"));
			const double rate = value.toDouble(-1.0);
			if(!value.isDouble() || !std::isfinite(rate) || std::floor(rate) != rate || rate < 8.0 || rate > 2000.0)
				return neuroMCPError("LSL_AUDIT_TARGET_RATE_INVALID", QStringLiteral("target_sample_rate должен быть целым числом от 8 до 2000 Гц."));
			target_sample_rate_hz = static_cast<int>(rate);
		}
		if(lsl_shadow_audit_)
		{
			std::lock_guard<std::mutex> lock(lsl_shadow_audit_->mutex);
			if(!lsl_shadow_audit_->complete)
			{
				if(lsl_shadow_audit_->recording_id != id || lsl_shadow_audit_->target_sample_rate_hz != target_sample_rate_hz)
					return neuroMCPError("LSL_AUDIT_BUSY", QStringLiteral("Сначала дождитесь текущего офлайн-аудита; новый запрос не был подменён его результатом."));
				return QJsonObject{{"accepted", true}, {"state", lsl_shadow_audit_->state}, {"recording_id", lsl_shadow_audit_->recording_id},
					{"target_sample_rate", lsl_shadow_audit_->target_sample_rate_hz}, {"status_tool", "neuro.get_lsl_status"}};
			}
		}
		startLslShadowRecordingAudit(id, target_sample_rate_hz);
		return QJsonObject{{"accepted", true}, {"state", "queued"}, {"recording_id", id}, {"status_tool", "neuro.get_lsl_status"},
			{"target_sample_rate", target_sample_rate_hz}, {"integrity_status", "not_checked"},
			{"processing", "shared live LSL shadow resampler; no offline classifier or avatar command"}};
	}
	if(tool == QStringLiteral("neuro.save_lsl_audit_report"))
	{
		const QString experiment_id = arguments.value(QStringLiteral("experiment_id")).toString();
		static const QRegularExpression experiment_id_pattern(QStringLiteral("^S[0-9]{2}$"));
		if(!experiment_id_pattern.match(experiment_id).hasMatch())
			return neuroMCPError("LSL_AUDIT_EXPERIMENT_ID_INVALID", QStringLiteral("Укажите ID вида S04 или S05."));
		if(!lsl_shadow_audit_) return neuroMCPError("LSL_AUDIT_RESULT_MISSING", QStringLiteral("Сначала завершите neuro.audit_lsl_recording."));
		QJsonObject result;
		bool complete = false;
		{
			std::lock_guard<std::mutex> lock(lsl_shadow_audit_->mutex);
			complete = lsl_shadow_audit_->complete;
			result = lsl_shadow_audit_->result;
		}
		if(!complete || result.isEmpty())
			return neuroMCPError("LSL_AUDIT_NOT_COMPLETE", QStringLiteral("Дождитесь завершения аудита через neuro.get_lsl_status."));

		neurocontrol::LslAuditReportSnapshot report;
		report.experiment_id = experiment_id.toStdString();
		report.recording_id = result.value(QStringLiteral("recording_id")).toString().toStdString();
		report.audit_state = result.value(QStringLiteral("state")).toString().toStdString();
		report.integrity_status = result.value(QStringLiteral("integrity_status")).toString(QStringLiteral("not_checked")).toStdString();
		report.integrity_reason = result.value(QStringLiteral("integrity_reason")).toString().toStdString();
		report.input_rate_hz = result.value(QStringLiteral("input_rate_hz_nominal")).toInt();
		report.target_rate_hz = result.value(QStringLiteral("target_sample_rate_hz")).toInt(160);
		report.model_rate_hz = result.value(QStringLiteral("model_sample_rate_hz")).toInt();
		report.input_samples = result.value(QStringLiteral("input_samples")).toVariant().toULongLong();
		report.output_samples = result.value(QStringLiteral("output_samples")).toVariant().toULongLong();
		report.total_channels = static_cast<std::uint32_t>(std::max(0, result.value(QStringLiteral("channel_count")).toInt()));
		report.eeg_channels = static_cast<std::uint32_t>(std::max(0, result.value(QStringLiteral("eeg_channel_count")).toInt()));
		report.resample_up = result.value(QStringLiteral("resample_up")).toInt();
		report.resample_down = result.value(QStringLiteral("resample_down")).toInt();
		report.filter_taps = result.value(QStringLiteral("resampler_filter_taps")).toInt();
		report.group_delay_ms = result.value(QStringLiteral("fir_group_delay_ms")).toDouble();
		report.processing_ms = result.value(QStringLiteral("processing_ms")).toDouble();
		report.nominal_duration_seconds = result.value(QStringLiteral("duration_seconds_nominal")).toDouble();
		report.pipeline_errors = result.value(QStringLiteral("pipeline_errors")).toVariant().toULongLong();
		report.invalid_input_values = result.value(QStringLiteral("invalid_input_values")).toVariant().toULongLong();
		report.nonfinite_output_values = result.value(QStringLiteral("nonfinite_output_values")).toVariant().toULongLong();
		report.timestamp_gap_resets = result.value(QStringLiteral("pipeline_timestamp_gap_resets")).toVariant().toULongLong();
		report.invalid_frame_resets = result.value(QStringLiteral("pipeline_invalid_frame_resets")).toVariant().toULongLong();
		report.model_compatibility_state = result.value(QStringLiteral("model_compatibility_state")).toString().toStdString();
		for(const QJsonValue& reason : result.value(QStringLiteral("model_compatibility_reason_codes")).toArray())
			report.model_compatibility_reasons.push_back(reason.toString().toStdString());
		report.receive_rate_measurable_from_archive = result.value(QStringLiteral("receive_rate_measurable_from_archive")).toBool(false);
		report.classifier_ran = result.value(QStringLiteral("classifier_ran")).toBool(false);
		report.avatar_command_sent = result.value(QStringLiteral("avatar_command_sent")).toBool(false);

		std::filesystem::path saved_path;
		std::string write_error;
		if(!neurocontrol::writeLslAuditReport(std::filesystem::u8path(NEUROCONTROL_PROJECT_ROOT), report, saved_path, write_error))
			return neuroMCPError("LSL_AUDIT_REPORT_SAVE_FAILED", QString::fromStdString(write_error));
		return QJsonObject{{"saved", true}, {"experiment_id", experiment_id},
			{"report_path", QString::fromStdWString(saved_path.wstring())},
			{"recording_id", QString::fromStdString(report.recording_id)},
			{"integrity_status", QString::fromStdString(report.integrity_status)},
			{"raw_eeg_values_written", false}, {"public_git_changed", false}};
	}
	if(tool == QStringLiteral("neuro.load_lsl_recording"))
	{
		if(lsl_reader_ || lsl_recording_load_in_progress_) return neuroMCPError("LSL_BUSY", QStringLiteral("Дождитесь отключения потока и завершения текущей загрузки."));
		const QString id = arguments.value(QStringLiteral("recording_id")).toString();
		if(!validLslRecordingId(id)) return neuroMCPError("LSL_RECORDING_ID_INVALID", QStringLiteral("Укажите безопасный recording_id из neuro.list_lsl_recordings."));
		loadLslRecording(id);
		if(!lsl_recording_load_in_progress_) return neuroMCPError("LSL_RECORDING_LOAD_NOT_STARTED", QStringLiteral("Запись не принята для фоновой проверки; см. диагностику NeuroControl."));
		return QJsonObject{{"accepted", true}, {"state", "verifying_and_loading"}, {"recording_id", id},
			{"status_tool", "neuro.get_lsl_recording_status"}, {"integrity_check", "FNV-1a-64 payload and exact file structure"}};
	}
	if(tool == QStringLiteral("neuro.play_lsl_recording"))
	{
		if(source_->currentIndex() != 4 || lsl_loaded_recording_id_.isEmpty()) return neuroMCPError("LSL_RECORDING_NOT_LOADED", QStringLiteral("Сначала выберите завершённую запись через neuro.list_lsl_recordings и neuro.load_lsl_recording."));
		startLslRecordingPlayback();
		if(!playing_ || !lsl_recording_playback_) return neuroMCPError("LSL_RECORDING_PLAYBACK_NOT_STARTED", QStringLiteral("Архивное воспроизведение не запустилось; см. диагностику NeuroControl."));
		return QJsonObject{{"playing", true}, {"source", "lsl_recorded"}, {"sample_index", next_sample_},
			{"sample_count", static_cast<qint64>(lsl_recording_source_timestamps_.size())}, {"classifier_connected", false}, {"avatar_control_enabled", false}};
	}
	if(tool == QStringLiteral("neuro.stop_lsl_recording_playback"))
	{
		if(!lsl_recording_playback_) return neuroMCPError("LSL_RECORDING_PLAYBACK_NOT_RUNNING", QStringLiteral("Сохранённая LSL-запись сейчас не воспроизводится."));
		stopLslRecordingPlayback();
		return QJsonObject{{"playing", false}, {"loaded", !lsl_loaded_recording_id_.isEmpty()}, {"source", "lsl_recorded"}};
	}
	if(tool == QStringLiteral("neuro.seek_lsl_recording"))
	{
		if(source_->currentIndex() != 4 || lsl_recording_source_timestamps_.empty()) return neuroMCPError("LSL_RECORDING_NOT_LOADED", QStringLiteral("Нет загруженной LSL-записи."));
		const double seconds = arguments.value(QStringLiteral("position_seconds")).toDouble(-1.0);
		const double rate = lsl_recording_playback_rate_hz_ > 0.0 ? lsl_recording_playback_rate_hz_ : lsl_loaded_recording_metadata_.value(QStringLiteral("nominal_rate_hz")).toDouble();
		const double total = static_cast<double>(lsl_recording_source_timestamps_.size()) / rate;
		if(!std::isfinite(seconds) || !std::isfinite(rate) || rate <= 0.0 || seconds < 0.0 || seconds > total)
			return neuroMCPError("SEEK_OUT_OF_RANGE", QStringLiteral("Позиция должна находиться в диапазоне 0..%1 секунд.").arg(total, 0, 'f', 3));
		seekPlayback(static_cast<int>(std::floor(seconds * rate)));
		return QJsonObject{{"position_seconds", static_cast<double>(next_sample_) / rate}, {"sample_index", next_sample_},
			{"total_seconds", total}, {"playback_running", playing_}, {"playback_paused", paused_}};
	}
	if(tool == QStringLiteral("neuro.get_lsl_recording_timestamps"))
	{
		if(lsl_recording_source_timestamps_.empty()) return neuroMCPError("LSL_RECORDING_NOT_LOADED", QStringLiteral("Сначала загрузите и проверьте LSL-запись."));
		const qint64 requested_offset = arguments.value(QStringLiteral("offset")).toVariant().toLongLong();
		const int limit = std::clamp(arguments.value(QStringLiteral("limit")).toInt(200), 1, 1000);
		if(requested_offset < 0 || static_cast<quint64>(requested_offset) > lsl_recording_source_timestamps_.size())
			return neuroMCPError("TIMESTAMP_OFFSET_OUT_OF_RANGE", QStringLiteral("offset выходит за число отсчётов записи."));
		const std::size_t offset = static_cast<std::size_t>(requested_offset);
		const std::size_t end = std::min(lsl_recording_source_timestamps_.size(), offset + static_cast<std::size_t>(limit));
		QJsonArray timestamps;
		for(std::size_t i = offset; i < end; ++i)
		{
			std::uint64_t source_bits = 0, arrival_bits = 0;
			std::memcpy(&source_bits, &lsl_recording_source_timestamps_[i], sizeof(source_bits));
			std::memcpy(&arrival_bits, &lsl_recording_arrival_timestamps_[i], sizeof(arrival_bits));
			timestamps.append(QJsonObject{{"sample_index", static_cast<qint64>(i)},
				{"source_timestamp_seconds", std::isfinite(lsl_recording_source_timestamps_[i]) ? QJsonValue(lsl_recording_source_timestamps_[i]) : QJsonValue(QJsonValue::Null)},
				{"source_timestamp_ieee754_hex", QString::number(source_bits, 16).rightJustified(16, QLatin1Char('0'))},
				{"receiver_arrival_monotonic_seconds", std::isfinite(lsl_recording_arrival_timestamps_[i]) ? QJsonValue(lsl_recording_arrival_timestamps_[i]) : QJsonValue(QJsonValue::Null)},
				{"receiver_arrival_ieee754_hex", QString::number(arrival_bits, 16).rightJustified(16, QLatin1Char('0'))}});
		}
		return QJsonObject{{"offset", requested_offset}, {"returned", timestamps.size()},
			{"sample_count", static_cast<qint64>(lsl_recording_source_timestamps_.size())}, {"timestamps", timestamps},
			{"arrival_semantics", "one receiver monotonic clock value per received LSL chunk, repeated for samples in that chunk"},
			{"raw_eeg_contents_sent", false}};
	}
	if(tool == QStringLiteral("neuro.configure_lsl_visualization"))
	{
		if(arguments.contains(QStringLiteral("window_seconds")))
		{
			const int seconds = arguments.value(QStringLiteral("window_seconds")).toInt(-1);
			if(seconds < 2 || seconds > 30) return neuroMCPError("VISUALIZATION_WINDOW_INVALID", QStringLiteral("Окно графика должно быть от 2 до 30 секунд."));
			int index = time_window_->findData(seconds);
			if(index < 0) { time_window_->addItem(tr("%1 с").arg(seconds), seconds); index = time_window_->count() - 1; }
			time_window_->setCurrentIndex(index);
		}
		if(arguments.contains(QStringLiteral("amplitude_scale_uv")))
		{
			const double scale = arguments.value(QStringLiteral("amplitude_scale_uv")).toDouble(-1.0);
			if(!std::isfinite(scale) || scale < vertical_scale_->minimum() || scale > vertical_scale_->maximum())
				return neuroMCPError("VISUALIZATION_SCALE_INVALID", QStringLiteral("Масштаб должен быть от %1 до %2 мкВ.").arg(vertical_scale_->minimum()).arg(vertical_scale_->maximum()));
			vertical_scale_->setValue(scale);
		}
		int detail_index = -1;
		const QString detail = arguments.value(QStringLiteral("detail_channel")).toString();
		if(!detail.isEmpty())
		{
			for(int i = 0; i < channel_list_->count(); ++i)
				if(channel_list_->item(i)->data(Qt::UserRole).toString() == detail) { detail_index = i; break; }
			if(detail_index < 0) return neuroMCPError("CHANNEL_NOT_FOUND", QStringLiteral("Канал %1 отсутствует в текущем наборе графиков.").arg(detail));
		}
		if(arguments.contains(QStringLiteral("visible_channels")) && !arguments.value(QStringLiteral("visible_channels")).isArray())
			return neuroMCPError("VISIBLE_CHANNELS_INVALID", QStringLiteral("visible_channels должен быть массивом названий каналов."));
		std::set<QString> visible_names;
		for(const QJsonValue& value : arguments.value(QStringLiteral("visible_channels")).toArray())
		{
			if(!value.isString()) return neuroMCPError("VISIBLE_CHANNELS_INVALID", QStringLiteral("Названия в visible_channels должны быть строками."));
			visible_names.insert(value.toString());
		}
		if(detail_index >= 0)
			for(int i = 0; i < channel_list_->count(); ++i) channel_list_->item(i)->setCheckState(i == detail_index ? Qt::Checked : Qt::Unchecked);
		else if(arguments.contains(QStringLiteral("visible_channels")))
		{
			for(const QString& name : visible_names)
			{
				bool found = false;
				for(int i = 0; i < channel_list_->count(); ++i) if(channel_list_->item(i)->data(Qt::UserRole).toString() == name) { found = true; break; }
				if(!found) return neuroMCPError("CHANNEL_NOT_FOUND", QStringLiteral("Канал %1 отсутствует в текущем наборе графиков.").arg(name));
			}
			for(int i = 0; i < channel_list_->count(); ++i)
				channel_list_->item(i)->setCheckState(visible_names.count(channel_list_->item(i)->data(Qt::UserRole).toString()) ? Qt::Checked : Qt::Unchecked);
		}
		QJsonArray visible;
		for(int i = 0; i < channel_list_->count(); ++i) if(channel_list_->item(i)->checkState() == Qt::Checked) visible.append(channel_list_->item(i)->data(Qt::UserRole).toString());
		return QJsonObject{{"window_seconds", time_window_->currentData().toInt()}, {"amplitude_scale_uv", vertical_scale_->value()},
			{"visible_channels", visible}, {"detail_channel", detail}, {"display_unit", "microvolts"}};
	}
	if(tool == QStringLiteral("neuro.configure_lsl"))
	{
		if(lsl_connection_state_ == QLatin1String("connected") || lsl_connection_state_ == QLatin1String("connecting"))
			return neuroMCPError("LSL_BUSY", "Сначала отключите текущий LSL-поток.");
		if(lsl_library_state_ == QLatin1String("ready"))
			return QJsonObject{{"accepted", true}, {"state", "ready"}, {"library_path", lsl_library_path_},
				{"library_sha256", lsl_library_sha256_}, {"provenance", lsl_library_provenance_}, {"status_tool", "neuro.get_lsl_status"}};
		configureLslLibrary(false);
		if(lsl_library_state_ == QLatin1String("busy")) return neuroMCPError("LSL_BUSY", lsl_library_error_);
		return QJsonObject{{"accepted", true}, {"state", "configuring"}, {"status_tool", "neuro.get_lsl_status"}};
	}
	if(tool == QStringLiteral("neuro.get_lsl_status")) return lslStatusJson();
	if(tool == QStringLiteral("neuro.get_lsl_timestamps"))
	{
		const int limit = std::clamp(arguments.value(QStringLiteral("limit")).toInt(100), 1, 200);
		QJsonArray samples;
		const std::size_t first = lsl_recent_timestamps_.size() > static_cast<std::size_t>(limit) ?
			lsl_recent_timestamps_.size() - static_cast<std::size_t>(limit) : 0;
		for(std::size_t i = first; i < lsl_recent_timestamps_.size(); ++i)
			samples.append(QJsonObject{{"sample_index", static_cast<qint64>(received_samples_ - lsl_recent_timestamps_.size() + i + 1)},
				{"source_timestamp_seconds", lsl_recent_timestamps_[i].first},
				{"receiver_arrival_monotonic_seconds", lsl_recent_timestamps_[i].second}});
		return QJsonObject{{"timestamps", samples}, {"buffered_pairs", static_cast<int>(lsl_recent_timestamps_.size())},
			{"arrival_resolution", "one monotonic clock reading per received LSL chunk"},
			{"raw_eeg_contents_sent", false}};
	}
	if(tool == QStringLiteral("neuro.discover_lsl_streams"))
	{
		if(lsl_reader_ || lsl_connection_state_ == QLatin1String("connected")) return neuroMCPError("LSL_BUSY", "Отключите текущий LSL-поток перед новым поиском.");
		if(lsl_discovery_) return QJsonObject{{"accepted", true}, {"state", "searching"}, {"status_tool", "neuro.get_lsl_status"}};
		scanLsl();
		return QJsonObject{{"accepted", true}, {"state", lsl_library_state_ == QLatin1String("searching") ? QStringLiteral("configuring_library") : QStringLiteral("searching")}, {"status_tool", "neuro.get_lsl_status"}};
	}
	if(tool == QStringLiteral("neuro.run_lsl_diagnostic"))
	{
		if(lsl_diagnostic_running_) return neuroMCPError("LSL_DIAGNOSTIC_RUNNING", "Диагностический интервал уже выполняется.");
		if(source_->currentIndex() != 3 || lsl_connection_state_ != QLatin1String("connected"))
			return neuroMCPError("LSL_NOT_CONNECTED", "Сначала выберите lsl_live, подключите поток по UID и дождитесь состояния connected.");
		const int duration = std::clamp(arguments.value(QStringLiteral("duration_seconds")).toInt(15), 1, 300);
		startLslDiagnostic(duration);
		return QJsonObject{{"accepted", true}, {"state", "running"}, {"duration_seconds", duration},
			{"auto_disconnect", true}, {"status_tool", "neuro.get_lsl_status"}};
	}
	if(tool == QStringLiteral("neuro.connect_lsl_stream"))
	{
		if(lsl_library_state_ != QLatin1String("ready")) return neuroMCPError("LSL_LIBRARY_NOT_READY", "Вызовите neuro.configure_lsl и дождитесь library_state=ready в neuro.get_lsl_status.");
		const QString uid = arguments.value(QStringLiteral("stream_uid")).toString();
		int index = -1;
		for(int i = 0; i < lsl_discovered_.size(); ++i) if(!uid.isEmpty() && lsl_discovered_.at(i).uid == uid) { index = i; break; }
		if(index < 0) return neuroMCPError("LSL_STREAM_NOT_FOUND", "Сначала вызовите neuro.discover_lsl_streams и выберите UID из neuro.get_lsl_status.");
		const QString units = arguments.value(QStringLiteral("units_override")).toString(QStringLiteral("auto"));
		const int units_index = lsl_units_override_->findData(units);
		if(units_index < 0) return neuroMCPError("LSL_UNITS_INVALID", "Допустимы auto, microvolts, volts или millivolts.");
		if(source_->currentIndex() != 3) source_->setCurrentIndex(3);
		lsl_streams_->setCurrentIndex(index);
		lsl_units_override_->setCurrentIndex(units_index);
		connectLsl();
		return QJsonObject{{"accepted", true}, {"state", "connecting"}, {"stream_uid", uid},
			{"status_tool", "neuro.get_lsl_status"}, {"classifier_connected", false}, {"avatar_control_enabled", false}};
	}
	if(tool == QStringLiteral("neuro.disconnect_lsl_stream"))
	{
		if(source_->currentIndex() == 3) stop(); else disconnectLsl();
		return QJsonObject{{"state", "disconnected"}, {"avatar_control_enabled", false}};
	}
	if(tool == QStringLiteral("neuro.list_experiments"))
		return neurocontrol::NeuroExperimentLogger::listSessions(arguments.value(QStringLiteral("limit")).toInt(20));
	if(tool == QStringLiteral("neuro.get_experiment_summary"))
	{
		const QJsonObject result = neurocontrol::NeuroExperimentLogger::readSessionSummary(arguments.value(QStringLiteral("session_id")).toString());
		return result.value(QStringLiteral("ok")).toBool() ? result : neuroMCPError(result.value(QStringLiteral("error_code")).toString(), result.value(QStringLiteral("error")).toString());
	}
	if(tool == QStringLiteral("neuro.get_experiment_events"))
	{
		const QJsonObject result = neurocontrol::NeuroExperimentLogger::readEventPage(arguments.value(QStringLiteral("session_id")).toString(),
			arguments.value(QStringLiteral("cursor_bytes")).toVariant().toLongLong(), arguments.value(QStringLiteral("limit")).toInt(100));
		return result.value(QStringLiteral("ok")).toBool() ? result : neuroMCPError(result.value(QStringLiteral("error_code")).toString(), result.value(QStringLiteral("error")).toString());
	}
	const QString neuro_root = findNeurocontrolRoot();
	if(neuro_root.isEmpty()) return neuroMCPError(QStringLiteral("NEUROCONTROL_ROOT_NOT_FOUND"), QStringLiteral("Не найден каталог neurocontrol рядом с исходниками или приложением."));
	const QDir root_dir(neuro_root);
	const QString raw_dir = root_dir.filePath(QStringLiteral("datasets/raw"));
	const QString prepared_data_dir = root_dir.filePath(QStringLiteral("datasets/processed"));
	const QString model_dir = root_dir.filePath(QStringLiteral("models"));
	struct RecordingSelection { QString id; QString path; QString metadata_path; bool prepared = false; };
	const auto modelPath = [&](const QString& model_id) -> QString {
		if(model_id.isEmpty() || model_id.size() > 80) return QString();
		for(const QChar ch : model_id)
			if(!ch.isLetterOrNumber() && ch != QLatin1Char('_') && ch != QLatin1Char('-') && ch != QLatin1Char('.')) return QString();
		QString name = model_id;
		if(!name.endsWith(QStringLiteral(".json"), Qt::CaseInsensitive)) name += QStringLiteral(".json");
		const QString path = QDir(model_dir).filePath(name);
		return QFileInfo::exists(path) && pathIsWithin(model_dir, path) ? QFileInfo(path).absoluteFilePath() : QString();
	};
	const auto auditReportFor = [&](const QString& recording_id) -> QJsonObject {
		for(const QString& report_name : {QStringLiteral("audit.json"), QStringLiteral("audit_hands.json")})
		{
			const QJsonObject report = readJsonObjectFile(QDir(raw_dir).filePath(report_name));
			for(const QJsonValue& value : report.value(QStringLiteral("recordings")).toArray())
			{
				const QJsonObject item = value.toObject();
				if(QFileInfo(item.value(QStringLiteral("path")).toString()).completeBaseName().compare(recording_id, Qt::CaseInsensitive) == 0)
					return item;
			}
		}
		return {};
	};
	const auto hashFile = [](const QString& path) -> QString {
		QFile file(path);
		if(!file.open(QIODevice::ReadOnly)) return QString();
		QCryptographicHash hash(QCryptographicHash::Sha256);
		while(!file.atEnd())
		{
			const QByteArray chunk = file.read(1024 * 1024);
			if(chunk.isEmpty() && file.error() != QFileDevice::NoError) return QString();
			hash.addData(chunk);
		}
		return QString::fromLatin1(hash.result().toHex());
	};
	const auto recordingFileInfo = [&](const QString& path, const QString& recording_id) -> QJsonObject {
		QFileInfo file(path);
		QJsonObject result{{"recording_id", recording_id}, {"file_name", file.fileName()},
			{"path", file.canonicalFilePath()}, {"bytes", static_cast<qint64>(file.size())}, {"readable", file.isReadable()},
			{"format", file.suffix().toUpper()}, {"source_path", QDir(raw_dir).relativeFilePath(file.canonicalFilePath())},
			{"import_required", true}};
		QJsonObject audit = auditReportFor(recording_id);
		if(audit.isEmpty())
			result.insert(QStringLiteral("integrity"), QStringLiteral("not_audited"));
		else
		{
			const QString expected_sha = audit.value(QStringLiteral("sha256")).toString();
			const QString actual_sha = hashFile(path);
			const bool metadata_matches = audit.value(QStringLiteral("bytes")).toVariant().toLongLong() == file.size();
			const bool hash_matches = !expected_sha.isEmpty() && actual_sha.compare(expected_sha, Qt::CaseInsensitive) == 0;
			const bool verified = metadata_matches && hash_matches && audit.value(QStringLiteral("official_sha256_verified")).toBool() && audit.value(QStringLiteral("finite_signal_values_verified")).toBool();
			result.insert(QStringLiteral("integrity"), verified ? QStringLiteral("verified") : QStringLiteral("mismatch_or_incomplete"));
			result.insert(QStringLiteral("sha256"), actual_sha);
			result.insert(QStringLiteral("official_sha256_verified"), audit.value(QStringLiteral("official_sha256_verified")).toBool() && hash_matches);
			result.insert(QStringLiteral("duration_seconds"), audit.value(QStringLiteral("duration_seconds")));
			result.insert(QStringLiteral("sample_rate_hz"), audit.value(QStringLiteral("sample_rate_hz")));
			result.insert(QStringLiteral("eeg_channels"), audit.value(QStringLiteral("eeg_channels")));
			result.insert(QStringLiteral("channel_names"), audit.value(QStringLiteral("channel_names")));
			result.insert(QStringLiteral("annotations"), audit.value(QStringLiteral("annotations")));
		}
		return result;
	};
	const auto preparedRecordingInfo = [&](const QString& recording_id, const QString& metadata_path) -> QJsonObject {
		if(metadata_path.isEmpty() || !pathIsWithin(prepared_data_dir, metadata_path)) return {};
		QFile metadata_file(metadata_path);
		if(!metadata_file.open(QIODevice::ReadOnly) || metadata_file.size() <= 0 || metadata_file.size() > 8 * 1024 * 1024) return {};
		const QJsonObject metadata = QJsonDocument::fromJson(metadata_file.readAll()).object();
		if(metadata.value(QStringLiteral("format")).toString() != QStringLiteral("metasiberia-neurocontrol-f32-v1")) return {};
		const QString data_name = metadata.value(QStringLiteral("data_file")).toString();
		if(data_name.isEmpty() || QFileInfo(data_name).fileName() != data_name || data_name.contains(QLatin1Char('/')) || data_name.contains(QLatin1Char('\\'))) return {};
		const int sample_rate = metadata.value(QStringLiteral("sample_rate_hz")).toInt();
		const int samples = metadata.value(QStringLiteral("samples")).toInt();
		const QJsonArray channel_array = metadata.value(QStringLiteral("channels")).toArray();
		if(sample_rate <= 0 || sample_rate > 4096 || samples <= 0 || samples > qint64(sample_rate) * 3600 || channel_array.isEmpty() || channel_array.size() > 256 || metadata.value(QStringLiteral("source_file")).toString().isEmpty()) return {};
		QStringList channels;
		for(const QJsonValue& value : channel_array)
		{
			const QString name = value.toString();
			if(name.isEmpty() || channels.contains(name)) return {};
			channels.push_back(name);
		}
		const QString binary_path = QFileInfo(metadata_path).dir().filePath(data_name);
		if(!pathIsWithin(prepared_data_dir, binary_path) || !pathIsWithin(QFileInfo(metadata_path).absolutePath(), binary_path)) return {};
		QFileInfo binary(binary_path);
		const qint64 expected_bytes = qint64(samples) * channels.size() * 4;
		if(!binary.isFile() || !binary.isReadable() || expected_bytes <= 0 || expected_bytes > qint64(1024) * 1024 * 1024 || binary.size() != expected_bytes) return {};
		const QString actual_sha = hashFile(binary_path);
		const QString expected_sha = metadata.value(QStringLiteral("data_sha256")).toString();
		if(actual_sha.isEmpty() || expected_sha.size() != 64 || actual_sha.compare(expected_sha, Qt::CaseInsensitive) != 0) return {};
		const QString source_sha = metadata.value(QStringLiteral("source_sha256")).toString();
		if(!source_sha.isEmpty() && source_sha.size() != 64) return {};
		return QJsonObject{{"recording_id", recording_id}, {"dataset", metadata.value(QStringLiteral("dataset"))},
			{"subject", metadata.value(QStringLiteral("subject"))}, {"session", metadata.value(QStringLiteral("session"))},
			{"file_name", metadata.value(QStringLiteral("source_file"))}, {"path", binary.canonicalFilePath()}, {"metadata_path", QFileInfo(metadata_path).canonicalFilePath()},
			{"source_path", metadata.value(QStringLiteral("source_path"))},
			{"bytes", binary.size()}, {"readable", true}, {"format", metadata.value(QStringLiteral("format"))},
			{"sample_rate_hz", sample_rate}, {"samples", samples}, {"duration_seconds", double(samples) / sample_rate}, {"channels", channel_array},
			{"units", metadata.value(QStringLiteral("units"))}, {"source_file", metadata.value(QStringLiteral("source_file"))},
			{"source_sha256", source_sha}, {"data_sha256", actual_sha}, {"annotations", metadata.value(QStringLiteral("annotations"))},
			{"events", metadata.value(QStringLiteral("events"))}, {"preprocessing", metadata.value(QStringLiteral("preprocessing"))},
			{"provenance", metadata.value(QStringLiteral("provenance"))}, {"integrity", QStringLiteral("verified")},
			{"loaded", QFileInfo(loaded_recording_metadata_path_).canonicalFilePath() == QFileInfo(metadata_path).canonicalFilePath()},
			{"raw_eeg_contents_sent", false}};
	};
	const auto selectByPath = [&](const QString& requested_path) -> RecordingSelection {
		QFileInfo requested(requested_path);
		if(!requested.exists() || !requested.isFile() || !requested.isReadable()) return {};
		const QString canonical = requested.canonicalFilePath();
		if(pathIsWithin(raw_dir, canonical))
		{
			const QString suffix = requested.suffix();
			if(suffix.compare(QStringLiteral("edf"), Qt::CaseInsensitive) != 0 && suffix.compare(QStringLiteral("bdf"), Qt::CaseInsensitive) != 0) return {};
			return {relativeRecordingId(raw_dir, canonical, false), canonical, QString(), false};
		}
		if(!pathIsWithin(prepared_data_dir, canonical)) return {};
		QString metadata_path = canonical;
		if(requested.suffix().compare(QStringLiteral("f32"), Qt::CaseInsensitive) == 0)
		{
			QDir directory(requested.absolutePath());
			const QString base = requested.completeBaseName();
			for(const QFileInfo& candidate : directory.entryInfoList(QStringList{QStringLiteral("*.json"), QStringLiteral("*.JSON")}, QDir::Files | QDir::Readable))
				if(candidate.completeBaseName().compare(base, Qt::CaseInsensitive) == 0) { metadata_path = candidate.canonicalFilePath(); break; }
		}
		else if(requested.suffix().compare(QStringLiteral("json"), Qt::CaseInsensitive) != 0) return {};
		const QString id = relativeRecordingId(prepared_data_dir, metadata_path, true);
		const QJsonObject info = preparedRecordingInfo(id, metadata_path);
		if(info.isEmpty()) return {};
		if(requested.suffix().compare(QStringLiteral("f32"), Qt::CaseInsensitive) == 0 && QFileInfo(info.value(QStringLiteral("path")).toString()).canonicalFilePath() != canonical) return {};
		return {id, info.value(QStringLiteral("path")).toString(), metadata_path, true};
	};
	const auto selectById = [&](const QString& requested_id) -> RecordingSelection {
		if(requested_id.isEmpty() || requested_id.size() > 1024) return {};
		for(const QFileInfo& file : filesUnder(raw_dir, {QStringLiteral("*.edf"), QStringLiteral("*.EDF"), QStringLiteral("*.bdf"), QStringLiteral("*.BDF")}))
		{
			const QString id = relativeRecordingId(raw_dir, file.canonicalFilePath(), false);
			if(id.compare(requested_id, Qt::CaseInsensitive) == 0) return selectByPath(file.canonicalFilePath());
		}
		for(const QFileInfo& file : filesUnder(prepared_data_dir, {QStringLiteral("*.json"), QStringLiteral("*.JSON")}))
		{
			const QString id = relativeRecordingId(prepared_data_dir, file.canonicalFilePath(), true);
			if(id.compare(requested_id, Qt::CaseInsensitive) != 0) continue;
			const QJsonObject info = preparedRecordingInfo(id, file.canonicalFilePath());
			if(!info.isEmpty()) return {id, info.value(QStringLiteral("path")).toString(), file.canonicalFilePath(), true};
		}
		return {};
	};
	const auto selectRecording = [&](const QJsonObject& args) -> RecordingSelection {
		const QString id = args.value(QStringLiteral("recording_id")).toString().trimmed();
		const QString path = args.value(QStringLiteral("path")).toString().trimmed();
		if(!id.isEmpty() && !path.isEmpty()) return {};
		if(!path.isEmpty()) return selectByPath(path);
		return selectById(id);
	};
	const auto controllerTrace = [&]() -> QJsonObject {
		if(!client_) return QJsonObject{{"available", false}, {"reason", QStringLiteral("NeuroControl недоступен.")}};
		if(!client_->neuro_control_trace_available) return QJsonObject{
			{"available", false}, {"reason", QStringLiteral("Контроллер ещё не получил ни одной команды NeuroControl.")},
			{"stream_generation", static_cast<qint64>(client_->neuro_control_stream_generation)},
			{"eeg_heartbeat_available", client_->neuro_control_eeg_heartbeat.available},
			{"eeg_heartbeat_valid", client_->neuro_control_eeg_heartbeat.valid},
			{"heartbeat_sample_index", static_cast<qint64>(client_->neuro_control_eeg_heartbeat.sample_index)},
			{"eeg_data_age_seconds", client_->neuro_control_eeg_heartbeat.available ?
				Clock::getTimeSinceInit() - client_->neuro_control_eeg_heartbeat.timestamp_seconds : QJsonValue()},
			{"motion_command_armed", client_->neuro_control_motion_command_armed}};
		const neurocontrol::ControllerTrace& trace = client_->neuro_control_trace;
		return QJsonObject{{"available", true}, {"event_id", static_cast<qint64>(trace.event_id)},
			{"evaluated_at_monotonic_seconds", trace.evaluated_at_seconds}, {"command_age_seconds", trace.command_age_seconds},
			{"classifier_maximum_age_seconds", trace.classifier_maximum_age_seconds}, {"eeg_data_age_seconds", trace.eeg_data_age_seconds},
			{"classification_input_age_seconds", trace.classification_input_age_seconds},
			{"stream_generation", static_cast<qint64>(trace.stream_generation)},
			{"heartbeat_sample_index", static_cast<qint64>(trace.heartbeat_sample_index)},
			{"command_sample_index", static_cast<qint64>(trace.command_sample_index)},
			{"controller_enabled", trace.enabled}, {"physical_input_active", trace.physical_input_active},
			{"avatar_movement_allowed", trace.avatar_movement_allowed}, {"passed_safety", trace.passed_safety},
			{"eeg_heartbeat_available", trace.eeg_heartbeat_available}, {"eeg_heartbeat_valid", trace.eeg_heartbeat_valid},
			{"motion_command_armed", trace.motion_command_armed},
			{"safety_reason", neuroMotionReasonName(trace.reason)}, {"forward_input_applied", trace.forward_input_applied},
			{"turn_input_applied", trace.turn_input_applied}, {"observed_ground_speed_mps", trace.observed_ground_speed_mps},
			{"movement_input_applied", trace.forward_input_applied || trace.turn_input_applied != 0},
			{"motion_measurement_is_causal", false}};
	};
	if(tool == QStringLiteral("neuro.get_status"))
	{
		const bool training = training_process_ && training_process_->state() != QProcess::NotRunning;
		const QString loaded_recording = loaded_recording_metadata_path_.isEmpty() ? QString() : QFileInfo(loaded_recording_metadata_path_).fileName();
		const QJsonObject prediction = last_prediction_;
		return QJsonObject{
			{"source", neuroSourceName(source_ ? source_->currentIndex() : -1)},
			{"source_status", source_status_label_ ? source_status_label_->text() : QString()},
			{"connection_state", source_ && source_->currentIndex() == 3 ? lsl_connection_state_ : source_ && source_->currentIndex() == 2 ? QStringLiteral("hardware_unavailable") : source_ && source_->currentIndex() == 1 ? QStringLiteral("synthetic") : source_ && source_->currentIndex() == 4 ? QStringLiteral("archived_lsl_recording") : QStringLiteral("local_recording")},
			{"playback_running", playing_}, {"playback_paused", paused_}, {"playback_sample", next_sample_},
			{"recording", loaded_recording}, {"recording_metadata_path", loaded_recording_metadata_path_},
			{"model", loaded_model_path_.isEmpty() ? QString() : QFileInfo(loaded_model_path_).fileName()}, {"model_path", loaded_model_path_},
			{"classifier_ready", classifier_ != nullptr}, {"classification_compatible", source_->currentIndex() == 0 && classifier_ && !recorded_samples_.empty() && recording_channels_ == model_channels_ && sample_rate_ == model_metadata_.value("sample_rate_hz").toInt()},
			{"avatar_control_enabled", client_ && client_->neuro_control_enabled},
			{"last_command", client_ ? neuroCommandName(client_->neuro_control_command.type) : QStringLiteral("STOP")},
			{"last_prediction", prediction}, {"last_error", last_error_},
			{"model_threshold", model_threshold_}, {"controller_safety_floor", 0.75}, {"effective_threshold", applied_threshold_},
			{"training_job_running", training}, {"training_job_id", active_job_id_}, {"training_job_kind", active_job_kind_},
			{"lsl_recording", lslRecordingStatusJson()}, {"neurocontrol_root", neuro_root}, {"client_build", currentClientBuildInfo()}, {"controller_trace", controllerTrace()},
			{"lsl", lslStatusJson()}
		};
	}
	if(tool == QStringLiteral("neuro.list_sources"))
	{
		return QJsonObject{{"sources", QJsonArray{
			QJsonObject{{"id", "recorded_edf"}, {"name", "Записанная ЭЭГ · EDF/EDF+"}, {"available", true}, {"controls_avatar", true}, {"status", source_ && source_->currentIndex() == 0 ? "selected" : "available"}},
			QJsonObject{{"id", "brainflow_synthetic"}, {"name", "Синтетическая ЭЭГ · BrainFlow Synthetic Board"}, {"available", true}, {"controls_avatar", false}, {"status", source_ && source_->currentIndex() == 1 ? (playing_ ? "running" : "selected") : "available"}},
			QJsonObject{{"id", "live_eeg"}, {"name", "Физическое ЭЭГ-устройство"}, {"available", false}, {"controls_avatar", false}, {"status", "hardware_not_connected"}},
			QJsonObject{{"id", "lsl_live"}, {"name", "LSL — поток в реальном времени"}, {"available", true}, {"controls_avatar", false},
				{"status", lsl_connection_state_ == QLatin1String("disconnected") && lsl_library_state_ != QLatin1String("ready") ? QStringLiteral("library_unconfigured") : lsl_connection_state_},
				{"library_ready", lsl_library_state_ == QLatin1String("ready")}, {"library_state", lsl_library_state_}},
			QJsonObject{{"id", "lsl_recorded"}, {"name", "Архивная запись LSL · только визуальное воспроизведение"}, {"available", true}, {"controls_avatar", false},
				{"status", lsl_loaded_recording_id_.isEmpty() ? QStringLiteral("no_recording_loaded") : playing_ && lsl_recording_playback_ ? QStringLiteral("playing") : QStringLiteral("loaded")}}
		}}};
	}
	if(tool == QStringLiteral("neuro.list_recordings"))
	{
		QJsonArray recordings;
		for(const QFileInfo& file : filesUnder(raw_dir, {QStringLiteral("*.edf"), QStringLiteral("*.EDF"), QStringLiteral("*.bdf"), QStringLiteral("*.BDF")}))
		{
			const QString id = relativeRecordingId(raw_dir, file.canonicalFilePath(), false);
			if(!id.isEmpty()) recordings.append(recordingFileInfo(file.canonicalFilePath(), id));
		}
		for(const QFileInfo& file : filesUnder(prepared_data_dir, {QStringLiteral("*.json"), QStringLiteral("*.JSON")}))
		{
			const QString id = relativeRecordingId(prepared_data_dir, file.canonicalFilePath(), true);
			const QJsonObject info = preparedRecordingInfo(id, file.canonicalFilePath());
			if(!info.isEmpty()) recordings.append(info);
		}
		return QJsonObject{{"dataset_directory", QFileInfo(raw_dir).absoluteFilePath()},
			{"prepared_recordings_directory", QFileInfo(prepared_data_dir).absoluteFilePath()},
			{"recordings", recordings}, {"count", recordings.size()}, {"raw_eeg_contents_sent", false}};
	}
	if(tool == QStringLiteral("neuro.get_recording_info"))
	{
		const QString id = arguments.value(QStringLiteral("recording_id")).toString();
		const QString requested_path = arguments.value(QStringLiteral("path")).toString();
		if(id.isEmpty() && requested_path.isEmpty() && !loaded_recording_metadata_path_.isEmpty())
		{
			if(!pathIsWithin(prepared_data_dir, loaded_recording_metadata_path_)) return neuroMCPError("PATH_NOT_ALLOWED", "Текущие метаданные находятся вне каталога подготовленных записей.");
			const QJsonObject metadata = readJsonObjectFile(loaded_recording_metadata_path_);
			if(metadata.isEmpty()) return neuroMCPError("RECORDING_METADATA_UNAVAILABLE", "Не удалось прочитать метаданные текущей записи.");
			QJsonObject result{{"loaded", true}, {"metadata_path", loaded_recording_metadata_path_}, {"metadata", metadata}};
			result.insert(QStringLiteral("sample_rate_hz"), metadata.value(QStringLiteral("sample_rate_hz")));
			result.insert(QStringLiteral("samples"), metadata.value(QStringLiteral("samples")));
			result.insert(QStringLiteral("channels"), metadata.value(QStringLiteral("channels")));
			result.insert(QStringLiteral("events"), metadata.value(QStringLiteral("annotations")));
			return result;
		}
		if(!id.isEmpty() && !requested_path.isEmpty()) return neuroMCPError("RECORDING_SELECTOR_AMBIGUOUS", "Укажите только один параметр: recording_id или path.");
		if(id.isEmpty() && requested_path.isEmpty()) return neuroMCPError("RECORDING_SELECTOR_REQUIRED", "Укажите recording_id из neuro.list_recordings или полный путь внутри datasets/raw либо datasets/processed.");
		const RecordingSelection selected = selectRecording(arguments);
		if(selected.path.isEmpty()) return neuroMCPError("RECORDING_NOT_FOUND_OR_PATH_NOT_ALLOWED", "Запись не найдена, формат не поддерживается или путь находится вне разрешённых datasets/raw и datasets/processed.");
		if(selected.prepared)
		{
			QJsonObject result = preparedRecordingInfo(selected.id, selected.metadata_path);
			result.insert(QStringLiteral("metadata"), readJsonObjectFile(selected.metadata_path));
			result.insert(QStringLiteral("events"), result.value(QStringLiteral("annotations")));
			return result;
		}
		return recordingFileInfo(selected.path, selected.id);
	}
	if(tool == QStringLiteral("neuro.list_models"))
	{
		QJsonArray models;
		const QFileInfoList files = QDir(model_dir).entryInfoList(QStringList{QStringLiteral("*.json")}, QDir::Files | QDir::Readable, QDir::Name);
		for(const QFileInfo& file : files)
		{
			if(!pathIsWithin(model_dir, file.absoluteFilePath())) continue;
			const QJsonObject meta = readJsonObjectFile(file.absoluteFilePath());
			models.append(QJsonObject{{"model_id", file.completeBaseName()}, {"file_name", file.fileName()}, {"path", file.absoluteFilePath()},
				{"readable", file.isReadable()}, {"valid_json", !meta.isEmpty()}, {"algorithm", meta.value("algorithm")},
				{"sample_rate_hz", meta.value("sample_rate_hz")}, {"channels", meta.value("channels")},
				{"forward_threshold", meta.value("forward_threshold")}, {"loaded", QFileInfo(loaded_model_path_).absoluteFilePath() == file.absoluteFilePath() && !loaded_model_path_.isEmpty()}});
		}
		return QJsonObject{{"models_directory", QFileInfo(model_dir).absoluteFilePath()}, {"models", models}, {"count", models.size()}};
	}
	if(tool == QStringLiteral("neuro.get_model_info"))
	{
		QString path = loaded_model_path_;
		const QString requested_id = arguments.value(QStringLiteral("model_id")).toString();
		if(!requested_id.isEmpty()) path = modelPath(requested_id);
		else if(path.isEmpty() && !model_dir.isEmpty()) return neuroMCPError("MODEL_NOT_LOADED", "Модель не загружена. Укажите model_id из neuro.list_models, чтобы получить сведения о файле.");
		if(path.isEmpty() || !pathIsWithin(model_dir, path)) return neuroMCPError("MODEL_NOT_FOUND", QStringLiteral("Модель %1 не найдена в разрешённом каталоге.").arg(requested_id));
		const QJsonObject meta = readJsonObjectFile(path);
		if(meta.isEmpty()) return neuroMCPError("MODEL_INVALID", "Файл модели пуст или не является JSON-объектом.");
		QDir model_root(QFileInfo(path).absolutePath());
		model_root.cdUp();
		QString metrics_path = model_root.filePath(QStringLiteral("datasets/processed/%1_metrics.json").arg(QFileInfo(path).completeBaseName()));
		if(!QFileInfo::exists(metrics_path) && QFileInfo(path).completeBaseName() == QStringLiteral("S001_feet_vs_rest")) metrics_path = model_root.filePath(QStringLiteral("datasets/processed/S001_metrics.json"));
		const QJsonObject metrics = pathIsWithin(neuro_root, metrics_path) ? readJsonObjectFile(metrics_path) : QJsonObject();
		return QJsonObject{{"model_id", QFileInfo(path).completeBaseName()}, {"path", QFileInfo(path).absoluteFilePath()},
			{"loaded", !loaded_model_path_.isEmpty() && QFileInfo(loaded_model_path_).absoluteFilePath() == QFileInfo(path).absoluteFilePath()},
			{"algorithm", meta.contains("algorithm") ? meta.value("algorithm") : QJsonValue(QStringLiteral("CSP + LDA"))}, {"sample_rate_hz", meta.value("sample_rate_hz")},
			{"window_samples", meta.value("window_samples")}, {"step_samples", meta.value("step_samples")},
			{"channels", meta.value("channels")}, {"positive_class", meta.contains("positive_class_ru") ? meta.value("positive_class_ru") : meta.value("positive_class")},
			{"negative_class", meta.contains("negative_class_ru") ? meta.value("negative_class_ru") : meta.value("negative_class")},
			{"model_threshold", meta.value("forward_threshold")}, {"controller_safety_floor", 0.75},
			{"effective_threshold", std::max(0.75, meta.value("forward_threshold").toDouble(0.75))},
			{"experimental_turn_mapping", meta.value("experimental_turn_mapping")}, {"training_runs", meta.value("training_runs")},
			{"test_run", meta.contains("test_run") ? meta.value("test_run") : meta.value("held_out_run")}, {"metrics_path", QFileInfo::exists(metrics_path) ? QFileInfo(metrics_path).absoluteFilePath() : QString()},
			{"metrics", metrics}, {"model_metadata", meta}};
	}
	if(tool == QStringLiteral("neuro.check_model_compatibility"))
	{
		QString path = loaded_model_path_;
		const QString requested_id = arguments.value(QStringLiteral("model_id")).toString();
		if(!requested_id.isEmpty()) path = modelPath(requested_id);
		if(path.isEmpty() || !pathIsWithin(model_dir, path)) return neuroMCPError("MODEL_NOT_FOUND", "Не найдена выбранная модель.");
		if(loaded_recording_metadata_path_.isEmpty() || recorded_samples_.empty()) return neuroMCPError("RECORDING_NOT_LOADED", "Сначала загрузите запись ЭЭГ.");
		const QJsonObject meta = readJsonObjectFile(path);
		if(meta.isEmpty()) return neuroMCPError("MODEL_INVALID", "Не удалось прочитать метаданные модели.");
		QStringList reasons;
		QStringList model_channels;
		for(const QJsonValue& channel : meta.value(QStringLiteral("channels")).toArray()) model_channels.push_back(channel.toString());
		QStringList recording_channels;
		for(const std::string& channel : recording_channels_) recording_channels.push_back(QString::fromStdString(channel));
		if(model_channels != recording_channels) reasons.push_back(QStringLiteral("Набор/порядок каналов записи не совпадает с моделью."));
		if(sample_rate_ != meta.value(QStringLiteral("sample_rate_hz")).toInt()) reasons.push_back(QStringLiteral("Частота дискретизации записи не совпадает с моделью."));
		return QJsonObject{{"compatible", reasons.isEmpty()}, {"model_id", QFileInfo(path).completeBaseName()},
			{"recording", QFileInfo(loaded_recording_metadata_path_).fileName()}, {"model_sample_rate_hz", meta.value("sample_rate_hz")},
			{"recording_sample_rate_hz", sample_rate_}, {"model_channels", QJsonArray::fromStringList(model_channels)},
			{"recording_channels", QJsonArray::fromStringList(recording_channels)}, {"reasons", QJsonArray::fromStringList(reasons)}};
	}
	if(tool == QStringLiteral("neuro.evaluate_model"))
	{
		if(training_process_->state() != QProcess::NotRunning)
			return neuroMCPError("JOB_ALREADY_RUNNING", QStringLiteral("Сейчас выполняется задача %1 (%2). Дождитесь её завершения.").arg(active_job_id_, active_job_kind_));
		const QString model_id = arguments.value(QStringLiteral("model_id")).toString();
		const QString recording_id = arguments.value(QStringLiteral("recording_id")).toString().toUpper();
		const QString model_path = modelPath(model_id);
		if(model_path.isEmpty()) return neuroMCPError("MODEL_NOT_FOUND", "Укажите существующую модель из neuro.list_models.");
		if(!validPhysioNetRecordingId(recording_id)) return neuroMCPError("RECORDING_ID_INVALID", "Оценка поддерживает только подготовленные записи PhysioNet S001R04/R06/R08/R10/R12/R14.");
		const QString processed_dir = root_dir.filePath(QStringLiteral("datasets/processed"));
		const QString metadata_path = QDir(processed_dir).filePath(recording_id + QStringLiteral(".json"));
		if(!QFileInfo::exists(metadata_path) || !pathIsWithin(processed_dir, metadata_path))
			return neuroMCPError("PREPARED_RECORDING_NOT_FOUND", QStringLiteral("Не найдены подготовленные данные %1. Сначала выполните neuro.load_recording.").arg(recording_id));
		if(evaluation_script_path_.isEmpty() || !QFileInfo::exists(evaluation_script_path_)) return neuroMCPError("EVALUATION_SCRIPT_NOT_FOUND", "В проекте не найден neurocontrol/tools/evaluate_model.py.");
		if(python_path_.isEmpty() || !QFileInfo::exists(python_path_)) return neuroMCPError("PYTHON_NOT_CONFIGURED", "Укажите Python с NumPy и SciPy во вкладке «Обучение модели».");
		const QString report_dir = QDir(processed_dir).filePath(QStringLiteral("evaluations"));
		last_evaluation_report_path_ = QDir(report_dir).filePath(QStringLiteral("%1_%2_%3.json")
			.arg(QFileInfo(model_path).completeBaseName(), recording_id, QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMddTHHmmsszzzZ"))));
		last_evaluation_report_ = QJsonObject();
		last_error_.clear();
		evaluation_model_id_ = QFileInfo(model_path).completeBaseName();
		evaluation_recording_id_ = recording_id;
		active_job_is_evaluation_ = true;
		runPythonScript(evaluation_script_path_, {QStringLiteral("--model"), model_path, QStringLiteral("--metadata"), metadata_path,
			QStringLiteral("--output"), last_evaluation_report_path_}, false);
		if(training_process_->state() == QProcess::NotRunning)
		{
			active_job_is_evaluation_ = false;
			return neuroMCPError("EVALUATION_NOT_STARTED", last_error_.isEmpty() ? "Не удалось запустить оценку модели." : last_error_);
		}
		return QJsonObject{{"accepted", true}, {"state", "running"}, {"job_id", active_job_id_}, {"kind", "model_evaluation"},
			{"model_id", evaluation_model_id_}, {"recording_id", evaluation_recording_id_}, {"report_path", last_evaluation_report_path_},
			{"asynchronous", true}, {"status_tool", "neuro.get_training_status"}};
	}
	if(tool == QStringLiteral("neuro.get_signal_info"))
	{
		QJsonArray channels, channel_info;
		for(std::size_t i = 0; i < active_channels_.size(); ++i)
		{
			const QString name = QString::fromStdString(active_channels_[i]);
			const QString type = i < active_channel_types_.size() ? QString::fromStdString(active_channel_types_[i]) : QString();
			const QString unit = i < active_channel_units_.size() ? QString::fromStdString(active_channel_units_[i]) : QString();
			channels.append(name);
			channel_info.append(QJsonObject{{"index", static_cast<int>(i)}, {"name", name}, {"type", type}, {"unit", unit}, {"display_unit", "microvolts"}});
		}
		return QJsonObject{{"source", neuroSourceName(source_ ? source_->currentIndex() : -1)},
			{"stream_state", source_->currentIndex() == 3 ? lsl_connection_state_ : playing_ ? paused_ ? QStringLiteral("paused") : QStringLiteral("running") : QStringLiteral("stopped")},
			{"sample_rate_hz", sample_rate_}, {"channel_count", channel_count_}, {"channel_names", channels},
			{"channels", channel_info},
			{"received_samples", static_cast<qint64>(received_samples_)}, {"dropped_samples", static_cast<qint64>(dropped_samples_)},
			{"measured_receive_rate_hz", receive_rate_hz_}, {"recording_samples", channel_count_ > 0 ? static_cast<qint64>(recorded_samples_.size() / static_cast<std::size_t>(channel_count_)) : 0},
			{"classification_compatible", source_->currentIndex() == 0 && classifier_ && recording_channels_ == model_channels_ && sample_rate_ == model_metadata_.value("sample_rate_hz").toInt()},
			{"lsl", source_->currentIndex() == 3 ? QJsonValue(lslStatusJson()) : QJsonValue(QJsonValue::Null)},
			{"lsl_recording", source_->currentIndex() == 4 || !lsl_loaded_recording_id_.isEmpty() ? QJsonValue(lslRecordingStatusJson()) : QJsonValue(QJsonValue::Null)},
			{"classifier_connected", source_->currentIndex() == 0 && classifier_ != nullptr},
			{"avatar_control_enabled", source_->currentIndex() == 0 && client_->neuro_control_enabled}};
	}
	if(tool == QStringLiteral("neuro.get_spectrum"))
	{
		if(!spectrum_ || spectrum_->bins().empty()) return neuroMCPError("SPECTRUM_NOT_READY", "Спектр появится после получения не менее одной секунды сигнала выбранного канала.");
		QJsonArray bins;
		for(const auto& bin : spectrum_->bins()) bins.append(QJsonObject{{"frequency_hz", bin.first}, {"power_db_uv2_per_hz", bin.second}});
		return QJsonObject{{"channel", spectrum_channel_ && spectrum_channel_->currentIndex() >= 0 ? spectrum_channel_->currentText() : QString()},
			{"sample_rate_hz", sample_rate_}, {"window_seconds", 2}, {"method", "Hann-windowed single-sided periodogram; UI display bins 0.5 Hz"}, {"bins", bins}};
	}
	if(tool == QStringLiteral("neuro.get_prediction"))
	{
		if(last_prediction_.isEmpty()) return neuroMCPError("NO_PREDICTION", "Пока нет предсказаний. Загрузите совместимые EDF и модель, затем запустите воспроизведение.");
		QJsonObject result = last_prediction_;
		const QJsonObject trace = controllerTrace();
		result.insert(QStringLiteral("controller_trace"), trace);
		if(trace.value(QStringLiteral("available")).toBool() && trace.value(QStringLiteral("event_id")).toVariant().toLongLong() == result.value(QStringLiteral("event_id")).toVariant().toLongLong())
			result.insert(QStringLiteral("controller_reason"), trace.value(QStringLiteral("safety_reason")));
		else result.insert(QStringLiteral("controller_reason"), result.value(QStringLiteral("command_sent_to_controller")).toBool() ? QStringLiteral("awaiting_controller_frame") : QStringLiteral("not_sent_control_disabled"));
		return result;
	}
	if(tool == QStringLiteral("neuro.get_avatar_status"))
	{
		const neurocontrol::Command& command = client_->neuro_control_command;
		return QJsonObject{{"control_enabled", client_->neuro_control_enabled}, {"current_command", neuroCommandName(command.type)},
			{"command_event_id", static_cast<qint64>(command.event_id)}, {"command_confidence", command.confidence},
			{"command_source", command.source == neurocontrol::CommandSource::RecordedEeg ? "recorded_eeg" : command.source == neurocontrol::CommandSource::SyntheticEeg ? "synthetic_eeg" : command.source == neurocontrol::CommandSource::SimulatedTest ? "simulated_test" : "live_eeg"},
			{"minimum_confidence", command.minimum_confidence}, {"controller_trace", controllerTrace()}};
	}
	if(tool == QStringLiteral("neuro.get_diagnostics"))
	{
		const int requested_limit = arguments.value(QStringLiteral("limit")).toInt(100);
		const int limit = std::clamp(requested_limit, 1, 500);
		const QString level = arguments.value(QStringLiteral("level")).toString();
		QJsonArray entries;
		int skipped = 0;
		for(auto it = diagnostic_entries_.crbegin(); it != diagnostic_entries_.crend(); ++it)
		{
			if(!level.isEmpty() && !it->contains(QStringLiteral("[%1]").arg(level), Qt::CaseInsensitive)) continue;
			if(skipped++ >= limit) break;
			entries.prepend(*it);
		}
		return QJsonObject{{"entries", entries}, {"count", entries.size()}, {"limit", limit},
			{"last_error", last_error_}, {"last_prediction", last_prediction_}, {"controller_trace", controllerTrace()}};
	}
	if(tool == QStringLiteral("neuro.get_training_status"))
	{
		const bool running = training_process_ && training_process_->state() != QProcess::NotRunning;
		return QJsonObject{{"job_id", active_job_id_}, {"kind", active_job_kind_},
			{"state", running ? QStringLiteral("running") : last_job_finished_ ? (last_job_success_ ? QStringLiteral("succeeded") : QStringLiteral("failed")) : QStringLiteral("idle")},
			{"running", running}, {"finished", last_job_finished_}, {"success", last_job_finished_ ? QJsonValue(last_job_success_) : QJsonValue()},
			{"exit_code", last_job_exit_code_}, {"status", training_status_ ? training_status_->text() : QString()},
			{"progress_indeterminate", running}, {"model", loaded_model_path_}, {"last_error", last_error_},
			{"last_evaluation_report_path", last_evaluation_report_path_}, {"last_evaluation_report", last_evaluation_report_}};
	}
	if(tool == QStringLiteral("neuro.get_knowledge"))
	{
		static const QMap<QString, QString> documents = {
			{"overview", "NEUROCONTROL_OVERVIEW.md"}, {"architecture", "NEUROCONTROL_ARCHITECTURE.md"},
			{"glossary", "NEUROCONTROL_GLOSSARY_RU.md"}, {"datasets", "NEUROCONTROL_DATASETS.md"},
			{"models", "NEUROCONTROL_MODELS.md"}, {"tools", "NEUROCONTROL_MCP_TOOLS.md"},
			{"experiments", "NEUROCONTROL_EXPERIMENTS.md"}, {"troubleshooting", "NEUROCONTROL_TROUBLESHOOTING.md"},
			{"medical_eeg", "NEUROCONTROL_MEDICAL_EEG.md"}, {"ai_instructions", "NEUROCONTROL_SAFE_USE.md"}
		};
		const QString topic = arguments.value(QStringLiteral("topic")).toString();
		if(!documents.contains(topic)) return neuroMCPError("KNOWLEDGE_TOPIC_NOT_FOUND", "Укажите один из разделов: overview, architecture, glossary, datasets, models, tools, experiments, troubleshooting, medical_eeg, ai_instructions.");
		const QString path = root_dir.filePath(QStringLiteral("knowledge/%1").arg(documents.value(topic)));
		if(!pathIsWithin(neuro_root, path)) return neuroMCPError("KNOWLEDGE_PATH_NOT_ALLOWED", "Файл базы знаний находится вне каталога NeuroControl.");
		QFile file(path);
		if(!file.open(QIODevice::ReadOnly) || file.size() > 128 * 1024) return neuroMCPError("KNOWLEDGE_DOCUMENT_UNAVAILABLE", QStringLiteral("Не удалось прочитать документ %1.").arg(documents.value(topic)));
		return QJsonObject{{"topic", topic}, {"document", documents.value(topic)}, {"path", QFileInfo(path).absoluteFilePath()}, {"text", QString::fromUtf8(file.readAll())}};
	}
	if(tool == QStringLiteral("neuro.select_source"))
	{
		const QString source_id = arguments.value(QStringLiteral("source_id")).toString();
		int index = -1;
		if(source_id == QStringLiteral("recorded_edf")) index = 0;
		else if(source_id == QStringLiteral("brainflow_synthetic")) index = 1;
		else if(source_id == QStringLiteral("lsl_live")) index = 3;
		else if(source_id == QStringLiteral("lsl_recorded"))
		{
			if(lsl_loaded_recording_id_.isEmpty() || lsl_recording_source_timestamps_.empty())
				return neuroMCPError("LSL_RECORDING_NOT_LOADED", QStringLiteral("Сначала загрузите завершённую запись через neuro.load_lsl_recording."));
			index = 4;
		}
		else if(source_id == QStringLiteral("live_eeg")) return neuroMCPError("HARDWARE_UNAVAILABLE", "Физическое ЭЭГ-устройство не подключено; драйвер для медицинского оборудования не реализован.");
		else return neuroMCPError("SOURCE_NOT_FOUND", "Неизвестный source_id. Вызовите neuro.list_sources.");
		source_->setCurrentIndex(index);
		return QJsonObject{{"selected_source", source_id}, {"playback_running", playing_}, {"avatar_control_enabled", client_->neuro_control_enabled}};
	}
	if(tool == QStringLiteral("neuro.load_recording"))
	{
		const QString id = arguments.value(QStringLiteral("recording_id")).toString().trimmed();
		const QString requested_path = arguments.value(QStringLiteral("path")).toString().trimmed();
		if(!id.isEmpty() && !requested_path.isEmpty()) return neuroMCPError("RECORDING_SELECTOR_AMBIGUOUS", "Укажите только один параметр: recording_id или path.");
		if(id.isEmpty() && requested_path.isEmpty()) return neuroMCPError("RECORDING_SELECTOR_REQUIRED", "Укажите recording_id из neuro.list_recordings или полный путь внутри разрешённой папки NeuroControl.");
		const RecordingSelection selected = selectRecording(arguments);
		if(selected.path.isEmpty()) return neuroMCPError("RECORDING_NOT_FOUND_OR_PATH_NOT_ALLOWED", "Запись не найдена, её формат не поддерживается или путь находится вне datasets/raw и datasets/processed.");
		if(selected.prepared)
		{
			const QJsonObject info = preparedRecordingInfo(selected.id, selected.metadata_path);
			if(info.isEmpty()) return neuroMCPError("PREPARED_RECORDING_INVALID", "Подготовленный F32-поток отсутствует, повреждён или его JSON-метаданные недействительны.");
			loadRecordingPath(selected.metadata_path);
			if(QFileInfo(loaded_recording_metadata_path_).canonicalFilePath() != QFileInfo(selected.metadata_path).canonicalFilePath() || recorded_samples_.empty())
				return neuroMCPError("RECORDING_LOAD_FAILED", QStringLiteral("Не удалось загрузить подготовленную запись %1; подробности доступны в диагностике NeuroControl.").arg(selected.id));
			const bool compatible = classifier_ && recording_channels_ == model_channels_ && sample_rate_ == model_metadata_.value(QStringLiteral("sample_rate_hz")).toInt();
			return QJsonObject{{"loaded", true}, {"state", QStringLiteral("loaded")}, {"recording_id", selected.id},
				{"metadata_path", selected.metadata_path}, {"data_path", info.value(QStringLiteral("path"))},
				{"source_file", info.value(QStringLiteral("source_file"))}, {"source_sha256", info.value(QStringLiteral("source_sha256"))},
				{"sample_rate_hz", info.value(QStringLiteral("sample_rate_hz"))}, {"samples", info.value(QStringLiteral("samples"))},
				{"duration_seconds", info.value(QStringLiteral("duration_seconds"))}, {"channels", info.value(QStringLiteral("channels"))},
				{"annotations", info.value(QStringLiteral("annotations"))}, {"preprocessing", info.value(QStringLiteral("preprocessing"))},
				{"compatible_with_loaded_model", compatible},
				{"playback_started", false}, {"raw_eeg_contents_sent", false}};
		}
		if(!prepareAndLoadRecording(selected.path))
			return neuroMCPError(training_process_->state() != QProcess::NotRunning ? "JOB_ALREADY_RUNNING" : "RECORDING_IMPORT_UNAVAILABLE",
				last_error_.isEmpty() ? (python_path_.isEmpty() ? QStringLiteral("Подготовка EDF/BDF требует настроенного Python-окружения NeuroControl с MNE, NumPy и SciPy.") : QStringLiteral("Не удалось начать подготовку записи; проверьте журнал NeuroControl.")) : last_error_);
		return QJsonObject{{"accepted", true}, {"state", QStringLiteral("preparing")}, {"job_id", active_job_id_}, {"recording_id", selected.id},
			{"source_path", selected.path}, {"format", QFileInfo(selected.path).suffix().toUpper()}, {"raw_eeg_contents_sent", false},
			{"message", QStringLiteral("EDF/BDF обрабатывается существующим асинхронным импортёром; после завершения проверьте neuro.get_training_status и neuro.get_recording_info.")}};
	}
	if(tool == QStringLiteral("neuro.load_model"))
	{
		const QString id = arguments.value(QStringLiteral("model_id")).toString();
		const QString path = modelPath(id);
		if(path.isEmpty()) return neuroMCPError("MODEL_NOT_FOUND", QStringLiteral("Модель %1 отсутствует в %2.").arg(id, model_dir));
		loadModelPath(path);
		if(loaded_model_path_.isEmpty() || QFileInfo(loaded_model_path_).absoluteFilePath() != QFileInfo(path).absoluteFilePath())
			return neuroMCPError("MODEL_LOAD_FAILED", last_error_.isEmpty() ? QStringLiteral("Модель не загружена. Проверьте формат и журнал NeuroControl.") : last_error_);
		return QJsonObject{{"loaded", true}, {"model", QFileInfo(path).fileName()}, {"path", path},
			{"model_threshold", model_threshold_}, {"controller_safety_floor", 0.75}, {"effective_threshold", applied_threshold_},
			{"compatible_with_loaded_recording", !recorded_samples_.empty() && recording_channels_ == model_channels_ && sample_rate_ == model_metadata_.value("sample_rate_hz").toInt()}};
	}
	if(tool == QStringLiteral("neuro.start_playback"))
	{
		if(source_->currentIndex() == 2) return neuroMCPError("HARDWARE_UNAVAILABLE", "Физическое ЭЭГ-устройство не подключено.");
		if(source_->currentIndex() == 3) return neuroMCPError("LSL_USE_CONNECT_TOOL", "Для LSL используйте neuro.discover_lsl_streams и neuro.connect_lsl_stream.");
		if(source_->currentIndex() == 4) return neuroMCPError("LSL_USE_RECORDING_PLAYBACK_TOOL", "Для архивной LSL-записи используйте neuro.play_lsl_recording.");
		if(source_->currentIndex() == 0 && recorded_samples_.empty()) return neuroMCPError("RECORDING_NOT_LOADED", "Сначала загрузите EDF-запись.");
		start();
		if(!playing_) return neuroMCPError("PLAYBACK_NOT_STARTED", last_error_.isEmpty() ? QStringLiteral("Воспроизведение не запустилось; см. диагностику NeuroControl.") : last_error_);
		return QJsonObject{{"playing", true}, {"paused", paused_}, {"source", neuroSourceName(source_->currentIndex())},
			{"classifier_ready", classifier_ != nullptr}, {"classification_compatible", classifier_ && recording_channels_ == model_channels_ && sample_rate_ == model_metadata_.value("sample_rate_hz").toInt()},
			{"avatar_control_enabled", client_->neuro_control_enabled}};
	}
	if(tool == QStringLiteral("neuro.pause_playback"))
	{
		if(!playing_) return neuroMCPError("PLAYBACK_NOT_RUNNING", "Воспроизведение уже остановлено.");
		if(!paused_) pausePlayback();
		return QJsonObject{{"playing", playing_}, {"paused", paused_}, {"playback_sample", next_sample_}};
	}
	if(tool == QStringLiteral("neuro.resume_playback"))
	{
		if(!paused_) return neuroMCPError("PLAYBACK_NOT_PAUSED", QStringLiteral("Воспроизведение не находится на паузе."));
		resumePlayback();
		return QJsonObject{{"playing", playing_}, {"paused", paused_}, {"playback_sample", next_sample_},
			{"source", neuroSourceName(source_->currentIndex())}, {"classifier_connected", source_->currentIndex() == 0 && classifier_ != nullptr},
			{"avatar_control_enabled", source_->currentIndex() == 0 && client_->neuro_control_enabled}};
	}
	if(tool == QStringLiteral("neuro.stop_playback"))
	{
		stop();
		return QJsonObject{{"playing", playing_}, {"paused", paused_}, {"avatar_control_enabled", client_->neuro_control_enabled}, {"last_command", "STOP"}};
	}
	if(tool == QStringLiteral("neuro.seek_playback"))
	{
		if(source_->currentIndex() == 4) return neuroMCPError("LSL_USE_RECORDING_SEEK_TOOL", "Для архивной LSL-записи используйте neuro.seek_lsl_recording.");
		if(source_->currentIndex() != 0 || recorded_samples_.empty() || sample_rate_ <= 0) return neuroMCPError("RECORDING_NOT_LOADED", "Позиционирование доступно только для загруженной записи EDF.");
		const double seconds = arguments.value(QStringLiteral("position_seconds")).toDouble(-1.0);
		const double total_seconds = static_cast<double>(recorded_samples_.size() / static_cast<std::size_t>(channel_count_)) / sample_rate_;
		if(!std::isfinite(seconds) || seconds < 0.0 || seconds > total_seconds) return neuroMCPError("SEEK_OUT_OF_RANGE", QStringLiteral("position_seconds должен быть от 0 до %1.").arg(total_seconds, 0, 'f', 2));
		seekPlayback(static_cast<int>(std::round(seconds * sample_rate_)));
		return QJsonObject{{"position_seconds", static_cast<double>(next_sample_) / sample_rate_}, {"total_seconds", total_seconds}, {"playback_running", playing_}};
	}
	if(tool == QStringLiteral("neuro.set_avatar_control"))
	{
		const bool enabled = arguments.value(QStringLiteral("enabled")).toBool(false);
		if(enabled)
		{
			const bool compatible = source_->currentIndex() == 0 && playing_ && !paused_ && classifier_ && !recorded_samples_.empty() &&
				recording_channels_ == model_channels_ && sample_rate_ == model_metadata_.value("sample_rate_hz").toInt();
			if(!compatible) return neuroMCPError("CONTROL_PREREQUISITES_NOT_MET", "Для разрешения управления нужны источник EDF, загруженные совместимые модель и запись, а также активное воспроизведение.");
			if(!control_enabled_->isEnabled()) return neuroMCPError("CONTROL_NOT_AVAILABLE", "Текущие защитные условия NeuroControl запрещают управление.");
		}
		control_enabled_->setChecked(enabled);
		return QJsonObject{{"enabled", client_->neuro_control_enabled}, {"playing", playing_}, {"source", neuroSourceName(source_->currentIndex())},
			{"effective_threshold", applied_threshold_}, {"controller_trace", controllerTrace()}};
	}
	if(tool == QStringLiteral("neuro.emergency_stop"))
	{
		stop();
		if(client_->neuro_control_enabled || client_->neuro_control_command.type != neurocontrol::CommandType::Stop ||
			client_->neuro_control_command.event_id != next_command_event_id_)
			return neuroMCPError("EMERGENCY_STOP_FAILED", "Контроллер не подтвердил отключение управления и команду STOP.");
		appendDiagnostic(tr("Аварийная остановка по запросу MCP: воспроизведение и управление прекращены."));
		const QJsonObject lsl_status = lslStatusJson();
		return QJsonObject{{"emergency_stop", true}, {"playback_running", playing_}, {"avatar_control_enabled", client_->neuro_control_enabled},
			{"command", "STOP"}, {"event_id", static_cast<qint64>(next_command_event_id_)},
			{"lsl_state", lsl_status.value(QStringLiteral("state"))},
			{"lsl_cleanup_pending", lsl_status.value(QStringLiteral("reader_cleanup_pending"))}};
	}
	if(tool == QStringLiteral("neuro.train_model"))
	{
		if(training_process_->state() != QProcess::NotRunning) return neuroMCPError("JOB_ALREADY_RUNNING", QStringLiteral("Сейчас выполняется задача %1 (%2).").arg(active_job_id_, active_job_kind_));
		const QString task = arguments.value(QStringLiteral("task")).toString();
		if(!task.isEmpty())
		{
			const int task_index = training_task_->findData(task);
			if(task_index < 0) return neuroMCPError("UNSUPPORTED_TRAINING_TASK", "Поддерживаются только task=feet и task=hands.");
			training_task_->setCurrentIndex(task_index);
		}
		const auto setParameter = [&](const QString& key, QDoubleSpinBox* box) -> bool {
			if(!arguments.contains(key)) return true;
			const double value = arguments.value(key).toDouble(std::numeric_limits<double>::quiet_NaN());
			if(!std::isfinite(value) || value < box->minimum() || value > box->maximum()) return false;
			box->setValue(value);
			return true;
		};
		if(!setParameter(QStringLiteral("low_hz"), low_cutoff_) || !setParameter(QStringLiteral("high_hz"), high_cutoff_) ||
			!setParameter(QStringLiteral("window_seconds"), epoch_seconds_) || !setParameter(QStringLiteral("step_seconds"), epoch_step_seconds_) ||
			!setParameter(QStringLiteral("offset_seconds"), event_offset_seconds_) || !setParameter(QStringLiteral("threshold"), confidence_threshold_))
			return neuroMCPError("TRAINING_PARAMETER_OUT_OF_RANGE", "Один из параметров обучения вне диапазона интерфейса.");
		if(arguments.contains(QStringLiteral("notch_50"))) notch_filter_->setChecked(arguments.value(QStringLiteral("notch_50")).toBool());
		if(arguments.contains(QStringLiteral("channels")))
		{
			QStringList requested;
			for(const QJsonValue& value : arguments.value(QStringLiteral("channels")).toArray()) requested.push_back(value.toString());
			if(requested.size() < 2) return neuroMCPError("TOO_FEW_CHANNELS", "Для CSP + LDA требуется не менее двух каналов.");
			for(const QString& name : requested)
				if(training_channels_->findItems(name, Qt::MatchExactly).isEmpty()) return neuroMCPError("UNKNOWN_CHANNEL", QStringLiteral("Канал %1 не входит в разрешённый список 10–10.").arg(name));
			for(int i = 0; i < training_channels_->count(); ++i)
				training_channels_->item(i)->setCheckState(requested.contains(training_channels_->item(i)->text()) ? Qt::Checked : Qt::Unchecked);
		}
		refreshDatasetStatus();
		if(!dataset_verified_) return neuroMCPError("DATASET_NOT_VERIFIED", dataset_status_text_);
		if(low_cutoff_->value() >= high_cutoff_->value()) return neuroMCPError("INVALID_FILTER_BAND", "Верхняя частота фильтра должна быть выше нижней.");
		if(python_path_.isEmpty() || !QFileInfo::exists(python_path_)) return neuroMCPError("PYTHON_NOT_CONFIGURED", "Укажите Python с MNE, NumPy, SciPy и scikit-learn во вкладке «Обучение модели».");
		train_button_->click();
		if(training_process_->state() == QProcess::NotRunning) return neuroMCPError("TRAINING_NOT_STARTED", last_error_.isEmpty() ? "Не удалось запустить обучение; см. журнал NeuroControl." : last_error_);
		return QJsonObject{{"accepted", true}, {"state", "running"}, {"job_id", active_job_id_}, {"kind", "training"},
			{"task", training_task_->currentData().toString()}, {"threshold", confidence_threshold_->value()}, {"asynchronous", true}};
	}
	if(tool == QStringLiteral("neuro.run_diagnostics"))
	{
		QJsonArray checks;
		checks.append(QJsonObject{{"name", "neurocontrol_root"}, {"ok", !neuro_root.isEmpty()}, {"path", neuro_root}});
		checks.append(QJsonObject{{"name", "python"}, {"ok", !python_path_.isEmpty() && QFileInfo::exists(python_path_)}, {"path", python_path_}});
		checks.append(QJsonObject{{"name", "training_script"}, {"ok", QFileInfo::exists(training_script_path_)}, {"path", training_script_path_}});
		checks.append(QJsonObject{{"name", "evaluation_script"}, {"ok", QFileInfo::exists(evaluation_script_path_)}, {"path", evaluation_script_path_}});
		checks.append(QJsonObject{{"name", "recording_loaded"}, {"ok", !recorded_samples_.empty()}, {"path", loaded_recording_metadata_path_}});
		checks.append(QJsonObject{{"name", "model_loaded"}, {"ok", classifier_ != nullptr}, {"path", loaded_model_path_}});
		checks.append(QJsonObject{{"name", "model_compatible"}, {"ok", classifier_ && recording_channels_ == model_channels_ && sample_rate_ == model_metadata_.value("sample_rate_hz").toInt()}});
		checks.append(QJsonObject{{"name", "dataset_verified"}, {"ok", dataset_verified_}, {"details", dataset_status_text_}});
		const QJsonObject client_build = currentClientBuildInfo();
		checks.append(QJsonObject{{"name", "client_build"}, {"ok", client_build.value(QStringLiteral("executable_exists")).toBool()}, {"details", client_build}});
		checks.append(QJsonObject{{"name", "avatar_control_enabled"}, {"ok", client_->neuro_control_enabled}, {"controller_trace", controllerTrace()}});
		return QJsonObject{{"checks", checks}, {"last_error", last_error_}, {"training_job", active_job_id_}};
	}
	return neuroMCPError("UNKNOWN_NEURO_TOOL", QStringLiteral("Инструмент %1 не реализован в NeuroControl.").arg(tool));
}

void NeuroControlWidget::appendDiagnostic(const QString& text)
{
	const qint64 now = QDateTime::currentMSecsSinceEpoch();
	if(text == last_diagnostic_)
	{
		++diagnostic_repeat_count_;
		last_diagnostic_time_ms_ = now;
		return;
	}
	if(!last_diagnostic_.isEmpty() && diagnostic_repeat_count_ > 0)
	{
		const QString repeated = QStringLiteral("[%1] [INFO] [%2] %3 (повторено %4 раз)")
			.arg(QDateTime::currentDateTime().toString("HH:mm:ss.zzz"), last_diagnostic_category_, last_diagnostic_)
			.arg(diagnostic_repeat_count_);
		diagnostic_entries_.push_back(repeated);
	}
	diagnostic_repeat_count_ = 0;
	last_diagnostic_ = text;
	last_diagnostic_time_ms_ = now;
	QString level = QStringLiteral("INFO");
	const QString lower = text.toLower();
	if(lower.contains(QStringLiteral("не удалось")) || lower.contains(QStringLiteral("ошиб")) || lower.contains(QStringLiteral("поврежд")) || lower.contains(QStringLiteral("прерван"))) level = QStringLiteral("ERROR");
	else if(lower.contains(QStringLiteral("аварийн")) || lower.contains(QStringLiteral("не подключено")) || lower.contains(QStringLiteral("переполнение")) || lower.contains(QStringLiteral("ненадёж"))) level = QStringLiteral("WARNING");
	else if(lower.startsWith(QStringLiteral("debug:"))) level = QStringLiteral("DEBUG");
	const QString category = lower.contains(QStringLiteral("brainflow")) || lower.contains(QStringLiteral("синтет")) ? tr("BrainFlow") :
		lower.contains(QStringLiteral("edf")) || lower.contains(QStringLiteral("запис")) || lower.contains(QStringLiteral("событи")) ? tr("EDF") :
		lower.contains(QStringLiteral("python")) || lower.contains(QStringLiteral("mne")) || lower.contains(QStringLiteral("зависим")) ? tr("Python") :
		lower.contains(QStringLiteral("обуч")) || lower.contains(QStringLiteral("набор")) || lower.contains(QStringLiteral("physionet")) ? tr("Обучение") :
		lower.contains(QStringLiteral("модел")) || lower.contains(QStringLiteral("классиф")) || lower.contains(QStringLiteral("оценка ээг")) ? tr("Классификатор") :
		lower.contains(QStringLiteral("аватар")) || lower.contains(QStringLiteral("команд")) || lower.contains(QStringLiteral("поворот")) ? tr("Аватар") : tr("Интерфейс");
	last_diagnostic_category_ = category;
	if(level == QStringLiteral("ERROR")) last_error_ = text;
	diagnostic_entries_.push_back(QStringLiteral("[%1] [%2] [%3] %4")
		.arg(QDateTime::currentDateTime().toString("HH:mm:ss.zzz"), level, category, text));
	while(diagnostic_entries_.size() > 2000) diagnostic_entries_.removeFirst();
	refreshDiagnosticView();
	refreshOverviewStatus();
}

void NeuroControlWidget::refreshDiagnosticView()
{
	if(!diagnostics_) return;
	const QString level = diagnostic_filter_ ? diagnostic_filter_->currentText() : tr("Все уровни");
	const QString category = diagnostic_category_filter_ ? diagnostic_category_filter_->currentText() : tr("Все категории");
	QStringList visible;
	for(const QString& entry : diagnostic_entries_)
		if((level == tr("Все уровни") || entry.contains(QStringLiteral("[%1]").arg(level))) &&
			(category == tr("Все категории") || entry.contains(QStringLiteral("[%1]").arg(category)))) visible.push_back(entry);
	diagnostics_->setPlainText(visible.join(QLatin1Char('\n')));
	diagnostics_->moveCursor(QTextCursor::End);
}

void NeuroControlWidget::refreshDiagnosticsSummary()
{
	if(!diagnostics_summary_) return;
	const qint64 now = QDateTime::currentMSecsSinceEpoch();
	if(source_->currentIndex() != 3 && playing_ && !paused_ && last_rate_update_ms_ > 0 && now - last_rate_update_ms_ >= 1000)
	{
		receive_rate_hz_ = static_cast<double>(received_samples_ - rate_sample_snapshot_) * 1000.0 / static_cast<double>(now - last_rate_update_ms_);
		rate_measured_ = true;
		rate_sample_snapshot_ = received_samples_;
		last_rate_update_ms_ = now;
	}
	const QString source = source_->currentIndex() == 3 ? tr("LSL") : source_->currentIndex() == 1 ? tr("BrainFlow Synthetic Board") : source_->currentIndex() == 0 ? tr("EDF/F32") : tr("устройство не подключено");
	const bool lsl = source_->currentIndex() == 3;
	const QString nominal = lsl ? (lsl_active_stream_.nominal_hz > 0 ? QString::number(lsl_active_stream_.nominal_hz, 'g', 8) + QStringLiteral(" Гц") : tr("не указана")) : QString::number(sample_rate_) + QStringLiteral(" Гц");
	const QString rate = !playing_ ? tr("поток остановлен") : paused_ ? tr("воспроизведение на паузе") : !rate_measured_ ? tr("измеряется") :
		QStringLiteral("%1 отсч/с · номинально: %2").arg(receive_rate_hz_, 0, 'f', 1).arg(nominal);
	const QString inference = lsl || last_classification_time_.isEmpty() ? QStringLiteral("—") : QStringLiteral("%1 мс").arg(last_inference_ms_, 0, 'f', 2);
	const QString controller = lsl ? tr("отключён") : control_enabled_ && control_enabled_->isChecked() ? tr("разрешён") : tr("остановлен");
	QString heartbeat = lsl ? tr("не используется") : tr("ожидание обработанных отсчётов");
	if(!lsl && client_ && client_->neuro_control_eeg_heartbeat.available)
	{
		const double age = Clock::getTimeSinceInit() - client_->neuro_control_eeg_heartbeat.timestamp_seconds;
		heartbeat = client_->neuro_control_eeg_heartbeat.valid ?
			tr("активен · отсчёт %1 · %2 с назад").arg(client_->neuro_control_eeg_heartbeat.sample_index).arg(age, 0, 'f', 2) :
			tr("отсчёт недостоверен · %1").arg(client_->neuro_control_eeg_heartbeat.sample_index);
	}
	diagnostics_summary_->setText(tr("Источник: %1 · получено %2 отсчётов · локально отброшено %3\nЧастота приёма: %4 · задержка классификации: %5\nПоследняя классификация: %6 · контроллер: %7 · heartbeat ЭЭГ: %8")
		.arg(source).arg(received_samples_).arg(dropped_samples_).arg(rate, inference,
			last_classification_time_.isEmpty() ? tr("—") : last_classification_time_, controller, heartbeat));
	refreshOverviewStatus();
}

void NeuroControlWidget::publishStop()
{
	neurocontrol::Command command;
	command.type = neurocontrol::CommandType::Stop;
	command.timestamp_seconds = Clock::getTimeSinceInit();
	command.sample_timestamp_seconds = command.timestamp_seconds;
	command.minimum_confidence = applied_threshold_;
	command.event_id = ++next_command_event_id_;
	command.quality = neurocontrol::SignalQuality::Good;
	command.stream_generation = neuro_stream_generation_;
	client_->setNeuroControlCommand(command);
	appendExperimentEvent(QJsonObject{{QStringLiteral("event_type"), QStringLiteral("command_submitted")},
		{QStringLiteral("command"), QStringLiteral("STOP")},
		{QStringLiteral("command_event_id"), static_cast<qint64>(command.event_id)},
		{QStringLiteral("stream_generation"), static_cast<qint64>(command.stream_generation)},
		{QStringLiteral("controller_enabled"), client_->neuro_control_enabled},
		{QStringLiteral("sample_index"), static_cast<qint64>(command.sample_index)},
		{QStringLiteral("submission_result"), client_->neuro_control_command.event_id == command.event_id ? QStringLiteral("submitted") : QStringLiteral("rejected")}});
	state_label_->setText(tr("Состояние: ОСТАНОВКА"));
	confidence_->setValue(0);
	const bool control_enabled = control_enabled_ && control_enabled_->isChecked();
	if(avatar_status_label_) avatar_status_label_->setText(control_enabled ? tr("СТОИТ") : tr("УПРАВЛЕНИЕ ОТКЛЮЧЕНО"));
	if(avatar_action_label_) avatar_action_label_->setText(tr("СТОИТ"));
	if(avatar_confidence_) avatar_confidence_->setValue(0);
	if(positive_class_probability_) positive_class_probability_->setValue(0);
	if(negative_class_probability_) negative_class_probability_->setValue(0);
	if(avatar_prediction_label_) avatar_prediction_label_->setText(tr("Последняя команда: STOP"));
	refreshOverviewStatus();
}

void NeuroControlWidget::updateChannels(const std::vector<std::string>& names,
	const std::vector<std::string>& types, const std::vector<std::string>& units)
{
	active_channels_ = names;
	active_channel_types_ = types;
	active_channel_units_ = units;
	channel_list_->blockSignals(true);
	channel_list_->clear();
	spectrum_channel_->blockSignals(true);
	spectrum_channel_->clear();
	for(std::size_t index = 0; index < names.size(); ++index)
	{
		const QString name = QString::fromUtf8(names[index].c_str());
		const QString type = index < types.size() ? QString::fromUtf8(types[index].c_str()) : QString();
		const QString unit = index < units.size() ? QString::fromUtf8(units[index].c_str()) : QString();
		const QString display_type = type.compare(QStringLiteral("ECG"), Qt::CaseInsensitive) == 0 ? tr("ЭКГ") :
			type.compare(QStringLiteral("EEG"), Qt::CaseInsensitive) == 0 ? tr("ЭЭГ") : type;
		const QString label = display_type.isEmpty() ? name : QStringLiteral("%1 · %2").arg(name, display_type);
		auto* item = new QListWidgetItem(label, channel_list_);
		item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
		item->setCheckState(Qt::Checked);
		item->setData(Qt::UserRole, name);
		if(!unit.isEmpty()) item->setToolTip(tr("Тип: %1 · исходная единица: %2 · отображение: мкВ").arg(display_type, unit));
		spectrum_channel_->addItem(label);
	}
	spectrum_channel_->blockSignals(false);
	channel_list_->blockSignals(false);
	waveform_->setChannels(names, sample_rate_ > 0 ? sample_rate_ : 250);
	waveform_->setChannelTypes(types);
	overview_waveform_->setChannels(names, sample_rate_ > 0 ? sample_rate_ : 250);
	overview_waveform_->setChannelTypes(types);
	std::vector<bool> overview_visible(names.size(), false);
	if(!overview_visible.empty()) overview_visible[0] = true;
	overview_waveform_->setVisible(overview_visible);
	waveform_->setEvents(event_marks_);
	refreshSpectrum();
	if(!names.empty()) source_status_label_->setText(tr("Источник: %1 канал(ов) · %2 Гц").arg(names.size()).arg(sample_rate_));
}

void NeuroControlWidget::refreshSpectrum()
{
	const int selected = spectrum_channel_ ? spectrum_channel_->currentIndex() : -1;
	if(!spectrum_ || !waveform_ || selected < 0 || sample_rate_ <= 0)
	{
		if(spectrum_) spectrum_->setSpectrum({}, sample_rate_);
		return;
	}
	spectrum_->setSpectrum(waveform_->channelTail(selected, sample_rate_ * 2), sample_rate_);
}

void NeuroControlWidget::refreshDatasetStatus()
{
	dataset_verified_ = false;
	const bool hands = training_task_ && training_task_->currentData().toString() == QStringLiteral("hands");
	const int runs[3] = {hands ? 4 : 6, hands ? 8 : 10, hands ? 12 : 14};
	const QString root = findNeurocontrolRoot();
	if(root.isEmpty()) dataset_status_text_ = tr("Каталог neurocontrol не найден рядом с исходниками или приложением.");
	else
	{
		const QDir raw(QDir(root).filePath(QStringLiteral("datasets/raw")));
		QFile report_file(raw.filePath(hands ? QStringLiteral("audit_hands.json") : QStringLiteral("audit.json")));
		const QJsonArray records = report_file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(report_file.readAll()).object().value("recordings").toArray() : QJsonArray();
		dataset_status_text_.clear();
		for(int run : runs)
		{
			const QString name = QStringLiteral("S001R%1.edf").arg(run, 2, 10, QLatin1Char('0'));
			QFile file(raw.filePath(name));
			if(!file.open(QIODevice::ReadOnly)) { dataset_status_text_ = tr("Нет %1. Нажмите «Загрузить набор».").arg(name); break; }
			QJsonObject verified;
			for(const QJsonValue& value : records)
				if(QFileInfo(value.toObject().value("path").toString()).fileName() == name) { verified = value.toObject(); break; }
			if(verified.isEmpty()) { dataset_status_text_ = tr("%1 найден, но нет отчёта MNE. Нажмите «Проверить файлы EDF через MNE».").arg(name); break; }
			const QByteArray bytes = file.readAll();
			const QString hash = QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
			const QJsonObject events = verified.value("annotations").toObject();
			if(bytes.size() != verified.value("bytes").toInt() || hash.compare(verified.value("sha256").toString(), Qt::CaseInsensitive) != 0 ||
			   !verified.value("official_sha256_verified").toBool() || !verified.value("finite_signal_values_verified").toBool() ||
			   verified.value("sample_rate_hz").toDouble() != 160.0 || verified.value("eeg_channels").toInt() != 64 ||
			   events.value("T0").toInt() <= 0 || events.value("T1").toInt() <= 0 || events.value("T2").toInt() <= 0)
			{
				dataset_status_text_ = tr("%1 повреждён или не соответствует проверенному EDF. Повторите проверку данных.").arg(name);
				break;
			}
		}
		if(dataset_status_text_.isEmpty())
		{
			dataset_verified_ = true;
			dataset_status_text_ = tr("PhysioNet EEG Motor Movement/Imagery · S001 · 3 записи · 64 канала · 160 Hz · T0/T1/T2 · MNE и SHA-256 проверены");
		}
	}
	if(dataset_status_label_) dataset_status_label_->setText(dataset_status_text_);
	if(dataset_training_status_) dataset_training_status_->setText(dataset_status_text_);
	if(train_button_ && (!training_process_ || training_process_->state() == QProcess::NotRunning)) train_button_->setEnabled(dataset_verified_);
	refreshOverviewStatus();
}

void NeuroControlWidget::refreshOverviewStatus()
{
	if(!system_details_label_) return;
	if(dataset_status_label_) dataset_status_label_->setText(dataset_status_text_);
	if(model_details_label_)
	{
		if(loaded_model_path_.isEmpty()) model_details_label_->setText(tr("Модель не обучена или не выбрана. Загрузите JSON или обучите CSP + LDA."));
		else
		{
			const QJsonArray training_runs = model_metadata_.value("training_runs").toArray();
			const QString runs = training_runs.size() >= 2 ? QStringLiteral("R%1/R%2").arg(training_runs[0].toInt(), 2, 10, QLatin1Char('0')).arg(training_runs[1].toInt(), 2, 10, QLatin1Char('0')) : tr("не указаны");
			model_details_label_->setText(tr("%1 · CSP + LDA · S001 · обучение %2 · проверка R%3 · %4 UTC · %5 · порог модели %6 · защитный минимум 0.75 · применяемый %7")
				.arg(QFileInfo(loaded_model_path_).fileName()).arg(runs).arg(model_metadata_.value("test_run").toInt(), 2, 10, QLatin1Char('0'))
				.arg(model_metadata_.value("trained_at_utc").toString(tr("дата не указана")))
				.arg(model_positive_label_)
				.arg(model_threshold_, 0, 'f', 2)
				.arg(applied_threshold_, 0, 'f', 2));
		}
	}
	if(recognition_details_label_) recognition_details_label_->setText(last_classification_time_.isEmpty() ? tr("Предсказаний нет") :
		tr("%1 · %2").arg(last_classification_time_, avatar_prediction_label_->text()));
	const bool python_ready = !python_path_.isEmpty() && QFileInfo::exists(python_path_);
	const bool tools_ready = QFileInfo::exists(training_script_path_) && QFileInfo::exists(download_script_path_) && QFileInfo::exists(audit_script_path_);
	int errors = 0, warnings = 0;
	for(const QString& entry : diagnostic_entries_)
	{
		if(entry.contains(QStringLiteral("[ERROR]"))) ++errors;
		else if(entry.contains(QStringLiteral("[WARNING]"))) ++warnings;
	}
	system_details_label_->setText(tr("Python: %1 · инструменты: %2 · ошибок: %3 · предупреждений: %4")
		.arg(python_ready ? tr("найден") : tr("не найден"), tools_ready ? tr("найдены") : tr("не найдены"))
		.arg(errors).arg(warnings));
}

void NeuroControlWidget::refreshControlAvailability()
{
	const bool recorded = source_ && source_->currentIndex() == 0;
	const bool compatible = recorded && classifier_ && !recorded_samples_.empty() &&
		recording_channels_ == model_channels_ && sample_rate_ == model_metadata_.value("sample_rate_hz").toInt();
	if(control_enabled_) control_enabled_->setEnabled(compatible);
	if(avatar_control_enabled_) avatar_control_enabled_->setEnabled(compatible);
	if(!compatible && control_enabled_ && control_enabled_->isChecked()) control_enabled_->setChecked(false);
	const bool lsl_recorded = source_ && source_->currentIndex() == 4 && !lsl_loaded_recording_id_.isEmpty() && !lsl_recording_playback_samples_.empty();
	if(start_button_ && !playing_) start_button_->setEnabled((recorded && !recorded_samples_.empty()) || (source_ && source_->currentIndex() == 1) || lsl_recorded);
	if(save_model_button_) save_model_button_->setEnabled(!loaded_model_path_.isEmpty() && QFileInfo::exists(loaded_model_path_));
}

void NeuroControlWidget::updateRecordingMetadata(const QJsonObject& metadata)
{
	event_marks_.clear();
	const int run = metadata.value("run").toInt();
	const QJsonArray annotations = metadata.value("annotations").toArray();
	for(const QJsonValue& value : annotations)
	{
		const QJsonObject event = value.toObject();
		const QString code = event.value("label").toString();
		QString meaning = code;
		if((run == 4 || run == 6 || run == 8 || run == 10 || run == 12 || run == 14) && code == QStringLiteral("T0")) meaning = tr("T0 покой");
		else if((run == 6 || run == 10 || run == 14) && code == QStringLiteral("T1")) meaning = tr("T1 обе кисти");
		else if((run == 6 || run == 10 || run == 14) && code == QStringLiteral("T2")) meaning = tr("T2 обе стопы");
		else if((run == 4 || run == 8 || run == 12) && code == QStringLiteral("T1")) meaning = tr("T1 левая кисть");
		else if((run == 4 || run == 8 || run == 12) && code == QStringLiteral("T2")) meaning = tr("T2 правая кисть");
		const int sample = event.value("start_sample").toInt(-1);
		if(sample >= 0) event_marks_.push_back({sample, meaning.toStdString()});
	}
	waveform_->setEvents(event_marks_);
	const int samples = metadata.value("samples").toInt();
	const double duration = sample_rate_ > 0 ? static_cast<double>(samples) / sample_rate_ : 0.0;
	const int total_seconds = static_cast<int>(std::round(duration));
	const QString mmss = QStringLiteral("%1:%2").arg(total_seconds / 60, 2, 10, QLatin1Char('0')).arg(total_seconds % 60, 2, 10, QLatin1Char('0'));
	playback_time_label_->setText(QStringLiteral("00:00 / %1").arg(mmss));
	seek_slider_->setRange(0, std::max(0, samples - 1));
	seek_slider_->setEnabled(source_->currentIndex() == 0 && samples > 0);
	playback_speed_->setEnabled(source_->currentIndex() == 0);
	if(diagnostics_ && !event_marks_.empty())
		appendDiagnostic(run > 0 ? tr("Загружены %1 маркера событий серии R%2; значение T1/T2 определено по серии.").arg(event_marks_.size()).arg(run, 2, 10, QLatin1Char('0')) : tr("Загружены %1 маркера EDF; назначение кодов событий показано без предположений.").arg(event_marks_.size()));
}

void NeuroControlWidget::pausePlayback()
{
	if(!playing_ || paused_) return;
	if(lsl_recording_playback_)
	{
		paused_ = true;
		timer_->stop();
		start_button_->setEnabled(true);
		start_button_->setText(tr("Продолжить"));
		pause_button_->setEnabled(true);
		pause_button_->setText(tr("Продолжить"));
		lsl_recording_status_label_->setText(tr("Запись %1 · пауза · исходные timestamps сохранены")
			.arg(lsl_loaded_recording_id_));
		appendDiagnostic(tr("Архивное воспроизведение LSL приостановлено; классификатор и управление аватаром не затронуты."));
		return;
	}
	appendExperimentEvent(QJsonObject{{QStringLiteral("event_type"), QStringLiteral("playback_paused")},
		{QStringLiteral("playback_sample"), next_sample_}, {QStringLiteral("stream_generation"), static_cast<qint64>(neuro_stream_generation_)}});
	paused_ = true;
	timer_->stop();
	if(classifier_) classifier_->reset();
	neuro_stream_generation_ = client_->resetNeuroControlStream();
	publishStop();
	start_button_->setText(tr("Продолжить"));
	pause_button_->setText(tr("Продолжить"));
	appendDiagnostic(tr("Воспроизведение приостановлено."));
	refreshDiagnosticsSummary();
}

void NeuroControlWidget::resumePlayback()
{
	if(!paused_) return;
	if(lsl_recording_playback_)
	{
		paused_ = false;
		playback_start_sample_ = next_sample_;
		lsl_recording_playback_start_sample_ = static_cast<std::uint64_t>(next_sample_);
		elapsed_.restart();
		timer_->start();
		start_button_->setEnabled(false);
		start_button_->setText(tr("Старт"));
		pause_button_->setEnabled(true);
		pause_button_->setText(tr("Пауза"));
		lsl_recording_status_label_->setText(tr("Воспроизводится запись %1 · архив")
			.arg(lsl_loaded_recording_id_));
		appendDiagnostic(tr("Архивное воспроизведение LSL продолжено."));
		return;
	}
	neuro_stream_generation_ = client_->resetNeuroControlStream();
	appendExperimentEvent(QJsonObject{{QStringLiteral("event_type"), QStringLiteral("playback_resumed")},
		{QStringLiteral("playback_sample"), next_sample_}, {QStringLiteral("stream_generation"), static_cast<qint64>(neuro_stream_generation_)}});
	paused_ = false;
	playback_start_sample_ = next_sample_;
	elapsed_.restart();
	playback_epoch_monotonic_seconds_ = Clock::getTimeSinceInit();
	timer_->start();
	start_button_->setText(tr("Старт"));
	pause_button_->setText(tr("Пауза"));
	appendDiagnostic(tr("Воспроизведение продолжено."));
	rate_measured_ = false;
	rate_sample_snapshot_ = received_samples_;
	last_rate_update_ms_ = QDateTime::currentMSecsSinceEpoch();
	refreshDiagnosticsSummary();
}

void NeuroControlWidget::seekPlayback(int sample)
{
	if(source_->currentIndex() == 4 && !lsl_recording_source_timestamps_.empty())
	{
		const std::uint64_t total = static_cast<std::uint64_t>(lsl_recording_source_timestamps_.size());
		const int last = static_cast<int>(std::min<std::uint64_t>(total - 1, static_cast<std::uint64_t>(std::numeric_limits<int>::max())));
		next_sample_ = std::clamp(sample, 0, last);
		playback_start_sample_ = next_sample_;
		lsl_recording_playback_start_sample_ = static_cast<std::uint64_t>(next_sample_);
		waveform_->clear();
		overview_waveform_->clear();
		if(playing_ && !paused_) elapsed_.restart();
		const double rate = lsl_recording_playback_rate_hz_ > 0.0 ? lsl_recording_playback_rate_hz_ : static_cast<double>(std::max(1, sample_rate_));
		const double duration = static_cast<double>(total) / rate;
		const int seconds = static_cast<int>(next_sample_ / rate);
		const int total_seconds = static_cast<int>(std::ceil(duration));
		playback_time_label_->setText(QStringLiteral("%1:%2 / %3:%4")
			.arg(seconds / 60, 2, 10, QLatin1Char('0')).arg(seconds % 60, 2, 10, QLatin1Char('0'))
			.arg(total_seconds / 60, 2, 10, QLatin1Char('0')).arg(total_seconds % 60, 2, 10, QLatin1Char('0')));
		updating_seek_slider_ = true;
		seek_slider_->setValue(next_sample_);
		updating_seek_slider_ = false;
		return;
	}
	if(source_->currentIndex() != 0 || channel_count_ <= 0 || recorded_samples_.empty()) return;
	appendExperimentEvent(QJsonObject{{QStringLiteral("event_type"), QStringLiteral("playback_seek")},
		{QStringLiteral("from_sample"), next_sample_}, {QStringLiteral("requested_sample"), sample},
		{QStringLiteral("stream_generation_before_reset"), static_cast<qint64>(neuro_stream_generation_)}});
	neuro_stream_generation_ = client_->resetNeuroControlStream();
	if(control_enabled_->isChecked()) publishStop();
	const int total = static_cast<int>(recorded_samples_.size() / static_cast<std::size_t>(channel_count_));
	next_sample_ = std::clamp(sample, 0, std::max(0, total - 1));
	playback_start_sample_ = next_sample_;
	if(classifier_) classifier_->reset();
	waveform_->clear();
	overview_waveform_->clear();
	if(playing_ && !paused_)
	{
		elapsed_.restart();
		playback_epoch_monotonic_seconds_ = Clock::getTimeSinceInit();
	}
	const int seconds = sample_rate_ > 0 ? next_sample_ / sample_rate_ : 0;
	const int total_seconds = sample_rate_ > 0 ? total / sample_rate_ : 0;
	playback_time_label_->setText(QStringLiteral("%1:%2 / %3:%4")
		.arg(seconds / 60, 2, 10, QLatin1Char('0')).arg(seconds % 60, 2, 10, QLatin1Char('0'))
		.arg(total_seconds / 60, 2, 10, QLatin1Char('0')).arg(total_seconds % 60, 2, 10, QLatin1Char('0')));
}

void NeuroControlWidget::loadModel()
{
	const QString path = QFileDialog::getOpenFileName(this, tr("Модель нейроуправления"), QString(), tr("Файлы модели JSON (*.json)"));
	if(path.isEmpty()) return;
	loadModelPath(path);
}

void NeuroControlWidget::saveModelCopy()
{
	if(loaded_model_path_.isEmpty() || !QFileInfo::exists(loaded_model_path_))
	{
		appendDiagnostic(tr("Сначала загрузите или обучите модель."));
		return;
	}
	const QString destination = QFileDialog::getSaveFileName(this, tr("Сохранить копию модели NeuroControl"),
		QFileInfo(loaded_model_path_).fileName(), tr("Модель NeuroControl (*.json)"));
	if(destination.isEmpty()) return;
	QFile source(loaded_model_path_);
	if(!source.open(QIODevice::ReadOnly)) { appendDiagnostic(tr("Не удалось прочитать текущую модель.")); return; }
	const QByteArray contents = source.readAll();
	QSaveFile copy(destination);
	if(!copy.open(QIODevice::WriteOnly) || copy.write(contents) != contents.size() || !copy.commit())
	{
		appendDiagnostic(tr("Не удалось сохранить копию модели."));
		return;
	}
	appendDiagnostic(tr("Копия модели сохранена: %1.").arg(QFileInfo(destination).fileName()));
}

void NeuroControlWidget::loadModelPath(const QString& path)
{
	if(path.isEmpty()) return;
	stop();
	QFile file(path);
	if(!file.open(QIODevice::ReadOnly)) { appendDiagnostic(tr("Не удалось открыть модель.")); return; }
	const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
	try
	{
		auto model = parseModel(document.object());
		if(!model.valid()) throw std::runtime_error("Некорректный формат модели");
		model_threshold_ = model.forward_threshold;
		applied_threshold_ = std::max(0.75, model.forward_threshold);
		model.forward_threshold = applied_threshold_;
		auto classifier = std::make_unique<neurocontrol::NeuroClassifier>(model);
		auto lsl_shadow_model = std::make_shared<neurocontrol::Model>(model);
		if(lsl_shadow_checkbox_ && lsl_shadow_checkbox_->isChecked()) lsl_shadow_checkbox_->setChecked(false);
		if(lsl_shadow_contract_confirm_checkbox_ && lsl_shadow_contract_confirm_checkbox_->isChecked())
		{
			QSignalBlocker blocker(lsl_shadow_contract_confirm_checkbox_);
			lsl_shadow_contract_confirm_checkbox_->setChecked(false);
		}
		model_channels_ = model.channels;
		model_positive_command_ = model.positive_command;
		model_negative_command_ = model.negative_command;
		model_turn_mapping_experimental_ = model.experimental_turn_mapping;
		model_positive_label_ = document.object().value("positive_class_ru").toString(document.object().value("positive_class").toString());
		model_negative_label_ = document.object().value("negative_class_ru").toString(document.object().value("negative_class").toString());
		if(model_positive_label_ == QStringLiteral("both-feet imagery (FORWARD proxy)")) model_positive_label_ = tr("Воображение движения обеих стоп (T2)");
		if(model_positive_label_ == QStringLiteral("right-fist imagery (assigned TURN_RIGHT)")) model_positive_label_ = tr("Воображение движения правого кулака (T2)");
		if(model_negative_label_ == QStringLiteral("rest")) model_negative_label_ = tr("Покой (T0)");
		if(model_negative_label_ == QStringLiteral("left-fist imagery (assigned TURN_LEFT)")) model_negative_label_ = tr("Воображение движения левого кулака (T1)");
		if(model_positive_label_.isEmpty()) model_positive_label_ = model.positive_command == neurocontrol::CommandType::Forward ? tr("T2 · обе стопы") : tr("положительный класс");
		if(model_negative_label_.isEmpty()) model_negative_label_ = model.negative_command == neurocontrol::CommandType::Stop ? tr("T0 · покой") : tr("отрицательный класс");
		classifier_ = std::move(classifier);
		lsl_shadow_model_ = std::move(lsl_shadow_model);
		lsl_shadow_pipeline_.reset();
		resetLslShadowPipeline(QString(), true);
		model_metadata_ = document.object();
		loaded_model_path_ = QFileInfo(path).absoluteFilePath();
		refreshLslShadowCompatibility();
		model_label_->setText(QFileInfo(path).fileName());
		refreshControlAvailability();
		refreshOverviewStatus();
		const QString mapping = model.positive_command == neurocontrol::CommandType::Forward ? tr("T2/обе стопы → FORWARD proxy") :
			model.positive_command == neurocontrol::CommandType::TurnRight ? tr("T2/правая кисть → условный TURN_RIGHT; T1/левая → TURN_LEFT") : tr("классы модели сопоставлены командам аватара");
		appendDiagnostic(tr("Модель CSP + LDA загружена: %1.").arg(mapping));
		confidence_->setFormat(tr("p(%1): %p%").arg(model_positive_label_));
		positive_class_probability_->setFormat(tr("p(%1): %p%").arg(model_positive_label_));
		negative_class_probability_->setFormat(tr("p(%1): %p%").arg(model_negative_label_));
		QDir model_root(QFileInfo(path).absolutePath());
		model_root.cdUp();
		QString metrics_path = model_root.filePath(QStringLiteral("datasets/processed/%1_metrics.json").arg(QFileInfo(path).completeBaseName()));
		if(!QFileInfo::exists(metrics_path) && QFileInfo(path).completeBaseName() == QStringLiteral("S001_feet_vs_rest"))
			metrics_path = model_root.filePath(QStringLiteral("datasets/processed/S001_metrics.json"));
		refreshTrainingReport(metrics_path);
	}
	catch(const std::exception&) { appendDiagnostic(tr("Не удалось загрузить модель. Проверьте её формат и данные.")); }
}

void NeuroControlWidget::refreshTrainingReport(const QString& path)
{
	QFile file(path);
	if(!file.open(QIODevice::ReadOnly))
	{
		training_result_->setHtml(tr("<p>Отчёт качества ещё не создан. Проверьте, что инструмент обучения завершился успешно.</p>"));
		return;
	}
	const QJsonObject metrics = QJsonDocument::fromJson(file.readAll()).object();
	if(metrics.isEmpty())
	{
		training_result_->setHtml(tr("<p>Файл отчёта пуст или имеет неверный формат.</p>"));
		return;
	}
	const QJsonArray rows = metrics.value("confusion_matrix_rows_actual_negative_positive").toArray();
	const QJsonArray training_runs = metrics.value("training_runs").toArray();
	const QJsonObject class_names = metrics.value("classes_ru").toObject().isEmpty() ? metrics.value("classes").toObject() : metrics.value("classes_ru").toObject();
	QString matrix = tr("нет данных");
	QString runs = tr("нет данных");
	QString task_name = metrics.value("task").toString();
	if(task_name == QStringLiteral("feet")) task_name = tr("Воображение движения обеих стоп против покоя");
	else if(task_name == QStringLiteral("hands")) task_name = tr("Воображение движения левой и правой кисти");
	const auto localized_class = [](QString name) {
		if(name == QStringLiteral("rest")) return tr("Покой (T0)");
		if(name == QStringLiteral("both-feet imagery (FORWARD proxy)")) return tr("Воображение движения обеих стоп (T2)");
		if(name == QStringLiteral("left-fist imagery (assigned TURN_LEFT)")) return tr("Воображение движения левого кулака (T1)");
		if(name == QStringLiteral("right-fist imagery (assigned TURN_RIGHT)")) return tr("Воображение движения правого кулака (T2)");
		return name;
	};
	if(training_runs.size() >= 2) runs = QStringLiteral("R%1, R%2").arg(training_runs[0].toInt(), 2, 10, QLatin1Char('0')).arg(training_runs[1].toInt(), 2, 10, QLatin1Char('0'));
	if(rows.size() == 2 && rows[0].toArray().size() == 2 && rows[1].toArray().size() == 2)
		matrix = QStringLiteral("%1 %2<br>%3 %4").arg(rows[0].toArray()[0].toInt()).arg(rows[0].toArray()[1].toInt()).arg(rows[1].toArray()[0].toInt()).arg(rows[1].toArray()[1].toInt());
	const QString html = QStringLiteral("<h3>Отчёт проверки на отдельной серии</h3><table><tr><td>Задача</td><td>%1</td></tr><tr><td>Класс покоя / движения</td><td>%2 / %3</td></tr><tr><td>Серии обучения / проверки</td><td>%4 / R%5</td></tr><tr><td>Обучающих эпох</td><td>%6</td></tr><tr><td>Проверочных эпох</td><td>%7</td></tr><tr><td>Сбалансированная точность (Balanced Accuracy)</td><td>%8%</td></tr><tr><td>Точность положительного класса (Precision)</td><td>%9%</td></tr><tr><td>Полнота (Recall)</td><td>%10%</td></tr><tr><td>F1 положительного класса</td><td>%11%</td></tr><tr><td>Матрица ошибок (строки: класс покоя / класс движения)</td><td>%12</td></tr><tr><td>Ложные команды движения на контрольном классе</td><td>%13 из %14</td></tr><tr><td>Время обучения</td><td>%15 с</td></tr></table><p><b>Ограничение данных:</b> %16</p>")
		.arg(task_name.toHtmlEscaped())
		.arg(localized_class(class_names.value("0").toString()).toHtmlEscaped())
		.arg(localized_class(class_names.value("1").toString()).toHtmlEscaped())
		.arg(runs).arg(metrics.value("held_out_run").toInt(), 2, 10, QLatin1Char('0'))
		.arg(metrics.value("train_windows").toInt()).arg(metrics.value("test_windows").toInt())
		.arg(metrics.value("balanced_accuracy").toDouble() * 100.0, 0, 'f', 1)
		.arg(metrics.value("precision_positive").toDouble() * 100.0, 0, 'f', 1)
		.arg(metrics.value("recall_positive").toDouble() * 100.0, 0, 'f', 1)
		.arg(metrics.value("f1_positive").toDouble() * 100.0, 0, 'f', 1)
		.arg(matrix).arg(metrics.value("false_motion_on_challenge").toInt()).arg(metrics.value("challenge_windows").toInt())
		.arg(metrics.value("training_seconds").toDouble(), 0, 'f', 2)
		.arg(metrics.value("label_warning_ru").toString(metrics.value("label_warning").toString()).toHtmlEscaped());
	training_result_->setHtml(html + tr("<p>Ложные команды FORWARD на покое: %1. Ложные команды на контрольном классе T1: %2. Дата обучения: %3 UTC.</p>")
		.arg(metrics.value("false_motion_on_negative").toInt())
		.arg(metrics.value("false_motion_on_challenge").toInt())
		.arg(metrics.value("trained_at_utc").toString(tr("не указана")).toHtmlEscaped()));
}

void NeuroControlWidget::runPythonScript(const QString& script, const QStringList& arguments, bool training)
{
	if(training_process_->state() != QProcess::NotRunning)
	{
		appendDiagnostic(tr("Другой инструмент NeuroControl ещё выполняется."));
		return;
	}
	if(script.isEmpty() || !QFileInfo::exists(script))
	{
		training_status_->setText(tr("Скрипт не найден. Укажите файл из папки neurocontrol/tools."));
		appendDiagnostic(tr("Не найден выбранный скрипт NeuroControl."));
		return;
	}
	if(python_path_.isEmpty() || !QFileInfo::exists(python_path_))
	{
		const QString executable = QFileDialog::getOpenFileName(this, tr("Выберите Python с зависимостями NeuroControl"), QString(), tr("Приложения Python (python.exe);;Все файлы (*)"));
		if(executable.isEmpty()) return;
		python_path_ = executable;
		QSettings().setValue(QStringLiteral("neurocontrol/pythonPath"), python_path_);
	}
	QStringList process_arguments;
	process_arguments.push_back(script);
	process_arguments.append(arguments);
	active_job_id_ = QStringLiteral("neuro-job-%1").arg(++next_job_id_);
	active_job_kind_ = training ? QStringLiteral("training") : active_job_is_evaluation_ ? QStringLiteral("model_evaluation") : active_job_is_preparing_ ? QStringLiteral("recording_preparation") : active_job_is_audit_ ? QStringLiteral("dataset_audit") : QStringLiteral("dataset_download");
	last_job_finished_ = false;
	last_job_success_ = false;
	last_job_exit_code_ = -1;
	active_job_is_training_ = training;
	training_status_->setText(training ? tr("Обучение выполняется в отдельном процессе…") : active_job_is_evaluation_ ? tr("Оценка выбранной модели выполняется в отдельном процессе…") : active_job_is_preparing_ ? tr("Подготовка EDF выполняется в отдельном процессе…") : active_job_is_audit_ ? tr("Проверка EDF через MNE выполняется…") : tr("Загрузка записей выполняется в отдельном процессе…"));
	training_progress_->setRange(0, 0);
	training_progress_->setFormat(training ? tr("Обучение / проверка") : active_job_is_evaluation_ ? tr("Оценка модели") : active_job_is_preparing_ ? tr("Подготовка EDF") : active_job_is_audit_ ? tr("Проверка EDF") : tr("Загрузка записей"));
	train_button_->setEnabled(false);
	download_button_->setEnabled(false);
	audit_button_->setEnabled(false);
	training_process_->setWorkingDirectory(QFileInfo(script).absolutePath());
	training_process_->start(python_path_, process_arguments);
	appendDiagnostic(tr("Запущен инструмент %1.").arg(QFileInfo(script).fileName()));
}

void NeuroControlWidget::loadRecording()
{
	const QString path = QFileDialog::getOpenFileName(this, tr("Запись ЭЭГ"), QString(), tr("Записи EDF/BDF (*.edf *.bdf);;Подготовленные метаданные (*.json)"));
	if(path.isEmpty()) return;
	if(QFileInfo(path).suffix().compare("edf", Qt::CaseInsensitive) == 0 || QFileInfo(path).suffix().compare("bdf", Qt::CaseInsensitive) == 0)
	{
		prepareAndLoadRecording(path);
		return;
	}
	loadRecordingPath(path);
}


bool NeuroControlWidget::prepareAndLoadRecording(const QString& path)
{
	if(training_process_->state() != QProcess::NotRunning)
	{
		appendDiagnostic(tr("Дождитесь завершения текущей задачи NeuroControl."));
		return false;
	}
	if(python_path_.isEmpty() || !QFileInfo::exists(python_path_))
	{
		appendDiagnostic(tr("Для подготовки EDF сначала укажите Python с MNE, NumPy, SciPy и scikit-learn."));
		return false;
	}
	const QString neuro_root = findNeurocontrolRoot();
	if(neuro_root.isEmpty()) { appendDiagnostic(tr("Не найден каталог neurocontrol с инструментами обработки.")); return false; }
	const QString output_dir = QDir(neuro_root).filePath(QStringLiteral("datasets/processed/imported"));
	if(!QDir().mkpath(output_dir)) { appendDiagnostic(tr("Не удалось создать папку для подготовленной записи.")); return false; }
	prepared_metadata_path_ = QDir(output_dir).filePath(QStringLiteral("%1_%2.json")
		.arg(QFileInfo(path).completeBaseName(), QString::number(QDateTime::currentMSecsSinceEpoch())));
	active_job_is_preparing_ = true;
	runPythonScript(prepare_recording_script_path_, {QStringLiteral("--recording"), path, QStringLiteral("--metadata"), prepared_metadata_path_}, false);
	if(training_process_->state() == QProcess::NotRunning) active_job_is_preparing_ = false;
	return training_process_->state() != QProcess::NotRunning;
}

void NeuroControlWidget::loadRecordingPath(const QString& path)
{
	stop();
	loaded_recording_metadata_path_.clear();
	recorded_samples_.clear();
	recording_channels_.clear();
	channel_count_ = 0;
	recording_label_->setText(tr("Запись не выбрана"));
	refreshControlAvailability();
	QString metadata_path = path;
	const QString neuro_root = findNeurocontrolRoot();
	const QString processed_root = QDir(neuro_root).filePath(QStringLiteral("datasets/processed"));
	if(neuro_root.isEmpty() || !pathIsWithin(processed_root, metadata_path))
	{
		appendDiagnostic(tr("Метаданные записи находятся вне разрешённого каталога подготовленных данных."));
		return;
	}
	QFile metadata_file(metadata_path);
	if(!metadata_file.open(QIODevice::ReadOnly) || metadata_file.size() <= 0 || metadata_file.size() > 8 * 1024 * 1024)
	{
		appendDiagnostic(tr("Не удалось открыть метаданные записи или их размер недопустим."));
		return;
	}
	const QJsonObject meta = QJsonDocument::fromJson(metadata_file.readAll()).object();
	if(meta.value("format").toString() != "metasiberia-neurocontrol-f32-v1" || meta.value("sample_rate_hz").toInt() <= 0)
	{
		appendDiagnostic(tr("Неверный формат или частота записи.")); return;
	}
	const auto channels = meta.value("channels").toArray();
	const int samples = meta.value("samples").toInt();
	const int channel_count = channels.size();
	const int sample_rate = meta.value("sample_rate_hz").toInt();
	if(samples <= 0 || channel_count <= 0 || channel_count > 256 || sample_rate <= 0 || sample_rate > 4096 || samples > qint64(sample_rate) * 3600)
	{
		appendDiagnostic(tr("Неверные размеры записи.")); return;
	}
	QStringList channel_names;
	for(const QJsonValue& channel : channels)
	{
		const QString name = channel.toString();
		if(name.isEmpty() || channel_names.contains(name))
		{
			appendDiagnostic(tr("Список каналов записи пуст или содержит повторения.")); return;
		}
		channel_names.push_back(name);
	}
	const QString data_name = meta.value("data_file").toString();
	if(data_name.isEmpty() || data_name.contains(QLatin1Char('/')) || data_name.contains(QLatin1Char('\\')) || QFileInfo(data_name).fileName() != data_name)
	{
		appendDiagnostic(tr("Имя файла потока ЭЭГ недопустимо.")); return;
	}
	const QString binary_path = QFileInfo(metadata_path).dir().filePath(data_name);
	if(!pathIsWithin(processed_root, binary_path) || !pathIsWithin(QFileInfo(metadata_path).absolutePath(), binary_path))
	{
		appendDiagnostic(tr("Поток ЭЭГ находится вне разрешённого каталога подготовленной записи.")); return;
	}
	QFile binary(binary_path);
	const qint64 expected_bytes = qint64(samples) * channel_count * 4;
	if(expected_bytes <= 0 || expected_bytes > qint64(1024) * 1024 * 1024 ||
	   !binary.open(QIODevice::ReadOnly) || binary.size() != expected_bytes)
	{
		appendDiagnostic(tr("Подготовленный поток ЭЭГ отсутствует или повреждён.")); return;
	}
	const QByteArray bytes = binary.readAll();
	const QByteArray digest = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
	if(QString::fromLatin1(digest) != meta.value("data_sha256").toString())
	{
		appendDiagnostic(tr("Контрольная сумма потока ЭЭГ не совпадает с метаданными.")); return;
	}
	recorded_samples_.resize(static_cast<std::size_t>(samples) * channel_count);
	std::memcpy(recorded_samples_.data(), bytes.constData(), static_cast<std::size_t>(bytes.size()));
	if(std::any_of(recorded_samples_.begin(), recorded_samples_.end(), [](float value) { return !std::isfinite(value); }))
	{
		recorded_samples_.clear();
		appendDiagnostic(tr("Подготовленный поток содержит NaN или бесконечные отсчёты."));
		refreshControlAvailability();
		return;
	}
	recording_channels_.clear();
	for(const QJsonValue& channel : channels) recording_channels_.push_back(channel.toString().toStdString());
	sample_rate_ = sample_rate;
	channel_count_ = channel_count;
	recording_label_->setText(meta.value("source_file").toString() + tr(" · %1 Гц · ").arg(sample_rate_) + QString::number(samples) + tr(" отсчётов"));
	if(source_->currentIndex() != 0) source_->setCurrentIndex(0);
	updateChannels(recording_channels_);
	updateRecordingMetadata(meta);
	loaded_recording_metadata_path_ = QFileInfo(metadata_path).absoluteFilePath();
	source_status_label_->setText(tr("Источник: подготовленная запись ЭЭГ · %1 каналов · %2 Гц · %3 мин")
		.arg(channel_count_).arg(sample_rate_).arg(samples / std::max(1, sample_rate_) / 60));
	appendDiagnostic(tr("Подготовленная запись ЭЭГ и исходные метаданные загружены; воспроизведение не запущено."));
	refreshControlAvailability();
}

void NeuroControlWidget::start()
{
	if(source_->currentIndex() == 4) { startLslRecordingPlayback(); return; }
	if(playing_) stop();
	if(source_->currentIndex() == 2) { appendDiagnostic(tr("Устройство ЭЭГ не подключено. Режим предусмотрен для будущей поддержки оборудования.")); return; }
	if(source_->currentIndex() == 3) { appendDiagnostic(tr("Для потока LSL используйте кнопку «Подключить». Теневая классификация включается отдельно только после проверки совместимости; управление аватаром для LSL отключено.")); return; }
	if(source_->currentIndex() == 1)
	{
		if(synthetic_reader_) { appendDiagnostic(tr("BrainFlow Synthetic Board уже запущен.")); return; }
		synthetic_reader_ = std::make_unique<NeuroControlSyntheticReader>();
		NeuroControlSyntheticReader* const reader = synthetic_reader_.get();
		reader->thread = std::thread([reader]() {
			try
			{
				neurocontrol::BrainflowSyntheticSource source;
				source.start();
				{
					std::lock_guard<std::mutex> lock(reader->mutex);
					reader->sample_rate = source.sampleRateHz();
					reader->channels = source.channels();
				}
				while(!reader->stop.load())
				{
					const std::vector<float> samples = source.readSamples();
					const std::size_t channel_count = source.channels().size();
					if(channel_count > 0 && !samples.empty())
					{
						std::lock_guard<std::mutex> lock(reader->mutex);
						for(std::size_t offset = 0; offset + channel_count <= samples.size(); offset += channel_count)
						{
							if(reader->samples.size() >= 10000)
							{
								reader->samples.pop_front();
								++reader->dropped_samples;
							}
							reader->samples.emplace_back(samples.begin() + static_cast<std::ptrdiff_t>(offset), samples.begin() + static_cast<std::ptrdiff_t>(offset + channel_count));
						}
					}
					std::this_thread::sleep_for(std::chrono::milliseconds(10));
				}
				source.stop();
			}
			catch(const std::exception& e)
			{
				std::lock_guard<std::mutex> lock(reader->mutex);
				reader->error = e.what();
			}
		});
		appendDiagnostic(tr("BrainFlow Synthetic Board запущен; графики получают многоканальные отсчёты напрямую от BrainFlow. Распознавание и управление аватаром для синтетики отключены."));
	}
	else
	{
		if(recorded_samples_.empty()) { appendDiagnostic(tr("Сначала загрузите подготовленную запись EDF.")); return; }
		if(classifier_ && recording_channels_ == model_channels_ && sample_rate_ == model_metadata_.value("sample_rate_hz").toInt()) classifier_->reset();
		else appendDiagnostic(tr("Воспроизведение EDF без совместимой модели: графики доступны, классификация и управление отключены."));
	}
	neuro_stream_generation_ = client_->resetNeuroControlStream();
	received_samples_ = 0;
	dropped_samples_ = 0;
	rate_sample_snapshot_ = 0;
	receive_rate_hz_ = 0.0;
	rate_measured_ = false;
	last_rate_update_ms_ = QDateTime::currentMSecsSinceEpoch();
	last_classification_time_.clear();
	last_inference_ms_ = 0.0;
	next_sample_ = 0;
	playback_start_sample_ = next_sample_;
	playing_ = true;
	paused_ = false;
	elapsed_.start();
	playback_epoch_monotonic_seconds_ = Clock::getTimeSinceInit();
	start_button_->setText(tr("Старт"));
	start_button_->setEnabled(false);
	pause_button_->setEnabled(source_->currentIndex() == 0);
	pause_button_->setText(tr("Пауза"));
	if(source_->currentIndex() == 1)
	{
		std::vector<std::string> names;
		int rate = 0;
		{
			std::lock_guard<std::mutex> lock(synthetic_reader_->mutex);
			names = synthetic_reader_->channels;
			rate = synthetic_reader_->sample_rate;
		}
		if(rate > 0) sample_rate_ = rate;
		if(!names.empty()) updateChannels(names);
		source_status_label_->setText(tr("Источник: BrainFlow Synthetic Board · получение данных"));
	}
	startExperimentSession();
	timer_->start();
}

void NeuroControlWidget::stop()
{
	if(lsl_recording_playback_) stopLslRecordingPlayback();
	recordControllerTelemetry();
	disconnectLsl();
	++test_generation_;
	neuro_stream_generation_ = client_->resetNeuroControlStream();
	playing_ = false;
	paused_ = false;
	if(timer_) timer_->stop();
	if(synthetic_reader_)
	{
		synthetic_reader_->stop.store(true);
		if(synthetic_reader_->thread.joinable()) synthetic_reader_->thread.join();
		if(synthetic_reader_->dropped_samples > 0)
			appendDiagnostic(tr("Буфер Synthetic Board отбросил %1 старых отсчётов.").arg(synthetic_reader_->dropped_samples));
		synthetic_reader_.reset();
	}
	if(control_enabled_ && control_enabled_->isChecked()) control_enabled_->setChecked(false);
	client_->setNeuroControlEnabled(false);
	if(state_label_) publishStop();
	if(experiment_logger_ && experiment_logger_->isActive())
	{
		appendExperimentEvent(QJsonObject{{QStringLiteral("event_type"), QStringLiteral("playback_stopped")},
			{QStringLiteral("playback_sample"), next_sample_}, {QStringLiteral("stream_generation"), static_cast<qint64>(neuro_stream_generation_)}});
		experiment_logger_->finishSession(QStringLiteral("playback_stopped"), Clock::getTimeSinceInit());
	}
	if(start_button_) { start_button_->setEnabled(true); start_button_->setText(tr("Старт")); }
	if(pause_button_) { pause_button_->setEnabled(false); pause_button_->setText(tr("Пауза")); }
	if(source_status_label_ && source_->currentIndex() == 1) source_status_label_->setText(tr("Источник: BrainFlow Synthetic Board · остановлен"));
	refreshDiagnosticsSummary();
	refreshControlAvailability();
}

void NeuroControlWidget::tick()
{
	if(lsl_recording_playback_)
	{
		if(!playing_ || paused_) return;
		if(channel_count_ <= 0 || lsl_recording_playback_rate_hz_ <= 0.0 || lsl_recording_source_timestamps_.empty())
		{
			appendDiagnostic(tr("Архивное воспроизведение LSL остановлено: неверные размеры записи или частота."));
			stopLslRecordingPlayback();
			return;
		}
		const std::size_t total = lsl_recording_source_timestamps_.size();
		const std::size_t current = static_cast<std::size_t>(std::max(0, next_sample_));
		const double elapsed_samples = std::floor(static_cast<double>(elapsed_.elapsed()) * lsl_recording_playback_rate_hz_ / 1000.0);
		const std::size_t expected = std::min(total, static_cast<std::size_t>(playback_start_sample_) +
			static_cast<std::size_t>(std::max(0.0, elapsed_samples)));
		const std::size_t end = std::min(expected, current + 2048u);
		std::vector<double> microvolts(static_cast<std::size_t>(channel_count_));
		for(std::size_t sample = current; sample < end; ++sample)
		{
			const std::size_t row_offset = sample * static_cast<std::size_t>(channel_count_);
			if(row_offset + static_cast<std::size_t>(channel_count_) > lsl_recording_playback_samples_.size())
			{
				appendDiagnostic(tr("Архивное воспроизведение LSL остановлено: повреждён размер массива данных."));
				stopLslRecordingPlayback();
				return;
			}
			for(int channel = 0; channel < channel_count_; ++channel)
			{
				const double factor = static_cast<std::size_t>(channel) < lsl_recording_unit_to_volts_.size() ? lsl_recording_unit_to_volts_[static_cast<std::size_t>(channel)] : 0.0;
				microvolts[static_cast<std::size_t>(channel)] = static_cast<double>(lsl_recording_playback_samples_[row_offset + static_cast<std::size_t>(channel)]) * factor * 1e6;
			}
			const double display_time = static_cast<double>(sample) / lsl_recording_playback_rate_hz_;
			waveform_->appendFrame(microvolts.data(), channel_count_, display_time);
			overview_waveform_->appendFrame(microvolts.data(), channel_count_, display_time);
		}
		next_sample_ = static_cast<int>(end);
		if(end > current)
		{
			received_samples_ += end - current;
			waveform_->refresh();
			overview_waveform_->refresh();
			refreshSpectrum();
			const int seconds = static_cast<int>(static_cast<double>(next_sample_) / lsl_recording_playback_rate_hz_);
			const int total_seconds = static_cast<int>(std::ceil(static_cast<double>(total) / lsl_recording_playback_rate_hz_));
			playback_time_label_->setText(QStringLiteral("%1:%2 / %3:%4")
				.arg(seconds / 60, 2, 10, QLatin1Char('0')).arg(seconds % 60, 2, 10, QLatin1Char('0'))
				.arg(total_seconds / 60, 2, 10, QLatin1Char('0')).arg(total_seconds % 60, 2, 10, QLatin1Char('0')));
			updating_seek_slider_ = true;
			seek_slider_->setValue(next_sample_);
			updating_seek_slider_ = false;
		}
		if(next_sample_ >= static_cast<int>(total)) stopLslRecordingPlayback();
		return;
	}
	recordControllerTelemetry();
	if(source_->currentIndex() == 1 && synthetic_reader_)
	{
		std::deque<std::vector<float>> pending;
		std::vector<std::string> names;
		int rate = 0;
		std::string error;
		std::uint64_t dropped = 0;
		{
			std::lock_guard<std::mutex> lock(synthetic_reader_->mutex);
			names = synthetic_reader_->channels;
			rate = synthetic_reader_->sample_rate;
			error.swap(synthetic_reader_->error);
			dropped = synthetic_reader_->dropped_samples;
			synthetic_reader_->dropped_samples = 0;
			pending.swap(synthetic_reader_->samples);
		}
		if(!error.empty()) { appendDiagnostic(tr("Поток BrainFlow прерван: %1").arg(QString::fromStdString(error))); stop(); return; }
		if(!names.empty() && active_channels_ != names) updateChannels(names);
		if(rate > 0 && sample_rate_ != rate)
		{
			sample_rate_ = rate;
			waveform_->setChannels(names, rate);
			overview_waveform_->setChannels(names, rate);
		}
		if(dropped > 0) appendDiagnostic(tr("Переполнение буфера: пропущено %1 старых отсчётов.").arg(dropped));
		received_samples_ += pending.size();
		dropped_samples_ += dropped;
		for(const auto& row : pending)
		{
			std::vector<double> values;
			values.reserve(row.size());
			for(float value : row) values.push_back(value); // BrainFlow reports microvolts.
			const double timestamp = static_cast<double>(next_sample_++) / std::max(1, rate);
			waveform_->appendFrame(values.data(), static_cast<int>(values.size()), timestamp);
			overview_waveform_->appendFrame(values.data(), static_cast<int>(values.size()), timestamp);
		}
		if(!pending.empty()) { waveform_->refresh(); overview_waveform_->refresh(); refreshSpectrum(); }
		refreshDiagnosticsSummary();
		return;
	}
	if(!playing_ || paused_) return;
	if(source_->currentIndex() == 0)
	{
		const int total = static_cast<int>(recorded_samples_.size() / static_cast<std::size_t>(channel_count_));
		const double speed = playback_speed_->currentData().toDouble();
		const int expected = std::min(total, playback_start_sample_ + static_cast<int>(elapsed_.elapsed() * sample_rate_ * speed / 1000.0));
		if(expected - next_sample_ > sample_rate_ * 4)
		{
			appendDiagnostic(tr("Воспроизведение отстало более чем на 4 секунды; управление остановлено."));
			stop(); return;
		}
		std::vector<double> values(static_cast<std::size_t>(channel_count_));
		for(; next_sample_ < expected; ++next_sample_)
		{
			++received_samples_;
			const float* row = recorded_samples_.data() + static_cast<std::size_t>(next_sample_) * channel_count_;
			for(int channel = 0; channel < channel_count_; ++channel) values[static_cast<std::size_t>(channel)] = row[channel] * 1e6;
			const double time = static_cast<double>(next_sample_) / sample_rate_;
			waveform_->appendFrame(values.data(), channel_count_, time);
			overview_waveform_->appendFrame(values.data(), channel_count_, time);
			if(!classifier_ || recording_channels_ != model_channels_ || sample_rate_ != model_metadata_.value("sample_rate_hz").toInt()) continue;
			const auto inference_started = std::chrono::steady_clock::now();
			const auto result = classifier_->pushSample(row, static_cast<std::size_t>(channel_count_));
			const std::uint64_t eeg_sample_index = static_cast<std::uint64_t>(next_sample_) + 1;
			const double eeg_sample_timestamp = std::min(Clock::getTimeSinceInit(), playback_epoch_monotonic_seconds_ +
				static_cast<double>(eeg_sample_index - static_cast<std::uint64_t>(std::max(0, playback_start_sample_))) /
				(std::max(1, sample_rate_) * std::max(0.01, speed)));
			client_->setNeuroControlEEGHeartbeat(neuro_stream_generation_, eeg_sample_index, eeg_sample_timestamp, result.sample_valid);
			if(!result.available) continue;
			last_inference_ms_ = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - inference_started).count();
			last_classification_time_ = QDateTime::currentDateTime().toString("HH:mm:ss.zzz");
			neurocontrol::Command command;
			command.type = result.command;
			command.confidence = command.type == neurocontrol::CommandType::Forward || command.type == neurocontrol::CommandType::TurnRight ?
				result.forward_probability : command.type == neurocontrol::CommandType::TurnLeft ? 1.0 - result.forward_probability : 1.0;
			command.timestamp_seconds = Clock::getTimeSinceInit();
			command.minimum_confidence = applied_threshold_;
			command.event_id = ++next_command_event_id_;
			command.stream_generation = neuro_stream_generation_;
			command.sample_index = eeg_sample_index;
			command.sample_timestamp_seconds = eeg_sample_timestamp;
			const int model_stride = model_metadata_.value(QStringLiteral("step_samples")).toInt(0);
			const int stride_samples = model_stride > 0 ? model_stride : sample_rate_;
			const double playback_speed = std::max(0.01, speed);
			const double expected_classifier_interval = static_cast<double>(stride_samples) /
				(std::max(1, sample_rate_) * playback_speed);
			command.maximum_age_seconds = std::max(neurocontrol::kNeuroDataHeartbeatTimeoutSeconds,
				expected_classifier_interval + std::max(0.25, last_inference_ms_ * 2.0 / 1000.0));
			command.quality = result.signal_good ? neurocontrol::SignalQuality::Good : neurocontrol::SignalQuality::Poor;
			command.source = neurocontrol::CommandSource::RecordedEeg;
			command.experimental_turn_mapping = model_turn_mapping_experimental_;
			const QString action = command.type == neurocontrol::CommandType::Forward ? QStringLiteral("FORWARD") : command.type == neurocontrol::CommandType::TurnLeft ? QStringLiteral("TURN_LEFT") : command.type == neurocontrol::CommandType::TurnRight ? QStringLiteral("TURN_RIGHT") : QStringLiteral("STOP");
			const QJsonValue probability_walk = model_positive_command_ == neurocontrol::CommandType::Forward ? QJsonValue(result.forward_probability) :
				model_negative_command_ == neurocontrol::CommandType::Forward ? QJsonValue(1.0 - result.forward_probability) : QJsonValue(QJsonValue::Null);
			const QJsonValue probability_stop = model_positive_command_ == neurocontrol::CommandType::Stop ? QJsonValue(result.forward_probability) :
				model_negative_command_ == neurocontrol::CommandType::Stop ? QJsonValue(1.0 - result.forward_probability) : QJsonValue(QJsonValue::Null);
			const QString candidate_class = result.class_confident ?
				(result.candidate_command == model_positive_command_ ? model_positive_label_ : model_negative_label_) : tr("между порогами");
			QString classifier_reason;
			if(!result.signal_good) classifier_reason = QStringLiteral("signal_quality_not_good");
			else if(result.awaiting_confirmation) classifier_reason = QStringLiteral("awaiting_second_confident_window");
			else if(!result.class_confident) classifier_reason = QStringLiteral("probability_inside_decision_margin");
			else if(command.type == neurocontrol::CommandType::Stop && result.candidate_command == model_negative_command_) classifier_reason = QStringLiteral("negative_class_maps_to_stop");
			const double candidate_confidence = result.class_confident ?
				(result.candidate_command == model_positive_command_ ? result.forward_probability : 1.0 - result.forward_probability) :
				std::max(result.forward_probability, 1.0 - result.forward_probability);
			last_prediction_ = QJsonObject{
				{"event_id", static_cast<qint64>(command.event_id)},
				{"timestamp_utc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
				{"sample_index", static_cast<qint64>(result.sample_index)},
				{"processed_eeg_sample_index", static_cast<qint64>(command.sample_index)},
				{"classification_input_age_seconds", command.timestamp_seconds - command.sample_timestamp_seconds},
				{"stream_generation", static_cast<qint64>(neuro_stream_generation_)},
				{"classifier_maximum_age_seconds", command.maximum_age_seconds},
				{"class", candidate_class}, {"candidate_command", neuroCommandName(result.candidate_command)}, {"class_confident", result.class_confident},
				{"awaiting_confirmation", result.awaiting_confirmation}, {"consecutive_candidate_windows", result.consecutive_candidate_windows},
				{"class_command", action}, {"class_scores", QJsonObject{{model_positive_label_, result.forward_probability}, {model_negative_label_, 1.0 - result.forward_probability}}},
				{"confidence", candidate_confidence}, {"candidate_confidence", candidate_confidence},
				{"positive_probability", result.forward_probability}, {"model_threshold", model_threshold_}, {"controller_floor", 0.75}, {"effective_threshold", applied_threshold_},
				{"signal_good", result.signal_good}, {"inference_ms", last_inference_ms_}, {"classifier_reason", classifier_reason},
				{"command_sent_to_controller", control_enabled_->isChecked()}
			};
			appendExperimentEvent(QJsonObject{
				{QStringLiteral("event_type"), QStringLiteral("prediction")},
				{QStringLiteral("command_event_id"), static_cast<qint64>(command.event_id)},
				{QStringLiteral("stream_generation"), static_cast<qint64>(neuro_stream_generation_)},
				{QStringLiteral("sample_index"), static_cast<qint64>(command.sample_index)},
				{QStringLiteral("window_sample_index"), static_cast<qint64>(result.sample_index)},
				{QStringLiteral("timestamp_utc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
				{QStringLiteral("monotonic_seconds"), command.timestamp_seconds},
				{QStringLiteral("classification_input_age_seconds"), command.timestamp_seconds - command.sample_timestamp_seconds},
				{QStringLiteral("probability_walk"), probability_walk},
				{QStringLiteral("probability_stop"), probability_stop},
				{QStringLiteral("model_positive_class"), model_positive_label_},
				{QStringLiteral("model_negative_class"), model_negative_label_},
				{QStringLiteral("positive_class_probability"), result.forward_probability},
				{QStringLiteral("negative_class_probability"), 1.0 - result.forward_probability},
				{QStringLiteral("candidate_class"), candidate_class},
				{QStringLiteral("candidate_command"), neuroCommandName(result.candidate_command)},
				{QStringLiteral("classification_result"), action},
				{QStringLiteral("class_confident"), result.class_confident},
				{QStringLiteral("awaiting_confirmation"), result.awaiting_confirmation},
				{QStringLiteral("consecutive_candidate_windows"), result.consecutive_candidate_windows},
				{QStringLiteral("forward_confirmed"), command.type == neurocontrol::CommandType::Forward},
				{QStringLiteral("model_threshold"), model_threshold_},
				{QStringLiteral("controller_safety_floor"), 0.75},
				{QStringLiteral("effective_threshold"), applied_threshold_},
				{QStringLiteral("signal_good"), result.signal_good},
				{QStringLiteral("inference_ms"), last_inference_ms_},
				{QStringLiteral("classifier_reason"), classifier_reason},
				{QStringLiteral("command_sent_to_controller"), control_enabled_->isChecked()}});
			if(control_enabled_->isChecked())
			{
				client_->setNeuroControlCommand(command);
				appendExperimentEvent(QJsonObject{{QStringLiteral("event_type"), QStringLiteral("command_submitted")},
					{QStringLiteral("command"), action},
					{QStringLiteral("command_event_id"), static_cast<qint64>(command.event_id)},
					{QStringLiteral("stream_generation"), static_cast<qint64>(command.stream_generation)},
					{QStringLiteral("sample_index"), static_cast<qint64>(command.sample_index)},
					{QStringLiteral("timestamp_utc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
					{QStringLiteral("monotonic_seconds"), command.timestamp_seconds},
					{QStringLiteral("confidence"), command.confidence},
					{QStringLiteral("submission_result"), client_->neuro_control_command.event_id == command.event_id ? QStringLiteral("submitted") : QStringLiteral("rejected")}});
			}
			QString prediction;
			if(!result.signal_good)
				prediction = tr("СИГНАЛ НЕНАДЁЖЕН · STOP · p(%1) = %2").arg(model_positive_label_).arg(result.forward_probability, 0, 'f', 3);
			else if(result.awaiting_confirmation)
				prediction = tr("Команда: STOP · кандидат: %1 · окно %2/2 · p(%3) = %4 · применяемый порог %5")
					.arg(candidate_class).arg(result.consecutive_candidate_windows).arg(model_positive_label_)
					.arg(result.forward_probability, 0, 'f', 3).arg(applied_threshold_, 0, 'f', 2);
			else
				prediction = tr("Команда: %1 · класс: %2 · p(%3) = %4 · применяемый порог %5")
					.arg(action, candidate_class, model_positive_label_).arg(result.forward_probability, 0, 'f', 3).arg(applied_threshold_, 0, 'f', 2);
			state_label_->setText(tr("Состояние: %1").arg(action));
			const double confidence = command.type == model_negative_command_ ? 1.0 - result.forward_probability : result.forward_probability;
			confidence_->setValue(static_cast<int>(std::round(result.forward_probability * 100.0)));
			avatar_prediction_label_->setText(prediction);
			avatar_confidence_->setValue(static_cast<int>(std::round(confidence * 100.0)));
			positive_class_probability_->setValue(static_cast<int>(std::round(result.forward_probability * 100.0)));
			negative_class_probability_->setValue(static_cast<int>(std::round((1.0 - result.forward_probability) * 100.0)));
			avatar_status_label_->setText(!control_enabled_->isChecked() ? tr("УПРАВЛЕНИЕ ОТКЛЮЧЕНО") : command.type == neurocontrol::CommandType::Forward ? tr("ИДЁТ ВПЕРЁД") : command.type == neurocontrol::CommandType::TurnLeft ? tr("ПОВОРАЧИВАЕТ ВЛЕВО") : command.type == neurocontrol::CommandType::TurnRight ? tr("ПОВОРАЧИВАЕТ ВПРАВО") : tr("СТОИТ"));
			avatar_action_label_->setText(avatar_status_label_->text());
			const QString classifier_state = QStringLiteral("%1|%2|%3|%4")
				.arg(action, classifier_reason, candidate_class).arg(result.consecutive_candidate_windows);
			if(!has_reported_command_ || command.type != last_reported_command_ || classifier_state != last_reported_classifier_state_)
			{
				appendDiagnostic(tr("Оценка ЭЭГ: p(%1) = %2; порог модели %3; применяемый порог %4; кандидат %5; команда %6; окон подтверждения %7/2; состояние классификатора: %8.")
					.arg(model_positive_label_).arg(result.forward_probability, 0, 'f', 3)
					.arg(model_threshold_, 0, 'f', 2).arg(applied_threshold_, 0, 'f', 2).arg(candidate_class).arg(action)
					.arg(result.consecutive_candidate_windows)
					.arg(classifier_reason.isEmpty() ? tr("нет блокировки классификатора") : classifier_reason));
				last_reported_command_ = command.type;
				last_reported_classifier_state_ = classifier_state;
				has_reported_command_ = true;
			}
		}
		if(expected > playback_start_sample_)
		{
			waveform_->refresh();
			overview_waveform_->refresh();
			const int current_seconds = next_sample_ / std::max(1, sample_rate_);
			const int total_seconds = total / std::max(1, sample_rate_);
			playback_time_label_->setText(QStringLiteral("%1:%2 / %3:%4")
				.arg(current_seconds / 60, 2, 10, QLatin1Char('0')).arg(current_seconds % 60, 2, 10, QLatin1Char('0'))
				.arg(total_seconds / 60, 2, 10, QLatin1Char('0')).arg(total_seconds % 60, 2, 10, QLatin1Char('0')));
			updating_seek_slider_ = true;
			seek_slider_->setValue(next_sample_);
			updating_seek_slider_ = false;
			refreshSpectrum();
		}
		if(next_sample_ >= total) { appendDiagnostic(tr("Конец записи. Управление остановлено.")); stop(); }
		refreshDiagnosticsSummary();
		return;
	}
}

void NeuroControlWidget::sendTestTurn(bool left)
{
	neurocontrol::Command command;
	command.type = left ? neurocontrol::CommandType::TurnLeft : neurocontrol::CommandType::TurnRight;
	command.confidence = 1.0;
	command.timestamp_seconds = Clock::getTimeSinceInit();
	command.minimum_confidence = applied_threshold_;
	command.event_id = ++next_command_event_id_;
	command.quality = neurocontrol::SignalQuality::Good;
	command.source = neurocontrol::CommandSource::SimulatedTest;
	command.forward_held = test_forward_->isChecked();
	client_->setNeuroControlEnabled(true);
	client_->setNeuroControlCommand(command);
	appendExperimentEvent(QJsonObject{{QStringLiteral("event_type"), QStringLiteral("command_submitted")},
		{QStringLiteral("command"), left ? QStringLiteral("TURN_LEFT") : QStringLiteral("TURN_RIGHT")},
		{QStringLiteral("command_source"), QStringLiteral("simulated_test")},
		{QStringLiteral("command_event_id"), static_cast<qint64>(command.event_id)},
		{QStringLiteral("submission_result"), client_->neuro_control_command.event_id == command.event_id ? QStringLiteral("submitted") : QStringLiteral("rejected")}});
	appendDiagnostic(left ? tr("Симуляция: поворот влево") : tr("Симуляция: поворот вправо"));
	const int generation = ++test_generation_;
	QTimer::singleShot(400, this, [this, generation] { if(generation == test_generation_) { publishStop(); if(!control_enabled_->isChecked()) client_->setNeuroControlEnabled(false); } });
}
