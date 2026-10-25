#pragma once

#include "EegSource.h"
#include <QtCore/QByteArray>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QVector>
#include <cstdint>
#include <deque>
#include <memory>

namespace neurocontrol {

struct LslStreamDescriptor {
    QString uid;
    QString source_id;
    QString name;
    QString type;
    QString hostname;
    QString format;
    double nominal_hz = 0.0;
    int channel_count = 0;
    QStringList channels;
    QStringList channel_types;
    QStringList units;
    QByteArray raw_xml;
    bool native_float32_exact_available = false;
};

struct LslTimestampDiagnostics {
    std::uint64_t received = 0;
    std::uint64_t repeated = 0;
    std::uint64_t backwards = 0;
    std::uint64_t nonfinite = 0;
    std::uint64_t coarse_intervals = 0;
    std::uint64_t integer_valued = 0;
    bool correction_available = false;
    bool clock_compatible = false;
    double last_source_seconds = 0.0;
    double last_arrival_seconds = 0.0;
    double last_correction_seconds = 0.0;
};

// Qt-only native LSL adapter. Calls that can wait for discovery or data must
// run on a worker thread. No classifier or avatar input is invoked here.
class LslEegSource final : public EegSource {
public:
    static bool discoverInstalledLabRecorderLibrary(const QString& application_directory, const QString& saved_library_path,
        QString& library_path, QString& provenance, QString& sha256, QString& error);
    static bool validateWindowsX64Library(const QString& library_path, QString& sha256, QString& error);
    static bool discover(const QString& library_path, QVector<LslStreamDescriptor>& streams, QString& error);
    static double unitScaleToVolts(const QString& unit);
    LslEegSource(QString library_path, LslStreamDescriptor selection, QString units_override = {});
    ~LslEegSource() override;
    int sampleRateHz() const override;
    const std::vector<std::string>& channels() const override;
    bool start(std::string& error) override;
    void stop() override;
    bool read(EegFrame& frame, std::string& error) override;
    std::vector<EegFrame> readChunk(std::string& error);
    const LslStreamDescriptor& descriptor() const;
    const LslTimestampDiagnostics& diagnostics() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::deque<EegFrame> buffered_;
};

} // namespace neurocontrol
