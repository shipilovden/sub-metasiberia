#include "LslEegSource.h"

#include <QtCore/QLibrary>
#include <QtCore/QCryptographicHash>
#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QTextCodec>
#include <QtCore/QXmlStreamReader>
#include <QtCore/QtEndian>
#include <algorithm>
#include <cmath>
#include <limits>

namespace neurocontrol {
namespace {

QString nativeText(const char* text)
{
    if(!text) return {};
    const QByteArray bytes(text);
    const QString utf8 = QString::fromUtf8(bytes);
    return utf8.contains(QChar::ReplacementCharacter) ? QString::fromLocal8Bit(bytes) : utf8;
}

QString xmlText(const QByteArray& bytes)
{
    const QString utf8 = QString::fromUtf8(bytes);
    if(!utf8.contains(QChar::ReplacementCharacter)) return utf8;
    const QTextCodec* codec = QTextCodec::codecForName("Windows-1251");
    return codec ? codec->toUnicode(bytes) : utf8;
}

QString formatName(int value)
{
    switch(value)
    {
    case 1: return QStringLiteral("float32");
    case 2: return QStringLiteral("double64");
    case 3: return QStringLiteral("string");
    case 4: return QStringLiteral("int32");
    case 5: return QStringLiteral("int16");
    case 6: return QStringLiteral("int8");
    case 7: return QStringLiteral("int64");
    default: return QStringLiteral("unknown");
    }
}

double unitFactor(const QString& unit)
{
    const QString value = unit.trimmed().toLower();
    if(value == QStringLiteral("microvolts") || value == QStringLiteral("microvolt") ||
       value == QStringLiteral("uv") || value == QStringLiteral("µv") || value == QStringLiteral("μv") ||
       value == QStringLiteral("мкв")) return 1e-6;
    if(value == QStringLiteral("volts") || value == QStringLiteral("volt") || value == QStringLiteral("v") ||
       value == QStringLiteral("в")) return 1.0;
    if(value == QStringLiteral("millivolts") || value == QStringLiteral("millivolt") ||
       value == QStringLiteral("mv") || value == QStringLiteral("мв")) return 1e-3;
    return 0.0;
}

void parseChannels(LslStreamDescriptor& descriptor)
{
    QXmlStreamReader xml(xmlText(descriptor.raw_xml));
    while(!xml.atEnd())
    {
        xml.readNext();
        if(!xml.isStartElement() || xml.name() != QLatin1String("channel")) continue;
        QString label, type, unit;
        while(xml.readNextStartElement())
        {
            const QString key = xml.name().toString();
            const QString value = xml.readElementText(QXmlStreamReader::SkipChildElements).trimmed();
            if(key == QLatin1String("label")) label = value;
            else if(key == QLatin1String("type")) type = value;
            else if(key == QLatin1String("unit")) unit = value;
        }
        descriptor.channels.push_back(label);
        descriptor.channel_types.push_back(type);
        descriptor.units.push_back(unit);
    }
    if(descriptor.channels.size() != descriptor.channel_count)
    {
        descriptor.channels.clear();
        descriptor.channel_types.clear();
        descriptor.units.clear();
    }
}

struct Api {
    explicit Api(const QString& path) : library(path) {}
    using ResolveAll = int (__cdecl *)(void**, unsigned int, double);
    using DestroyInfo = void (__cdecl *)(void*);
    using GetString = const char* (__cdecl *)(void*);
    using GetInt = int (__cdecl *)(void*);
    using GetDouble = double (__cdecl *)(void*);
    using GetXml = char* (__cdecl *)(void*);
    using DestroyString = void (__cdecl *)(char*);
    using CreateInlet = void* (__cdecl *)(void*, int, int, int);
    using DestroyInlet = void (__cdecl *)(void*);
    using OpenStream = void (__cdecl *)(void*, double, int*);
    using CloseStream = void (__cdecl *)(void*);
    using FullInfo = void* (__cdecl *)(void*, double, int*);
    using PullChunk = unsigned int (__cdecl *)(void*, double*, double*, unsigned int, unsigned int, double, int*);
    using PullChunkFloat = unsigned int (__cdecl *)(void*, float*, double*, unsigned int, unsigned int, double, int*);
    using TimeCorrection = double (__cdecl *)(void*, double, int*);
    using LocalClock = double (__cdecl *)();
    QLibrary library;
    ResolveAll resolve_all = nullptr;
    DestroyInfo destroy_info = nullptr;
    GetString get_name = nullptr, get_type = nullptr, get_source_id = nullptr, get_uid = nullptr, get_hostname = nullptr;
    GetInt get_channel_count = nullptr, get_channel_format = nullptr;
    GetDouble get_nominal_srate = nullptr;
    GetXml get_xml = nullptr;
    DestroyString destroy_string = nullptr;
    CreateInlet create_inlet = nullptr;
    DestroyInlet destroy_inlet = nullptr;
    OpenStream open_stream = nullptr;
    CloseStream close_stream = nullptr;
    FullInfo full_info = nullptr;
    PullChunk pull_chunk = nullptr;
    PullChunkFloat pull_chunk_float = nullptr;
    TimeCorrection time_correction = nullptr;
    LocalClock local_clock = nullptr;

