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
    [[nodiscard]] static bool generate(const QString& pcm16Path, const QString& waveformPath,
                                       std::atomic_bool* cancelled, QString* error = nullptr);
    [[nodiscard]] static QVector<WaveformLevel> read(const QString& waveformPath, QString* error = nullptr);
};

} // namespace BreezeDesk
