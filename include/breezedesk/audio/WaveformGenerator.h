#pragma once

#include <QString>
#include <QVector>

#include <atomic>

namespace BreezeDesk {

struct WaveformLevel {
    quint32 samplesPerPeak = 0;
    QVector<qint16> minimums;
    QVector<qint16> maximums;
};

class WaveformGenerator final {
  public:
    // A peak pair is serialized as two qint16 values (four bytes). Keeping the
    // aggregate payload capped at 64 MiB also gives readers a deterministic memory
    // bound before they reserve any vectors.
    static constexpr quint64 MaximumSerializedPeakPairs =
        (64ULL * 1024ULL * 1024ULL) / (sizeof(qint16) * 2ULL);

    [[nodiscard]] static bool generate(const QString& pcm16Path, const QString& waveformPath,
                                       std::atomic_bool* cancelled, QString* error = nullptr);
    [[nodiscard]] static QVector<WaveformLevel> read(const QString& waveformPath, QString* error = nullptr);
};

} // namespace BreezeDesk