    bool load(QString& error, QStringList* missing_symbols = nullptr)
    {
        if(library.fileName().isEmpty()) { error = QStringLiteral("Путь к проверенной lsl.dll не настроен."); return false; }
        if(!library.load()) { error = library.errorString(); return false; }
#define LSL_SYMBOL(member, symbol) member = reinterpret_cast<decltype(member)>(library.resolve(symbol));
        LSL_SYMBOL(resolve_all, "lsl_resolve_all")
        LSL_SYMBOL(destroy_info, "lsl_destroy_streaminfo")
        LSL_SYMBOL(get_name, "lsl_get_name")
        LSL_SYMBOL(get_type, "lsl_get_type")
        LSL_SYMBOL(get_source_id, "lsl_get_source_id")
        LSL_SYMBOL(get_uid, "lsl_get_uid")
        LSL_SYMBOL(get_hostname, "lsl_get_hostname")
        LSL_SYMBOL(get_channel_count, "lsl_get_channel_count")
        LSL_SYMBOL(get_channel_format, "lsl_get_channel_format")
        LSL_SYMBOL(get_nominal_srate, "lsl_get_nominal_srate")
        LSL_SYMBOL(get_xml, "lsl_get_xml")
        LSL_SYMBOL(destroy_string, "lsl_destroy_string")
        LSL_SYMBOL(create_inlet, "lsl_create_inlet")
        LSL_SYMBOL(destroy_inlet, "lsl_destroy_inlet")
        LSL_SYMBOL(open_stream, "lsl_open_stream")
        LSL_SYMBOL(close_stream, "lsl_close_stream")
        LSL_SYMBOL(full_info, "lsl_get_fullinfo")
        LSL_SYMBOL(pull_chunk, "lsl_pull_chunk_d")
        LSL_SYMBOL(pull_chunk_float, "lsl_pull_chunk_f")
        LSL_SYMBOL(time_correction, "lsl_time_correction")
        LSL_SYMBOL(local_clock, "lsl_local_clock")
#undef LSL_SYMBOL
        const QStringList names{
            QStringLiteral("lsl_resolve_all"), QStringLiteral("lsl_destroy_streaminfo"),
            QStringLiteral("lsl_get_name"), QStringLiteral("lsl_get_type"), QStringLiteral("lsl_get_source_id"),
            QStringLiteral("lsl_get_uid"), QStringLiteral("lsl_get_hostname"), QStringLiteral("lsl_get_channel_count"),
            QStringLiteral("lsl_get_channel_format"), QStringLiteral("lsl_get_nominal_srate"), QStringLiteral("lsl_get_xml"),
            QStringLiteral("lsl_destroy_string"), QStringLiteral("lsl_create_inlet"), QStringLiteral("lsl_destroy_inlet"),
            QStringLiteral("lsl_open_stream"), QStringLiteral("lsl_close_stream"), QStringLiteral("lsl_get_fullinfo"),
            QStringLiteral("lsl_pull_chunk_d"), QStringLiteral("lsl_time_correction"), QStringLiteral("lsl_local_clock")};
        const bool complete = resolve_all && destroy_info && get_name && get_type && get_source_id && get_uid &&
            get_hostname && get_channel_count && get_channel_format && get_nominal_srate && get_xml &&
            destroy_string && create_inlet && destroy_inlet && open_stream && close_stream && full_info &&
            pull_chunk && time_correction && local_clock;
        if(complete) return true;
        if(missing_symbols)
            for(const QString& name : names)
                if(!library.resolve(name.toLatin1().constData())) missing_symbols->append(name);
        error = QStringLiteral("В библиотеке LSL отсутствуют необходимые функции C API: %1.").arg(missing_symbols ? missing_symbols->join(QStringLiteral(", ")) : QStringLiteral("проверьте экспорты"));
        library.unload();
        return false;
    }

