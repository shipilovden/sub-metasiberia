#include "LslAuditReportWriter.h"

#include <chrono>
#include <cmath>
#include <cctype>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>

namespace neurocontrol {
namespace {

bool validExperimentId(const std::string& id)
{
	return id.size() == 3 && id[0] == 'S' && std::isdigit(static_cast<unsigned char>(id[1])) &&
		std::isdigit(static_cast<unsigned char>(id[2]));
}

bool validRecordingId(const std::string& id)
{
	if(id.empty() || id.size() > 96) return false;
	for(unsigned char c : id)
		if(!std::isalnum(c) && c != '_' && c != '-') return false;
	return true;
}

bool pathIsWithin(const std::filesystem::path& parent, const std::filesystem::path& child,
	std::error_code& error)
{
	const auto relative = std::filesystem::relative(child, parent, error);
	if(error || relative.empty() || relative.is_absolute()) return false;
	for(const auto& component : relative)
		if(component == "..") return false;
	return true;
}

std::string markdownCell(std::string value)
{
	if(value.size() > 512) value.resize(512);
	std::string escaped;
	escaped.reserve(value.size());
	for(char c : value)
	{
		if(c == '\r' || c == '\n') escaped.push_back(' ');
		else
		{
			if(c == '|') escaped.push_back('\\');
			escaped.push_back(c);
		}
	}
	return escaped;
}

std::string number(double value, int precision = 3)
{
	if(!std::isfinite(value)) return "не измерено";
	std::ostringstream out;
	out << std::fixed << std::setprecision(precision) << value;
	return out.str();
}

std::string utcNow()
{
	const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
	std::tm utc{};
#if defined(_WIN32)
	gmtime_s(&utc, &now);
#else
	gmtime_r(&now, &utc);
#endif
	std::ostringstream out;
	out << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
	return out.str();
}

std::string render(const LslAuditReportSnapshot& s)
{
	std::ostringstream out;
	out << "# Эксперимент " << s.experiment_id << " — офлайн-аудит LSL\n\n"
		<< "Дата отчёта UTC: " << utcNow() << "\n\n"
		<< "Источник — локальная сохранённая запись; сырые значения ЭЭГ в отчёт не включаются.\n\n"
		<< "## Результат проверки\n\n"
		<< "- Состояние аудита: `" << markdownCell(s.audit_state) << "`\n"
		<< "- Целостность: `" << markdownCell(s.integrity_status) << "`\n"
		<< "- Причина / подробность: " << (s.integrity_reason.empty() ? "нет" : markdownCell(s.integrity_reason)) << "\n"
		<< "- ID записи: `" << markdownCell(s.recording_id) << "`\n"
		<< "- Проверка приёмной частоты по архиву: " << (s.receive_rate_measurable_from_archive ? "доступна" : "невозможна по сохранённым chunk timestamps") << "\n\n"
		<< "## Ресемплер\n\n"
		<< "- Частота входа: " << (s.input_rate_hz > 0 ? std::to_string(s.input_rate_hz) + " Гц" : "не прочитана") << "\n"
		<< "- Запрошенная частота выхода: " << s.target_rate_hz << " Гц\n"
		<< "- Частота модели: " << (s.model_rate_hz > 0 ? std::to_string(s.model_rate_hz) + " Гц" : "модель не загружена") << "\n"
		<< "- Отсчёты: " << s.input_samples << " → " << s.output_samples << "\n"
		<< "- Каналы: " << s.total_channels << " всего, " << s.eeg_channels << " EEG\n"
		<< "- Рациональный коэффициент: " << s.resample_up << '/' << s.resample_down << "\n"
		<< "- FIR taps / group delay: " << s.filter_taps << " / " << number(s.group_delay_ms) << " мс\n"
		<< "- Номинальная длительность / обработка: " << number(s.nominal_duration_seconds) << " с / " << number(s.processing_ms) << " мс\n"
		<< "- Ошибки / invalid input / non-finite output: " << s.pipeline_errors << " / " << s.invalid_input_values << " / " << s.nonfinite_output_values << "\n"
		<< "- Сбросы по разрыву timestamps / неверному кадру: " << s.timestamp_gap_resets << " / " << s.invalid_frame_resets << "\n\n"
		<< "## Совместимость модели и безопасность\n\n"
		<< "- Состояние совместимости модели: `" << (s.model_compatibility_state.empty() ? "не проверена" : markdownCell(s.model_compatibility_state)) << "`\n"
		<< "- Причины / коды: ";
	if(s.model_compatibility_reasons.empty()) out << "нет дополнительных кодов";
	else
	{
		for(std::size_t i = 0; i < s.model_compatibility_reasons.size(); ++i)
		{
			if(i != 0) out << ", ";
			out << '`' << markdownCell(s.model_compatibility_reasons[i]) << '`';
		}
	}
	out << "\n- Классификатор запускался: " << (s.classifier_ran ? "да" : "нет") << "\n"
		<< "- Команда аватару отправлялась: " << (s.avatar_command_sent ? "да" : "нет") << "\n\n"
		<< "Отчёт содержит только агрегаты и диагностические коды; значения ЭЭГ не сохраняются.\n";
	return out.str();
}

} // namespace

bool writeLslAuditReport(const std::filesystem::path& project_root,
	const LslAuditReportSnapshot& snapshot, std::filesystem::path& output_path, std::string& error)
{
	output_path.clear();
	error.clear();
	if(!validExperimentId(snapshot.experiment_id))
	{
		error = "Experiment ID must have the form S followed by two digits.";
		return false;
	}
	if(!validRecordingId(snapshot.recording_id))
	{
		error = "Recording ID is invalid.";
		return false;
	}
	if(snapshot.target_rate_hz < 8 || snapshot.target_rate_hz > 2000 ||
		(snapshot.input_rate_hz != 0 && (snapshot.input_rate_hz < 8 || snapshot.input_rate_hz > 4000)))
	{
		error = "Report sample rates are outside the supported range.";
		return false;
	}

	std::error_code ec;
	const auto canonical_root = std::filesystem::canonical(project_root, ec);
	if(ec || !std::filesystem::is_directory(canonical_root))
	{
		error = "Project root is unavailable.";
		return false;
	}
	const auto requested_directory = canonical_root / "neurocontrol" / "docs" / "experiments";
	std::filesystem::create_directories(requested_directory, ec);
	if(ec)
	{
		error = "Cannot create the local NeuroControl experiments documentation directory.";
		return false;
	}
	const auto canonical_directory = std::filesystem::canonical(requested_directory, ec);
	if(ec || !pathIsWithin(canonical_root, canonical_directory, ec))
	{
		error = "The local report directory resolves outside the project root.";
		return false;
	}

	const auto destination = canonical_directory / (snapshot.experiment_id + "_lsl_audit.md");
	if(std::filesystem::exists(destination, ec) || ec)
	{
		error = ec ? "Cannot inspect the report destination." : "A report for this experiment ID already exists; it will not be overwritten.";
		return false;
	}
	const auto temp = destination.string() + ".tmp-" +
		std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
	{
		std::ofstream file(temp, std::ios::binary | std::ios::out | std::ios::trunc);
		if(!file)
		{
			error = "Cannot create a temporary report file.";
			return false;
		}
		const std::string contents = render(snapshot);
		file.write(contents.data(), static_cast<std::streamsize>(contents.size()));
		file.flush();
		if(!file)
		{
			file.close();
			std::filesystem::remove(temp, ec);
			error = "Could not write the complete report.";
			return false;
		}
	}
	if(std::filesystem::exists(destination, ec) || ec)
	{
		std::filesystem::remove(temp, ec);
		error = "A report for this experiment ID already exists; it will not be overwritten.";
		return false;
	}
	std::filesystem::rename(temp, destination, ec);
	if(ec)
	{
		std::filesystem::remove(temp, ec);
		error = "Could not atomically publish the local report file.";
		return false;
	}
	output_path = destination;
	return true;
}

} // namespace neurocontrol
