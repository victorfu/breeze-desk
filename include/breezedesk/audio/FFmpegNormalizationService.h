#pragma once

#include "breezedesk/audio/IAudioNormalizer.h"

#include <QString>

namespace BreezeDesk {

class FFmpegNormalizationService final : public QObject, public IAudioNormalizer {
    Q_OBJECT

  public:
    explicit FFmpegNormalizationService(QString ffmpegPath, QObject* parent = nullptr);
    NormalizationOperation* normalize(const QString& sourcePath, const QString& outputPath, qint64 durationMs,
                                      QObject* parent = nullptr) override;

  private:
    QString m_ffmpegPath;
};

} // namespace BreezeDesk