    LslStreamDescriptor describe(void* info)
    {
        LslStreamDescriptor d;
        d.name = nativeText(get_name(info));
        d.type = nativeText(get_type(info));
        d.source_id = nativeText(get_source_id(info));
        d.uid = nativeText(get_uid(info));
        d.hostname = nativeText(get_hostname(info));
        d.channel_count = get_channel_count(info);
        d.nominal_hz = get_nominal_srate(info);
        d.format = formatName(get_channel_format(info));
        char* xml = get_xml(info);
        if(xml) { d.raw_xml = QByteArray(xml); destroy_string(xml); parseChannels(d); }
        return d;
    }
};

bool sameStream(const LslStreamDescriptor& wanted, const LslStreamDescriptor& found)
{
    if(!wanted.uid.isEmpty() && wanted.uid == found.uid) return true;
    return !wanted.source_id.isEmpty() && wanted.source_id == found.source_id &&
        wanted.name == found.name && wanted.type == found.type;
}

} // namespace

double LslEegSource::unitScaleToVolts(const QString& unit)
{
    return unitFactor(unit);
}

namespace {

bool hasOfficialLabRecorderArchiveMarker(const QString& directory)
{
    const QFileInfo executable(QDir(directory).filePath(QStringLiteral("LabRecorder.exe")));
    const QFileInfo library(QDir(directory).filePath(QStringLiteral("lsl.dll")));
    const QFileInfo readme(QDir(directory).filePath(QStringLiteral("README.md")));
    if(!executable.isFile() || !library.isFile() || !readme.isFile()) return false;
    QFile file(readme.absoluteFilePath());
    if(!file.open(QIODevice::ReadOnly) || file.size() > 512 * 1024) return false;
    return file.readAll().toLower().contains("github.com/labstreaminglayer/app-labrecorder");
}

void appendLabRecorderCandidates(const QString& directory, QStringList& candidates)
{
    const QDir root(directory);
    if(!root.exists()) return;
    if(hasOfficialLabRecorderArchiveMarker(root.absolutePath())) candidates.append(root.absolutePath());
    const QFileInfoList children = root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for(const QFileInfo& child : children)
    {
        if(!child.fileName().contains(QStringLiteral("labrecorder"), Qt::CaseInsensitive) &&
           !child.fileName().contains(QStringLiteral("lsl_labrecorder"), Qt::CaseInsensitive)) continue;
        if(hasOfficialLabRecorderArchiveMarker(child.absoluteFilePath())) candidates.append(child.absoluteFilePath());
        const QDir nested(child.absoluteFilePath());
        const QFileInfoList packages = nested.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for(const QFileInfo& package : packages)
            if(hasOfficialLabRecorderArchiveMarker(package.absoluteFilePath())) candidates.append(package.absoluteFilePath());
    }
}

bool peImageIsWindowsX64(const QString& path, QString& error)
{
#if !defined(_WIN32)
    Q_UNUSED(path);
    error = QStringLiteral("Автоматическая загрузка lsl.dll доступна только в Windows x64-клиенте.");
    return false;
#else
    if(sizeof(void*) != 8)
    {
        error = QStringLiteral("Для LSL требуется 64-разрядный клиент Windows.");
        return false;
    }
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly) || file.size() < 70 || file.size() > 64ll * 1024 * 1024)
    {
        error = QStringLiteral("lsl.dll недоступна или имеет недопустимый размер.");
        return false;
    }
    const QByteArray dos = file.read(64);
    if(dos.size() != 64 || dos.at(0) != 'M' || dos.at(1) != 'Z')
    {
        error = QStringLiteral("Файл не имеет корректного заголовка PE.");
        return false;
    }
    const quint32 pe_offset = qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(dos.constData() + 0x3c));
    if(pe_offset > static_cast<quint64>(file.size()) - 6 || !file.seek(pe_offset))
    {
        error = QStringLiteral("Заголовок PE выходит за границы файла.");
        return false;
    }
    const QByteArray pe = file.read(6);
    if(pe.size() != 6 || pe.at(0) != 'P' || pe.at(1) != 'E' || pe.at(2) != 0 || pe.at(3) != 0)
    {
        error = QStringLiteral("Файл не имеет корректной сигнатуры PE.");
        return false;
    }
    const quint16 machine = qFromLittleEndian<quint16>(reinterpret_cast<const uchar*>(pe.constData() + 4));
    if(machine != 0x8664)
    {
        error = QStringLiteral("Ожидалась Windows x64 DLL (AMD64); другая разрядность отклонена.");
        return false;
    }
    return true;
#endif
}

QString fileSha256(const QString& path)
{
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly)) return {};
    return QString::fromLatin1(QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex());
}

} // namespace

bool LslEegSource::validateWindowsX64Library(const QString& library_path, QString& sha256, QString& error)
{
    const QString canonical = QFileInfo(library_path).canonicalFilePath();
    if(canonical.isEmpty()) { error = QStringLiteral("Файл lsl.dll не найден."); return false; }
    if(!peImageIsWindowsX64(canonical, error)) return false;
    Api api(canonical);
    QStringList missing;
    if(!api.load(error, &missing)) return false;
    api.library.unload();
    sha256 = fileSha256(canonical);
    if(sha256.size() != 64) { error = QStringLiteral("Не удалось вычислить SHA-256 для lsl.dll."); return false; }
    error.clear();
    return true;
}

bool LslEegSource::discoverInstalledLabRecorderLibrary(const QString& application_directory, const QString& saved_library_path,
    QString& library_path, QString& provenance, QString& sha256, QString& error)
{
    library_path.clear();
    provenance.clear();
    sha256.clear();
    QStringList candidate_directories;
    if(!saved_library_path.isEmpty()) candidate_directories.append(QFileInfo(saved_library_path).absolutePath());

    QString current = QDir(application_directory).absolutePath();
    for(int depth = 0; depth < 5 && !current.isEmpty(); ++depth)
    {
        appendLabRecorderCandidates(current, candidate_directories);
        const QString parent = QFileInfo(current).absolutePath();
        if(parent == current) break;
        current = parent;
    }
    const QString program_files = qEnvironmentVariable("ProgramW6432", qEnvironmentVariable("ProgramFiles"));
    const QString local_programs = QDir(qEnvironmentVariable("LOCALAPPDATA")).filePath(QStringLiteral("Programs"));
    for(const QString& root : {program_files, qEnvironmentVariable("ProgramFiles(x86)"), local_programs})
        if(!root.isEmpty()) appendLabRecorderCandidates(QDir(root).absolutePath(), candidate_directories);

    candidate_directories.removeDuplicates();
    struct ValidCandidate { QString path; QString sha; QDateTime modified; };
    QVector<ValidCandidate> valid;
    for(const QString& directory : candidate_directories)
    {
        if(!hasOfficialLabRecorderArchiveMarker(directory)) continue;
        const QString path = QFileInfo(QDir(directory).filePath(QStringLiteral("lsl.dll"))).canonicalFilePath();
        QString candidate_sha, candidate_error;
        if(path.isEmpty() || !validateWindowsX64Library(path, candidate_sha, candidate_error)) continue;
        const QFileInfo exe(QDir(directory).filePath(QStringLiteral("LabRecorder.exe")));
        valid.push_back({path, candidate_sha, exe.lastModified().toUTC()});
    }
    if(valid.isEmpty())
    {
        error = QStringLiteral("Совместимая lsl.dll не найдена в подтверждаемом пакете LabRecorder. Ожидаются рядом LabRecorder.exe, lsl.dll и README с официальным адресом проекта.");
        return false;
    }
    const QString saved_canonical = QFileInfo(saved_library_path).canonicalFilePath();
    for(const auto& candidate : valid)
        if(!saved_canonical.isEmpty() && candidate.path.compare(saved_canonical, Qt::CaseInsensitive) == 0)
        {
            library_path = candidate.path;
            sha256 = candidate.sha;
            break;
        }
    if(library_path.isEmpty())
    {
        std::sort(valid.begin(), valid.end(), [](const ValidCandidate& a, const ValidCandidate& b) { return a.modified > b.modified; });
        library_path = valid.front().path;
        sha256 = valid.front().sha;
    }
    provenance = QStringLiteral("LabRecorder release archive marker: adjacent LabRecorder.exe and README with the official project URL; DLL validated as x64 and loaded to check the C API.");
    error.clear();
    return true;
}

struct LslEegSource::Impl {
    Impl(QString path, LslStreamDescriptor selected, QString units) : api(path), selection(std::move(selected)), units_override(std::move(units)) {}
    Api api;
    LslStreamDescriptor selection;
    LslStreamDescriptor active;
    QString units_override;
    std::vector<std::string> channel_names;
    std::vector<double> factors;
    LslTimestampDiagnostics diagnostics;
    void* inlet = nullptr;
    double previous_source = std::numeric_limits<double>::quiet_NaN();
    double previous_arrival = std::numeric_limits<double>::quiet_NaN();
    double correction_checked_at = -1e30;
};

bool LslEegSource::discover(const QString& library_path, QVector<LslStreamDescriptor>& streams, QString& error)
{
    streams.clear();
    if(library_path.isEmpty()) { error = QStringLiteral("Сначала вызовите neuro.configure_lsl для поиска и проверки установленной lsl.dll."); return false; }
    Api api(library_path);
    if(!api.load(error)) return false;
    void* found[128] = {};
    const int count = api.resolve_all(found, 128, 0.5);
    for(int i = 0; i < count && i < 128; ++i)
    {
        if(!found[i]) continue;
        LslStreamDescriptor d = api.describe(found[i]);
        api.destroy_info(found[i]);
        if(d.channel_count > 0 && d.channel_count <= 256 && d.format != QLatin1String("string")) streams.push_back(std::move(d));
    }
    return true;
}

LslEegSource::LslEegSource(QString library_path, LslStreamDescriptor selection, QString units_override)
    : impl_(new Impl(std::move(library_path), std::move(selection), std::move(units_override))) {}
LslEegSource::~LslEegSource() { stop(); }
int LslEegSource::sampleRateHz() const { return static_cast<int>(std::round(impl_->active.nominal_hz)); }
const std::vector<std::string>& LslEegSource::channels() const { return impl_->channel_names; }
const LslStreamDescriptor& LslEegSource::descriptor() const { return impl_->active; }
const LslTimestampDiagnostics& LslEegSource::diagnostics() const { return impl_->diagnostics; }

bool LslEegSource::start(std::string& error)
{
    stop();
    QString message;
    if(!impl_->api.load(message)) { error = message.toStdString(); return false; }
    void* found[128] = {};
    const int count = impl_->api.resolve_all(found, 128, 0.5);
    void* selected = nullptr;
    for(int i = 0; i < count && i < 128; ++i)
    {
        if(!found[i]) continue;
        const LslStreamDescriptor d = impl_->api.describe(found[i]);
        if(!selected && sameStream(impl_->selection, d)) selected = found[i];
        else impl_->api.destroy_info(found[i]);
    }
    if(!selected) { error = "Выбранный LSL-поток не найден."; return false; }
    impl_->inlet = impl_->api.create_inlet(selected, 30, 0, 1);
    impl_->api.destroy_info(selected);
    if(!impl_->inlet) { error = "Не удалось создать LSL inlet."; return false; }
    int ec = 0;
    impl_->api.open_stream(impl_->inlet, 1.0, &ec);
    if(ec != 0) { error = "Не удалось открыть LSL-поток (код " + std::to_string(ec) + ")."; stop(); return false; }
    void* full = impl_->api.full_info(impl_->inlet, 1.0, &ec);
    if(ec != 0 || !full) { error = "Не удалось получить полные метаданные LSL (код " + std::to_string(ec) + ")."; stop(); return false; }
    impl_->active = impl_->api.describe(full);
    impl_->api.destroy_info(full);
    impl_->active.native_float32_exact_available = impl_->active.format == QLatin1String("float32") && impl_->api.pull_chunk_float != nullptr;
    if(impl_->active.channel_count < 1 || impl_->active.channel_count > 256 ||
       impl_->active.format == QLatin1String("string") || impl_->active.format == QLatin1String("unknown"))
    { error = "Неподдерживаемый формат или число каналов LSL."; stop(); return false; }
    impl_->factors.clear();
    impl_->channel_names.clear();
    for(int i = 0; i < impl_->active.channel_count; ++i)
    {
        const QString unit = impl_->units_override.isEmpty() ? impl_->active.units.value(i) : impl_->units_override;
        const double factor = unitFactor(unit);
        if(factor == 0.0)
        {
            error = QStringLiteral("Неизвестные единицы канала %1: '%2'. Выберите явное переопределение единиц.")
                .arg(i + 1).arg(unit).toStdString();
            stop(); return false;
        }
        impl_->factors.push_back(factor);
        const QString label = impl_->active.channels.value(i).isEmpty() ? QStringLiteral("Канал %1").arg(i + 1) : impl_->active.channels.at(i);
        impl_->channel_names.push_back(label.toUtf8().toStdString());
    }
    impl_->diagnostics = {};
    impl_->previous_source = std::numeric_limits<double>::quiet_NaN();
    impl_->previous_arrival = std::numeric_limits<double>::quiet_NaN();
    impl_->correction_checked_at = -1e30;
    return true;
}

void LslEegSource::stop()
{
    buffered_.clear();
    if(impl_->inlet)
    {
        impl_->api.close_stream(impl_->inlet);
        impl_->api.destroy_inlet(impl_->inlet);
        impl_->inlet = nullptr;
    }
}

std::vector<EegFrame> LslEegSource::readChunk(std::string& error)
{
    std::vector<EegFrame> frames;
    if(!impl_->inlet) { error = "LSL-поток не подключён."; return frames; }
    const int channels = impl_->active.channel_count;
    const bool native_float32 = impl_->active.format == QLatin1String("float32") && impl_->api.pull_chunk_float;
    std::vector<double> values(native_float32 ? 0 : static_cast<std::size_t>(64 * channels));
    std::vector<float> float_values(native_float32 ? static_cast<std::size_t>(64 * channels) : 0);
    double timestamps[64] = {};
    int ec = 0;
    const unsigned int elements = native_float32 ?
        impl_->api.pull_chunk_float(impl_->inlet, float_values.data(), timestamps,
            static_cast<unsigned int>(float_values.size()), 64, 0.1, &ec) :
        impl_->api.pull_chunk(impl_->inlet, values.data(), timestamps,
            static_cast<unsigned int>(values.size()), 64, 0.1, &ec);
    const double arrival = impl_->api.local_clock();
    if(ec != 0) { error = "LSL чтение завершилось с кодом " + std::to_string(ec); return frames; }
    if(elements == 0) return frames;
    const std::size_t capacity = native_float32 ? float_values.size() : values.size();
    if(elements > capacity || elements % static_cast<unsigned int>(channels) != 0)
    { error = "LSL вернул некорректный размер порции."; return frames; }
    const std::size_t count = elements / static_cast<unsigned int>(channels);
    if(arrival - impl_->correction_checked_at >= 5.0)
    {
        impl_->correction_checked_at = arrival;
        int correction_error = 0;
        const double correction = impl_->api.time_correction(impl_->inlet, 0.02, &correction_error);
        if(correction_error == 0 && std::isfinite(correction))
        {
            impl_->diagnostics.last_correction_seconds = correction;
            impl_->diagnostics.correction_available = true;
        }
    }
    const double interval = std::isfinite(impl_->previous_arrival) && arrival > impl_->previous_arrival ?
        (arrival - impl_->previous_arrival) / static_cast<double>(count) :
        1.0 / std::max(1.0, impl_->active.nominal_hz);
    frames.reserve(count);
    for(std::size_t row = 0; row < count; ++row)
    {
        EegFrame frame;
        frame.source_timestamp_seconds = timestamps[row];
        frame.arrival_monotonic_seconds = arrival;
        frame.timestamp_seconds = arrival;
        frame.display_timestamp_seconds = arrival - static_cast<double>(count - 1 - row) * interval;
        frame.signal_good = true;
        frame.volts.reserve(static_cast<std::size_t>(channels));
        if(native_float32)
        {
            frame.raw_source_values.reserve(static_cast<std::size_t>(channels));
            frame.raw_source_values_exact = true;
        }
        for(int channel = 0; channel < channels; ++channel)
        {
            const std::size_t value_index = row * static_cast<std::size_t>(channels) + static_cast<std::size_t>(channel);
            const double raw_value = native_float32 ? static_cast<double>(float_values[value_index]) : values[value_index];
            if(native_float32) frame.raw_source_values.push_back(float_values[value_index]);
            const double volts = raw_value * impl_->factors[static_cast<std::size_t>(channel)];
            const bool valid = std::isfinite(volts) && std::abs(volts) <= std::numeric_limits<float>::max();
            if(!valid) frame.signal_good = false;
            frame.volts.push_back(valid ? static_cast<float>(volts) : 0.0f);
        }
        auto& d = impl_->diagnostics;
        ++d.received;
        const double source = frame.source_timestamp_seconds;
        if(!std::isfinite(source)) ++d.nonfinite;
        else
        {
            if(source == std::floor(source)) ++d.integer_valued;
            if(std::isfinite(impl_->previous_source))
            {
                const double delta = source - impl_->previous_source;
                if(delta == 0.0) ++d.repeated;
                else if(delta < 0.0) ++d.backwards;
                else if(impl_->active.nominal_hz > 0 && delta > 2.0 / impl_->active.nominal_hz) ++d.coarse_intervals;
            }
            impl_->previous_source = source;
            d.clock_compatible = d.correction_available &&
                std::abs(source + d.last_correction_seconds - arrival) < 60.0;
        }
        d.last_source_seconds = source;
        d.last_arrival_seconds = arrival;
        frames.push_back(std::move(frame));
    }
    impl_->previous_arrival = arrival;
    return frames;
}

bool LslEegSource::read(EegFrame& frame, std::string& error)
{
    if(buffered_.empty())
    {
        auto frames = readChunk(error);
        for(auto& value : frames) buffered_.push_back(std::move(value));
    }
    if(buffered_.empty()) return false;
    frame = std::move(buffered_.front());
    buffered_.pop_front();
    return true;
}

} // namespace neurocontrol
